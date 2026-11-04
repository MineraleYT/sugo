// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import io.github.mineraleyt.sugo

/**
 * A Banner in a place of its own, so that nothing around it moves when the message comes, goes
 * or changes: the slot is always as tall as a banner with `lines` lines of text (or with its
 * buttons, if they are taller), shown or not. The banner fills it and fades in and out; a longer
 * text is cut after `lines` lines (all of it in a tooltip and for screen readers). Buttons go in
 * as children, as in a Banner.
 */
Item {
    id: slot

    property bool shown: false
    property string text
    property string tone: "info"
    /// The lines of text the slot has room for.
    property int lines: 2
    default property alias actions: banner.actions
    readonly property alias banner: banner

    // The text and tone are kept while the banner fades out, so it does not go blank or change
    // colour on its way out.
    property string heldText
    property string heldTone: "info"
    function hold(): void {
        if (shown) {
            heldText = text;
            heldTone = tone;
        }
    }
    onShownChanged: hold()
    onTextChanged: hold()
    onToneChanged: hold()
    Component.onCompleted: hold()

    implicitHeight: Math.max(measure.implicitHeight, banner.actionsHeight + Theme.spaceMd * 2)

    // A banner of `lines` lines, never shown: the slot's height.
    Banner {
        id: measure
        width: slot.width
        visible: false
        text: "M" + "\nM".repeat(Math.max(0, slot.lines - 1))
    }

    Banner {
        id: banner
        anchors.fill: parent
        text: slot.heldText
        tone: slot.heldTone
        maximumLineCount: slot.lines
        opacity: slot.shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic }
        }
    }
}
