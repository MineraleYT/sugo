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
    readonly property int change: StatsModel.previousWorkSeconds > 0
        ? Math.round((StatsModel.workSeconds - StatsModel.previousWorkSeconds) * 100 / StatsModel.previousWorkSeconds) : 0

    /// The bucket with the most work ("Thursday", "14:00", "March"), or "".
    readonly property string bestBucket: {
        let best = null;
        for (const bucket of StatsModel.buckets) {
            if (bucket.workSeconds > 0 && (!best || bucket.workSeconds > best.workSeconds)) {
                best = bucket;
            }
        }
        return best ? best.name : "";
    }
    readonly property string bestLabel: StatsModel.period === StatsModel.Period.Day ? i18n("Best hour")
                                      : StatsModel.period === StatsModel.Period.Year ? i18n("Best month") : i18n("Best day")

    /// A small number with its label under it.
    component Figure: ColumnLayout {
        id: figure
        property string value
        property string label
        property string note
        spacing: Theme.hairline * 2
        Text {
            text: figure.value
            color: Theme.foreground
            font.pixelSize: Theme.fontHeading + 2
            font.weight: Font.Medium
            font.features: { "tnum": 1 }
        }
        Text {
            text: figure.label
            color: Theme.muted
            font.pixelSize: Theme.fontCaption
        }
        // Faded rather than hidden: its line stays, so the chart below does not move when a
        // period has a note and the next has none.
        Text {
            opacity: figure.note.length > 0 ? 1 : 0
            text: figure.note.length > 0 ? figure.note : " "
            color: Theme.subtle
            font.pixelSize: Theme.fontSmall
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

    Flickable {
        id: flickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + Theme.space2xl * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

        ColumnLayout {
            id: column
            x: (flickable.width - width) / 2
            y: Theme.space2xl
            width: Math.min(flickable.width - Theme.space2xl * 2, Theme.contentMaxWidth)
            spacing: Theme.space2xl

            // Which stretch of time: back, the period, forward; and how long a stretch.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXs

                SButton {
                    kind: "ghost"
                    iconOnly: true
                    iconName: "chevron-left"
                    text: i18n("Previous")
                    onClicked: StatsModel.previousPeriod()
                }
                Text {
                    text: StatsModel.title
                    color: Theme.foreground
                    font.pixelSize: Theme.fontTitle + 1
                    font.weight: Font.Medium
                    Layout.leftMargin: Theme.spaceXs
                    Layout.rightMargin: Theme.spaceXs
                }
                SButton {
                    kind: "ghost"
                    iconOnly: true
                    iconName: "chevron-right"
                    text: i18n("Next")
                    enabled: StatsModel.canGoNext
                    onClicked: StatsModel.nextPeriod()
                }
                SButton {
                    visible: !StatsModel.isCurrent
                    kind: "ghost"
                    text: i18n("Today")
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

            // The total, how it compares, and three numbers.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space3xl

                ColumnLayout {
                    spacing: Theme.spaceXs
                    Text {
                        text: StatsModel.formatDuration(StatsModel.workSeconds)
                        color: Theme.foreground
                        font.pixelSize: Theme.fontDisplay + Theme.spaceSm
                        font.weight: Font.Medium
                        font.letterSpacing: -Theme.hairline
                        font.features: { "tnum": 1 }
                    }
                    RowLayout {
                        spacing: Theme.spaceSm
                        Text {
                            text: i18n("Focused")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                        Text {
                            visible: page.comparison.length > 0
                            text: page.comparison
                            color: page.change > 0 ? Theme.positive : page.change < 0 ? Theme.danger : Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Figure {
                    value: String(StatsModel.completedWorkSessions)
                    label: i18n("Sessions")
                    note: StatsModel.interruptedWorkSessions > 0
                          ? i18np("+%1 interrupted", "+%1 interrupted", StatsModel.interruptedWorkSessions) : ""
                }
                Figure {
                    value: StatsModel.formatDuration(StatsModel.breakSeconds)
                    label: i18n("Breaks")
                }
                // Always there ("—" until there is a best one), so the figures beside it stay put.
                Figure {
                    value: page.bestBucket.length > 0 ? page.bestBucket : "—"
                    label: page.bestLabel
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

                                // Breaks: a green cap on top of the bar.
                                Rectangle {
                                    visible: bucket.modelData.breakSeconds > 0 && bucket.modelData.workSeconds > 0
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
