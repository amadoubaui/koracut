/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"

#include "kdenlivesettings.h"
#include "utils/uiutils.h"
#include "koracut/koracutfonts.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutthemes.h"

#include <KColorSchemeManager>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QPalette>
#include <QFontDatabase>
#include <QRegularExpression>

// Un thème installé pendant la séance (Affichage > Thème > Installer un thème…) : le gestionnaire
// de schémas de KF6 ne relit pas son dossier, il ne le connaît donc pas. KoraCut l'applique depuis
// son fichier, puis doit savoir revenir à un thème normal. 23 septembre 2026.
TEST_CASE("Thème installé pendant la séance", "[KoraCutTheme]")
{
    // Les thèmes livrés sont écrits avant que le gestionnaire ne dresse sa liste, comme dans main.cpp.
    KoraCutThemes::self()->installBuiltins();
    auto *manager = KColorSchemeManager::instance();
    const QString sombre = UiUtils::themeFile(QStringLiteral("KoraCutDark"));
    REQUIRE_FALSE(sombre.isEmpty());

    // La feuille de style est une ressource de l'application, pas de la bibliothèque : l'exécutable
    // de test ne l'a pas. On ne vérifie alors que la palette.
    const bool feuille = QFile::exists(QStringLiteral(":/koracut.qss"));
    const QString modeAvant = KdenliveSettings::koracut_theme_mode();
    KdenliveSettings::setKoracut_theme_mode(QStringLiteral("dark"));
    UiUtils::applyThemeMode();
    const QColor fondSombre = qApp->palette().color(QPalette::Window);
    if (feuille) CHECK(qApp->styleSheet().contains(QStringLiteral("#1E1E1E"), Qt::CaseInsensitive));

    // Le thème d'essai : Sombre, avec un fond de fenêtre et une couleur de feuille à lui.
    const QString id = QStringLiteral("KoraCutEssaiTestSeance");
    const QString chemin = UiUtils::userThemesFolder() + QLatin1Char('/') + id + QStringLiteral(".colors");
    QDir().mkpath(UiUtils::userThemesFolder());
    QFile source(sombre);
    REQUIRE(source.open(QIODevice::ReadOnly));
    QString texte = QString::fromUtf8(source.readAll());
    const qsizetype fenetre = texte.indexOf(QStringLiteral("[Colors:Window]"));
    REQUIRE(fenetre >= 0);
    const QRegularExpression fond(QStringLiteral("\\nBackgroundNormal=[^\\n]*"));
    const QRegularExpressionMatch m = fond.match(texte, fenetre);
    REQUIRE(m.hasMatch());
    texte.replace(m.capturedStart(), m.capturedLength(), QStringLiteral("\nBackgroundNormal=10,20,30"));
    texte.replace(QRegularExpression(QStringLiteral("\\nName\\[[^\\]]*\\]=[^\\n]*")), QString());
    texte.replace(QRegularExpression(QStringLiteral("\\nName=[^\\n]*")), QStringLiteral("\nName=Essai séance"));
    texte.replace(QRegularExpression(QStringLiteral("\\nRemplacements=[^\\n]*")), QString());
    texte.replace(QStringLiteral("[KoraCut]"), QStringLiteral("[KoraCut]\nRemplacements=#1E1E1E:#0A141E"));
    {
        QFile cible(chemin);
        REQUIRE(cible.open(QIODevice::WriteOnly | QIODevice::Truncate));
        cible.write(texte.toUtf8());
    }
    REQUIRE_FALSE(manager->indexForSchemeId(id).isValid());
    CHECK(UiUtils::themeFile(id) == chemin);
    CHECK(UiUtils::themeName(chemin, id) == QStringLiteral("Essai séance"));
    CHECK(UiUtils::themeBase(chemin) == QStringLiteral("dark"));

    KdenliveSettings::setKoracut_theme_mode(id);
    UiUtils::applyThemeMode();
    CHECK(qApp->palette().color(QPalette::Window) == QColor(10, 20, 30));
    if (feuille) CHECK(qApp->styleSheet().contains(QStringLiteral("#0A141E"), Qt::CaseInsensitive));
    if (feuille) CHECK_FALSE(qApp->styleSheet().contains(QStringLiteral("#1E1E1E"), Qt::CaseInsensitive));

    // Retour à Sombre : le gestionnaire croit Sombre toujours actif, il faut que l'écran suive.
    KdenliveSettings::setKoracut_theme_mode(QStringLiteral("dark"));
    UiUtils::applyThemeMode();
    CHECK(qApp->palette().color(QPalette::Window) == fondSombre);
    if (feuille) CHECK(qApp->styleSheet().contains(QStringLiteral("#1E1E1E"), Qt::CaseInsensitive));

    // Un thème qui n'existe nulle part : on retombe sur Sombre, sans rien casser.
    KdenliveSettings::setKoracut_theme_mode(QStringLiteral("KoraCutQuiNExistePas"));
    UiUtils::applyThemeMode();
    CHECK(qApp->palette().color(QPalette::Window) == fondSombre);

    QFile::remove(chemin);
    KdenliveSettings::setKoracut_theme_mode(modeAvant);
}

