/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Files the settings page and its root helper work with. The engine's own paths
// come from config.h, so a package that moves them moves them everywhere.

#include "config.h"

#include <QString>

namespace FaceUnlock
{
inline const QString engineConfigPath = QString::fromUtf8(configPath);
inline const QString modelsPath = QString::fromUtf8(modelsDir);
inline const QString snapshotsPath = QString::fromUtf8(snapshotsDir);
inline const QString serviceUserName = QString::fromUtf8(serviceUser);

inline QString modelPath(const QString &user)
{
    return QString::fromStdString(modelPath(user.toStdString()));
}

inline const QString engineBinary = QStringLiteral("/usr/bin/kenning");
inline const QString pamModulePath = QStringLiteral("/usr/lib/security/pam_kenning.so");
// Login screen settings, read by plasma-login-manager-kenning.
inline const QString systemConfigPath = QStringLiteral("/etc/xdg/kenningrc");
inline const QString lockScreenPath = QStringLiteral("/usr/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen/LockScreenUi.qml");
inline const QString lockScreenMarker = QStringLiteral("kenning lock screen patch");
// Installed by plasma-login-manager-kenning. An unpatched login manager would scan
// the face before checking a typed password, so the login screen is only offered
// with this one.
inline const QString loginManagerSupportPath = QStringLiteral("/usr/share/kenning/login-manager");
inline const QString userThemesPath = QStringLiteral("/usr/local/share/kenning/themes");
inline const QString systemThemesPath = QStringLiteral("/usr/share/kenning/themes");
// Manages lock screen face unlock: Plasma's Face sign-in method (PAM service kde-face)
// on Plasma 6.8 and newer, pam_kenning.so in the password service before that.
inline const QString pamTidy = QStringLiteral("/usr/lib/kenning/pam-tidy");
}
