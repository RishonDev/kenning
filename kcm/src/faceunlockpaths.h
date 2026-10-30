/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Files and settings shared by the settings page and its root helper. The helper
// only writes the settings listed here, and only values their check accepts.

#include <QFileInfo>
#include <QList>
#include <QRegularExpression>
#include <QString>

#include <functional>
#include <optional>

namespace FaceUnlock
{
inline const QString howdyConfigPath = QStringLiteral("/etc/howdy/config.ini");
inline const QString howdyComparePath = QStringLiteral("/usr/lib/howdy/compare.py");
inline const QString systemConfigPath = QStringLiteral("/etc/xdg/faceunlockrc");
inline const QString lockScreenPath = QStringLiteral("/usr/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen/LockScreenUi.qml");
inline const QString lockScreenMarker = QStringLiteral("plasma-faceunlock lock screen patch");
inline const QString userThemesPath = QStringLiteral("/usr/local/share/faceunlock/themes");
inline const QString systemThemesPath = QStringLiteral("/usr/share/faceunlock/themes");
inline const QString pamHowdyLine = QStringLiteral("auth       sufficient                  pam_howdy.so");

inline QString modelPath(const QString &user)
{
    return QStringLiteral("/etc/howdy/models/%1.dat").arg(user);
}

inline bool isPamHowdyLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    return !trimmed.startsWith(QLatin1Char('#')) && trimmed.contains(QLatin1String("pam_howdy.so"));
}

// Theme ids double as directory names, so no slashes or leading dots.
inline bool isThemeId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return pattern.match(id).hasMatch();
}

struct PamService {
    QString id;
    QString path;
};

inline QList<PamService> pamServices()
{
    return {
        {QStringLiteral("login"), QStringLiteral("/etc/pam.d/plasmalogin")},
        {QStringLiteral("lockscreen"), QStringLiteral("/etc/pam.d/kde")},
        {QStringLiteral("sudo"), QStringLiteral("/etc/pam.d/sudo")},
        {QStringLiteral("polkit"), QStringLiteral("/etc/pam.d/polkit-1")},
    };
}

struct Setting {
    QString group;
    QString key;
    std::function<bool(const QString &)> accepts;
};

inline bool isBool(const QString &value)
{
    return value == QLatin1String("true") || value == QLatin1String("false");
}

inline std::function<bool(const QString &)> numberIn(double min, double max)
{
    return [min, max](const QString &value) {
        bool ok = false;
        const double number = value.toDouble(&ok);
        return ok && number >= min && number <= max;
    };
}

// Howdy settings, keyed "section/key" as in config.ini.
inline std::optional<Setting> howdySetting(const QString &id)
{
    static const QList<std::pair<QString, std::function<bool(const QString &)>>> settings = {
        {QStringLiteral("core/disabled"), isBool},
        {QStringLiteral("core/detection_notice"), isBool},
        {QStringLiteral("core/no_confirmation"), isBool},
        {QStringLiteral("core/abort_if_ssh"), isBool},
        {QStringLiteral("core/abort_if_lid_closed"), isBool},
        {QStringLiteral("video/certainty"), numberIn(1, 10)},
        {QStringLiteral("video/timeout"), numberIn(1, 30)},
        {QStringLiteral("video/dark_threshold"), numberIn(0, 100)},
        {QStringLiteral("snapshots/save_failed"), isBool},
    };
    for (const auto &[key, accepts] : settings) {
        if (key == id) {
            const int slash = key.indexOf(QLatin1Char('/'));
            return Setting{key.left(slash), key.mid(slash + 1), accepts};
        }
    }
    return std::nullopt;
}

// Login screen settings in /etc/xdg/faceunlockrc, keyed "Group/Key".
inline std::optional<Setting> systemSetting(const QString &id)
{
    static const QList<std::pair<QString, std::function<bool(const QString &)>>> settings = {
        {QStringLiteral("Appearance/AnimationStyle"), isThemeId},
        {QStringLiteral("LoginScreen/AutoScan"), isBool},
        {QStringLiteral("LoginScreen/WelcomePause"), numberIn(0, 3000)},
    };
    for (const auto &[key, accepts] : settings) {
        if (key == id) {
            const int slash = key.indexOf(QLatin1Char('/'));
            return Setting{key.left(slash), key.mid(slash + 1), accepts};
        }
    }
    return std::nullopt;
}
}
