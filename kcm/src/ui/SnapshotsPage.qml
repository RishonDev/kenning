/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

KCM.GridViewKCM {
    id: page

    title: i18nc("@title", "Failed Attempts")

    // The photos need the user's password; nothing shows until it was given.
    property bool loaded: false
    Connections {
        target: kcm
        function onSnapshotsChanged() {
            page.loaded = true;
        }
    }
    Component.onCompleted: kcm.refreshSnapshots()

    actions: [
        Kirigami.Action {
            icon.name: "view-refresh"
            text: i18nc("@action:button", "Refresh")
            onTriggered: kcm.refreshSnapshots()
        },
        Kirigami.Action {
            icon.name: "edit-delete"
            text: i18nc("@action:button", "Delete All…")
            enabled: kcm.snapshots.length > 0
            onTriggered: deleteDialog.open()
        }
    ]

    view.model: kcm.snapshots

    view.delegate: KCM.GridDelegate {
        id: tile
        required property var modelData

        text: modelData.time.toLocaleString(Qt.locale(), Locale.ShortFormat)
        thumbnailAvailable: true
        thumbnail: Image {
            anchors.fill: parent
            source: tile.modelData.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        onClicked: {
            viewer.photo = modelData;
            viewer.open();
        }
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: page.view.count === 0
        icon.name: page.loaded ? "camera-photo" : "object-locked"
        text: page.loaded ? i18n("No failed attempts") : i18n("Photos are protected by your password")
        explanation: page.loaded ? i18n("When a face that isn't yours is seen, a photo of it appears here.") : ""
        helpfulAction: page.loaded ? null : showAction
    }

    Kirigami.Action {
        id: showAction
        icon.name: "unlock"
        text: i18nc("@action:button", "Show Photos")
        onTriggered: kcm.refreshSnapshots()
    }

    Kirigami.Dialog {
        id: viewer
        property var photo
        title: photo ? photo.time.toLocaleString(Qt.locale(), Locale.LongFormat) : ""
        padding: 0

        Image {
            source: viewer.photo ? viewer.photo.source : ""
            fillMode: Image.PreserveAspectFit
            implicitWidth: Kirigami.Units.gridUnit * 32
            implicitHeight: sourceSize.width > 0 ? implicitWidth * sourceSize.height / sourceSize.width : implicitWidth * 0.75
        }
    }

    Kirigami.PromptDialog {
        id: deleteDialog
        title: i18nc("@title:window", "Delete All Photos?")
        subtitle: i18n("The photos of failed attempts will be deleted. This can't be undone.")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "edit-delete"
                text: i18nc("@action:button", "Delete All")
                onTriggered: {
                    kcm.clearSnapshots();
                    deleteDialog.close();
                }
            }
        ]
    }

    footer: RowLayout {
        Kirigami.Icon {
            source: "dialog-information"
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Layout.preferredWidth
        }
        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: i18n("The newest 20 are kept. Only you and administrators can see them.")
        }
    }
}
