#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: GPL-2.0-or-later
# Adds face unlock to the Plasma lock screen: the face scan animation (style chosen
# in System Settings > Face Unlock), skipping the "Unlock" button after face
# recognition, and a pause before unlocking. Run by a pacman hook after
# plasma-desktop updates. All edits apply together or not at all, so a changed
# upstream file stays stock.
#
# usage: kenning-lockscreen.py [lockscreen dir] [FaceScanIndicator.qml]
#        kenning-lockscreen.py --restore [lockscreen dir]   put the stock files back

import glob
import os
import shutil
import subprocess
import sys
from pathlib import Path

# MARKER tags every patched file; VERSION changes whenever the edits do, so an
# older patch is replaced by re-patching the saved original (see ORIGINALS).
MARKER = "kenning lock screen patch"
# Markers of earlier releases, so files they patched are re-patched from the original.
LEGACY_MARKERS = ("lazysetup face unlock", "plasma-faceunlock lock screen patch")
VERSION = "face unlock patch 10"

RESTORE = len(sys.argv) > 1 and sys.argv[1] == "--restore"
if RESTORE:
    sys.argv.pop(1)

lockscreen = Path(sys.argv[1] if len(sys.argv) > 1 else
                  "/usr/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen")  # see SYSTEM_LOCKSCREEN
indicator = Path(sys.argv[2] if len(sys.argv) > 2 else Path(__file__).with_name("FaceScanIndicator.qml"))

CONFIG_XML = ("  </group>", f"""    <!-- {MARKER}, {VERSION} -->
    <entry name="faceUnlockShowButton" type="Bool">
      <label>Ask for an Unlock button press after face recognition.</label>
      <default>false</default>
    </entry>
    <entry name="faceAnimation" type="String">
      <label>Face scan animation: hello, ring or text.</label>
      <default>hello</default>
    </entry>
    <entry name="unlockDelay" type="Int">
      <label>Milliseconds to show a greeting before unlocking after face recognition.</label>
      <default>500</default>
      <min>0</min>
      <max>3000</max>
    </entry>
    <entry name="preferredMethod" type="String">
      <label>Sign-in method to start with: face or fingerprint; empty for the one used last.</label>
      <default></default>
    </entry>
    <entry name="useLastMethod" type="Bool">
      <label>Start with the sign-in method used last instead of preferredMethod.</label>
      <default>false</default>
    </entry>
  </group>""")

CONFIG_QML = ("""        KCM.SettingHighlighter {
            highlight: configForm.cfg_showMediaControlsDefault != configForm.cfg_showMediaControls
        }
    }
""", f"""        KCM.SettingHighlighter {{
            highlight: configForm.cfg_showMediaControlsDefault != configForm.cfg_showMediaControls
        }}
    }}

    // {MARKER}, {VERSION}
    property alias cfg_faceUnlockShowButton: faceUnlockShowButton.checked
    property bool cfg_faceUnlockShowButtonDefault: false
    property alias cfg_unlockDelay: unlockDelay.value
    property int cfg_unlockDelayDefault: 500

    QQC2.CheckBox {{
        id: faceUnlockShowButton
        Kirigami.FormData.label: i18nc("@title: group", "Face unlock:")
        text: i18nc("@option:check", "Show an Unlock button after recognizing me")

        KCM.SettingHighlighter {{
            highlight: configForm.cfg_faceUnlockShowButtonDefault != configForm.cfg_faceUnlockShowButton
        }}
    }}

    QQC2.SpinBox {{
        id: unlockDelay
        Kirigami.FormData.label: i18nc("@label:spinbox", "Pause before unlocking:")
        enabled: !faceUnlockShowButton.checked
        from: 0
        to: 3000
        stepSize: 100
        textFromValue: (value, locale) => i18nc("@item:valuesuffix milliseconds", "%1 ms", value)
        valueFromText: (text, locale) => parseInt(text)

        KCM.SettingHighlighter {{
            highlight: configForm.cfg_unlockDelayDefault != configForm.cfg_unlockDelay
        }}
    }}
""")