TEST_CASE("Thèmes par base, pour le suivi du système", "[KoraCutTheme]")
{
    const auto sombres = UiUtils::koraCutThemesFor(QStringLiteral("dark"));
    const auto clairs = UiUtils::koraCutThemesFor(QStringLiteral("light"));
    auto contient = [](const QList<QPair<QString, QString>> &liste, const QString &id) {
        for (const auto &t : liste) {
            if (t.first == id) {
                return true;
            }
        }
        return false;
    };
    CHECK(contient(sombres, QStringLiteral("KoraCutDark")));
    CHECK(contient(sombres, QStringLiteral("KoraCutTresSombre")));
    CHECK_FALSE(contient(sombres, QStringLiteral("KoraCutLight")));
    CHECK(contient(clairs, QStringLiteral("KoraCutLight")));
    CHECK_FALSE(contient(clairs, QStringLiteral("KoraCutDark")));
}

// ---- Le moteur de thèmes (25 septembre 2026) ------------------------------------------------------

namespace {
QString gabarit()
{
    QFile f(QStringLiteral(":/koracut.qss"));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
QString regle(const QString &sheet, const QString &selecteur)
{
    // Le corps de la première règle qui commence exactement par ce sélecteur.
    const qsizetype debut = sheet.indexOf(QLatin1Char('\n') + selecteur + QStringLiteral(" {"));
    if (debut < 0) {
        return {};
    }
    const qsizetype ouvre = sheet.indexOf(QLatin1Char('{'), debut);
    return sheet.mid(ouvre, sheet.indexOf(QLatin1Char('}'), ouvre) - ouvre);
}
} // namespace

TEST_CASE("Gabarit : Sombre et Clair rendus comme les deux feuilles d'avant", "[KoraCutTheme]")
{
    const QString tmpl = gabarit();
    REQUIRE_FALSE(tmpl.isEmpty());
    const QString sombre = KoraCutThemes::renderStyleSheet(tmpl, KoraCutThemes::self()->tokens(QStringLiteral("KoraCutDark")));
    const QString clair = KoraCutThemes::renderStyleSheet(tmpl, KoraCutThemes::self()->tokens(QStringLiteral("KoraCutLight")));
    // Plus aucun jeton hors des commentaires.
    static const QRegularExpression reste(QStringLiteral("\\{[a-z_]+(@[0-9/]+)?\\}"));
    const auto sansCommentaires = [](QString t) -> QString { return t.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption)); };
    CHECK_FALSE(sansCommentaires(sombre).contains(reste));
    CHECK_FALSE(sansCommentaires(clair).contains(reste));
    // Des valeurs relevées dans koracut.qss et koracut-light.qss d'avant le 25 septembre 2026.
    CHECK(regle(sombre, QStringLiteral("QToolBar")).contains(QStringLiteral("background: #2C2C2C;")));
    CHECK(regle(clair, QStringLiteral("QToolBar")).contains(QStringLiteral("background: #FFFFFF;")));
    CHECK(regle(sombre, QStringLiteral("QToolTip")).contains(QStringLiteral("border: 1px solid rgba(255, 255, 255, 41);")));
    CHECK(regle(clair, QStringLiteral("QToolTip")).contains(QStringLiteral("border: 1px solid rgba(0, 0, 0, 41);")));
    CHECK(regle(clair, QStringLiteral("QTabBar::tab")).contains(QStringLiteral("color: rgba(0, 0, 0, 158);")));
    CHECK(regle(clair, QStringLiteral("QToolButton#koracutCategoryChip")).contains(QStringLiteral("rgba(0, 0, 0, 10)")));
    CHECK(regle(sombre, QStringLiteral("QToolButton#koracutCategoryChip")).contains(QStringLiteral("rgba(255, 255, 255, 14)")));
    CHECK(sombre.contains(QStringLiteral("chevron-bas-sombre.svg")));
    CHECK(clair.contains(QStringLiteral("chevron-bas-clair.svg")));
    // Les rayons se mettent à l'échelle : 0 donne des coins droits.
    const QString droit = KoraCutThemes::renderStyleSheet(tmpl, KoraCutThemes::self()->tokens(QStringLiteral("KoraCutDark")), 0);
    CHECK_FALSE(droit.contains(QRegularExpression(QStringLiteral("radius:\\s*[1-9]"))));
}

