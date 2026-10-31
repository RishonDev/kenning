/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// kenning daemon: serves face checks for pam_kenning and the setup prompt, so
// the face models can stay root-only. Protocol and access rules: service.h.

#include "daemon.h"
#include "engine.h"
#include "service.h"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <map>
#include <poll.h>
#include <pwd.h>
#include <sys/stat.h>
#include <vector>

namespace FaceUnlock
{
namespace
{
// Socket activation keeps no process around between uses; after this long without
// a request the service exits and systemd starts it again on the next one.
constexpr int idleExitMs = 5 * 60 * 1000;

// Failed attempts per user, for the rate limit (service.h). Only a face that was
// seen and not recognized counts: someone else, or a photo. An empty camera view,
// darkness or a scan cancelled because a password was typed don't.
using Clock = std::chrono::steady_clock;
struct Attempts {
    std::vector<Clock::time_point> failures; // within the window
    Clock::time_point lockedUntil{};
    Clock::time_point keepUntil{}; // forgettable after this
};
std::map<uid_t, Attempts> attempts;

struct RateLimit {
    int maxFailures; // 0: no limit
    std::chrono::minutes window;
    std::chrono::minutes lockout;
    explicit RateLimit(const Config &config)
        : maxFailures(std::max(0, int(config.number("core/max_failures", 5))))
        , window(std::max(1, int(config.number("core/failure_window", 15))))
        , lockout(std::max(1, int(config.number("core/lockout_minutes", 10))))
    {
    }
};

void forgetExpired()
{
    const auto now = Clock::now();
    std::erase_if(attempts, [now](const auto &entry) {
        return entry.second.keepUntil <= now;
    });
}

bool lockedOut(uid_t uid, const RateLimit &limit)
{
    forgetExpired();
    const auto found = attempts.find(uid);
    return limit.maxFailures > 0 && found != attempts.end() && Clock::now() < found->second.lockedUntil;
}

void recordResult(uid_t uid, const std::string &user, const ScanResult &result, const RateLimit &limit)
{
    if (result.code == Recognized) {
        attempts.erase(uid);
        return;
    }
    if (limit.maxFailures == 0 || result.code != Timeout || result.stats.faceFrames == 0) {
        return;
    }
    const auto now = Clock::now();
    Attempts &record = attempts[uid];
    std::erase_if(record.failures, [&](Clock::time_point time) {
        return time + limit.window <= now;
    });
    record.failures.push_back(now);
    record.keepUntil = now + limit.window;
    if (int(record.failures.size()) >= limit.maxFailures) {
        record.failures.clear();
        record.lockedUntil = now + limit.lockout;
        record.keepUntil = record.lockedUntil;
        std::cerr << "kenning daemon: face unlock locked for " << user << " for " << limit.lockout.count() << " minutes after "
                  << limit.maxFailures << " unrecognized faces\n";
    }
}

// Keeps the newest photos of faces that weren't recognized, readable only by this
// service; System Settings shows them through its root helper.
constexpr int maxSnapshots = 20;

void saveSnapshot(const std::string &user, const cv::Mat &image)
{
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path dir = fs::path(snapshotsDir) / user;
    fs::create_directories(dir, error);
    fs::permissions(dir, fs::perms::owner_all, error);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const fs::path file = dir / (std::to_string(seconds) + ".jpg");
    if (!cv::imwrite(file.string(), image, {cv::IMWRITE_JPEG_QUALITY, 85})) {
        std::cerr << "kenning daemon: can't save " << file << '\n';
        return;
    }
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, error);

    std::vector<fs::path> photos;
    for (const auto &entry : fs::directory_iterator(dir, error)) {
        if (entry.path().extension() == ".jpg") {
            photos.push_back(entry.path());
        }
    }
    // Named by time, so the oldest sort first.
    std::sort(photos.begin(), photos.end());
    for (size_t i = 0; i + maxSnapshots < photos.size(); ++i) {
        fs::remove(photos[i], error);
    }
}

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
        if (!std::filesystem::exists(modelPath(user))) {
            return reply(client, NoModel);
        }
        return reply(client, lockedOut(account->pw_uid, RateLimit(Config())) ? LockedOut : Recognized);
    }
    if (command == "compare") {
        // Config is read per request so settings changes apply without a restart.
        // Stop when the client hangs up (the lock screen restarting its check, PAM
        // giving up), so the next request doesn't wait for an abandoned scan.
        const auto hungUp = [client] {
            pollfd state{client, POLLRDHUP, 0};
            return poll(&state, 1, 0) > 0 && (state.revents & (POLLRDHUP | POLLHUP | POLLERR));
        };
        // A broken install (unreadable networks, a camera driver error) must answer
        // "failed" so PAM falls back to the password, not take the service down.
        const Config config;
        const RateLimit limit(config);
        if (lockedOut(account->pw_uid, limit)) {
            return reply(client, LockedOut);
        }
        try {
            const ScanResult result = compare(config, user, hungUp);
            recordResult(account->pw_uid, user, result, limit);
            if (result.code == Timeout && !result.snapshot.empty()) {
                saveSnapshot(user, result.snapshot);
            }
            return reply(client, result.code);
        } catch (const std::exception &error) {
            std::cerr << "kenning daemon: scan failed: " << error.what() << '\n';
            return reply(client, Error);
        }
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
            // Exiting would forget failed attempts and lockouts: stay until they expire.
            forgetExpired();
            if (attempts.empty()) {
                return 0;
            }
            continue;
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
