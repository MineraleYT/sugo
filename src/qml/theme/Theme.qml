// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick
import io.github.mineraleyt.sugo

/**
 * Every colour and size of the interface. The QML elsewhere uses only these (tests/check_qml_colours.sh
 * and tests/check_qml_sizes.sh fail on literals outside this file).
 *
 * The look: neutral and nearly monochrome, 1 px borders, little decoration. The brand colours
 * (peach → red → magenta, green for breaks) appear only where the time is: the ring, the bars,
 * the status. Dark or light: the "Color scheme" setting, or the desktop's choice for "System"
 * (dark when the desktop does not say).
 */
QtObject {
    id: theme

    readonly property bool dark: AppSettings.colorScheme === "dark"
                                 || (AppSettings.colorScheme !== "light" && Qt.styleHints.colorScheme !== Qt.ColorScheme.Light)

    // --- Surfaces ----------------------------------------------------------------------------
    readonly property color background: dark ? "#0e0e10" : "#fbfbfb"
    readonly property color sidebar: dark ? "#131315" : "#f4f4f5"
    readonly property color card: dark ? "#161618" : "#ffffff"
    readonly property color raised: dark ? "#1c1c1f" : "#ffffff"
    readonly property color border: dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.08)
    readonly property color borderStrong: dark ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.15)
    readonly property color hover: dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.04)
    readonly property color pressed: dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0, 0, 0, 0.08)
    readonly property color selected: dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06)
    readonly property color track: dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.07)
    readonly property color scrim: Qt.rgba(0, 0, 0, dark ? 0.6 : 0.3)

    // --- Text --------------------------------------------------------------------------------
    readonly property color foreground: dark ? "#ececee" : "#18181b"
    readonly property color muted: dark ? "#8e8e97" : "#6b6b74"
    readonly property color subtle: dark ? "#5d5d66" : "#a1a1aa"

    /// The main action: the text colour as a fill.
    readonly property color primary: dark ? "#ececee" : "#18181b"
    readonly property color primaryText: dark ? "#0e0e10" : "#fbfbfb"
    readonly property color focusRing: dark ? Qt.rgba(1, 1, 1, 0.5) : Qt.rgba(0, 0, 0, 0.45)

    // --- Brand and states --------------------------------------------------------------------
    readonly property color brandPeach: "#ffa36b"
    readonly property color brand: "#ff3d3d"
    readonly property color brandMagenta: "#c2185b"
    /// Text and knobs on the brand colour.
    readonly property color brandText: "#ffffff"
    readonly property color breakLight: "#7ee0a1"
    readonly property color breakColor: "#22c55e"
    readonly property color breakDeep: "#0f9d6b"
    readonly property color positive: dark ? "#4ade80" : "#16a34a"
    readonly property color warning: dark ? "#fbbf24" : "#d97706"
    readonly property color danger: dark ? "#f87171" : "#dc2626"
    readonly property color dangerFill: dark ? Qt.rgba(0.97, 0.44, 0.44, 0.12) : Qt.rgba(0.86, 0.15, 0.15, 0.08)
    readonly property color warningFill: dark ? Qt.rgba(0.98, 0.75, 0.14, 0.1) : Qt.rgba(0.85, 0.47, 0.02, 0.08)

    // --- Window buttons of other systems ---------------------------------------------------
    readonly property color windowsClose: "#c42b1c"
    readonly property color macClose: "#ff5f57"
    readonly property color macMinimize: "#febc2e"
    readonly property color macMaximize: "#28c840"

    // --- The application icon, drawn live by LogoIntro (same colours as its SVG) ---------------
    readonly property color logoTileTop: "#33363d"
    readonly property color logoTileBottom: "#16181c"
    readonly property color logoEdge: Qt.rgba(1, 1, 1, 0.12)
    readonly property color logoLeafLight: "#8ee88a"
    readonly property color logoLeafDeep: "#1f9d55"
    /// The soft light of the brand behind the logo (welcome tour), strongest in the middle.
    readonly property color glowInner: theme.alpha(brand, dark ? 0.32 : 0.18)
    readonly property color glowOuter: dark ? theme.alpha(brandMagenta, 0.12) : theme.alpha(brandPeach, 0.08)

    // --- Spacing, shapes, sizes ---------------------------------------------------------------
    readonly property int hairline: 1
    readonly property int spaceXs: 4
    readonly property int spaceSm: 6
    readonly property int spaceMd: 8
    readonly property int spaceLg: 12
    readonly property int spaceXl: 16
    readonly property int space2xl: 24
    readonly property int space3xl: 32
    readonly property int space4xl: 48

    readonly property int radiusSm: 5
    readonly property int radius: 8
    readonly property int radiusLg: 12

    readonly property int iconSm: 14
    readonly property int icon: 16
    readonly property int iconMd: 20
    readonly property int iconLg: 32
    readonly property int iconXl: 40
    /// The application icon in the welcome tour.
    readonly property int logoHero: 128

    readonly property int controlHeight: 32
    readonly property int controlHeightLg: 40
    readonly property int navHeight: 34
    readonly property int sidebarWidth: 236
    readonly property int topBarHeight: 46
    readonly property int contentMaxWidth: 760
    readonly property int windowWidth: 980
    readonly property int windowHeight: 680
    readonly property int windowMinWidth: 380
    readonly property int windowMinHeight: 520
    /// Below this window width the sidebar becomes a drawer.
    readonly property int wideBreakpoint: 680

    // --- Type --------------------------------------------------------------------------------
    readonly property string monoFamily: "monospace"
    readonly property int fontMicro: 10
    readonly property int fontSmall: 11
    readonly property int fontCaption: 12
    readonly property int fontBody: 13
    readonly property int fontTitle: 14
    readonly property int fontHeading: 20
    readonly property int fontDisplay: 26
    /// The name of the application in the welcome tour.
    readonly property int fontHero: 36
    readonly property real letterSpacingCaps: 1.2
    readonly property real letterSpacingTight: -0.8

    // --- Motion ------------------------------------------------------------------------------
    readonly property int durationShort: 120
    readonly property int durationLong: 260
    /// Between one element and the next when a group comes in one after the other.
    readonly property int durationStagger: 60
    readonly property int toolTipDelay: 600

    /// `c` with its alpha multiplied by `a` (for washes of the brand and state colours).
    function alpha(c: color, a: real): color {
        return Qt.rgba(c.r, c.g, c.b, c.a * a);
    }
}
