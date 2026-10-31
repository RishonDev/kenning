/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// A user's face models: JSON in Howdy's layout, one file per user in modelsDir,
// readable only by the service user.

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace FaceUnlock
{
struct FaceModel {
    int id = 0;
    std::string label;
    long long time = 0; // seconds since the epoch
    std::vector<cv::Mat> encodings; // SFace features, 1x128 CV_32F
};

std::vector<FaceModel> readModels(const std::string &path);
bool writeModels(const std::string &path, const std::vector<FaceModel> &models);

// The same for a user's file, which belongs to the service user afterwards.
std::vector<FaceModel> loadModels(const std::string &user);
bool saveModels(const std::string &user, const std::vector<FaceModel> &models);

// Hands a file in the models directory to the service user (when run as root).
void giveToService(const std::string &path);
}
