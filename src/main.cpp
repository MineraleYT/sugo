// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QDebug>
#include <QEvent>
#include <QPointer>
#include <QUrl>
#include <QQmlContext>
#include <QQmlExtensionPlugin>

#include <QLocale>

#include <functional>
#include <memory>

#include "Localization.h"

#include "AppSettings.h"
#include "DataManager.h"
#include "Database.h"
#include "DBusTimerService.h"
#include "KRunnerService.h"
#include "InhibitorFactory.h"
#include "NotifierFactory.h"
#include "FocusSoundController.h"
#include "PhaseNotifier.h"
#include "PresetModel.h"
#include "PresetPersistence.h"
#include "PresetRepository.h"
#include "SessionRecorder.h"
#include "SessionRepository.h"
#include "SilenceController.h"
#ifdef Q_OS_MACOS
#include "LocalSingleInstance.h"
#include "MacApp.h"
#else
#include "SingleInstance.h"
#endif
#include "StatsModel.h"
#include "TimerBinding.h"
#include "TimerEngine.h"
#include "TrayController.h"
#include "UnixSignals.h"
#include "PowerInhibitor.h"
#include "ScreenLockWatcher.h"
#include "Diagnostics.h"
#include "CalendarSync.h"
#include "ThemeIcons.h"
#include "UpdateChecker.h"

// The QML module is a static library (sugo_core); its plugin registers the types.
Q_IMPORT_QML_PLUGIN(SugoQmlPlugin)

#ifdef Q_OS_MACOS
namespace
{
/**
 * What macOS tells the application as a whole, seen on the application object.
 *
 * - A click on the Dock icon while the window is hidden shows it. Cocoa reports it as
 *   applicationShouldHandleReopen:, which Qt turns into an ApplicationActivate event (forced
 *   even when the application is already active). Activation by other means (Cmd+Tab, a click
 *   on a notification) also shows a hidden window. After a `--background` launch the
 *   activation macOS may give a newly launched application is ignored: the filter is armed a
 *   moment after the event loop starts.
 * - Cmd+Q, Quit in the Dock menu, logging out or shutting down: Cocoa asks
 *   applicationShouldTerminate:, Qt sends QEvent::Quit to the application, which closes every
 *   window and quits only if they all accept (otherwise macOS cancels the logout). The window
 *   refuses closing (it hides, or asks first) unless its `platformQuitting` property is set:
 *   set it here, before Qt closes it. QCoreApplication::quit() takes the same way on macOS,
 *   so this also covers the menu bar icon's Quit. The running phase is then saved as
 *   interrupted by the aboutToQuit -> TimerEngine::stop connection.
 */
class MacApplicationEvents : public QObject
{
public:
    MacApplicationEvents(QQuickWindow *window, std::function<void()> showWindow, bool armed, QObject *parent)
        : QObject(parent)
        , m_window(window)
        , m_showWindow(std::move(showWindow))
        , m_armed(armed)
    {
    }

