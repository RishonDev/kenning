/*
 * SPDX-FileCopyrightText: 2026 Rishon Jonathan R
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

// Face unlock progress for the login screen, the lock screen and the settings page; themes: see THEMES.md.
Item {
    id: root

    // 0 idle (hidden), 1 scanning, 2 recognized, 3 failed
    property int faceState: 0
    property string message
    property string style: "hello"
    property var themeDirs: ["file:///usr/local/share/kenning/themes/", "file:///usr/share/kenning/themes/"]

    readonly property bool active: faceState !== 0
    readonly property string themeId: /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(style) ? style : "hello"

// The form is disabled while PAM runs, which would grey the text out.
    property color textColor
    Component.onCompleted: textColor = Kirigami.Theme.textColor
    Connections {
        target: root.Kirigami.Theme
        function onTextColorChanged() {
            if (root.enabled) {
                root.textColor = root.Kirigami.Theme.textColor;
            }
        }
    }

    Layout.fillWidth: true
    implicitWidth: loader.item ? loader.item.implicitWidth : 0
    implicitHeight: active && loader.item ? loader.item.implicitHeight : 0
    opacity: active ? 1 : 0
    clip: true

    Behavior on implicitHeight {
        NumberAnimation {
            duration: Kirigami.Units.longDuration
            easing.type: Easing.InOutQuad
        }
    }
    Behavior on opacity {
        NumberAnimation {
            duration: Kirigami.Units.longDuration
        }
    }

    property int attempt: 0
    onThemeIdChanged: attempt = 0

    Loader {
        id: loader
        anchors.horizontalCenter: parent.horizontalCenter
        source: root.attempt < root.themeDirs.length
            ? root.themeDirs[root.attempt] + root.themeId + "/contents/ui/main.qml"
            : root.themeDirs[root.themeDirs.length - 1] + "hello/contents/ui/main.qml"
        onStatusChanged: {
            if (status === Loader.Error && root.attempt < root.themeDirs.length) {
                root.attempt++;
            }
        }
    }

    Binding {
        target: loader.item
        when: loader.status === Loader.Ready
        property: "faceState"
        value: root.faceState
    }
    Binding {
        target: loader.item
        when: loader.status === Loader.Ready
        property: "message"
        value: root.message
    }
    Binding {
        target: loader.item
        when: loader.status === Loader.Ready
        property: "highlightColor"
        value: Kirigami.Theme.highlightColor
    }
    Binding {
        target: loader.item
        when: loader.status === Loader.Ready
        property: "successColor"
        value: Kirigami.Theme.positiveTextColor
    }
    Binding {
        target: loader.item
        when: loader.status === Loader.Ready
        property: "failureColor"
        value: Kirigami.Theme.negativeTextColor
    }
    Binding {
        target: loader.item
        when: loader.status === Loader.Ready
        property: "textColor"
        value: root.textColor
    }
}
