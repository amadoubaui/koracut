/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QString>

class KdenliveDoc;

/** @brief Les projets Motion d'un projet KoraCut : un dossier HyperFrames par composition.
 *
 * `<dossier du projet>/motion/<nom>/index.html`, avec `compositions/` et `assets/` (document 19, § 2).
 * Le miroir crée toujours un projet complet, même pour une composition seule : `snapshot`,
 * `compositions --json` et Studio exigent un index.html à la racine.
 */
namespace KoraCutMotionProject {

struct Info
{
    QString name;
    QString dir;
    QString indexPath;
};

/** @brief Un nom de dossier sûr à partir d'un nom saisi (lettres, chiffres, tirets). */
QString sanitizeName(const QString &name);

/** @brief Le dossier `motion/` du projet KoraCut. Projet enregistré : à côté du fichier ; sinon, dans
 * le dossier des données du projet, sous l'identifiant du document. */
QString motionRoot(KdenliveDoc *doc);

/** @brief Le dossier de cache des rendus d'un projet Motion : `cache/motion/<nom>/`. */
QString cacheDir(KdenliveDoc *doc, const QString &name);

/** @brief Les projets Motion présents dans @p root (un dossier avec un index.html). */
QList<Info> list(const QString &root);

/** @brief Crée un projet depuis le gabarit vide, aux dimensions et i/s données.
 * @return le chemin de l'index.html, vide en cas d'échec (@p error dit pourquoi). */
QString create(const QString &root, const QString &name, int width, int height, double fps, QString *error = nullptr);

/** @brief Importe un dossier HyperFrames (avec son index.html) ou un fichier .html seul.
 * Un fichier seul devient un projet : il est copié en index.html et les fichiers locaux qu'il cite
 * (scripts, feuilles de style, médias, polices des feuilles) viennent avec lui, les « ../ » du
 * chemin retirés. @return le chemin de l'index.html, vide en cas d'échec. */
QString import(const QString &root, const QString &source, const QString &name, QString *error = nullptr);

/** @brief Le gabarit d'un index.html minimal. */
QString templateHtml(const QString &compositionId, int width, int height, double fps);

/** @brief Copie récursive d'un dossier. */
bool copyDirectory(const QString &from, const QString &to);

/** @brief Le fichier projet dont @p cacheFolder est le dossier de cache (le témoin `.<chemin>.koracut` que
 * KoraCut y pose), vide s'il n'y en a pas. */
QString projectFileOfCache(const QString &cacheFolder);

/** @brief Vide un dossier de cache de projet sans toucher à ses rendus Motion (`motion/`) : ils coûtent un
 * rendu Chrome chacun et le projet s'en sert encore (15 octobre 2026). Le dossier lui-même et le témoin du
 * fichier projet restent.
 * @return vrai si tout ce qui devait partir est parti. */
bool clearCacheKeepingMotion(const QString &cacheFolder);

} // namespace KoraCutMotionProject
