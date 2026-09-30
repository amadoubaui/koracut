/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutarchive.h"
#include "xml/xml.hpp"

#include <QDomDocument>
#include <QSet>

int KoraCutArchive::stripPreview(QDomDocument &doc)
{
    QSet<QString> previewIds;
    QList<QDomElement> toRemove;
    const QDomNodeList playlists = doc.elementsByTagName(QStringLiteral("playlist"));
    for (int i = 0; i < playlists.count(); ++i) {
        const QDomElement pl = playlists.item(i).toElement();
        if (Xml::getXmlProperty(pl, QStringLiteral("kdenlive:playlistid")) != QLatin1String("timeline_preview")) {
            continue;
        }
        previewIds << pl.attribute(QStringLiteral("id"));
        const QDomNodeList entries = pl.elementsByTagName(QStringLiteral("entry"));
        for (int j = 0; j < entries.count(); ++j) {
            previewIds << entries.item(j).toElement().attribute(QStringLiteral("producer"));
        }
        toRemove << pl;
    }
    const int removed = int(toRemove.size());
    if (removed == 0) {
        return 0;
    }
    // Les morceaux eux-mêmes : seulement ceux qui sont bien dans un dossier « preview » du cache.
    for (const QString &tag : {QStringLiteral("producer"), QStringLiteral("chain")}) {
        const QDomNodeList items = doc.elementsByTagName(tag);
        for (int i = 0; i < items.count(); ++i) {
            const QDomElement e = items.item(i).toElement();
            if (previewIds.contains(e.attribute(QStringLiteral("id"))) &&
                Xml::getXmlProperty(e, QStringLiteral("resource")).contains(QLatin1String("/preview/"))) {
                toRemove << e;
            }
        }
    }
    const QDomNodeList tracks = doc.elementsByTagName(QStringLiteral("track"));
    for (int i = 0; i < tracks.count(); ++i) {
        const QDomElement t = tracks.item(i).toElement();
        if (previewIds.contains(t.attribute(QStringLiteral("producer")))) {
            toRemove << t;
        }
    }
    for (QDomElement &e : toRemove) {
        e.parentNode().removeChild(e);
    }
    const QDomNodeList props = doc.elementsByTagName(QStringLiteral("property"));
    for (int i = 0; i < props.count(); ++i) {
        QDomElement p = props.item(i).toElement();
        if (p.attribute(QStringLiteral("name")).endsWith(QLatin1String("previewchunks"))) {
            while (p.hasChildNodes()) {
                p.removeChild(p.firstChild());
            }
        }
    }
    return removed;
}
