/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// kenning daemon: serves face checks for pam_kenning and the setup prompt, so
// the face models can stay root-only. Protocol and access rules: service.h.

#include "daemon.h"
#include "engine.h"
#include "service.h"

#include <filesystem>
#include <iostream>
#include <poll.h>
#include <pwd.h>
#include <sys/stat.h>

namespace FaceUnlock
{
namespace
{
// Socket activation keeps no process around between uses; after this long without
// a request the service exits and systemd starts it again on the next one.
constexpr int idleExitMs = 5 * 60 * 1000;

// The listening socket from systemd (LISTEN_FDS, fd 3), or -1.
int activatedSocket()
{
    const char *pid = std::getenv("LISTEN_PID");
    const char *fds = std::getenv("LISTEN_FDS");
    if (pid && fds && std::atoi(pid) == getpid() && std::atoi(fds) >= 1) {
        return 3;
    }
    return -1;
}

// For running by hand (debugging): create the socket ourselves.
int ownSocket()
{
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socketPath, sizeof(address.sun_path) - 1);
    unlink(socketPath);
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(fd, 8) != 0) {
        return -1;
    }
    chmod(socketPath, 0666);
    return fd;
}

void reply(int client, int code)
{
    const std::string text = std::to_string(code) + '\n';
    send(client, text.data(), text.size(), MSG_NOSIGNAL);
}

void serve(int client)
{
    // A client that connects and says nothing must not hold up everyone else.
    timeval timeout{2, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    ucred peer{};
    socklen_t peerSize = sizeof(peer);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &peerSize) != 0) {
        return;
    }

    char buffer[256] = {};
    size_t received = 0;
    while (received < sizeof(buffer) - 1 && !std::memchr(buffer, '\n', received)) {
        const ssize_t n = recv(client, buffer + received, sizeof(buffer) - 1 - received, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return;
        }
        received += size_t(n);
    }
    const std::string request(buffer, strnlen(buffer, received));
    const auto space = request.find(' ');
    const auto end = request.find('\n');
    if (space == std::string::npos || end == std::string::npos || end < space) {
        return reply(client, Aborted);
    }
    const std::string command = request.substr(0, space);
    const std::string user = request.substr(space + 1, end - space - 1);

    const passwd *account = user.empty() || user.find('/') != std::string::npos ? nullptr : getpwnam(user.c_str());
    if (!account || (peer.uid != 0 && peer.uid != account->pw_uid)) {
        return reply(client, Aborted);
    }

    if (command == "has-model") {
        return reply(client, std::filesystem::exists(modelPath(user)) ? Recognized : NoModel);
    }
    if (command == "compare") {
        // Config is read per request so settings changes apply without a restart.
        return reply(client, compare(Config(), user).code);
    }
    reply(client, Aborted);
}
}

int runDaemon()
{
    int listener = activatedSocket();
    const bool activated = listener >= 0;
    if (!activated) {
        listener = ownSocket();
    }
    if (listener < 0) {
        std::cerr << "kenning daemon: can't listen on " << socketPath << '\n';
        return Error;
    }

    for (;;) {
        pollfd waiting{listener, POLLIN, 0};
        const int ready = poll(&waiting, 1, activated ? idleExitMs : -1);
        if (ready == 0) {
            return 0;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Error;
        }
        const int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            continue;
        }
        serve(client);
        close(client);
    }
}
}
