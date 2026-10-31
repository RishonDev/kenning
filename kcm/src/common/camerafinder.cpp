/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "camerafinder.h"

#include <QDir>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace FaceUnlock
{
namespace
{
QFileInfoList stableCameraPaths()
{
    const QDir byPath(QStringLiteral("/dev/v4l/by-path"));
    return byPath.entryInfoList({QStringLiteral("*-video-index0")}, QDir::System, QDir::Name);
}
}

CameraFormats probeCamera(const QString &path)
{
    CameraFormats found;
    const int fd = open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return found;
    }
    v4l2_capability capability{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) == 0) {
        const quint32 capabilities = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) ? capability.device_caps : capability.capabilities;
        found.capture = capabilities & V4L2_CAP_VIDEO_CAPTURE;
        found.loopback = QByteArray(reinterpret_cast<const char *>(capability.driver)) == "v4l2 loopback";
    }
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

Camera findCamera()
{
    Camera colourCamera;
    for (const QFileInfo &entry : stableCameraPaths()) {
        const CameraFormats camera = probeCamera(entry.absoluteFilePath());
        if (!camera.capture || camera.loopback) {
            continue;
        }
        if (camera.grey && !camera.colour) {
            return {.path = entry.absoluteFilePath(), .colour = false};
        }
        // Several paths can name one device (usb-… and usbv2-…); keep the first.
        if (camera.colour && colourCamera.path.isEmpty()) {
            colourCamera = {.path = entry.absoluteFilePath(), .colour = true};
        }
    }
    return colourCamera;
}

QString findPreviewCamera()
{
    for (const QFileInfo &entry : stableCameraPaths()) {
        if (const CameraFormats camera = probeCamera(entry.absoluteFilePath()); camera.capture && camera.colour) {
            return entry.absoluteFilePath();
        }
    }
    const QDir devices(QStringLiteral("/dev"));
    for (const QFileInfo &entry : devices.entryInfoList({QStringLiteral("video*")}, QDir::System, QDir::Name)) {
        if (const CameraFormats camera = probeCamera(entry.absoluteFilePath()); camera.capture && camera.loopback && camera.colour) {
            return entry.absoluteFilePath();
        }
    }
    return {};
}
}
