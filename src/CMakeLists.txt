# SPDX-License-Identifier: GPL-3.0-or-later

# All the C++ code but main(), compiled once: the program and the tests link it. It is also the QML
# module; its static plugin (sugo_qml) is linked into the program and imported in main.cpp.
qt6_add_library(sugo_core STATIC)

set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt6_add_qml_module(sugo_core
    URI "io.github.mineraleyt.sugo"
    VERSION 1.0
    PLUGIN_TARGET sugo_qml
    CLASS_NAME SugoQmlPlugin
    QML_FILES
        qml/Main.qml
        qml/pages/TimerPage.qml
        qml/pages/StatsPage.qml
        qml/pages/PresetEditPage.qml
        qml/pages/AppSettingsPage.qml
        qml/pages/AboutPage.qml
        qml/pages/WelcomePage.qml
        qml/components/ProgressRing.qml
        qml/theme/Theme.qml
        qml/components/LineIcon.qml
        qml/components/Kbd.qml
        qml/components/SButton.qml
        qml/components/NavItem.qml
        qml/components/StatusPill.qml
        qml/components/Toast.qml
        qml/components/TimerIcon.qml
        qml/components/TrayBadge.qml
        qml/components/Sidebar.qml
        qml/components/TopBar.qml
        qml/components/ConfirmDialog.qml
        qml/components/Banner.qml
        qml/components/FormSection.qml
        qml/components/FormRow.qml
        qml/components/SSwitch.qml
        qml/components/STextField.qml
        qml/components/SComboBox.qml
        qml/components/CommandPalette.qml
        qml/components/SSlider.qml
        qml/components/Segmented.qml
        qml/components/NumberField.qml
        qml/components/BrandGlow.qml
        qml/components/LogoIntro.qml
        qml/components/WindowButtons.qml
        qml/components/ThemeIcon.qml
)

# C++ types exposed to QML (QML_ELEMENT / QML_SINGLETON) live in core/.
target_sources(sugo_core PRIVATE
    core/Clock.h
    core/TimerEngine.h
    core/TimerEngine.cpp
    core/TimerPreset.h
    core/PresetSettings.h
    core/PresetSettings.cpp
    core/Localization.h
    core/Localization.cpp
    core/Settings.h
    core/Settings.cpp
    core/PresetModel.h
    core/PresetModel.cpp
    core/TimerBinding.h
    core/TimerBinding.cpp
    core/PhaseNotifier.h
    core/PhaseNotifier.cpp
    core/FocusSoundController.h
    core/FocusSoundController.cpp
    core/DataManager.h
    core/DataManager.cpp
    core/SilenceController.h
    core/SilenceController.cpp
    core/AppSettings.h
    core/AppSettings.cpp
    core/TrayPresenter.h
    core/TrayPresenter.cpp
    core/TrayController.h
    core/TrayController.cpp
    core/TrayIconStyle.h
    core/UpdateChecker.h
    core/UpdateChecker.cpp
    core/Diagnostics.h
    core/Diagnostics.cpp
    core/CalendarSync.h
    core/CalendarSync.cpp
    platform/NotificationInhibitor.h
    platform/NotificationInhibitor.cpp
    platform/PowerInhibitor.h
    platform/PowerInhibitor.cpp
    platform/NoopInhibitor.h
    platform/FreedesktopInhibitor.h
    platform/FreedesktopInhibitor.cpp
    platform/InhibitorFactory.h
    platform/InhibitorFactory.cpp
    platform/Notifier.h
    platform/FreedesktopNotifier.h
    platform/FreedesktopNotifier.cpp
    platform/TrayNotifier.h
    platform/TrayNotifier.cpp
    platform/NotifierFactory.h
    platform/NotifierFactory.cpp
    platform/UnixSignals.h
    platform/UnixSignals.cpp
    platform/Autostart.h
    platform/Autostart.cpp
    platform/SystemTray.h
    platform/SystemTray.cpp
    platform/SingleInstance.h
    platform/SingleInstance.cpp
    platform/DBusTimerService.h
    platform/DBusTimerService.cpp
    platform/KRunnerService.h
    platform/KRunnerService.cpp
    platform/ScreenLockWatcher.h
    platform/ScreenLockWatcher.cpp
    platform/SecretStore.h
    platform/SecretStore.cpp
    platform/DesktopStyle.h
    platform/DesktopStyle.cpp
    platform/AppImage.h
    platform/AppImage.cpp
    platform/ThemeIcons.h
    platform/ThemeIcons.cpp
    db/Database.h
    db/Database.cpp
    db/PresetRepository.h
    db/PresetRepository.cpp
    db/PresetPersistence.h
    db/PresetPersistence.cpp
    db/SessionRepository.h
    db/SessionRepository.cpp
    db/SessionRecorder.h
    db/SessionRecorder.cpp
    stats/StatsTypes.h
    stats/StatsAggregator.h
    stats/StatsAggregator.cpp
    stats/StatsModel.h
    stats/StatsModel.cpp
)
target_include_directories(sugo_core PUBLIC core db platform stats)
# Qt's resource and QML cache objects share these include paths but may compile before moc has
# created them; -Wmissing-include-dirs would warn in a clean parallel build.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/sugo_core_autogen/include ${CMAKE_CURRENT_BINARY_DIR}/sugo_qml_autogen/include)

