// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThemeIcons.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QUrlQuery>

ThemeIcons::ThemeIcons(QObject *parent)
    : QObject(parent)
{
}

bool ThemeIcons::exists(const QString &name)
{
    return !name.isEmpty() && QIcon::hasThemeIcon(name);
}

ThemeIconProvider::ThemeIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage ThemeIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const qsizetype queryAt = id.indexOf(QLatin1Char('?'));
    const QString name = id.left(queryAt);
    QColor color;
    if (queryAt >= 0) {
        const QString hex = QUrlQuery(id.mid(queryAt + 1)).queryItemValue(QStringLiteral("color"));
        if (!hex.isEmpty()) {
            color = QColor::fromString(QLatin1Char('#') + hex);
        }
    }

    const QSize wanted(requestedSize.width() > 0 ? requestedSize.width() : (requestedSize.height() > 0 ? requestedSize.height() : 32),
                       requestedSize.height() > 0 ? requestedSize.height() : (requestedSize.width() > 0 ? requestedSize.width() : 32));
    QImage image;
    if (ThemeIcons::exists(name)) {
        // The pixmap may come out smaller than asked (fixed-size icons): centre it.
        const QPixmap pixmap = QIcon::fromTheme(name).pixmap(wanted, 1.0);
        image = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize drawn = pixmap.size().scaled(wanted, Qt::KeepAspectRatio);
        painter.drawPixmap(QRect(QPoint((wanted.width() - drawn.width()) / 2, (wanted.height() - drawn.height()) / 2), drawn), pixmap);
        if (color.isValid()) {
            // Every visible pixel in `color`, keeping the icon's own transparency.
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(image.rect(), color);
        }
    }
    if (size) {
        *size = image.size();
    }
    return image;
}