UI_SUCCEEDED = ("""            if (authenticator.hadPrompt) {
                Qt.quit();
            } else {
                mainStack.replace(null, Qt.resolvedUrl("NoPasswordUnlock.qml"),""",
"""            if (authenticator.hadPrompt) {
                Qt.quit();
            } else if (!config.faceUnlockShowButton) {
                lockScreenUi.finishFaceUnlock();
            } else {
                mainStack.replace(null, Qt.resolvedUrl("NoPasswordUnlock.qml"),""")

UI_FUNCTION = ("""    property string pendingPassword
""", f"""    property string pendingPassword

    // {MARKER}, {VERSION}: face scan progress for FaceScanIndicator (0 idle, 1 scanning,
    // 2 recognized, 3 failed), fed by pam_howdy's messages.
    property int faceState: 0
    property string faceMessage
    // False after a failure a new scan can't fix (lockout, no face model).
    property bool faceRetryable: true

    // Sign-in method to start with, as authenticator type: 3 Plasma's Face method (Plasma
    // 6.8 and newer with Kenning), 1 fingerprint (System Settings > Face Unlock, when a
    // fingerprint is enrolled), or -1 for the one used last, as Plasma does by default.
    // A method that isn't offered falls back to the one used last.
    readonly property int preferredMethod: config.useLastMethod ? -1
                                           : config.preferredMethod === "fingerprint" ? 1 : 3
    // Whether Plasma offers its Face method and it works (a face model, the service up).
    property bool faceOffered: false
    // Asks the authenticator buttons to switch to the method of this type, if it works.
    signal switchMethod(int type)

    // Switches after the current authenticator's signal has returned: switching deletes it.
    function switchMethodLater(type) {{
        Qt.callLater(() => lockScreenUi.switchMethod(type));
    }}

    // Plasma starts the fingerprint reader before its PAM worker is ready and doesn't retry
    // until a key or the mouse wakes the screen; retry for a few seconds so a finger works
    // straight away. Plasma ignores the retry while authentication is already running.
    Timer {{
        property int tries: 0
        interval: 500
        repeat: true
        running: lockScreenUi.preferredMethod === 1
        onTriggered: {{
            if (++tries > 6 || authenticator.authenticator !== 1) {{
                stop();
            }} else if (!authenticator.busy) {{
                authenticator.startAuthenticating();
            }}
        }}
    }}

    // Fingerprint first: when it fails or stops working, fall back to the face (Plasma's
    // Face method, or the password, which scans the face first where face unlock lives
    // there), and the password.
    function fingerprintFallback() {{
        Qt.callLater(() => {{
            lockScreenUi.switchMethod(lockScreenUi.faceOffered ? 3 : 0);
            lockScreenUi.handleMessage(i18nc("@info:status", "Fingerprint not recognized. Look at the camera or type your password."));
        }});
    }}

    // Plasma's Face method failed: fall back to the password (and fingerprint, which
    // Plasma runs beside it) instead of Plasma's immediate retry; waking the screen
    // scans again.
    function faceFallback() {{
        if (faceState !== 3) {{
            handleFaceMessage("Failure, general abort");
        }}
        faceNeedsRetry = true;
        switchMethodLater(0);
    }}

    // Returns true when the message belonged to face unlock and is shown by the indicator.
    function handleFaceMessage(msg) {{
        if (msg.startsWith("Attempting facial authentication")) {{
            faceState = 1;
            faceRetryable = true;
            faceMessage = i18nc("@info:status", "Looking for you…");
            lockScreenRoot.uiVisible = true;
            return true;
        }}
        if (msg.startsWith("Identified face as")) {{
            faceState = 2;
            faceMessage = i18nc("@info:status", "Welcome back");
            return true;
        }}
        if (/^(Failure, |Face detection image too dark)/.test(msg)) {{
            faceState = 3;
            faceRetryable = !msg.includes("too many attempts") && !msg.includes("no face model");
            faceMessage = msg.includes("too dark") ? i18nc("@info:status", "Too dark or bright to see you")
                        : msg.includes("too many attempts") ? i18nc("@info:status", "Too many attempts. Use your password.")
                        : msg.includes("no face model") ? i18nc("@info:status", "Face unlock isn't set up")
                        : i18nc("@info:status", "Couldn't recognize you");
            faceResetTimer.restart();
            faceNeedsRetry = true;
            return true;
        }}
        return false;
    }}

    Timer {{
        id: faceResetTimer
        interval: 3000
        onTriggered: lockScreenUi.faceState = 0
    }}

    // pam_howdy runs once per PAM conversation; after it fails the conversation waits
    // for a password, so a new scan needs a fresh conversation. Never while typing.
    property bool faceNeedsRetry: false

    function rescanFace() {{
        if (authenticator.unlocked || mainBlock.mainPasswordBox.text.length > 0) {{
            return;
        }}
        faceNeedsRetry = false;
        if (authenticator.authenticator === 0 && faceOffered) {{
            switchMethod(3);
            return;
        }}
        authenticator.stopAuthenticating();
        authenticator.startAuthenticating();
    }}

    // After resume, give the IR camera a moment to come back before scanning.
    Timer {{
        id: resumeRescanTimer
        interval: 1500
        onTriggered: lockScreenUi.rescanFace()
    }}

    Connections {{
        target: sessionManagement
        function onResumingFromSuspend() {{
            resumeRescanTimer.restart();
        }}
    }}

    // Unlock without the Unlock button, optionally showing the greeting first.
    function finishFaceUnlock() {{
        faceState = 2;
        faceMessage = i18nc("@info:status", "Welcome back");
        if (config.unlockDelay > 0) {{
            faceUnlockTimer.start();
        }} else {{
            Qt.quit();
        }}
    }}

    Timer {{
        id: faceUnlockTimer
        interval: config.unlockDelay
        onTriggered: Qt.quit()
    }}
""")

