// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import stream.sugo.app

/**
 * The application icon drawn live, with its entrance: the tile comes in, the spiral draws itself
 * from the centre outwards, the leaf sprouts at its tip and a soft light of the brand settles
 * behind it. `play()` runs it (about 1.3 s); until then nothing shows, and `finish()` jumps to
 * the end. Same picture as data/icons/hicolor/scalable/apps/stream.sugo.app.svg.
 */
Item {
    id: logo

    property real size: Theme.logoHero
    /// When the text around the logo should start coming in, from `play()`.
    readonly property int textDelay: Theme.durationShort + Theme.durationLong * 3 + Theme.durationStagger

    // The parts of the entrance, 0..1 (the leaf overshoots a little).
    property real tileIn: 0
    property real draw: 0
    property real leaf: 0
    property real ping: 0
    property real glowOpacity: 0
    property real glowScale: 0.6

    function play(): void {
        intro.restart();
    }

    function finish(): void {
        intro.stop();
        tileIn = 1;
        draw = 1;
        leaf = 1;
        ping = 0;
        glowOpacity = 0.5;
        glowScale = 1;
    }

    implicitWidth: size
    implicitHeight: size
    Accessible.role: Accessible.Graphic
    Accessible.name: i18n("Sugo")

    ParallelAnimation {
        id: intro
        NumberAnimation {
            target: logo; property: "tileIn"; from: 0; to: 1
            duration: Theme.durationLong; easing.type: Easing.OutBack; easing.overshoot: 1.2
        }
        SequentialAnimation {
            PropertyAction { target: logo; property: "draw"; value: 0 }
            PauseAnimation { duration: Theme.durationShort }
            NumberAnimation {
                target: logo; property: "draw"; from: 0; to: 1
                duration: Theme.durationLong * 3; easing.type: Easing.InOutCubic
            }
        }
        SequentialAnimation {
            PropertyAction { target: logo; property: "leaf"; value: 0 }
            PauseAnimation { duration: Theme.durationShort + Theme.durationLong * 3 - Theme.durationStagger }
            NumberAnimation {
                target: logo; property: "leaf"; from: 0; to: 1
                duration: Theme.durationLong * 1.5; easing.type: Easing.OutBack; easing.overshoot: 2
            }
        }
        SequentialAnimation {
            PropertyAction { target: logo; property: "ping"; value: 0 }
            PauseAnimation { duration: Theme.durationShort + Theme.durationLong * 3 - Theme.durationStagger / 2 }
            NumberAnimation {
                target: logo; property: "ping"; from: 0; to: 1
                duration: Theme.durationLong * 2.4; easing.type: Easing.OutCubic
            }
        }
        SequentialAnimation {
            ParallelAnimation {
                NumberAnimation {
                    target: logo; property: "glowOpacity"; from: 0; to: 1
                    duration: Theme.durationLong * 3; easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    target: logo; property: "glowScale"; from: 0.6; to: 1.04
                    duration: Theme.durationLong * 3; easing.type: Easing.OutCubic
                }
            }
            ParallelAnimation {
                NumberAnimation {
                    target: logo; property: "glowOpacity"; to: 0.5
                    duration: Theme.durationLong * 2; easing.type: Easing.InOutSine
                }
                NumberAnimation {
                    target: logo; property: "glowScale"; to: 1
                    duration: Theme.durationLong * 2; easing.type: Easing.InOutSine
                }
            }
        }
    }

    // artwork-begin
    // The centre line of the spiral, from its inner end to the tip with the leaf (x, y pairs).
    readonly property var spiral: [
        66.6, 73.5, 66.4, 73.9, 66.2, 74.3, 65.9, 74.7, 65.6, 75.0, 65.3, 75.3, 64.9, 75.6, 64.4, 75.9,
        63.9, 76.1, 63.4, 76.3, 62.9, 76.5, 62.3, 76.6, 61.7, 76.7, 61.1, 76.7, 60.4, 76.6, 59.8, 76.5,
        59.1, 76.3, 58.5, 76.1, 57.9, 75.7, 57.2, 75.4, 56.6, 74.9, 56.1, 74.5, 55.5, 73.9, 55.0, 73.3,
        54.6, 72.6, 54.2, 71.9, 53.8, 71.1, 53.5, 70.3, 53.3, 69.5, 53.2, 68.6, 53.1, 67.7, 53.1, 66.8,
        53.1, 65.8, 53.3, 64.9, 53.5, 63.9, 53.9, 63.0, 54.3, 62.1, 54.8, 61.1, 55.4, 60.3, 56.0, 59.4,
        56.8, 58.6, 57.6, 57.9, 58.5, 57.2, 59.4, 56.6, 60.5, 56.1, 61.5, 55.6, 62.7, 55.2, 63.9, 55.0,
        65.1, 54.8, 66.3, 54.7, 67.6, 54.7, 68.8, 54.8, 70.1, 55.1, 71.4, 55.4, 72.6, 55.8, 73.9, 56.4,
        75.1, 57.1, 76.2, 57.8, 77.3, 58.7, 78.4, 59.7, 79.3, 60.7, 80.2, 61.9, 81.0, 63.1, 81.7, 64.4,
        82.3, 65.8, 82.8, 67.3, 83.2, 68.8, 83.4, 70.3, 83.6, 71.9, 83.6, 73.5, 83.4, 75.1, 83.2, 76.7,
        82.8, 78.3, 82.2, 79.9, 81.6, 81.4, 80.8, 82.9, 79.8, 84.4, 78.8, 85.8, 77.6, 87.1, 76.3, 88.3,
        74.9, 89.4, 73.4, 90.4, 71.8, 91.3, 70.2, 92.1, 68.4, 92.7, 66.6, 93.2, 64.7, 93.6, 62.8, 93.8,
        60.9, 93.9, 59.0, 93.7, 57.0, 93.5, 55.1, 93.1, 53.1, 92.5, 51.3, 91.7, 49.4, 90.8, 47.7, 89.8,
        46.0, 88.6, 44.4, 87.2, 42.9, 85.7, 41.5, 84.1, 40.2, 82.4, 39.1, 80.5, 38.1, 78.6, 37.3, 76.5,
        36.7, 74.4, 36.2, 72.2, 35.8, 70.0, 35.7, 67.7, 35.7, 65.4, 36.0, 63.1, 36.4, 60.8, 37.0, 58.6,
        37.8, 56.4, 38.8, 54.2, 39.9, 52.1, 41.3, 50.1, 42.8, 48.2, 44.4, 46.4, 46.3, 44.7, 48.2, 43.2,
        50.3, 41.8, 52.5, 40.6, 54.8, 39.5, 57.2, 38.7, 59.7, 38.0, 62.3, 37.6
    ]
    readonly property real penWidth: 9.6
    // Distance along the line at each point.
    readonly property var lengths: {
        const out = [0];
        for (let i = 2; i < spiral.length; i += 2) {
            out.push(out[out.length - 1] + Math.hypot(spiral[i] - spiral[i - 2], spiral[i + 1] - spiral[i - 1]));
        }
        return out;
    }

    /**
     * The first `t` (0..1) of the spiral as an outline with round ends, so that it can be filled
     * with the gradient: one side of the line forwards, the end cap, the other side back.
     */
    function band(t: real): string {
        if (t <= 0) {
            return "";
        }
        const total = lengths[lengths.length - 1];
        const target = Math.min(1, t) * total;
        const pts = [spiral[0], spiral[1]];
        let k = 1;
        while (k < lengths.length && lengths[k] <= target) {
            pts.push(spiral[2 * k], spiral[2 * k + 1]);
            ++k;
        }
        if (k < lengths.length) {
            const f = (target - lengths[k - 1]) / (lengths[k] - lengths[k - 1]);
            pts.push(spiral[2 * k - 2] + (spiral[2 * k] - spiral[2 * k - 2]) * f,
                     spiral[2 * k - 1] + (spiral[2 * k + 1] - spiral[2 * k - 1]) * f);
        }
        const n = pts.length / 2;
        if (n < 2) {
            return "";
        }
        const h = penWidth / 2;
        const left = [];
        const right = [];
        for (let i = 0; i < n; ++i) {
            const a = Math.max(0, i - 1);
            const b = Math.min(n - 1, i + 1);
            let tx = pts[2 * b] - pts[2 * a];
            let ty = pts[2 * b + 1] - pts[2 * a + 1];
            const len = Math.hypot(tx, ty) || 1;
            tx /= len;
            ty /= len;
            left.push((pts[2 * i] - ty * h).toFixed(2) + " " + (pts[2 * i + 1] + tx * h).toFixed(2));
            right.push((pts[2 * i] + ty * h).toFixed(2) + " " + (pts[2 * i + 1] - tx * h).toFixed(2));
        }
        const cap = ` A ${h} ${h} 0 0 0 `;
        return "M " + left.join(" L ") + cap + right[n - 1] + " L " + right.reverse().slice(1).join(" L ")
            + cap + left[0] + " Z";
    }

    Item {
        width: 128
        height: 128
        scale: logo.size / 128
        transformOrigin: Item.TopLeft

        BrandGlow {
            x: -70
            y: -70
            width: 268
            height: 268
            opacity: logo.glowOpacity
            scale: logo.glowScale
        }

        // An outline of the tile that widens and fades as the leaf comes out.
        Rectangle {
            x: 8
            y: 8
            width: 112
            height: 112
            radius: 28
            color: "transparent"
            border.width: 1
            border.color: Theme.brand
            visible: logo.ping > 0 && logo.ping < 1
            opacity: 0.6 * (1 - logo.ping)
            scale: 1 + 0.35 * logo.ping
        }

        Rectangle {
            x: 8
            y: 8
            width: 112
            height: 112
            radius: 28
            opacity: Math.min(1, logo.tileIn)
            scale: 0.82 + 0.18 * logo.tileIn
            gradient: Gradient {
                GradientStop { position: 0; color: Theme.logoTileTop }
                GradientStop { position: 1; color: Theme.logoTileBottom }
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 0.5
                radius: 27.5
                color: "transparent"
                border.width: 1
                border.color: Theme.logoEdge
            }
        }

        Shape {
            width: 128
            height: 128
            visible: logo.draw > 0
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "transparent"
                strokeWidth: -1
                fillGradient: LinearGradient {
                    x1: 32
                    y1: 30
                    x2: 96
                    y2: 98
                    GradientStop { position: 0; color: Theme.brandPeach }
                    GradientStop { position: 0.5; color: Theme.brand }
                    GradientStop { position: 1; color: Theme.brandMagenta }
                }
                PathSvg { path: logo.band(logo.draw) }
            }
        }

        // The leaf grows and turns into place from its stem, at the tip of the spiral.
        Shape {
            visible: logo.leaf > 0
            preferredRendererType: Shape.CurveRenderer
            transform: [
                Scale { xScale: 0.81 * logo.leaf; yScale: 0.81 * logo.leaf },
                Rotation { angle: 20 - 40 * (1 - logo.leaf) },
                Translate { x: 64; y: 37.4 }
            ]
            ShapePath {
                strokeColor: "transparent"
                strokeWidth: -1
                fillGradient: LinearGradient {
                    x1: -7.5
                    y1: -24
                    x2: 7.5
                    y2: 0
                    GradientStop { position: 0; color: Theme.logoLeafLight }
                    GradientStop { position: 1; color: Theme.logoLeafDeep }
                }
                PathSvg { path: "M0 0 C-10 -6 -10 -18 -2 -24 C10 -18 10 -6 0 0 Z" }
            }
        }
    }
    // artwork-end
}
