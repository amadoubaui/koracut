/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDomDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>

/**
 * @brief Le niveau sonore à l'export (document 16, « L'export préparé »).
 *
 * Une seule brique pour la fenêtre Export, l'outil d'agent `exporter` et `mesurer_audio` :
 * - measure() lit le LUFS intégré d'une copie MLT de la timeline (filtre `loudness_meter`) ;
 * - prepare() mesure le mixage de la zone rendue, puis pose sur le tractor principal du rendu un gain
 *   fixe « cible moins mesure » (filtre `volume`), et un limiteur à -1 dBTP si la crête le demande.
 *   Si la mesure échoue (silence, module absent), on repose l'ancien filtre adaptatif `dynamic_loudness`
 *   et on le dit.
 */
namespace KoraCutLoudness {

/** Le niveau d'un silence, en dB. */
constexpr double kSilenceDb = -70.;

struct Measure
{
    double lufs = kSilenceDb;         // LUFS intégré (programme)
    double shortTermMax = kSilenceDb; // LUFS court terme, le plus fort
    double range = 0.;                // plage de sonie, en LU
    double truePeak = kSilenceDb;     // crête vraie, en dBTP
    double rms = kSilenceDb;          // niveau RMS, en dBFS
    int frames = 0;                   // images lues
    bool silent = true;               // rien à entendre
    bool ok = false;                  // la mesure a pu être faite
};

/**
 * @brief Mesure une timeline entre deux images (fin comprise).
 * @param xml la scène MLT (sceneList) de la timeline, lue dans le fil principal
 * @param fps la cadence du projet
 * @param mltTracks les pistes entendues, par leur index MLT (vide : toutes)
 * @param muteMixer coupe le gain et le panoramique de piste (la tranche du mixeur)
 * Peut tourner dans un fil de travail : elle ne touche ni au modèle ni à l'interface.
 */
Measure measure(const QString &xml, double fps, int start, int endInclusive, const QSet<int> &mltTracks = {}, bool muteMixer = false);

/**
 * @brief La même mesure, après un gain et, si @p limiter, le limiteur de l'export : ce que le fichier
 * rendu contiendra. Sert à vérifier le plan avant de le poser (28 septembre 2026).
 */
Measure measureTreated(const QString &xml, double fps, int start, int endInclusive, double gainDb, bool limiter);

/** Ce qui a été posé sur le rendu. */
struct Plan
{
    int target = 0;            // cible en LUFS
    bool measured = false;     // la mesure a réussi : gain fixe posé
    double lufs = kSilenceDb;  // LUFS intégré mesuré avant le gain
    double truePeak = kSilenceDb;
    double gainDb = 0.;        // le gain posé
    bool limiter = false;      // un limiteur a été posé après le gain
    QString limiterService;    // son service MLT (avfilter.alimiter ou avfilter.acompressor)
    bool verified = false;     // le résultat a été mesuré avec le gain et le limiteur
    double lufsAfter = kSilenceDb;     // LUFS intégré attendu dans le fichier
    double truePeakAfter = kSilenceDb; // crête vraie attendue, avant encodage
    bool fallback = false;     // repli sur dynamic_loudness
    QString note;              // ce qu'il faut savoir, en français simple
};

/**
 * @brief Mesure le mixage de la timeline courante entre @p in et @p out (fin comprise, -1 : fin du projet)
 * puis pose le gain et le limiteur sur le tractor principal de @p doc (la scène du rendu).
 * La mesure tourne dans un fil, l'interface reste vivante, la barre d'état dit « Mesure du niveau sonore… ».
 */
Plan prepare(QDomDocument &doc, int in, int out, int targetLufs);

/** Le plan en JSON, pour les réponses d'outils. */
QJsonObject planJson(const Plan &plan);

/** Le plan en une phrase (barre d'état, journal). */
QString planText(const Plan &plan);

} // namespace KoraCutLoudness
