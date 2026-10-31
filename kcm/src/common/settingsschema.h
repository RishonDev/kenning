/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// The settings the root helper may write, and the values it accepts for each.

#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace FaceUnlock
{
struct Setting {
    QString group;
    QString key;
    std::function<bool(const QString &)> accepts;
};

// Engine settings, keyed "section/key" as in config.ini.
std::optional<Setting> engineSetting(const QString &id);
// Login screen settings in kenningrc, keyed "Group/Key".
std::optional<Setting> systemSetting(const QString &id);

bool isBoolText(const QString &value);
// Theme ids double as directory names, so no slashes or leading dots.
bool isThemeId(const QString &id);

inline const QString pamLine = QStringLiteral("auth       sufficient                  pam_kenning.so");
bool isPamLine(const QString &line);
// pam_kenning or a leftover pam_howdy; both are removed so only one engine runs.
bool isFacePamLine(const QString &line);

struct PamService {
    QString id; // as in the settings page's "pam/<id>"
    QString path;
};
QList<PamService> pamServices();
}
