// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import stream.sugo.app

/// The icon of a timer (`iconName` as stored with it): one of the built-in tiles, or a theme icon.
ThemeIcon {
    property string iconName

    size: Theme.iconMd
    name: PresetModel.iconSource(iconName)
}
