/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDialog>
#include <QMap>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

/** @brief Fenêtre « Baisse automatique de la musique ».
 *
 * On dit pour chaque piste audio si c'est de la voix, de la musique ou rien. KoraCut lit les
 * niveaux déjà calculés pour les formes d'onde, repère les passages de voix
 * (KoraCutDucking::duckedIntervals) et pose un effet Volume à images clés sur chaque clip de
 * musique concerné. Tout part dans une seule étape d'annulation. « Retirer la baisse » enlève
 * ces effets-là, et seulement eux (propriété kdenlive:koracut_ducking).
 */
class KoraCutDuckingDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KoraCutDuckingDialog(QWidget *parent = nullptr);

private:
    void apply();
    void removeDucking();
    void restoreDefaults();
    void saveSettings();

    QMap<int, QComboBox *> m_roles; ///< id de piste -> rôle choisi
    QDoubleSpinBox *m_duck{nullptr};
    QDoubleSpinBox *m_threshold{nullptr};
    QSpinBox *m_anticipation{nullptr};
    QSpinBox *m_hold{nullptr};
    QSpinBox *m_fade{nullptr};
    QSpinBox *m_mergeGap{nullptr};
    QLabel *m_result{nullptr};
};