# The fallback goes before the stock failure handling, which would retry the fingerprint.
UI_FALLBACK = ("""        function onFailed(kind) {
            if (kind != 0) { // if this is coming from the noninteractive authenticators
                return;
            }
""", """        function onFailed(kind) {
            if (kind != 0) { // if this is coming from the noninteractive authenticators
                return;
            }
            if (authenticator.authenticator === 3) {
                lockScreenUi.faceFallback();
                return;
            }
            if (authenticator.authenticator === 1 && lockScreenUi.preferredMethod === 1) {
                lockScreenUi.fingerprintFallback();
                return;
            }
""")

# Start with the preferred method; otherwise, or when it isn't offered, with the one used last.
UI_METHOD = ("""                            Component.onCompleted: {
                                if (checked) {
                                    clicked()
                                }
                            }
""", """                            Component.onCompleted: {
                                if (type === 3) {
                                    lockScreenUi.faceOffered = functional
                                }
                                if (type === lockScreenUi.preferredMethod) {
                                    clicked()
                                } else if (checked) {
                                    // After every button exists, unless the preferred one took over.
                                    Qt.callLater(() => {
                                        if (authenticator.authenticator === type) {
                                            clicked()
                                        }
                                    })
                                }
                            }

                            Connections {
                                target: lockScreenUi
                                function onSwitchMethod(to) {
                                    if (to === type && functional) {
                                        clicked()
                                    }
                                }
                            }
""")

# A method that stops working falls back: Plasma's Face method (no face model, lid closed,
# service down) quietly to the password, a preferred fingerprint reader (unplugged,
# fprintd gone) to the face and password.
UI_DEFUNCT = ("""                            onFunctionalChanged: {
                                if (functional) {
                                    return
                                }
""", """                            onFunctionalChanged: {
                                if (functional) {
                                    return
                                }
                                if (type === 3) {
                                    lockScreenUi.faceOffered = false
                                    if (authenticator.authenticator === 3) {
                                        lockScreenUi.switchMethodLater(0)
                                        return
                                    }
                                }
                                if (type === 1 && authenticator.authenticator === 1 && lockScreenUi.preferredMethod === 1) {
                                    lockScreenUi.fingerprintFallback()
                                    return
                                }
""")

UI_FACE_SUCCEEDED = ("""            if (!lockScreenUi.expectingPrompt) {
                Qt.quit()
            }
""", """            // Plasma's Face method: the greeting and pause, or the Unlock button (System Settings).
            if (authenticator.authenticator === 3) {
                if (config.faceUnlockShowButton) {
                    mainStack.replace(null, Qt.resolvedUrl("NoPasswordUnlock.qml"), { userListModel: users }, StackView.Immediate);
                    mainStack.forceActiveFocus();
                } else {
                    lockScreenUi.finishFaceUnlock();
                }
                return;
            }
            if (!lockScreenUi.expectingPrompt) {
                Qt.quit()
            }
""")

