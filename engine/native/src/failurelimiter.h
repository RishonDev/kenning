/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Like pam_faillock: once both enough unrecognized faces and enough wrong passwords fall
// within window, face unlock is locked for lockout (the password keeps working).

#include "config.h"

#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace FaceUnlock
{
struct FailureLimits {
    int maxFailures = 3; // unrecognized faces; 0: no limit
    int maxPasswordFailures = 3; // wrong passwords, also needed; 0: faces alone lock
    std::chrono::minutes window{15};
    std::chrono::minutes lockout{10};

    static FailureLimits fromConfig(const Config &config);
};

class FailureLimiter
{
public:
    using Clock = std::chrono::steady_clock;

    enum class Kind {
        Face,
        Password,
    };

    bool isLockedOut(uid_t uid, const FailureLimits &limits, Clock::time_point now);
    void recordSuccess(uid_t uid);
    // Returns true when this failure locked the user out.
    bool recordFailure(uid_t uid, Kind kind, const FailureLimits &limits, Clock::time_point now);
    bool isEmpty(Clock::time_point now);

    // Text the limiter can be restored from in a new process, until the next reboot.
    std::string save() const;
    void restore(std::string_view text);

private:
    struct Record {
        std::vector<Clock::time_point> faces;
        std::vector<Clock::time_point> passwords;
        Clock::time_point lockedUntil{};
        Clock::time_point forgetAfter{};
    };

    void forgetExpired(Clock::time_point now);

    std::map<uid_t, Record> m_records;
};
}
