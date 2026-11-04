<!--
  SPDX-FileCopyrightText: 2026 Sugo contributors
  SPDX-License-Identifier: CC-BY-SA-4.0
-->
# Bundled alert sounds

The end-of-phase and pre-alarm sounds that the settings page offers, for systems without
freedesktop sound themes (macOS). They are installed only in the macOS app bundle, at
`Sugo.app/Contents/Resources/sounds/<theme>/stereo/<name>.oga`; on Linux Sugo uses the
desktop's own sound themes. `AppSettings::findSoundFile()` looks them up by event sound name.

The files are unchanged copies from the upstream sound themes (as packaged by Ubuntu); each
file's `.license` sidecar gives its author and licence (texts in `LICENSES/`).

| File | Upstream | Author | Licence |
|---|---|---|---|
| `ocean/stereo/*.oga` | [Ocean sound theme](https://invent.kde.org/plasma/ocean-sound-theme) 6.6.4 | Guilherme Marçal Silva | CC-BY-SA-4.0 |
| `freedesktop/stereo/complete.oga` | [freedesktop sound theme](https://www.freedesktop.org/wiki/Specifications/sound-theme-spec/) 0.8 | Dr. Richard Boulanger et al ([Berklee44v11](http://www.archive.org/details/Berklee44v11)) | CC-BY-3.0 |
| `freedesktop/stereo/message.oga`, `window-attention.oga` | freedesktop sound theme 0.8 (`window-attention` is its `dialog-warning`) | Ivica Bukvic ("Borealis" sound theme) | CC-BY-SA-3.0 |

`ocean/stereo/bell.oga` is Ocean's `bell-window-system.oga`, which the theme links as `bell`.

The sounds stay in Ogg Vorbis: Qt Multimedia (FFmpeg backend, as in Qt's macOS builds) decodes
them. `afplay`, the fallback player on macOS, cannot read Ogg.
