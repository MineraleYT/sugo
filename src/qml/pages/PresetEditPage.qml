// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// One timer: everything about it on one page, changed in place and kept with Save.
QQC2.Page {
    id: page

    /// Empty means "create a new timer".
    property string uuid: ""

    readonly property bool isNew: uuid === ""
    readonly property var preset: isNew
        ? ({ name: "", category: "", iconName: "sugo-timer", workSeconds: 25 * 60, shortBreakSeconds: 5 * 60,
             longBreakSeconds: 15 * 60, cyclesBeforeLong: 4, builtin: false,
             options: ({ autoStartMode: 0, silenceWhileWorking: false, soundOnEnd: true, notifyOnEnd: true }) })
        : PresetModel.get(uuid)
    property string selectedIcon: isNew ? "sugo-timer" : (preset.iconName || "sugo-timer")
    readonly property var iconChoices: PresetModel.availableIcons()
    readonly property bool valid: nameField.text.trim().length > 0
    readonly property bool isCurrent: !isNew && PresetModel.currentUuid === uuid
    /// Deleting the running timer would switch timers, which stops the session.
    readonly property bool lockedByTimer: isCurrent && TimerEngine.active

    // `preset` is read once, for the controls' starting values; what shows the stored name follows
    // every change of the timer (a save, a rename elsewhere) through this count.
    property int storedRevision: 0
    Connections {
        target: PresetModel
        function onPresetChanged(changedUuid) {
            if (changedUuid === page.uuid) {
                ++page.storedRevision;
            }
        }
    }
    /// The name as stored, not as being typed.
    readonly property string storedName: {
        void storedRevision;
        return isNew ? "" : (PresetModel.get(uuid).name || "");
    }

    /// The top bar shows "Timers / <name>".
    readonly property string section: i18n("Timers")
    title: isNew ? i18n("New Timer") : storedName
    background: null

    /// What Save would store, as the controls show it now.
    function currentFields(): var {
        return {
            name: nameField.text,
            category: categoryField.text,
            iconName: page.selectedIcon,
            workSeconds: workBox.value * 60,
            shortBreakSeconds: shortBox.value * 60,
            longBreakSeconds: longBox.value * 60,
            cyclesBeforeLong: cyclesBox.value,
            autoStartMode: autoStartBox.currentIndex,
            silenceWhileWorking: silenceBox.checked,
            soundOnEnd: soundBox.checked,
            notifyOnEnd: notifyBox.checked
        };
    }

    // The fields as the page opened, to tell whether anything was changed since.
    property string initialFields: ""
    // Taken once every control has its starting value.
    Component.onCompleted: Qt.callLater(() => initialFields = JSON.stringify(currentFields()))
    readonly property bool modified: initialFields.length > 0 && JSON.stringify(currentFields()) !== initialFields
    /// A confirmation is showing (also read by the smoke test).
    readonly property bool confirming: discardDialog.visible || resetDialog.visible

    /// Save does something: a new timer, or changes to store.
    readonly property bool canSave: valid && (isNew || modified)

    /// Takes the numbers still being typed: clicking a button or a shortcut leaves the focus in the field.
    function commitFields(): void {
        workBox.commit();
        shortBox.commit();
        longBox.commit();
        cyclesBox.commit();
    }

    function save(): void {
        commitFields();
        if (!canSave) {
            return;
        }
        const fields = currentFields();
        if (isNew) {
            const created = PresetModel.create(fields);
            leaving = true;
            applicationWindow().openTimer(created);
        } else {
            PresetModel.update(uuid, fields);
            initialFields = JSON.stringify(currentFields());
        }
    }

    /// Back to the timer page (a new timer that is not created after all).
    function close(): void {
        applicationWindow().navigate("timer");
    }

    // Set once the page is being left for good, so leaving does not ask again.
    property bool leaving: false

    /// Leaves the page for `then` (e.g. going to another page); without it, drops the changes.
    function leave(then: var): void {
        leaving = true;
        if (then) {
            then();
        } else if (isNew) {
            close();
        } else {
            applicationWindow().openTimer(uuid, true);
        }
    }

    /// Escape and the sidebar: unsaved changes are only thrown away once confirmed.
    function requestClose(then: var): void {
        commitFields();
        discardDialog.then = then ?? null;
        if (modified) {
            discardDialog.open();
        } else {
            leave(discardDialog.then);
        }
    }

    function requestReset(): void {
        resetDialog.open();
    }

    function start(): void {
        commitFields();
        // Changes are saved first (Start is disabled meanwhile).
        if (TimerEngine.active || modified) {
            return;
        }
        PresetModel.currentUuid = uuid;
        TimerEngine.start();
        applicationWindow().navigate("timer");
    }

    ConfirmDialog {
        id: discardDialog
        property var then: null
        title: i18n("Discard Changes?")
        text: i18n("The changes you made to this timer have not been saved.")
        confirmText: i18n("Discard")
        confirmKind: "danger"
        cancelText: i18n("Keep Editing")
        onConfirmed: {
            discardDialog.close();
            page.leave(discardDialog.then);
        }
    }

    ConfirmDialog {
        id: resetDialog
        title: i18n("Reset to Defaults?")
        text: i18n("“%1” goes back to its original durations and options. Your changes to it are lost.", page.storedName)
        confirmText: i18n("Reset")
        onConfirmed: {
            resetDialog.close();
            PresetModel.reset(page.uuid);
            page.leaving = true;
            applicationWindow().openTimer(page.uuid, true);
        }
    }

    ConfirmDialog {
        id: deleteDialog
        title: i18n("Delete Timer")
        text: i18n("Delete “%1”? Statistics already recorded with it are kept.", page.storedName)
        confirmText: i18n("Delete")
        confirmKind: "danger"
        onConfirmed: {
            deleteDialog.close();
            // The timer may have been started meanwhile (tray, shortcut, D-Bus).
            if (page.lockedByTimer) {
                applicationWindow().showPassiveNotification(
                    i18n("“%1” was started meanwhile, so it was not deleted. Stop it to delete it.", page.storedName));
                return;
            }
            page.leaving = true;
            PresetModel.remove(page.uuid);
            applicationWindow().navigate("timer");
        }
    }

    Shortcut {
        sequences: [StandardKey.Save]
        enabled: page.visible && !page.confirming
        onActivated: page.save()
    }

    Shortcut {
        sequences: [StandardKey.Cancel]
        enabled: page.visible && !autoStartBox.popup.visible && !page.confirming && (page.modified || page.isNew)
        onActivated: page.requestClose()
    }

    // Shown while there is something to keep: a new timer, or changes.
    footer: Rectangle {
        visible: page.isNew || page.modified
        implicitHeight: visible ? Theme.controlHeightLg + Theme.spaceLg * 2 : 0
        color: Theme.background

        Rectangle {
            width: parent.width
            height: Theme.hairline
            color: Theme.border
        }

        RowLayout {
            anchors.verticalCenter: parent.verticalCenter
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - Theme.space2xl * 2, Theme.contentMaxWidth)
            spacing: Theme.spaceMd

            Text {
                text: page.isNew ? i18n("A new timer") : i18n("Unsaved changes")
                color: Theme.muted
                font.pixelSize: Theme.fontBody
                Layout.fillWidth: true
            }
            SButton {
                kind: "ghost"
                text: page.isNew ? i18n("Cancel") : i18n("Discard")
                onClicked: page.requestClose()
            }
            SButton {
                kind: "primary"
                text: page.isNew ? i18n("Create Timer") : i18n("Save")
                shortcutText: DesktopStyle.shortcutText("Ctrl+S")
                enabled: page.canSave
                onClicked: page.save()
            }
        }
    }

    /// The name and category, edited where they are shown: plain text at rest, a light box hugging
    /// the text when hovered or focused. The padding never changes, so the text does not move.
    component InlineField: QQC2.TextField {
        id: inline
        // As wide as the text (or the placeholder), plus room for the caret, never wider than the column.
        implicitWidth: Math.ceil(widthMetrics.advanceWidth) + Theme.hairline * 2 + leftPadding + rightPadding
        Layout.fillWidth: true
        Layout.maximumWidth: implicitWidth
        leftPadding: Theme.spaceSm
        rightPadding: Theme.spaceSm
        topPadding: Theme.spaceXs
        bottomPadding: Theme.spaceXs
        color: Theme.foreground
        placeholderTextColor: Theme.subtle
        selectionColor: Theme.alpha(Theme.brand, 0.35)
        selectedTextColor: Theme.foreground
        hoverEnabled: true
        TextMetrics {
            id: widthMetrics
            font: inline.font
            text: inline.text.length > 0 ? inline.text : inline.placeholderText
        }
        background: Rectangle {
            radius: Theme.radius
            color: inline.activeFocus || inline.hovered ? Theme.hover : "transparent"
            border.width: inline.activeFocus ? Theme.hairline : 0
            border.color: Theme.borderStrong
        }
    }

    // The icons to choose from, under the tile.
    QQC2.Popup {
        id: iconPopup
        parent: iconTile
        y: iconTile.height + Theme.spaceSm
        width: Theme.space4xl * 6.5
        padding: Theme.spaceMd
        focus: true
        // A press on the tile is left to the tile, which toggles the popup: closing on the press
        // would have the click open it again.
        closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutsideParent
        background: Rectangle {
            radius: Theme.radiusLg
            color: Theme.raised
            border.width: Theme.hairline
            border.color: Theme.borderStrong
        }
        contentItem: Flow {
            spacing: Theme.spaceXs
            Repeater {
                model: page.iconChoices
                delegate: QQC2.AbstractButton {
                    id: iconBtn
                    required property var modelData
                    readonly property bool isSelected: page.selectedIcon === modelData.iconName
                    hoverEnabled: true
                    text: modelData.label
                    Accessible.role: Accessible.RadioButton
                    Accessible.checked: isSelected
                    implicitWidth: Theme.iconXl + Theme.spaceSm
                    implicitHeight: implicitWidth
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: modelData.label
                    QQC2.ToolTip.delay: Theme.toolTipDelay
                    background: Rectangle {
                        radius: Theme.radiusMd
                        color: iconBtn.isSelected ? Theme.selected : (iconBtn.hovered ? Theme.hover : "transparent")
                        border.width: iconBtn.isSelected || iconBtn.visualFocus ? Theme.hairline : 0
                        border.color: iconBtn.visualFocus ? Theme.focusRing : Theme.borderStrong
                    }
                    contentItem: Item {
                        TimerIcon {
                            anchors.centerIn: parent
                            iconName: iconBtn.modelData.iconName
                            size: Theme.iconLg - Theme.spaceXs
                        }
                    }
                    onClicked: {
                        page.selectedIcon = modelData.iconName;
                        iconPopup.close();
                    }
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

            // The timer: its tile (click to change it), its name and category, edited in place.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceLg

                QQC2.AbstractButton {
                    id: iconTile
                    implicitWidth: Theme.iconXl + Theme.spaceLg
                    implicitHeight: implicitWidth
                    hoverEnabled: true
                    focusPolicy: Qt.TabFocus
                    text: i18n("Icon")
                    Accessible.description: i18n("Choose the icon of this timer")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18n("Change the icon")
                    QQC2.ToolTip.delay: Theme.toolTipDelay
                    onClicked: iconPopup.opened ? iconPopup.close() : iconPopup.open()
                    background: Rectangle {
                        radius: Theme.radiusLg
                        color: iconTile.hovered || iconPopup.visible ? Theme.hover : "transparent"
                        border.width: iconTile.hovered || iconPopup.visible || iconTile.visualFocus ? Theme.hairline : 0
                        border.color: iconTile.visualFocus ? Theme.focusRing : Theme.borderStrong
                    }
                    contentItem: Item {
                        TimerIcon {
                            anchors.centerIn: parent
                            iconName: page.selectedIcon
                            size: Theme.iconXl + Theme.spaceXs
                        }
                    }
                }

                // The fields are only as wide as their text; the column still takes the rest of the row.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Number.POSITIVE_INFINITY
                    // The box around a field starts a little past the tile, the text a little further.
                    Layout.leftMargin: -Theme.spaceXs
                    spacing: 0
                    InlineField {
                        id: nameField
                        text: page.preset.name
                        maximumLength: PresetModel.maxNameLength
                        placeholderText: i18n("Name this timer")
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                        Accessible.name: i18n("Name")
                        onAccepted: page.save()
                        Component.onCompleted: if (page.isNew) forceActiveFocus()
                    }
                    InlineField {
                        id: categoryField
                        text: page.preset.category
                        maximumLength: PresetModel.maxNameLength
                        placeholderText: i18n("Add a category, e.g. Work or Study")
                        color: Theme.muted
                        font.pixelSize: Theme.fontBody
                        Accessible.name: i18n("Category")
                        onAccepted: page.save()
                    }
                }

                SButton {
                    visible: !page.isNew
                    kind: page.isCurrent && TimerEngine.active ? "ghost" : "primary"
                    iconName: page.isCurrent && TimerEngine.active ? "dot" : "play"
                    text: page.isCurrent && TimerEngine.active ? i18n("Running") : i18n("Start")
                    enabled: !TimerEngine.active && !page.modified
                    toolTipText: TimerEngine.active && !page.isCurrent ? i18n("Stop the timer to switch to a different timer.") : ""
                    onClicked: page.start()
                }
                SButton {
                    visible: !page.isNew
                    kind: "ghost"
                    iconOnly: true
                    iconName: "copy"
                    text: i18n("Duplicate")
                    onClicked: {
                        const copy = PresetModel.duplicate(page.uuid);
                        if (copy.length > 0) {
                            applicationWindow().openTimer(copy);
                        }
                    }
                }
                // A disabled button gets no hover events, so the wrapper says why it cannot be deleted.
                Item {
                    visible: !page.isNew
                    implicitWidth: deleteButton.implicitWidth
                    implicitHeight: deleteButton.implicitHeight
                    HoverHandler {
                        id: deleteHover
                    }
                    QQC2.ToolTip.visible: !deleteButton.enabled && deleteHover.hovered
                    QQC2.ToolTip.text: deleteButton.text
                    QQC2.ToolTip.delay: Theme.toolTipDelay
                    SButton {
                        id: deleteButton
                        objectName: "deleteButton-" + page.uuid
                        anchors.fill: parent
                        kind: "ghost"
                        iconOnly: true
                        destructive: true
                        iconName: "trash"
                        text: page.preset.builtin ? i18n("The default timer cannot be deleted")
                            : page.lockedByTimer ? i18n("This timer is running. Stop it to delete it.")
                            : i18n("Delete")
                        enabled: !page.preset.builtin && !page.lockedByTimer
                        onClicked: deleteDialog.open()
                    }
                }
            }

            // One cycle as it will run: every phase as wide as it lasts.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: cycleColumn.implicitHeight + Theme.spaceLg * 2
                radius: Theme.radiusLg
                color: Theme.card
                border.width: Theme.hairline
                border.color: Theme.border

                readonly property var phases: {
                    const list = [];
                    const sessions = Math.max(1, cyclesBox.value);
                    for (let i = 0; i < sessions; ++i) {
                        list.push({ work: true, minutes: workBox.value });
                        if (i < sessions - 1 || cyclesBox.value === 0) {
                            if (shortBox.value > 0) {
                                list.push({ work: false, long: false, minutes: shortBox.value });
                            }
                        } else if (longBox.value > 0) {
                            list.push({ work: false, long: true, minutes: longBox.value });
                        }
                    }
                    return list;
                }
                readonly property int totalMinutes: phases.reduce((sum, p) => sum + p.minutes, 0)

                ColumnLayout {
                    id: cycleColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spaceLg
                    spacing: Theme.spaceMd

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: cyclesBox.value > 0 ? i18n("One cycle") : i18n("Work and break")
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Text {
                            readonly property int total: cycleColumn.parent.totalMinutes
                            text: total >= 60 ? i18nc("hours and minutes", "%1h %2m", Math.floor(total / 60), total % 60)
                                              : i18nc("minutes", "%1 min", total)
                            color: Theme.muted
                            font.pixelSize: Theme.fontCaption
                            font.features: { "tnum": 1 }
                        }
                    }

                    Item {
                        id: cycleRow
                        Layout.fillWidth: true
                        implicitHeight: Theme.spaceXl + Theme.spaceSm
                        readonly property var phases: cycleColumn.parent.phases
                        readonly property real gaps: Theme.hairline * 3 * (phases.length - 1)
                        Row {
                            id: cycleBlocks
                            anchors.fill: parent
                            spacing: Theme.hairline * 3
                            Repeater {
                                model: cycleRow.phases
                                Rectangle {
                                    id: block
                                    required property var modelData
                                    width: Math.max(Theme.spaceXs, (cycleRow.width - cycleRow.gaps) * modelData.minutes / Math.max(1, cycleColumn.parent.totalMinutes))
                                    height: cycleRow.height
                                    radius: Theme.radiusSm
                                    color: modelData.work ? Theme.brand : Theme.alpha(Theme.breakColor, modelData.long ? 0.7 : 0.35)
                                    gradient: modelData.work ? workGradient : null
                                    Gradient {
                                        id: workGradient
                                        GradientStop { position: 0; color: Theme.brandPeach }
                                        GradientStop { position: 1; color: Theme.brand }
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: parent.width > implicitWidth + Theme.spaceMd
                                        text: block.modelData.minutes
                                        color: Theme.brandText
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                        // Drag a phase by one of its edges, as when resizing a partition: the half you grab is the
                        // edge that moves, so dragging outwards lengthens it (every phase of its kind follows).
                        // Lives on the row, not the blocks: each change rebuilds the blocks and would drop the grab.
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            property var box: null
                            property int startValue: 0
                            property real startX: 0
                            // +1 when the right edge was grabbed, -1 for the left one.
                            property int direction: 1
                            // Frozen at press, so the scale does not shift under the pointer while dragging.
                            property real pixelsPerMinute: 1
                            onPressed: mouse => {
                                const block = cycleBlocks.childAt(mouse.x, mouse.y);
                                box = block && block.modelData ? (block.modelData.work ? workBox : block.modelData.long ? longBox : shortBox) : null;
                                if (!box) {
                                    mouse.accepted = false;
                                    return;
                                }
                                startValue = box.value;
                                startX = mouse.x;
                                direction = mouse.x < block.x + block.width / 2 ? -1 : 1;
                                pixelsPerMinute = Math.max(1, (cycleRow.width - cycleRow.gaps) / Math.max(1, cycleColumn.parent.totalMinutes));
                            }
                            onPositionChanged: mouse => {
                                if (box) {
                                    box.setValue(startValue + direction * Math.round((mouse.x - startX) / pixelsPerMinute));
                                }
                            }
                            onReleased: box = null
                            onCanceled: box = null
                        }
                    }
                }
            }

            FormSection {
                title: i18n("Durations")

                FormRow {
                    first: true
                    title: i18n("Work")
                    // A few common lengths, one click away.
                    Repeater {
                        model: [15, 25, 45, 50]
                        delegate: QQC2.AbstractButton {
                            id: chip
                            required property int modelData
                            readonly property bool on: workBox.value === modelData
                            implicitHeight: Theme.controlHeight - Theme.spaceXs
                            leftPadding: Theme.spaceMd
                            rightPadding: Theme.spaceMd
                            hoverEnabled: true
                            focusPolicy: Qt.TabFocus
                            text: modelData
                            Accessible.name: i18nc("@action set the work duration", "%1 minutes", modelData)
                            onClicked: workBox.setValue(modelData)
                            background: Rectangle {
                                radius: Theme.radius
                                color: chip.on ? Theme.selected : (chip.hovered ? Theme.hover : "transparent")
                                border.width: Theme.hairline
                                border.color: chip.on ? Theme.borderStrong : Theme.border
                            }
                            contentItem: Text {
                                text: chip.text
                                color: chip.on ? Theme.foreground : Theme.muted
                                font.pixelSize: Theme.fontCaption
                                font.features: { "tnum": 1 }
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                    NumberField {
                        id: workBox
                        Layout.leftMargin: Theme.spaceSm
                        from: 1
                        to: PresetModel.maxWorkMinutes
                        value: Math.max(1, Math.round(page.preset.workSeconds / 60))
                        unit: i18nc("minutes, short", "min")
                        Accessible.name: i18n("Work")
                    }
                }
                FormRow {
                    title: i18n("Short break")
                    NumberField {
                        id: shortBox
                        from: 1
                        to: PresetModel.maxBreakMinutes
                        value: Math.max(1, Math.round(page.preset.shortBreakSeconds / 60))
                        unit: i18nc("minutes, short", "min")
                        Accessible.name: i18n("Short break")
                    }
                }
                FormRow {
                    title: i18n("Long break")
                    NumberField {
                        id: longBox
                        from: 1
                        to: PresetModel.maxBreakMinutes
                        value: Math.max(1, Math.round(page.preset.longBreakSeconds / 60))
                        unit: i18nc("minutes, short", "min")
                        Accessible.name: i18n("Long break")
                    }
                }
                FormRow {
                    title: i18n("Long break after")
                    description: i18n("How many work sessions come before the long break.")
                    NumberField {
                        id: cyclesBox
                        from: 0
                        to: PresetModel.maxCycles
                        value: page.preset.cyclesBeforeLong
                        unit: i18ncp("work sessions, short", "session", "sessions", cyclesBox.value)
                        fromText: i18n("Never")
                        Accessible.name: i18n("Long break after")
                    }
                }
            }

            FormSection {
                title: i18n("Behavior")

                FormRow {
                    first: true
                    title: i18n("Start automatically")
                    description: i18n("Whether breaks, work phases, or neither start by themselves.")
                    SComboBox {
                        id: autoStartBox
                        Accessible.name: i18n("Start automatically")
                        model: [i18n("Manual"), i18n("Breaks only"), i18n("All phases")]
                        currentIndex: (page.preset.options && page.preset.options.autoStartMode !== undefined)
                            ? Math.max(0, Math.min(2, Number(page.preset.options.autoStartMode)))
                            : 0
                    }
                }
                FormRow {
                    title: i18n("Silence notifications while working")
                    description: SilenceController.available
                        ? (DesktopStyle.macos
                           ? i18n("Turns Do Not Disturb on for the work phase and off for the break, through the two Focus shortcuts.")
                           : i18n("Turns off all system notifications for the work phase and back on for the break."))
                        : DesktopStyle.macos
                          ? i18n("Needs the two Focus shortcuts: install them in Settings → Focus.")
                          : i18n("Not available: this desktop's notification service cannot be silenced by applications.")
                    enabled: SilenceController.available || DesktopStyle.macos
                    // macOS: until the shortcuts exist, a way to set them up instead of the switch.
                    SButton {
                        objectName: "silenceSetupButton"
                        visible: DesktopStyle.macos && !SilenceController.available
                        text: i18n("Set Up…")
                        iconName: "settings"
                        onClicked: applicationWindow().openSettings("focus")
                    }
                    SSwitch {
                        id: silenceBox
                        visible: SilenceController.available || !DesktopStyle.macos
                        enabled: SilenceController.available
                        Accessible.name: i18n("Silence notifications while working")
                        checked: (page.preset.options && page.preset.options.silenceWhileWorking !== undefined)
                            ? Boolean(page.preset.options.silenceWhileWorking)
                            : false
                    }
                }
                FormRow {
                    title: i18n("Play a sound when a phase ends")
                    SSwitch {
                        id: soundBox
                        Accessible.name: i18n("Play a sound when a phase ends")
                        checked: (page.preset.options && page.preset.options.soundOnEnd !== undefined)
                            ? Boolean(page.preset.options.soundOnEnd)
                            : true
                    }
                }
                FormRow {
                    title: i18n("Show a notification when a phase ends")
                    SSwitch {
                        id: notifyBox
                        Accessible.name: i18n("Show a notification when a phase ends")
                        checked: (page.preset.options && page.preset.options.notifyOnEnd !== undefined)
                            ? Boolean(page.preset.options.notifyOnEnd)
                            : true
                    }
                }
            }

            SButton {
                visible: page.preset.builtin === true
                kind: "ghost"
                iconName: "undo"
                text: i18n("Reset to Defaults")
                onClicked: page.requestReset()
            }
        }
    }
}
