/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.kirigami.delegates as KD

KCM.ScrollViewKCM {
    id: page

    title: i18nc("@title", "Your Face")

    actions: [
        Kirigami.Action {
            icon.name: "list-add"
            text: i18nc("@action:button", "Add Look…")
            enabled: !kcm.busy && kcm.devicePath !== ""
            onTriggered: kcm.push("SetupPage.qml", {improving: true})
        }
    ]

    view: ListView {
        model: kcm.faces

        delegate: QQC2.ItemDelegate {
            id: item
            required property var modelData
            width: ListView.view.width
            hoverEnabled: false
            down: false

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    source: "user-identity"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }
                KD.TitleSubtitle {
                    Layout.fillWidth: true
                    title: item.modelData.label
                    subtitle: i18nc("@info face model date", "Added %1", item.modelData.added.toLocaleString(Qt.locale(), Locale.ShortFormat))
                }
                QQC2.ToolButton {
                    icon.name: "edit-delete-remove"
                    enabled: !kcm.busy
                    display: QQC2.AbstractButton.IconOnly
                    text: i18nc("@action:button", "Remove %1", item.modelData.label)
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: kcm.removeFace(item.modelData.id)
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: parent.count === 0
            icon.name: "user-identity"
            text: i18n("No face is set up")
            explanation: i18n("Without a saved look, you sign in with your password.")
            helpfulAction: Kirigami.Action {
                icon.name: "list-add"
                text: i18nc("@action:button", "Set Up…")
                enabled: !kcm.busy && kcm.devicePath !== ""
                onTriggered: kcm.push("SetupPage.qml", {improving: false})
            }
        }
    }

    footer: QQC2.Label {
        padding: Kirigami.Units.largeSpacing
        wrapMode: Text.WordWrap
        opacity: 0.7
        text: i18n("Each look is a set of captures from straight ahead and a few angles. Add another for glasses on or off, a hat, or different lighting.")
    }
}
