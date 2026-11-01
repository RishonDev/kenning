/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Like a phone after a restart: face unlock waits until the user has signed in another way.

#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/types.h>

namespace FaceUnlock
{
class SignedInUsers
{
public:
    bool contains(uid_t uid) const
    {
        return m_users.contains(uid);
    }

    // Returns true when uid is new.
    bool add(uid_t uid)
    {
        return m_users.insert(uid).second;
    }

    // Text this can be restored from in a new process, until the next reboot.
    std::string save() const
    {
        std::string text;
        for (const uid_t uid : m_users) {
            text += std::to_string(uid) + '\n';
        }
        return text;
    }

    void restore(std::string_view text)
    {
        std::istringstream lines{std::string(text)};
        for (uid_t uid = 0; lines >> uid;) {
            m_users.insert(uid);
        }
    }

private:
    std::set<uid_t> m_users;
};
}
