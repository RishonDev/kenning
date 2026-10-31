/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QDir>
#include <QString>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace FaceUnlock
{
struct Camera {
    QString path; // empty when there is no usable camera
    bool colour = false; // an ordinary webcam rather than an IR camera
};

struct CameraFormats {
    bool capture = false;
    bool loopback = false; // v4l2loopback: a virtual camera any program can feed
    bool grey = false;
    bool colour = false;
};

inline CameraFormats probeCamera(const QString &path)
{
    CameraFormats found;
    const int fd = open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return found;
    }
    v4l2_capability capability{};
    found.capture = ioctl(fd, VIDIOC_QUERYCAP, &capability) == 0
        && ((capability.capabilities & V4L2_CAP_DEVICE_CAPS ? capability.device_caps : capability.capabilities) & V4L2_CAP_VIDEO_CAPTURE);
    found.loopback = QByteArray(reinterpret_cast<const char *>(capability.driver)) == "v4l2 loopback";
    v4l2_fmtdesc format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (found.capture && ioctl(fd, VIDIOC_ENUM_FMT, &format) == 0) {
        found.grey |= format.pixelformat == V4L2_PIX_FMT_GREY;
        found.colour |= format.pixelformat == V4L2_PIX_FMT_MJPEG || format.pixelformat == V4L2_PIX_FMT_YUYV;
        ++format.index;
    }
    close(fd);
    return found;
}

// The camera face unlock should use: the IR sensor (the capture device that only
// offers greyscale frames) if there is one, otherwise an ordinary colour webcam.
// Only /dev/v4l/by-path devices count, which leaves out virtual cameras such as
// v4l2loopback: any program can feed those, including a video of the user.
// by-path names survive reboots and replugging, unlike /dev/videoN.
inline Camera findCamera()
{
    Camera colourCamera;
    const QDir byPath(QStringLiteral("/dev/v4l/by-path"));
    for (const QFileInfo &entry : byPath.entryInfoList({QStringLiteral("*-video-index0")}, QDir::System, QDir::Name)) {
        const CameraFormats camera = probeCamera(entry.absoluteFilePath());
        if (!camera.capture || camera.loopback) {
            continue;
        }
        if (camera.grey && !camera.colour) {
            return {entry.absoluteFilePath(), false};
        }
        // Several paths can name one device (usb-… and usbv2-…); keep the first.
        if (camera.colour && colourCamera.path.isEmpty()) {
            colourCamera = {entry.absoluteFilePath(), true};
        }
    }
    return colourCamera;
}

// A colour camera for previews. Virtual cameras are fine here, since nothing is
// authenticated with them.
inline QString findPreviewCamera()
{
    const QDir byPath(QStringLiteral("/dev/v4l/by-path"));
    for (const QFileInfo &entry : byPath.entryInfoList({QStringLiteral("*-video-index0")}, QDir::System, QDir::Name)) {
        if (const CameraFormats camera = probeCamera(entry.absoluteFilePath()); camera.capture && camera.colour) {
            return entry.absoluteFilePath();
        }
    }
    const QDir dev(QStringLiteral("/dev"));
    for (const QFileInfo &entry : dev.entryInfoList({QStringLiteral("video*")}, QDir::System, QDir::Name)) {
        if (const CameraFormats camera = probeCamera(entry.absoluteFilePath()); camera.capture && camera.loopback && camera.colour) {
            return entry.absoluteFilePath();
        }
    }
    return {};
}
}
