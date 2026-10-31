/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "snapshots.h"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <vector>

namespace FaceUnlock
{
namespace fs = std::filesystem;

namespace
{
constexpr int jpegQuality = 85;
}

bool saveSnapshot(const fs::path &directory, const cv::Mat &image, std::size_t keep)
{
    std::error_code error;
    fs::create_directories(directory, error);
    fs::permissions(directory, fs::perms::owner_all, error);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const fs::path file = directory / (std::to_string(seconds) + ".jpg");
    if (!cv::imwrite(file.string(), image, {cv::IMWRITE_JPEG_QUALITY, jpegQuality})) {
        return false;
    }
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, error);
    pruneSnapshots(directory, keep);
    return true;
}

void pruneSnapshots(const fs::path &directory, std::size_t keep)
{
    std::error_code error;
    std::vector<fs::path> photos;
    for (const auto &entry : fs::directory_iterator(directory, error)) {
        if (entry.path().extension() == ".jpg") {
            photos.push_back(entry.path());
        }
    }
    if (photos.size() <= keep) {
        return;
    }
    // Named by time, so the oldest sort first.
    std::sort(photos.begin(), photos.end());
    for (std::size_t i = 0; i + keep < photos.size(); ++i) {
        fs::remove(photos[i], error);
    }
}
}
