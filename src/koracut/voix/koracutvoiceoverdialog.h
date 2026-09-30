/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QPointer>

class KoraCutVoiceOver;
class QAudioOutput;
class QCheckBox;
class QComboBox;
class QLabel;
class QMediaPlayer;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

/** @brief « Générer la voix off » depuis le panneau Script (chantier 5, 29 septembre 2026).
 *
 * Les phrases viennent du script (une ligne, une sélection, ou tout le script scène par scène).
 * On choisit le moteur, la voix, le ton et la vitesse, on génère (en tâche de fond, la fenêtre
 * reste libre et la lecture continue), on écoute chaque voix, puis on la pose sur la piste
 * « Voix off » en une seule étape d'annulation : à la scène reliée quand la ligne a un marqueur,
 * sinon à la suite depuis la tête de lecture. Les voix qu'on n'a pas gardées sont effacées à la
 * fermeture : rien ne traîne sur le disque.
 */
class KoraCutVoiceOverDialog : public QDialog
{
    Q_OBJECT
public:
    struct Item
    {
        QString label;
        QString text;
        /** L'image de la scène reliée (marqueur ou zone), -1 sans lien. */
        int position{-1};
    };

    explicit KoraCutVoiceOverDialog(const QList<Item> &items, QWidget *parent = nullptr);
    ~KoraCutVoiceOverDialog() override;

    /** @brief Pour les essais sans souris : l'état de chaque ligne, une ligne de texte. */
    QString debugState() const;
    /** @brief Pour les essais sans souris : lance la génération. */
    void startGeneration();
    /** @brief Pour les essais sans souris : pose ce qui est prêt. */
    void placeReady(bool onTimeline);

protected:
    void reject() override;

private:
    struct Row
    {
        Item item;
        QTreeWidgetItem *tree{nullptr};
        QString file;
        QJsonObject result;
        bool imported{false};
    };

    void refreshEngine();
    void refreshVoices();
    void setBusy(bool busy);
    void onPhrase(const QJsonObject &phrase);
    void play(int row);
    void deleteUnkept();
    QString currentEngine() const;

    QList<Row> m_rows;
    KoraCutVoiceOver *m_job{nullptr};
    QMediaPlayer *m_player{nullptr};
    QAudioOutput *m_audio{nullptr};
    int m_playing{-1};

    QWidget *m_notice{nullptr};
    QLabel *m_noticeText{nullptr};
    QPushButton *m_installButton{nullptr};
    QProgressBar *m_installBar{nullptr};
    QComboBox *m_engine{nullptr};
    QComboBox *m_voice{nullptr};
    QComboBox *m_tone{nullptr};
    QSpinBox *m_speed{nullptr};
    QSpinBox *m_takes{nullptr};
    QTreeWidget *m_list{nullptr};
    QLabel *m_status{nullptr};
    QProgressBar *m_progress{nullptr};
    QPushButton *m_generate{nullptr};
    QPushButton *m_place{nullptr};
    QPushButton *m_binOnly{nullptr};
    QPushButton *m_close{nullptr};
};
