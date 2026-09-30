/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <KNSCore/Entry>
#include <QDialog>

class QLabel;

/** @brief Fenêtre « Extensions » : toutes les sources de téléchargement au même endroit.
 *
 * Demande d'Amadou Ba du 22 septembre 2026 : le téléchargement d'extensions était caché, éparpillé
 * dans la barre d'outils de chaque panneau (effets, transitions, titres, rendu, raccourcis). Cette
 * fenêtre ne remplace rien : elle ouvre les mêmes sources KNewStuff (data/knewstuff/*.knsrc),
 * côte à côte, plus les thèmes, qui s'installent depuis un fichier. Voir le document 14, § 3.
 */
class KoraCutExtensionsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KoraCutExtensionsDialog(QWidget *parent = nullptr);

Q_SIGNALS:
    /** @brief Des extensions ont été installées ou retirées depuis la source @p knsrc : à recharger
     * tout de suite (23 septembre 2026 ; avant, il fallait relancer KoraCut). */
    void installed(const QString &knsrc, const QList<KNSCore::Entry> &entries);

private:
    void browse(const QString &knsrc);
    QLabel *m_status{nullptr};
};
