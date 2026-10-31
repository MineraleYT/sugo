// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import io.github.mineraleyt.sugo

/// A soft round light in the brand colours, fading out towards its edge (behind the welcome logo).
Shape {
    id: glow

    // Decoration only: never takes clicks.
    enabled: false
    containsMode: Shape.BoundingRectContains
    preferredRendererType: Shape.CurveRenderer

    readonly property real radius: Math.min(width, height) / 2

    ShapePath {
        strokeColor: "transparent"
        strokeWidth: -1
        fillGradient: RadialGradient {
            centerX: glow.width / 2
            centerY: glow.height / 2
            centerRadius: glow.radius
            focalX: centerX
            focalY: centerY
            GradientStop { position: 0; color: Theme.glowInner }
            GradientStop { position: 0.55; color: Theme.glowOuter }
            GradientStop { position: 1; color: "transparent" }
        }
        PathAngleArc {
            centerX: glow.width / 2
            centerY: glow.height / 2
            radiusX: glow.radius
            radiusY: glow.radius
            startAngle: 0
            sweepAngle: 360
        }
    }
}
