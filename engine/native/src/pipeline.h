/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// The camera and the two recognition networks, shared by scanning and enrolment.

#include "config.h"

#include <opencv2/core.hpp>
#include <opencv2/objdetect/face.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <vector>

namespace FaceUnlock
{
using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start);

// Columns of a YuNet detection row: box, five landmarks, confidence.
namespace YuNet
{
constexpr int rightEyeX = 4;
constexpr int rightEyeY = 5;
constexpr int leftEyeX = 6;
constexpr int leftEyeY = 7;
constexpr int noseX = 8;
constexpr int noseY = 9;
constexpr int rightMouthY = 11;
constexpr int leftMouthY = 13;
constexpr int confidence = 14;
}

enum class Lighting {
    Black, // nothing lit at all, e.g. the IR emitter's off frames
    Unusable, // too dark or washed out by bright light
    Usable,
};

// Share of near-black pixels as Howdy measures it (the darkest of 8 histogram bins),
// plus a check for frames washed out by sunlight or a bright lamp.
Lighting judgeLighting(const cv::Mat &grey, double darkThresholdPercent);

class Pipeline
{
public:
    enum class Frame {
        Missing,
        Black,
        Unusable,
        Usable
    };

    explicit Pipeline(const Config &config);

    bool ready() const;
    bool colour() const;
    double cameraMs() const;
    double networksMs() const;

    // Unless Missing or Black, image holds the frame as BGR, scaled down to video/max_height.
    // Unusable frames are still worth searching: at night an IR picture is mostly
    // black background around a well-lit face.
    Frame read(cv::Mat &image);
    // The frame to search, plus turned copies when video/rotate asks for them.
    std::vector<cv::Mat> orientations(const cv::Mat &image) const;
    // One row per face (see YuNet).
    cv::Mat detectFaces(const cv::Mat &image);
    cv::Mat encode(const cv::Mat &image, const cv::Mat &face);
    double cosineSimilarity(const cv::Mat &feature, const cv::Mat &known) const;

private:
    cv::VideoCapture m_camera;
    cv::Ptr<cv::FaceDetectorYN> m_detector;
    cv::Ptr<cv::FaceRecognizerSF> m_recognizer;
    int m_maxHeight = 320;
    double m_darkThreshold = 60;
    int m_rotate = 0;
    bool m_colour = false;
    double m_cameraMs = 0;
    double m_networksMs = 0;
};
}
