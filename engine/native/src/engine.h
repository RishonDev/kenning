/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Face recognition on OpenCV's YuNet detector and SFace recognizer. Config keys,
// model files and exit codes follow Howdy, so the settings page, PAM module and
// the login and lock screens treat both engines the same way.

#include "config.h"
#include "facemodels.h"

#include <opencv2/core.hpp>

#include <functional>
#include <string>
#include <vector>

namespace FaceUnlock
{
// Score as Howdy reports it: lower means a closer match. For SFace this is
// (1 - cosine similarity) * 10, so the recommended cosine of 0.363 is 6.37.
double matchScore(double cosine);
// The score reported when no face was compared at all.
constexpr double noMatchScore = 99;

// What a scan saw, for kenning test.
struct ScanStats {
    double cameraMs = 0; // opening the camera
    double networksMs = 0; // loading YuNet and SFace
    double firstFrameMs = 0; // from the start of the scan to the first frame
    double detectMs = 0; // total time in the detector
    double encodeMs = 0; // total time aligning and encoding faces
    int frames = 0;
    int unusableFrames = 0; // too dark or too bright
    int faceFrames = 0; // frames with at least one face
    double bestDetection = 0; // highest detector confidence, 0 to 1
};

struct ScanResult {
    ExitCode code = Error;
    double bestScore = noMatchScore;
    std::string message;
    ScanStats stats = {};
    cv::Mat snapshot = {}; // the closest face that didn't match, with [snapshots] save_failed
};

// Looks for the user's face until config video/timeout runs out, or until
// cancelled() returns true (checked once per frame).
ScanResult compare(const Config &config, const std::string &user, const std::function<bool()> &cancelled = {});

using ProgressCallback = std::function<void(const std::string &)>;

// Captures encodings of exactly one face for a new model: looking straight, then
// turned left, right, up and down. progress receives each instruction.
ScanResult enroll(const Config &config, std::vector<cv::Mat> &encodings, const ProgressCallback &progress);
}
