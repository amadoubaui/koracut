/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutvideotypes.h"

#include <QDialog>
#include <QSize>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class KoraCutTypeListModel;

/** @brief Ce que l'assistant a décidé pour le projet neuf. */
struct KoraCutProjectPlan
{
    QString typeId;   ///< id du type de vidéo (vide : projet libre)
    QString name;     ///< nom du projet
    QSize frame;      ///< largeur × hauteur
    int fps{25};          ///< images par seconde, arrondies (29,97 donne 30)
    /** La cadence exacte en fraction, pour 23,976, 29,97 et 59,94 (24000/1001, 30000/1001,
     * 60000/1001). fpsNum à 0 : la cadence est `fps`, entière. */
    int fpsNum{0};
    int fpsDen{1};

    /** @brief Règle la cadence depuis un nombre (25, 29.97, 23.976…). Faux si elle ne se lit pas. */
    bool setRate(double rate)
    {
        if (!(rate >= 1.0) || rate > 240.0) {
            return false;
        }
        const int rounded = int(rate + 0.5);
        if (qAbs(rate - rounded) < 0.0005) {
            fps = rounded;
            fpsNum = 0;
            fpsDen = 1;
            return true;
        }
        // Les cadences de la télévision américaine : un entier × 1000/1001.
        if (qAbs(rate - rounded * 1000.0 / 1001.0) < 0.01) {
            fps = rounded;
            fpsNum = rounded * 1000;
            fpsDen = 1001;
            return true;
        }
        fps = rounded;
        fpsNum = int(rate * 1000.0 + 0.5);
        fpsDen = 1000;
        return true;
    }
    int rateNum() const { return fpsNum > 0 ? fpsNum : fps; }
    int rateDen() const { return fpsNum > 0 ? fpsDen : 1; }
    double rate() const { return double(rateNum()) / rateDen(); }
    QStringList videoTracks; ///< V1 d'abord
    QStringList audioTracks; ///< A1 d'abord
    int exportLufs{-14};
    QString exportCodec;
};

/** @brief « Nouveau projet » : choisir le type de vidéo, KoraCut règle le reste.
 *
 * Demande URGENTE d'Amadou Ba du 23 septembre 2026. Deux étapes :
 * 1. Quel genre de vidéo ? Une recherche, les familles en pastilles, des cartes avec la vignette
 *    du format (l'image 16:9, 9:16, 1:1, 4:5), le titre, le format et la durée, une ligne de
 *    description. Pas de liste de « 1080 » à lire.
 * 2. Ton projet : ce que KoraCut a réglé, à changer si on veut (nom, format, images par seconde,
 *    pistes), la cible d'export, trois conseils du guide, et « Commencer le montage ».
 * « Réglages avancés… » ouvre l'ancienne fenêtre de Kdenlive, pour qui la veut.
 */
class KoraCutNewProjectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KoraCutNewProjectDialog(QWidget *parent = nullptr);
    KoraCutProjectPlan plan() const;
    /** @brief Vrai si la personne a demandé l'ancienne fenêtre de réglages. */
    bool wantsAdvanced() const { return m_advanced; }

private:
    void buildChoosePage();
    void buildReviewPage();
    void chooseType(const QString &id);
    void refreshReview();
    void refreshTracks();

    QStackedWidget *m_pages{nullptr};
    // Étape 1
    QLineEdit *m_search{nullptr};
    QListView *m_cards{nullptr};
    KoraCutTypeListModel *m_model{nullptr};
    QList<QToolButton *> m_familyChips;
    QString m_family;
    // Étape 2
    QString m_typeId;
    QLabel *m_preview{nullptr};
    QLabel *m_typeTitle{nullptr};
    QLabel *m_typeDescription{nullptr};
    QLineEdit *m_name{nullptr};
    QComboBox *m_format{nullptr};
    QComboBox *m_fps{nullptr};
    QSpinBox *m_videoCount{nullptr};
    QSpinBox *m_audioCount{nullptr};
    QLabel *m_trackNames{nullptr};
    QLabel *m_export{nullptr};
    QLabel *m_tips{nullptr};
    QStringList m_videoNames;
    QStringList m_audioNames;
    int m_exportLufs{-14};
    QString m_exportCodec;
    bool m_advanced{false};
};
