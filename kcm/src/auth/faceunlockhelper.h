/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KAuth/ActionReply>
#include <QObject>

using namespace KAuth;

// Root side of the face unlock settings: Howdy face models, /etc/howdy/config.ini,
// the PAM files that load pam_howdy.so and /etc/xdg/faceunlockrc.
// Every action works on the calling user only.
class FaceUnlockHelper : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    ActionReply list(const QVariantMap &args);
    ActionReply add(const QVariantMap &args);
    ActionReply remove(const QVariantMap &args);
    ActionReply test(const QVariantMap &args);
    ActionReply save(const QVariantMap &args);
    ActionReply installtheme(const QVariantMap &args);
    ActionReply removetheme(const QVariantMap &args);
};
