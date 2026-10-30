/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Face recognition on OpenCV's YuNet detector and SFace recognizer. Config keys,
// model files and exit codes follow Howdy, so the settings page, PAM module and
// the login and lock screens treat both engines the same way.

#include "config.h"

#include <opencv2/core.hpp>

#include <functional>
#include <string>
#include <vector>

namespace FaceUnlock
{
struct FaceModel {
    int id = 0;
    std::string label;
    long long time = 0;
    std::vector<cv::Mat> encodings; // SFace features, 1x128 CV_32F
};

std::vector<FaceModel> loadModels(const std::string &user);
bool saveModels(const std::string &user, const std::vector<FaceModel> &models);

// Score as Howdy reports it: lower means a closer match. For SFace this is
// (1 - cosine similarity) * 10, so the recommended cosine of 0.363 is 6.37.
double matchScore(double cosine);

struct ScanResult {
    ExitCode code = Error;
    double bestScore = 99;
    std::string message;
};

// Looks for the user's face until config video/timeout runs out.
ScanResult compare(const Config &config, const std::string &user);

// Captures encodings of exactly one face for a new model. progress receives
// Howdy's messages ("Please look straight into the camera", "Scan complete" …).
ScanResult enroll(const Config &config, std::vector<cv::Mat> &encodings, const std::function<void(const std::string &)> &progress);
}
