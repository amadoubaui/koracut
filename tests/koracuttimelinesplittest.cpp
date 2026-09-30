/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"

#include "core.h"
#include "koracut/koracuttimelinesplit.h"

#include <mlt++/MltProfile.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSplitter>
#include <QWidget>

// Plusieurs séquences côte à côte (document 13, § 5, 24 septembre 2026) : la découpe de la zone
// Timeline en vues, sans la timeline elle-même. Chaque « vue » est ici un widget nommé.

using KoraCutSplit::Side;

static QWidget *vue(const char *nom)
{
    auto *w = new QWidget;
    w->setObjectName(QString::fromLatin1(nom));
    return w;
}

TEST_CASE("Découper la zone Timeline en vues", "[KoraCutSplit]")
{
    KoraCutTimelineSplitHost zone;
    zone.resize(1200, 600);
    QWidget *a = vue("a");
    zone.setRoot(a);
    CHECK(zone.describe() == QStringLiteral("a"));

    // Center ne découpe rien.
    QWidget *rien = vue("rien");
    zone.place(rien, a, Side::Center);
    CHECK(zone.describe() == QStringLiteral("a"));

    QWidget *b = vue("b");
    zone.place(b, a, Side::Right);
    CHECK(zone.describe() == QStringLiteral("H[a,b]"));

    QWidget *c = vue("c");
    zone.place(c, b, Side::Bottom);
    CHECK(zone.describe() == QStringLiteral("H[a,V[b,c]]"));

    // À côté de toute la zone : la découpe entière passe d'un côté.
    QWidget *d = vue("d");
    zone.place(d, nullptr, Side::Top);
    CHECK(zone.describe() == QStringLiteral("V[d,H[a,V[b,c]]]"));

    // Même sens que le séparateur voisin : la vue s'y range, sans séparateur de plus.
    QWidget *e = vue("e");
    zone.place(e, a, Side::Left);
    CHECK(zone.describe() == QStringLiteral("V[d,H[e,a,V[b,c]]]"));

    // Les vues se partagent la place à parts égales.
    auto *ligne = qobject_cast<QSplitter *>(a->parentWidget());
    REQUIRE(ligne != nullptr);
    CHECK(ligne->count() == 3);

    // Retirer une vue referme les séparateurs qui ne gardent qu'une vue.
    zone.remove(d);
    CHECK(zone.describe() == QStringLiteral("H[e,a,V[b,c]]"));
    zone.remove(b);
    CHECK(zone.describe() == QStringLiteral("H[e,a,c]"));
    zone.remove(e);
    zone.remove(c);
    CHECK(zone.describe() == QStringLiteral("a"));
    CHECK(zone.root() == a);
    CHECK(a->parentWidget() == &zone);

    // La vue principale ne se retire pas.
    zone.remove(a);
    CHECK(zone.describe() == QStringLiteral("a"));
}

TEST_CASE("Zone de dépôt sous le pointeur", "[KoraCutSplit]")
{
    const QRect vue(0, 0, 1000, 500);
    const int onglets = 30;
    // Sur la barre d'onglets : dans les onglets de cette vue.
    CHECK(KoraCutSplit::sideAt(vue, QPoint(500, 10), onglets) == Side::Center);
    CHECK(KoraCutSplit::sideAt(vue, QPoint(50, 250), onglets) == Side::Left);
    CHECK(KoraCutSplit::sideAt(vue, QPoint(950, 250), onglets) == Side::Right);
    CHECK(KoraCutSplit::sideAt(vue, QPoint(500, 40), onglets) == Side::Top);
    CHECK(KoraCutSplit::sideAt(vue, QPoint(500, 490), onglets) == Side::Bottom);
    // Au milieu : dans les onglets aussi.
    CHECK(KoraCutSplit::sideAt(vue, QPoint(500, 265), onglets) == Side::Center);
    // Hors de la vue : rien.
    CHECK(KoraCutSplit::sideAt(vue, QPoint(1200, 265), onglets) == Side::Center);

    CHECK(KoraCutSplit::zoneRect(vue, Side::Right) == QRect(500, 0, 500, 500));
    CHECK(KoraCutSplit::zoneRect(vue, Side::Left) == QRect(0, 0, 500, 500));
    CHECK(KoraCutSplit::zoneRect(vue, Side::Top) == QRect(0, 0, 1000, 250));
    CHECK(KoraCutSplit::zoneRect(vue, Side::Bottom) == QRect(0, 250, 1000, 250));
    CHECK(KoraCutSplit::zoneRect(vue, Side::Center) == vue);
}