UI_WAKE = ("""            } else if (uiVisible) {
                fadeoutTimer.restart();
            }
            authenticator.startAuthenticating();""", """            } else if (uiVisible) {
                fadeoutTimer.restart();
            }
            if (uiVisible && lockScreenUi.faceNeedsRetry) {
                lockScreenUi.rescanFace();
            }
            authenticator.startAuthenticating();""")

# A key that types text means the user is entering their password: drop the pending
# face retry so the keypress that wakes the screen doesn't start a new scan.
UI_KEYS = ("""        Keys.onPressed: event => {
            uiVisible = true;""", """        Keys.onPressed: event => {
            if (event.text.trim().length > 0) {
                lockScreenUi.faceNeedsRetry = false;
                // Typing while Plasma's Face method scans means a password: switch to it
                // (the password field is hidden until then), keeping the key.
                if (authenticator.authenticator === 3) {
                    lockScreenUi.faceState = 0;
                    lockScreenUi.switchMethod(0);
                    mainBlock.mainPasswordBox.text += event.text;
                    mainBlock.mainPasswordBox.forceActiveFocus();
                    uiVisible = true;
                    event.accepted = true;
                    return;
                }
            }
            uiVisible = true;""")

# A password submitted while the face scan runs has no prompt to answer yet (PAM is
# still in the face module); hold it until the password prompt arrives.
UI_PASSWORD = ("""                    if (authenticator.pamTimeout) {
                        lockScreenUi.pendingPassword = password""", """                    if (authenticator.pamTimeout || lockScreenUi.faceState === 1) {
                        lockScreenUi.pendingPassword = password""")

UI_PROMPT = ("""        function onPromptForSecretChanged() {
            mainBlock.showPassword = false;""", """        function onPromptForSecretChanged() {
            if (!authenticator.pamTimeout && lockScreenUi.pendingPassword.length > 0) {
                authenticator.respond(lockScreenUi.pendingPassword);
                lockScreenUi.pendingPassword = "";
                mainBlock.enabled = true;
                return;
            }
            mainBlock.showPassword = false;""")

UI_FAILED = ("""            const msg = i18ndc("plasma_shell_org.kde.plasma.desktop", "@info:status", "Unlocking failed");
            lockScreenUi.handleMessage(msg);""",
"""            if (lockScreenUi.faceState === 1) {
                lockScreenUi.handleFaceMessage("Failure, general abort");
            } else {
                const msg = i18ndc("plasma_shell_org.kde.plasma.desktop", "@info:status", "Unlocking failed");
                lockScreenUi.handleMessage(msg);
            }""")

UI_INFO = ("""        function onInfoMessageChanged() {
            lockScreenUi.handleMessage(authenticator.infoMessage);
        }

        function onErrorMessageChanged() {
            lockScreenUi.handleMessage(authenticator.errorMessage);
        }""",
"""        function onInfoMessageChanged() {
            if (!lockScreenUi.handleFaceMessage(authenticator.infoMessage)) {
                lockScreenUi.handleMessage(authenticator.infoMessage);
            }
        }

        function onErrorMessageChanged() {
            if (!lockScreenUi.handleFaceMessage(authenticator.errorMessage)) {
                lockScreenUi.handleMessage(authenticator.errorMessage);
            }
        }""")

# The indicator goes above the authenticator buttons, which only show with several methods.
UI_INDICATOR = ("""                authenticationTypeItem: RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    visible: authenticationTypeRepeater.count > 1
""", """                authenticationTypeItem: ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true

                    FaceScanIndicator {
                        faceState: lockScreenUi.faceState
                        message: lockScreenUi.faceMessage
                        style: config.faceAnimation || "hello"
                    }

                    PlasmaComponents3.Button {
                        Layout.alignment: Qt.AlignHCenter
                        visible: lockScreenUi.faceNeedsRetry && lockScreenUi.faceRetryable && lockScreenUi.faceState !== 1
                                 && mainBlock.mainPasswordBox.text.length === 0
                        icon.name: "view-refresh"
                        text: i18nc("@action:button", "Try Face Again")
                        onClicked: lockScreenUi.rescanFace()
                    }

                    RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    visible: authenticationTypeRepeater.count > 1
""")

UI_INDICATOR_END = ("""                    }
                }

                notificationMessage: {""", """                    }
                }
                }

                notificationMessage: {""")

