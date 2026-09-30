/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

#include "renderpresets/renderpresetmodel.hpp"

/** @brief Le module Export comme la page Deliver de DaVinci Resolve (document 23).
 *
 * Les modèles (YouTube, TikTok, Master H.264…) viennent de data/koracut-export-models.json.
 * Un modèle donne des réglages par défaut (Settings) ; la personne les change dans les onglets
 * Vidéo, Audio et Fichier, et ses choix sont gardés dans le projet (propriété koracut:export_reglages,
 * une entrée par séquence). Settings::params() donne les paramètres du consumer avformat de MLT,
 * que RenderRequest pose dans le fichier .mlt rendu par koracut_render.
 */
namespace KoraCutExport {

struct Model
{
    QString id;
    QString name;
    QString detail;
    bool platform{false}; ///< plateforme (vrai logo) ou modèle technique (badge)
    QString logo;         ///< nom du SVG dans :/koracut/plateformes/, vide pour un badge
    QColor logoColor;
    QColor logoBackground; ///< fond de la pastille, invalide : le logo seul
    QString badge;         ///< texte du badge d'un modèle technique (H.264, WAV…)
    int width{0};          ///< taille visée, 0 : comme la séquence
    int height{0};
    QString orientation; ///< h, v, c (carré), p (portrait 4:5), vide : toutes
    int maxFps{0};
    QString container; ///< mp4, mov, wav, m4a, mp3, gif, png
    QString video;     ///< h264, h265, prores_hq, prores_4444, gif, png, vide : sans image
    QString profile;
    int bitrateKbps{0}; ///< plafond, 0 : Automatique
    int bitrateKbps60{0};
    QString audio; ///< aac, pcm24, pcm16, mp3, vide : sans son
    int audioKbps{192};
    int sampleRate{48000};
    int lufs{0};
    double peak{-1.};
    int maxSeconds{0};
    int maxMegabytes{0};
    QString guides; ///< repères du moniteur (koracut-reperes-plateformes.json)
    QStringList legacyIds;
    bool isValid() const { return !id.isEmpty(); }
    bool hasVideo() const { return !video.isEmpty(); }
    bool hasAudio() const { return !audio.isEmpty(); }
    bool isStill() const { return video == QLatin1String("png"); }
};

const QList<Model> &models();
/** @brief Le modèle par son identifiant, ou par un ancien identifiant des outils d'agent (reels_tiktok_9x16…). */
const Model *model(const QString &id);

/** @brief Les réglages d'un export, tels que les montre la page (onglets Vidéo, Audio, Fichier). */
struct Settings
{
    QString model;
    bool video{true};
    bool audio{true};
    QString container;
    QString videoCodec;
    bool hardware{false}; ///< encodeur matériel (VideoToolbox sur Mac) si disponible
    QString profile;
    int width{0}; ///< 0 : comme la séquence
    int height{0};
    int bitrateKbps{0}; ///< 0 : Automatique, sinon « Restreindre à »
    int keyframes{0};   ///< 0 : automatique, sinon une image clé toutes les N images
    bool reorder{true}; ///< réorganisation des images (images B)
    QString audioCodec;
    int audioKbps{192};
    int sampleRate{48000};
    int channels{2};
    bool normalize{false};
    int lufs{-14};
    double peak{-1.};
    QString subtitles{QStringLiteral("incruster")}; ///< incruster, sans, fichier (un .srt à côté)
    QString folder;                                 ///< vide : Exports/ à côté du projet
    QString name{QStringLiteral("{projet} - {modele}")};
    bool inOut{false}; ///< Exporter : Toute la timeline (false) ou Sélection Entrée/Sortie
    bool customized{false}; ///< la personne a changé un réglage du modèle

    QJsonObject toJson() const;
    static Settings fromJson(const QJsonObject &o);
    /** @brief Les réglages par défaut d'un modèle pour une séquence de cette taille et cadence. */
    static Settings forModel(const Model &m, const QSize &sequence, double fps);

    QString extension() const;
    /** @brief Les paramètres du consumer avformat. */
    RenderPresetParams params(const QSize &sequence, double fps) const;
    /** @brief La taille du fichier rendu : celle demandée, ou celle de la séquence. */
    QSize outputSize(const QSize &sequence) const;
    /** @brief Débit estimé en kb/s (image et son), pour la taille estimée. */
    double estimatedKbps(const QSize &sequence, double fps) const;
};

/** @brief Remplace {projet}, {sequence}, {date}, {heure}, {format}, {modele}, {version} ; retire ce qu'un nom de fichier ne supporte pas. */
QString expandName(const QString &pattern, const Settings &s, const QString &project, const QString &sequence, int version = 1);
/** @brief Le dossier par défaut : Exports/ à côté du projet enregistré, sinon ~/Movies/KoraCut/Exports. */
QString defaultFolder();
/** @brief Le chemin complet d'un export : dossier, nom et extension, avec {version} poussée jusqu'à un fichier libre. */
QString outputPath(const Settings &s, const QString &project, const QString &sequence);

/** @brief L'encodeur matériel de ce codec (h264 ou h265) sur cette machine : h264_videotoolbox sur Mac,
 *  h264_nvenc, h264_qsv, h264_amf ou h264_vaapi ailleurs ; vide s'il n'y en a pas. */
QString hardwareEncoder(const QString &videoCodec);
/** @brief Vrai si l'encodeur matériel de ce codec existe sur la machine (h264_videotoolbox…). */
bool hardwareAvailable(const QString &videoCodec);

/** @brief Le modèle par défaut d'un type de vidéo du guide, pour une séquence de cette orientation. */
QString modelForVideoType(const QString &typeId, const QSize &sequence);
/** @brief Le modèle par défaut quand le projet n'a pas de type : YouTube pour du 16:9, TikTok pour du 9:16. */
QString modelForSize(const QSize &sequence);
/** @brief Vrai si le modèle vise ce rapport d'image (ou n'en vise aucun). */
bool modelFits(const Model &m, const QSize &sequence);

/** @brief Les réglages gardés dans le projet pour cette séquence (uuid), sinon ceux du type, sinon ceux du format. */
Settings documentSettings(const QString &sequenceUuid, const QSize &sequence, double fps);
/** @brief Garde les réglages de la séquence dans le projet (koracut:export_reglages), sans étape d'annulation. */
void saveDocumentSettings(const QString &sequenceUuid, const Settings &s);

/** @brief Ce que rend enqueue() : les rendus ajoutés, ou une erreur (code des outils d'agent). */
struct Enqueued
{
    bool ok{false};
    QString code;
    QString message;
    QString path;
    QStringList jobIds;
    QString subtitleFile;
    int frames{0};
    bool still{false}; ///< image PNG écrite tout de suite, sans passer par la file
};

/** @brief Prépare le rendu de la séquence courante avec ces réglages et l'ajoute à la file.
 * @p held : « Ajouter à la file » (attend « Tout exporter ») ; sinon le rendu part dès que la file est libre.
 * @p path vide : outputPath(). @p in, @p out (images, out compris) : -1 pour suivre s.inOut. */
Enqueued enqueue(const Settings &s, bool held, const QString &path = QString(), int in = -1, int out = -1);

/** @brief Le nom de la séquence courante et celui du projet, pour les noms de fichiers. */
QString currentSequenceName();
QString currentProjectName();

/** @brief Retire les playlists koracut-*.mlt de plus d'un jour laissées dans le dossier temporaire. */
int cleanTemporaryFiles();

} // namespace KoraCutExport
