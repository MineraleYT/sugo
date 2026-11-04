<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
# Fallback icons

macOS has no freedesktop icon theme, and Qt's own Apple icons cover only some names. These are
the icon names Sugo asks the theme for (the menu bar icon's menu, the Dock menu), taken from KDE's
Breeze icons (`breeze/actions/16` and `breeze/actions/22`, kf6-breeze-icon-theme 6.24,
LGPL-3.0-or-later, see `LICENSES/LGPL-3.0-or-later.txt`).

They are built into the program as the theme `:/icons/sugo-fallback` (see
`ThemeIcons::installFallbackTheme()`), which main.cpp makes the fallback theme on macOS.

Changed from Breeze: the text colour `#232629` is `#808080`, so the icons read on light and dark
menus alike (Breeze recolours its icons through KDE's own icon loader, which Qt does not do).

When the code asks for a new theme icon, add it here in both sizes, with its `.license` file, and
list it in `src/CMakeLists.txt`.
