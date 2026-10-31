/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QThread>

#include <atomic>

class CameraReader : public QThread
{
    Q_OBJECT
public:
    explicit CameraReader(const QString &devicePath, QObject *parent = nullptr);
    void stop();

Q_SIGNALS:
    void frameReady(const QImage &frame);
    void failed(const QString &message);

protected:
    void run() override;

private:
    QString m_devicePath;
    std::atomic_bool m_stop = false;
};

// Set active to false before the engine uses the camera: a V4L2 device has one reader.
class CameraPreview : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString devicePath READ devicePath WRITE setDevicePath NOTIFY devicePathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit CameraPreview(QQuickItem *parent = nullptr);
    ~CameraPreview() override;

    QString devicePath() const;
    void setDevicePath(const QString &path);
    bool active() const;
    void setActive(bool active);
    bool hasFrame() const;
    QString error() const;

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void devicePathChanged();
    void activeChanged();
    void hasFrameChanged();
    void errorChanged();

private:
    void restart();
    void stopReader();

    QString m_devicePath;
    bool m_active = false;
    QString m_error;
    QImage m_frame;
    CameraReader *m_reader = nullptr;
};
