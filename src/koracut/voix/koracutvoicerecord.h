/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;
class QAbstractButton;
class KoraCutVoiceLevel;

/** @brief La voix off rapide, comme CapCut (29 septembre 2026, demande d'Amadou Ba).
 *
 *  Un micro dans la barre d'outils de la timeline ouvre ce petit panneau flottant : le micro choisi,
 *  le niveau, « Couper le son du projet » (coché par défaut : ce qui joue dans la timeline ne sort
 *  pas des haut-parleurs pendant qu'on parle, le micro ne le reprend donc pas), le compte à rebours,
 *  et un gros bouton rouge. La voix se pose à la tête de lecture, sur une piste « Voix » libre ou
 *  une nouvelle piste. Tout le reste (capture, compte à rebours sur le moniteur, pose du clip,
 *  annulation) est le moteur d'enregistrement de Kdenlive (MediaCapture, TimelineController).
 *
 *  Léger : aucun minuteur ne tourne tant qu'on n'enregistre pas. */
class KoraCutVoiceRecord : public QFrame
{
    Q_OBJECT

public:
    /** @brief Le panneau unique de la fenêtre (créé à la demande). */
    static KoraCutVoiceRecord *instance(QWidget *window);
    /** @brief Ouvre le panneau sous (ou au-dessus de) @p anchor, ou le ferme s'il est déjà ouvert et qu'on n'enregistre pas. */
    void toggleNear(QWidget *anchor);
    bool isRecording() const;
    /** @brief Lance ou arrête l'enregistrement (le bouton rouge, ou le micro de la barre pendant qu'on enregistre). */
    void startOrStop();

Q_SIGNALS:
    void recordingChanged(bool recording);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    explicit KoraCutVoiceRecord(QWidget *window);
    void start();
    void stop();
    void tick();
    void recordStateChanged(int tid, bool recording);
    void finish();
    void refreshDevices();
    void updateUi();
    void placeNear(QWidget *anchor);

    QPointer<QWidget> m_window;
    QComboBox *m_device{nullptr};
    KoraCutVoiceLevel *m_level{nullptr};
    QCheckBox *m_mute{nullptr};
    QCheckBox *m_countdown{nullptr};
    QAbstractButton *m_record{nullptr};
    QLabel *m_state{nullptr};
    QLabel *m_time{nullptr};
    QTimer *m_timer{nullptr};
    QElapsedTimer m_since;
    int m_track{-1};
    /** Demandé et pas encore fini (compte à rebours compris). */
    bool m_active{false};
    /** La capture a vraiment commencé (après le compte à rebours). */
    bool m_capturing{false};
    bool m_muted{false};
};
