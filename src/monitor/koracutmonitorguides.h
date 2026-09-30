/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>

/** @brief La logique pure du moniteur KoraCut (document 20), sans widget, pour être testée.
 *
 * Trois choses : les repères de plateforme (lus dans data/koracut-reperes-plateformes.json),
 * l'aimantation d'un cadre aux bords, au centre, aux tiers et aux repères, et la graduation des
 * règles selon le zoom. Plus la conversion entre le zoom de Kdenlive (1 = l'image entière tient
 * dans le panneau) et le pourcentage affiché (100 % = un pixel du projet pour un pixel d'écran).
 */
namespace KoraCutMonitorGuides {

struct Zone
{
    QString id;
    QString label;
    /** Fractions du cadre de la plateforme, de 0 à 1. */
    QRectF rect;
    /** Si > 0, la zone n'existe que sur les N dernières secondes (écran de fin YouTube). */
    int lastSeconds{0};
};

struct Platform
{
    QString id;
    QString name;
    QStringList keywords;
    /** Largeur et hauteur du ratio (9, 16). */
    QSize ratio;
    QList<Zone> zones;
    /** La zone sûre, en fractions. */
    QRectF safe;
    bool isValid() const { return !id.isEmpty(); }
};

/** @brief Lit le fichier des plateformes (la ressource embarquée par défaut). */
QList<Platform> loadPlatforms(const QString &path = QString());

/** @brief La plateforme d'identifiant donné, ou une plateforme vide. */
Platform platform(const QString &id, const QList<Platform> &platforms);

/** @brief Le cadre de la plateforme dans un projet de cette taille : même ratio, centré, le plus grand possible. */
QRectF platformFrame(const Platform &platform, const QSize &project);

/** @brief Les zones à dessiner, en pixels du projet.
 *
 * Chaque entrée est une map : x, y, w, h (pixels), id, label, kind (« zone », « sure » ou
 * « outside » pour ce qui dépasse le cadre de la plateforme), lastFrames (0 ou le nombre
 * d'images de fin concernées).
 */
QVariantList platformZones(const Platform &platform, const QSize &project, double fps);

/** @brief L'identifiant de la première plateforme citée dans le texte « plateformes » d'un type de vidéo, ou vide. */
QString platformForVideoType(const QString &platformsText, const QList<Platform> &platforms);

struct SnapResult
{
    QRectF rect;
    /** La cible touchée, ou -1. */
    double lineX{-1};
    double lineY{-1};
};

/** @brief Aimante un cadre (bord gauche, centre, bord droit ; haut, centre, bas) aux cibles données. */
SnapResult snapRect(const QRectF &rect, const QList<double> &targetsX, const QList<double> &targetsY, double threshold);

/** @brief Les cibles d'un axe : les bords, le centre, les tiers, plus les repères. */
QList<double> axisTargets(double size, const QList<double> &guides);

/** @brief Le pas des graduations principales d'une règle (en pixels du projet) pour une échelle
 *  donnée (pixels d'écran par pixel du projet). Au moins 60 px d'écran entre deux chiffres. */
double rulerStep(double scale);

/** @brief Zoom Kdenlive vers pourcentage affiché. displayWidth = largeur de l'image à zoom 1. */
double zoomToPercent(double zoom, double displayWidth, int profileWidth);
double percentToZoom(double percent, double displayWidth, int profileWidth);

/** @brief Les crans du menu de zoom, en pourcentage. */
QList<int> zoomSteps();

} // namespace KoraCutMonitorGuides