# Private: what a test may use of Qt is up to the test (see tests/CMakeLists.txt).
target_link_libraries(sugo_core PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::DBus
    Qt6::Network
    Qt6::Multimedia
)

set(sugo_app_sources main.cpp)
if(APPLE)
    # Sugo.app: the icon (Contents/Resources/Sugo.icns, from packaging/macos/render-icons.sh's PNGs)
    # and an Info.plist whose version is only X.Y.Z (nightly suffixes are not valid there).
    include(ECMAddAppIcon)
    file(GLOB sugo_macos_icons "${PROJECT_SOURCE_DIR}/data/icons/macos/*-apps-io.github.mineraleyt.sugo.png")
    if(sugo_macos_icons)
        ecm_add_app_icon(sugo_app_sources ICONS ${sugo_macos_icons} OUTFILE_BASENAME Sugo)
    endif()
    string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" SUGO_BUNDLE_VERSION "${sugo_version}")
    if(NOT SUGO_BUNDLE_VERSION)
        set(SUGO_BUNDLE_VERSION "${PROJECT_VERSION}")
    endif()
    set(SUGO_BUNDLE_EXECUTABLE Sugo)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in" "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
endif()

# A plain program on Linux; Sugo.app on macOS (MACOSX_BUNDLE is ignored elsewhere).
add_executable(sugo MACOSX_BUNDLE ${sugo_app_sources})
if(APPLE)
    set_target_properties(sugo PROPERTIES
        OUTPUT_NAME Sugo
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.mineraleyt.sugo
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    )
endif()

# The timer icons, shown through PresetModel::iconSource() as qrc:/icons/<name>.svg.
qt6_add_resources(sugo "preseticons"
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/data/icons/presets"
    FILES
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-break.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-coding.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-favorite.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-focus.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-goal.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-research.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-sprint.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-study.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-tasks.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-timer.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-tomato.svg
        ${PROJECT_SOURCE_DIR}/data/icons/presets/sugo-writing.svg
)

# Embedded fallback so the icon also shows when running from the build tree.
qt6_add_resources(sugo "appicon"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/data/icons/hicolor/scalable/apps"
    FILES "${CMAKE_SOURCE_DIR}/data/icons/hicolor/scalable/apps/io.github.mineraleyt.sugo.svg"
)

# The QML plugin first, then the library it links privately: listed the other way round, CMake repeats
# libsugo_core.a after the plugin to keep the static link order (Apple's ld warns about duplicates).
target_link_libraries(sugo PRIVATE
    sugo_qml
    sugo_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::DBus
    Qt6::Network
)

if(APPLE)
    # KDE_INSTALL_TARGETS_DEFAULT_ARGS would put the bundle in /Applications/KDE (absolute).
    install(TARGETS sugo BUNDLE DESTINATION . RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})
else()
    install(TARGETS sugo ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
endif()
