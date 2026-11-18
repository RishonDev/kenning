/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// kenning daemon: face checks for pam_kenning, so the face models stay root-only.

#include "daemon.h"
#include "engine.h"
#include "failurelimiter.h"
#include "service.h"
#include "signedin.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <poll.h>
#include <pwd.h>
#include <sys/stat.h>
#include <systemd/sd-login.h>
#include <vector>

namespace FaceUnlock
{
namespace
{
// Socket activated: exits after this long without a request.
constexpr int idleExitMs = 5 * 60 * 1000;
// A request must arrive whole within this time, however slowly it trickles in. Requests
// are read side by side, so a slow client only holds up itself.
constexpr auto requestDeadline = std::chrono::seconds(2);
constexpr int replyTimeoutSeconds = 2;
constexpr std::size_t maxRequestLength = 255;
// Connections still sending their request: per user, and in all.
constexpr std::size_t maxPendingPerUser = 4;
constexpr std::size_t maxPending = 64;
// In /run like pam_faillock's records: kept across service restarts, cleared at reboot.
constexpr const char *failuresPath = "/run/kenning/failures";
constexpr const char *signedInPath = "/run/kenning/signed-in";

void reply(int client, int code)
{
    const std::string text = std::to_string(code) + '\n';
    send(client, text.data(), text.size(), MSG_NOSIGNAL);
}

// A local login of this user: the login screen refuses face unlock until there is one,
// so it was made with a password (or fingerprint, or autologin).
bool hasLocalLogin(uid_t uid)
{
    char **sessions = nullptr;
    const int count = sd_uid_get_sessions(uid, 0, &sessions);
    bool found = false;
    for (int i = 0; i < count; ++i) {
        char *sessionClass = nullptr;
        if (!found && sd_session_is_remote(sessions[i]) == 0 && sd_session_get_class(sessions[i], &sessionClass) >= 0) {
            found = std::strcmp(sessionClass, "user") == 0;
        }
        std::free(sessionClass);
        std::free(sessions[i]);
    }
    std::free(sessions);
    return found;
}

bool clientHungUp(int client)
{
    pollfd state{client, POLLRDHUP, 0};
    return poll(&state, 1, 0) > 0 && (state.revents & (POLLRDHUP | POLLHUP | POLLERR));
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

// For running by hand: create the socket ourselves.
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

using Clock = std::chrono::steady_clock;

struct PendingClient {
    int fd = -1;
    ucred peer{};
    Clock::time_point deadline;
    std::string buffer;
    bool complete = false; // a newline arrived
    bool done = false; // complete, hung up, or out of room
};

// Connections whose request hasn't fully arrived, read without blocking.
class PendingClients
{
public:
    const std::vector<PendingClient> &clients() const
    {
        return m_clients;
    }

    void accept(int listener, Clock::time_point now)
    {
        const int fd = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) {
            return;
        }
        PendingClient client{.fd = fd, .peer = {}, .deadline = now + requestDeadline, .buffer = {}};
        socklen_t peerSize = sizeof(client.peer);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &client.peer, &peerSize) != 0 || m_clients.size() >= maxPending
            || pendingFrom(client.peer.uid) >= maxPendingPerUser) {
            close(fd);
            return;
        }
        m_clients.push_back(std::move(client));
    }

    void read(int fd)
    {
        const auto found = std::find_if(m_clients.begin(), m_clients.end(), [fd](const PendingClient &client) {
            return client.fd == fd;
        });
        if (found == m_clients.end() || found->done) {
            return;
        }
        char chunk[maxRequestLength];
        for (;;) {
            const std::size_t room = maxRequestLength - found->buffer.size();
            const ssize_t count = recv(fd, chunk, room, 0);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return;
            }
            if (count <= 0) {
                found->done = true;
                return;
            }
            found->buffer.append(chunk, std::size_t(count));
            if (found->buffer.find('\n') != std::string::npos) {
                found->complete = found->done = true;
                return;
            }
            if (found->buffer.size() >= maxRequestLength) {
                found->done = true;
                return;
            }
        }
    }

    // Removes and returns the clients to answer now, made blocking again.
    std::vector<PendingClient> takeFinished(Clock::time_point now)
    {
        std::vector<PendingClient> finished;
        std::erase_if(m_clients, [&](PendingClient &client) {
            if (!client.done && now < client.deadline) {
                return false;
            }
            fcntl(client.fd, F_SETFL, fcntl(client.fd, F_GETFL) & ~O_NONBLOCK);
            finished.push_back(std::move(client));
            return true;
        });
        return finished;
    }

    int millisecondsUntilNextDeadline(Clock::time_point now) const
    {
        auto next = m_clients.front().deadline;
        for (const PendingClient &client : m_clients) {
            next = std::min(next, client.deadline);
        }
        return int(std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count() + 1));
    }

private:
    std::size_t pendingFrom(uid_t uid) const
    {
        return std::size_t(std::count_if(m_clients.cbegin(), m_clients.cend(), [uid](const PendingClient &client) {
            return client.peer.uid == uid;
        }));
    }

    std::vector<PendingClient> m_clients;
};

class Service
{
public:
    Service()
    {
        restoreFailures();
        std::ifstream file(signedInPath);
        m_signedIn.restore(std::string(std::istreambuf_iterator<char>(file), {}));
    }

