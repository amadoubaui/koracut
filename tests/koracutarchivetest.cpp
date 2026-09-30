/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Un projet archivé ou partagé ne garde pas le rendu d'aperçu de cette machine (24 septembre 2026) :
// les pistes « timeline_preview » et leurs morceaux sont retirés, les vrais clips restent.

#include "catch.hpp"

#include "koracut/koracutarchive.h"

#include <QDomDocument>

static const char *kProjet = R"(<mlt root="/Users/x/Projet">
 <producer id="producer1"><property name="resource">/Users/x/Library/Caches/koracut/1/preview/325.mp4</property></producer>
 <producer id="producer2"><property name="resource">/Users/x/Library/Caches/koracut/1/preview/350.mp4</property></producer>
 <chain id="chain0"><property name="resource">rushs/plan.mp4</property></chain>
 <playlist id="playlist9"><property name="kdenlive:playlistid">timeline_preview</property><entry producer="producer1"/><entry producer="producer2"/></playlist>
 <playlist id="playlist1"><entry producer="chain0"/></playlist>
 <tractor id="tractor0">
  <property name="kdenlive:sequenceproperties.previewchunks">325-350</property>
  <property name="kdenlive:sequenceproperties.dirtypreviewchunks">375</property>
  <track producer="playlist1"/>
  <track producer="playlist9"/>
 </tractor>
</mlt>)";

TEST_CASE("Archive : le rendu d'aperçu ne part pas avec le projet", "[KoraCut]")
{
    QDomDocument doc;
    REQUIRE(doc.setContent(QByteArray(kProjet)));
    CHECK(KoraCutArchive::stripPreview(doc) == 1);
    const QString xml = doc.toString();
    CHECK_FALSE(xml.contains(QStringLiteral("/preview/")));
    CHECK_FALSE(xml.contains(QStringLiteral("timeline_preview")));
    CHECK_FALSE(xml.contains(QStringLiteral("playlist9")));
    CHECK(xml.contains(QStringLiteral("rushs/plan.mp4")));
    CHECK(doc.elementsByTagName(QStringLiteral("track")).count() == 1);
    CHECK_FALSE(xml.contains(QStringLiteral("325-350")));
    // Un projet sans aperçu ne change pas.
    QDomDocument propre;
    REQUIRE(propre.setContent(QByteArray("<mlt><playlist id=\"p\"><entry producer=\"c\"/></playlist></mlt>")));
    const QString avant = propre.toString();
    CHECK(KoraCutArchive::stripPreview(propre) == 0);
    CHECK(propre.toString() == avant);
}
