/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "engine.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/face.hpp>
#include <opencv2/videoio.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <pwd.h>
#include <unistd.h>
#include <chrono>
#include <cmath>
#include <future>
#include <mutex>
#include <optional>

namespace FaceUnlock
{
namespace
{
using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Networks {
    cv::Ptr<cv::FaceDetectorYN> detector;
    cv::Ptr<cv::FaceRecognizerSF> recognizer;
    float detectionThreshold = 0;
    double loadMs = 0; // 0 when they came from the cache
};

// Loaded once per process: the service (kenning daemon) runs many scans and
// keeps them, a one-off command loads them once anyway.
Networks networks(float detectionThreshold)
{
    static std::mutex mutex;
    static std::optional<Networks> cache;
    const std::lock_guard lock(mutex);
    if (cache && cache->detectionThreshold == detectionThreshold) {
        Networks cached = *cache;
        cached.loadMs = 0;
        return cached;
    }
    const auto start = Clock::now();
    Networks loaded;
    loaded.detector = cv::FaceDetectorYN::create(detectorModel, "", cv::Size(320, 320), detectionThreshold, 0.3f, 50);
    loaded.recognizer = cv::FaceRecognizerSF::create(recognizerModel, "");
    loaded.detectionThreshold = detectionThreshold;
    loaded.loadMs = millisecondsSince(start);
    cache = loaded;
    return loaded;
}

// The camera and both networks, set up for one scan.
struct Pipeline {
    cv::VideoCapture camera;
    cv::Ptr<cv::FaceDetectorYN> detector;
    cv::Ptr<cv::FaceRecognizerSF> recognizer;
    int maxHeight = 320;
    double darkThreshold = 60;
    int rotate = 0;
    // Ordinary webcams: keep colour, which the networks were trained on. Converting
    // to greyscale (with or without contrast normalisation) matched worse in tests.
    bool colour = false;
    double cameraMs = 0;
    double networksMs = 0;

    explicit Pipeline(const Config &config)
    {
        // Lower finds faces in poor light; every face still has to match the user's.
        const float detectionThreshold = float(config.number("video/detection_threshold", 0.8));
        // Opening the camera wakes it from USB suspend (~250 ms); load the networks meanwhile.
        auto loading = std::async(std::launch::async, networks, detectionThreshold);

        const std::string device = config.string("video/device_path");
        const auto start = Clock::now();
        const bool opened = !device.empty() && device != "none" && camera.open(device, cv::CAP_V4L2);
        cameraMs = millisecondsSince(start);
        const Networks loaded = loading.get();
        if (!opened) {
            return;
        }
        detector = loaded.detector;
        recognizer = loaded.recognizer;
        networksMs = loaded.loadMs;
        maxHeight = int(config.number("video/max_height", 320));
        darkThreshold = config.number("video/dark_threshold", 60);
        rotate = int(config.number("video/rotate", 0));
        colour = config.boolean("video/colour", false);
    }

    bool ready() const
    {
        return camera.isOpened() && detector && recognizer;
    }

    enum class Frame { Missing, Black, Dark, Usable };

    // Reads a frame; on Usable, image holds it as BGR scaled to maxHeight.
    Frame read(cv::Mat &image)
    {
        cv::Mat frame;
        if (!camera.read(frame) || frame.empty()) {
            return Frame::Missing;
        }
        cv::Mat grey;
        if (frame.channels() == 1) {
            grey = frame;
        } else {
            cv::cvtColor(frame, grey, cv::COLOR_BGR2GRAY);
        }

        // Same test as Howdy: share of pixels in the darkest of 8 histogram bins.
        const int histSize = 8;
        const float range[] = {0, 256};
        const float *ranges[] = {range};
        cv::Mat hist;
        cv::calcHist(&grey, 1, nullptr, cv::Mat(), hist, 1, &histSize, ranges);
        const double total = cv::sum(hist)[0];
        const double darkness = total > 0 ? hist.at<float>(0) / total * 100 : 100;
        if (darkness >= 100) {
            return Frame::Black;
        }
        // Washed out (sunlight, a bright lamp) counts as too dark: either way no usable face.
        const double brightness = total > 0 ? hist.at<float>(histSize - 1) / total * 100 : 0;
        if (darkness > darkThreshold || brightness > 50) {
            return Frame::Dark;
        }

        if (colour && frame.channels() == 3) {
            image = frame;
        } else {
            cv::cvtColor(grey, image, cv::COLOR_GRAY2BGR);
        }
        if (image.rows > maxHeight) {
            cv::resize(image, image, cv::Size(image.cols * maxHeight / image.rows, maxHeight));
        }
        return Frame::Usable;
    }

