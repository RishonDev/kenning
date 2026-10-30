/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KQuickConfigModule>

#include <QVariantList>
#include <QVariantMap>

// System Settings page for Howdy face unlock.
//
// settings is one flat map that QML edits through setValue():
//   "core/…", "video/…", "snapshots/…"  /etc/howdy/config.ini (helper)
//   "pam/<service>"                      pam_howdy.so in /etc/pam.d (helper)
//   "Appearance/…", "LoginScreen/…"      /etc/xdg/faceunlockrc (helper)
//   "lock/…"                             ~/.config/kscreenlockerrc (written directly)
class FaceUnlockKcm : public KQuickConfigModule
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList faces READ faces NOTIFY facesChanged)
    // {id, name, description, author, removable}; user-installed themes come first
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString devicePath READ devicePath NOTIFY settingsChanged)
    Q_PROPERTY(bool howdyInstalled READ howdyInstalled CONSTANT)
    Q_PROPERTY(bool lockScreenPatched READ lockScreenPatched CONSTANT)

public:
    FaceUnlockKcm(QObject *parent, const KPluginMetaData &data);

    QVariantMap settings() const;
    QVariantList faces() const;
    QVariantList themes() const;
    bool busy() const;
    QString userName() const;
    QString devicePath() const;
    bool howdyInstalled() const;
    bool lockScreenPatched() const;

    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void addFace(const QString &label);
    Q_INVOKABLE void removeFace(int id);
    Q_INVOKABLE void testRecognition();
    Q_INVOKABLE void refreshFaces();
    Q_INVOKABLE void refreshThemes();
    Q_INVOKABLE void installTheme(const QUrl &archive);
    Q_INVOKABLE void removeTheme(const QString &id);

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

Q_SIGNALS:
    void settingsChanged();
    void facesChanged();
    void themesChanged();
    void busyChanged();
    void progress(const QString &line);
    // operation is "add", "remove" or "test"; exitCode is the command's exit status
    // (-1 if authorization failed or the helper could not run it).
    void operationFinished(const QString &operation, int exitCode, const QString &message);
    void errorOccurred(const QString &message);

private:
    void runHelper(const QString &operation, const QVariantMap &args);
    void setBusy(bool busy);
    void updateState();
    static QVariantMap defaultSettings();

    QVariantMap m_settings;
    QVariantMap m_loaded;
    QVariantList m_faces;
    QVariantList m_themes;
    QString m_devicePath;
    bool m_busy = false;
};