# Plasma before 6.8: face unlock runs in the password service, before its prompt.
# Most of the 6.8 edits fit as they are: what they add for Plasma's Face method and
# fingerprint switching checks authenticator.authenticator, which doesn't exist there,
# so it stays idle. These take the place of the ones that don't fit.

# The functions go before the theme settings; pendingPassword is ours there.
UI_FUNCTION_BEFORE_6_8 = ("""    Kirigami.Theme.inherit: false
""", UI_FUNCTION[1] + """
    Kirigami.Theme.inherit: false
""")

# Hold a password submitted while the face scan runs (PAM is still in the face module,
# with no prompt to answer yet), and put the scan animation under the password field.
UI_PASSWORD_BEFORE_6_8 = ("""                onPasswordResult: password => {
                    authenticator.respond(password)
                }
""", """                onPasswordResult: password => {
                    if (lockScreenUi.faceState === 1) {
                        lockScreenUi.pendingPassword = password;
                        return;
                    }
                    authenticator.respond(password)
                }

                FaceScanIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    faceState: lockScreenUi.faceState
                    message: lockScreenUi.faceMessage
                    style: config.faceAnimation || "hello"
                }

                PlasmaComponents3.Button {
                    Layout.alignment: Qt.AlignHCenter
                    visible: lockScreenUi.faceNeedsRetry && lockScreenUi.faceRetryable && lockScreenUi.faceState !== 1
                             && mainBlock.mainPasswordBox.text.length === 0
                    icon.name: "view-refresh"
                    text: i18nc("@action:button", "Try Face Again")
                    onClicked: lockScreenUi.rescanFace()
                }
""")

UI_PROMPT_BEFORE_6_8 = ("""        function onPromptForSecretChanged(msg) {
            mainBlock.showPassword = false;""", """        function onPromptForSecretChanged(msg) {
            if (lockScreenUi.pendingPassword.length > 0) {
                authenticator.respond(lockScreenUi.pendingPassword);
                lockScreenUi.pendingPassword = "";
                return;
            }
            mainBlock.showPassword = false;""")

# Plasma 6.3 to 6.5: an older settings dialog (no configForm id) and message call.
CONFIG_QML_6_3 = (CONFIG_QML[0].replace("configForm.", ""), CONFIG_QML[1].replace("configForm.", ""))
_I18N_6_8 = 'i18ndc("plasma_shell_org.kde.plasma.desktop", "@info:status", '
_I18N_6_3 = 'i18nd("plasma_shell_org.kde.plasma.desktop", '
UI_FAILED_6_3 = (UI_FAILED[0].replace(_I18N_6_8, _I18N_6_3), UI_FAILED[1].replace(_I18N_6_8, _I18N_6_3))

_UI_BEFORE_6_8 = [UI_SUCCEEDED, UI_FUNCTION_BEFORE_6_8, UI_INFO, UI_WAKE, UI_KEYS, UI_PASSWORD_BEFORE_6_8,
                  UI_PROMPT_BEFORE_6_8, UI_FALLBACK]

# Edit sets by Plasma version, newest first; the first whose edits all fit is used.
EDIT_SETS = [
    ("6.8", {
        "config.xml": [CONFIG_XML],
        "config.qml": [CONFIG_QML],
        "LockScreenUi.qml": [UI_SUCCEEDED, UI_FUNCTION, UI_FAILED, UI_INFO, UI_INDICATOR, UI_INDICATOR_END, UI_WAKE, UI_KEYS,
                             UI_PASSWORD, UI_PROMPT, UI_FALLBACK, UI_METHOD, UI_DEFUNCT, UI_FACE_SUCCEEDED],
    }),
    ("6.6", {
        "config.xml": [CONFIG_XML],
        "config.qml": [CONFIG_QML],
        "LockScreenUi.qml": _UI_BEFORE_6_8 + [UI_FAILED],
    }),
    ("6.3", {
        "config.xml": [CONFIG_XML],
        "config.qml": [CONFIG_QML_6_3],
        "LockScreenUi.qml": _UI_BEFORE_6_8 + [UI_FAILED_6_3],
    }),
]
FILES = list(EDIT_SETS[0][1])


SYSTEM_LOCKSCREEN = "/usr/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen"


