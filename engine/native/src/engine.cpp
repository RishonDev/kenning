/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "engine.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/face.hpp>
#include <opencv2/videoio.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <chrono>

namespace FaceUnlock
{
namespace
{
using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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
        const std::string device = config.string("video/device_path");
        auto start = Clock::now();
        if (device.empty() || device == "none" || !camera.open(device, cv::CAP_V4L2)) {
            return;
        }
        cameraMs = millisecondsSince(start);
        maxHeight = int(config.number("video/max_height", 320));
        darkThreshold = config.number("video/dark_threshold", 60);
        rotate = int(config.number("video/rotate", 0));
        colour = config.boolean("video/colour", false);
        start = Clock::now();
        // Lower finds faces in poor light; every face still has to match the user's.
        const float detectionThreshold = float(config.number("video/detection_threshold", 0.8));
        detector = cv::FaceDetectorYN::create(detectorModel, "", cv::Size(320, 320), detectionThreshold, 0.3f, 50);
        recognizer = cv::FaceRecognizerSF::create(recognizerModel, "");
        networksMs = millisecondsSince(start);
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
        if (darkness > darkThreshold) {
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
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    // Only root reads face models; the lock screen asks the service (service.h). Set on
    // the path: after commit() the QSaveFile no longer refers to the saved file.
    return file.commit() && QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

double matchScore(double cosine)
{
    return (1.0 - cosine) * 10.0;
}

ScanResult compare(const Config &config, const std::string &user)
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

ScanResult enroll(const Config &config, std::vector<cv::Mat> &encodings, const std::function<void(const std::string &)> &progress)
{
    Pipeline pipeline(config);
    if (!pipeline.ready()) {
        return {Error, 99, "Failure, not possible to open camera at configured path"};
    }
    progress("Please look straight into the camera");

    // Like Howdy: up to 60 frames. Keep a few encodings spread over the capture so
    // small changes in pose are covered. Black frames while a camera warms up (or
    // fades in, as some webcams do) don't count, within a time limit.
    constexpr int maxFrames = 60;
    constexpr int wanted = 5;
    const auto deadline = Clock::now() + std::chrono::seconds(6);
    int litFrames = 0;
    int darkFrames = 0;
    int hits = 0;
    while (litFrames < maxFrames && int(encodings.size()) < wanted && Clock::now() < deadline) {
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
                return {Aborted, 99, "Multiple faces detected, aborting"};
            }
            if (faces.rows == 1) {
                // Every few hits, so the encodings cover a little movement.
                if (++hits % 3 == 1) {
                    encodings.push_back(pipeline.encode(view, faces.row(0)));
                }
                break;
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
        return {TooDark, 99, "All frames were too dark, please check dark_threshold in config"};
    }
    return {Aborted, 99, "No face detected, aborting"};
}
}