    // The frame to search, plus turned copies when video/rotate asks for them
    // (as Howdy: 0 upright only, 1 upright and portrait, 2 portrait only).
    std::vector<cv::Mat> orientations(const cv::Mat &image) const
    {
        std::vector<cv::Mat> images;
        if (rotate != 2) {
            images.push_back(image);
        }
        if (rotate == 1 || rotate == 2) {
            for (const auto code : {cv::ROTATE_90_CLOCKWISE, cv::ROTATE_90_COUNTERCLOCKWISE}) {
                cv::Mat turned;
                cv::rotate(image, turned, code);
                images.push_back(turned);
            }
        }
        return images;
    }

    cv::Mat faces(const cv::Mat &image)
    {
        cv::Mat found;
        detector->setInputSize(image.size());
        detector->detect(image, found);
        return found;
    }

    cv::Mat encode(const cv::Mat &image, const cv::Mat &face)
    {
        cv::Mat aligned;
        cv::Mat feature;
        recognizer->alignCrop(image, face, aligned);
        recognizer->feature(aligned, feature);
        return feature.clone();
    }
};
}

void giveToService(const std::string &path)
{
    // Only root can hand files over; a test build run as a user keeps its own.
    if (geteuid() != 0) {
        return;
    }
    if (const passwd *account = getpwnam(serviceUser)) {
        chown(path.c_str(), account->pw_uid, account->pw_gid);
        chown(modelsDir, account->pw_uid, account->pw_gid);
    }
}

std::vector<FaceModel> loadModels(const std::string &user)
{
    std::vector<FaceModel> models;
    QFile file(QString::fromStdString(modelPath(user)));
    if (!file.open(QIODevice::ReadOnly)) {
        return models;
    }
    for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject object = value.toObject();
        FaceModel model;
        model.id = object.value(QStringLiteral("id")).toInt();
        model.label = object.value(QStringLiteral("label")).toString().toStdString();
        model.time = object.value(QStringLiteral("time")).toInteger();
        for (const auto &encoding : object.value(QStringLiteral("data")).toArray()) {
            const QJsonArray numbers = encoding.toArray();
            cv::Mat feature(1, int(numbers.size()), CV_32F);
            for (int i = 0; i < numbers.size(); ++i) {
                feature.at<float>(i) = float(numbers.at(i).toDouble());
            }
            model.encodings.push_back(feature);
        }
        models.push_back(model);
    }
    return models;
}

