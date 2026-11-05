// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import stream.sugo.app

/// Circular progress in the brand gradient; children are laid out inside the ring.
Item {
    id: root

    /// 0..1, drawn clockwise from the top.
    property real progress: 0
    /// The gradient along the arc, from its start to its end.
    property color startColor: Theme.brandPeach
    property color middleColor: Theme.brand
    property color endColor: Theme.brandMagenta
    property color trackColor: Theme.track
    property real lineWidth: Theme.spaceMd

    /// Eases forward to each new value; off when the caller animates `progress` itself.
    property bool animated: true

    // Drawn value: eases forward in small steps; jumps (no animation) when the progress resets.
    property real shown: progress
    onProgressChanged: {
        shownAnim.enabled = animated && progress > shown;
        shown = progress;
    }
    Behavior on shown {
        id: shownAnim
        NumberAnimation { duration: Theme.durationLong; easing.type: Easing.OutCubic }
    }

    default property alias content: contentItem.data

    readonly property real cx: width / 2
    readonly property real cy: height / 2
    readonly property real radius: (Math.min(width, height) - lineWidth) / 2
    readonly property real outer: radius + lineWidth / 2
    readonly property real inner: radius - lineWidth / 2
    readonly property real sweep: 360 * Math.min(0.99995, Math.max(0, shown))
    readonly property real endAngle: (-90 + sweep) * Math.PI / 180

    function px(r: real, a: real): real {
        return cx + r * Math.cos(a);
    }
    function py(r: real, a: real): real {
        return cy + r * Math.sin(a);
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.trackColor
            strokeWidth: root.lineWidth
            fillColor: "transparent"
            PathAngleArc {
                centerX: root.cx
                centerY: root.cy
                radiusX: root.radius
                radiusY: root.radius
                startAngle: -90
                sweepAngle: 360
            }
        }

        // The arc as a filled band with round ends, so it can take a gradient. Winding fill: when
        // the ring is full, the end cap overlapping the start must not punch a hole in it.
        ShapePath {
            strokeColor: "transparent"
            strokeWidth: -1
            fillRule: ShapePath.WindingFill
            fillGradient: LinearGradient {
                x1: root.width * 0.15
                y1: 0
                x2: root.width * 0.85
                y2: root.height
                GradientStop { position: 0; color: root.shown > 0 ? root.startColor : "transparent" }
                GradientStop { position: 0.5; color: root.shown > 0 ? root.middleColor : "transparent" }
                GradientStop { position: 1; color: root.shown > 0 ? root.endColor : "transparent" }
            }
            startX: root.cx
            startY: root.cy - root.outer
            PathArc {
                x: root.px(root.outer, root.endAngle)
                y: root.py(root.outer, root.endAngle)
                radiusX: root.outer
                radiusY: root.outer
                useLargeArc: root.sweep > 180
            }
            PathArc {
                x: root.px(root.inner, root.endAngle)
                y: root.py(root.inner, root.endAngle)
                radiusX: root.lineWidth / 2
                radiusY: root.lineWidth / 2
            }
            PathArc {
                x: root.cx
                y: root.cy - root.inner
                radiusX: root.inner
                radiusY: root.inner
                useLargeArc: root.sweep > 180
                direction: PathArc.Counterclockwise
            }
            PathArc {
                x: root.cx
                y: root.cy - root.outer
                radiusX: root.lineWidth / 2
                radiusY: root.lineWidth / 2
            }
        }
    }

    Item {
        id: contentItem
        anchors.fill: parent
    }
}
