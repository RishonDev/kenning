/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace FaceUnlock
{
// Serves requests on the service socket until idle (socket-activated) or forever
// (started by hand). Returns the process exit code.
int runDaemon();
}
