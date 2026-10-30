/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// The face unlock service (kenning daemon, socket-activated by systemd) owns the
// camera and the face models, which only root can read. Clients talk to it over a
// Unix socket, one request per connection:
//
//   request   "<command> <user>\n"   command: compare | has-model
//   reply     "<code>\n"             an ExitCode: 0 recognized / has a model,
//                                    10 no model, 11 timeout, 12 refused, 13 too dark
//
// The service answers only for root or for the user's own account, so nobody can
// ask about someone else's face. Header-only so pam_kenning (which must not load
// OpenCV) can use it.

#include "config.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace FaceUnlock
{
#ifndef KENNING_SOCKET
#define KENNING_SOCKET "/run/kenning.sock"
#endif
inline constexpr const char *socketPath = KENNING_SOCKET;

// Sends one request and returns the reply code, or -1 when the service can't be
// reached. timeoutSeconds bounds the whole wait (a scan takes up to video/timeout,
// and requests are served one at a time).
inline int askService(const std::string &command, const std::string &user, int timeoutSeconds)
{
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socketPath, sizeof(address.sun_path) - 1);
    timeval timeout{timeoutSeconds, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        close(fd);
        return -1;
    }

    const std::string request = command + ' ' + user + '\n';
    if (send(fd, request.data(), request.size(), MSG_NOSIGNAL) != ssize_t(request.size())) {
        close(fd);
        return -1;
    }
    char reply[16] = {};
    size_t received = 0;
    while (received < sizeof(reply) - 1) {
        const ssize_t n = recv(fd, reply + received, sizeof(reply) - 1 - received, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        received += size_t(n);
        if (std::memchr(reply, '\n', received)) {
            break;
        }
    }
    close(fd);
    if (received == 0 || !std::memchr(reply, '\n', received)) {
        return -1;
    }
    return std::atoi(reply);
}
}
