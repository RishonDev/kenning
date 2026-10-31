/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QString>

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

CameraFormats probeCamera(const QString &path);

// The camera face unlock should use: the IR sensor (the capture device that only
// offers greyscale frames) if there is one, otherwise an ordinary colour webcam.
// Only /dev/v4l/by-path devices count, which leaves out virtual cameras such as
// v4l2loopback: any program can feed those, including a video of the user.
// by-path names survive reboots and replugging, unlike /dev/videoN.
Camera findCamera();

// A colour camera for previews. Virtual cameras are fine here, since nothing is
// authenticated with them.
QString findPreviewCamera();
}