bool saveModels(const std::string &user, const std::vector<FaceModel> &models)
{
    QJsonArray array;
    for (const FaceModel &model : models) {
        QJsonArray data;
        for (const cv::Mat &feature : model.encodings) {
            QJsonArray numbers;
            for (int i = 0; i < feature.cols; ++i) {
                numbers.append(double(feature.at<float>(i)));
            }
            data.append(numbers);
        }
        array.append(QJsonObject{
            {QStringLiteral("id"), model.id},
            {QStringLiteral("label"), QString::fromStdString(model.label)},
            {QStringLiteral("time"), model.time},
            {QStringLiteral("data"), data},
        });
    }
    const QString path = QString::fromStdString(modelPath(user));
    if (QDir().mkpath(QString::fromUtf8(modelsDir))) {
        QFile::setPermissions(QString::fromUtf8(modelsDir), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    // Only the service reads face models; everyone else asks it (service.h). Set on
    // the path: after commit() the QSaveFile no longer refers to the saved file.
    if (!file.commit() || !QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return false;
    }
    giveToService(path.toStdString());
    return true;
}

double matchScore(double cosine)
{
    return (1.0 - cosine) * 10.0;
}

ScanResult compare(const Config &config, const std::string &user, const std::function<bool()> &cancelled)
{
    const std::vector<FaceModel> models = loadModels(user);
    const bool hasEncodings = std::any_of(models.begin(), models.end(), [](const FaceModel &model) {
        return !model.encodings.empty();
    });
    if (!hasEncodings) {
        return {NoModel, 99, "Failure, no face model known"};
    }

    const auto scanStart = Clock::now();
    Pipeline pipeline(config);
    if (!pipeline.ready()) {
        return {Error, 99, "Failure, not possible to open camera at configured path"};
    }

    ScanStats stats;
    stats.cameraMs = pipeline.cameraMs;
    stats.networksMs = pipeline.networksMs;
    const double certainty = config.number("video/certainty", 6.0);
    const auto deadline = Clock::now() + std::chrono::duration<double>(config.number("video/timeout", 4));
    double best = 99;

    while (Clock::now() < deadline) {
        if (cancelled && cancelled()) {
            return {Aborted, best, "Failure, cancelled", stats};
        }
        cv::Mat image;
        const auto frame = pipeline.read(image);
        if (frame == Pipeline::Frame::Missing) {
            continue;
        }
        if (++stats.frames == 1) {
            stats.firstFrameMs = millisecondsSince(scanStart);
        }
        if (frame != Pipeline::Frame::Usable) {
            ++stats.darkFrames;
            continue;
        }
        bool sawFace = false;
        for (const cv::Mat &view : pipeline.orientations(image)) {
            auto start = Clock::now();
            const cv::Mat faces = pipeline.faces(view);
            stats.detectMs += millisecondsSince(start);
            for (int i = 0; i < faces.rows; ++i) {
                sawFace = true;
                // Column 14 of a YuNet row is the detection confidence.
                stats.bestDetection = std::max(stats.bestDetection, double(faces.at<float>(i, 14)));
                start = Clock::now();
                const cv::Mat feature = pipeline.encode(view, faces.row(i));
                stats.encodeMs += millisecondsSince(start);
                for (const FaceModel &model : models) {
                    for (const cv::Mat &known : model.encodings) {
                        best = std::min(best, matchScore(pipeline.recognizer->match(feature, known, cv::FaceRecognizerSF::FR_COSINE)));
                    }
                }
                if (best <= certainty) {
                    stats.faceFrames += 1;
                    return {Recognized, best, "Identified face as " + user, stats};
                }
            }
        }
        stats.faceFrames += sawFace ? 1 : 0;
    }

    if (stats.frames > 0 && stats.darkFrames == stats.frames) {
        return {TooDark, best, "Failure, image too dark", stats};
    }
    return {Timeout, best, "Failure, timeout reached", stats};
}

namespace
{
// Head pose from YuNet's landmarks (columns 4-13: eyes, nose tip, mouth corners),
// only meaningful relative to the same person looking straight ahead.
struct Pose {
    double yaw = 0;
    double pitch = 0;
};

Pose headPose(const cv::Mat &face)
{
    const auto at = [&face](int column) {
        return double(face.at<float>(0, column));
    };
    const double eyesX = (at(4) + at(6)) / 2;
    const double eyesY = (at(5) + at(7)) / 2;
    const double eyeDistance = std::max(1.0, std::hypot(at(6) - at(4), at(7) - at(5)));
    const double mouthY = (at(11) + at(13)) / 2;
    return {(at(8) - eyesX) / eyeDistance, (at(9) - eyesY) / std::max(1.0, mouthY - eyesY)};
}

// The camera may be mirrored, so the first turn on an axis sets the direction and
// the second must go the other way.
struct EnrollStep {
    const char *instruction;
    double Pose::*axis; // nullptr: looking straight
    double minimum; // turn needed, from looking straight
    double maximum; // further than this aligns faces badly
    bool opposite; // the other way from the previous step
    double steady = 0; // drift allowed on the other axis
    bool back = false; // back to looking straight, nothing captured
};
}

ScanResult enroll(const Config &config, std::vector<cv::Mat> &encodings, const std::function<void(const std::string &)> &progress)
{
    Pipeline pipeline(config);
    if (!pipeline.ready()) {
        return {Error, 99, "Failure, not possible to open camera at configured path"};
    }

    // Only looking straight is required; a turn not seen in time is skipped.
    const EnrollStep back{"Please look back at the camera", nullptr, 0, 0, false, 0, true};
    const EnrollStep steps[] = {
        {"Please look straight into the camera", nullptr, 0, 0, false},
        {"Please turn your head slightly to the left", &Pose::yaw, 0.12, 0.45, false, 0.06},
        back,
        {"Please turn your head slightly to the right", &Pose::yaw, 0.12, 0.45, true, 0.06},
        back,
        {"Please tilt your head slightly up", &Pose::pitch, 0.07, 0.3, false, 0.1},
        back,
        {"Please tilt your head slightly down", &Pose::pitch, 0.07, 0.3, true, 0.1},
    };
    // Black frames while the camera warms up don't count.
    constexpr int maxFrames = 60;
    int litFrames = 0;
    int darkFrames = 0;
    Pose straight;
    double direction = 0; // sign of the previous turn on the same axis

    for (const EnrollStep &step : steps) {
        const bool looking = step.axis == nullptr && !step.back;
        if (!looking && encodings.empty()) {
            break;
        }
        progress(step.instruction);
        const int wanted = looking ? 3 : 2;
        const auto deadline = Clock::now() + std::chrono::seconds(looking ? 6 : step.back ? 4 : 5);
        int captured = 0;
        int hits = 0;
        bool hinted = false;
        Pose sum;
        while (captured < wanted && Clock::now() < deadline && (!looking || litFrames < maxFrames)) {
            cv::Mat image;
            const auto frame = pipeline.read(image);
            if (frame == Pipeline::Frame::Missing || frame == Pipeline::Frame::Black) {
                continue;
            }
            ++litFrames;
            if (frame == Pipeline::Frame::Dark) {
                ++darkFrames;
                continue;
            }
            for (const cv::Mat &view : pipeline.orientations(image)) {
                const cv::Mat faces = pipeline.faces(view);
                if (faces.rows > 1) {
                    encodings.clear();
                    return {Aborted, 99, "Multiple faces detected, aborting"};
                }
                if (faces.rows == 0) {
                    continue;
                }
                const Pose pose = headPose(faces.row(0));
                if (step.back) {
                    if (std::abs(pose.yaw - straight.yaw) < 0.08 && std::abs(pose.pitch - straight.pitch) < 0.05) {
                        ++captured;
                    }
                    break;
                }
                if (!looking) {
                    const double turn = pose.*step.axis - straight.*step.axis;
                    const double drift = step.axis == &Pose::yaw ? pose.pitch - straight.pitch : pose.yaw - straight.yaw;
                    if (std::abs(turn) >= step.minimum / 2 && std::abs(drift) > step.steady) {
                        if (!hinted) {
                            progress(step.axis == &Pose::yaw ? "Hint: keep your head level" : "Hint: keep your face toward the screen");
                            hinted = true;
                        }
                        break;
                    }
                    if (std::abs(turn) < step.minimum || std::abs(turn) > step.maximum
                        || (step.opposite && direction != 0 && std::signbit(turn) == std::signbit(direction))) {
                        break;
                    }
                    if (!step.opposite) {
                        direction = turn;
                    }
                }
                // Every few hits, so the encodings cover a little movement.
                if (++hits % (looking ? 3 : 2) == 1) {
                    encodings.push_back(pipeline.encode(view, faces.row(0)));
                    ++captured;
                    sum.yaw += pose.yaw;
                    sum.pitch += pose.pitch;
                }
                break;
            }
        }
        if (looking && captured > 0) {
            straight = {sum.yaw / captured, sum.pitch / captured};
        } else if (!looking && !step.back && captured == 0) {
            progress("Skipped, that turn wasn't seen");
            if (!step.opposite) {
                direction = 0;
            }
        }
    }

    if (!encodings.empty()) {
        progress("Scan complete");
        return {Recognized, 0, "Scan complete"};
    }
    if (litFrames == 0) {
        return {Aborted, 99, pipeline.colour ? "Camera saw only black frames - is it covered or switched off?"
                                             : "Camera saw only black frames - is IR emitter working?"};
    }
    if (darkFrames == litFrames) {
        return {TooDark, 99, "All frames were too dark or too bright, please check dark_threshold in config"};
    }
    return {Aborted, 99, "No face detected, aborting"};
}
}
