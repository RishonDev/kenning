/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QString>

namespace FaceUnlock
{
struct LockScreenState {
    bool known = false;
    bool faceService = false; // Plasma's Face method rather than the password service
    bool enabled = false;
};

// pam-tidy's answer, "<face|password> <on|off>".
LockScreenState parseLockScreenStatus(const QString &status);
LockScreenState lockScreenState();
}