def is_patched(text: str) -> bool:
    return MARKER in text or any(marker in text for marker in LEGACY_MARKERS)


# Unpatched copies, saved before the first patch. When the distribution updates
# Plasma, its package manager writes fresh unpatched files, which replace these.
ORIGINALS = Path(os.environ.get("KENNING_ORIGINALS", "/var/lib/kenning/lockscreen-originals"))


def original(name: str) -> str | None:
    """The unpatched file: the copy saved before patching, or on Arch, from the
    package cache (installs patched by releases that didn't save copies)."""
    saved = ORIGINALS / name
    if saved.is_file():
        return saved.read_text()
    if not shutil.which("pacman"):
        return None
    owner = subprocess.run(["pacman", "-Qo", f"{SYSTEM_LOCKSCREEN}/{name}"], capture_output=True, text=True).stdout.split()
    # "<path> is owned by <package> <version>"
    if len(owner) < 2:
        return None
    package_name, version = owner[-2], owner[-1]
    member = f"{SYSTEM_LOCKSCREEN.lstrip('/')}/{name}"
    for package in glob.glob(f"/var/cache/pacman/pkg/{package_name}-{version}-*.pkg.tar.*"):
        if package.endswith(".sig"):
            continue
        result = subprocess.run(["bsdtar", "-xOf", package, member], capture_output=True, text=True)
        if result.returncode == 0 and result.stdout:
            return result.stdout
    return None


def restore() -> int:
    for name in FILES:
        if not is_patched((lockscreen / name).read_text()):
            continue
        text = original(name)
        if text is None:
            print(f"kenning lock screen: no saved original of {name}; reinstall the package that provides it "
                  "(plasma-desktop, or plasma-desktop-data on Debian and Ubuntu)", file=sys.stderr)
            continue
        (lockscreen / name).write_text(text)
        print(f"kenning lock screen: restored {name}")
    (lockscreen / "FaceScanIndicator.qml").unlink(missing_ok=True)
    shutil.rmtree(ORIGINALS, ignore_errors=True)
    return 0


def main() -> int:
    if not (lockscreen / "LockScreenUi.qml").is_file():
        print(f"kenning lock screen: no Plasma lock screen in {lockscreen}; nothing to do")
        return 0
    if RESTORE:
        return restore()
    current = {name: (lockscreen / name).read_text() for name in FILES}
    if all(VERSION in text for text in current.values()):
        print("kenning lock screen: already patched")
        edits = None
    else:
        stock = {}
        for name, text in current.items():
            if is_patched(text):
                text = original(name)
                if text is None:
                    print(f"kenning lock screen: {name} has an older patch and no saved original; reinstall the "
                          "package that provides it to update it", file=sys.stderr)
                    return 0
            stock[name] = text
        # All or nothing: a Plasma version none of the edit sets fits keeps its stock
        # lock screen, where face unlock still works, without the animation.
        fits = [(version, edits) for version, edits in EDIT_SETS
                if all(stock[name].count(old) == 1 for name, file_edits in edits.items() for old, _ in file_edits)]
        if not fits:
            print("kenning lock screen: LockScreenUi.qml doesn't match a Plasma version this add-on supports "
                  "(6.3 or newer); leaving the lock screen stock", file=sys.stderr)
            return 0
        version, edits = fits[0]
        patched = {}
        for name, file_edits in edits.items():
            text = stock[name]
            for old, new in file_edits:
                if text.count(old) != 1:
                    print(f"kenning lock screen: {name} edits overlap for Plasma {version}; leaving the lock screen "
                          "stock", file=sys.stderr)
                    return 0
                text = text.replace(old, new)
            patched[name] = text

    # The indicator is a separate file; refresh it every run so updates reach it.
    if not indicator.is_file():
        print(f"kenning lock screen: {indicator} missing; leaving the lock screen stock", file=sys.stderr)
        return 0
    shutil.copyfile(indicator, lockscreen / "FaceScanIndicator.qml")

    if edits is None:
        return 0
    ORIGINALS.mkdir(parents=True, exist_ok=True)
    for name, text in stock.items():
        (ORIGINALS / name).write_text(text)
    for name, text in patched.items():
        (lockscreen / name).write_text(text)
    print(f"kenning lock screen: patched {', '.join(patched)} (Plasma {version} lock screen)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
