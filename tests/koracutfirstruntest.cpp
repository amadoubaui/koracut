/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"

#include "koracut/koracutfirstrun.h"
#include "koracut/koracutvideotypes.h"
#include "koracut/koracutupdatecheck.h"

#include <KLocalizedString>
#include <QAbstractButton>
#include <QApplication>
#include <QKeyEvent>
#include <QPushButton>

// L'accueil de premier lancement (4 octobre 2026) : deux cartes, trois interrupteurs, trois sorties.
// Ce qu'on vérifie ici : ce que la fenêtre répond selon le geste, sans souris.

namespace {
QAbstractButton *carte(KoraCutFirstRunDialog &dialog, const QString &debut)
{
    const QList<QAbstractButton *> boutons = dialog.findChildren<QAbstractButton *>();
    for (QAbstractButton *b : boutons) {
        if (b->isCheckable() && KLocalizedString::removeAcceleratorMarker(b->text()).startsWith(debut)) {
            return b;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("Accueil de premier lancement : rien de choisi, Échap passe", "[KoraCutFirstRun]")
{
    KoraCutFirstRunDialog dialog;
    dialog.show();
    CHECK(dialog.choice().format.isEmpty());
    QKeyEvent echap(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&dialog, &echap);
    CHECK(dialog.result() == QDialog::Rejected);
    CHECK(dialog.choice().outcome == KoraCutFirstRun::Outcome::Empty);
    CHECK(dialog.choice().format.isEmpty());
}

TEST_CASE("Accueil de premier lancement : une carte puis le type de vidéo", "[KoraCutFirstRun]")
{
    KoraCutFirstRunDialog dialog;
    dialog.show();
    REQUIRE(dialog.play(QStringLiteral("vertical")));
    CHECK(dialog.choice().format == QLatin1String("vertical"));
    // Les deux cartes s'excluent.
    REQUIRE(dialog.play(QStringLiteral("horizontal")));
    CHECK(dialog.choice().format == QLatin1String("horizontal"));
    REQUIRE(dialog.play(QStringLiteral("sans-recents")));
    REQUIRE(dialog.play(QStringLiteral("terminal")));
    REQUIRE(dialog.play(QStringLiteral("bouton:type")));
    const KoraCutFirstRun::Choice choix = dialog.choice();
    CHECK(choix.outcome == KoraCutFirstRun::Outcome::ChooseType);
    CHECK(choix.format == QLatin1String("horizontal"));
    CHECK_FALSE(choix.showRecents);
    CHECK(choix.terminal);
    CHECK_FALSE(dialog.play(QStringLiteral("mot-inconnu")));
}

TEST_CASE("Accueil de premier lancement : Entrée sur une carte la choisit et valide", "[KoraCutFirstRun]")
{
    KoraCutFirstRunDialog dialog;
    dialog.show();
    QAbstractButton *verticale = carte(dialog, QStringLiteral("Vertical"));
    REQUIRE(verticale != nullptr);
    verticale->setFocus();
    QKeyEvent entree(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(verticale, &entree);
    CHECK(verticale->isChecked());
    CHECK(dialog.result() == QDialog::Accepted);
    CHECK(dialog.choice().outcome == KoraCutFirstRun::Outcome::ChooseType);
    CHECK(dialog.choice().format == QLatin1String("vertical"));
}

TEST_CASE("Accueil de premier lancement : la porte la retire sans rien changer", "[KoraCutFirstRun]")
{
    KoraCutFirstRunDialog dialog;
    dialog.show();
    REQUIRE(dialog.play(QStringLiteral("vertical")));
    dialog.dismiss();
    CHECK(dialog.choice().outcome == KoraCutFirstRun::Outcome::Dismissed);
    // Une fois fermée, un second retrait ne change plus la réponse.
    dialog.dismiss();
    CHECK(dialog.choice().outcome == KoraCutFirstRun::Outcome::Dismissed);
}

TEST_CASE("Accueil de premier lancement : tient sur un portable 13 pouces", "[KoraCutFirstRun]")
{
    KoraCutFirstRunDialog dialog;
    dialog.show();
    // 1280 × 800, moins la barre de menus, le Dock et la barre de titre.
    CHECK(dialog.sizeHint().width() <= 900);
    CHECK(dialog.sizeHint().height() <= 640);
}

TEST_CASE("Le sens préféré se garde et s'efface", "[KoraCutFirstRun]")
{
    const QString avant = KoraCutVideoTypes::preferredFormat();
    KoraCutVideoTypes::setPreferredFormat(QStringLiteral("vertical"));
    CHECK(KoraCutVideoTypes::preferredFormat() == QLatin1String("vertical"));
    // Une valeur inconnue ne s'écrit pas : elle efface.
    KoraCutVideoTypes::setPreferredFormat(QStringLiteral("carre-ou-autre"));
    CHECK(KoraCutVideoTypes::preferredFormat().isEmpty());
    KoraCutVideoTypes::setPreferredFormat(avant);
    CHECK(KoraCutVideoTypes::preferredFormat() == avant);
}

TEST_CASE("Mise à jour : une date publiée plus récente que celle du paquet est annoncée (KC-009)", "[KoraCut]")
{
    CHECK(KoraCutUpdateCheck::isNewer(QStringLiteral("2026.10.21"), QStringLiteral("2026.10.05")));
    CHECK(KoraCutUpdateCheck::isNewer(QStringLiteral("2027.01.02"), QStringLiteral("2026.12.31")));
    CHECK_FALSE(KoraCutUpdateCheck::isNewer(QStringLiteral("2026.10.05"), QStringLiteral("2026.10.05")));
    CHECK_FALSE(KoraCutUpdateCheck::isNewer(QStringLiteral("2026.10.04"), QStringLiteral("2026.10.05")));
    // Une forme inattendue (fichier abîmé, version compilée à la main) n'annonce rien.
    CHECK_FALSE(KoraCutUpdateCheck::isNewer(QStringLiteral("demain"), QStringLiteral("2026.10.05")));
    CHECK_FALSE(KoraCutUpdateCheck::isNewer(QStringLiteral("2026.10.21"), QString()));
    // Hors paquet, pas de date : rien n'est vérifié ; la variable d'essai en donne une.
    qputenv("KORACUT_PAQUET_DATE", "2026.10.05");
    CHECK(KoraCutUpdateCheck::packageInfo().value(QStringLiteral("date")).toString() == QStringLiteral("2026.10.05"));
    qunsetenv("KORACUT_PAQUET_DATE");
    CHECK(KoraCutUpdateCheck::feedUrl() == QStringLiteral("https://koracut.org/version.json"));
}
