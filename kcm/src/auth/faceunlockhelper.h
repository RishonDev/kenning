/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KAuth/ActionReply>
#include <QObject>

using namespace KAuth;

// Every action works on the calling user only.
class FaceUnlockHelper : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    ActionReply list(const QVariantMap &args);
    ActionReply add(const QVariantMap &args);
    ActionReply remove(const QVariantMap &args);
    ActionReply save(const QVariantMap &args);
    ActionReply installtheme(const QVariantMap &args);
    ActionReply removetheme(const QVariantMap &args);
    ActionReply snapshots(const QVariantMap &args);
    ActionReply clearsnapshots(const QVariantMap &args);
};
