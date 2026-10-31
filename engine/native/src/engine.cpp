/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "engine.h"
#include "headpose.h"
#include "pipeline.h"

#include <algorithm>
#include <optional>

namespace FaceUnlock
{
namespace
{
const char *const cameraFailure = "Failure, not possible to open camera at configured path";

bool hasEncodings(const std::vector<FaceModel> &models)
{
    return std::any_of(models.cbegin(), models.cend(), [](const FaceModel &model) {
        return !model.encodings.empty();
    });
}

double bestScoreAgainst(const Pipeline &pipeline, const cv::Mat &feature, const std::vector<FaceModel> &models)
{
    double best = noMatchScore;
    for (const FaceModel &model : models) {
        for (const cv::Mat &known : model.encodings) {
            best = std::min(best, matchScore(pipeline.cosineSimilarity(feature, known)));
        }
    }
    return best;
}
}

double matchScore(double cosine)
{
    return (1.0 - cosine) * 10.0;
}

ScanResult compare(const Config &config, const std::string &user, const std::function<bool()> &cancelled)
{
    const std::vector<FaceModel> models = loadModels(user);
    if (!hasEncodings(models)) {
        return {.code = NoModel, .message = "Failure, no face model known"};
    }

    const auto scanStart = Clock::now();
    Pipeline pipeline(config);
    if (!pipeline.ready()) {
        return {.code = Error, .message = cameraFailure};
    }

    ScanStats stats{.cameraMs = pipeline.cameraMs(), .networksMs = pipeline.networksMs()};
    const double certainty = config.number("video/certainty", 6.0);
    const auto deadline = Clock::now() + std::chrono::duration<double>(config.number("video/timeout", 4));
    const bool keepSnapshot = config.boolean("snapshots/save_failed", false);
    double best = noMatchScore;
    cv::Mat snapshot;

    while (Clock::now() < deadline) {
        if (cancelled && cancelled()) {
            return {.code = Aborted, .bestScore = best, .message = "Failure, cancelled", .stats = stats};
        }
        cv::Mat image;
        const Pipeline::Frame frame = pipeline.read(image);
        if (frame == Pipeline::Frame::Missing) {
            continue;
        }
        if (++stats.frames == 1) {
            stats.firstFrameMs = millisecondsSince(scanStart);
        }
        if (frame != Pipeline::Frame::Usable) {
            ++stats.unusableFrames;
            continue;
        }
        bool sawFace = false;
        for (const cv::Mat &view : pipeline.orientations(image)) {
            auto start = Clock::now();
            const cv::Mat faces = pipeline.detectFaces(view);
            stats.detectMs += millisecondsSince(start);
            for (int row = 0; row < faces.rows; ++row) {
                sawFace = true;
                stats.bestDetection = std::max(stats.bestDetection, double(faces.at<float>(row, YuNet::confidence)));
                start = Clock::now();
                const cv::Mat feature = pipeline.encode(view, faces.row(row));
                stats.encodeMs += millisecondsSince(start);
                const double score = bestScoreAgainst(pipeline, feature, models);
                if (keepSnapshot && (score < best || snapshot.empty())) {
                    snapshot = view.clone();
                }
                best = std::min(best, score);
                if (best <= certainty) {
                    ++stats.faceFrames;
                    return {.code = Recognized, .bestScore = best, .message = "Identified face as " + user, .stats = stats};
                }
            }
        }
        if (sawFace) {
            ++stats.faceFrames;
        }
    }

    if (stats.frames > 0 && stats.unusableFrames == stats.frames) {
        return {.code = TooDark, .bestScore = best, .message = "Failure, image too dark", .stats = stats};
    }
    // A photo only when someone was seen: a face that wasn't the user's.
    return {.code = Timeout, .bestScore = best, .message = "Failure, timeout reached", .stats = stats, .snapshot = stats.faceFrames > 0 ? snapshot : cv::Mat()};
}

namespace
{
enum class StepKind {
    LookStraight,
    Turn,
    LookBack
};

// The settings page matches these instructions to show its own text.
struct EnrolmentStep {
    StepKind kind;
    const char *instruction;
    double HeadPose::*axis = nullptr; // turns only
    TurnLimits limits = {};
    bool opposesPrevious = false; // the other way from the previous turn on this axis
};

constexpr TurnLimits yawLimits{.minimum = 0.12, .maximum = 0.45, .maxDrift = 0.06};
constexpr TurnLimits pitchLimits{.minimum = 0.07, .maximum = 0.3, .maxDrift = 0.1};
constexpr EnrolmentStep lookBack{.kind = StepKind::LookBack, .instruction = "Please look back at the camera"};

// Only looking straight is required; a turn not seen in time is skipped.
constexpr EnrolmentStep enrolmentSteps[] = {
    {.kind = StepKind::LookStraight, .instruction = "Please look straight into the camera"},
    {.kind = StepKind::Turn, .instruction = "Please turn your head slightly to the left", .axis = &HeadPose::yaw, .limits = yawLimits},
    lookBack,
    {.kind = StepKind::Turn,
     .instruction = "Please turn your head slightly to the right",
     .axis = &HeadPose::yaw,
     .limits = yawLimits,
     .opposesPrevious = true},
    lookBack,
    {.kind = StepKind::Turn, .instruction = "Please tilt your head slightly up", .axis = &HeadPose::pitch, .limits = pitchLimits},
    lookBack,
    {.kind = StepKind::Turn, .instruction = "Please tilt your head slightly down", .axis = &HeadPose::pitch, .limits = pitchLimits, .opposesPrevious = true},
};

struct StepBudget {
    int wanted; // encodings, or straight sightings when looking back
    std::chrono::seconds timeLimit;
    int encodeEvery; // sightings, so the encodings cover a little movement
};

StepBudget budgetFor(StepKind kind)
{
    switch (kind) {
    case StepKind::LookStraight:
        return {.wanted = 3, .timeLimit = std::chrono::seconds(6), .encodeEvery = 3};
    case StepKind::Turn:
        return {.wanted = 2, .timeLimit = std::chrono::seconds(5), .encodeEvery = 2};
    case StepKind::LookBack:
        break;
    }
    return {.wanted = 2, .timeLimit = std::chrono::seconds(4), .encodeEvery = 0};
}

// Lit frames allowed for looking straight; black frames while the camera warms up don't count.
constexpr int maxStraightFrames = 60;

struct StepProgress {
    int captured = 0;
    int sightings = 0;
    bool hinted = false;
    HeadPose poseSum;
};

class Enrolment
{
public:
    Enrolment(Pipeline &pipeline, std::vector<cv::Mat> &encodings, const ProgressCallback &progress)
        : m_pipeline(pipeline)
        , m_encodings(encodings)
        , m_progress(progress)
    {
    }

