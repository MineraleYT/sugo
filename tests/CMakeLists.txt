# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
include(ECMAddTests)

# Each test links the program's code (sugo_core, see src/CMakeLists.txt), Qt Test and the Qt modules
# it uses itself; a test built on every system names Qt6::DBus as ${SUGO_DBUS}, empty on macOS.
# The modules decide what QTEST_MAIN creates: a QApplication with Qt6::Widgets, a
# QGuiApplication with Qt6::Gui (both need QT_QPA_PLATFORM=offscreen on CI), else a QCoreApplication.
function(sugo_add_test name)
    ecm_add_test(${name}.cpp TEST_NAME ${name} LINK_LIBRARIES Qt6::Test sugo_core ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

sugo_add_test(tst_timerengine Qt6::Core Qt6::Qml)
sugo_add_test(tst_focussoundcontroller Qt6::Core Qt6::Gui Qt6::Qml ${SUGO_DBUS} Qt6::Multimedia)
sugo_add_test(tst_datamanager Qt6::Core Qt6::Qml Qt6::Sql)
sugo_add_test(tst_presetmodel Qt6::Core Qt6::Qml)
sugo_add_test(tst_timerbinding Qt6::Core Qt6::Qml)
sugo_add_test(tst_database Qt6::Core Qt6::Sql)
sugo_add_test(tst_presetrepository Qt6::Core Qt6::Qml Qt6::Sql)
sugo_add_test(tst_sessions Qt6::Core Qt6::Qml Qt6::Sql)
sugo_add_test(tst_silence Qt6::Core Qt6::Qml)
sugo_add_test(tst_autostart Qt6::Core Qt6::Gui ${SUGO_DBUS})
sugo_add_test(tst_appsettings Qt6::Core Qt6::Gui Qt6::Qml ${SUGO_DBUS})
sugo_add_test(tst_traypresenter Qt6::Core Qt6::Qml)
sugo_add_test(tst_statsaggregator Qt6::Core Qt6::Sql)
sugo_add_test(tst_statsmodel Qt6::Core Qt6::Qml Qt6::Sql)
sugo_add_test(tst_updatechecker Qt6::Core Qt6::Qml Qt6::Network)
sugo_add_test(tst_screenlockwatcher Qt6::Core Qt6::Gui Qt6::Qml ${SUGO_DBUS})
sugo_add_test(tst_diagnostics Qt6::Core Qt6::Gui Qt6::Qml Qt6::Sql ${SUGO_DBUS})
# The test builds a QGuiApplication (clipboard access): run it without a display, e.g. on CI.
set_tests_properties(tst_diagnostics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

sugo_add_test(tst_calendarsync Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network ${SUGO_DBUS})
set_tests_properties(tst_calendarsync PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

sugo_add_test(tst_phasenotifier Qt6::Core Qt6::Gui Qt6::Qml ${SUGO_DBUS})
# A GUI application (AppSettings); no display on CI.
set_tests_properties(tst_phasenotifier PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

sugo_add_test(tst_traynotifier Qt6::Core Qt6::Gui Qt6::Widgets)
set_tests_properties(tst_traynotifier PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

sugo_add_test(tst_traycontroller Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml ${SUGO_DBUS})
set_tests_properties(tst_traycontroller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

sugo_add_test(tst_desktopstyle Qt6::Core Qt6::Gui Qt6::Qml)
# A QGuiApplication (the system's fixed-width font); no display on CI.
set_tests_properties(tst_desktopstyle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
sugo_add_test(tst_themeicons Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
set_tests_properties(tst_themeicons PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

sugo_add_test(tst_localization Qt6::Core Qt6::Qml)

# The installed runner definition carries the project version (see the top-level CMakeLists.txt).
add_test(NAME tst_runner_version
    COMMAND grep -qx "X-KDE-PluginInfo-Version=${PROJECT_VERSION}" ${CMAKE_BINARY_DIR}/io.github.mineraleyt.sugo.runner.desktop
)

# Linux only: the freedesktop services over D-Bus (notifications, inhibition, KRunner, Secret Service,
# the single-instance bus name and the /Timer service), and the shutdown test (D-Bus, GNU tools).
if(NOT APPLE)
    # macOS keeps the screen awake with IOKit power assertions (tst_macpowerassertion).
    sugo_add_test(tst_powerinhibitor Qt6::Core Qt6::Gui Qt6::Qml Qt6::DBus)
    sugo_add_test(tst_freedesktopinhibitor Qt6::Core Qt6::DBus)
    sugo_add_test(tst_singleinstance Qt6::Core Qt6::DBus)
    sugo_add_test(tst_krunnerservice Qt6::Core Qt6::Qml Qt6::DBus)
    sugo_add_test(tst_freedesktopnotifier Qt6::Core Qt6::DBus)
    sugo_add_test(tst_dbustimerservice Qt6::Core Qt6::Qml Qt6::DBus)
    sugo_add_test(tst_secretstore Qt6::Core Qt6::DBus)

    # Starts the program on a private bus, uses /Timer, quits with SIGTERM: the teardown must be clean.
    add_test(NAME tst_app_shutdown
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/shutdown_app.sh $<TARGET_FILE:sugo>
    )
    set_tests_properties(tst_app_shutdown PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

    # They check the sources, the same on every system: Linux runs them.
    # Colours in the interface come from the theme (or TrayIconStyle), never from literals.
    add_test(NAME tst_qml_colours
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/check_qml_colours.sh ${CMAKE_SOURCE_DIR}/src/qml
    )

    add_test(NAME tst_qml_sizes
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/check_qml_sizes.sh ${CMAKE_SOURCE_DIR}/src/qml
    )
endif()

add_test(NAME tst_app_startup
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/smoke_app.sh $<TARGET_FILE:sugo>
)
set_tests_properties(tst_app_startup PROPERTIES TIMEOUT 15)

add_test(NAME tst_stats_page_ui
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/smoke_app.sh $<TARGET_FILE:sugo> stats
)
set_tests_properties(tst_stats_page_ui PROPERTIES TIMEOUT 10)

add_test(NAME tst_about_page_ui
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/smoke_app.sh $<TARGET_FILE:sugo> about
)
set_tests_properties(tst_about_page_ui PROPERTIES TIMEOUT 10)

add_test(NAME tst_settings_page_ui
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/smoke_app.sh $<TARGET_FILE:sugo> settings
)
set_tests_properties(tst_settings_page_ui PROPERTIES TIMEOUT 10)

add_test(NAME tst_presets_page_ui
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/smoke_app.sh $<TARGET_FILE:sugo> presets
)
set_tests_properties(tst_presets_page_ui PROPERTIES TIMEOUT 10)

add_test(NAME tst_timer_page_ui
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/smoke_app.sh $<TARGET_FILE:sugo> timer
)
set_tests_properties(tst_timer_page_ui PROPERTIES TIMEOUT 10)

add_test(NAME tst_welcome_page_ui
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/smoke_app.sh $<TARGET_FILE:sugo> welcome
)
set_tests_properties(tst_welcome_page_ui PROPERTIES TIMEOUT 10)

# --- macOS port: B1 menu bar icon, notifications, single instance, Dock ---
sugo_add_test(tst_localsingleinstance Qt6::Core Qt6::Network)
sugo_add_test(tst_macnotificationmapping Qt6::Core)

# --- macOS port: B2 (keep-awake, screen lock and sleep, Keychain, login item) ---
sugo_add_test(tst_launchagentautostart Qt6::Core ${SUGO_DBUS})
if(APPLE)
    sugo_add_test(tst_macpowerassertion Qt6::Core "-framework CoreFoundation" "-framework IOKit")
    sugo_add_test(tst_mackeychainstore Qt6::Core "-framework Security")
endif()

# --- macOS port: B3 sounds ---
# The sounds bundled for macOS (data/sounds), checked against the names the settings page offers.
target_compile_definitions(tst_appsettings PRIVATE SUGO_BUNDLED_SOUNDS_ROOT="${PROJECT_SOURCE_DIR}/data")

# --- macOS port: W2-B Dock integration and fallback icons ---
sugo_add_test(tst_dockcontroller Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml ${SUGO_DBUS})
set_tests_properties(tst_dockcontroller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- macOS port: W2-A (Focus through Shortcuts, sugo:// links) ---
# Both run on Linux too: the Focus with a fake `shortcuts` script (tests/FakeShortcuts.h).
sugo_add_test(tst_shortcutsfocusinhibitor Qt6::Core)
sugo_add_test(tst_timercommands Qt6::Core Qt6::Qml)
