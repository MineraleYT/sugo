// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// How much was done: the period's total and how it compares, one chart, where the time went,
/// and the streak.
QQC2.Page {
    id: page

    title: i18n("Statistics")
    background: null

    /// Laid out with real sizes (read by the smoke test).
    readonly property bool contentLaidOut: column.width > 0 && chart.width > 0

    // The model last caught up with the clock up to a few minutes ago: show "now" as it is.
    Component.onCompleted: StatsModel.checkDateChange()

    readonly property var periods: [
        { id: "day", text: i18n("Day"), value: StatsModel.Period.Day },
        { id: "week", text: i18n("Week"), value: StatsModel.Period.Week },
        { id: "month", text: i18n("Month"), value: StatsModel.Period.Month },
        { id: "year", text: i18n("Year"), value: StatsModel.Period.Year }
    ]
    readonly property string periodId: periods.find(p => p.value === StatsModel.period)?.id ?? "week"

    readonly property var breakdowns: [
        { id: "timer", text: i18n("Timer"), value: StatsModel.BreakdownMode.ByTimer },
        { id: "category", text: i18n("Category"), value: StatsModel.BreakdownMode.ByCategory },
        { id: "task", text: i18n("Task"), value: StatsModel.BreakdownMode.ByTask }
    ]
    readonly property string breakdownId: breakdowns.find(b => b.value === StatsModel.breakdownMode)?.id ?? "timer"


    /// "+35% vs last week", or nothing to compare with.
    readonly property string comparison: {
        const before = StatsModel.previousWorkSeconds;
        if (before <= 0) {
            return "";
        }
        const change = Math.round((StatsModel.workSeconds - before) * 100 / before);
        const sign = change > 0 ? "+" : "";
        switch (StatsModel.period) {
        case StatsModel.Period.Day:
            return i18nc("change against the day before", "%1%2% vs the day before", sign, change);
        case StatsModel.Period.Week:
            return i18nc("change against the week before", "%1%2% vs the week before", sign, change);
        case StatsModel.Period.Month:
            return i18nc("change against the month before", "%1%2% vs the month before", sign, change);
        default:
            return i18nc("change against the year before", "%1%2% vs the year before", sign, change);
        }
    }
    /// One cell of the figures strip: a number, its label, and a quieter line under them.
    /// `index` places it in the strip, for the lines between the cells.
    component Figure: Item {
        id: figure
        property int index
        property string value
        /// A word after the value, in the same size ("days").
        property string unit
        property string label
        property string note
        readonly property int columns: figures.columns
        Layout.fillWidth: true
        // The first cell is a little wider, for the total and its comparison.
        Layout.preferredWidth: !page.compact && index === 0 ? 7 : 5
        implicitHeight: figureColumn.implicitHeight + Theme.spaceLg * 2

        // The lines between the cells.
        Rectangle {
            visible: figure.index % figure.columns > 0
            width: Theme.hairline
            height: parent.height
            color: Theme.border
        }
        Rectangle {
            visible: figure.index >= figure.columns
            width: parent.width
            height: Theme.hairline
            color: Theme.border
        }

        ColumnLayout {
            id: figureColumn
            anchors.fill: parent
            anchors.topMargin: Theme.spaceLg
            anchors.bottomMargin: Theme.spaceLg
            anchors.leftMargin: Theme.spaceLg + Theme.hairline * 2
            anchors.rightMargin: Theme.spaceLg + Theme.hairline * 2
            spacing: Theme.hairline * 2

            Text {
                text: figure.unit.length > 0 ? figure.value + " " + figure.unit : figure.value
                color: Theme.foreground
                font.pixelSize: Theme.fontHeading
                font.weight: Font.Medium
                font.letterSpacing: -Theme.hairline / 2
                font.features: { "tnum": 1 }
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: figure.label
                color: Theme.muted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // Faded rather than hidden: its line stays, so nothing below moves when a period
            // has a note and the next has none.
            Text {
                opacity: figure.note.length > 0 ? 1 : 0
                text: figure.note.length > 0 ? figure.note : " "
                color: Theme.subtle
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    /// A bordered surface with a title.
    component Panel: Rectangle {
        id: panel
        default property alias content: panelColumn.data
        property string title
        property alias headerExtra: panelExtra.data
        implicitHeight: panelColumn.implicitHeight + Theme.spaceLg * 2
        radius: Theme.radiusLg
        color: Theme.card
        border.width: Theme.hairline
        border.color: Theme.border

        ColumnLayout {
            id: panelColumn
            anchors.fill: parent
            anchors.margins: Theme.spaceLg
            anchors.leftMargin: Theme.spaceXl
            anchors.rightMargin: Theme.spaceXl
            spacing: Theme.spaceMd

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: panel.title
                    color: Theme.foreground
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                RowLayout {
                    id: panelExtra
                    spacing: Theme.spaceXs
                }
            }
        }
    }

    /// A narrow window: smaller margins, the figures two by two, the panels one under the other.
    readonly property bool compact: width < Theme.compactBreakpoint
    readonly property real paddingX: compact ? Theme.pagePaddingCompact : Theme.pagePaddingX
    readonly property real paddingY: compact ? Theme.pagePaddingCompact : Theme.pagePaddingY

    Flickable {
        id: flickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + page.paddingY * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

        ColumnLayout {
            id: column
            x: (flickable.width - width) / 2
            y: page.paddingY
            width: Math.min(flickable.width - page.paddingX * 2, Theme.statsMaxWidth)
            spacing: page.compact ? Theme.spaceLg : Theme.spaceXl

            // Which stretch of time: back, the period, forward; and how long a stretch.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                SButton {
                    kind: "ghost"
                    iconOnly: true
                    iconName: "chevron-left"
                    text: i18n("Previous")
                    implicitHeight: Theme.iconButton
                    onClicked: StatsModel.previousPeriod()
                }
                // Its own width at most (the spacer takes the rest), cut short when the row is
                // too narrow for it and the period buttons.
                Text {
                    text: StatsModel.title
                    color: Theme.foreground
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: implicitWidth
                }
                SButton {
                    kind: "ghost"
                    iconOnly: true
                    iconName: "chevron-right"
                    text: i18n("Next")
                    implicitHeight: Theme.iconButton
                    enabled: StatsModel.canGoNext
                    onClicked: StatsModel.nextPeriod()
                }
                SButton {
                    visible: !StatsModel.isCurrent
                    kind: "ghost"
                    text: i18n("Today")
                    implicitHeight: Theme.iconButton
                    onClicked: StatsModel.today()
                }
                Item {
                    Layout.fillWidth: true
                }
                Segmented {
                    model: page.periods
                    current: page.periodId
                    onActivated: id => StatsModel.period = page.periods.find(p => p.id === id).value
                }
            }

            // The total and how it compares, the sessions, the breaks and the streak, in one strip
            // (two by two in a narrow window).
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: figures.implicitHeight
                radius: Theme.radiusLg
                color: Theme.card
                border.width: Theme.hairline
                border.color: Theme.border

                GridLayout {
                    id: figures
                    anchors.left: parent.left
                    anchors.right: parent.right
                    columns: page.compact ? 2 : 4
                    columnSpacing: 0
                    rowSpacing: 0

                    Figure {
                        index: 0
                        value: StatsModel.formatDuration(StatsModel.workSeconds)
                        label: i18n("Focused")
                        note: page.comparison
                    }
                    Figure {
                        index: 1
                        value: String(StatsModel.completedWorkSessions)
                        label: i18n("Sessions")
                        note: StatsModel.interruptedWorkSessions > 0
                              ? i18np("+%1 interrupted", "+%1 interrupted", StatsModel.interruptedWorkSessions) : ""
                    }
                    Figure {
                        index: 2
                        value: StatsModel.formatDuration(StatsModel.breakSeconds)
                        label: i18n("Breaks")
                    }
                    // Days in a row, whatever the period shown.
                    Figure {
                        index: 3
                        value: String(StatsModel.currentStreak)
                        unit: i18np("day", "days", StatsModel.currentStreak)
                        label: i18n("Streak")
                        note: i18n("Best %1", StatsModel.bestStreak)
                    }
                }
            }

            // One chart: work per hour, day or month, in the brand gradient; breaks as a thin cap.
            Panel {
                Layout.fillWidth: true
                title: i18n("Activity")

                Item {
                    id: chart
                    Layout.fillWidth: true
                    implicitHeight: Theme.space4xl * 3.5

                    readonly property var buckets: StatsModel.buckets
                    // The tallest stack (work and the break cap on it), with a little room above.
                    readonly property real scaleTop: Math.max(1, buckets.reduce((m, b) => Math.max(m, b.workSeconds + b.breakSeconds), 0) * 1.12)
                    readonly property real labelHeight: Theme.spaceXl + Theme.spaceXs
                    readonly property real plotHeight: height - labelHeight - Theme.spaceSm
                    readonly property real slot: buckets.length > 0 ? width / buckets.length : width
                    // Labels thin out when there are many bars (a month, a day's hours).
                    readonly property int labelEvery: slot < Theme.space3xl ? Math.ceil(Theme.space3xl / Math.max(1, slot)) : 1

                    // Baseline.
                    Rectangle {
                        y: chart.plotHeight
                        width: parent.width
                        height: Theme.hairline
                        color: Theme.border
                    }

                    Repeater {
                        model: chart.buckets

                        Item {
                            id: bucket
                            required property var modelData
                            required property int index
                            x: index * chart.slot
                            width: chart.slot
                            height: chart.height

                            readonly property real workHeight: chart.plotHeight * modelData.workSeconds / chart.scaleTop
                            readonly property real breakHeight: chart.plotHeight * modelData.breakSeconds / chart.scaleTop

                            // The tooltip's text, for screen readers too.
                            Accessible.role: Accessible.StaticText
                            Accessible.name: modelData.tooltip

                            HoverHandler {
                                id: bucketHover
                            }
                            QQC2.ToolTip.visible: bucketHover.hovered && (modelData.workSeconds > 0 || modelData.breakSeconds > 0)
                            QQC2.ToolTip.text: modelData.tooltip
                            QQC2.ToolTip.delay: Theme.durationShort

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.max(Theme.spaceXs, Math.min(chart.slot * 0.62, Theme.space3xl + Theme.spaceXs))
                                y: chart.plotHeight - height
                                height: Math.max(bucket.modelData.workSeconds > 0 ? Theme.spaceXs : Theme.hairline * 2, bucket.workHeight)
                                radius: Math.min(width / 2, Theme.radiusSm)
                                opacity: bucketHover.hovered ? 0.85 : 1
                                color: Theme.track
                                gradient: bucket.modelData.workSeconds > 0 ? barGradient : null
                                Gradient {
                                    id: barGradient
                                    GradientStop { position: 0; color: Theme.brand }
                                    GradientStop { position: 1; color: Theme.brandMagenta }
                                }
                                Behavior on height { NumberAnimation { duration: Theme.durationLong; easing.type: Easing.OutCubic } }

                                // Breaks: a green cap on top of the bar, or on the empty stub when
                                // there were only breaks (the chart then has data to show).
                                Rectangle {
                                    visible: bucket.modelData.breakSeconds > 0
                                    anchors.bottom: parent.top
                                    anchors.bottomMargin: Theme.hairline * 2
                                    width: parent.width
                                    height: Math.max(Theme.hairline * 2, bucket.breakHeight)
                                    radius: parent.radius
                                    color: Theme.alpha(Theme.breakColor, 0.7)
                                }
                            }

                            Text {
                                visible: bucket.index % chart.labelEvery === 0
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: chart.plotHeight + Theme.spaceSm
                                text: bucket.modelData.label
                                color: bucket.modelData.isCurrent ? Theme.foreground : Theme.subtle
                                font.pixelSize: Theme.fontSmall
                                font.weight: bucket.modelData.isCurrent ? Font.Medium : Font.Normal
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -chart.labelHeight / 2
                        visible: !StatsModel.hasData
                        text: i18n("No focus time in this period yet.")
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: column.width >= Theme.contentMaxWidth * 0.8 ? 2 : 1
                columnSpacing: Theme.spaceLg
                rowSpacing: Theme.spaceLg

                // Where the time went, by timer, category or task.
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 3
                    Layout.alignment: Qt.AlignTop
                    title: i18n("Where the time went")
                    headerExtra: Segmented {
                        model: page.breakdowns
                        current: page.breakdownId
                        onActivated: id => StatsModel.breakdownMode = page.breakdowns.find(b => b.id === id).value
                    }

                    Text {
                        visible: StatsModel.breakdown.length === 0
                        text: i18n("Nothing recorded in this period.")
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                        Layout.topMargin: Theme.spaceXs
                    }

                    Repeater {
                        model: StatsModel.breakdown.slice(0, 8)

                        RowLayout {
                            id: entry
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd

                            Text {
                                text: entry.modelData.name
                                color: Theme.foreground
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                                Layout.preferredWidth: Theme.space4xl * 2.4
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: Theme.spaceXs
                                radius: height / 2
                                color: Theme.track
                                Rectangle {
                                    width: parent.width * Math.min(1, entry.modelData.percentage / 100)
                                    height: parent.height
                                    radius: parent.radius
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: Theme.brandPeach }
                                        GradientStop { position: 1; color: Theme.brand }
                                    }
                                }
                            }
                            Text {
                                text: entry.modelData.formattedDuration
                                color: Theme.foreground
                                font.pixelSize: Theme.fontCaption + 1
                                font.features: { "tnum": 1 }
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: Theme.space4xl + Theme.spaceMd
                            }
                        }
                    }
                }

                // Days in a row.
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 2
                    Layout.alignment: Qt.AlignTop
                    title: i18n("Streak")

                    RowLayout {
                        spacing: Theme.spaceSm
                        Text {
                            text: StatsModel.currentStreak
                            color: Theme.foreground
                            font.pixelSize: Theme.fontDisplay + Theme.spaceSm
                            font.weight: Font.Medium
                        }
                        Text {
                            Layout.alignment: Qt.AlignBaseline
                            text: i18np("day", "days", StatsModel.currentStreak)
                            color: Theme.muted
                            font.pixelSize: Theme.fontBody
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Text {
                            Layout.alignment: Qt.AlignBaseline
                            text: i18n("Best %1", StatsModel.bestStreak)
                            color: Theme.subtle
                            font.pixelSize: Theme.fontCaption
                        }
                    }

                    // The days of the period with focus time, as a calendar: one column per weekday.
                    Grid {
                        id: calendar
                        visible: StatsModel.period === StatsModel.Period.Week || StatsModel.period === StatsModel.Period.Month
                        columns: 7
                        spacing: Theme.spaceXs
                        // Empty cells before the first day, so each column is one weekday.
                        readonly property int lead: StatsModel.days.length > 0
                            ? (StatsModel.days[0].weekday - Qt.locale().firstDayOfWeek + 7) % 7 : 0

                        Repeater {
                            model: calendar.lead
                            Item {
                                width: Theme.spaceLg + Theme.spaceXs
                                height: width
                            }
                        }
                        Repeater {
                            model: StatsModel.days
                            Rectangle {
                                required property var modelData
                                width: Theme.spaceLg + Theme.spaceXs
                                height: width
                                radius: Theme.radiusSm - 1
                                color: modelData.workSeconds > 0 ? Theme.brand : Theme.track
                                opacity: modelData.workSeconds > 0 ? Math.min(1, 0.45 + modelData.workSeconds / 7200) : 1
                                border.width: modelData.isToday ? Theme.hairline : 0
                                border.color: Theme.foreground
                                Accessible.role: Accessible.StaticText
                                Accessible.name: modelData.tooltip
                                HoverHandler {
                                    id: dayHover
                                }
                                QQC2.ToolTip.visible: dayHover.hovered
                                QQC2.ToolTip.text: modelData.tooltip
                                QQC2.ToolTip.delay: Theme.durationShort
                            }
                        }
                    }

                }
            }
        }
    }
}