    // A result when enrolment has to stop early.
    std::optional<ScanResult> runStep(const EnrolmentStep &step)
    {
        m_progress(step.instruction);
        const StepBudget budget = budgetFor(step.kind);
        const auto deadline = Clock::now() + budget.timeLimit;
        StepProgress progress;
        while (progress.captured < budget.wanted && Clock::now() < deadline && !outOfFrames(step)) {
            cv::Mat image;
            const Pipeline::Frame frame = m_pipeline.read(image);
            if (frame == Pipeline::Frame::Missing || frame == Pipeline::Frame::Black) {
                continue;
            }
            ++m_litFrames;
            if (frame == Pipeline::Frame::Unusable) {
                ++m_unusableFrames;
                continue;
            }
            for (const cv::Mat &view : m_pipeline.orientations(image)) {
                const cv::Mat faces = m_pipeline.detectFaces(view);
                if (faces.rows > 1) {
                    m_encodings.clear();
                    return ScanResult{.code = Aborted, .message = "Multiple faces detected, aborting"};
                }
                if (faces.rows == 1) {
                    handleFace(step, budget, view, faces.row(0), progress);
                    break;
                }
            }
        }
        finishStep(step, progress);
        return std::nullopt;
    }

    ScanResult finish()
    {
        if (!m_encodings.empty()) {
            m_progress("Scan complete");
            return {.code = Recognized, .bestScore = 0, .message = "Scan complete"};
        }
        if (m_litFrames == 0) {
            return {.code = Aborted,
                    .message = m_pipeline.colour() ? "Camera saw only black frames - is it covered or switched off?"
                                                   : "Camera saw only black frames - is IR emitter working?"};
        }
        if (m_unusableFrames == m_litFrames) {
            return {.code = TooDark, .message = "All frames were too dark or too bright, please check dark_threshold in config"};
        }
        return {.code = Aborted, .message = "No face detected, aborting"};
    }

private:
    bool outOfFrames(const EnrolmentStep &step) const
    {
        return step.kind == StepKind::LookStraight && m_litFrames >= maxStraightFrames;
    }

