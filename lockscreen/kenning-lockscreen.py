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
import shutil
import subprocess
import sys
from pathlib import Path

# MARKER tags every patched file; VERSION changes whenever the edits do, so an
# older patch is replaced by re-patching the original file from the package cache.
MARKER = "kenning lock screen patch"
# Markers of earlier releases, so files they patched are re-patched from the original.
LEGACY_MARKERS = ("lazysetup face unlock", "plasma-faceunlock lock screen patch")
VERSION = "face unlock patch 5"

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

    // Returns true when the message belonged to face unlock and is shown by the indicator.
    function handleFaceMessage(msg) {{
        if (msg.startsWith("Attempting facial authentication")) {{
            faceState = 1;
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
            faceMessage = msg.includes("too dark") ? i18nc("@info:status", "It's too dark to see you")
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

EDITS = {
    "config.xml": [CONFIG_XML],
    "config.qml": [CONFIG_QML],
    "LockScreenUi.qml": [UI_SUCCEEDED, UI_FUNCTION, UI_FAILED, UI_INFO, UI_INDICATOR, UI_INDICATOR_END, UI_WAKE, UI_KEYS, UI_PASSWORD, UI_PROMPT],
}


SYSTEM_LOCKSCREEN = "/usr/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen"


def is_patched(text: str) -> bool:
    return MARKER in text or any(marker in text for marker in LEGACY_MARKERS)


def original(name: str) -> str | None:
    """The unpatched file from the cached package (plasma-desktop) that installed it."""
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
    for name in EDITS:
        if not is_patched((lockscreen / name).read_text()):
            continue
        text = original(name)
        if text is None:
            print(f"face unlock lock screen: original {name} not in the package cache; reinstall plasma-desktop",
                  file=sys.stderr)
            continue
        (lockscreen / name).write_text(text)
        print(f"face unlock lock screen: restored {name}")
    (lockscreen / "FaceScanIndicator.qml").unlink(missing_ok=True)
    return 0


def main() -> int:
    if RESTORE:
        return restore()
    patched = {}
    for name, edits in EDITS.items():
        text = (lockscreen / name).read_text()
        if VERSION in text:
            print(f"face unlock lock screen: {name} already patched")
            continue
        if is_patched(text):
            text = original(name)
            if text is None:
                print(f"face unlock lock screen: {name} has an older patch and the original is not in the "
                      "package cache; reinstall plasma-desktop to update it", file=sys.stderr)
                return 0
        for old, new in edits:
            if text.count(old) != 1:
                print(f"face unlock lock screen: {name} changed upstream; leaving the lock screen stock",
                      file=sys.stderr)
                return 0
            text = text.replace(old, new)
        patched[name] = text

    # The indicator is a separate file; refresh it every run so updates reach it.
    if not indicator.is_file():
        print(f"face unlock lock screen: {indicator} missing; leaving the lock screen stock", file=sys.stderr)
        return 0
    shutil.copyfile(indicator, lockscreen / "FaceScanIndicator.qml")

    for name, text in patched.items():
        (lockscreen / name).write_text(text)
    if patched:
        print(f"face unlock lock screen: patched {', '.join(patched)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
