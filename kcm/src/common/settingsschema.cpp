/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "settingsschema.h"

#include <QRegularExpression>

namespace FaceUnlock
{
namespace
{
using Validator = std::function<bool(const QString &)>;

Validator numberIn(double minimum, double maximum)
{
    return [minimum, maximum](const QString &value) {
        bool isNumber = false;
        const double number = value.toDouble(&isNumber);
        return isNumber && number >= minimum && number <= maximum;
    };
}

std::optional<Setting> findSetting(const QList<std::pair<QString, Validator>> &settings, const QString &id)
{
    for (const auto &[key, accepts] : settings) {
        if (key == id) {
            const qsizetype slash = key.indexOf(QLatin1Char('/'));
            return Setting{key.left(slash), key.mid(slash + 1), accepts};
        }
    }
    return std::nullopt;
}

bool isActiveLineWith(const QString &line, QLatin1StringView module)
{
    const QString trimmed = line.trimmed();
    return !trimmed.startsWith(QLatin1Char('#')) && trimmed.contains(module);
}
}

std::optional<Setting> engineSetting(const QString &id)
{
    static const QList<std::pair<QString, Validator>> settings = {
        {QStringLiteral("core/disabled"), isBoolText},
        {QStringLiteral("core/detection_notice"), isBoolText},
        {QStringLiteral("core/no_confirmation"), isBoolText},
        {QStringLiteral("core/abort_if_ssh"), isBoolText},
        {QStringLiteral("core/abort_if_lid_closed"), isBoolText},
        {QStringLiteral("video/certainty"), numberIn(1, 10)},
        {QStringLiteral("video/timeout"), numberIn(1, 30)},
        {QStringLiteral("video/dark_threshold"), numberIn(0, 100)},
        {QStringLiteral("snapshots/save_failed"), isBoolText},
    };
    return findSetting(settings, id);
}

std::optional<Setting> systemSetting(const QString &id)
{
    static const QList<std::pair<QString, Validator>> settings = {
        {QStringLiteral("Appearance/AnimationStyle"), isThemeId},
        {QStringLiteral("LoginScreen/AutoScan"), isBoolText},
        {QStringLiteral("LoginScreen/WelcomePause"), numberIn(0, 3000)},
    };
    return findSetting(settings, id);
}

bool isBoolText(const QString &value)
{
    return value == QLatin1String("true") || value == QLatin1String("false");
}

bool isThemeId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return pattern.match(id).hasMatch();
}

bool isPamLine(const QString &line)
{
    return isActiveLineWith(line, QLatin1StringView("pam_kenning.so"));
}

bool isFacePamLine(const QString &line)
{
    return isPamLine(line) || isActiveLineWith(line, QLatin1StringView("pam_howdy.so"));
}

QList<PamService> pamServices()
{
    return {
        {QStringLiteral("login"), QStringLiteral("/etc/pam.d/plasmalogin")},
        {QStringLiteral("lockscreen"), QStringLiteral("/etc/pam.d/kde")},
        {QStringLiteral("sudo"), QStringLiteral("/etc/pam.d/sudo")},
        {QStringLiteral("polkit"), QStringLiteral("/etc/pam.d/polkit-1")},
    };
}
}
