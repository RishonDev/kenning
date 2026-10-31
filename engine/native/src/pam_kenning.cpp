/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// pam_kenning: face authentication. The recognition runs in the face unlock
// service (kenning daemon, see service.h), which owns the camera and the
// root-only face models, so OpenCV never loads into the program asking for
// authentication. Messages match pam_howdy, which the login and lock screens use
// to drive the face scan animation.

#include "config.h"
#include "service.h"

#include <security/pam_ext.h>
#include <security/pam_modules.h>

#ifdef KENNING_HAVE_LOGIND
#include <systemd/sd-login.h>
#endif

#include <cstring>
#include <filesystem>
#include <fstream>
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

// True when the person asking may not be at this computer, so the camera would
// look at someone else (possibly the real user, who would then approve a remote
// sudo without knowing). The environment alone can't decide this: sudo passes the
// caller's, and SSH_* can simply be unset. A process inside a logind session (an
// SSH login is one) is judged by that session; the variables can only add a
// refusal.
//
// Not covered, and not coverable here: on Plasma 6 terminals and apps run under the
// user's service manager outside any session, like anything an SSH login starts
// with systemd-run --user. Code already running as the user can therefore always
// get a request started from the desktop; the sudo warning in System Settings says so.
bool remoteSession(pam_handle_t *pamh)
{
    const void *rhost = nullptr;
    if (pam_get_item(pamh, PAM_RHOST, &rhost) == PAM_SUCCESS && rhost && *static_cast<const char *>(rhost)
        && std::strcmp(static_cast<const char *>(rhost), "localhost") != 0) {
        return true;
    }
    if (std::getenv("SSH_CONNECTION") || std::getenv("SSH_CLIENT") || std::getenv("SSH_TTY")) {
        return true;
    }
#ifdef KENNING_HAVE_LOGIND
    // The login session of the program asking (sudo, the lock screen, ...).
    char *session = nullptr;
    if (sd_pid_get_session(0, &session) >= 0 && session) {
        const int remote = sd_session_is_remote(session);
        std::free(session);
        return remote != 0; // remote, or logind can't tell
    }
#endif
    return false;
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
    // Quick check first, so users without a face model never see the scan start.
    const int hasModel = FaceUnlock::askService("has-model", user, FaceUnlock::quickTimeoutSeconds);
    if (hasModel < 0) {
        pam_syslog(pamh, LOG_ERR, "Face unlock service not reachable at %s", FaceUnlock::socketPath);
        return PAM_AUTHINFO_UNAVAIL;
    }
    if (hasModel == FaceUnlock::LockedOut) {
        say(pamh, "Failure, too many attempts; use your password");
        return PAM_AUTHINFO_UNAVAIL;
    }
    if (hasModel != FaceUnlock::Recognized) {
        return PAM_AUTHINFO_UNAVAIL;
    }

    if (config.boolean("core/detection_notice", true)) {
        say(pamh, "Attempting facial authentication");
    }

    int code = FaceUnlock::askService("compare", user, FaceUnlock::scanTimeoutSeconds(config));
    if (code < 0) {
        code = FaceUnlock::Error;
    }
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
    case FaceUnlock::LockedOut:
        say(pamh, "Failure, too many attempts; use your password");
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