    void arm() { m_armed = true; }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == QCoreApplication::instance() && m_window) {
            if (event->type() == QEvent::ApplicationActivate) {
                if (m_armed && !m_window->isVisible()) {
                    m_showWindow();
                }
            } else if (event->type() == QEvent::Quit) {
                m_window->setProperty("platformQuitting", true);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QQuickWindow> m_window;
    std::function<void()> m_showWindow;
    bool m_armed;
};
} // namespace
#endif

int main(int argc, char *argv[])
{
    Diagnostics::installLogHandler();

    // The same look everywhere: our own components, and the Basic style (coloured from the
    // theme) for the standard controls.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QApplication app(argc, argv);

    // The interface is in English: so are day and month names, in the user's regional order.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::system().territory()));

    QCoreApplication::setOrganizationName(QStringLiteral("mineraleyt"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.mineraleyt"));
    QCoreApplication::setApplicationName(QStringLiteral("sugo"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.mineraleyt.sugo"));
    // Our own file, not the theme's: on Wayland Qt sends a theme icon's name, which the compositor
    // can't resolve when nothing is installed (an AppImage run directly).
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/io.github.mineraleyt.sugo.svg")));

    QGuiApplication::setApplicationDisplayName(i18n("Sugo"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    // What the About page and the sidebar show.
    const QVariantMap about{
        {QStringLiteral("displayName"), i18n("Sugo")},
        {QStringLiteral("version"), QStringLiteral(PROJECT_VERSION)},
        {QStringLiteral("shortDescription"), i18n("A Pomodoro timer with time statistics")},
        {QStringLiteral("copyrightStatement"), i18n("© 2026 Sugo contributors")},
    };

    QCommandLineParser parser;
    parser.setApplicationDescription(about.value(QStringLiteral("shortDescription")).toString());
    parser.addHelpOption();
    parser.addVersionOption();
#ifdef Q_OS_MACOS
    const QCommandLineOption backgroundOption(QStringLiteral("background"),
                                              i18n("Start hidden in the menu bar (used by the login item)"));
#else
    const QCommandLineOption backgroundOption(QStringLiteral("background"),
                                              i18n("Start hidden in the system tray (used by the login entry)"));
#endif
    parser.addOption(backgroundOption);
    parser.process(app);
    const bool background = parser.isSet(backgroundOption);

    // Closing the window must not end the program: the tray (or the close handler in QML) decides.
    app.setQuitOnLastWindowClosed(false);

    // One instance only. A second launch (login entry plus menu, say) shows the first one's
    // window and exits, so two programs never share the database. A second `--background`
    // launch (the login entry while already running) exits without popping the window up.
#ifdef Q_OS_MACOS
    // No session bus here: a lock file and a local socket, private to the user.
    LocalSingleInstance singleInstance(LocalSingleInstance::defaultName(QStringLiteral("sugo")));
    using AppSingleInstance = LocalSingleInstance;
#else
    SingleInstance singleInstance(QDBusConnection::sessionBus(), QStringLiteral("io.github.mineraleyt.sugo"));
    using AppSingleInstance = SingleInstance;
#endif
    if (!singleInstance.acquire(!background)) {
        return EXIT_SUCCESS;
    }

    // Lifetimes: objects are destroyed in reverse order of declaration. The database, the
    // repositories and the notification inhibitor are declared before the QML engine (whose
    // singletons, e.g. SilenceController and StatsModel, use them), and the helpers parented to
    // `services` after it, so each object goes away before what it points to.

    // Storage. If the file cannot be used, run with a throw-away in-memory database so the
    // timer still works; nothing is saved in that case (the UI shows a warning).
    auto database = std::make_unique<Database>(Database::defaultPath());
    if (!database->open()) {
        qWarning() << "Cannot open the database" << Database::defaultPath() << ":" << database->lastError();
        qWarning() << "Using a temporary in-memory database: timers and statistics will NOT be saved";
        database = std::make_unique<Database>(QStringLiteral(":memory:"));
        if (!database->open()) {
            qCritical() << "Cannot open even an in-memory database:" << database->lastError();
            return EXIT_FAILURE;
        }
    }
    if (!database->movedAsidePath().isEmpty()) {
        if (database->movedAsideWasCorrupt()) {
            qWarning() << "The existing database was damaged; it was kept as" << database->movedAsidePath()
                       << "and a new one was started";
        } else {
            qWarning() << "The existing database was not created by Sugo; it was kept as"
                       << database->movedAsidePath() << "and a new one was started";
        }
    }
    PresetRepository presetRepository(database->connectionName());
    SessionRepository sessionRepository(database->connectionName());
    const auto inhibitor = createNotificationInhibitor();

    QQmlApplicationEngine engine;
    QObject services; // parent of the C++ helpers below; destroyed before the engine
    // i18n() and friends for the QML files.
    engine.rootContext()->setContextObject(new LocalizedContext(&engine));
    // Icons of the desktop's theme, as image://icon/<name> (the engine owns the provider).
    engine.addImageProvider(QStringLiteral("icon"), new ThemeIconProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("appAboutData"), about);
    const QString smokePage = qEnvironmentVariable("SUGO_SMOKE_PAGE");
    const bool statsSmokeTest = qEnvironmentVariable("SUGO_STATS_SMOKE_TEST") == QStringLiteral("1");
    engine.rootContext()->setContextProperty(QStringLiteral("appStatsSmokeTest"), statsSmokeTest);
    engine.rootContext()->setContextProperty(QStringLiteral("appSmokePage"), smokePage);
    // For development: open on another page ("presets" for the timer in use, "stats", "settings", "about", "welcome").
    engine.rootContext()->setContextProperty(QStringLiteral("appStartPage"), qEnvironmentVariable("SUGO_START_PAGE"));
    const bool smokeTest = statsSmokeTest || !smokePage.isEmpty();
    int smokeWarnings = 0;
    if (smokeTest) {
        // Any QML warning (a failed binding, an undefined property...) fails the smoke test.
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [&smokeWarnings](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings) {
                qWarning().noquote() << warning.toString();
            }
            smokeWarnings += int(warnings.size());
        });
    }

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);

    // Services that must outlive any page. They are wired before the UI loads so the
    // first frame already shows the stored timers. Order matters: load presets first,
    // then configure the engine from them, then start recording.
    auto *timer = engine.singletonInstance<TimerEngine *>("io.github.mineraleyt.sugo", "TimerEngine");
    auto *presets = engine.singletonInstance<PresetModel *>("io.github.mineraleyt.sugo", "PresetModel");
    auto *settings = engine.singletonInstance<AppSettings *>("io.github.mineraleyt.sugo", "AppSettings");
    if (!timer || !presets || !settings) {
        qCritical() << "Cannot create the QML singletons";
        return EXIT_FAILURE;
    }
    settings->setLaunchedInBackground(background);
    settings->applyColorScheme();
    auto *presetPersistence = new PresetPersistence(presets, &presetRepository, &services);
    new TimerBinding(timer, presets, &services);
    auto *silence = engine.singletonInstance<SilenceController *>("io.github.mineraleyt.sugo", "SilenceController");
    if (!silence) {
        qCritical() << "Cannot create the QML singletons";
        return EXIT_FAILURE;
    }
    silence->attach(timer, presets, inhibitor.get());
    // Global shortcuts, scripts and KRunner: only while the settings allow other programs to control the timer.
    const auto externalControlAllowed = [settings]() { return settings->allowExternalControl(); };
    auto *timerDBusObject = new TimerDBusObject(timer);
    auto *dbusTimer = new DBusTimerService(timer, presets, timerDBusObject);
    dbusTimer->setControlAllowed(externalControlAllowed);
#ifdef Q_OS_LINUX
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Timer"), timerDBusObject, QDBusConnection::ExportAdaptors);
#endif
    auto *krunner = new KRunnerService(timer, presets, &services);
    krunner->setControlAllowed(externalControlAllowed);
#ifdef Q_OS_LINUX
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/krunner"), krunner, QDBusConnection::ExportAllSlots);
#endif
    auto *recorder = new SessionRecorder(timer, presets, &sessionRepository, &services);

    auto *stats = engine.singletonInstance<StatsModel *>("io.github.mineraleyt.sugo", "StatsModel");
    if (!stats) {
        qCritical() << "Cannot create the StatsModel singleton";
        return EXIT_FAILURE;
    }
    stats->attach(&sessionRepository, recorder);
    auto *dataManager = new DataManager(database.get(), &sessionRepository, &presetRepository, presets, stats, &services);
    dataManager->setSessionRecorder(recorder);
    dataManager->setPresetPersistence(presetPersistence);
    engine.rootContext()->setContextProperty(QStringLiteral("DataManager"), dataManager);

    // The AppImage and the macOS app look for updates by themselves (when the user lets them) and install them.
    auto *updates = engine.singletonInstance<UpdateChecker *>("io.github.mineraleyt.sugo", "UpdateChecker");
    if (updates && !smokeTest) {
        updates->attach(settings);
    }

    auto *powerInhibitor = new PowerInhibitor(QDBusConnection::sessionBus(), &services);
    powerInhibitor->attach(timer, settings);
    auto *screenLockWatcher = engine.singletonInstance<ScreenLockWatcher *>("io.github.mineraleyt.sugo", "ScreenLockWatcher");
    if (screenLockWatcher) {
        screenLockWatcher->attach(timer, settings);
    }
    auto *diagnostics = engine.singletonInstance<Diagnostics *>("io.github.mineraleyt.sugo", "Diagnostics");
    if (diagnostics) {
        diagnostics->attach(settings, dataManager);
    }
    auto *calendarSync = engine.singletonInstance<CalendarSync *>("io.github.mineraleyt.sugo", "CalendarSync");
    if (calendarSync) {
        QObject::connect(recorder, &SessionRecorder::workSessionRecorded, calendarSync, &CalendarSync::enqueueWorkSession);
        QObject::connect(dataManager, &DataManager::sessionNoteChanged, calendarSync, &CalendarSync::onNoteChanged);
        // The calendar login and server never appear in a diagnostics report.
        const auto updateRedactions = [calendarSync]() {
            Diagnostics::setAccountRedactions({calendarSync->username(), QUrl(calendarSync->serverUrl()).host(),
                                               QUrl(calendarSync->calendarUrl()).host()});
        };
        updateRedactions();
        for (const auto signal : {&CalendarSync::usernameChanged, &CalendarSync::serverUrlChanged, &CalendarSync::calendarUrlChanged}) {
            QObject::connect(calendarSync, signal, calendarSync, updateRedactions);
        }
    }
    auto *focusSound = new FocusSoundController(&services);
    focusSound->attach(timer, settings);
    engine.rootContext()->setContextProperty(QStringLiteral("FocusSoundController"), focusSound);
    stats->setProtectWeekendStreak(settings->protectWeekendStreak());
    QObject::connect(settings, &AppSettings::protectWeekendStreakChanged, stats, [settings, stats]() {
        stats->setProtectWeekendStreak(settings->protectWeekendStreak());
    });
    auto *tray = new TrayController(timer, presets, settings, &services);
    // Desktop notifications; where there is no notification server, messages from the tray icon.
    auto *notifications = createNotifier([tray]() { return tray->icon(); }, &services).release();
    auto *notifier = new PhaseNotifier(timer, presets, inhibitor.get(), notifications, settings, &services);
    notifier->setLastWorkSessionIdProvider([recorder]() { return recorder->lastWorkSessionId(); });
    // Direct inline reply from the notification.
    QObject::connect(notifier, &PhaseNotifier::taskNoteSubmitted, dataManager, [dataManager](qint64 sessionId, const QString &note) {
        if (sessionId > 0) {
            dataManager->updateSessionNote(sessionId, note);
        }
    });

    // Quitting mid-phase ends it as an interrupted session instead of losing it.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, timer, &TimerEngine::stop);
    // ... and always hands the notifications back before the process ends.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, silence, &SilenceController::shutdown);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, powerInhibitor, &PowerInhibitor::shutdown);
    installQuitSignalHandlers(&app);

    engine.loadFromModule("io.github.mineraleyt.sugo", "Main");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    // For development and for the screenshots of the store pages: save a picture of the window
    // (SUGO_SCREENSHOT=file.png, after SUGO_SCREENSHOT_DELAY ms, 1500 by default) and quit.
    if (window && qEnvironmentVariableIsSet("SUGO_SCREENSHOT")) {
        const int delay = qEnvironmentVariableIsSet("SUGO_SCREENSHOT_DELAY") ? qEnvironmentVariableIntValue("SUGO_SCREENSHOT_DELAY") : 1500;
        QTimer::singleShot(delay, window, [window]() {
            const QString file = qEnvironmentVariable("SUGO_SCREENSHOT");
            if (!window->grabWindow().save(file)) {
                qWarning() << "Cannot save the screenshot to" << file;
            }
            QCoreApplication::quit();
        });
    }
    if (window) {
        tray->setWindow(window);
        auto activateWindow = [window](const QString &activationToken = QString()) {
#ifdef Q_OS_LINUX
            // On Wayland the compositor only gives focus with the token of the launch that asked
            // for it; Qt's Wayland plugin takes it from this variable on requestActivate().
            if (!activationToken.isEmpty()) {
                qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
            }
#else
            Q_UNUSED(activationToken)
#endif
#ifdef Q_OS_MACOS
            // requestActivate() alone would leave the window behind the active application.
            macActivateApp();
#endif
            window->show();
            if (window->windowStates() & Qt::WindowMinimized) {
                window->showNormal();
            }
            window->raise();
            window->requestActivate();
        };

        // A click on the tray icon while the window is hidden.
        QObject::connect(tray, &TrayController::showWindowRequested, window, [activateWindow]() { activateWindow(); });

        // A second launch asks this instance to come to the front.
        QObject::connect(&singleInstance, &AppSingleInstance::activateRequested, window, activateWindow);

#ifdef Q_OS_MACOS
        // The Dock icon, and quitting from the system (see MacApplicationEvents).
        auto *macEvents = new MacApplicationEvents(window, [activateWindow]() { activateWindow(); }, !background, &services);
        app.installEventFilter(macEvents);
        if (background) {
            QTimer::singleShot(1000, macEvents, [macEvents]() { macEvents->arm(); });
        }
#endif

        // KRunner runner action to open window
        QObject::connect(krunner, &KRunnerService::openRequested, window, [activateWindow]() { activateWindow(); });

        // The tray menu's Statistics / Settings entries: show the window on that page.
        QObject::connect(tray, &TrayController::openPageRequested, window, [window, activateWindow](const QString &page) {
            activateWindow();
            QMetaObject::invokeMethod(window, "openPage", Q_ARG(QString, page));
        });

        // A click on a notification with nothing more specific to do.
        QObject::connect(notifier, &PhaseNotifier::showWindowRequested, window, activateWindow);

        // When a task note prompt is requested (e.g. from end-of-work notification action)
        QObject::connect(notifier, &PhaseNotifier::taskNotePromptRequested, window,
                         [window, activateWindow](const QString &presetName, qint64 sessionId, const QString &activationToken) {
                             activateWindow(activationToken);
                             QMetaObject::invokeMethod(window, "openTaskNotePrompt", Q_ARG(QVariant, presetName),
                                                       Q_ARG(QVariant, sessionId));
                         });
    }

    const int status = app.exec();

    // Teardown. Nothing outside may reach what is about to go: first the D-Bus objects (the timer
    // is an engine singleton, the runner a child of `services`), then the context properties
    // that point into `services`, which is destroyed before the engine and its window.
#ifdef Q_OS_LINUX
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/krunner"));
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/Timer"));
#endif
    engine.rootContext()->setContextProperty(QStringLiteral("DataManager"), QVariant::fromValue<QObject *>(nullptr));
    engine.rootContext()->setContextProperty(QStringLiteral("FocusSoundController"), QVariant::fromValue<QObject *>(nullptr));
    if (smokeTest && status == EXIT_SUCCESS && smokeWarnings > 0) {
        qCritical() << "The smoke test produced" << smokeWarnings << "QML warning(s)";
        return 30;
    }
    // "Restart to update": the updated AppImage, or the new Sugo.app put in place now, starts once
    // this process has ended (on macOS a plain quit also puts it in place).
    if (updates) {
        updates->launchUpdated();
    }
    return status;
}
