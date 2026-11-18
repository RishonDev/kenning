/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "failurelimiter.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace FaceUnlock
{
FailureLimits FailureLimits::fromConfig(const Config &config)
{
    // Clamped as doubles: converting a huge value or NaN to int would be undefined.
    const auto setting = [&config](const char *key, int fallback, double minimum, double maximum) {
        const double value = config.number(key, fallback);
        return std::isnan(value) ? fallback : int(std::clamp(value, minimum, maximum));
    };
    const auto count = [&setting](const char *key, int fallback) {
        return setting(key, fallback, 0, 1000);
    };
    const auto minutes = [&setting](const char *key, int fallback) {
        return std::chrono::minutes(setting(key, fallback, 1, 24 * 60));
    };
    return {
        .maxFailures = count("core/max_failures", 3),
        .maxPasswordFailures = count("core/max_password_failures", 3),
        .window = minutes("core/failure_window", 15),
        .lockout = minutes("core/lockout_minutes", 10),
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

bool FailureLimiter::recordFailure(uid_t uid, Kind kind, const FailureLimits &limits, Clock::time_point now)
{
    if (limits.maxFailures == 0) {
        return false;
    }
    Record &record = m_records[uid];
    const auto expired = [&](Clock::time_point time) {
        return time + limits.window <= now;
    };
    std::erase_if(record.faces, expired);
    std::erase_if(record.passwords, expired);
    (kind == Kind::Face ? record.faces : record.passwords).push_back(now);
    record.forgetAfter = std::max(record.forgetAfter, now + limits.window);

    const bool facesReached = int(record.faces.size()) >= limits.maxFailures;
    const bool passwordsReached = limits.maxPasswordFailures == 0 || int(record.passwords.size()) >= limits.maxPasswordFailures;
    if (!facesReached || !passwordsReached || now < record.lockedUntil) {
        return false;
    }
    record.faces.clear();
    record.passwords.clear();
    record.lockedUntil = now + limits.lockout;
    record.forgetAfter = record.lockedUntil;
    return true;
}

bool FailureLimiter::isEmpty(Clock::time_point now)
{
    forgetExpired(now);
    return m_records.empty();
}

// One line per user, in clock ticks: uid, lockedUntil, forgetAfter, each face failure,
// then "p" and each password failure. Lines without "p" (older versions) are all faces.
std::string FailureLimiter::save() const
{
    std::ostringstream text;
    for (const auto &[uid, record] : m_records) {
        text << uid << ' ' << record.lockedUntil.time_since_epoch().count() << ' ' << record.forgetAfter.time_since_epoch().count();
        for (const Clock::time_point failure : record.faces) {
            text << ' ' << failure.time_since_epoch().count();
        }
        text << " p";
        for (const Clock::time_point failure : record.passwords) {
            text << ' ' << failure.time_since_epoch().count();
        }
        text << '\n';
    }
    return text.str();
}

void FailureLimiter::restore(std::string_view text)
{
    const auto at = [](Clock::rep ticks) {
        return Clock::time_point(Clock::duration(ticks));
    };
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        uid_t uid = 0;
        Clock::rep lockedUntil = 0;
        Clock::rep forgetAfter = 0;
        if (!(fields >> uid >> lockedUntil >> forgetAfter)) {
            continue;
        }
        Record record{.faces = {}, .passwords = {}, .lockedUntil = at(lockedUntil), .forgetAfter = at(forgetAfter)};
        std::vector<Clock::time_point> *list = &record.faces;
        for (std::string field; fields >> field;) {
            if (field == "p") {
                list = &record.passwords;
                continue;
            }
            Clock::rep ticks = 0;
            if (std::istringstream(field) >> ticks) {
                list->push_back(at(ticks));
            }
        }
        m_records[uid] = record;
    }
}

void FailureLimiter::forgetExpired(Clock::time_point now)
{
    std::erase_if(m_records, [now](const auto &entry) {
        return entry.second.forgetAfter <= now;
    });
}
}
