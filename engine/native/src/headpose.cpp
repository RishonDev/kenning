/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "headpose.h"
#include "pipeline.h"

#include <algorithm>
#include <cmath>

namespace FaceUnlock
{
namespace
{
constexpr double straightYawTolerance = 0.08;
constexpr double straightPitchTolerance = 0.05;
}

HeadPose headPose(const cv::Mat &face)
{
    const auto at = [&face](int column) {
        return double(face.at<float>(0, column));
    };
    const double eyesX = (at(YuNet::rightEyeX) + at(YuNet::leftEyeX)) / 2;
    const double eyesY = (at(YuNet::rightEyeY) + at(YuNet::leftEyeY)) / 2;
    const double eyeDistance = std::max(1.0, std::hypot(at(YuNet::leftEyeX) - at(YuNet::rightEyeX), at(YuNet::leftEyeY) - at(YuNet::rightEyeY)));
    const double mouthY = (at(YuNet::rightMouthY) + at(YuNet::leftMouthY)) / 2;
    return {
        .yaw = (at(YuNet::noseX) - eyesX) / eyeDistance,
        .pitch = (at(YuNet::noseY) - eyesY) / std::max(1.0, mouthY - eyesY),
    };
}

bool isLookingStraight(const HeadPose &pose, const HeadPose &straight)
{
    return std::abs(pose.yaw - straight.yaw) < straightYawTolerance && std::abs(pose.pitch - straight.pitch) < straightPitchTolerance;
}

TurnVerdict judgeTurn(double turn, double drift, const TurnLimits &limits, bool mustOppose, double previousTurn)
{
    // Halfway into the turn is enough to tell that the head is drifting as well.
    if (std::abs(turn) >= limits.minimum / 2 && std::abs(drift) > limits.maxDrift) {
        return TurnVerdict::Drifting;
    }
    if (std::abs(turn) < limits.minimum) {
        return TurnVerdict::TooSmall;
    }
    if (std::abs(turn) > limits.maximum) {
        return TurnVerdict::TooFar;
    }
    if (mustOppose && previousTurn != 0 && std::signbit(turn) == std::signbit(previousTurn)) {
        return TurnVerdict::WrongWay;
    }
    return TurnVerdict::Accepted;
}
}
