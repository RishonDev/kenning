/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lockscreenstate.h"
#include "paths.h"

#include <QProcess>

namespace FaceUnlock
{
LockScreenState parseLockScreenStatus(const QString &status)
{
    const QString trimmed = status.trimmed();
    const bool on = trimmed.endsWith(QLatin1String(" on"));
    if (!on && !trimmed.endsWith(QLatin1String(" off"))) {
        return {};
    }
    return {.known = true, .faceService = trimmed.startsWith(QLatin1String("face")), .enabled = on};
}

LockScreenState lockScreenState()
{
    constexpr int timeoutMs = 5000;
    QProcess process;
    process.start(pamTidy, {QStringLiteral("lockscreen"), QStringLiteral("status")});
    process.waitForFinished(timeoutMs);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }
    return parseLockScreenStatus(QString::fromUtf8(process.readAllStandardOutput()));
}
}
