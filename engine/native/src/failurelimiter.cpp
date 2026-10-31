/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "failurelimiter.h"

#include <algorithm>

namespace FaceUnlock
{
FailureLimits FailureLimits::fromConfig(const Config &config)
{
    return {
        .maxFailures = std::max(0, int(config.number("core/max_failures", 5))),
        .window = std::chrono::minutes(std::max(1, int(config.number("core/failure_window", 15)))),
        .lockout = std::chrono::minutes(std::max(1, int(config.number("core/lockout_minutes", 10)))),
    };
}

bool FailureLimiter::isLockedOut(uid_t uid, const FailureLimits &limits, Clock::time_point now)
{
    forgetExpired(now);
    const auto found = m_records.find(uid);
    return limits.maxFailures > 0 && found != m_records.end() && now < found->second.lockedUntil;
}

void FailureLimiter::recordSuccess(uid_t uid)
{
    m_records.erase(uid);
}

bool FailureLimiter::recordFailure(uid_t uid, const FailureLimits &limits, Clock::time_point now)
{
    if (limits.maxFailures == 0) {
        return false;
    }
    Record &record = m_records[uid];
    std::erase_if(record.failures, [&](Clock::time_point time) {
        return time + limits.window <= now;
    });
    record.failures.push_back(now);
    record.forgetAfter = now + limits.window;
    if (int(record.failures.size()) < limits.maxFailures) {
        return false;
    }
    record.failures.clear();
    record.lockedUntil = now + limits.lockout;
    record.forgetAfter = record.lockedUntil;
    return true;
}

bool FailureLimiter::isEmpty(Clock::time_point now)
{
    forgetExpired(now);
    return m_records.empty();
}

void FailureLimiter::forgetExpired(Clock::time_point now)
{
    std::erase_if(m_records, [now](const auto &entry) {
        return entry.second.forgetAfter <= now;
    });
}
}
