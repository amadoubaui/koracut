/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

/** @brief Les types de vidéos du guide PIXEWORK, qui règlent un projet neuf.
 *
 * Demande URGENTE d'Amadou Ba du 23 septembre 2026 : créer un projet en choisissant le type de
 * vidéo qu'on veut faire, et que KoraCut règle tout (format, images par seconde, pistes, export).
 * Les 126 types viennent du guide « PIXEWORK_Guide_Types_Videos_2026 » (13 familles), transformés
 * par Sources/koracut-video-types/generer.py en data/koracut-video-types.json.
 */
namespace KoraCutVideoTypes {

struct Family
{
    QString code;
    QString name;
    QString description;
};

struct VideoType
{
    QString id;
    QString family;
    QString name;
    QString description;
    QString platforms;
    QString format; ///< horizontal, vertical, carre, portrait
    int fps{25};
    QString duration;
    int difficulty{1};
    QStringList videoTracks; ///< V1 d'abord
    QStringList audioTracks; ///< A1 d'abord
    int exportLufs{-14};
    QString exportCodec;
    QString exportModel;      ///< modèle d'export pour le format du type (data/koracut-export-models.json)
    QString exportModelOther; ///< modèle pour une séquence de l'autre sens (9:16 dans un projet 16:9)
    QStringList tips;
};

const QList<Family> &families();
const QList<VideoType> &all();
const VideoType *byId(const QString &id);

/** @brief Taille de l'image pour un format, en HD (les machines modestes d'abord). */
QSize frameSize(const QString &format);
/** @brief Nom court du format, pour l'afficher (« Vertical 9:16 »). */
QString formatLabel(const QString &format);
/** @brief Texte sans accents ni majuscules, pour chercher. */
QString searchable(const QString &text);

} // namespace KoraCutVideoTypes
