/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Shapes

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

import org.kde.kenning

// Guided face setup: preview, confirmation, countdown, then the guided capture.
KCM.SimpleKCM {
    id: page

    property bool improving: false
    // Set when the person accepted the ordinary-webcam warning on the settings page
    property bool webcamAccepted: false
    // "ready", "countdown", "capturing", "done", "failed"
    property string step: "ready"
    property string detail: ""
    property string instruction: ""
    property int countdown: 0

    Settings {
        id: prefs
        location: StandardPaths.writableLocation(StandardPaths.ConfigLocation) + "/kenning-setuprc"
        category: "Setup"
        property bool voice: true
    }

    // Separate file, so a missing speech module only disables voice
    Loader {
        id: voice
        source: "Voice.qml"
    }
    readonly property bool voiceAvailable: voice.item !== null && voice.item.available

    function speak(text) {
        if (prefs.voice && voiceAvailable) {
            voice.item.say(text);
        }
    }

    function start() {
        detail = "";
        instruction = "";
        stepIndex = -1;
        returning = false;
        skippedSteps = [];
        countdown = 3;
        step = "countdown";
        speak(i18n("Get ready."));
        countdownTimer.start();
    }

    Timer {
        id: countdownTimer
        interval: 1000
        repeat: true
        onTriggered: {
            if (page.step !== "countdown") {
                stop();
                return;
            }
            page.countdown -= 1;
            if (page.countdown > 0) {
                page.speak(page.countdown.toString());
                return;
            }
            stop();
            page.step = "capturing";
            kcm.addFace(label.text);
        }
    }

    title: improving ? i18n("Improve Recognition") : i18n("Set Up Face Recognition")

    // The engine's instruction lines, in order
    readonly property var guidedSteps: [
        {match: "look straight into the camera", pose: "straight", text: i18n("Look straight at the camera")},
        {match: "to the left", pose: "left", text: i18n("Slowly turn your head a little to the left")},
        {match: "to the right", pose: "right", text: i18n("Now a little to the right")},
        {match: "slightly up", pose: "up", text: i18n("Tilt your head a little up")},
        {match: "slightly down", pose: "down", text: i18n("And a little down")},
    ]
    property int stepIndex: -1
    property bool returning: false
    property var skippedSteps: []

    function friendly(line) {
        if (line.includes("black frames")) {
            return kcm.colourCamera ? i18n("The camera only saw black frames. Is it covered or switched off?")
                                    : i18n("The camera only saw black frames. Is the IR emitter working?");
        }
        if (line.includes("too dark")) {
            return kcm.colourCamera ? i18n("It's too dark or too bright for the camera to see you. Light your face from the front, with no bright window behind you.")
                                    : i18n("It's too dark or too bright for the camera to see you. Move out of direct sunlight or away from a bright lamp.");
        }
        if (line.includes("No face detected")) {
            return kcm.colourCamera ? i18n("No face was found. Make sure your face is lit from the front, not by a window behind you, and try again.")
                                    : i18n("No face was found. Look straight at the camera and try again.");
        }
        if (line.includes("Multiple faces")) {
            return i18n("More than one face was found. Make sure only you are in view.");
        }
        return "";
    }

    // "Name's face", then "Name's face 2"…; first name only if over 24 characters.
    function defaultLabel() {
        const taken = kcm.faces.map(face => face.label);
        const label = (name, number) => number === 1 ? i18nc("@info default name of the first face model", "%1's face", name)
                                                     : i18nc("@info default name of another face model", "%1's face %2", name, number);
        const fitting = number => {
            const full = label(kcm.displayName, number);
            return full.length <= 24 ? full : label(kcm.displayName.split(" ")[0], number).slice(0, 24);
        };
        let number = kcm.faces.length === 0 ? 1 : kcm.faces.length + 1;
        while (taken.includes(fitting(number))) {
            ++number;
        }
        return fitting(number);
    }

    Connections {
        target: kcm
        function onProgress(line) {
            const index = page.guidedSteps.findIndex(step => line.includes(step.match));
            if (index >= 0 || line.includes("look back at the camera")) {
                page.returning = index < 0;
                page.stepIndex = Math.max(index, page.stepIndex);
                page.instruction = page.returning ? i18n("Now look back at the camera") : page.guidedSteps[index].text;
                page.detail = "";
                page.speak(page.instruction);
                return;
            }
            if (line.startsWith("Hint:")) {
                page.detail = line.includes("level") ? i18n("Keep your head level, without tilting it up or down.")
                                                     : i18n("Keep your face toward the screen, without turning it sideways.");
                page.speak(line.includes("level") ? i18n("Keep your head level") : i18n("Keep your face toward the screen"));
                return;
            }
            if (line.startsWith("Skipped")) {
                page.skippedSteps = page.skippedSteps.concat([page.stepIndex]);
                return;
            }
            const text = page.friendly(line);
            if (text) {
                page.detail = text;
            }
        }
        function onOperationFinished(operation, exitCode, message) {
            if (operation !== "add") {
                return;
            }
            if (exitCode === 0) {
                // First face on this account: switch face unlock on where people expect it.
                // A colour webcam can be fooled by a photo, so then only for signing in;
                // approving system changes stays on the password.
                if (!page.improving && kcm.faces.length === 0) {
                    const services = ["pam/lockscreen"];
                    if (kcm.loginScreenSupported) {
                        services.push("pam/login");
                    }
                    if (!kcm.colourCamera) {
                        services.push("pam/sudo");
                    }
                    for (const service of services) {
                        kcm.setValue(service, true);
                    }
                    kcm.setValue("core/disabled", false);
                    kcm.save();
                }
                page.step = "done";
                page.detail = "";
                page.speak(i18n("Done. You can unlock with your face now."));
            } else {
                page.step = "failed";
                page.detail = page.detail || page.friendly(message) || message;
                page.speak(i18n("That didn't work. %1", page.detail));
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width, Kirigami.Units.gridUnit * 26)
        spacing: Kirigami.Units.largeSpacing * 2

        Item {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: Kirigami.Units.gridUnit * 14
            implicitHeight: implicitWidth

            CameraPreview {
                id: preview
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                devicePath: kcm.previewPath
                // Stopped from the countdown on: while the webcam streams, the IR
                // emitter of a combined camera stays off for a couple of seconds.
                active: page.visible && (page.step === "ready" || page.step === "failed")
                visible: active && hasFrame
            }

            Kirigami.Heading {
                anchors.centerIn: parent
                visible: page.step === "countdown"
                level: 1
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 4
                text: page.countdown
            }

            // Placeholder while there is no picture
            ColumnLayout {
                anchors.centerIn: parent
                visible: !preview.visible && page.step !== "countdown" && page.step !== "capturing"
                Kirigami.Icon {
                    Layout.alignment: Qt.AlignHCenter
                    source: page.step === "done" ? "checkmark" : "camera-web"
                    implicitWidth: Kirigami.Units.iconSizes.huge
                    implicitHeight: implicitWidth
                    color: page.step === "done" ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.textColor
                    isMask: page.step === "done"
                }
                QQC2.Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 10
                    visible: preview.error !== "" && (page.step === "ready" || page.step === "failed")
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    text: i18n("Camera preview unavailable: %1", preview.error)
                }
            }

            HeadGuide {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing * 2
                visible: page.step === "capturing"
                pose: page.stepIndex >= 0 ? page.guidedSteps[page.stepIndex].pose : "straight"
                returning: page.returning
            }

            // Ring around the preview: orbits while capturing, closes on the result
            Shape {
                id: ring
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                RotationAnimator on rotation {
                    running: page.step === "capturing"
                    loops: Animation.Infinite
                    from: 0
                    to: 360
                    duration: 1200
                }

                ShapePath {
                    strokeColor: page.step === "done" ? Kirigami.Theme.positiveTextColor
                               : page.step === "failed" ? Kirigami.Theme.negativeTextColor
                               : Kirigami.Theme.highlightColor
                    strokeWidth: Kirigami.Units.smallSpacing * 1.5
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap

                    PathAngleArc {
                        centerX: ring.width / 2
                        centerY: ring.height / 2
                        radiusX: ring.width / 2 - Kirigami.Units.smallSpacing
                        radiusY: radiusX
                        startAngle: -90
                        sweepAngle: page.step === "capturing" ? 90 : 360

                        Behavior on sweepAngle {
                            NumberAnimation {
                                duration: Kirigami.Units.veryLongDuration
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
                }
            }
        }

        // One dot per step: done, skipped (hollow), current, or still to come
        Row {
            Layout.alignment: Qt.AlignHCenter
            visible: page.step === "capturing" || page.step === "done"
            spacing: Kirigami.Units.largeSpacing

            Repeater {
                model: page.guidedSteps.length
                Rectangle {
                    required property int index
                    readonly property bool skipped: page.skippedSteps.includes(index)
                    readonly property bool finished: index < page.stepIndex || (index === page.stepIndex && page.returning) || page.step === "done"
                    width: Kirigami.Units.gridUnit * 0.6
                    height: width
                    radius: width / 2
                    color: skipped ? "transparent"
                         : finished ? Kirigami.Theme.positiveTextColor
                         : index === page.stepIndex ? Kirigami.Theme.highlightColor
                         : Qt.alpha(Kirigami.Theme.textColor, 0.2)
                    border.width: skipped ? 2 : 0
                    border.color: Kirigami.Theme.disabledTextColor

                    Behavior on color {
                        ColorAnimation {
                            duration: Kirigami.Units.longDuration
                        }
                    }
                }
            }
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            level: 2
            text: {
                switch (page.step) {
                case "countdown": return i18n("Starting in %1…", page.countdown);
                case "capturing": return page.instruction || i18n("Look straight at the camera");
                case "done": return page.improving ? i18n("Recognition improved") : i18n("All set!");
                case "failed": return i18n("That didn't work");
                default: return page.improving ? i18n("Ready to add another look?") : i18n("Ready to scan your face?");
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: {
                if (page.detail) {
                    return page.detail;
                }
                switch (page.step) {
                case "countdown": return i18n("Look at the camera, about an arm's length from the screen.");
                case "capturing": return i18n("It moves on by itself once it has seen you. Keep your eyes on the screen.");
                case "done": return page.improving
                    ? i18n("You can now sign in and unlock with your face.")
                    : i18n("You can now sign in and unlock with your face. Do you sometimes wear glasses? Scan again with them taken off or put on, so both work.");
                case "failed": return "";
                default: return page.improving
                    ? i18n("Change one thing first, such as glasses on or off, a hat or different lighting. Then look straight at the camera and turn your head slowly when asked.")
                    : i18n("Sit about an arm's length from the screen with your face in the frame. You'll look straight at the camera, then slowly turn your head left, right, up and down when asked, looking back at the camera after each turn. It takes under a minute.");
                }
            }
        }

        // A reminder of the warning accepted on the settings page
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: kcm.colourCamera && !page.improving && (page.step === "ready" || page.step === "failed")
            type: Kirigami.MessageType.Warning
            text: i18n("Using the ordinary webcam: less secure than an infrared camera, and it needs your face lit from the front.")
        }

        QQC2.TextField {
            id: label
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Kirigami.Units.gridUnit * 14
            visible: page.step === "ready" || page.step === "failed"
            placeholderText: i18n("Name for this look")
            text: page.defaultLabel()
            maximumLength: 24
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            visible: page.step !== "done"
            spacing: 0

            readonly property string voiceHint: i18n("Needs a speech engine: install speech-dispatcher (with a voice such as espeak-ng), then open this page again.")

            // A disabled checkbox gets no hover events, so the row shows its tooltip.
            HoverHandler {
                id: voiceHover
            }
            QQC2.ToolTip {
                visible: voiceHover.hovered && !voiceHelp.hovered && voiceHelp.visible
                text: parent.voiceHint
            }

            QQC2.CheckBox {
                text: i18n("Read the steps aloud")
                enabled: page.voiceAvailable
                checked: prefs.voice && page.voiceAvailable
                onToggled: {
                    prefs.voice = checked;
                    if (!checked && voice.item) {
                        voice.item.stop();
                    }
                }
            }
            Kirigami.ContextualHelpButton {
                id: voiceHelp
                visible: voice.status !== Loader.Loading && !page.voiceAvailable
                toolTipText: parent.voiceHint
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter

            QQC2.Button {
                visible: page.step === "ready" || page.step === "failed"
                icon.name: "media-record"
                text: page.step === "failed" ? i18n("Try Again") : i18n("I'm Ready")
                // An ordinary webcam only after its warning was accepted (settings page)
                enabled: !kcm.busy && kcm.devicePath !== "" && (!kcm.colourCamera || page.improving || page.webcamAccepted)
                onClicked: page.start()
            }
            QQC2.Button {
                visible: page.step === "done"
                icon.name: "list-add"
                text: page.improving ? i18n("Add Another Look") : i18n("Add a Look With or Without Glasses")
                onClicked: {
                    page.improving = true;
                    // Rebound: the face list may refresh after this
                    label.text = Qt.binding(page.defaultLabel);
                    page.step = "ready";
                }
            }
            QQC2.Button {
                visible: page.step !== "capturing"
                icon.name: page.step === "done" ? "dialog-ok" : "dialog-cancel"
                text: page.step === "done" ? i18n("Done") : i18n("Cancel")
                onClicked: {
                    if (page.step === "countdown") {
                        page.step = "ready";
                        if (voice.item) {
                            voice.item.stop();
                        }
                    } else {
                        kcm.pop();
                    }
                }
            }
        }
    }
}
