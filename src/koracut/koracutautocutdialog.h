/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDialog>
#include <QMap>

#include "koracutautocut.h"

class KoraCutTranscriber;
class QPushButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

/** @brief Fenêtre « Coupe automatique » (type AutoCut) : les silences (lot 1) et les mots parasites (lot 2).
 *
 * On choisit les pistes de voix, un rythme ou des réglages fins, et ce qu'on fait des silences :
 * les marquer pour relire (le défaut, rien n'est coupé), couper et refermer, couper en laissant
 * les trous, ou rendre muet. Tout part dans une seule étape d'annulation. Les silences marqués se
 * coupent ensuite d'un bouton, une fois relus. Voir « 15 - Coupe automatique (type AutoCut).md ».
 */
class KoraCutAutoCutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KoraCutAutoCutDialog(QWidget *parent = nullptr);

private:
    enum Mode { Marquer = 0, CouperRefermer, CouperTrous, RendreMuet };
    void applyRythme(int index);
    void analyseAndApply();
    /** @brief Suite de l'analyse une fois les mots connus (ou sans mots si on ne cherche pas les « euh »). */
    void applyResults(const QList<KoraCutAutoCut::Word> &words);
    void setBusy(bool busy);
    void cutMarked();
    void removeMarks();
    void saveSettings();

    QMap<int, QCheckBox *> m_tracks; ///< id de piste audio -> analysée ou non
    QComboBox *m_rythme{nullptr};
    QCheckBox *m_autoThreshold{nullptr};
    QDoubleSpinBox *m_threshold{nullptr};
    QSpinBox *m_minSilence{nullptr};
    QSpinBox *m_minSpeech{nullptr};
    QSpinBox *m_padBefore{nullptr};
    QSpinBox *m_padAfter{nullptr};
    QComboBox *m_mode{nullptr};
    QCheckBox *m_moveGuides{nullptr};
    QCheckBox *m_fillers{nullptr};
    QCheckBox *m_ambiguous{nullptr};
    QCheckBox *m_repeats{nullptr};
    QCheckBox *m_sync{nullptr};
    class QLineEdit *m_sureWords{nullptr};
    class QLineEdit *m_suspectWords{nullptr};
    class QLineEdit *m_ambiguousWords{nullptr};
    QSpinBox *m_repeatMismatches{nullptr};
    QSpinBox *m_repeatWindow{nullptr};
    QCheckBox *m_cameras{nullptr};
    QMap<int, QComboBox *> m_cameraFor; ///< piste de voix -> choix de la piste vidéo de sa caméra
    QComboBox *m_wide{nullptr};
    QDoubleSpinBox *m_minShot{nullptr};
    QDoubleSpinBox *m_maxShot{nullptr};
    QString m_cameraReport;
    /** @brief Monte les caméras selon qui parle (lot 5), avant les coupes. */
    void applyCameras();
    QString m_syncReport;
    QLabel *m_modelLabel{nullptr};
    KoraCutTranscriber *m_transcriber{nullptr};
    QList<QPushButton *> m_buttons;
    // Ce que l'analyse a trouvé avant la transcription, gardé pour la suite.
    QList<int> m_voiceTracks;
    QVector<double> m_levels;
    double m_usedThreshold{-40.};
    QList<KoraCutAutoCut::Interval> m_silences;
    int m_missing{0};
    QLabel *m_result{nullptr};
    bool m_fillingRythme{false};
};