    // client is blocking again, with the request read in full.
    void serve(int client, const ucred &peer, const std::optional<Request> &request)
    {
        const timeval timeout{replyTimeoutSeconds, 0};
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (!request) {
            return reply(client, Aborted);
        }
        const passwd *account = request->user.find('/') == std::string::npos ? getpwnam(request->user.c_str()) : nullptr;
        if (!account) {
            return reply(client, Aborted);
        }
        const bool needsPassword = Config().boolean("core/password_after_boot", true) && !signedInSinceBoot(account->pw_uid);
        // Asked by the login screen so it can skip the face scan after a restart. Open to anyone:
        // who has signed in since boot is public already (last, loginctl).
        if (request->command == "password-first") {
            return reply(client, needsPassword ? NeedsPassword : Recognized);
        }
        // Only root, or the user asking about their own face.
        if (peer.uid != 0 && peer.uid != account->pw_uid) {
            return reply(client, Aborted);
        }
        // From pam_kenning when PAM ends: a typed password was wrong. Counted even before the
        // first sign-in after a restart, as pam_faillock would.
        if (request->command == "password-failed") {
            recordFailure(request->user, account->pw_uid, FailureLimiter::Kind::Password, FailureLimits::fromConfig(Config()));
            return reply(client, Recognized);
        }

        if (needsPassword) {
            return reply(client, NeedsPassword);
        }
        if (request->command == "has-model") {
            return reply(client, hasModel(request->user, account->pw_uid));
        }
        if (request->command == "compare") {
            return reply(client, compareFace(client, request->user, account->pw_uid));
        }
        reply(client, Aborted);
    }

    bool canExit()
    {
        // Exiting would forget failed attempts and lockouts.
        return m_failures.isEmpty(FailureLimiter::Clock::now());
    }

private:
    bool signedInSinceBoot(uid_t uid)
    {
        if (m_signedIn.contains(uid)) {
            return true;
        }
        if (!hasLocalLogin(uid)) {
            return false;
        }
        // Remembered so face unlock still works on the login screen after signing out.
        m_signedIn.add(uid);
        const std::string temporary = std::string(signedInPath) + ".new";
        std::ofstream(temporary) << m_signedIn.save();
        std::rename(temporary.c_str(), signedInPath);
        return true;
    }

    int hasModel(const std::string &user, uid_t uid)
    {
        if (!std::filesystem::exists(modelPath(user))) {
            return NoModel;
        }
        return m_failures.isLockedOut(uid, FailureLimits::fromConfig(Config()), FailureLimiter::Clock::now()) ? LockedOut : Recognized;
    }

    int compareFace(int client, const std::string &user, uid_t uid)
    {
        // Read per request so settings changes apply without a restart.
        const Config config;
        const FailureLimits limits = FailureLimits::fromConfig(config);
        if (m_failures.isLockedOut(uid, limits, FailureLimiter::Clock::now())) {
            return LockedOut;
        }
        // A broken install must answer "failed", so PAM falls back to the password.
        try {
            const ScanResult result = compare(config, user, [client] {
                return clientHungUp(client);
            });
            recordResult(user, uid, result, limits);
            return result.code;
        } catch (const std::exception &error) {
            std::cerr << "kenning daemon: scan failed: " << error.what() << '\n';
            return Error;
        }
    }

    void restoreFailures()
    {
        std::ifstream file(failuresPath);
        m_failures.restore(std::string(std::istreambuf_iterator<char>(file), {}));
    }

    void saveFailures()
    {
        const std::string temporary = std::string(failuresPath) + ".new";
        std::ofstream(temporary) << m_failures.save();
        std::rename(temporary.c_str(), failuresPath);
    }

    void recordResult(const std::string &user, uid_t uid, const ScanResult &result, const FailureLimits &limits)
    {
        if (result.code == Recognized) {
            m_failures.recordSuccess(uid);
            saveFailures();
            return;
        }
        // A face that was seen and not recognized counts, however the scan ended: a client
        // that hangs up just before the timeout must not get unlimited tries.
        if (result.stats.faceFrames > 0) {
            recordFailure(user, uid, FailureLimiter::Kind::Face, limits);
        }
    }

    void recordFailure(const std::string &user, uid_t uid, FailureLimiter::Kind kind, const FailureLimits &limits)
    {
        if (m_failures.recordFailure(uid, kind, limits, FailureLimiter::Clock::now())) {
            std::cerr << "kenning daemon: face unlock locked for " << user << " for " << limits.lockout.count() << " minutes after " << limits.maxFailures
                      << " unrecognized faces and " << limits.maxPasswordFailures << " wrong passwords\n";
        }
        saveFailures();
    }

    FailureLimiter m_failures;
    SignedInUsers m_signedIn;
};
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

    Service service;
    PendingClients pending;
    auto idleSince = Clock::now();
    for (;;) {
        std::vector<pollfd> waiting{{listener, POLLIN, 0}};
        for (const PendingClient &client : pending.clients()) {
            waiting.push_back({client.fd, POLLIN, 0});
        }
        int timeoutMs = -1;
        if (!pending.clients().empty()) {
            timeoutMs = pending.millisecondsUntilNextDeadline(Clock::now());
        } else if (activated) {
            timeoutMs = int(std::max<long long>(0, idleExitMs - std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - idleSince).count()));
        }
        const int ready = poll(waiting.data(), waiting.size(), timeoutMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Error;
        }
        const auto now = Clock::now();
        if (ready == 0 && pending.clients().empty()) {
            if (service.canExit()) {
                return 0;
            }
            idleSince = now;
            continue;
        }

        for (std::size_t i = 1; i < waiting.size(); ++i) {
            if (waiting[i].revents) {
                pending.read(waiting[i].fd);
            }
        }
        // Whole requests, then ones out of time (answered "aborted"), oldest first.
        for (PendingClient &client : pending.takeFinished(now)) {
            service.serve(client.fd, client.peer, client.complete ? parseRequest(client.buffer) : std::nullopt);
            close(client.fd);
        }
        if (waiting[0].revents & POLLIN) {
            pending.accept(listener, now);
        }
        idleSince = Clock::now();
    }
}
}
