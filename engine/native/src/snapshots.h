/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Photos of unrecognized faces ([snapshots] save_failed), one folder per user.

#include <opencv2/core.hpp>

#include <filesystem>
#include <string>

namespace FaceUnlock
{
constexpr std::size_t maxSnapshotsPerUser = 20;

// Saves image as <seconds since the epoch>.jpg in directory, then keeps the newest keep.
bool saveSnapshot(const std::filesystem::path &directory, const cv::Mat &image, std::size_t keep = maxSnapshotsPerUser);
void pruneSnapshots(const std::filesystem::path &directory, std::size_t keep);
}
