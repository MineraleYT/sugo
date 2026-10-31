// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

/**
 * The fixed colours of the running-timer tray icon: the application icon's dark tile, whose border
 * fills in red (work) or green (break) around white minutes, readable on any panel whatever the
 * colour scheme.
 *
 * The one place they are defined: TrayController paints the real icon with them and its pictures in
 * the interface (TrayBadge.qml) read them from QML (TrayIconStyle), so the two cannot drift apart.
 */
namespace TrayIconColors
{
inline QColor work()
{
    return QColor(0xef, 0x44, 0x44);
}
inline QColor rest()
{
    return QColor(0x22, 0xc5, 0x5e);
}
inline QColor dial()
{
    return QColor(20, 23, 28, 240);
}
inline QColor digits()
{
    return QColor(Qt::white);
}
/// The tile's faint outline, so it stands out on a dark panel too.
inline QColor edge()
{
    return QColor(255, 255, 255, 30);
}
/// The leaf at the end of the spiral, as in the application icon.
inline QColor leaf()
{
    return QColor(0x3d, 0xaa, 0x4a);
}
/// Opacity (0..255) of the ring's track, in the phase colour.
constexpr int TrackAlpha = 55;

inline QColor track(const QColor &phase)
{
    QColor c = phase;
    c.setAlpha(TrackAlpha);
    return c;
}
} // namespace TrayIconColors

class TrayIconStyle : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QColor workColor READ workColor CONSTANT FINAL)
    Q_PROPERTY(QColor breakColor READ breakColor CONSTANT FINAL)
    Q_PROPERTY(QColor dialColor READ dialColor CONSTANT FINAL)
    Q_PROPERTY(QColor digitColor READ digitColor CONSTANT FINAL)
    Q_PROPERTY(QColor edgeColor READ edgeColor CONSTANT FINAL)
    Q_PROPERTY(QColor leafColor READ leafColor CONSTANT FINAL)
    /// The track's opacity, 0..1.
    Q_PROPERTY(qreal trackOpacity READ trackOpacity CONSTANT FINAL)

public:
    using QObject::QObject;

    QColor workColor() const { return TrayIconColors::work(); }
    QColor breakColor() const { return TrayIconColors::rest(); }
    QColor dialColor() const { return TrayIconColors::dial(); }
    QColor digitColor() const { return TrayIconColors::digits(); }
    QColor edgeColor() const { return TrayIconColors::edge(); }
    QColor leafColor() const { return TrayIconColors::leaf(); }
    qreal trackOpacity() const { return TrayIconColors::TrackAlpha / 255.0; }
};