    void handleFace(const EnrolmentStep &step, const StepBudget &budget, const cv::Mat &view, const cv::Mat &face, StepProgress &progress)
    {
        const HeadPose pose = headPose(face);
        switch (step.kind) {
        case StepKind::LookBack:
            if (isLookingStraight(pose, m_straight)) {
                ++progress.captured;
            }
            return;
        case StepKind::Turn:
            if (!acceptTurn(step, pose, progress)) {
                return;
            }
            break;
        case StepKind::LookStraight:
            break;
        }
        if (++progress.sightings % budget.encodeEvery == 1) {
            m_encodings.push_back(m_pipeline.encode(view, face));
            ++progress.captured;
            progress.poseSum.yaw += pose.yaw;
            progress.poseSum.pitch += pose.pitch;
        }
    }

    bool acceptTurn(const EnrolmentStep &step, const HeadPose &pose, StepProgress &progress)
    {
        const bool turningSideways = step.axis == &HeadPose::yaw;
        const double turn = pose.*step.axis - m_straight.*step.axis;
        const double drift = turningSideways ? pose.pitch - m_straight.pitch : pose.yaw - m_straight.yaw;
        const TurnVerdict verdict = judgeTurn(turn, drift, step.limits, step.opposesPrevious, m_previousTurn);
        if (verdict == TurnVerdict::Drifting && !progress.hinted) {
            m_progress(turningSideways ? "Hint: keep your head level" : "Hint: keep your face toward the screen");
            progress.hinted = true;
        }
        if (verdict != TurnVerdict::Accepted) {
            return false;
        }
        if (!step.opposesPrevious) {
            m_previousTurn = turn;
        }
        return true;
    }

    void finishStep(const EnrolmentStep &step, const StepProgress &progress)
    {
        if (step.kind == StepKind::LookStraight && progress.captured > 0) {
            m_straight = {.yaw = progress.poseSum.yaw / progress.captured, .pitch = progress.poseSum.pitch / progress.captured};
        } else if (step.kind == StepKind::Turn && progress.captured == 0) {
            m_progress("Skipped, that turn wasn't seen");
            if (!step.opposesPrevious) {
                m_previousTurn = 0;
            }
        }
    }

    Pipeline &m_pipeline;
    std::vector<cv::Mat> &m_encodings;
    const ProgressCallback &m_progress;
    int m_litFrames = 0;
    int m_unusableFrames = 0;
    HeadPose m_straight;
    double m_previousTurn = 0; // the last accepted turn on the current axis
};
}

ScanResult enroll(const Config &config, std::vector<cv::Mat> &encodings, const ProgressCallback &progress)
{
    Pipeline pipeline(config);
    if (!pipeline.ready()) {
        return {.code = Error, .message = cameraFailure};
    }
    Enrolment enrolment(pipeline, encodings, progress);
    for (const EnrolmentStep &step : enrolmentSteps) {
        // Turns are measured from looking straight, so they need its encodings.
        if (step.kind != StepKind::LookStraight && encodings.empty()) {
            break;
        }
        if (auto failure = enrolment.runStep(step)) {
            return *failure;
        }
    }
    return enrolment.finish();
}
}
