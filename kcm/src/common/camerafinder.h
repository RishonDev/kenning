/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QString>

namespace FaceUnlock
{
struct Camera {
    QString path; // empty without a usable camera
    bool colour = false;
};

struct CameraFormats {
    bool capture = false;
    bool loopback = false; // v4l2loopback
    bool grey = false;
    bool colour = false;
};

CameraFormats probeCamera(const QString &path);

// The IR sensor (greyscale only) if there is one, else a colour webcam. Only by-path devices,
// so virtual cameras, which any program can feed, are left out.
Camera findCamera();

// A colour camera for previews; virtual cameras are fine, nothing is authenticated with them.
QString findPreviewCamera();
}
