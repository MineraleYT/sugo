// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/**
 * A small number field with its unit ("25 min"): type the number, or use the arrow keys or the
 * mouse wheel. `valueModified()` follows a change by the user.
 */
QQC2.Control {
    id: field

    property int value: 0
    property int from: 0
    property int to: 99
    /// After the number, e.g. "min".
    property string unit
    /// Shown instead of the number for `from` (e.g. "Never" for 0), if set.
    property string fromText
    signal valueModified()

    function setValue(v: int): void {
        const clamped = Math.max(from, Math.min(to, v));
        if (clamped !== value) {
            value = clamped;
            valueModified();
        }
        input.text = Qt.binding(() => field.displayText);
    }

    readonly property string displayText: fromText.length > 0 && value === from ? fromText : String(value)

    implicitWidth: Math.max(Theme.space4xl * 1.7, row.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spaceMd + Theme.hairline * 2
    rightPadding: Theme.spaceMd + Theme.hairline * 2
    hoverEnabled: true
    activeFocusOnTab: true
    onActiveFocusChanged: if (activeFocus) input.forceActiveFocus()

    background: Rectangle {
        radius: Theme.radius
        color: Theme.background
        border.width: Theme.hairline
        border.color: input.activeFocus ? Theme.focusRing : (field.hovered ? Theme.borderStrong : Theme.border)
        Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
    }

    contentItem: RowLayout {
        id: row
        spacing: Theme.spaceXs
        TextInput {
            id: input
            Layout.fillWidth: true
            text: field.displayText
            color: field.enabled ? Theme.foreground : Theme.subtle
            selectionColor: Theme.alpha(Theme.brand, 0.35)
            selectedTextColor: Theme.foreground
            font.pixelSize: Theme.fontBody
            font.weight: Font.Medium
            font.features: { "tnum": 1 }
            horizontalAlignment: Text.AlignRight
            selectByMouse: true
            inputMethodHints: Qt.ImhDigitsOnly
            Accessible.name: field.Accessible.name
            Keys.onUpPressed: field.setValue(field.value + 1)
            Keys.onDownPressed: field.setValue(field.value - 1)
            onEditingFinished: {
                const match = text.match(/\d+/);
                field.setValue(match ? parseInt(match[0]) : field.value);
            }
            onActiveFocusChanged: if (activeFocus) selectAll()
        }
        Text {
            visible: field.unit.length > 0 && !(field.fromText.length > 0 && field.value === field.from)
            text: field.unit
            color: Theme.muted
            font.pixelSize: Theme.fontBody
        }
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => field.setValue(field.value + (event.angleDelta.y > 0 ? 1 : -1))
    }
}
