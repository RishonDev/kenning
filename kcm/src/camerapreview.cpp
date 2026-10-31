/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "camerapreview.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

namespace
{
int xioctl(int fd, unsigned long request, void *arg)
{
    int result;
    do {
        result = ioctl(fd, request, arg);
    } while (result == -1 && errno == EINTR);
    return result;
}

QImage toImage(const v4l2_pix_format &format, const uchar *data, size_t size)
{
    const int width = format.width;
    const int height = format.height;
    switch (format.pixelformat) {
    case V4L2_PIX_FMT_GREY: {
        QImage image(width, height, QImage::Format_Grayscale8);
        for (int y = 0; y < height && size_t((y + 1) * format.bytesperline) <= size; ++y) {
            memcpy(image.scanLine(y), data + y * format.bytesperline, width);
        }
        return image;
    }
    case V4L2_PIX_FMT_YUYV: {
        // Two pixels per 4 bytes (Y0 U Y1 V) sharing their colour; BT.601 video range.
        QImage image(width, height, QImage::Format_RGB888);
        const auto clamp = [](int value) {
            return uchar(std::clamp(value >> 8, 0, 255));
        };
        for (int y = 0; y < height && size_t((y + 1) * format.bytesperline) <= size; ++y) {
            const uchar *row = data + y * format.bytesperline;
            uchar *out = image.scanLine(y);
            for (int x = 0; x + 1 < width; x += 2) {
                const int u = row[x * 2 + 1] - 128;
                const int v = row[x * 2 + 3] - 128;
                for (const int luma : {row[x * 2], row[x * 2 + 2]}) {
                    const int c = 298 * (luma - 16) + 128;
                    *out++ = clamp(c + 409 * v);
                    *out++ = clamp(c - 100 * u - 208 * v);
                    *out++ = clamp(c + 516 * u);
                }
            }
        }
        return image;
    }
    case V4L2_PIX_FMT_MJPEG:
        return QImage::fromData(data, int(size), "JPEG");
    default:
        return {};
    }
}
}

CameraReader::CameraReader(const QString &devicePath, QObject *parent)
    : QThread(parent)
    , m_devicePath(devicePath)
{
}

void CameraReader::stop()
{
    m_stop = true;
}

void CameraReader::run()
{
    const int fd = open(m_devicePath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        Q_EMIT failed(QString::fromLocal8Bit(strerror(errno)));
        return;
    }

    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_G_FMT, &format) < 0) {
        Q_EMIT failed(QStringLiteral("Not a video capture device"));
        close(fd);
        return;
    }

    v4l2_requestbuffers request{};
    request.count = 4;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &request) < 0 || request.count == 0) {
        Q_EMIT failed(QString::fromLocal8Bit(strerror(errno)));
        close(fd);
        return;
    }

    struct Buffer {
        void *start;
        size_t length;
    };
    QList<Buffer> buffers;
    for (unsigned i = 0; i < request.count; ++i) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            break;
        }
        void *start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer.m.offset);
        if (start == MAP_FAILED) {
            break;
        }
        buffers.append({start, buffer.length});
        xioctl(fd, VIDIOC_QBUF, &buffer);
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (buffers.size() != int(request.count) || xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        Q_EMIT failed(QStringLiteral("Could not start the camera"));
    } else {
        while (!m_stop) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            timeval timeout{0, 200000};
            if (select(fd + 1, &fds, nullptr, nullptr, &timeout) <= 0) {
                continue;
            }
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            if (xioctl(fd, VIDIOC_DQBUF, &buffer) < 0) {
                continue;
            }
            const QImage image = toImage(format.fmt.pix, static_cast<uchar *>(buffers[buffer.index].start), buffer.bytesused);
            if (!image.isNull()) {
                Q_EMIT frameReady(image);
            }
            xioctl(fd, VIDIOC_QBUF, &buffer);
        }
        xioctl(fd, VIDIOC_STREAMOFF, &type);
    }

    for (const Buffer &buffer : std::as_const(buffers)) {
        munmap(buffer.start, buffer.length);
    }
    close(fd);
}

CameraPreview::CameraPreview(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

CameraPreview::~CameraPreview()
{
    stopReader();
}

QString CameraPreview::devicePath() const
{
    return m_devicePath;
}

void CameraPreview::setDevicePath(const QString &path)
{
    if (m_devicePath != path) {
        m_devicePath = path;
        Q_EMIT devicePathChanged();
        restart();
    }
}

bool CameraPreview::active() const
{
    return m_active;
}

void CameraPreview::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        Q_EMIT activeChanged();
        restart();
    }
}

bool CameraPreview::hasFrame() const
{
    return !m_frame.isNull();
}

QString CameraPreview::error() const
{
    return m_error;
}

void CameraPreview::stopReader()
{
    if (m_reader) {
        m_reader->stop();
        m_reader->wait();
        delete m_reader;
        m_reader = nullptr;
    }
}

void CameraPreview::restart()
{
    stopReader();
    if (!m_active || m_devicePath.isEmpty()) {
        return;
    }
    m_error.clear();
    Q_EMIT errorChanged();

    m_reader = new CameraReader(m_devicePath);
    connect(m_reader, &CameraReader::frameReady, this, [this](const QImage &frame) {
        const bool first = m_frame.isNull();
        m_frame = frame;
        if (first) {
            Q_EMIT hasFrameChanged();
        }
        update();
    });
    connect(m_reader, &CameraReader::failed, this, [this](const QString &message) {
        m_error = message;
        Q_EMIT errorChanged();
    });
    m_reader->start();
}

void CameraPreview::paint(QPainter *painter)
{
    if (m_frame.isNull()) {
        return;
    }
    const QRectF bounds = boundingRect();
    QPainterPath circle;
    circle.addEllipse(bounds);
    painter->setClipPath(circle);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // Fill the circle, cropping the longer side, and mirror like a selfie camera.
    const QSizeF scaled = QSizeF(m_frame.size()).scaled(bounds.size(), Qt::KeepAspectRatioByExpanding);
    const QRectF target(bounds.center().x() - scaled.width() / 2, bounds.center().y() - scaled.height() / 2, scaled.width(), scaled.height());
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    painter->drawImage(target, m_frame.flipped(Qt::Horizontal));
#else
    painter->drawImage(target, m_frame.mirrored(true, false));
#endif
}
