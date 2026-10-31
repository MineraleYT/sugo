// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import io.github.mineraleyt.sugo

/// Settings of the program itself. The options of each timer live on that timer's own page.
QQC2.Page {
    id: page

    title: i18n("Settings")
    background: null

    // The settings are split in categories; only the cards of the chosen one are shown.
    property string category: "general"
    readonly property var categories: [
        { id: "general", text: i18n("General") },
        { id: "sounds", text: i18n("Sounds") },
        { id: "focus", text: i18n("Focus") },
        { id: "calendar", text: i18n("Calendar") },
        { id: "advanced", text: i18n("Advanced") }
    ]

    /// Index of the entry of `list` (an array of { value } objects) whose value is `value`;
    /// 0 when there is none. Used by the combo box bindings.
    function valueIndex(list: var, value: var): int {
        for (let i = 0; i < list.length; ++i) {
            if (list[i].value === value) {
                return i;
            }
        }
        return 0;
    }

    // Calendar sync state, guarded because the singleton can be gone at shutdown.
    readonly property bool calOn: CalendarSync ? CalendarSync.enabled : false
    readonly property string calProvider: CalendarSync ? CalendarSync.provider : "nextcloud"
    readonly property bool calCustom: calProvider === "caldav"
    readonly property bool calHasPassword: CalendarSync ? CalendarSync.hasPassword : false
    readonly property bool calConnected: calHasPassword && !calCustom
    readonly property int calStatus: CalendarSync ? CalendarSync.status : 0
    readonly property bool calWaiting: CalendarSync ? calStatus === CalendarSync.WaitingForBrowser : false
    readonly property int calPending: CalendarSync ? CalendarSync.pendingCount : 0
    // Signed in with something to send events to: password and user name are stored.
    readonly property bool calConfigured: calHasPassword && CalendarSync !== null && CalendarSync.username.length > 0
    // While the server is being asked, only the button that started the request is greyed out
    // (the two buttons used to dim together, which looked like both had been pressed).
    property string calAction: ""   // "refresh" or "test"
    readonly property bool calWorking: CalendarSync ? calStatus === CalendarSync.Working : false
    readonly property bool calFailed: CalendarSync ? calStatus === CalendarSync.Failed : false
    readonly property bool calNotConfigured: CalendarSync ? calStatus === CalendarSync.NotConfigured : false
    readonly property bool calRefreshing: calWorking && calAction === "refresh"
    // A successful connection test is shown on the Test button itself (a green check for a few
    // seconds); failures are explained in the message at the bottom of the card.
    property bool calTestOk: false
    onCalWorkingChanged: {
        if (calWorking) {
            calTestOk = false;
        } else {
            if (calAction === "test" && CalendarSync && calStatus === CalendarSync.Ready) {
                calTestOk = true;
                calTestOkTimer.restart();
            }
            calAction = "";
        }
    }
    Timer {
        id: calTestOkTimer
        interval: 3000
        onTriggered: page.calTestOk = false
    }
    readonly property string calLastSuccess: {
        const d = CalendarSync ? CalendarSync.lastSuccess : null;
        if (!d) {
            return "";
        }
        const date = new Date(d);
        return isNaN(date.getTime()) ? "" : date.toLocaleString(Qt.locale(), Locale.ShortFormat);
    }
    // Calendars to pick from; the saved one alone when no list has been fetched yet.
    readonly property var calChoices: {
        if (!CalendarSync) {
            return [];
        }
        const list = CalendarSync.calendars;
        if (list && list.length > 0) {
            return list;
        }
        if (CalendarSync.calendarUrl.length > 0) {
            return [{ name: CalendarSync.calendarName.length > 0 ? CalendarSync.calendarName : CalendarSync.calendarUrl,
                      url: CalendarSync.calendarUrl, color: "" }];
        }
        return [];
    }
    function calChoiceIndex(): int {
        const url = CalendarSync ? CalendarSync.calendarUrl : "";
        for (let i = 0; i < calChoices.length; ++i) {
            if (calChoices[i].url === url) {
                return i;
            }
        }
        return -1;
    }

    // The calendar list is not kept between runs, and calendars made on the server since the last
    // visit should show up: reload it when the page opens, if the calendar is on and signed in,
    // unless it was fetched a short while ago (the refresh button always reloads).
    Component.onCompleted: {
        if (CalendarSync && CalendarSync.enabled && CalendarSync.hasPassword && !calWaiting) {
            CalendarSync.refreshCalendarsIfStale();
        }
    }

    /// Disconnect forgets the password: asked first.
    function requestDisconnect(): void {
        disconnectDialog.open();
    }
    readonly property bool disconnectConfirming: disconnectDialog.visible

    ConfirmDialog {
        id: disconnectDialog
        title: i18n("Disconnect the Calendar?")
        text: i18n("Sugo forgets the password for this calendar service and stops sending events. You can sign in again at any time.")
        confirmText: i18n("Disconnect")
        confirmKind: "danger"
        onConfirmed: {
            disconnectDialog.close();
            if (CalendarSync) {
                CalendarSync.disconnect();
            }
        }
    }

    // Result of the last diagnostics action: "", "copied", "exported" or "exportFailed".
    property string diagnosticsResult: ""
    property bool diagnosticsDismissed: false
    // Translated reason of the last failed export, from Diagnostics.lastExportError().
    property string diagnosticsExportError: ""

    function showDiagnosticsResult(result: string): void {
        diagnosticsResult = result;
        diagnosticsDismissed = false;
    }

    Connections {
        target: AppSettings
        function onAutostartFailed(message) {
            applicationWindow().showPassiveNotification(message);
        }
    }

    Connections {
        target: DataManager
        function onOperationFailed(message) {
            applicationWindow().showPassiveNotification(message);
        }
        function onOperationSucceeded(message) {
            applicationWindow().showPassiveNotification(message);
        }
    }

    ConfirmDialog {
        id: clearHistoryDialog
        title: i18n("Clear session history?")
        text: i18n("This permanently removes all recorded sessions and statistics. Presets and settings will remain.")
        confirmText: i18n("Clear History")
        confirmKind: "danger"
        onConfirmed: {
            DataManager.clearHistory();
            clearHistoryDialog.close();
        }
    }

    ConfirmDialog {
        id: restoreConfirmDialog
        title: i18n("Restore this backup?")
        text: i18n("Restoring replaces your current presets and session history with the contents of the selected backup.")
        confirmText: i18n("Restore")
        onConfirmed: {
            DataManager.restoreDatabase(restoreDialog.selectedFile);
            restoreConfirmDialog.close();
        }
    }
    readonly property bool wide: width >= Theme.contentMaxWidth * 0.62

    // Switches bound to a setting lose their binding when the user toggles them. Re-binding
    // afterwards makes them show the real value again, e.g. when a change was refused.
    component SettingSwitch: SSwitch {
        id: sw
        required property string setting
        // The label is the title of the SettingRow around the switch: screen readers get it here.
        readonly property Item row: {
            let item = sw.parent;
            while (item && (item.first === undefined || item.title === undefined)) {
                item = item.parent;
            }
            return item;
        }
        readonly property string accessibleName: row ? row.title : ""
        Accessible.name: accessibleName
        Accessible.description: row ? row.description : ""
        checked: AppSettings[setting]
        onToggled: {
            AppSettings[setting] = checked;
            checked = Qt.binding(() => AppSettings[sw.setting]);
        }
    }

    // The tray icon in one badge style, resting mid-session. play() runs a work session and a
    // break through it once; while `looping` it runs them over and over.
    component BadgePreview: TrayBadge {
        id: preview
        property bool looping: false
        readonly property real restProgress: 0.6
        implicitWidth: Theme.iconMd
        progress: restProgress

        function play(): void {
            showcase.restart();
        }
        function rest(): void {
            showcase.stop();
            preview.isBreak = false;
            preview.progress = preview.restProgress;
            preview.scale = 1;
        }
        onLoopingChanged: looping ? play() : rest()

        SequentialAnimation {
            id: showcase
            loops: preview.looping ? Animation.Infinite : 1
            PropertyAction { target: preview; property: "isBreak"; value: false }
            ParallelAnimation {
                NumberAnimation { target: preview; property: "progress"; from: 0; to: 1; duration: Theme.durationLong * 5; easing.type: Easing.InOutQuad }
                NumberAnimation { target: preview; property: "scale"; from: 0.6; to: 1; duration: Theme.durationLong; easing.type: Easing.OutBack }
            }
            PauseAnimation { duration: Theme.durationLong }
            PropertyAction { target: preview; property: "isBreak"; value: true }
            NumberAnimation { target: preview; property: "progress"; from: 0; to: 1; duration: Theme.durationLong * 3; easing.type: Easing.InOutQuad }
            PauseAnimation { duration: Theme.durationLong }
            PropertyAction { target: preview; property: "isBreak"; value: false }
            NumberAnimation { target: preview; property: "progress"; from: 0; to: preview.restProgress; duration: Theme.durationLong * 2; easing.type: Easing.OutCubic }
        }
    }

    component SmallText: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontCaption + 1
        color: Theme.muted
    }

    /// A titled group of setting rows, shown when its category is chosen.
    component SettingsCard: ColumnLayout {
        id: card
        property string title
        property string iconName
        required property string category
        /// False hides the card in every category (a setting this copy of Sugo does not have).
        property bool available: true
        default property alias rows: rowsColumn.data

        visible: card.available && page.category === card.category
        Layout.fillWidth: true
        spacing: Theme.spaceMd

        Text {
            text: card.title
            color: Theme.foreground
            font.pixelSize: Theme.fontHeading - 3
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: rowsColumn.implicitHeight + Theme.spaceXs * 2
            radius: Theme.radiusLg
            color: Theme.card
            border.width: Theme.hairline
            border.color: Theme.border

            ColumnLayout {
                id: rowsColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spaceXl
                anchors.topMargin: Theme.spaceXs
                spacing: 0
            }
        }
    }

    /// One setting: title and description on the left, controls on the right (stacked when narrow).
    component SettingRow: ColumnLayout {
        id: row
        property string title
        property string description
        property bool first: false
        default property alias controls: controlBox.data

        Layout.fillWidth: true
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.hairline
            color: Theme.border
            visible: !row.first
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceLg
            Layout.bottomMargin: Theme.spaceLg
            columns: page.wide ? 2 : 1
            columnSpacing: Theme.spaceXl
            rowSpacing: Theme.spaceMd

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.hairline * 2
                opacity: row.enabled ? 1 : 0.5
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: row.title
                    color: Theme.foreground
                    font.pixelSize: Theme.fontBody + 1
                    font.weight: Font.Medium
                    wrapMode: Text.WordWrap
                }
                SmallText {
                    visible: row.description.length > 0
                    text: row.description
                }
            }

            RowLayout {
                id: controlBox
                Layout.alignment: page.wide ? (Qt.AlignRight | Qt.AlignVCenter) : Qt.AlignLeft
                spacing: Theme.spaceSm
            }
        }
    }

    Flickable {
        id: flickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + Theme.space2xl * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

        ColumnLayout {
            id: content
            x: (flickable.width - width) / 2
            y: Theme.space2xl
            width: Math.min(flickable.width - Theme.space2xl * 2, Theme.contentMaxWidth)
            spacing: Theme.space2xl

            // The categories: only the groups of the chosen one are shown.
            Segmented {
                model: page.categories
                current: page.category
                onActivated: id => page.category = id
            }

            SettingsCard {
                category: "general"
                title: i18n("Appearance")
                iconName: "preferences-desktop-theme"
                SettingRow {
                    first: true
                    title: i18n("Color scheme")
                    SComboBox {
                        id: colorSchemeBox
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: i18n("System default"), value: "system" },
                            { text: i18n("Light"), value: "light" },
                            { text: i18n("Dark"), value: "dark" }
                        ]
                        currentIndex: page.valueIndex(model, AppSettings.colorScheme || "system")
                        Accessible.name: i18n("Color scheme")
                        onActivated: AppSettings.colorScheme = currentValue
                    }
                }
            }

            SettingsCard {
                category: "general"
                title: i18n("Startup")
                iconName: "system-run"
                SettingRow {
                    first: true
                    title: i18n("Start Sugo automatically when I log in")
                    description: AppSettings.autostartSupported ? "" : i18n("Starting at login cannot be set up from inside this environment (for example a sandboxed package). Use your desktop's autostart settings instead.")
                    enabled: AppSettings.autostartSupported
                    SettingSwitch { setting: "startAtLogin" }
                }
                SettingRow {
                    title: i18n("Start minimized to the system tray")
                    description: i18n("When started at login, Sugo opens in the tray without showing its window.")
                    enabled: AppSettings.startAtLogin && AppSettings.showTrayIcon && AppSettings.trayAvailable
                    SettingSwitch { setting: "startMinimized" }
                }
            }

            // Only the AppImage replaces itself; other packages are updated by whoever installed them.
            SettingsCard {
                category: "general"
                available: UpdateChecker.canInstall
                title: i18n("Updates")
                iconName: "system-software-update"
                SettingRow {
                    first: true
                    title: i18n("Update channel")
                    description: AppSettings.updateChannel === "nightly"
                                 ? i18n("Every new build, as soon as it is made. May have bugs.")
                                 : UpdateChecker.currentVersion.indexOf("-nightly") >= 0
                                   ? i18n("Tested releases. This nightly build stays until a newer release comes out.")
                                   : i18n("Tested releases.")
                    Segmented {
                        model: [
                            { id: "stable", text: i18n("Stable") },
                            { id: "nightly", text: i18n("Nightly") }
                        ]
                        current: AppSettings.updateChannel
                        Accessible.name: i18n("Update channel")
                        onActivated: id => AppSettings.updateChannel = id
                    }
                }
                SettingRow {
                    title: i18n("Check for updates automatically")
                    description: i18n("Sugo looks for a new version every few hours and tells you; it downloads it only when you click Install. Turned off, it only looks when you ask, in About.")
                    SettingSwitch { setting: "autoCheckUpdates" }
                }
            }

            SettingsCard {
                category: "general"
                title: i18n("System tray")
                iconName: "notifications"
                Banner {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spaceMd
                    Layout.bottomMargin: Theme.spaceXs
                    visible: !AppSettings.trayAvailable
                    tone: "info"
                    text: i18n("No system tray was found on this desktop, so the tray options are not available and closing the window quits Sugo.")
                }
                SettingRow {
                    first: AppSettings.trayAvailable
                    title: i18n("Show an icon in the system tray")
                    description: AppSettings.trayAvailable ? i18n("While a timer runs, the tray icon shows its progress. Click it to show or hide the window; use its menu to start, pause, stop or quit.") : ""
                    enabled: AppSettings.trayAvailable
                    SettingSwitch { setting: "showTrayIcon" }
                }
                SettingRow {
                    title: i18n("Keep running in the tray when the window is closed")
                    enabled: AppSettings.showTrayIcon && AppSettings.trayAvailable
                    SettingSwitch { setting: "closeToTray" }
                }
                SettingRow {
                    title: i18n("Badge style")
                    enabled: AppSettings.showTrayIcon && AppSettings.trayAvailable
                    SComboBox {
                        id: trayBadgeBox
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: i18n("Border with minutes"), value: 0 },
                            { text: i18n("Fill with minutes"), value: 1 },
                            { text: i18n("Spiral (no numbers)"), value: 2 }
                        ]
                        currentIndex: AppSettings.trayBadgeStyle
                        Accessible.name: i18n("Badge style")
                        Accessible.description: displayText
                        onActivated: {
                            AppSettings.trayBadgeStyle = currentValue;
                            chosenBadge.play();
                        }

                        // The box shows the badge itself rather than its name, which is in the tooltip
                        // and the list; the list's badges play while the pointer is on them.
                        implicitWidth: leftPadding + Theme.iconMd + rightPadding
                        QQC2.ToolTip.visible: hovered && !popup.visible
                        QQC2.ToolTip.text: displayText
                        QQC2.ToolTip.delay: Theme.toolTipDelay
                        popup.x: width - popup.width
                        popup.width: Theme.space4xl * 5
                        contentItem: Item {
                            BadgePreview {
                                id: chosenBadge
                                anchors.verticalCenter: parent.verticalCenter
                                badgeStyle: trayBadgeBox.currentIndex
                                opacity: trayBadgeBox.enabled ? 1 : 0.5
                            }
                        }
                        delegate: QQC2.ItemDelegate {
                            id: badgeEntry
                            required property var model
                            required property int index
                            readonly property string label: (model.modelData ?? model).text ?? ""
                            width: ListView.view.width
                            implicitHeight: Theme.controlHeightLg
                            rightPadding: Theme.spaceMd * 2 + Theme.iconSm
                            highlighted: trayBadgeBox.highlightedIndex === index
                            hoverEnabled: true
                            Accessible.name: label
                            contentItem: RowLayout {
                                spacing: Theme.spaceMd
                                BadgePreview {
                                    badgeStyle: badgeEntry.index
                                    looping: badgeEntry.highlighted
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: badgeEntry.label
                                    color: Theme.foreground
                                    font: trayBadgeBox.font
                                    elide: Text.ElideRight
                                }
                            }
                            background: Rectangle {
                                radius: Theme.radiusSm
                                color: badgeEntry.highlighted || badgeEntry.hovered ? Theme.hover : "transparent"
                                LineIcon {
                                    visible: trayBadgeBox.currentIndex === badgeEntry.index
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.spaceMd
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "check"
                                    size: Theme.iconSm
                                    color: Theme.foreground
                                }
                            }
                        }
                    }
                }
            }

            SettingsCard {
                category: "sounds"
                title: i18n("Sounds")
                iconName: "audio-volume-high"
                SettingRow {
                    first: true
                    title: i18n("Phase sound")
                    SComboBox {
                        id: soundBox
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: i18n("Alarm Clock (Default)"), value: "alarm-clock-elapsed" },
                            { text: i18n("Completion Chime"), value: "complete" },
                            { text: i18n("Instant Message"), value: "message-new-instant" },
                            { text: i18n("Information Sound"), value: "dialog-information" },
                            { text: i18n("Bell"), value: "bell" },
                            { text: i18n("Success"), value: "completion-success" },
                            { text: i18n("Login Chime"), value: "service-login" },
                            { text: i18n("Attention"), value: "window-attention" },
                            { text: i18n("Ringing Phone"), value: "phone-incoming-call" }
                        ]
                        currentIndex: page.valueIndex(model, AppSettings.soundTheme)
                        Accessible.name: i18n("Phase sound")
                        onActivated: AppSettings.soundTheme = currentValue
                    }
                    SButton {
                        text: i18n("Test")
                        iconName: "play"
                        onClicked: AppSettings.playSoundPreview(soundBox.currentValue, AppSettings.soundVolume)
                    }
                }
                SettingRow {
                    title: i18n("Alert volume")
                    SSlider {
                        id: soundVolumeSlider
                        objectName: "soundVolumeSlider"
                        Layout.preferredWidth: Theme.space4xl * 3.5
                        from: 0
                        to: 100
                        value: AppSettings.soundVolume
                        Accessible.name: i18n("Alert volume")
                        onMoved: AppSettings.soundVolume = Math.round(value)
                    }
                    Text {
                        Layout.minimumWidth: Theme.space3xl + Theme.spaceXs
                        horizontalAlignment: Text.AlignRight
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                        font.features: { "tnum": 1 }
                        text: i18nc("volume percentage", "%1%", Math.round(soundVolumeSlider.value))
                    }
                }
                SettingRow {
                    title: i18n("Play a warning one minute before a phase ends")
                    SettingSwitch { setting: "preAlarmEnabled" }
                }
                SettingRow {
                    title: i18n("Pre-alarm sound")
                    enabled: AppSettings.preAlarmEnabled
                    SComboBox {
                        id: preAlarmBox
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: i18n("Information Sound"), value: "dialog-information" },
                            { text: i18n("Bell"), value: "bell" },
                            { text: i18n("Completion Chime"), value: "complete" },
                            { text: i18n("Alarm Clock"), value: "alarm-clock-elapsed" },
                            { text: i18n("Message"), value: "message" },
                            { text: i18n("Attention"), value: "window-attention" },
                            { text: i18n("Success"), value: "completion-success" }
                        ]
                        currentIndex: page.valueIndex(model, AppSettings.preAlarmSound)
                        Accessible.name: i18n("Pre-alarm sound")
                        onActivated: AppSettings.preAlarmSound = currentValue
                    }
                    SButton {
                        text: i18n("Test")
                        enabled: AppSettings.preAlarmEnabled
                        iconName: "play"
                        onClicked: AppSettings.playSoundPreview(preAlarmBox.currentValue, AppSettings.soundVolume)
                    }
                }
                SettingRow {
                    title: i18n("Focus sound")
                    description: i18n("A background sound that plays during work sessions.")
                    SComboBox {
                        id: focusModeBox
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: i18n("Off"), value: 0 },
                            { text: i18n("Clock tick"), value: 1 },
                            { text: i18n("Rain"), value: 2 },
                            { text: i18n("White noise"), value: 3 },
                            { text: i18n("Brown noise"), value: 4 },
                            { text: i18n("Pink noise"), value: 5 },
                            { text: i18n("Waves"), value: 6 }
                        ]
                        currentIndex: AppSettings.focusSoundMode
                        Accessible.name: i18n("Focus sound")
                        onActivated: AppSettings.focusSoundMode = currentValue
                    }
                }
                SettingRow {
                    title: i18n("Focus sound volume")
                    enabled: AppSettings.focusSoundMode > 0
                    SSlider {
                        id: focusVolumeSlider
                        Layout.preferredWidth: Theme.space4xl * 3
                        from: 0
                        to: 100
                        value: AppSettings.focusSoundVolume
                        Accessible.name: i18n("Focus sound volume")
                        onMoved: AppSettings.focusSoundVolume = Math.round(value)
                    }
                    Text {
                        Layout.minimumWidth: Theme.space3xl + Theme.spaceXs
                        horizontalAlignment: Text.AlignRight
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                        font.features: { "tnum": 1 }
                        text: i18nc("volume percentage", "%1%", Math.round(focusVolumeSlider.value))
                    }
                    SButton {
                        text: i18n("Preview")
                        iconName: "play"
                        onClicked: FocusSoundController.preview(focusModeBox.currentValue, Math.round(focusVolumeSlider.value))
                    }
                }
            }

            SettingsCard {
                category: "focus"
                title: i18n("Screen")
                iconName: "brightness-high"
                SettingRow {
                    first: true
                    title: i18n("Keep the screen awake during work sessions")
                    SettingSwitch { setting: "keepScreenAwake" }
                }
                SettingRow {
                    title: i18n("Pause timer when screen is locked")
                    description: i18n("Automatically pauses the active work phase when you lock your screen and prompts to resume when you unlock.")
                    SettingSwitch { setting: "autoPauseOnScreenLock" }
                }
            }

            SettingsCard {
                category: "focus"
                title: i18n("Streak and notes")
                iconName: "games-highscores"
                SettingRow {
                    first: true
                    title: i18n("Protect daily streak over weekends")
                    description: i18n("When enabled, weekends without recorded sessions do not reset your daily streak on Monday.")
                    SettingSwitch { setting: "protectWeekendStreak" }
                }
                SettingRow {
                    title: i18n("Prompt for a task note when a work session finishes")
                    description: i18n("When a work session ends, Sugo asks what you worked on. Notes are saved to the session history.")
                    SettingSwitch { setting: "promptTaskNote" }
                }
            }

            SettingsCard {
                id: calendarCard
                category: "calendar"
                title: i18n("Calendar")
                iconName: "view-calendar"

                SettingRow {
                    first: true
                    title: i18n("Add an event to your calendar each time you finish a work session.")
                    description: i18n("Sugo then sends the name of your timer, its category and the time of each finished work session to the calendar server you choose.")
                    SSwitch {
                        id: calendarSwitch
                        checked: page.calOn
                        Accessible.name: i18n("Add an event to your calendar each time you finish a work session.")
                        onToggled: {
                            if (CalendarSync) {
                                CalendarSync.enabled = checked;
                            }
                            checked = Qt.binding(() => page.calOn);
                        }
                    }
                }

                ColumnLayout {
                    id: calendarBody
                    Layout.fillWidth: true
                    spacing: 0
                    enabled: page.calOn
                    opacity: page.calOn ? 1 : 0.6

                    SettingRow {
                        title: i18n("Calendar service")
                        SComboBox {
                            id: calProviderBox
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { text: i18n("Nextcloud"), value: "nextcloud" },
                                { text: i18n("Custom CalDAV"), value: "caldav" }
                            ]
                            currentIndex: page.valueIndex(model, page.calProvider)
                            Accessible.name: i18n("Calendar service")
                            onActivated: {
                                if (CalendarSync) {
                                    CalendarSync.provider = currentValue;
                                    // Each service keeps its own settings: if the one just chosen is
                                    // already signed in, load its calendars.
                                    if (CalendarSync.hasPassword) {
                                        CalendarSync.refreshCalendars();
                                    }
                                }
                                currentIndex = Qt.binding(() => page.valueIndex(model, page.calProvider));
                            }
                        }
                    }

                    // Nextcloud: server address and browser login.
                    SettingRow {
                        visible: !page.calCustom
                        title: i18n("Server address")
                        enabled: !page.calConnected && !page.calWaiting
                        STextField {
                            Layout.fillWidth: !page.wide
                            Layout.preferredWidth: Theme.space4xl * 5
                            placeholderText: "https://cloud.example.org"
                            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                            text: CalendarSync ? CalendarSync.serverUrl : ""
                            Accessible.name: i18n("Server address")
                            onEditingFinished: {
                                if (CalendarSync) {
                                    CalendarSync.serverUrl = text;
                                }
                                text = Qt.binding(() => CalendarSync ? CalendarSync.serverUrl : "");
                            }
                        }
                    }
                    SettingRow {
                        visible: !page.calCustom
                        title: page.calConnected
                            ? i18n("Connected as %1", CalendarSync ? CalendarSync.username : "")
                            : (page.calWaiting && CalendarSync ? CalendarSync.statusText : "")
                        QQC2.BusyIndicator {
                            Layout.preferredWidth: Theme.iconLg
                            Layout.preferredHeight: Theme.iconLg
                            visible: page.calWaiting
                            running: visible && page.visible
                        }
                        SButton {
                            visible: !page.calConnected && !page.calWaiting
                            text: i18n("Log in")
                            iconName: "external"
                            onClicked: CalendarSync.startNextcloudLogin()
                        }
                        SButton {
                            visible: page.calWaiting
                            text: i18n("Cancel")
                            iconName: "close"
                            onClicked: CalendarSync.cancelLogin()
                        }
                        SButton {
                            visible: page.calConnected && !page.calWaiting
                            text: i18n("Test")
                            iconName: page.calTestOk ? "check" : "refresh"
                            // Not while any request runs: the test would not run, yet look passed.
                            enabled: !page.calWorking
                            Accessible.name: i18n("Test connection")
                            QQC2.ToolTip.text: i18n("Test connection")
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Theme.toolTipDelay
                            onClicked: {
                                page.calAction = "test";
                                CalendarSync.testConnection();
                            }
                        }
                        SButton {
                            visible: page.calConnected && !page.calWaiting
                            text: i18n("Disconnect")
                            iconName: "close"
                            onClicked: page.requestDisconnect()
                        }
                    }

                    // Custom CalDAV: address, username, password.
                    SettingRow {
                        visible: page.calCustom
                        title: i18n("Calendar address (CalDAV)")
                        description: i18n("Use the CalDAV address of the calendar. A subscription link ending in .ics is read-only and cannot receive events.")
                        STextField {
                            Layout.fillWidth: !page.wide
                            Layout.preferredWidth: Theme.space4xl * 5
                            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                            text: CalendarSync ? CalendarSync.calendarUrl : ""
                            Accessible.name: i18n("Calendar address (CalDAV)")
                            onEditingFinished: {
                                if (CalendarSync) {
                                    CalendarSync.calendarUrl = text;
                                }
                                text = Qt.binding(() => CalendarSync ? CalendarSync.calendarUrl : "");
                            }
                        }
                    }
                    SettingRow {
                        visible: page.calCustom
                        title: i18n("Username")
                        STextField {
                            Layout.fillWidth: !page.wide
                            Layout.preferredWidth: Theme.space4xl * 5
                            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                            text: CalendarSync ? CalendarSync.username : ""
                            Accessible.name: i18n("Username")
                            onEditingFinished: {
                                if (CalendarSync) {
                                    CalendarSync.username = text;
                                }
                                text = Qt.binding(() => CalendarSync ? CalendarSync.username : "");
                            }
                        }
                    }
                    SettingRow {
                        visible: page.calCustom
                        title: i18n("Password")
                        STextField {
                            id: calPasswordField
                            property bool dirty: false
                            Layout.fillWidth: !page.wide
                            Layout.preferredWidth: Theme.space4xl * 5
                            echoMode: TextInput.Password
                            // The saved password is never read back; dots only show that one is stored.
                            placeholderText: page.calHasPassword ? "••••••••" : ""
                            Accessible.name: i18n("Password")
                            onTextEdited: dirty = true
                            onEditingFinished: {
                                if (dirty && CalendarSync) {
                                    CalendarSync.setPassword(text);
                                }
                                dirty = false;
                                text = "";
                            }
                        }
                    }
                    // Same row as for Nextcloud: test the connection, or forget this service's login.
                    SettingRow {
                        visible: page.calCustom && page.calConfigured
                        title: i18n("Connection")
                        SButton {
                            text: i18n("Test")
                            iconName: page.calTestOk ? "check" : "refresh"
                            // Not while any request runs: the test would not run, yet look passed.
                            enabled: !page.calWorking
                            Accessible.name: i18n("Test connection")
                            QQC2.ToolTip.text: i18n("Test connection")
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Theme.toolTipDelay
                            onClicked: {
                                page.calAction = "test";
                                CalendarSync.testConnection();
                            }
                        }
                        SButton {
                            text: i18n("Disconnect")
                            iconName: "close"
                            onClicked: page.requestDisconnect()
                        }
                    }
                    // Calendar choice, with a refresh button to reload the list from the server.
                    SettingRow {
                        visible: page.calChoices.length > 0 || page.calConfigured
                        title: i18n("Calendar")
                        SComboBox {
                            id: calChoiceBox
                            Layout.fillWidth: !page.wide
                            Layout.preferredWidth: Theme.space4xl * 5
                            textRole: "name"
                            valueRole: "url"
                            model: page.calChoices
                            currentIndex: page.calChoiceIndex()
                            Accessible.name: i18n("Calendar")
                            onActivated: {
                                const item = page.calChoices[currentIndex];
                                if (CalendarSync && item) {
                                    CalendarSync.selectCalendar(item.url, item.name);
                                }
                                currentIndex = Qt.binding(() => page.calChoiceIndex());
                            }
                            enabled: page.calChoices.length > 0
                            delegate: QQC2.ItemDelegate {
                                id: calEntry
                                required property var modelData
                                required property int index
                                width: ListView.view ? ListView.view.width : calChoiceBox.width
                                implicitHeight: Theme.controlHeight
                                highlighted: calChoiceBox.highlightedIndex === index
                                hoverEnabled: true
                                background: Rectangle {
                                    radius: Theme.radiusSm
                                    color: calEntry.highlighted || calEntry.hovered ? Theme.hover : "transparent"
                                }
                                contentItem: RowLayout {
                                    spacing: Theme.spaceSm
                                    Rectangle {
                                        visible: !!calEntry.modelData.color
                                        implicitWidth: Theme.spaceMd + Theme.hairline
                                        implicitHeight: implicitWidth
                                        radius: width / 2
                                        color: calEntry.modelData.color || Theme.background
                                        border.width: Theme.hairline
                                        border.color: Theme.border
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: calEntry.modelData.name
                                        elide: Text.ElideRight
                                        color: Theme.foreground
                                        font.pixelSize: Theme.fontBody
                                    }
                                }
                            }
                        }
                        SButton {
                            kind: "ghost"
                            iconOnly: true
                            iconName: "refresh"
                            enabled: !page.calRefreshing
                            text: i18n("Find calendars")
                            QQC2.ToolTip.text: i18n("Find calendars")
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Theme.toolTipDelay
                            onClicked: {
                                page.calAction = "refresh";
                                CalendarSync.refreshCalendars();
                            }
                        }
                    }

                    SettingRow {
                        title: i18n("Include the task note in the event description")
                        description: i18n("Notes are sent to your calendar server.")
                        SSwitch {
                            checked: CalendarSync ? CalendarSync.includeNote : false
                            Accessible.name: i18n("Include the task note in the event description")
                            onToggled: {
                                if (CalendarSync) {
                                    CalendarSync.includeNote = checked;
                                }
                                checked = Qt.binding(() => CalendarSync ? CalendarSync.includeNote : false);
                            }
                        }
                    }
                }

                // Status area, kept readable even while the sync is switched off. Nothing is shown while
                // the server is being asked (the buttons are disabled meanwhile): only a result is.
                Banner {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spaceSm
                    Layout.bottomMargin: Theme.spaceMd
                    visible: page.calFailed || (page.calNotConfigured && !page.calWaiting)
                    tone: page.calFailed ? "danger" : "info"
                    text: {
                        if (!CalendarSync) {
                            return "";
                        }
                        if (page.calNotConfigured) {
                            // Signed in but no calendar chosen: the status says so.
                            if (page.calConfigured) {
                                return CalendarSync.statusText;
                            }
                            return page.calCustom
                                ? i18n("Enter the calendar address, your username and password to start sending events.")
                                : (CalendarSync.serverUrl.length > 0
                                   ? i18n("Log in to start sending events.")
                                   : i18n("Enter your server address and log in to start sending events."));
                        }
                        if (page.calFailed && CalendarSync.lastError.length > 0) {
                            return CalendarSync.lastError;
                        }
                        return CalendarSync.statusText;
                    }
                }
                SmallText {
                    Layout.bottomMargin: Theme.spaceSm
                    visible: page.calLastSuccess.length > 0
                    text: i18n("Last event sent: %1", page.calLastSuccess)
                }
                SmallText {
                    Layout.bottomMargin: Theme.spaceMd
                    visible: page.calPending > 0
                    text: i18np("%1 event waiting to be sent", "%1 events waiting to be sent", page.calPending)
                }
            }

            SettingsCard {
                category: "advanced"
                title: i18n("Data management")
                iconName: "server-database"
                SettingRow {
                    first: true
                    title: i18n("Database: %1", DataManager ? DataManager.databasePath : "")
                    description: TimerEngine.active ? i18n("Stop the active timer before restoring a backup.") : ""
                    SButton {
                        text: i18n("Back up…")
                        iconName: "download"
                        onClicked: backupDialog.open()
                    }
                    SButton {
                        text: i18n("Restore…")
                        iconName: "upload"
                        enabled: !TimerEngine.active
                        onClicked: restoreDialog.open()
                    }
                }
                SettingRow {
                    title: i18n("Session history")
                    SButton {
                        id: clearHistoryButton
                        destructive: true
                        text: i18n("Clear history…")
                        iconName: "trash"
                        enabled: DataManager !== null && DataManager.sessionCount > 0
                        onClicked: clearHistoryDialog.open()
                    }
                }
            }

            SettingsCard {
                category: "advanced"
                title: i18n("Global Shortcuts and Automation")
                iconName: "configure-shortcuts"
                SettingRow {
                    first: true
                    title: i18n("Allow other programs to control the timer")
                    description: i18n("Global shortcuts, scripts (D-Bus) and KRunner can start, pause, skip and stop it. Turn this off if you only use Sugo's own window and tray.")
                    SettingSwitch { setting: "allowExternalControl" }
                }
                SettingRow {
                    title: i18n("D-Bus commands")
                    description: i18n("Sugo can be controlled through D-Bus at %1, for example with the command below. Available commands: %2.",
                                      "io.github.mineraleyt.sugo /Timer", "toggle, start, pause, stop, skip, status")
                    Item { implicitWidth: 1 }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.spaceMd
                    Layout.preferredHeight: dbusRow.implicitHeight + Theme.spaceSm * 2
                    color: Theme.background
                    border.width: Theme.hairline
                    border.color: Theme.border
                    radius: Theme.radius
                    RowLayout {
                        id: dbusRow
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spaceMd
                        anchors.rightMargin: Theme.spaceXs
                        spacing: Theme.spaceSm
                        TextEdit {
                            id: dbusLabel
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            text: "qdbus6 io.github.mineraleyt.sugo /Timer toggle"
                            color: Theme.foreground
                            selectionColor: Theme.alpha(Theme.brand, 0.35)
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            Accessible.name: text
                        }
                        SButton {
                            kind: "ghost"
                            iconOnly: true
                            iconName: "copy"
                            text: i18n("Copy command")
                            Accessible.name: text
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Theme.toolTipDelay
                            onClicked: {
                                dbusLabel.selectAll();
                                dbusLabel.copy();
                                dbusLabel.deselect();
                                applicationWindow().showPassiveNotification(i18n("Command copied to the clipboard."));
                            }
                        }
                    }
                }
                // System Settings' shortcut page exists on KDE Plasma only.
                SettingRow {
                    visible: DesktopStyle.style === "kde"
                    title: i18n("Global shortcuts")
                    description: i18n("Assign keys to these commands in Plasma's System Settings.")
                    SButton {
                        text: i18n("Open Shortcut Settings")
                        iconName: "external"
                        onClicked: AppSettings.openShortcutsSettings()
                    }
                }
            }

            SettingsCard {
                category: "advanced"
                title: i18n("Troubleshooting & Diagnostics")
                iconName: "tools-report-bug"
                SettingRow {
                    first: true
                    title: i18n("Diagnostic report")
                    description: i18n("Generate a redacted report with system information and recent logs to share when reporting an issue.")
                    Item { implicitWidth: 1 }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.spaceMd
                    spacing: Theme.spaceSm
                    SButton {
                        text: i18n("Copy to Clipboard")
                        iconName: "copy"
                        onClicked: {
                            Diagnostics.copyToClipboard();
                            page.showDiagnosticsResult("copied");
                        }
                    }
                    SButton {
                        text: i18n("Export Diagnostics…")
                        iconName: "download"
                        onClicked: exportDiagnosticsDialog.open()
                    }
                    SButton {
                        text: i18n("View Report…")
                        iconName: "search"
                        onClicked: {
                            diagnosticsPreviewDialog.reportText = Diagnostics.generateReport();
                            diagnosticsPreviewDialog.open();
                        }
                    }
                }
        Banner {
            id: diagnosticsInlineMessage
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.spaceMd
            visible: page.diagnosticsResult.length > 0 && !page.diagnosticsDismissed
            tone: page.diagnosticsResult === "exportFailed" ? "danger" : "info"
            text: {
                switch (page.diagnosticsResult) {
                case "copied":
                    return i18n("Diagnostic report copied to clipboard.");
                case "exported":
                    return i18n("Diagnostic report successfully exported.");
                case "exportFailed":
                    return page.diagnosticsExportError.length > 0
                        ? page.diagnosticsExportError
                        : i18n("Failed to export diagnostic report.");
                }
                return "";
            }
            SButton {
                kind: "ghost"
                iconOnly: true
                iconName: "close"
                text: i18n("Dismiss")
                onClicked: page.diagnosticsDismissed = true
            }
        }

            }
        }
    }

        FileDialog {
            id: backupDialog
            title: i18n("Back up Sugo database")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "db"
            nameFilters: [i18n("SQLite database (*.db)")]
            onAccepted: DataManager.backupDatabase(selectedFile)
        }

        FileDialog {
            id: restoreDialog
            title: i18n("Choose a Sugo backup")
            fileMode: FileDialog.OpenFile
            nameFilters: [i18n("SQLite database (*.db)")]
            onAccepted: restoreConfirmDialog.open()
        }

        FileDialog {
            id: exportDiagnosticsDialog
            title: i18n("Export Diagnostics")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "txt"
            nameFilters: [i18n("Text files (*.txt)"), i18n("All files (*)")]
            onAccepted: {
                if (Diagnostics.exportReport(selectedFile)) {
                    page.showDiagnosticsResult("exported");
                } else {
                    page.diagnosticsExportError = Diagnostics.lastExportError();
                    page.showDiagnosticsResult("exportFailed");
                }
            }
        }

        ConfirmDialog {
            id: diagnosticsPreviewDialog
            title: i18n("Diagnostics Report")
            text: i18n("All personal details and usernames have been redacted.")
            confirmText: i18n("Copy to Clipboard")
            cancelText: i18n("Close")
            width: Math.min(Theme.contentMaxWidth, (parent ? parent.width : Theme.contentMaxWidth) - Theme.space2xl * 2)
            property string reportText: ""

            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.space4xl * 6
                QQC2.TextArea {
                    readOnly: true
                    text: diagnosticsPreviewDialog.reportText
                    color: Theme.foreground
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                    background: Rectangle {
                        radius: Theme.radius
                        color: Theme.background
                        border.width: Theme.hairline
                        border.color: Theme.border
                    }
                    Accessible.name: diagnosticsPreviewDialog.title
                }
            }

            onConfirmed: {
                Diagnostics.copyToClipboard();
                diagnosticsPreviewDialog.close();
                page.showDiagnosticsResult("copied");
            }
        }

}
