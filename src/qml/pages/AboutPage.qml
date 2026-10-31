// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

QQC2.Page {
    id: aboutPage

    title: i18n("About")
    background: null

    readonly property var aboutData: typeof appAboutData !== "undefined" ? appAboutData : ({
        displayName: i18n("Sugo"),
        shortDescription: i18n("A Pomodoro timer with time statistics"),
        version: "",
        copyrightStatement: ""
    })

    // Set when the user closes the update error message; cleared by every new check.
    property bool updateStatusDismissed: false
    // A check that found nothing new is acknowledged on the button itself for a few seconds.
    property bool upToDateShown: false

    Connections {
        target: UpdateChecker
        function onStatusChanged() {
            if (UpdateChecker.checking) {
                aboutPage.updateStatusDismissed = false;
                aboutPage.upToDateShown = false;
            } else if (UpdateChecker.status === UpdateChecker.UpToDate) {
                aboutPage.upToDateShown = true;
                upToDateTimer.restart();
            }
        }
    }
    Timer {
        id: upToDateTimer
        interval: 3000
        onTriggered: aboutPage.upToDateShown = false
    }

    /// A titled section: a small heading and a card.
    component Section: ColumnLayout {
        id: section
        property string title
        default property alias content: sectionColumn.data
        Layout.fillWidth: true
        spacing: Theme.spaceSm

        Text {
            text: section.title
            color: Theme.muted
            font.pixelSize: Theme.fontSmall
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.letterSpacingCaps
            Layout.leftMargin: Theme.spaceXs
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: sectionColumn.implicitHeight + Theme.spaceLg * 2
            radius: Theme.radiusLg - 2
            color: Theme.card
            border.width: Theme.hairline
            border.color: Theme.border
            ColumnLayout {
                id: sectionColumn
                anchors.fill: parent
                anchors.margins: Theme.spaceLg
                spacing: Theme.spaceMd
            }
        }
    }

    Flickable {
        id: flickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + Theme.space3xl * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

        ColumnLayout {
            id: column
            x: (flickable.width - width) / 2
            y: Theme.space3xl
            width: Math.min(flickable.width - Theme.space2xl * 2, Theme.contentMaxWidth * 0.75)
            spacing: Theme.space2xl

            // Who we are
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceMd

                Image {
                    Layout.alignment: Qt.AlignHCenter
                    source: "qrc:/icons/io.github.mineraleyt.sugo.svg"
                    sourceSize.width: Theme.iconXl * 2
                    sourceSize.height: Theme.iconXl * 2
                    Layout.preferredWidth: Theme.iconXl * 2
                    Layout.preferredHeight: Theme.iconXl * 2
                    Layout.bottomMargin: Theme.spaceXs
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: aboutPage.aboutData.displayName
                    color: Theme.foreground
                    font.pixelSize: Theme.fontDisplay
                    font.weight: Font.DemiBold
                }
                Kbd {
                    Layout.alignment: Qt.AlignHCenter
                    visible: aboutPage.aboutData.version.length > 0
                    text: i18n("Version %1", aboutPage.aboutData.version)
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spaceXs
                    text: aboutPage.aboutData.shortDescription
                    color: Theme.muted
                    font.pixelSize: Theme.fontBody
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            Section {
                title: i18n("Updates")

                Banner {
                    Layout.fillWidth: true
                    visible: UpdateChecker.hasUpdate
                    tone: "warning"
                    text: i18n("A newer version (%1) of Sugo is available.", UpdateChecker.latestVersion)
                    SButton {
                        visible: UpdateChecker.canInstall
                        kind: "primary"
                        text: i18n("Install")
                        onClicked: UpdateChecker.installUpdate()
                    }
                    SButton {
                        kind: UpdateChecker.canInstall ? "ghost" : "primary"
                        text: i18n("View Release")
                        onClicked: Qt.openUrlExternally(UpdateChecker.releaseUrl)
                    }
                }

                Banner {
                    Layout.fillWidth: true
                    visible: UpdateChecker.status === UpdateChecker.Downloading
                    text: i18n("Downloading Sugo %1… %2%", UpdateChecker.latestVersion, Math.round(UpdateChecker.progress * 100))
                }

                Banner {
                    Layout.fillWidth: true
                    visible: UpdateChecker.status === UpdateChecker.ReadyToRestart
                    text: i18n("Sugo %1 is installed. It starts after a restart.", UpdateChecker.latestVersion)
                    SButton {
                        kind: "primary"
                        text: i18n("Restart Now")
                        onClicked: aboutPage.Window.window.requestRestart()
                    }
                }

                Banner {
                    Layout.fillWidth: true
                    visible: !aboutPage.updateStatusDismissed && UpdateChecker.status === UpdateChecker.Error
                    tone: "danger"
                    text: UpdateChecker.status === UpdateChecker.Error
                          ? i18n("Could not update Sugo: %1", UpdateChecker.errorMessage) : ""
                    SButton {
                        kind: "ghost"
                        text: i18n("Open Releases Page")
                        onClicked: Qt.openUrlExternally("https://github.com/MineraleYT/sugo/releases")
                    }
                    SButton {
                        kind: "ghost"
                        iconOnly: true
                        iconName: "close"
                        text: i18n("Dismiss")
                        onClicked: aboutPage.updateStatusDismissed = true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd
                    Text {
                        text: aboutPage.upToDateShown ? i18n("Sugo is up to date.")
                              : !UpdateChecker.canInstall ? i18n("Look for a newer version on GitHub.")
                              : AppSettings.updateChannel === "nightly"
                                ? (AppSettings.autoCheckUpdates ? i18n("Sugo looks for new nightly builds by itself.")
                                                                  : i18n("Updates come from the nightly builds."))
                                : (AppSettings.autoCheckUpdates ? i18n("Sugo looks for new releases by itself.")
                                                                  : i18n("Updates come from the stable releases."))
                        color: aboutPage.upToDateShown ? Theme.positive : Theme.muted
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    SButton {
                        iconName: aboutPage.upToDateShown ? "check" : "download"
                        text: UpdateChecker.checking ? i18n("Checking for updates…") : i18n("Check for Updates")
                        enabled: !UpdateChecker.checking && UpdateChecker.status !== UpdateChecker.Downloading
                                 && UpdateChecker.status !== UpdateChecker.ReadyToRestart
                        onClicked: {
                            aboutPage.updateStatusDismissed = false;
                            UpdateChecker.checkForUpdates();
                        }
                    }
                }
            }

            Section {
                title: i18n("Links")

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm
                    SButton {
                        iconName: "external"
                        text: i18n("Report a bug")
                        onClicked: Qt.openUrlExternally("https://github.com/MineraleYT/sugo/issues")
                    }
                    SButton {
                        iconName: "external"
                        text: i18n("Project website")
                        onClicked: Qt.openUrlExternally("https://github.com/MineraleYT/sugo")
                    }
                }
            }

            Section {
                title: i18n("License & credits")

                Text {
                    Layout.fillWidth: true
                    text: aboutPage.aboutData.copyrightStatement
                    visible: text.length > 0
                    color: Theme.foreground
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: i18n("Licensed under GNU General Public License, version 3 or later.") + " "
                          + i18n("Sugo is an independent, open source project.")
                    color: Theme.muted
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
