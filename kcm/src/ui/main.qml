/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

KCM.SimpleKCM {
    id: root

    readonly property var settings: kcm.settings
    readonly property bool faceUnlockOn: !settings["core/disabled"]

    // Shown under an option while it is set to trade security or privacy for convenience
    component RiskWarning: Kirigami.InlineMessage {
        Layout.fillWidth: true
        type: Kirigami.MessageType.Warning
    }

    // Above this the match is looser than SFace's recommended cosine of 0.363 (6.37)
    readonly property real lenientCertainty: 6.4

    // Scanning → recognized → scanning → not recognized, shared by every theme preview
    readonly property var previewStates: [[1, i18n("Looking for you…")], [2, i18n("Welcome, %1", kcm.userName)],
                                          [1, i18n("Looking for you…")], [3, i18n("Couldn't recognize you")]]
    property int previewStep: 0

    Timer {
        running: root.visible
        repeat: true
        interval: 2200
        onTriggered: root.previewStep = (root.previewStep + 1) % root.previewStates.length
    }

    // Result of the last "Test recognition": "" none, "ok", "fail"
    property string testResult: ""
    property string testMessage: ""

    function testOutcome(exitCode, message) {
        switch (exitCode) {
        case 0: return i18n("Recognized you. Face unlock is working.");
        case 10: return i18n("No face is set up yet.");
        case 11: return i18n("Didn't see a matching face in time. Try Improve recognition.");
        case 13: return kcm.colourCamera ? i18n("The camera picture was too dark. Turn on a light in front of you.")
                                         : i18n("The camera picture was too dark. Is the IR emitter on?");
        case -1: return message;
        default: return i18n("Face recognition stopped (%1).", message);
        }
    }

    Connections {
        target: kcm
        function onOperationFinished(operation, exitCode, message) {
            if (operation === "test") {
                root.testResult = exitCode === 0 ? "ok" : "fail";
                root.testMessage = root.testOutcome(exitCode, message);
            }
        }
        function onErrorOccurred(message) {
            errorMessage.text = message;
            errorMessage.visible = true;
        }
    }

    // No infrared camera: face unlock would use the ordinary webcam, which is less safe.
    // Asked before setup starts; the settings page also keeps a warning beside the camera.
    Kirigami.PromptDialog {
        id: webcamDialog
        // Over the window, not centred on the page's whole scrolling height
        parent: QQC2.Overlay.overlay
        title: i18nc("@title:window", "Use the Ordinary Webcam?")
        subtitle: i18n("This computer has no infrared camera, so face recognition would use the ordinary webcam. That is less safe:\n\n• A photo or video of you may be enough to unlock it.\n• It is less accurate, and needs your face lit from the front; it won't work in the dark or with a bright window behind you.\n\nIt will only be used to sign in. Installing software and other changes to the system will still need your password.")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Use Webcam")
                icon.name: "camera-web"
                onTriggered: {
                    webcamDialog.close();
                    kcm.push("SetupPage.qml", {improving: false, webcamAccepted: true});
                }
            }
        ]
    }

    header: ColumnLayout {
        spacing: 0

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            position: Kirigami.InlineMessage.Position.Header
            visible: !kcm.engineInstalled
            text: i18n("The face recognition engine is not installed.")
        }

        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            position: Kirigami.InlineMessage.Position.Header
            showCloseButton: true
            visible: false
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // Overview: status, face count and the on/off switch
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing * 2

            FaceScanIndicator {
                Layout.fillWidth: false
                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                faceState: kcm.faces.length > 0 ? 2 : 1
                style: "hello"
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 2
                    text: i18n("Face recognition")
                    wrapMode: Text.WordWrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: kcm.faces.length > 0
                        ? i18np("Sign in, unlock and approve with your face. %1 face model saved for %2.",
                                "Sign in, unlock and approve with your face. %1 face models saved for %2.",
                                kcm.faces.length, kcm.userName)
                        : kcm.colourCamera ? i18n("Use the webcam to sign in with your face instead of typing your password.")
                        : i18n("Use the infrared camera to sign in with your face instead of typing your password.")
                }
            }

            QQC2.Switch {
                text: i18nc("@option:check face recognition on/off", "On")
                checked: root.faceUnlockOn
                enabled: kcm.engineInstalled
                onToggled: kcm.setValue("core/disabled", !checked)
            }
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            enabled: kcm.engineInstalled

            // Faces
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Your face")
            }

            // A Repeater directly in a FormLayout is not laid out, so the list gets its own column.
            ColumnLayout {
                Kirigami.FormData.label: i18n("Saved models:")
                spacing: 0

                Repeater {
                    model: kcm.faces

                    delegate: RowLayout {
                        required property var modelData

                        Kirigami.Icon {
                            source: "user-identity"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }
                        QQC2.Label {
                            text: modelData.label
                        }
                        QQC2.Label {
                            opacity: 0.7
                            text: modelData.added.toLocaleString(Qt.locale(), Locale.ShortFormat)
                        }
                        QQC2.ToolButton {
                            icon.name: "edit-delete-remove"
                            enabled: !kcm.busy
                            display: QQC2.AbstractButton.IconOnly
                            text: i18n("Remove %1", modelData.label)
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                            onClicked: kcm.removeFace(modelData.id)
                        }
                    }
                }

                QQC2.Label {
                    visible: kcm.faces.length === 0
                    text: i18n("None yet")
                    opacity: 0.7
                }
            }

            RowLayout {
                QQC2.Button {
                    icon.name: kcm.faces.length === 0 ? "list-add" : "view-refresh"
                    text: kcm.faces.length === 0 ? i18n("Set Up…") : i18n("Improve Recognition…")
                    enabled: !kcm.busy && kcm.devicePath !== ""
                    onClicked: {
                        if (kcm.faces.length === 0 && kcm.colourCamera) {
                            webcamDialog.open();
                        } else {
                            kcm.push("SetupPage.qml", {improving: kcm.faces.length > 0});
                        }
                    }
                }
                QQC2.Button {
                    icon.name: "dialog-ok-apply"
                    text: i18n("Test Recognition")
                    visible: kcm.devicePath !== ""
                    enabled: !kcm.busy && kcm.faces.length > 0
                    onClicked: {
                        root.testResult = "";
                        kcm.testRecognition();
                    }
                }
                QQC2.BusyIndicator {
                    visible: kcm.busy
                    running: visible
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }
                QQC2.Label {
                    visible: kcm.busy
                    text: i18n("Look at the camera…")
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: kcm.devicePath === ""
                type: Kirigami.MessageType.Warning
                text: i18n("No camera was found. Connect one and open this page again; an infrared camera, as on laptops that support face sign-in, works best and is the safest.")
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.testResult !== ""
                type: root.testResult === "ok" ? Kirigami.MessageType.Positive : Kirigami.MessageType.Warning
                text: root.testMessage
            }

            // Where it's used
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Use face recognition for")
            }

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Signing in:")
                text: i18n("Login screen")
                // Can always be switched off, only switched on with the Kenning login manager
                enabled: kcm.loginScreenSupported || settings["pam/login"]
                checked: settings["pam/login"]
                onToggled: kcm.setValue("pam/login", checked)
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: !kcm.loginScreenSupported
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: i18n("Needs the Kenning version of Plasma Login Manager (plasma-login-manager-kenning). Other login managers would look for your face before checking a password you typed.")
            }
            QQC2.CheckBox {
                text: i18n("Lock screen")
                checked: settings["pam/lockscreen"]
                onToggled: kcm.setValue("pam/lockscreen", checked)
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Approving changes:")
                text: i18n("Administrator prompts")
                checked: settings["pam/polkit"]
                onToggled: kcm.setValue("pam/polkit", checked)
            }
            QQC2.CheckBox {
                text: i18n("sudo in the terminal")
                checked: settings["pam/sudo"]
                onToggled: kcm.setValue("pam/sudo", checked)
            }
            RiskWarning {
                visible: settings["pam/polkit"] || settings["pam/sudo"]
                type: kcm.colourCamera ? Kirigami.MessageType.Error : Kirigami.MessageType.Warning
                text: kcm.colourCamera
                    ? i18n("Your face can approve changes to the system, and this camera is an ordinary webcam: a photo or video of you could approve them too. Leave these on the password unless you accept that risk.")
                    : i18n("Your face can approve changes to the system, such as installing software. Someone who looks like you, or holds the computer up to your face, could do the same, and so could a harmful program running as you, while you are at the computer. Your password always works too.")
            }

            // Lock screen
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Lock screen")
            }

            QQC2.CheckBox {
                id: autoDismiss
                Kirigami.FormData.label: i18n("When it recognizes me:")
                text: i18n("Dismiss the lock screen automatically")
                enabled: settings["pam/lockscreen"]
                checked: !settings["lock/faceUnlockShowButton"]
                onToggled: kcm.setValue("lock/faceUnlockShowButton", !checked)
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: autoDismiss.checked ? i18n("The screen unlocks as soon as the camera recognizes you, even if you only glanced at it.")
                                          : i18n("You will press Unlock after you are recognized.")
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Pause before unlocking:")
                enabled: settings["pam/lockscreen"] && autoDismiss.checked
                from: 0
                to: 3000
                stepSize: 100
                value: settings["lock/unlockDelay"]
                textFromValue: (value, locale) => i18nc("milliseconds", "%1 ms", value)
                valueFromText: (text, locale) => parseInt(text)
                onValueModified: kcm.setValue("lock/unlockDelay", value)
            }

            // With a fingerprint enrolled the lock screen can start with either method.
            QQC2.ComboBox {
                id: preferredMethod
                Kirigami.FormData.label: i18n("Sign in first with:")
                visible: kcm.fingerprintEnrolled
                enabled: settings["pam/lockscreen"] && !useLastMethod.checked
                model: [i18n("Face"), i18n("Fingerprint")]
                currentIndex: settings["lock/preferredMethod"] === "fingerprint" ? 1 : 0
                onActivated: kcm.setValue("lock/preferredMethod", currentIndex === 1 ? "fingerprint" : "face")
            }
            QQC2.CheckBox {
                id: useLastMethod
                visible: kcm.fingerprintEnrolled
                enabled: settings["pam/lockscreen"]
                text: i18n("Start with the method I used last time")
                checked: settings["lock/useLastMethod"]
                onToggled: kcm.setValue("lock/useLastMethod", checked)
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: kcm.fingerprintEnrolled
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: useLastMethod.checked ? i18n("The lock screen starts with face or fingerprint, whichever unlocked it last time. Your password always works.")
                    : preferredMethod.currentIndex === 1 ? i18n("If your fingerprint isn't read in time, the lock screen looks for your face. Your password always works.")
                    : i18n("If your face isn't recognized, use your fingerprint or password.")
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: !kcm.lockScreenPatched
                type: Kirigami.MessageType.Information
                text: i18n("The lock screen animation and these options aren't available for this Plasma version. Face unlock still works on this lock screen: look at the camera, then press Unlock.")
            }

            // Login screen (only with the Kenning login manager)
            Kirigami.Separator {
                visible: kcm.loginScreenSupported
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Login screen")
            }

            QQC2.CheckBox {
                visible: kcm.loginScreenSupported
                Kirigami.FormData.label: i18n("When the login screen appears:")
                text: i18n("Start looking for my face right away")
                enabled: settings["pam/login"]
                checked: settings["LoginScreen/AutoScan"]
                onToggled: kcm.setValue("LoginScreen/AutoScan", checked)
            }
            QQC2.SpinBox {
                visible: kcm.loginScreenSupported
                Kirigami.FormData.label: i18n("Show welcome for:")
                enabled: settings["pam/login"]
                from: 0
                to: 3000
                stepSize: 100
                value: settings["LoginScreen/WelcomePause"]
                textFromValue: (value, locale) => i18nc("milliseconds", "%1 ms", value)
                valueFromText: (text, locale) => parseInt(text)
                onValueModified: kcm.setValue("LoginScreen/WelcomePause", value)
            }

            // Appearance
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Appearance")
            }

            // Theme grid, like the icon and application style pages
            // FormLayout sizes items by their implicit size, which Flow keeps read-only
            Item {
                Kirigami.FormData.label: i18n("Theme:")
                implicitWidth: Kirigami.Units.gridUnit * 27
                implicitHeight: themeGrid.implicitHeight

                Flow {
                    id: themeGrid
                    width: parent.width
                    spacing: Kirigami.Units.largeSpacing

                    readonly property string selected: settings["Appearance/AnimationStyle"]

                    Repeater {
                        model: kcm.themes

                        delegate: QQC2.ItemDelegate {
                            id: tile
                            required property var modelData
                            readonly property bool current: modelData.id === themeGrid.selected

                            width: Kirigami.Units.gridUnit * 8.5
                            height: Kirigami.Units.gridUnit * 8.5
                            padding: Kirigami.Units.smallSpacing
                            highlighted: current
                            onClicked: kcm.setValue("Appearance/AnimationStyle", modelData.id)

                            QQC2.ToolTip.text: modelData.description + (modelData.author ? "\n" + i18n("By %1", modelData.author) : "")
                            QQC2.ToolTip.visible: hovered && modelData.description !== ""
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                            contentItem: ColumnLayout {
                                spacing: Kirigami.Units.smallSpacing

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: Kirigami.Units.cornerRadius
                                    color: "#1b1e20"
                                    border.width: tile.current ? 2 : 1
                                    border.color: tile.current ? Kirigami.Theme.highlightColor : Qt.rgba(1, 1, 1, 0.12)
                                    clip: true

                                    Kirigami.Theme.inherit: false
                                    Kirigami.Theme.colorSet: Kirigami.Theme.Complementary

                                    FaceScanIndicator {
                                        anchors.centerIn: parent
                                        width: parent.width / scale
                                        scale: 0.55
                                        style: tile.modelData.id
                                        faceState: root.previewStates[root.previewStep][0]
                                        message: root.previewStates[root.previewStep][1]
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                        text: tile.modelData.name
                                    }
                                    QQC2.ToolButton {
                                        visible: tile.modelData.removable
                                        icon.name: "edit-delete-remove"
                                        display: QQC2.AbstractButton.IconOnly
                                        text: i18n("Remove %1", tile.modelData.name)
                                        QQC2.ToolTip.text: text
                                        QQC2.ToolTip.visible: hovered
                                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                                        Layout.preferredWidth: Layout.preferredHeight
                                        onClicked: kcm.removeTheme(tile.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                QQC2.Button {
                    icon.name: "document-import"
                    text: i18n("Install from File…")
                    onClicked: themeFileDialog.open()
                }
                QQC2.Label {
                    opacity: 0.7
                    text: i18n("Themes run on the login and lock screens; only install ones you trust.")
                    wrapMode: Text.WordWrap
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 14
                }
            }

            QtDialogs.FileDialog {
                id: themeFileDialog
                title: i18n("Install Face Unlock Theme")
                nameFilters: [i18n("Theme archives (*.tar.gz *.tar.xz *.tar.zst *.tgz *.zip)")]
                onAccepted: kcm.installTheme(selectedFile)
            }

            // Large live preview of the selected theme on a lock-screen-like background
            Rectangle {
                Kirigami.FormData.label: i18n("Preview:")
                // FormLayout sizes items by their implicit size, not Layout.preferred*.
                implicitWidth: Kirigami.Units.gridUnit * 18
                implicitHeight: Kirigami.Units.gridUnit * 11
                radius: Kirigami.Units.cornerRadius
                color: "#1b1e20"
                border.color: Qt.rgba(1, 1, 1, 0.12)

                Kirigami.Theme.inherit: false
                Kirigami.Theme.colorSet: Kirigami.Theme.Complementary

                FaceScanIndicator {
                    anchors.centerIn: parent
                    width: parent.width
                    style: themeGrid.selected
                    faceState: root.previewStates[root.previewStep][0]
                    message: root.previewStates[root.previewStep][1]
                }
            }

            // Recognition tuning
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Recognition")
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Match threshold:")
                QQC2.Label {
                    text: i18nc("stricter face matching", "Strict")
                    opacity: 0.7
                }
                QQC2.Slider {
                    id: certainty
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    from: 4
                    to: 8
                    stepSize: 0.1
                    value: settings["video/certainty"]
                    onMoved: kcm.setValue("video/certainty", Math.round(value * 10) / 10)
                }
                QQC2.Label {
                    text: i18nc("more lenient face matching", "Lenient")
                    opacity: 0.7
                }
                QQC2.Label {
                    text: certainty.value.toFixed(1)
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: i18n("Lower values only accept closer matches. Around 6 suits most faces.")
            }
            RiskWarning {
                visible: certainty.value > root.lenientCertainty
                text: i18n("A lenient threshold accepts looser matches, so people who look like you, such as close relatives, may be recognized as you.")
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Give up after:")
                from: 1
                to: 30
                value: settings["video/timeout"]
                textFromValue: (value, locale) => i18np("%1 second", "%1 seconds", value)
                valueFromText: (text, locale) => parseInt(text)
                onValueModified: kcm.setValue("video/timeout", value)
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Skip frames darker than:")
                from: 0
                to: 100
                value: settings["video/dark_threshold"]
                textFromValue: (value, locale) => i18nc("percent", "%1%", value)
                valueFromText: (text, locale) => parseInt(text)
                onValueModified: kcm.setValue("video/dark_threshold", value)
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Messages:")
                text: i18n("Say when face recognition starts")
                checked: settings["core/detection_notice"]
                onToggled: kcm.setValue("core/detection_notice", checked)
            }
            RiskWarning {
                visible: !settings["core/detection_notice"]
                type: Kirigami.MessageType.Information
                text: i18n("The login and lock screens start their animation from this message. Without it they look like they are only waiting for a password while the camera is on.")
            }
            QQC2.CheckBox {
                text: i18n("Say who was recognized")
                checked: !settings["core/no_confirmation"]
                onToggled: kcm.setValue("core/no_confirmation", !checked)
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Security:")
                text: i18n("Don't use when the lid is closed")
                checked: settings["core/abort_if_lid_closed"]
                onToggled: kcm.setValue("core/abort_if_lid_closed", checked)
            }
            QQC2.CheckBox {
                text: i18n("Don't use over SSH")
                checked: settings["core/abort_if_ssh"]
                onToggled: kcm.setValue("core/abort_if_ssh", checked)
            }
            RiskWarning {
                visible: !settings["core/abort_if_lid_closed"]
                type: Kirigami.MessageType.Information
                text: i18n("With the lid closed the camera can't see anyone, so every sign-in waits for face recognition to give up before asking for the password.")
            }
            RiskWarning {
                visible: !settings["core/abort_if_ssh"]
                text: i18n("Over SSH the person signing in is somewhere else, but the camera looks at whoever is in front of this computer. Anyone sitting here could approve a remote sudo.")
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Camera:")
                text: kcm.devicePath || i18n("Not configured")
                elide: Text.ElideMiddle
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                opacity: 0.7
            }
            RiskWarning {
                visible: kcm.colourCamera
                text: i18n("This is an ordinary webcam, not an infrared camera. It is less accurate, a photo or video of you may fool it, and it needs your face lit from the front.")
            }
        }
    }
}
