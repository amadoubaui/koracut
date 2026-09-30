/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

namespace KoraCutMotion {
struct Composition;
}

/** @brief Le catalogue des compositions d'un seul élément (lot 5 du document 19).
 *
 * Demande d'Amadou Ba du 24 septembre 2026 : HyperFrames ne fait que ce qu'on lui demande. Une
 * animation = cette animation seule, un clip ; une transition = la transition seule, posée à cheval
 * sur la coupe. Chaque élément vit donc dans sa propre petite composition (`motion/<nom>/index.html`),
 * rendue en alpha et posée comme un seul clip. Ce fichier écrit ces compositions : les gabarits des
 * éléments (titre, bandeau, compteur, logo, légende), le catalogue des transitions (cordes coupées,
 * balayage à 32°, secousse, voile, éclair), et l'enveloppe d'un fragment écrit par un agent.
 *
 * Toutes ont une seule timeline GSAP en pause, un fond transparent, des polices système, et les
 * attributs propres à KoraCut sur la racine (`data-koracut-element`, `-style`, `-start`, `-cut`), que
 * HyperFrames ignore et que l'empreinte ne regarde pas. Une transition couvre toute l'image en son
 * milieu : la coupe se fait derrière.
 */
namespace KoraCutMotionCatalog {

/** Ce qu'il faut pour écrire une composition d'un seul élément. */
struct Spec
{
    QString id;          ///< data-composition-id (le nom propre du projet Motion)
    QString element;     ///< titre, bandeau, compteur, logo, legende, transition, autre
    QString style;       ///< un style du catalogue, vide : le premier de la sorte
    QString text;        ///< le texte (bandeau : « Nom | Fonction » ou deux lignes)
    QString file;        ///< logo : chemin relatif dans le dossier de la composition (assets/…)
    QString accent = QStringLiteral("#7C5CFF");
    QString textColor = QStringLiteral("#FFFFFF");
    QString background = QStringLiteral("#2C2C2C");
    int width = 1920;
    int height = 1080;
    double fps = 25.;
    double duration = 3.;  ///< en secondes
    double start = 0.;     ///< place dans la timeline, en secondes (data-koracut-start)
    double cut = -1.;      ///< transition : la coupe suivie, en secondes
    QString sequence;      ///< la séquence (uuid sans accolades) où vit l'élément : data-koracut-sequence
};

/** Les sortes d'éléments que le catalogue sait écrire (sans « transition » ni « autre »). */
QStringList elementKinds();
/** Les styles d'une sorte ; le premier est celui par défaut. */
QStringList elementStyles(const QString &element);
/** Les styles de transition ; le premier est celui par défaut. */
QStringList transitionStyles();
/** Durée conseillée d'une transition de ce style, en secondes. */
double transitionDefaultDuration(const QString &style);

/** @brief Écrit la composition d'un élément depuis le catalogue. Vide, et @p error dit pourquoi, si la
 * sorte ou le style est inconnu, ou s'il manque le texte (ou le fichier d'un logo). */
QString elementHtml(const Spec &spec, QString *error = nullptr);
/** @brief Écrit une transition du catalogue. */
QString transitionHtml(const Spec &spec, QString *error = nullptr);

/** @brief Le HTML d'un agent devient une composition d'un seul élément.
 * Une composition complète (racine `data-composition-id`) garde tout, sa racine reçoit les dimensions,
 * la cadence, la durée et les attributs de KoraCut. Un fragment est enveloppé : ses `<style>` vont dans
 * la tête, ses `<script>` s'exécutent avec `tl` (la timeline en pause déjà créée) et `root` (la racine),
 * le reste va dans la racine. Vide si le HTML contient plusieurs éléments temporels (`data-start`). */
QString wrapBrief(const QString &brief, const Spec &spec, QString *error = nullptr);

/** @brief Le nom montré (chutier, timeline, panneau) : « Titre · Big Buck Bunny », « Transition · cordes ».
 * Une composition ordinaire garde le nom de son projet. */
QString readableName(const KoraCutMotion::Composition &composition, const QString &project);
/** @brief Le nom d'une sorte, pour l'écran. */
QString kindLabel(const QString &element);
/** @brief Le format d'image en clair : « 16:9 », « 9:16 », « 1:1 », sinon « 1920×1080 ». Vide sans dimensions. */
QString formatLabel(int width, int height);
/** @brief Le nom du média dans le chutier : le nom lisible sans la sorte générique « Élément »,
 * suivi du format (« Busha · 16:9 », « Titre · Sintel · 9:16 »). Deux rendus d'un même élément,
 * un par format de séquence, se distinguent ainsi (25 septembre 2026). */
QString binClipName(const KoraCutMotion::Composition &composition, const QString &project);

} // namespace KoraCutMotionCatalog
