# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)

project(Sugo VERSION 1.0.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.8.0)  # QStyleHints::setColorScheme
set(ECM_MIN_VERSION 6.8.0)

find_package(ECM ${ECM_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The version the program shows and compares when it looks for updates. Nightly builds set it to
# a pre-release of the coming version, "1.0.1-nightly.20261031.42": newer than 1.0.0, older than
# 1.0.1 (see .github/workflows/nightly.yml).
# Empty: the project version (kept out of the cache, so a version bump reaches existing builds).
set(SUGO_VERSION "" CACHE STRING "Version shown by the program and compared by the update check (empty: the project version)")
set(sugo_version "${SUGO_VERSION}")
if(sugo_version STREQUAL "")
    set(sugo_version "${PROJECT_VERSION}")
endif()

add_compile_definitions(
    PROJECT_VERSION="${sugo_version}"
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_URL_CAST_FROM_STRING
    QT_USE_QSTRINGBUILDER
    QT_DISABLE_DEPRECATED_BEFORE=0x060800
)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Sql DBus Network)

# QML modules live under qrc:/qt/qml/ (QTP0001). Qt >= 6.8: one qmldir per QML subdirectory (QTP0004).
qt6_policy(SET QTP0001 NEW)
if(QT_KNOWN_POLICY_QTP0004)
    qt6_policy(SET QTP0004 NEW)
endif()
# The QML module's build output goes under its URI path, where Qt's tools (qmllint, qmlls) look for it.
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(FILES data/io.github.mineraleyt.sugo.desktop DESTINATION ${KDE_INSTALL_APPDIR})
# KRunner only looks for D-Bus runners in share/krunner/dbusplugins. The plugin version
# comes from project() so it can never fall behind a release.
configure_file(data/io.github.mineraleyt.sugo.runner.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.mineraleyt.sugo.runner.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.mineraleyt.sugo.runner.desktop DESTINATION ${KDE_INSTALL_DATADIR}/krunner/dbusplugins)
# D-Bus activation: KRunner or a `qdbus` call can start Sugo (hidden in the tray) on demand.
configure_file(data/io.github.mineraleyt.sugo.service.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.mineraleyt.sugo.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.mineraleyt.sugo.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES data/io.github.mineraleyt.sugo.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES data/icons/hicolor/scalable/apps/io.github.mineraleyt.sugo.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
