// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

QQC2.ApplicationWindow {
    id: root

    color: Theme.background
    // Linux: no system title bar; the top bar moves the window and has its buttons, the edges
    // resize it. macOS (DesktopStyle.nativeChrome): the system's title bar, transparent over the
    // content, with its own buttons (the traffic lights), resizing, full screen and tiling.
    flags: DesktopStyle.nativeChrome ? (Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint)
                                     : (Qt.Window | Qt.FramelessWindowHint)
    // The content fills the whole window, also under macOS' transparent title bar: by default
    // ApplicationWindow keeps it in the safe area below it, where nothing could move the window.
    // The top bar and the sidebar leave that strip free for the traffic lights themselves.
    topPadding: 0
    leftPadding: 0
    rightPadding: 0
    bottomPadding: 0
    readonly property bool maximized: visibility === Window.Maximized || visibility === Window.FullScreen
    readonly property bool wideScreen: width >= Theme.wideBreakpoint

    // The colours of the standard controls (Basic style).
    palette {
        window: Theme.background
        windowText: Theme.foreground
        base: Theme.card
        alternateBase: Theme.raised
        text: Theme.foreground
        button: Theme.card
        buttonText: Theme.foreground
        brightText: Theme.foreground
        highlight: Theme.foreground
        highlightedText: Theme.background
        placeholderText: Theme.subtle
        toolTipBase: Theme.raised
        toolTipText: Theme.foreground
        light: Theme.raised
        midlight: Theme.selected
        mid: Theme.borderStrong
        dark: Theme.muted
        shadow: Theme.scrim
        link: Theme.brandPeach
    }

    /// What the pages call to reach the window.
    function applicationWindow(): var {
        return root;
    }

    /// A short message at the bottom of the window; `timeout` "long" keeps it longer.
    function showPassiveNotification(message: string, timeout: var): void {
        toast.show(message, timeout);
    }

    title: {
        if (!TimerEngine.active) {
            return i18n("Sugo");
        }
        // As the top bar shows it: h:mm:ss past an hour, else mm:ss.
        const total = TimerEngine.remainingSeconds;
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60);
        const s = total % 60;
        const pad = n => String(n).padStart(2, "0");
        const timeStr = h > 0 ? h + ":" + pad(m) + ":" + pad(s) : pad(m) + ":" + pad(s);
        let phaseStr = i18n("Work");
        if (TimerEngine.phase === TimerEngine.Phase.ShortBreak) {
            phaseStr = i18n("Short break");
        } else if (TimerEngine.phase === TimerEngine.Phase.LongBreak) {
            phaseStr = i18n("Long break");
        }
        if (TimerEngine.paused) {
            return i18n("[%1] %2 (Paused) — Sugo", timeStr, phaseStr);
        }
        return i18n("[%1] %2 — Sugo", timeStr, phaseStr);
    }
    width: Theme.windowWidth
    height: Theme.windowHeight
    minimumWidth: Theme.windowMinWidth
    minimumHeight: Theme.windowMinHeight

    // Started with --background and a tray icon is there: begin hidden. The initial visibility
    // is set once in Component.onCompleted: a binding would hide the window again whenever the
    // tray or start-minimized settings change later.

    /// Set from C++ (main.cpp) when macOS asks the application to quit (Cmd+Q from the Dock,
    /// logging out): the window then lets itself close.
    property bool platformQuitting: false

    // The close button hides the window when the program is meant to live in the tray, and
    // always on macOS (Sugo stays in the Dock and the menu bar; Quit ends it); otherwise it quits
    // (the application does not quit on its own when a window closes).
    onClosing: close => {
        if (root.platformQuitting) {
            close.accepted = true;
        } else if (DesktopStyle.nativeChrome || AppSettings.keepRunningInTray) {
            close.accepted = false;
            root.hide();
        } else {
            close.accepted = false;
            root.requestQuit();
        }
    }

    /// Shows the window in front: from hidden, or from the Dock or the taskbar as it was (a
    /// maximised window stays maximised), as main.cpp does for the tray and second launches.
    function bringToFront(): void {
        DesktopStyle.showRestored(root);
        root.raise();
        root.requestActivate();
    }

    /// Quits, after asking when that would cut a running or paused phase short.
    function requestQuit(): void {
        if (quitDialog.visible) {
            return;
        }
        if (TimerEngine.active) {
            // Asked with the window closed or in the Dock (Cmd+Q, Sugo › Quit Sugo on macOS,
            // where closing only hides the window): the question must be seen.
            if (!root.visible || root.visibility === Window.Minimized) {
                root.bringToFront();
            }
            quitDialog.open();
        } else {
            root.quitNow();
        }
    }

    /// Quits so that the update just installed starts in its place (asks like requestQuit()).
    function requestRestart(): void {
        root.restarting = true;
        root.requestQuit();
    }

    property bool restarting: false

    function quitNow(): void {
        UpdateChecker.setRestartOnQuit(root.restarting);
        Qt.quit();
    }

    readonly property bool quitConfirming: quitDialog.visible
    readonly property bool taskNoteOpen: taskNoteDialog.visible

    ConfirmDialog {
        id: quitDialog
        title: root.restarting ? i18n("Restart Sugo?") : i18n("Quit Sugo?")
        text: root.restarting
              ? (TimerEngine.phase === TimerEngine.Phase.Work
                 ? i18n("A work session is in progress. Restarting ends it now, and it is saved as interrupted.")
                 : i18n("A break is in progress. Restarting ends it now."))
              : (TimerEngine.phase === TimerEngine.Phase.Work
                 ? i18n("A work session is in progress. Quitting ends it now, and it is saved as interrupted.")
                 : i18n("A break is in progress. Quitting ends it now."))
        confirmText: root.restarting ? i18n("Restart") : i18n("Quit")
        confirmKind: "danger"
        cancelText: i18n("Keep Running")
        onConfirmed: {
            quitDialog.close();
            root.quitNow();
        }
        onClosed: root.restarting = false
    }

    /// Asks what was done during a finished work session. `sessionId` is the session the note
    /// is stored on; without one the most recent work session is used.
    function openTaskNotePrompt(presetName, sessionId) {
        let id = (sessionId === undefined || sessionId === null) ? DataManager.lastWorkSessionId() : Number(sessionId);
        if (!(id > 0)) {
            return;
        }
        // One at a time: a note being typed stays with its own session, and this one is asked
        // for once the dialog closes.
        if (taskNoteDialog.visible) {
            if (id !== taskNoteDialog.sessionId) {
                root.pendingTaskNote = { presetName: presetName || "", sessionId: id };
            }
            return;
        }
        taskNoteDialog.presetName = presetName || "";
        taskNoteDialog.sessionId = id;
        taskNoteDialog.open();
    }

    function saveTaskNote(note: string): void {
        const text = note.trim();
        if (text.length > 0 && taskNoteDialog.sessionId > 0) {
            DataManager.updateSessionNote(taskNoteDialog.sessionId, text);
        }
        taskNoteDialog.close();
    }

    // Taskbar flash and optional in-app note prompt on phase completion.
    Connections {
        target: TimerEngine
        function onPhaseFinished(finished, next) {
            root.alert(0);
            if (finished === TimerEngine.Phase.Work && AppSettings.promptTaskNote) {
                root.offerTaskNote(PresetModel.currentName, DataManager.lastWorkSessionId());
            }
        }
    }

    // Values brought into range by the timer model are shown, not changed silently.
    property string lastAdjustmentMessage: ""
    Connections {
        target: PresetModel
        function onValuesAdjusted(uuid, fields) {
            const labels = {
                name: i18n("name"),
                category: i18n("category"),
                workSeconds: i18n("work duration"),
                shortBreakSeconds: i18n("short break"),
                longBreakSeconds: i18n("long break"),
                cyclesBeforeLong: i18n("work sessions before the long break")
            };
            const names = fields.map(field => labels[field] || field);
            root.lastAdjustmentMessage = i18np("This value of “%2” was outside the allowed range and was adjusted: %3.",
                                               "These values of “%2” were outside the allowed range and were adjusted: %3.",
                                               names.length, PresetModel.get(uuid).name, names.join(", "));
            root.showPassiveNotification(root.lastAdjustmentMessage, "long");
        }
    }

    // A work session that ends while the window is hidden: its note prompt waits for the window,
    // unless the timer's end notification is on (it asks for the note itself). Also one that ends
    // while the note of another is being asked for: it waits for that dialog to close.
    property var pendingTaskNote: null

    function offerTaskNote(presetName: string, sessionId: var): void {
        if (root.visible && root.active) {
            root.openTaskNotePrompt(presetName, sessionId);
            return;
        }
        const notified = PresetModel.option(PresetModel.currentUuid, "notifyOnEnd") === true;
        root.pendingTaskNote = notified ? null : { presetName: presetName, sessionId: sessionId };
    }

    function showPendingTaskNote(): void {
        if (!root.pendingTaskNote || !root.visible || !root.active) {
            return;
        }
        const pending = root.pendingTaskNote;
        root.pendingTaskNote = null;
        root.openTaskNotePrompt(pending.presetName, pending.sessionId);
    }

    // --- Page creation -------------------------------------------------------
    //
    // Pages are created here under a visual parent and pushed as items. The stack does not own
    // such pages, so popPage() and clearPages() destroy them.

    Item {
        id: pageParking
        visible: false
    }

    property var pageComponents: ({})
    Timer {
        interval: 100
        repeat: true
        running: appStatsSmokeTest
        property int attempts: 0
        onTriggered: {
            ++attempts;
            const statsPage = root.pageStack.currentItem;
            // By destination, not by the (translated) page title.
            if (statsPage && root.currentDestination === "stats" && statsPage.contentLaidOut) {
                // Changing the period and the breakdown is followed by the page.
                StatsModel.period = StatsModel.Period.Month;
                StatsModel.breakdownMode = StatsModel.BreakdownMode.ByCategory;
                if (statsPage.periodId !== "month" || statsPage.breakdownId !== "category") {
                    console.error("The statistics page does not follow the period or the breakdown");
                    Qt.exit(18);
                    return;
                }
                Qt.exit(0);
            } else if (attempts >= 50) {
                const component = root.pageComponents["StatsPage.qml"];
                console.error("Statistics page did not lay out within five seconds:",
                              "destination=", root.currentDestination,
                              "component=", component ? component.status : "missing",
                              "error=", component ? component.errorString() : "none",
                              "currentPage=", statsPage ? statsPage.title : "missing",
                              "contentReady=", statsPage ? statsPage.contentLaidOut : "unavailable");
                if (root.currentDestination !== "stats") {
                    Qt.exit(11);
                } else if (!component) {
                    Qt.exit(12);
                } else if (component.status === Component.Loading) {
                    Qt.exit(13);
                } else if (component.status === Component.Error) {
                    const error = component.errorString();
                    Qt.exit(error.length > 0 ? error.charCodeAt(0) : 200);
                } else if (!statsPage) {
                    Qt.exit(15);
                } else {
                    Qt.exit(16);
                }
            }
        }
    }

    Timer {
        interval: 100
        repeat: true
        id: smokeChecks
        running: typeof appSmokePage !== "undefined" && appSmokePage.length > 0
        property int attempts: 0
        property int stage: 0
        property var editor: null
        property string userUuid: ""
        property string builtinUuid: ""
        property bool paletteRan: false
        property real wideWidth: 0
        property real welcomePanelY: 0
        // The stage at which the timer page's track check began (0: not yet).
        property int trackCheckStage: 0
        property bool longSetChecked: false

        function findItem(item: var, predicate: var): var {
            if (!item) {
                return null;
            }
            if (predicate(item)) {
                return item;
            }
            const kids = item.children || [];
            for (let i = 0; i < kids.length; ++i) {
                const found = findItem(kids[i], predicate);
                if (found) {
                    return found;
                }
            }
            return null;
        }

        function fail(code: int, message: string): void {
            console.error(message);
            stop();
            Qt.exit(code);
        }

        // Settings: every switch has the name of its row for screen readers.
        function checkSettings(page: var): void {
            const switches = [];
            findItem(page, item => {
                if (item.setting !== undefined && item.accessibleName !== undefined) {
                    switches.push(item);
                }
                return false;
            });
            const unnamed = switches.filter(sw => sw.accessibleName.length === 0).map(sw => sw.setting);
            if (switches.length < 9 || unnamed.length > 0) {
                fail(23, "Settings switches without an accessible name: found=" + switches.length + " unnamed=" + unnamed.join(","));
                return;
            }
            // A dragged slider keeps following the setting (a drag sets `value` from C++, which
            // does not drop the binding).
            const slider = findItem(page, item => item.objectName === "soundVolumeSlider");
            if (!slider) {
                fail(29, "Sound volume slider not found");
                return;
            }
            slider.increase();
            slider.moved();
            AppSettings.soundVolume = 17;
            if (Math.round(slider.value) !== 17) {
                fail(29, "The volume slider no longer follows the setting after a drag: " + slider.value);
                return;
            }
            // The Focus card (shortcuts that silence notifications) exists on macOS only, and
            // openSettings() shows the category it is in.
            const focusCard = findItem(page, item => item.objectName === "focusModeCard");
            if (!focusCard || focusCard.available !== DesktopStyle.macos) {
                fail(44, "The Focus card is missing, or shown outside macOS");
                return;
            }
            root.openSettings("focus");
            const focusShown = page.category === "focus" && focusCard.visible === DesktopStyle.macos;
            page.category = "general";
            if (!focusShown) {
                fail(44, "openSettings(\"focus\") did not show the Focus category: " + page.category);
                return;
            }
            // "Install Shortcuts" is there, and off in a smoke run (no installer: the real tools
            // must never run).
            const installButton = findItem(focusCard, item => item.objectName === "installFocusShortcutsButton");
            if (!installButton || installButton.enabled) {
                fail(45, "The Focus card's install button is missing, or on in a smoke run");
                return;
            }
            // A calendar address still being typed (its field keeps the focus: a click on a
            // button does not take it) is stored before Log in, Test or Find calendars use it.
            if (CalendarSync) {
                const serverField = findItem(page, item => item.objectName === "calendarServerField");
                if (!serverField) {
                    fail(53, "The calendar server field was not found");
                    return;
                }
                page.category = "calendar";
                CalendarSync.enabled = true;
                serverField.forceActiveFocus();
                const focused = serverField.activeFocus;
                serverField.text = "https://cloud.example.org";
                page.commitCalendarFields();
                const stored = CalendarSync.serverUrl;
                CalendarSync.serverUrl = "";
                CalendarSync.enabled = false;
                page.category = "general";
                if (!focused || stored !== "https://cloud.example.org") {
                    fail(53, "The typed calendar address was not stored before the action: focused=" + focused
                         + " stored=" + stored);
                    return;
                }
            }
            // Disconnecting the calendar asks first.
            page.requestDisconnect();
            if (!page.disconnectConfirming) {
                fail(32, "Disconnecting the calendar did not ask first");
                return;
            }
            if (!checkSync(page)) {
                return;
            }
                        // Quitting while a phase runs asks first.
            // Also with the window closed (Cmd+Q on macOS): the window comes back to ask.
            TimerEngine.start();
            root.hide();
            root.requestQuit();
            const asked = root.quitConfirming;
            const shown = root.visible;
            quitDialog.close();
            TimerEngine.stop();
            if (!asked) {
                fail(28, "Quitting during a work session did not ask first");
            } else if (!shown) {
                fail(38, "Quitting with the window closed asked in the hidden window");
            }
        }

        // Settings, Sync tab (signed out: the controller is not attached in a smoke run).
        function checkSync(page: var): bool {
            const ids = page.categories.map(category => category.id);
            const at = ids.indexOf("sync");
            if (at < 0 || ids[at - 1] !== "calendar" || ids[at + 1] !== "advanced") {
                fail(56, "The Sync tab is missing, or not between Calendar and Advanced: " + ids.join(","));
                return false;
            }
            page.category = "sync";
            const signIn = findItem(page, item => item.objectName === "syncSignInButton");
            if (SyncController.state !== SyncController.Off || !signIn || !signIn.visible || signIn.text.length === 0) {
                fail(57, "Signed out, the Sync tab does not offer Sign in");
                return false;
            }
            // A message (no keyring, signed out on sugo.stream) goes in the Sign in row's
            // description, which keeps room for the longest of its texts. The layout catches up
            // only later, so each text is compared with that room rather than the row's height.
            const row = findItem(page, item => item.objectName === "syncSignInRow");
            if (!row || row.height <= 0 || row.descriptionVariants.length < 3 || row.descriptionRoom <= 0) {
                fail(58, "The Sync tab's Sign in row, or the messages it can show, is missing");
                return false;
            }
            for (const variant of [page.syncSignInText, page.syncNoKeyringText, page.syncSignedOutRemotelyText]) {
                row.description = variant;
                if (row.descriptionHeight > row.descriptionRoom + 0.5) {
                    fail(58, "A message would move the Sync tab: " + row.descriptionHeight + " > " + row.descriptionRoom
                         + " for \"" + variant + "\"");
                    return false;
                }
            }
            // Signing out asks first.
            page.requestSyncSignOut();
            const asked = page.syncSignOutConfirming;
            page.category = "general";
            if (!asked) {
                fail(59, "Signing out of Sugo Sync did not ask first");
                return false;
            }
            return true;
        }

        // Welcome: the tray icon's colours, the Focus row, the step dots; then a message on the
        // setup step must not move the step (stage 1 → 2, a tick apart so the layout settles).
        function checkWelcome(page: var): bool {
            if (stage === 1) {
                ++stage;
                const panel = findItem(page, item => item.objectName === "welcomeSetupPanel");
                const slot = findItem(page, item => item.objectName === "welcomeSetupMessage");
                if (!panel || !slot) {
                    fail(47, "The welcome tour's setup panel or its message place is missing");
                    return true;
                }
                welcomePanelY = panel.mapToItem(page, 0, 0).y;
                page.autostartError = "Smoke test: a message long enough to need more than one line in the welcome tour, "
                                      + "so that a place too small for it would show.";
                return false;
            }
            if (stage === 2) {
                const panel = findItem(page, item => item.objectName === "welcomeSetupPanel");
                const y = panel.mapToItem(page, 0, 0).y;
                page.autostartError = "";
                if (Math.abs(y - welcomePanelY) > 0.5) {
                    fail(47, "A message on the welcome tour's setup step moved the step: y " + welcomePanelY + " → " + y);
                }
                return true;
            }
            // The tray icon's picture uses the colours the real icon is painted with.
            const dial = findItem(page, item => item.objectName === "trayDialPreview");
            if (!dial || !Qt.colorEqual(dial.primary, dial.isBreak ? TrayIconStyle.breakColor : TrayIconStyle.workColor)) {
                fail(34, "The tray icon preview does not use the tray icon's colours");
                return true;
            }
            // The Focus shortcuts' row: hidden outside macOS, its button off without an installer.
            // (On macOS its step may not be on screen, so its visibility says nothing there.)
            const focusInstall = findItem(page, item => item.objectName === "welcomeInstallFocusShortcuts");
            if (!focusInstall || focusInstall.enabled || (!DesktopStyle.macos && focusInstall.visible)) {
                fail(46, "The welcome tour's Focus shortcuts row is missing, or shown outside macOS");
                return true;
            }
            // The step dots are named, move the slides, and keep following them.
            const indicator = findItem(page, item => item.objectName === "welcomeIndicator");
            const last = indicator ? indicator.count - 1 : -1;
            const lastDot = findItem(page, item => item.objectName === "welcomeStepDot-" + last);
            const firstDot = findItem(page, item => item.objectName === "welcomeStepDot-0");
            if (!indicator || last < 1 || !lastDot || !firstDot || lastDot.accessibleName.length === 0) {
                fail(31, "Welcome step dots missing or unnamed");
                return true;
            }
            lastDot.activate();
            if (indicator.currentIndex !== last) {
                fail(31, "Choosing a step dot did not move the tour: " + indicator.currentIndex);
                return true;
            }
            firstDot.activate();
            if (indicator.currentIndex !== 0) {
                fail(31, "The dots stopped following the tour: " + indicator.currentIndex);
                return true;
            }
            // On to the setup step for the layout check.
            page.current = 2;
            stage = 1;
            return false;
        }

        // Timer page: the picker's text, Stop/Skip confirmation, the note of a hidden window.
        function checkTimer(page: var): bool {
            switch (stage++) {
            case 0:
                // Started on a damaged database (smoke_app.sh damaged-db): the new one says so.
                if (DataManager !== null && DataManager.movedAsideDatabasePath.length > 0
                        && (!dataWarning.shown || !dataBanner.text.includes(DataManager.movedAsideDatabasePath))) {
                    fail(55, "The database moved aside at startup is not reported");
                    return true;
                }
                // New users have one timer: add a second one to choose (its row appears next tick).
                PresetModel.create({ name: "Smoke" });
                return false;
            case 1: {
                // A timer chosen in the sidebar becomes the one in use, and the timer page shows it.
                const entry = findItem(root.contentItem, item => item.objectName === "timerEntry-1");
                if (!entry) {
                    fail(40, "The timers of the sidebar are missing");
                    return true;
                }
                entry.clicked();
                if (PresetModel.currentIndex !== 1 || root.currentDestination !== "preset") {
                    fail(40, "Choosing a timer in the sidebar did not select it and open its page");
                    return true;
                }
                root.navigate("timer");
                page = root.pageStack.currentItem;
                if (page.title !== PresetModel.currentName) {
                    fail(40, "The timer page shows \"" + page.title + "\" instead of \"" + PresetModel.currentName + "\"");
                    return true;
                }
                // A phase that has only just started stops at once.
                TimerEngine.start();
                page.requestStop();
                if (TimerEngine.active || page.endConfirming) {
                    fail(41, "Stopping a phase that just started asked first");
                    return true;
                }
                // Work under way asks before it is cut short.
                root.confirmAfterSeconds = 0;
                TimerEngine.start();
                page.requestSkip();
                if (!TimerEngine.active || !page.endConfirming) {
                    fail(42, "Skipping work under way did not ask first");
                    return true;
                }
                TimerEngine.stop();
                // Stop from the search asks where it is, without leaving the page.
                root.navigate("stats");
                TimerEngine.start();
                root.requestEndPhase(false);
                if (root.currentDestination !== "stats" || !TimerEngine.active || !root.endPhaseConfirming) {
                    fail(48, "Stopping work under way away from the timer page did not ask there");
                    return true;
                }
                TimerEngine.stop();
                root.confirmAfterSeconds = 60;
                root.navigate("timer");
                // A note for a session that ended while the window was hidden waits for it.
                PresetModel.setOption(PresetModel.currentUuid, "notifyOnEnd", false);
                root.hide();
                root.offerTaskNote("Smoke", 1);
                if (!root.pendingTaskNote || root.taskNoteOpen) {
                    fail(43, "The task note was not kept for later while the window was hidden");
                    return true;
                }
                root.show();
                root.requestActivate();
                return false;
            }
            default:
                if (trackCheckStage === 0) {
                    if (!root.active && stage < 31) {
                        return false; // wait for the window to be active again
                    }
                    root.showPendingTaskNote();
                    if (root.active && (root.pendingTaskNote || !root.taskNoteOpen)) {
                        fail(43, "The kept task note did not open with the window");
                        return true;
                    }
                    taskNoteDialog.close();
                    // A work phase of one second, then (Manual mode) the break waits for Start.
                    PresetModel.setOption(PresetModel.currentUuid, "autoStartMode", 0);
                    PresetModel.setOption(PresetModel.currentUuid, "soundOnEnd", false);
                    TimerEngine.startCustomWork(1);
                    trackCheckStage = stage;
                    return false;
                }
                return checkTimerTrack(page);
            }
        }

        // The cycle track of the timer page: what is done stays done while the next phase waits
        // for Start, and nothing is done once stopped.
        function checkTimerTrack(page: var): bool {
            if (TimerEngine.active) {
                if (stage - trackCheckStage < 30) {
                    return false;
                }
                fail(49, "A work phase of one second did not end");
                return true;
            }
            if (longSetChecked) {
                const track = findItem(page, item => item.objectName === "cycleTrack");
                const row = findItem(page, item => item.objectName === "cycleSegments");
                if (!track || !row || row.childrenRect.width > track.width + 1) {
                    fail(52, "The track of a long set runs past its place: "
                         + (row ? row.childrenRect.width : "missing") + " > " + (track ? track.width : "missing"));
                }
                return true;
            }
            const first = findItem(page, item => item.objectName === "cycleSegment-0");
            const second = findItem(page, item => item.objectName === "cycleSegment-1");
            if (!first || !second) {
                fail(49, "The segments of the cycle track are missing");
                return true;
            }
            if (TimerEngine.phase !== TimerEngine.Phase.ShortBreak || !first.done || !second.current) {
                fail(49, "The work just done emptied the track while the break waits: phase " + TimerEngine.phase
                     + ", first done " + first.done + ", second current " + second.current);
                return true;
            }
            TimerEngine.start();
            TimerEngine.stop();
            if (first.done || !first.current) {
                fail(49, "The track still shows work done after Stop");
                return true;
            }
            // The longest set: its track lays out on the next tick.
            PresetModel.update(PresetModel.currentUuid, { cyclesBeforeLong: PresetModel.maxCycles });
            longSetChecked = true;
            return false;
        }

        // A timer's page: one stage per tick, so bindings and Qt.callLater() run in between.
        function checkPresets(page: var): bool {
            switch (stage++) {
            case 0:
                // The running timer cannot be deleted; once stopped it can.
                builtinUuid = PresetModel.currentUuid;
                userUuid = PresetModel.create({ name: "Smoke" });
                PresetModel.currentUuid = userUuid;
                root.openTimer(userUuid);
                TimerEngine.start();
                // The top bar at the narrowest window, with the section and the pill shown.
                wideWidth = root.width;
                root.width = Theme.windowMinWidth;
                return false;
            case 1: {
                const insideWindow = item => !item || !item.visible || item.mapToItem(root.contentItem, item.width, 0).x <= root.width + 1;
                const pill = findItem(root.contentItem, item => item.objectName === "statusPill");
                const buttons = findItem(root.contentItem, item => item.objectName === "topBarWindowButtons");
                if (root.width > Theme.windowMinWidth || !pill || !pill.visible || !insideWindow(pill) || !insideWindow(buttons)) {
                    fail(37, "The top bar does not fit a narrow window: width " + root.width + ", pill " + (pill ? pill.visible : "missing"));
                    return true;
                }
                root.width = wideWidth;
                const button = findItem(root.pageStack.currentItem, item => item.objectName === "deleteButton-" + userUuid);
                if (!button || button.enabled) {
                    fail(25, "Delete button of the running timer: " + (button ? "enabled" : "missing"));
                    return true;
                }
                TimerEngine.stop();
                if (!button.enabled) {
                    fail(25, "Delete button stays disabled after stopping the timer");
                    return true;
                }
                PresetModel.currentUuid = builtinUuid;
                root.navigate("timer");
                PresetModel.remove(userUuid);
                root.newTimer();
                editor = root.pageStack.currentItem;
                return false;
            }
            case 2:
                // A click on an entry of the search runs it (checked on the next tick, once the
                // list has its rows).
                paletteRan = false;
                commandPalette.commands = [{ text: "Smoke", run: () => { paletteRan = true; } }];
                commandPalette.open();
                return false;
            case 3: {
                const entry = findItem(commandPalette.contentItem, item => item.text === "Smoke" && typeof item.clicked === "function");
                if (!entry) {
                    fail(35, "The search shows no entries");
                    return true;
                }
                entry.clicked();
                if (!paletteRan || commandPalette.visible) {
                    fail(35, "Clicking an entry of the search did not run it");
                    return true;
                }
                // Unsaved edits are not thrown away without asking.
                if (!editor || !editor.isNew || editor.modified) {
                    fail(26, "New timer page reports changes before any edit");
                    return true;
                }
                editor.selectedIcon = editor.selectedIcon === "chronometer" ? "clock" : "chronometer";
                if (!editor.modified) {
                    fail(26, "Changing the icon did not mark the timer as modified");
                    return true;
                }
                root.navigate("timer");
                if (root.pageStack.currentItem !== editor || !editor.confirming || root.currentDestination !== "preset") {
                    fail(26, "Leaving through the sidebar with unsaved changes did not ask first");
                    return true;
                }
                editor.requestClose();
                if (root.pageStack.currentItem !== editor || !editor.confirming) {
                    fail(26, "Closing with unsaved changes did not ask first");
                    return true;
                }
                editor.leaving = true;
                root.openTimer(builtinUuid);
                editor = root.pageStack.currentItem;
                // A search that finds nothing says so (checked on the next tick).
                commandPalette.commands = [];
                commandPalette.open();
                return false;
            }
            case 4: {
                const nothingFound = findItem(commandPalette.contentItem, item => item.objectName === "nothingFound");
                if (!nothingFound || !nothingFound.visible || nothingFound.height <= 0 || nothingFound.parent.height <= 0) {
                    fail(36, "The search does not say when nothing was found");
                    return true;
                }
                commandPalette.close();
                // A value out of range is reported to the user, not changed silently.
                root.lastAdjustmentMessage = "";
                const adjustedUuid = PresetModel.create({ name: "Smoke", workSeconds: 100 * 3600 });
                const message = root.lastAdjustmentMessage;
                PresetModel.remove(adjustedUuid);
                if (message.length === 0) {
                    fail(33, "An adjusted value was not reported");
                    return true;
                }
                // Resetting a built-in timer asks first, and changes nothing until confirmed.
                const before = JSON.stringify(PresetModel.get(builtinUuid));
                if (!PresetModel.get(builtinUuid).builtin) {
                    fail(27, "The selected timer is not a built-in one");
                    return true;
                }
                editor.requestReset();
                if (root.pageStack.currentItem !== editor || !editor.confirming || JSON.stringify(PresetModel.get(builtinUuid)) !== before) {
                    fail(27, "Reset to defaults did not ask first");
                    return true;
                }
                // A timer stored without breaks (0 minutes: no break).
                editor.leaving = true;
                userUuid = PresetModel.create({ name: "Smoke", shortBreakSeconds: 0, longBreakSeconds: 0 });
                root.openTimer(userUuid);
                editor = root.pageStack.currentItem;
                return false;
            }
            case 5: {
                // Its page shows no break, and does not count that as a change.
                const shortField = findItem(editor, item => item.objectName === "shortBreakField");
                const longField = findItem(editor, item => item.objectName === "longBreakField");
                if (!shortField || !longField || shortField.value !== 0 || longField.value !== 0 || editor.modified) {
                    fail(50, "A timer without breaks opened with breaks, or as changed: "
                         + (shortField ? shortField.value : "missing") + ", " + (longField ? longField.value : "missing")
                         + ", modified " + editor.modified);
                    return true;
                }
                // The longest set: its preview lays out on the next tick.
                const cyclesField = findItem(editor, item => item.objectName === "cyclesField");
                if (!cyclesField) {
                    fail(52, "The field of the cycles is missing");
                    return true;
                }
                cyclesField.setValue(PresetModel.maxCycles);
                return false;
            }
            case 6: {
                // The preview of the longest set stays within its box.
                const preview = findItem(editor, item => item.objectName === "cyclePreview");
                const blocks = findItem(editor, item => item.objectName === "cycleBlocks");
                if (!preview || !blocks || blocks.childrenRect.width > preview.width + 1) {
                    fail(52, "The preview of a long set runs past its box: "
                         + (blocks ? blocks.childrenRect.width : "missing") + " > " + (preview ? preview.width : "missing"));
                    return true;
                }
                // Duplicate with unsaved changes asks first, and makes no copy meanwhile.
                const count = PresetModel.count;
                editor.duplicate();
                if (root.pageStack.currentItem !== editor || !editor.confirming || PresetModel.count !== count) {
                    fail(51, "Duplicate with unsaved changes did not ask first, or copied before the answer");
                    return true;
                }
                return true;
            }
            }
            return true;
        }

        onTriggered: {
            ++attempts;
            const currentPage = root.pageStack.currentItem;
            // Started next to a 1.0.0 database that 1.0.1 moved aside (smoke_app.sh sugo100-db):
            // its timer "mine" is back, and the toast says so.
            if (currentPage && attempts === 3 && DataManager !== null && DataManager.sugo100RecoveryReplacedPath.length > 0
                    && (PresetModel.indexOf("mine") < 0 || !toast.message.includes(DataManager.sugo100RecoveryReplacedPath))) {
                fail(60, "The timers of Sugo 1.0.0 taken back at startup are missing or not reported");
                return;
            }
            if (currentPage && attempts >= 3) {
                let done = true;
                if (appSmokePage === "settings") {
                    checkSettings(currentPage);
                } else if (appSmokePage === "welcome") {
                    done = checkWelcome(currentPage);
                } else if (appSmokePage === "presets") {
                    done = checkPresets(currentPage);
                } else if (appSmokePage === "timer") {
                    done = checkTimer(root.pageStack.get(0));
                } else if (appSmokePage === "inventory") {
                    done = featureInventory.step();
                }
                if (done && running) {
                    stop();
                    Qt.exit(0);
                }
            } else if (attempts >= 50) {
                const targetFile = root.destinations[appSmokePage === "presets" ? "preset" : appSmokePage];
                const component = targetFile ? root.pageComponents[targetFile] : null;
                console.error("Page", appSmokePage, "did not load within five seconds:",
                              "component=", component ? component.status : "missing",
                              "error=", component ? component.errorString() : "none",
                              "currentPage=", currentPage ? currentPage.title : "missing");
                Qt.exit(20);
            }
        }
    }

    /// `file` is relative to the pages/ directory, e.g. "TimerPage.qml".
    function createPage(file: string, properties: var): Item {
        let component = pageComponents[file];
        if (!component) {
            component = Qt.createComponent(Qt.resolvedUrl("pages/" + file));
            pageComponents[file] = component;
        }
        if (component.status !== Component.Ready) {
            console.error("Cannot load page", file + ":", component.errorString());
            return null;
        }
        return component.createObject(pageParking, properties ?? {});
    }

    /// Creates the page and pushes it; pages call this to open sub-pages.
    function pushPage(file: string, properties: var): Item {
        const page = createPage(file, properties);
        if (page) {
            pageStack.push(page);
        }
        return page;
    }

    property alias pageStack: stack

    /// Removes the page on top (a sub-page such as the timer editor).
    function popPage(): void {
        if (stack.depth > 1) {
            const page = stack.pop(QQC2.StackView.Immediate);
            if (page) {
                page.destroy();
            }
        }
    }

    function popToRoot(): void {
        while (stack.depth > 1) {
            popPage();
        }
    }

    function clearPages(): void {
        const pages = [];
        for (let i = 0; i < stack.depth; ++i) {
            pages.push(stack.get(i, QQC2.StackView.DontLoad));
        }
        stack.clear(QQC2.StackView.Immediate);
        pages.forEach(page => page && page.destroy());
    }

    /// The back button: a sub-page with unsaved changes asks first.
    function goBack(): void {
        const top = stack.currentItem;
        if (top && typeof top.requestClose === "function") {
            top.requestClose();
        } else {
            popPage();
        }
    }

    // --- Navigation ----------------------------------------------------------

    readonly property var destinations: ({
        timer: "TimerPage.qml",
        preset: "PresetEditPage.qml",
        stats: "StatsPage.qml",
        settings: "AppSettingsPage.qml",
        about: "AboutPage.qml",
        welcome: "WelcomePage.qml"
    })

    // The visible destination: "timer", "preset" (a timer's page), "stats", "settings", "about" or "welcome".
    property string currentDestination: "timer"
    /// The timer whose page is open ("" for a new one).
    property string openTimerUuid: ""
    readonly property bool isWelcomeActive: currentDestination === "welcome"

    /// Shows a destination; "preset" with the timer's `uuid`. `reload` builds the page afresh.
    function navigate(name: string, uuid: var, reload: var): void {
        const target = uuid ?? "";
        // A page with unsaved changes (a timer's page) asks before it is left. A number still
        // being typed counts, even one that did not change the value yet ("0", out of range).
        const top = pageStack.currentItem;
        if (top && typeof top.commitFields === "function") {
            top.commitFields();
        }
        if (top && top.modified === true && top.leaving !== true && typeof top.requestClose === "function") {
            top.requestClose(() => root.navigate(name, uuid, reload));
            return;
        }
        if (currentDestination === name && (name !== "preset" || openTimerUuid === target) && reload !== true) {
            popToRoot();
            return;
        }
        currentDestination = name;
        openTimerUuid = name === "preset" ? target : "";
        clearPages();
        pushPage(destinations[name], name === "preset" ? { uuid: target } : {});
    }

    /// For the C++ side (tray menu): a destination by name.
    function openPage(name: string): void {
        navigate(name);
    }

    /// The settings category the settings page opens on next ("" for its default).
    property string pendingSettingsCategory: ""

    /// Opens the settings on one category, e.g. "focus".
    function openSettings(category: string): void {
        pendingSettingsCategory = category;
        navigate("settings");
        // Already open: navigate() kept the page, so tell it here.
        const top = pageStack.currentItem;
        if (top && top.categories !== undefined && pendingSettingsCategory.length > 0) {
            top.category = pendingSettingsCategory;
            pendingSettingsCategory = "";
        }
    }

    /// Opens a timer's page; `reload` shows it afresh (after a reset, or dropped changes).
    function openTimer(uuid: string, reload: var): void {
        navigate("preset", uuid, reload === true);
    }

    function newTimer(): void {
        navigate("preset", "");
    }

    /// A timer chosen in the sidebar: it becomes the one in use (unless a phase runs) and its page opens.
    function chooseTimer(uuid: string): void {
        const top = pageStack.currentItem;
        if (top && typeof top.commitFields === "function") {
            top.commitFields();
        }
        if (top && top.modified === true && top.leaving !== true && typeof top.requestClose === "function") {
            top.requestClose(() => root.chooseTimer(uuid));
            return;
        }
        if (!TimerEngine.active) {
            PresetModel.currentUuid = uuid;
        }
        openTimer(uuid);
    }

    // Stop and Skip end the phase at once, unless that would cut short a work session that has
    // already run for a while: then they ask first. A quick mis-start stays one click away. The
    // question is asked here, over whatever page is shown, so the timer page, the S key, the search
    // and the menu bar ask the same without leaving the page (unsaved changes, the welcome tour).
    property int confirmAfterSeconds: 60
    readonly property bool workUnderWay: TimerEngine.active && TimerEngine.phase === TimerEngine.Phase.Work
        && TimerEngine.totalSeconds - TimerEngine.remainingSeconds >= confirmAfterSeconds
    readonly property bool endPhaseConfirming: endPhaseDialog.visible

    /// Stops the phase (or skips to the next one with `skipping`), asking first when work under
    /// way would be cut short.
    function requestEndPhase(skipping: bool): void {
        if (!TimerEngine.active) {
            return;
        }
        if (workUnderWay) {
            endPhaseDialog.skipping = skipping;
            endPhaseDialog.open();
        } else if (skipping) {
            TimerEngine.skip();
        } else {
            TimerEngine.stop();
        }
    }

    ConfirmDialog {
        id: endPhaseDialog
        property bool skipping: false
        // The question is about the work session it was asked for: once that ends some other
        // way (elapsed, tray, shortcut), confirming late must not stop or skip the next phase.
        Connections {
            target: TimerEngine
            function onPhaseChanged() {
                endPhaseDialog.close();
            }
            function onStateChanged() {
                if (!TimerEngine.active) {
                    endPhaseDialog.close();
                }
            }
        }
        title: skipping ? i18n("Skip This Work Session?") : i18n("Stop This Work Session?")
        text: skipping ? i18n("The rest of this work session is skipped and it is saved as interrupted.")
                       : i18n("The work session ends now and is saved as interrupted.")
        confirmText: skipping ? i18n("Skip") : i18n("Stop")
        confirmKind: "danger"
        cancelText: i18n("Keep Working")
        onConfirmed: {
            endPhaseDialog.close();
            if (skipping) {
                TimerEngine.skip();
            } else {
                TimerEngine.stop();
            }
        }
    }

    /// What the search (Ctrl+K) offers.
    function paletteCommands(): var {
        const list = [
            { text: i18n("Focus"), icon: "timer", hint: DesktopStyle.shortcutText("Ctrl+1"), run: () => root.navigate("timer") },
            { text: i18n("Statistics"), icon: "stats", hint: DesktopStyle.shortcutText("Ctrl+2"), run: () => root.navigate("stats") },
            { text: i18n("Settings"), icon: "settings", hint: DesktopStyle.shortcutText("Ctrl+,"), run: () => root.navigate("settings") },
            { text: i18n("About"), icon: "info", run: () => root.navigate("about") },
            { text: TimerEngine.paused ? i18n("Resume the timer") : TimerEngine.active ? i18n("Pause the timer") : i18n("Start the timer"),
              icon: TimerEngine.active && !TimerEngine.paused ? "pause" : "play", hint: i18nc("keyboard key", "Space"),
              run: () => TimerEngine.toggle() },
            { text: i18n("New timer"), icon: "plus", hint: DesktopStyle.shortcutText("Ctrl+N"), run: () => root.newTimer() }
        ];
        if (TimerEngine.active) {
            list.push({ text: i18n("Stop the timer"), icon: "stop", run: () => root.requestEndPhase(false) });
            list.push({ text: i18n("Skip to the next phase"), icon: "skip", run: () => root.requestEndPhase(true) });
        }
        for (let i = 0; i < PresetModel.count; ++i) {
            const uuid = PresetModel.uuidAt(i);
            const preset = PresetModel.get(uuid);
            list.push({ text: preset.name, timerIcon: preset.iconName || "sugo-timer",
                        hint: i18nc("minutes, short", "%1m", Math.round(preset.workSeconds / 60)),
                        run: () => root.chooseTimer(uuid) });
        }
        return list;
    }

    function openSearch(): void {
        commandPalette.commands = root.paletteCommands();
        commandPalette.open();
    }

    CommandPalette {
        id: commandPalette
    }

    // The smoke run "inventory": what every screen offers, for the platform parity test.
    FeatureInventory {
        id: featureInventory
        app: root
    }

    // On macOS the menu bar has these shortcuts: the Shortcut items below that would claim the
    // same keys are off there.
    Loader {
        active: DesktopStyle.nativeChrome
        source: active ? Qt.resolvedUrl("components/MacMenuBar.qml") : ""
        onLoaded: item.app = root
    }
    readonly property bool menuBarShortcuts: DesktopStyle.nativeChrome

    Shortcut {
        sequence: "Ctrl+K"
        enabled: !root.menuBarShortcuts && !root.isWelcomeActive && !root.dialogOpen
        onActivated: root.openSearch()
    }

    Shortcut {
        sequences: [StandardKey.New]
        enabled: !root.menuBarShortcuts && !root.isWelcomeActive && !root.dialogOpen
        onActivated: root.newTimer()
    }

    Shortcut {
        sequence: StandardKey.Quit
        enabled: !root.menuBarShortcuts
        onActivated: root.requestQuit()
    }

    // True while a control that reacts to Space itself (text inputs, buttons, combo boxes)
    // has keyboard focus: the key then belongs to it.
    readonly property bool spaceConsumedByFocus: activeFocusItem !== null
                                                 && (activeFocusItem.hasOwnProperty("cursorPosition")
                                                     || activeFocusItem.hasOwnProperty("checkable")
                                                     || activeFocusItem.hasOwnProperty("popup"))
    readonly property bool dialogOpen: quitDialog.visible || taskNoteDialog.visible || screenLockResumeDialog.visible
                                       || syncApprovalDialog.visible || syncRecoveryDialog.visible
                                       || endPhaseDialog.visible

    // Space starts, pauses or resumes the timer while the timer page is shown.
    Shortcut {
        sequence: "Space"
        enabled: root.currentDestination === "timer" && root.pageStack.depth === 1
                 && !root.spaceConsumedByFocus && !root.dialogOpen
        onActivated: TimerEngine.toggle()
    }

    // S skips to the next phase while the timer page is shown.
    Shortcut {
        sequence: "S"
        enabled: root.currentDestination === "timer" && TimerEngine.active
                 && !root.spaceConsumedByFocus && !root.dialogOpen
        onActivated: {
            const page = root.pageStack.currentItem;
            if (page && typeof page.requestSkip === "function") {
                page.requestSkip();
            }
        }
    }

    Repeater {
        model: ["timer", "stats", "settings", "about"]
        delegate: Item {
            id: pageShortcut
            required property string modelData
            required property int index
            Shortcut {
                sequence: "Ctrl+" + (pageShortcut.index + 1)
                enabled: !root.menuBarShortcuts && !root.isWelcomeActive && !root.dialogOpen
                onActivated: root.navigate(pageShortcut.modelData)
            }
        }
    }

    // KDE's Ctrl+Shift+, and the common Ctrl+,. Off on macOS, where StandardKey.Preferences is
    // Cmd+, as well (the same key twice would be ambiguous): the menu bar has it once there.
    Shortcut {
        sequences: [StandardKey.Preferences, "Ctrl+,"]
        enabled: !root.menuBarShortcuts && !root.isWelcomeActive && !root.dialogOpen
        onActivated: root.navigate("settings")
        // A platform theme whose Preferences key is Ctrl+, too lists it twice: still settings.
        onActivatedAmbiguously: root.navigate("settings")
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            id: sidebar
            Layout.fillHeight: true
            visible: root.wideScreen && !root.isWelcomeActive
            maximized: root.maximized
            onMinimizeRequested: root.showMinimized()
            onToggleMaximizeRequested: root.maximized ? root.showNormal() : root.showMaximized()
            onCloseRequested: root.close()
            current: root.currentDestination
            openTimer: root.openTimerUuid
            onNavigateRequested: name => root.navigate(name)
            onTimerRequested: uuid => root.chooseTimer(uuid)
            onNewTimerRequested: root.newTimer()
            onRestartRequested: root.requestRestart()
            onSearchRequested: root.openSearch()
            onMoveRequested: root.startSystemMove()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            TopBar {
                Layout.fillWidth: true
                minimal: root.isWelcomeActive
                maximized: root.maximized
                showLeftButtons: !sidebar.visible
                onMoveRequested: root.startSystemMove()
                onToggleMaximizeRequested: root.maximized ? root.showNormal() : root.showMaximized()
                onMinimizeRequested: root.showMinimized()
                onCloseRequested: root.close()
                page: stack.currentItem
                showStatus: root.currentDestination !== "timer"
                canGoBack: stack.depth > 1
                showMenuButton: !root.wideScreen
                onBackRequested: root.goBack()
                onMenuRequested: navDrawer.open()
                onStatusClicked: root.navigate("timer")
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                QQC2.StackView {
                    id: stack
                    anchors.fill: parent
                    clip: true
                    pushEnter: Transition {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationShort }
                    }
                    pushExit: Transition {}
                    popEnter: Transition {}
                    popExit: Transition {}
                    replaceEnter: Transition {}
                    replaceExit: Transition {}
                }

                // Shown on every page: nothing recorded now will survive a restart, or the last
                // changes are still waiting to be stored, or (until dismissed) the database found
                // at startup was damaged or not Sugo's and a new one was started. Laid over the
                // top of the page, so the page does not move when it comes or goes.
                Rectangle {
                    id: dataWarning
                    objectName: "dataWarning"
                    readonly property bool storageFailing: DataManager !== null && (DataManager.usingTemporaryDatabase === true || DataManager.saveFailing === true)
                    property bool movedAsideDismissed: false
                    readonly property bool movedAside: DataManager !== null && DataManager.movedAsideDatabasePath.length > 0 && !movedAsideDismissed
                    readonly property bool shown: storageFailing || movedAside
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.spaceMd
                    height: dataBanner.implicitHeight
                    z: 1
                    // The banner's fill is see-through: the page must not show under it.
                    color: Theme.background
                    radius: Theme.radius
                    opacity: shown ? 1 : 0
                    visible: opacity > 0
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationShort }
                    }

                    MessageSlot {
                        id: dataBanner
                        anchors.fill: parent
                        shown: dataWarning.shown
                        lines: 3
                        tone: "warning"
                        text: {
                            if (DataManager === null) {
                                return "";
                            }
                            if (DataManager.usingTemporaryDatabase === true) {
                                return i18n("Your data cannot be saved: the database could not be opened. Sugo is using a temporary database.");
                            }
                            if (DataManager.saveFailing === true) {
                                return i18n("Your latest sessions or timer changes could not be saved yet. Sugo keeps them and tries again; do not quit until this message is gone.");
                            }
                            return DataManager.movedAsideDatabaseWasCorrupt
                                ? i18n("Your database was damaged, so Sugo started a new one. The old file is kept as %1.", DataManager.movedAsideDatabasePath)
                                : i18n("The database file did not belong to Sugo, so Sugo started a new one. That file is kept as %1.", DataManager.movedAsideDatabasePath);
                        }
                        SButton {
                            kind: "ghost"
                            text: DesktopStyle.macos ? i18n("Show in Finder") : i18n("Open folder")
                            visible: dataWarning.movedAside && !dataWarning.storageFailing
                            onClicked: DataManager.openDatabaseFolder()
                        }
                        SButton {
                            kind: "ghost"
                            iconOnly: true
                            iconName: "close"
                            text: i18n("Dismiss")
                            visible: dataWarning.movedAside && !dataWarning.storageFailing
                            onClicked: dataWarning.movedAsideDismissed = true
                        }
                    }
                }
            }
        }
    }

    // The sidebar of narrow windows.
    QQC2.Drawer {
        id: navDrawer
        edge: Qt.LeftEdge
        width: Theme.sidebarWidth
        height: root.height
        interactive: !root.wideScreen && !root.isWelcomeActive
        // Turning `interactive` off leaves an open drawer open: close it when the window is
        // widened (the sidebar shows) or the welcome tour takes the window.
        onInteractiveChanged: if (!interactive) close()
        background: Item {}
        Sidebar {
            anchors.fill: parent
            // The window buttons stay in the top bar, which is the frame of a narrow window.
            showWindowButtons: false
            current: root.currentDestination
            openTimer: root.openTimerUuid
            onNavigateRequested: name => {
                navDrawer.close();
                root.navigate(name);
            }
            onTimerRequested: uuid => {
                navDrawer.close();
                root.chooseTimer(uuid);
            }
            onNewTimerRequested: {
                navDrawer.close();
                root.newTimer();
            }
            onRestartRequested: {
                navDrawer.close();
                root.requestRestart();
            }
            onSearchRequested: {
                navDrawer.close();
                commandPalette.commands = root.paletteCommands();
                commandPalette.open();
            }
        }
    }

    Toast {
        id: toast
    }

    // A frameless window still needs an outline and edges to resize it by (macOS's own window
    // has both).
    Rectangle {
        anchors.fill: parent
        visible: !root.maximized && !DesktopStyle.nativeChrome
        color: "transparent"
        border.width: Theme.hairline
        border.color: Theme.borderStrong
        z: 999
    }

    component ResizeEdge: Item {
        id: resizeEdge
        required property int edges
        property int cursor: Qt.ArrowCursor
        enabled: !root.maximized && !DesktopStyle.nativeChrome
        visible: enabled
        z: 1000
        HoverHandler {
            cursorShape: resizeEdge.cursor
        }
        DragHandler {
            target: null
            onActiveChanged: if (active) root.startSystemResize(resizeEdge.edges)
        }
    }
    ResizeEdge { edges: Qt.LeftEdge; cursor: Qt.SizeHorCursor; x: 0; y: Theme.spaceSm; width: Theme.spaceXs; height: root.height - Theme.spaceSm * 2 }
    ResizeEdge { edges: Qt.RightEdge; cursor: Qt.SizeHorCursor; x: root.width - width; y: Theme.spaceSm; width: Theme.spaceXs; height: root.height - Theme.spaceSm * 2 }
    ResizeEdge { edges: Qt.TopEdge; cursor: Qt.SizeVerCursor; x: Theme.spaceSm; y: 0; width: root.width - Theme.spaceSm * 2; height: Theme.spaceXs }
    ResizeEdge { edges: Qt.BottomEdge; cursor: Qt.SizeVerCursor; x: Theme.spaceSm; y: root.height - height; width: root.width - Theme.spaceSm * 2; height: Theme.spaceXs }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; cursor: Qt.SizeFDiagCursor; x: 0; y: 0; width: Theme.spaceSm; height: Theme.spaceSm }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; cursor: Qt.SizeBDiagCursor; x: root.width - width; y: 0; width: Theme.spaceSm; height: Theme.spaceSm }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; cursor: Qt.SizeBDiagCursor; x: 0; y: root.height - height; width: Theme.spaceSm; height: Theme.spaceSm }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; cursor: Qt.SizeFDiagCursor; x: root.width - width; y: root.height - height; width: Theme.spaceSm; height: Theme.spaceSm }

    Component.onCompleted: {
        root.visible = !AppSettings.startHidden;
        if (!AppSettings.firstRunCompleted && !appStatsSmokeTest && (!appSmokePage || appSmokePage.length === 0)) {
            currentDestination = "welcome";
            pushPage(destinations.welcome);
        } else {
            if (typeof appSmokePage !== "undefined" && appSmokePage.length > 0) {
                // The inventory starts on the focus page and goes through the others itself.
                currentDestination = appSmokePage === "inventory" ? "timer" : appSmokePage;
            } else if (appStatsSmokeTest) {
                currentDestination = "stats";
            } else if (startPageName.length > 0 && (destinations[startPageName] || startPageName === "presets")) {
                currentDestination = startPageName;
            }
            // "presets" (smoke test, development): the page of the timer in use.
            if (currentDestination === "presets") {
                currentDestination = "preset";
                openTimerUuid = PresetModel.currentUuid;
            }
            pushPage(destinations[currentDestination], currentDestination === "preset" ? { uuid: openTimerUuid } : {});
        }
        // The timers and history of Sugo 1.0.0, which 1.0.1 had moved aside, were taken back.
        if (DataManager !== null && DataManager.sugo100RecoveryReplacedPath.length > 0) {
            showPassiveNotification(i18n("Your timers and history from Sugo 1.0.0 are back. The empty database that had replaced them is kept as %1.",
                                         DataManager.sugo100RecoveryReplacedPath), "long");
        }
        // "settings/<category>", "welcome/<step>": that part of the page.
        const startPart = appStartPage.split("/")[1] ?? "";
        if (startPart.length > 0 && currentDestination === startPageName) {
            const page = pageStack.currentItem;
            if (startPageName === "settings") {
                page.category = startPart;
            } else if (startPageName === "welcome") {
                page.current = Number(startPart);
            }
        }
    }
    /// SUGO_START_PAGE without its part ("settings" for "settings/focus").
    readonly property string startPageName: appStartPage.split("/")[0]

    ConfirmDialog {
        id: taskNoteDialog
        property string presetName: ""
        property real sessionId: -1
        title: i18n("Work Session Finished")
        text: presetName.length > 0
              ? i18n("What did you accomplish during “%1”?", presetName)
              : i18n("What did you accomplish during this session?")
        confirmText: i18n("Save Note")
        confirmEnabled: taskNoteField.text.trim().length > 0
        cancelText: i18n("Skip")
        onConfirmed: root.saveTaskNote(taskNoteField.text)

        STextField {
            id: taskNoteField
            Layout.fillWidth: true
            placeholderText: i18n("e.g. Refactored auth module, wrote tests…")
            onAccepted: root.saveTaskNote(text)
        }

        // The latest notes, to use one again.
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spaceSm
            visible: recentRepeater.count > 0

            Repeater {
                id: recentRepeater
                model: []

                QQC2.AbstractButton {
                    id: chip
                    required property string modelData
                    text: modelData
                    hoverEnabled: true
                    focusPolicy: Qt.TabFocus
                    implicitHeight: Theme.controlHeight - Theme.spaceSm
                    implicitWidth: Math.min(chipLabel.implicitWidth + leftPadding + rightPadding, taskNoteField.width)
                    leftPadding: Theme.spaceMd + Theme.hairline * 2
                    rightPadding: leftPadding
                    Accessible.name: i18nc("@action:button use a recent note", "Use “%1”", modelData)
                    onClicked: taskNoteField.text = modelData

                    background: Rectangle {
                        radius: height / 2
                        color: chip.down ? Theme.pressed : chip.hovered ? Theme.hover : "transparent"
                        border.width: Theme.hairline
                        border.color: chip.visualFocus ? Theme.focusRing : chip.hovered ? Theme.borderStrong : Theme.border
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                    }
                    contentItem: Text {
                        id: chipLabel
                        text: chip.text
                        color: chip.hovered ? Theme.foreground : Theme.muted
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        onOpened: {
            taskNoteField.text = "";
            recentRepeater.model = DataManager.recentTaskNotes(5);
            taskNoteField.forceActiveFocus();
        }
        // A session that ended while this one's note was asked for.
        onClosed: Qt.callLater(root.showPendingTaskNote)
    }

    // The resume prompt waits while the window is hidden (e.g. in the tray) and appears once the
    // window is shown again, if the timer is still paused because of the lock.
    property bool screenLockPromptPending: false

    function showPendingScreenLockPrompt(): void {
        if (!screenLockPromptPending || !root.visible || !root.active) {
            return;
        }
        // Not over a task note: this prompt waits until that dialog closes.
        if (taskNoteDialog.visible) {
            return;
        }
        screenLockPromptPending = false;
        if (TimerEngine.paused && (ScreenLockWatcher.pausedByScreenLock || ScreenLockWatcher.pausedBySleep)) {
            screenLockResumeDialog.open();
        }
    }

    // The task note first: the resume prompt waits for it to close.
    onActiveChanged: {
        showPendingTaskNote();
        showPendingScreenLockPrompt();
    }
    onVisibleChanged: {
        showPendingTaskNote();
        showPendingScreenLockPrompt();
    }
    Connections {
        target: taskNoteDialog
        function onClosed() {
            Qt.callLater(root.showPendingScreenLockPrompt);
        }
    }

    Connections {
        target: ScreenLockWatcher
        function onScreenUnlockedAfterPause() {
            if (!TimerEngine.paused) {
                return;
            }
            root.screenLockPromptPending = true;
            root.showPendingScreenLockPrompt();
        }
        function onWokeUpAfterPause() {
            onScreenUnlockedAfterPause();
        }
        function onPausedByScreenLockChanged() {
            if (!ScreenLockWatcher.pausedByScreenLock && !ScreenLockWatcher.pausedBySleep) {
                root.screenLockPromptPending = false;
            }
        }
        function onPausedBySleepChanged() {
            onPausedByScreenLockChanged();
        }
    }

    // Sugo Sync: what happened, once, in a toast (the Sync tab shows how things stand).
    Connections {
        target: SyncController
        function onMessage(text) {
            root.showPassiveNotification(text, "long");
        }
        // Signed out here, but the server was not told: the device keeps its place on the plan.
        function onSignedOut(remoteDone) {
            if (!remoteDone) {
                root.showPassiveNotification(i18n("Signed out on this computer, but Sugo Sync could not be reached. Disconnect this device on sugo.stream to free its place."), "long");
            }
        }
        function onApprovalRequested() {
            root.showSyncApproval();
        }
        function onRecoveryCodeToConfirm(code) {
            syncRecoveryDialog.show(code);
        }
    }

    // A new recovery code (first device, or a new one asked for): shown once here, kept by the
    // user, and checked by typing one of its groups back.
    ConfirmDialog {
        id: syncRecoveryDialog
        // Closed by its buttons or Escape only: a click beside it is not "Later".
        closePolicy: QQC2.Popup.CloseOnEscape
        property string code
        property int group: 0
        readonly property int groups: code.length > 0 ? code.split("-").length : 0

        function show(newCode: string): void {
            code = newCode;
            group = Math.floor(Math.random() * Math.max(1, groups));
            recoveryGroupField.text = "";
            open();
        }

        title: i18n("Keep your recovery code")
        text: i18n("With this code a new device opens your synced timers when none of your other devices is at hand. If you lose all your devices and this code, the synced timers cannot be opened again: write it down or keep it in a password manager.")
        confirmText: i18n("Done")
        cancelText: i18n("Later")
        confirmEnabled: SyncController !== null && SyncController.confirmRecoveryGroup(group, recoveryGroupField.text)
        onConfirmed: {
            syncRecoveryDialog.close();
            root.showPassiveNotification(i18n("Recovery code checked. Keep it somewhere safe."));
        }
        onOpened: recoveryGroupField.forceActiveFocus()
        onClosed: {
            code = "";
            recoveryGroupField.text = "";
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Math.max(recoveryCodeText.implicitHeight, recoveryCopyButton.implicitHeight) + Theme.spaceMd * 2
            radius: Theme.radius
            color: Theme.background
            border.width: Theme.hairline
            border.color: Theme.border
            TextEdit {
                id: recoveryCodeText
                anchors.left: parent.left
                anchors.right: recoveryCopyButton.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.spaceLg
                anchors.rightMargin: Theme.spaceSm
                readOnly: true
                selectByMouse: true
                text: syncRecoveryDialog.code
                color: Theme.foreground
                selectionColor: Theme.alpha(Theme.brand, 0.35)
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontTitle + 1
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                Accessible.name: i18n("Recovery code")
            }
            SButton {
                id: recoveryCopyButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: Theme.spaceSm
                kind: "ghost"
                iconOnly: true
                iconName: "copy"
                text: i18n("Copy recovery code")
                onClicked: {
                    recoveryCodeText.selectAll();
                    recoveryCodeText.copy();
                    recoveryCodeText.deselect();
                    root.showPassiveNotification(i18n("Recovery code copied to the clipboard."));
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: i18n("To check that you have kept it, type group %1 of %2.",
                       syncRecoveryDialog.group + 1, syncRecoveryDialog.groups)
            color: Theme.muted
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }
        STextField {
            id: recoveryGroupField
            Layout.fillWidth: true
            font.family: Theme.monoFamily
            maximumLength: 8
            placeholderText: i18n("Group %1", syncRecoveryDialog.group + 1)
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
            Accessible.name: i18n("Group %1 of the recovery code", syncRecoveryDialog.group + 1)
            onAccepted: {
                if (syncRecoveryDialog.confirmEnabled) {
                    syncRecoveryDialog.confirmed();
                }
            }
        }
    }

    /// The first device waiting for approval, in the dialog (unless it is already open: the next
    /// one follows when it closes).
    function showSyncApproval(): void {
        const device = SyncController ? SyncController.pendingApproval : null;
        if (syncApprovalDialog.visible || !device || !device.id) {
            return;
        }
        syncApprovalDialog.device = device;
        syncApprovalDialog.approved = false;
        syncApprovalDialog.open();
    }

    // A new device asks for the timers' key. It gets it only from here, and only if the user sees
    // the same verification code on both screens (a key sent to anyone else is refused there).
    ConfirmDialog {
        id: syncApprovalDialog
        // Closed by its buttons or Escape only: a click beside it is not "Not Now".
        closePolicy: QQC2.Popup.CloseOnEscape
        property var device: ({})
        property bool approved: false
        title: i18n("Approve “%1”?", device.name || "")
        text: i18n("A new device wants to sync your timers. Approve it only if it is yours and shows the same code as here.")
        confirmText: i18n("Approve")
        cancelText: i18n("Not Now")
        onConfirmed: {
            approved = true;
            syncApprovalDialog.close();
        }
        // Approve, Not Now or Escape: answered once it has closed, then the next device, if any.
        onClosed: {
            const id = device.id || "";
            device = ({});
            if (SyncController && id.length > 0) {
                if (approved) {
                    SyncController.approveDevice(id);
                } else {
                    SyncController.ignoreDevice(id);
                }
            }
            Qt.callLater(root.showSyncApproval);
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: approvalCode.implicitHeight + approvalCaption.implicitHeight + Theme.spaceXs + Theme.spaceLg * 2
            radius: Theme.radius
            color: Theme.background
            border.width: Theme.hairline
            border.color: Theme.border
            Column {
                anchors.centerIn: parent
                spacing: Theme.spaceXs
                Text {
                    id: approvalCaption
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: i18n("Verification code")
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                }
                Text {
                    id: approvalCode
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: syncApprovalDialog.device.verificationCode || ""
                    color: Theme.foreground
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontDisplay
                    font.weight: Font.DemiBold
                    font.letterSpacing: Theme.letterSpacingCaps * 2
                    Accessible.role: Accessible.StaticText
                    Accessible.name: i18n("Verification code") + " " + text
                }
            }
        }
    }

    ConfirmDialog {
        id: screenLockResumeDialog
        title: ScreenLockWatcher.pausedBySleep ? i18n("Welcome Back") : i18n("Screen Unlocked")
        text: ScreenLockWatcher.pausedBySleep
              ? i18n("The timer was paused while the computer was asleep. Would you like to resume?")
              : i18n("The timer was paused while your screen was locked. Would you like to resume?")
        confirmText: i18n("Resume")
        cancelText: i18n("Keep Paused")
        // Resumed or stopped some other way (tray, shortcut, KRunner): the question is moot.
        Connections {
            target: TimerEngine
            function onStateChanged() {
                if (!TimerEngine.paused) {
                    screenLockResumeDialog.close();
                }
            }
        }
        onConfirmed: {
            TimerEngine.resume();
            screenLockResumeDialog.close();
        }
        // Resume, Keep Paused or Escape: the notice is answered either way.
        onClosed: ScreenLockWatcher.clearLockPauseNotice()
    }
}
