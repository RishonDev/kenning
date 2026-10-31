/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// The service (kenning daemon) owns the camera and the face models. One request per
// connection: "<command> <user>\n" (compare or has-model), answered "<ExitCode>\n".
// It answers only for root or the user's own account. Header-only, for pam_kenning.

#include "config.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
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

// For has-model, which answers at once.
inline constexpr int quickTimeoutSeconds = 5;

// For compare: requests are served one at a time, so allow for a scan already running.
inline int scanTimeoutSeconds(const Config &config)
{
    return int(config.number("video/timeout", 4)) * 2 + 10;
}

struct Request {
    std::string command;
    std::string user;
};

inline std::optional<Request> parseRequest(std::string_view text)
{
    const auto end = text.find('\n');
    const auto space = text.find(' ');
    if (end == std::string_view::npos || space == std::string_view::npos || space > end) {
        return std::nullopt;
    }
    Request request{std::string(text.substr(0, space)), std::string(text.substr(space + 1, end - space - 1))};
    if (request.command.empty() || request.user.empty()) {
        return std::nullopt;
    }
    return request;
}

// Returns the reply code, or -1 when the service can't be reached.
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