TEST_CASE("Thèmes livrés : palettes complètes et lisibles", "[KoraCutTheme]")
{
    const auto themes = KoraCutThemes::self()->list();
    QStringList livres;
    for (const auto &t : themes) {
        if (t.builtin) {
            livres << t.slug;
        }
    }
    for (const char *slug : {"sombre", "clair", "tres-sombre", "gris", "contraste-eleve", "matrix", "spider-man", "punk", "ocean", "sable", "nuit-violette"}) {
        CHECK(livres.contains(QString::fromLatin1(slug)));
    }
    for (const auto &t : themes) {
        if (!t.builtin) {
            continue;
        }
        const auto tokens = KoraCutThemes::self()->tokens(t.id);
        for (const QString &name : KoraCutThemes::tokenNames()) {
            INFO(t.id.toStdString() << " " << name.toStdString());
            CHECK(tokens.value(name).isValid());
        }
        for (const auto &c : KoraCutThemes::self()->checks(t.id)) {
            INFO(t.id.toStdString() << " : " << c.fg.toStdString() << " sur " << c.bg.toStdString() << " = " << c.ratio);
            CHECK((c.ok || c.accepted));
        }
    }
    // Les écarts acceptés ne concernent que les thèmes de la charte.
    CHECK(KoraCutThemes::contrast(QColor(0xFFFFFF), QColor(0x0D99FF)) < 3.0);
    CHECK(KoraCutThemes::isDark(KoraCutThemes::self()->tokens(QStringLiteral("KoraCutMatrix"))));
    CHECK_FALSE(KoraCutThemes::isDark(KoraCutThemes::self()->tokens(QStringLiteral("KoraCutSable"))));
}

TEST_CASE("Palette dérivée et corrigée", "[KoraCutTheme]")
{
    // Deux couleurs suffisent.
    const auto full = KoraCutThemes::derive({{QStringLiteral("fond"), QColor(0x101820)}, {QStringLiteral("accent"), QColor(0xFFB000)}});
    CHECK(full.size() == KoraCutThemes::tokenNames().size());
    CHECK(full.value(QStringLiteral("texte")) == QColor(0xFFFFFF));
    CHECK(full.value(QStringLiteral("texte_sur_accent")) == QColor(Qt::black));
    // Un texte gris foncé sur un fond noir : illisible, le moteur le corrige et le dit.
    QMap<QString, QColor> illisible{{QStringLiteral("fond"), QColor(Qt::black)}, {QStringLiteral("texte"), QColor(0x303030)}, {QStringLiteral("accent"), QColor(0x101010)}};
    const auto corrections = KoraCutThemes::correct(illisible);
    CHECK(corrections.size() >= 2);
    const auto corrigee = KoraCutThemes::derive(illisible);
    for (const auto &c : KoraCutThemes::contrastChecks(corrigee, illisible.keys())) {
        INFO(c.fg.toStdString() << " sur " << c.bg.toStdString() << " = " << c.ratio);
        CHECK(c.ok);
    }
    // Un fond à mi-chemin : ni le blanc ni le noir ne suffisent au début, le fond bouge aussi.
    QMap<QString, QColor> gris{{QStringLiteral("fond"), QColor(0x777777)}, {QStringLiteral("surface"), QColor(0x777777)}, {QStringLiteral("texte"), QColor(0x888888)}};
    KoraCutThemes::correct(gris);
    for (const auto &c : KoraCutThemes::contrastChecks(KoraCutThemes::derive(gris), gris.keys())) {
        INFO(c.fg.toStdString() << " sur " << c.bg.toStdString() << " = " << c.ratio);
        CHECK(c.ok);
    }
    CHECK(KoraCutThemes::parseColor(QStringLiteral("#FFFFFF9E")).alpha() == 158);
    CHECK(KoraCutThemes::colorText(QColor(255, 255, 255, 158)) == QStringLiteral("#FFFFFF9E"));
    CHECK(KoraCutThemes::simplify(QStringLiteral("Spider-Man")) == KoraCutThemes::simplify(QStringLiteral("spider man")));
}

