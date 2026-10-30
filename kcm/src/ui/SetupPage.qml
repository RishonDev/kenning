/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Shapes

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

import org.kde.kenning

// Guided face setup: live camera preview, one capture, then the
// option to improve recognition with more captures (glasses on/off, lighting).
KCM.SimpleKCM {
    id: page

    property bool improving: false
    // Set when the person accepted the ordinary-webcam warning on the settings page
    property bool webcamAccepted: false
    // "ready", "capturing", "done", "failed"
    property string step: "ready"
    property string detail: ""

    title: improving ? i18n("Improve Recognition") : i18n("Set Up Face Recognition")

    function friendly(line) {
        if (line.includes("look straight into the camera")) {
            return i18n("Keep looking at the camera…");
        }
        if (line.includes("black frames")) {
            return kcm.colourCamera ? i18n("The camera only saw black frames. Is it covered or switched off?")
                                    : i18n("The camera only saw black frames. Is the IR emitter working?");
        }
        if (line.includes("too dark")) {
            return kcm.colourCamera ? i18n("It's too dark for the camera to see you. Turn on a light in front of you.")
                                    : i18n("It's too dark for the camera to see you.");
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

    function defaultLabel() {
        return improving ? i18n("Improvement %1", kcm.faces.length) : i18n("My face");
    }

    Connections {
        target: kcm
        function onProgress(line) {
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
            } else {
                page.step = "failed";
                page.detail = page.detail || page.friendly(message) || message;
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
                devicePath: kcm.devicePath
                // The engine needs the camera to itself while it captures
                active: page.step === "ready" && page.visible
            }

            // Placeholder while there is no picture
            ColumnLayout {
                anchors.centerIn: parent
                visible: !preview.hasFrame || !preview.active
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
                    visible: preview.error !== "" && page.step === "ready"
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    text: i18n("Camera preview unavailable: %1", preview.error)
                }
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

        Kirigami.Heading {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            level: 2
            text: {
                switch (page.step) {
                case "capturing": return i18n("Look straight at the camera");
                case "done": return page.improving ? i18n("Recognition improved") : i18n("All set!");
                case "failed": return i18n("That didn't work");
                default: return page.improving ? i18n("Add another look") : i18n("Make sure your face is centered in the frame");
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
                case "capturing": return i18n("This takes a few seconds.");
                case "done": return i18n("You can now sign in and unlock with your face. If you wear glasses, add a look with them on and off.");
                case "failed": return "";
                default: return page.improving
                    ? i18n("Capture yourself in a different situation, such as with glasses, a hat or different lighting.")
                    : i18n("Hold still about an arm's length from the screen.");
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

            QQC2.Button {
                visible: page.step === "ready" || page.step === "failed"
                icon.name: "media-record"
                text: page.step === "failed" ? i18n("Try Again") : i18n("Start")
                // An ordinary webcam only after its warning was accepted (settings page)
                enabled: !kcm.busy && kcm.devicePath !== "" && (!kcm.colourCamera || page.improving || page.webcamAccepted)
                onClicked: {
                    page.detail = "";
                    page.step = "capturing";
                    kcm.addFace(label.text);
                }
            }
            QQC2.Button {
                visible: page.step === "done"
                icon.name: "list-add"
                text: i18n("Improve Recognition")
                onClicked: {
                    page.improving = true;
                    label.text = page.defaultLabel();
                    page.step = "ready";
                }
            }
            QQC2.Button {
                visible: page.step !== "capturing"
                icon.name: page.step === "done" ? "dialog-ok" : "dialog-cancel"
                text: page.step === "done" ? i18n("Done") : i18n("Cancel")
                onClicked: kcm.pop()
            }
        }
    }
}
