/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracut/koracuticons.h"

#include <QDebug>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace KoraCutIcons {

QPixmap pixmap(const QString &resourcePath, int size, const QColor &color, qreal dpr)
{
    if (size <= 0) {
        return QPixmap();
    }
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "KoraCut: SVG absent des ressources:" << resourcePath;
        return QPixmap();
    }
    QByteArray data = f.readAll();
    data.replace("currentColor", color.name(QColor::HexRgb).toLatin1());

    QSvgRenderer renderer(data);
    if (!renderer.isValid()) {
        qWarning() << "KoraCut: SVG illisible:" << resourcePath;
        return QPixmap();
    }
    if (dpr <= 0) {
        dpr = 1.0;
    }
    QPixmap pix(QSize(size, size) * dpr);
    pix.fill(Qt::transparent);
    pix.setDevicePixelRatio(dpr);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&p, QRectF(0, 0, size, size));
    return pix;
}

QIcon icon(const QString &resourcePath, int size, const QColor &color, const QColor &activeColor, qreal dpr)
{
    QIcon ic;
    const QPixmap normal = pixmap(resourcePath, size, color, dpr);
    const QPixmap active = pixmap(resourcePath, size, activeColor, dpr);
    if (normal.isNull()) {
        return ic;
    }
    ic.addPixmap(normal, QIcon::Normal, QIcon::Off);
    ic.addPixmap(active, QIcon::Normal, QIcon::On);
    ic.addPixmap(active, QIcon::Active, QIcon::Off);
    ic.addPixmap(active, QIcon::Active, QIcon::On);
    ic.addPixmap(active, QIcon::Selected, QIcon::Off);
    ic.addPixmap(active, QIcon::Selected, QIcon::On);
    return ic;
}

} // namespace KoraCutIcons
