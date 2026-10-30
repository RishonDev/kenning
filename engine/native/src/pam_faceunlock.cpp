/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// pam_faceunlock: face authentication. The recognition runs in a separate
// process (faceunlock compare USER) so OpenCV never loads into the program
// asking for authentication. Messages match pam_howdy, which the login and lock
// screens use to drive the face scan animation.

#include "config.h"

#include <fcntl.h>

#include <security/pam_ext.h>
#include <security/pam_modules.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <sys/wait.h>
#include <syslog.h>

namespace
{
using FaceUnlock::Config;

void say(pam_handle_t *pamh, const std::string &message)
{
    pam_info(pamh, "%s", message.c_str());
}

bool lidClosed()
{
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator("/proc/acpi/button/lid", error)) {
        std::ifstream state(entry.path() / "state");
        std::string line;
        if (std::getline(state, line) && line.find("closed") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool remoteSession(pam_handle_t *pamh)
{
    const void *rhost = nullptr;
    if (pam_get_item(pamh, PAM_RHOST, &rhost) == PAM_SUCCESS && rhost && *static_cast<const char *>(rhost)) {
        return std::strcmp(static_cast<const char *>(rhost), "localhost") != 0;
    }
    return std::getenv("SSH_CONNECTION") || std::getenv("SSH_CLIENT") || std::getenv("SSH_TTY");
}

int runCompare(const char *user)
{
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    // A fixed environment: the caller's (e.g. sudo's) is under the user's control.
    const char *argv[] = {FACEUNLOCK_BINARY, "compare", user, nullptr};
    const char *envp[] = {"PATH=/usr/bin", "LANG=C.UTF-8", nullptr};
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, FACEUNLOCK_BINARY, &actions, nullptr, const_cast<char **>(argv), const_cast<char **>(envp));
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) {
        return FaceUnlock::Error;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    return WIFEXITED(status) ? WEXITSTATUS(status) : FaceUnlock::Aborted;
}
}

extern "C" {

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int, int, const char **)
{
    // Set by the login manager while checking a password the user typed, so a
    // failed face scan isn't repeated before the password is checked.
    if (const char *skip = pam_getenv(pamh, "FACEUNLOCK_SKIP"); skip && std::strcmp(skip, "1") == 0) {
        return PAM_AUTHINFO_UNAVAIL;
    }

    const Config config;
    if (config.boolean("core/disabled", false)) {
        return PAM_AUTHINFO_UNAVAIL;
    }
    if (config.boolean("core/abort_if_ssh", true) && remoteSession(pamh)) {
        return PAM_AUTHINFO_UNAVAIL;
    }
    if (config.boolean("core/abort_if_lid_closed", true) && lidClosed()) {
        return PAM_AUTHINFO_UNAVAIL;
    }

    const char *user = nullptr;
    if (pam_get_user(pamh, &user, nullptr) != PAM_SUCCESS || !user || !*user) {
        return PAM_USER_UNKNOWN;
    }
    if (!std::filesystem::exists(FaceUnlock::modelPath(user))) {
        return PAM_AUTHINFO_UNAVAIL;
    }

    if (config.boolean("core/detection_notice", true)) {
        say(pamh, "Attempting facial authentication");
    }

    const int code = runCompare(user);
    switch (code) {
    case FaceUnlock::Recognized:
        if (!config.boolean("core/no_confirmation", false)) {
            say(pamh, std::string("Identified face as ") + user);
        }
        pam_syslog(pamh, LOG_INFO, "Login approved");
        return PAM_SUCCESS;
    case FaceUnlock::NoModel:
        say(pamh, "Failure, no face model known");
        return PAM_AUTHINFO_UNAVAIL;
    case FaceUnlock::Timeout:
        say(pamh, "Failure, timeout reached");
        break;
    case FaceUnlock::TooDark:
        say(pamh, "Failure, image too dark");
        break;
    default:
        say(pamh, "Failure, general abort");
        break;
    }
    pam_syslog(pamh, LOG_NOTICE, "Face not recognized (code %d)", code);
    return PAM_AUTH_ERR;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *, int, int, const char **)
{
    return PAM_IGNORE;
}
}
