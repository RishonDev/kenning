/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Head turns during enrolment, judged from YuNet's landmarks. The values are
// ratios, only meaningful relative to the same person looking straight ahead.

#include <opencv2/core.hpp>

namespace FaceUnlock
{
struct HeadPose {
    double yaw = 0; // nose offset from between the eyes, in eye distances
    double pitch = 0; // nose height between eyes (0) and mouth (1)
};

// face is one YuNet detection row.
HeadPose headPose(const cv::Mat &face);

bool isLookingStraight(const HeadPose &pose, const HeadPose &straight);

struct TurnLimits {
    double minimum; // turn needed
    double maximum; // further than this aligns faces badly
    double maxDrift; // allowed on the other axis
};

enum class TurnVerdict {
    Accepted,
    TooSmall,
    TooFar,
    WrongWay, // the same way as the previous turn on this axis
    Drifting, // turned, but also moved on the other axis
};

// turn and drift are measured from looking straight. The camera may be mirrored,
// so a turn that must oppose the previous one only compares signs with it;
// previousTurn is 0 when there was none.
TurnVerdict judgeTurn(double turn, double drift, const TurnLimits &limits, bool mustOppose, double previousTurn);
}
