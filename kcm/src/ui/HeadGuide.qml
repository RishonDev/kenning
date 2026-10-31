/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami

// A drawn head acting out the current Set Up step, mirrored like the preview.
Item {
    id: guide

    // "straight", "left", "right", "up" or "down"
    property string pose: "straight"
    // Turning back from pose to looking straight
    property bool returning: false

    property real amount: 0
    readonly property real yaw: (pose === "left" ? -1 : pose === "right" ? 1 : 0) * amount
    readonly property real pitch: (pose === "up" ? -1 : pose === "down" ? 1 : 0) * amount

    readonly property real headWidth: Math.min(width, height / 1.3) * 0.66
    readonly property real headHeight: headWidth * 1.3
    readonly property real centerX: width / 2
    readonly property real centerY: height / 2
    // Features move further than the outline as the head turns, which reads as depth.
    readonly property real featureX: centerX + yaw * headWidth * 0.3
    readonly property real featureY: centerY + pitch * headHeight * 0.2

    function update() {
        motion.stop();
        motionBack.stop();
        amount = 0;
        if (visible && pose !== "straight") {
            (returning ? motionBack : motion).start();
        }
    }
    onPoseChanged: update()
    onReturningChanged: update()
    onVisibleChanged: update()
    Component.onCompleted: update()

    SequentialAnimation {
        id: motion
        loops: Animation.Infinite
        NumberAnimation { target: guide; property: "amount"; to: 1; duration: 1100; easing.type: Easing.InOutSine }
        PauseAnimation { duration: 900 }
        NumberAnimation { target: guide; property: "amount"; to: 0; duration: 900; easing.type: Easing.InOutSine }
        PauseAnimation { duration: 500 }
    }

    SequentialAnimation {
        id: motionBack
        loops: Animation.Infinite
        PropertyAction { target: guide; property: "amount"; value: 1 }
        PauseAnimation { duration: 400 }
        NumberAnimation { target: guide; property: "amount"; to: 0; duration: 900; easing.type: Easing.InOutSine }
        PauseAnimation { duration: 1200 }
    }

    Repeater {
        model: [-1, 1]
        Rectangle {
            required property int modelData
            readonly property real shown: Math.max(0, 1 - 1.6 * Math.max(0, modelData * guide.yaw))
            width: guide.headWidth * 0.2 * shown
            height: guide.headHeight * 0.24
            radius: width / 2
            color: Qt.alpha(Kirigami.Theme.highlightColor, 0.3)
            border.color: Kirigami.Theme.highlightColor
            border.width: Kirigami.Units.smallSpacing
            visible: shown > 0.05
            x: guide.centerX + modelData * guide.headWidth * (0.46 - 0.12 * Math.abs(guide.yaw)) - guide.yaw * guide.headWidth * 0.08 - width / 2
            y: guide.featureY - guide.headHeight * 0.1 - height / 2
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        // Head
        ShapePath {
            strokeColor: Kirigami.Theme.highlightColor
            strokeWidth: Kirigami.Units.smallSpacing
            fillColor: Qt.tint(Kirigami.Theme.backgroundColor, Qt.alpha(Kirigami.Theme.highlightColor, 0.15))
            PathAngleArc {
                centerX: guide.centerX + guide.yaw * guide.headWidth * 0.04
                centerY: guide.centerY + guide.pitch * guide.headHeight * 0.03
                radiusX: guide.headWidth / 2 * (1 - 0.06 * Math.abs(guide.yaw))
                radiusY: guide.headHeight / 2 * (1 - 0.05 * Math.abs(guide.pitch))
                startAngle: 0
                sweepAngle: 360
            }
        }

        // Nose
        ShapePath {
            strokeColor: Kirigami.Theme.textColor
            strokeWidth: Kirigami.Units.smallSpacing * 0.75
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                moveToStart: true
                centerX: guide.featureX + guide.yaw * guide.headWidth * 0.12
                centerY: guide.featureY + guide.headHeight * (0.04 + guide.pitch * 0.03)
                radiusX: guide.headWidth * 0.06
                radiusY: guide.headHeight * 0.035
                startAngle: 15
                sweepAngle: 150
            }
        }

        // Mouth
        ShapePath {
            strokeColor: Kirigami.Theme.textColor
            strokeWidth: Kirigami.Units.smallSpacing * 0.75
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                moveToStart: true
                centerX: guide.featureX + guide.yaw * guide.headWidth * 0.02
                centerY: guide.featureY + guide.headHeight * (0.16 - guide.pitch * 0.02)
                radiusX: guide.headWidth * 0.15 * (1 - 0.25 * Math.abs(guide.yaw))
                radiusY: guide.headHeight * 0.06 * (1 - 0.3 * Math.abs(guide.pitch))
                startAngle: 25
                sweepAngle: 130
            }
        }
    }

    // Eyes: the one on the side the head turns to narrows as it nears the edge.
    Repeater {
        model: [-1, 1]
        Rectangle {
            required property int modelData
            readonly property real squeeze: 1 - 0.45 * Math.max(0, modelData * guide.yaw)
            width: guide.headWidth * 0.1 * squeeze
            height: guide.headWidth * 0.13 * (1 - 0.2 * Math.abs(guide.pitch))
            radius: width / 2
            color: Kirigami.Theme.textColor
            x: guide.featureX + modelData * guide.headWidth * 0.19 * (1 - 0.3 * Math.abs(guide.yaw)) - width / 2
            y: guide.featureY - guide.headHeight * 0.1 - height / 2
        }
    }

    // Arrow pointing the way, drawn pointing right and rotated
    Shape {
        id: arrow
        readonly property real gap: guide.headWidth / 2 + Kirigami.Units.largeSpacing * 1.5 + width / 2
        readonly property real vgap: guide.headHeight / 2 + Kirigami.Units.largeSpacing + height / 2
        visible: guide.pose !== "straight" && !guide.returning
        width: Kirigami.Units.iconSizes.smallMedium
        height: width
        preferredRendererType: Shape.CurveRenderer
        rotation: ({left: 180, right: 0, up: -90, down: 90})[guide.pose] ?? 0
        opacity: 0.3 + 0.7 * guide.amount
        x: guide.centerX + (guide.pose === "left" ? -gap : guide.pose === "right" ? gap : 0) - width / 2
        y: guide.centerY + (guide.pose === "up" ? -vgap : guide.pose === "down" ? vgap : 0) - height / 2

        ShapePath {
            strokeColor: Kirigami.Theme.highlightColor
            strokeWidth: Kirigami.Units.smallSpacing
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            startX: 0
            startY: arrow.height / 2
            PathLine { x: arrow.width; y: arrow.height / 2 }
            PathMove { x: arrow.width * 0.55; y: arrow.height * 0.1 }
            PathLine { x: arrow.width; y: arrow.height / 2 }
            PathLine { x: arrow.width * 0.55; y: arrow.height * 0.9 }
        }
    }
}
