// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// The focus page: the time in large, where it sits in the cycle, and the three controls.
QQC2.Page {
    id: page

    /// The top bar shows "Focus / <timer>".
    readonly property string section: i18n("Focus")
    title: PresetModel.currentName
    background: null

    readonly property bool isIdle: TimerEngine.state === TimerEngine.State.Idle
    readonly property bool isWorkPhase: TimerEngine.phase === TimerEngine.Phase.Work
    readonly property bool running: TimerEngine.active && !TimerEngine.paused

    // Stop and Skip ask first when work under way would be cut short: the window asks
    // (Main.qml), as for the search and the menu bar.
    readonly property bool endConfirming: applicationWindow().endPhaseConfirming

    function requestStop(): void {
        applicationWindow().requestEndPhase(false);
    }

    function requestSkip(): void {
        applicationWindow().requestEndPhase(true);
    }

    // --- The cycle, as segments: work, break, work, break… and the long break at the end ---

    readonly property var segments: {
        const cycles = TimerEngine.cyclesBeforeLong;
        const work = Math.max(1, TimerEngine.workSeconds);
        const shortBreak = TimerEngine.shortBreakSeconds;
        const longBreak = TimerEngine.longBreakSeconds;
        const list = [];
        const count = Math.max(1, cycles);
        for (let i = 0; i < count; ++i) {
            list.push({ work: true, seconds: work, label: cycles > 0 ? i18n("Work %1", i + 1) : i18n("Work") });
            const last = i === count - 1;
            if (last && cycles > 0 && longBreak > 0) {
                list.push({ work: false, seconds: longBreak, label: i18n("Long break") });
            } else if (shortBreak > 0) {
                list.push({ work: false, seconds: shortBreak, label: i18n("Short break") });
            }
        }
        return list;
    }

    /// Which segment the timer is in.
    readonly property int currentSegment: {
        const done = TimerEngine.completedCycles;
        if (isWorkPhase) {
            const wanted = Math.min(done, Math.max(0, TimerEngine.cyclesBeforeLong - 1));
            let works = 0;
            for (let i = 0; i < segments.length; ++i) {
                if (segments[i].work) {
                    if (works === wanted) {
                        return i;
                    }
                    ++works;
                }
            }
            return 0;
        }
        if (TimerEngine.phase === TimerEngine.Phase.LongBreak) {
            return segments.length - 1;
        }
        // The break after the work just finished.
        let works = 0;
        for (let i = 0; i < segments.length; ++i) {
            if (segments[i].work) {
                ++works;
            } else if (works >= Math.max(1, done)) {
                return i;
            }
        }
        return segments.length - 1;
    }

    readonly property string phaseText: {
        if (isIdle) {
            return isWorkPhase ? i18n("Ready to focus") : i18n("Time for a break");
        }
        if (TimerEngine.paused) {
            return i18n("Paused");
        }
        if (isWorkPhase) {
            const category = PresetModel.currentCategory.trim();
            return category.length > 0 ? i18nc("focusing · category", "Focusing · %1", category) : i18n("Focusing");
        }
        return TimerEngine.phase === TimerEngine.Phase.LongBreak ? i18n("Long break") : i18n("Short break");
    }

    readonly property color phaseColor: isIdle ? Theme.muted
                                       : TimerEngine.paused ? Theme.warning
                                       : isWorkPhase ? Theme.brand : Theme.breakColor

    function pad(n: int): string {
        return String(n).padStart(2, "0");
    }
    readonly property int hours: Math.floor(TimerEngine.remainingSeconds / 3600)
    readonly property string minutesText: hours > 0 ? hours + ":" + pad(Math.floor((TimerEngine.remainingSeconds % 3600) / 60))
                                                    : pad(Math.floor(TimerEngine.remainingSeconds / 60))
    readonly property string secondsText: pad(TimerEngine.remainingSeconds % 60)

    readonly property real clockSize: Math.max(Theme.space4xl * 1.3, Math.min(page.width * 0.17, page.height * 0.2, Theme.space4xl * 2.6))
    readonly property real trackWidth: Math.min(page.width - Theme.space3xl * 2, Theme.contentMaxWidth * 0.62)

    // A soft glow of the phase colour behind the clock, only while time is running: a circle
    // whose colour fades to nothing at its edge, stretched into a wide ellipse.
    Shape {
        id: glow
        readonly property color tint: page.isWorkPhase ? Theme.brand : Theme.breakColor
        readonly property real diameter: page.clockSize * 4.4
        anchors.centerIn: clockColumn
        anchors.verticalCenterOffset: -page.clockSize * 0.3
        width: diameter
        height: diameter
        transform: Scale {
            origin.x: glow.width / 2
            origin.y: glow.height / 2
            xScale: 1.5
            yScale: 0.9
        }
        opacity: page.running ? (Theme.dark ? 1 : 0.6) : 0
        visible: opacity > 0
        preferredRendererType: Shape.CurveRenderer
        Behavior on opacity { NumberAnimation { duration: Theme.durationLong * 3; easing.type: Easing.InOutQuad } }

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.diameter / 2
                centerY: glow.diameter / 2
                centerRadius: glow.diameter / 2
                focalX: centerX
                focalY: centerY
                GradientStop { position: 0; color: Theme.alpha(glow.tint, 0.16) }
                GradientStop { position: 0.4; color: Theme.alpha(glow.tint, 0.08) }
                GradientStop { position: 0.75; color: Theme.alpha(glow.tint, 0.02) }
                GradientStop { position: 1; color: Theme.alpha(glow.tint, 0) }
            }
            PathAngleArc {
                centerX: glow.diameter / 2
                centerY: glow.diameter / 2
                radiusX: glow.diameter / 2
                radiusY: glow.diameter / 2
                startAngle: 0
                sweepAngle: 360
            }
        }
    }

    ColumnLayout {
        id: clockColumn
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -Theme.spaceXl
        width: Math.min(implicitWidth, page.width - Theme.space2xl * 2)
        spacing: Theme.space2xl

        // The phase, in its colour.
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: phaseRow.implicitWidth + Theme.spaceLg * 2
            implicitHeight: phaseRow.implicitHeight + Theme.spaceSm * 2
            radius: height / 2
            color: Theme.alpha(page.phaseColor, page.isIdle ? 0.08 : 0.1)
            border.width: Theme.hairline
            border.color: Theme.alpha(page.phaseColor, page.isIdle ? 0.25 : 0.3)
            Behavior on color { ColorAnimation { duration: Theme.durationLong } }
            Accessible.role: Accessible.StaticText
            Accessible.name: page.phaseText

            RowLayout {
                id: phaseRow
                anchors.centerIn: parent
                spacing: Theme.spaceMd
                Rectangle {
                    implicitWidth: Theme.spaceSm
                    implicitHeight: Theme.spaceSm
                    radius: width / 2
                    color: page.phaseColor
                    SequentialAnimation on opacity {
                        running: page.running
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 0.3; duration: Theme.durationLong * 4; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1; duration: Theme.durationLong * 4; easing.type: Easing.InOutSine }
                    }
                }
                Text {
                    // A long category is cut: the pill is never wider than the track below.
                    Layout.maximumWidth: Math.max(0, page.trackWidth - Theme.spaceLg * 2 - Theme.spaceSm - phaseRow.spacing)
                    elide: Text.ElideRight
                    text: page.phaseText
                    color: page.isIdle ? Theme.muted : Qt.lighter(page.phaseColor, Theme.dark ? 1.3 : 0.8)
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Medium
                }
            }
        }

        // The time left: minutes, a quiet colon, seconds.
        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: -Theme.spaceMd
            Accessible.role: Accessible.StaticText
            // In words, unit by unit ("1 hour 5 minutes 3 seconds left"), not the clock's "1:05".
            Accessible.name: {
                const total = TimerEngine.remainingSeconds;
                const minutes = Math.floor((total % 3600) / 60);
                const parts = [];
                if (page.hours > 0) {
                    parts.push(i18np("%1 hour", "%1 hours", page.hours));
                }
                parts.push(i18np("%1 minute", "%1 minutes", minutes));
                parts.push(i18np("%1 second", "%1 seconds", total % 60));
                return i18nc("time left, e.g. 5 minutes 3 seconds", "%1 left", parts.join(" "));
            }
            opacity: TimerEngine.paused ? 0.55 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.durationLong } }

            Text {
                text: page.minutesText
                color: Theme.foreground
                font.pixelSize: page.clockSize
                font.weight: Font.Medium
                font.letterSpacing: -page.clockSize * 0.04
                font.features: { "tnum": 1 }
            }
            Text {
                text: ":"
                color: Theme.alpha(Theme.muted, 0.6)
                font.pixelSize: page.clockSize
                font.weight: Font.Medium
                leftPadding: page.clockSize * 0.02
                rightPadding: page.clockSize * 0.04
            }
            Text {
                text: page.secondsText
                color: Theme.foreground
                font.pixelSize: page.clockSize
                font.weight: Font.Medium
                font.letterSpacing: -page.clockSize * 0.04
                font.features: { "tnum": 1 }
            }
        }

        // The whole cycle: each phase as wide as it lasts; the current one fills as time goes.
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: page.trackWidth
            spacing: Theme.spaceSm

            Item {
                id: track
                objectName: "cycleTrack"
                Layout.fillWidth: true
                implicitHeight: Theme.spaceSm
                // A long set (up to 99 work phases) still fits: the gaps take at most half the
                // track, the segments' minimum width shrinks to their share of the rest, and the
                // track clips what rounding leaves over.
                clip: true
                readonly property int count: page.segments.length
                readonly property real total: page.segments.reduce((sum, s) => sum + s.seconds, 0)
                readonly property real gap: count > 1 ? Math.min(Theme.spaceXs, width / 2 / (count - 1)) : 0
                readonly property real available: Math.max(0, width - gap * (count - 1))
                readonly property real minSegment: Math.min(Theme.spaceSm, available / Math.max(1, count))
                // Each segment as long as its phase, at least minSegment; when those minimums would
                // push the row past the track, each gets the minimum plus its share of what is left
                // (the hairline only absorbs floating-point error in a sum that fits).
                readonly property bool crowded: page.segments.reduce(
                    (sum, s) => sum + Math.max(minSegment, available * s.seconds / Math.max(1, total)), 0) > available + Theme.hairline
                function segmentWidth(seconds: real): real {
                    return crowded ? minSegment + (available - minSegment * count) * seconds / Math.max(1, total)
                                   : Math.max(minSegment, available * seconds / Math.max(1, total));
                }

                Row {
                    objectName: "cycleSegments"
                    anchors.fill: parent
                    spacing: track.gap

                    Repeater {
                        model: page.segments

                        Rectangle {
                            id: segment
                            required property var modelData
                            required property int index
                            objectName: "cycleSegment-" + index
                            // Also while the next phase waits for Start (Manual mode): the work
                            // done in this set stays done. Stop and the end of the set reset the
                            // count, so nothing shows done then, nor before any work is counted.
                            readonly property bool done: index < page.currentSegment && TimerEngine.completedCycles > 0
                            readonly property bool current: index === page.currentSegment
                            width: track.segmentWidth(modelData.seconds)
                            height: track.height
                            radius: height / 2
                            color: Theme.track
                            clip: true

                            Rectangle {
                                height: parent.height
                                radius: parent.radius
                                width: segment.done ? parent.width
                                     : segment.current && !page.isIdle ? parent.width * TimerEngine.progress : 0
                                Behavior on width { NumberAnimation { duration: Theme.durationLong; easing.type: Easing.OutCubic } }
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: segment.modelData.work ? Theme.brandPeach : Theme.breakLight }
                                    GradientStop { position: 1; color: segment.modelData.work ? Theme.brand : Theme.breakColor }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: page.segments[page.currentSegment] ? page.segments[page.currentSegment].label : ""
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                }
                Item {
                    Layout.fillWidth: true
                }
                Text {
                    readonly property var next: page.segments[(page.currentSegment + 1) % page.segments.length]
                    text: next ? i18nc("the phase after this one, its minutes", "Next: %1 · %2 min", next.label, Math.round(next.seconds / 60)) : ""
                    color: Theme.subtle
                    font.pixelSize: Theme.fontCaption
                }
            }
        }

        // Stop · start/pause · skip.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spaceSm
            spacing: Theme.spaceXl

            RoundButton {
                diameter: Theme.space3xl + Theme.spaceMd
                iconName: "stop"
                text: i18n("Stop")
                enabled: TimerEngine.active
                onClicked: page.requestStop()
            }
            RoundButton {
                diameter: Theme.space4xl + Theme.spaceXl
                primary: true
                iconName: page.isIdle || TimerEngine.paused ? "play" : "pause"
                text: TimerEngine.paused ? i18n("Resume") : page.isIdle ? (page.isWorkPhase ? i18n("Start") : i18n("Start break")) : i18n("Pause")
                onClicked: TimerEngine.toggle()
            }
            RoundButton {
                diameter: Theme.space3xl + Theme.spaceMd
                iconName: "skip"
                text: i18n("Skip")
                enabled: TimerEngine.active
                onClicked: page.requestSkip()
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spaceSm
            Kbd {
                text: i18nc("keyboard key", "Space")
            }
            Text {
                id: spaceHint
                text: TimerEngine.active && !TimerEngine.paused ? i18n("pause") : i18n("start")
                color: Theme.subtle
                font.pixelSize: Theme.fontCaption
                // As wide as the wider word.
                Layout.preferredWidth: Math.ceil(Math.max(hintMetrics.advanceWidth(i18n("pause")), hintMetrics.advanceWidth(i18n("start"))))
                FontMetrics {
                    id: hintMetrics
                    font: spaceHint.font
                }
            }
            // Faded rather than hidden: the hints beside them keep their place.
            Kbd {
                Layout.leftMargin: Theme.spaceSm
                opacity: TimerEngine.active ? 1 : 0
                text: i18nc("keyboard key", "S")
            }
            Text {
                opacity: TimerEngine.active ? 1 : 0
                text: i18n("skip")
                color: Theme.subtle
                font.pixelSize: Theme.fontCaption
            }
        }
    }

    // Paused by the screen lock or by sleep: say so, and offer to go on.
    // Over the clock and out of its column, so the clock does not move when it comes or goes.
    MessageSlot {
        objectName: "lockPauseMessage"
        anchors.bottom: clockColumn.top
        anchors.bottomMargin: Theme.spaceXl
        anchors.horizontalCenter: clockColumn.horizontalCenter
        width: page.trackWidth
        lines: 2
        shown: (ScreenLockWatcher.pausedByScreenLock || ScreenLockWatcher.pausedBySleep) && TimerEngine.paused
        tone: "warning"
        text: ScreenLockWatcher.pausedBySleep ? i18n("The timer was paused because the computer went to sleep.")
                                              : i18n("The timer was paused because your screen was locked.")
        SButton {
            kind: "ghost"
            text: i18n("Resume")
            onClicked: {
                ScreenLockWatcher.clearLockPauseNotice();
                TimerEngine.resume();
            }
        }
        SButton {
            kind: "ghost"
            iconOnly: true
            iconName: "close"
            text: i18n("Dismiss")
            onClicked: ScreenLockWatcher.clearLockPauseNotice()
        }
    }

    // Reminder that the desktop is not showing notifications because of us.
    RowLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spaceXl
        spacing: Theme.spaceSm
        visible: SilenceController.available && SilenceController.active
        LineIcon {
            name: "bell-off"
            size: Theme.iconSm
            color: Theme.subtle
        }
        Text {
            text: i18n("Notifications silenced until the break")
            color: Theme.subtle
            font.pixelSize: Theme.fontCaption
        }
    }

    /// A round control: the main one is filled with the text colour.
    component RoundButton: QQC2.AbstractButton {
        id: round
        property real diameter: Theme.space3xl
        property bool primary: false
        property string iconName

        implicitWidth: diameter
        implicitHeight: diameter
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.name: text
        QQC2.ToolTip.visible: hovered && !primary
        QQC2.ToolTip.text: text
        QQC2.ToolTip.delay: Theme.toolTipDelay

        background: Rectangle {
            radius: width / 2
            color: round.primary ? Theme.alpha(Theme.primary, round.down ? 0.8 : (round.hovered ? 0.9 : 1))
                                 : (round.down ? Theme.pressed : (round.hovered && round.enabled ? Theme.hover : "transparent"))
            border.width: round.primary ? 0 : Theme.hairline
            border.color: Theme.borderStrong
            scale: round.down ? 0.96 : 1
            Behavior on scale { NumberAnimation { duration: Theme.durationShort } }
            Behavior on color { ColorAnimation { duration: Theme.durationShort } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -Theme.hairline * 3
                radius: width / 2
                color: "transparent"
                border.width: Theme.hairline
                border.color: Theme.focusRing
                visible: round.visualFocus
            }
        }
        contentItem: Item {
            LineIcon {
                anchors.centerIn: parent
                name: round.iconName
                size: round.primary ? Theme.iconMd + Theme.spaceXs : Theme.icon
                color: round.primary ? Theme.primaryText : (round.enabled ? Theme.foreground : Theme.subtle)
            }
        }
    }
}
