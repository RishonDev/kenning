/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// The service's rate limit, like pam_faillock for passwords: after maxFailures
// faces that were seen but not recognized within window, a user's face unlock is
// locked for lockout. Only such faces count: someone else, or a photo. An empty
// camera view, darkness or a scan cancelled because a password was typed don't.

#include "config.h"

#include <chrono>
#include <map>
#include <sys/types.h>
#include <vector>

namespace FaceUnlock
{
struct FailureLimits {
    int maxFailures = 5; // 0: no limit
    std::chrono::minutes window{15};
    std::chrono::minutes lockout{10};

    static FailureLimits fromConfig(const Config &config);
};

class FailureLimiter
{
public:
    using Clock = std::chrono::steady_clock;

    bool isLockedOut(uid_t uid, const FailureLimits &limits, Clock::time_point now);
    void recordSuccess(uid_t uid);
    // Returns true when this failure locked the user out.
    bool recordFailure(uid_t uid, const FailureLimits &limits, Clock::time_point now);
    // Nothing to remember any more: the service may exit without losing a lockout.
    bool isEmpty(Clock::time_point now);

private:
    struct Record {
        std::vector<Clock::time_point> failures; // within the window
        Clock::time_point lockedUntil{};
        Clock::time_point forgetAfter{};
    };

    void forgetExpired(Clock::time_point now);

    std::map<uid_t, Record> m_records;
};
}