TEST_CASE("Noms des côtés (porte de commande)", "[KoraCutSplit]")
{
    bool ok = false;
    CHECK(KoraCutSplit::sideFromName(QStringLiteral("droite"), &ok) == Side::Right);
    CHECK(ok);
    CHECK(KoraCutSplit::sideFromName(QStringLiteral("Gauche"), &ok) == Side::Left);
    CHECK(KoraCutSplit::sideFromName(QStringLiteral("haut"), &ok) == Side::Top);
    CHECK(KoraCutSplit::sideFromName(QStringLiteral("bottom"), &ok) == Side::Bottom);
    CHECK(KoraCutSplit::sideFromName(QStringLiteral("onglet"), &ok) == Side::Center);
    CHECK(ok);
    KoraCutSplit::sideFromName(QStringLiteral("travers"), &ok);
    CHECK_FALSE(ok);
    for (Side side : {Side::Left, Side::Right, Side::Top, Side::Bottom, Side::Center}) {
        CHECK(KoraCutSplit::sideFromName(KoraCutSplit::sideName(side)) == side);
    }
}

TEST_CASE("Enregistrer puis reposer la découpe", "[KoraCutSplit]")
{
    // Enregistrer : la découpe devient du JSON (comme la propriété koracut:vues_timeline).
    KoraCutTimelineSplitHost avant;
    avant.resize(1200, 600);
    QWidget *a = vue("a");
    avant.setRoot(a);
    avant.place(vue("b"), a, Side::Right);
    avant.place(vue("c"), a, Side::Bottom);
    REQUIRE(avant.describe() == QStringLiteral("H[V[a,c],b]"));
    const auto nom = [](QWidget *w) { return QJsonObject{{QStringLiteral("nom"), w->objectName()}}; };
    const QByteArray texte = QJsonDocument(avant.serialize(nom)).toJson(QJsonDocument::Compact);
    CHECK(texte.contains("\"sens\":\"h\""));

    // Rouvrir : une zone neuve, la vue principale seule, et la découpe relue depuis le texte.
    KoraCutTimelineSplitHost apres;
    apres.resize(1200, 600);
    QWidget *principale = vue("a");
    apres.setRoot(principale);
    const QJsonObject relu = QJsonDocument::fromJson(texte).object();
    REQUIRE(apres.restore(relu, [principale](const QJsonObject &leaf) -> QWidget * {
        const QString n = leaf.value(QStringLiteral("nom")).toString();
        return n == QLatin1String("a") ? principale : vue(n.toLatin1().constData());
    }));
    CHECK(apres.describe() == QStringLiteral("H[V[a,c],b]"));
    CHECK(apres.root() != principale);

    // Une vue qui n'existe plus est ignorée, et une découpe reposée ne se repose pas deux fois.
    KoraCutTimelineSplitHost partiel;
    QWidget *seule = vue("a");
    partiel.setRoot(seule);
    REQUIRE(partiel.restore(relu, [seule](const QJsonObject &leaf) -> QWidget * {
        const QString n = leaf.value(QStringLiteral("nom")).toString();
        return n == QLatin1String("a") ? seule : (n == QLatin1String("b") ? vue("b") : nullptr);
    }));
    CHECK(partiel.describe() == QStringLiteral("H[a,b]"));
    CHECK_FALSE(partiel.restore(relu, [](const QJsonObject &) -> QWidget * { return nullptr; }));
}

TEST_CASE("Retourner le profil pour une séquence verticale", "[KoraCutSplit]")
{
    // Document 13, § 5 bis : le profil du projet se retourne en place, même cadence.
    pCore->setCurrentProfile(QStringLiteral("atsc_1080p_25"));
    Mlt::Profile &profil = pCore->getProjectProfile();
    mlt_profile avant = profil.get_profile();
    REQUIRE(profil.width() == 1920);
    CHECK_FALSE(pCore->koraCutSetVertical(false));
    REQUIRE(pCore->koraCutSetVertical(true));
    CHECK(profil.width() == 1080);
    CHECK(profil.height() == 1920);
    CHECK(profil.frame_rate_num() == 25);
    CHECK(pCore->isVertical());
    // Le même objet MLT : les producteurs déjà créés dessus suivent.
    CHECK(profil.get_profile() == avant);
    REQUIRE(pCore->koraCutSetVertical(false));
    CHECK(profil.width() == 1920);
    CHECK(profil.height() == 1080);
}
