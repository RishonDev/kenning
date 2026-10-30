/*
 * SPDX-FileCopyrightText: 2026 Rishon Jonathan R
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

// Text only: the message, gently pulsing while scanning.
// Theme API 1: the host (FaceScanIndicator) sets the properties below.
Item {
    id: root

    property int faceState // 0 idle, 1 scanning, 2 recognized, 3 failed
    property string message
    property color highlightColor
    property color successColor
    property color failureColor
    property color textColor

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight + Kirigami.Units.smallSpacing * 2

    PlasmaComponents3.Label {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, Kirigami.Units.gridUnit * 16)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        font.pointSize: Kirigami.Theme.defaultFont.pointSize + 2
        color: root.faceState === 2 ? root.successColor : (root.faceState === 3 ? root.failureColor : root.textColor)
        text: root.message

        SequentialAnimation on opacity {
            running: root.faceState === 1
            loops: Animation.Infinite
            alwaysRunToEnd: true
            NumberAnimation { from: 1; to: 0.5; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.5; to: 1; duration: 900; easing.type: Easing.InOutSine }
        }
    }
}