TEST_CASE("Outils du thème et des réglages", "[KoraCutTheme]")
{
    const QString modeAvant = KdenliveSettings::koracut_theme_mode();
    const auto call = [](const char *tool, const QJsonObject &args) { return KoraCutAgentTools::call(nullptr, QString::fromLatin1(tool), args); };

    const QJsonObject liste = call("theme_lister", {{QStringLiteral("avec_palette"), true}});
    REQUIRE(liste.value(QStringLiteral("ok")).toBool());
    CHECK(liste.value(QStringLiteral("themes")).toArray().size() >= 11);
    CHECK(liste.value(QStringLiteral("palette")).toObject().contains(QStringLiteral("accent")));

    // « Spider Man », puis retour au thème d'avant.
    call("theme_appliquer", {{QStringLiteral("nom"), QStringLiteral("sombre")}});
    const QJsonObject spider = call("theme_appliquer", {{QStringLiteral("nom"), QStringLiteral("Spider Man")}});
    REQUIRE(spider.value(QStringLiteral("ok")).toBool());
    CHECK(KdenliveSettings::koracut_theme_mode() == QStringLiteral("KoraCutSpiderMan"));
    CHECK(KoraCutThemes::token(QStringLiteral("accent")) == QColor(0xFF3B45));
    CHECK(KoraCutThemes::trackColor(QStringLiteral("piste_video"), Qt::gray) == QColor(0xD0222D));
    CHECK(qApp->palette().color(QPalette::Window) == QColor(0x111A40));
    const QJsonObject retour = call("theme_appliquer", {{QStringLiteral("nom"), QStringLiteral("precedent")}});
    CHECK(retour.value(QStringLiteral("ok")).toBool());
    CHECK(KdenliveSettings::koracut_theme_mode() == QStringLiteral("dark"));
    CHECK(KoraCutThemes::trackColor(QStringLiteral("piste_video"), Qt::gray) == QColor(Qt::gray));
    CHECK(call("theme_appliquer", {{QStringLiteral("nom"), QStringLiteral("licorne")}}).value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() ==
          QStringLiteral("theme_inconnu"));

    // Matrix : police à chasse fixe et coins droits.
    call("theme_appliquer", {{QStringLiteral("nom"), QStringLiteral("matrix")}});
    // Depuis le 30 septembre 2026, la chasse fixe est la police embarquée (koracutfonts.h), la même partout.
    CHECK(QApplication::font().family() == KoraCutFonts::code().family());
    call("theme_appliquer", {{QStringLiteral("nom"), QStringLiteral("sombre")}});
    CHECK(QApplication::font().family() != KoraCutFonts::code().family());

    // Un thème créé, illisible au départ : corrigé, appliqué, modifié, supprimé.
    const QJsonObject cree = call("theme_creer", {{QStringLiteral("nom"), QStringLiteral("Essai moteur")},
                                                  {QStringLiteral("couleurs"), QJsonObject{{QStringLiteral("fond"), QStringLiteral("#000000")},
                                                                                           {QStringLiteral("texte"), QStringLiteral("#222222")},
                                                                                           {QStringLiteral("accent"), QStringLiteral("#00FF41")}}},
                                                  {QStringLiteral("inspiration"), QStringLiteral("essai")}});
    REQUIRE(cree.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(cree.value(QStringLiteral("corrections")).toArray().isEmpty());
    CHECK(cree.value(QStringLiteral("controles")).toObject().value(QStringLiteral("passes")).toInt() ==
          cree.value(QStringLiteral("controles")).toObject().value(QStringLiteral("total")).toInt());
    const QString id = cree.value(QStringLiteral("id")).toString();
    CHECK(id == QStringLiteral("KoraCutPersoEssaiMoteur"));
    CHECK(KdenliveSettings::koracut_theme_mode() == id);
    CHECK(qApp->styleSheet().contains(QStringLiteral("#00FF41")));
    const QJsonObject modifie = call("theme_modifier", {{QStringLiteral("couleurs"), QJsonObject{{QStringLiteral("accent"), QStringLiteral("#FF5A1F")}}}});
    CHECK(modifie.value(QStringLiteral("id")).toString() == id);
    CHECK(qApp->styleSheet().contains(QStringLiteral("#FF5A1F")));
    CHECK(KoraCutThemes::token(QStringLiteral("accent")) == QColor(0xFF5A1F));
    CHECK(call("theme_creer", {{QStringLiteral("nom"), QStringLiteral("Vide")}, {QStringLiteral("couleurs"), QJsonObject{{QStringLiteral("brillance"), QStringLiteral("#FFFFFF")}}}})
              .value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("entree_invalide"));
    // La copie d'un thème livré garde les écarts de la charte qu'on ne touche pas.
    const QJsonObject copie = call("theme_modifier", {{QStringLiteral("nom"), QStringLiteral("sombre")}, {QStringLiteral("couleurs"), QJsonObject{{QStringLiteral("fond"), QStringLiteral("#181818")}}}});
    const QString copieId = copie.value(QStringLiteral("id")).toString();
    CHECK(copieId.startsWith(QStringLiteral("KoraCutPerso")));
    CHECK(copieId.endsWith(QStringLiteral("Perso")));
    CHECK(KoraCutThemes::token(QStringLiteral("texte_sur_accent")) == QColor(0xFFFFFF));
    CHECK(call("theme_supprimer", {{QStringLiteral("nom"), QStringLiteral("matrix")}}).value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() ==
          QStringLiteral("fichier_protege"));
    CHECK(call("theme_supprimer", {{QStringLiteral("nom"), copieId}}).value(QStringLiteral("ok")).toBool());
    CHECK(call("theme_supprimer", {{QStringLiteral("nom"), QStringLiteral("Essai moteur")}}).value(QStringLiteral("ok")).toBool());
    CHECK(UiUtils::themeFile(id).isEmpty());

    // Les réglages : liste blanche, bornes, écriture puis relecture, retour à la valeur d'avant.
    const QJsonObject reglages = call("reglages_lister", {{QStringLiteral("section"), QStringLiteral("timeline")}});
    REQUIRE(reglages.value(QStringLiteral("ok")).toBool());
    CHECK(reglages.value(QStringLiteral("reglages")).toArray().size() >= 5);
    const bool magnetisme = KdenliveSettings::snaptopoints();
    const QJsonObject ecrit = call("reglages_ecrire", {{QStringLiteral("cle"), QStringLiteral("snaptopoints")}, {QStringLiteral("valeur"), !magnetisme}});
    CHECK(ecrit.value(QStringLiteral("ok")).toBool());
    CHECK(ecrit.value(QStringLiteral("avant")).toBool() == magnetisme);
    CHECK(KdenliveSettings::snaptopoints() == !magnetisme);
    call("reglages_ecrire", {{QStringLiteral("cle"), QStringLiteral("snaptopoints")}, {QStringLiteral("valeur"), magnetisme}});
    CHECK(KdenliveSettings::snaptopoints() == magnetisme);
    CHECK(call("reglages_ecrire", {{QStringLiteral("cle"), QStringLiteral("autosave_time")}, {QStringLiteral("valeur"), 5}})
              .value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("entree_invalide"));
    CHECK(call("reglages_ecrire", {{QStringLiteral("cle"), QStringLiteral("image_duration")}, {QStringLiteral("valeur"), QStringLiteral("5 s")}})
              .value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("entree_invalide"));
    CHECK(call("reglages_ecrire", {{QStringLiteral("cle"), QStringLiteral("defaultimageapp")}, {QStringLiteral("valeur"), QStringLiteral("/bin/sh")}})
              .value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("reglage_inconnu"));
    const QJsonObject lu = call("reglages_lire", {{QStringLiteral("cles"), QJsonArray{QStringLiteral("autosave_time"), QStringLiteral("image_duration")}}});
    CHECK(lu.value(QStringLiteral("reglages")).toArray().size() == 2);

    KdenliveSettings::setKoracut_theme_mode(modeAvant);
    UiUtils::applyThemeMode();
}
