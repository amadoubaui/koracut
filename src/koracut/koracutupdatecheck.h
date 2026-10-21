/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QString>

class QObject;

/** @brief « Une nouvelle version de KoraCut existe » pour ceux qui l'ont téléchargée (demande KC-009, 21 octobre 2026).
 *
 * Le paquet publié porte sa date (`koracut-paquet.json`, écrit par paquet.sh dans les données de l'application).
 * Une fois par jour au plus, vingt secondes après le lancement, KoraCut lit `https://koracut.org/version.json` ;
 * quand la date publiée est plus récente, un message du chutier propose de la télécharger. Rien n'est envoyé
 * d'autre que la demande du fichier. Une version compilée à la main (sans `koracut-paquet.json`) ne vérifie rien :
 * elle a son propre bandeau (MainWindow::koraCutRelay). Réglage `[KoraCut] verifierMisesAJour` (oui par défaut).
 */
namespace KoraCutUpdateCheck {
/** @brief Ce que le paquet dit de lui : version et date (« 2026.10.21 »), vide hors paquet.
 *  KORACUT_PAQUET_DATE remplace la date, pour les essais. */
QJsonObject packageInfo();
/** @brief Vrai quand @p published (« 2026.10.21 ») est plus récente que @p installed. */
bool isNewer(const QString &published, const QString &installed);
/** @brief L'adresse du fichier lu (KORACUT_MAJ_URL la remplace, file:// accepté). */
QString feedUrl();
/** @brief Lance le contrôle plus tard, si le paquet a une date et que le réglage le permet.
 *  @p force : ne pas attendre 24 heures depuis le dernier contrôle (essais). */
void schedule(QObject *parent, bool force = false);
}
