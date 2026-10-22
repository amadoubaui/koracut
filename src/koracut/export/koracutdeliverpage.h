/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// Le module Export comme la page Deliver de DaVinci Resolve (document 23). Trois panneaux :
// - « Modèles » (export_modeles) : le bandeau des modèles en tuiles, avec le vrai logo de chaque
//   plateforme, en haut sur toute la largeur ;
// - « Paramètres d'export » (export_reglages) : préréglage, nom du fichier, emplacement, plage, onglets
//   Vidéo / Audio / Fichier, taille estimée, « Ajouter à la file » et « Exporter tout de suite » ;
// - « File de rendu » (export_file) : les rendus, leur progression, le temps restant, « Tout exporter ».
// Le moniteur Programme et la timeline restent ceux du Montage, pour vérifier la zone.

#include "koracutexport.h"

#include <QPointer>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
class KoraCutExportQueuePanel;
class KoraCutExportSettingsPanel;
class KoraCutExportTiles;

namespace KoraCutExport {
/** @brief Le logo d'un modèle : pastille de la marque, logo seul, ou badge du codec. */
QPixmap modelIcon(const Model &m, int size, qreal dpr);
}

/** @brief Le coordinateur : les réglages de la séquence courante, gardés dans le projet. */
class KoraCutDeliverPage : public QObject
{
    Q_OBJECT
public:
    KoraCutDeliverPage(QWidget *window, KoraCutExportTiles *tiles, KoraCutExportSettingsPanel *settings, KoraCutExportQueuePanel *queue);
    static KoraCutDeliverPage *instance();

    const KoraCutExport::Settings &settings() const { return m_settings; }
    /** @brief Choisit un modèle : ses réglages par défaut remplacent ceux de la séquence. */
    void applyModel(const QString &id);
    /** @brief Remplace les réglages (la page les montre et les garde dans le projet). */
    void setSettings(const KoraCutExport::Settings &s, bool customized);
    /** @brief Relit les réglages de la séquence courante (changement de séquence ou de projet, outil d'agent). */
    void reload();
    /** @brief « Ajouter à la file » (held) ou « Exporter tout de suite ». */
    KoraCutExport::Enqueued render(bool held);
    /** @brief Le modèle recommandé pour la séquence (type du projet, sinon format). */
    QString recommendedModel() const;
    /** @brief Pseudo-actions de capture « livraison:… » (KORACUT_SHOT_ACTIONS), réponses KORACUT-LIVRAISON. */
    static void shotAction(const QString &command);

Q_SIGNALS:
    void settingsChanged();

private:
    void check();
    QPointer<KoraCutExportTiles> m_tiles;
    QPointer<KoraCutExportSettingsPanel> m_panel;
    QPointer<KoraCutExportQueuePanel> m_queue;
    KoraCutExport::Settings m_settings;
    QString m_uuid;
    QTimer *m_watch{nullptr};
};

/** @brief Le bandeau des modèles en tuiles (vrais logos), défilant à la molette. */
class KoraCutExportTiles : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutExportTiles(QWidget *parent = nullptr);
    void setCurrent(const QString &id);
    void setRecommended(const QString &id, const QSize &sequence);

Q_SIGNALS:
    void modelChosen(const QString &id);

protected:
    void changeEvent(QEvent *event) override;

private:
    void rebuild();
    QScrollArea *m_scroll{nullptr};
    QWidget *m_row{nullptr};
    QList<QAbstractButton *> m_tiles;
    QString m_current;
    QString m_recommended;
    QSize m_sequence;
};

/** @brief « Paramètres d'export » : les réglages du modèle choisi, en onglets Vidéo, Audio, Fichier. */
class KoraCutExportSettingsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutExportSettingsPanel(QWidget *parent = nullptr);
    void showSettings(const KoraCutExport::Settings &s);
    KoraCutExport::Settings read() const;
    void setTab(int index);
    /** @brief La taille estimée, la durée et les avertissements (rapport d'image, durée max, cadence). */
    void refreshSummary();

protected:
    void changeEvent(QEvent *event) override;

Q_SIGNALS:
    void edited();
    void modelChosen(const QString &id);
    void addToQueue();
    void renderNow();
    void advancedRequested();

private:
    QWidget *videoTab();
    QWidget *audioTab();
    QWidget *fileTab();
    void updateEnabled();
    void fillModels();
    bool m_loading{false};
    KoraCutExport::Settings m_base;
    QComboBox *m_preset{nullptr};
    QLineEdit *m_name{nullptr};
    QLabel *m_namePreview{nullptr};
    QLineEdit *m_folder{nullptr};
    QRadioButton *m_whole{nullptr};
    QRadioButton *m_inOut{nullptr};
    QLabel *m_rangeInfo{nullptr};
    QList<QToolButton *> m_tabButtons;
    QStackedWidget *m_tabs{nullptr};
    // Vidéo
    QCheckBox *m_video{nullptr};
    QComboBox *m_container{nullptr};
    QComboBox *m_codec{nullptr};
    QCheckBox *m_hardware{nullptr};
    QComboBox *m_resolution{nullptr};
    QSpinBox *m_width{nullptr};
    QSpinBox *m_height{nullptr};
    QLabel *m_fps{nullptr};
    QRadioButton *m_qualityAuto{nullptr};
    QRadioButton *m_qualityCap{nullptr};
    QDoubleSpinBox *m_cap{nullptr};
    QComboBox *m_profile{nullptr};
    QRadioButton *m_keysAuto{nullptr};
    QRadioButton *m_keysEvery{nullptr};
    QSpinBox *m_keys{nullptr};
    QCheckBox *m_reorder{nullptr};
    QComboBox *m_subtitles{nullptr};
    // Audio
    QCheckBox *m_audio{nullptr};
    QComboBox *m_audioCodec{nullptr};
    QComboBox *m_audioRate{nullptr};
    QComboBox *m_sampleRate{nullptr};
    QComboBox *m_channels{nullptr};
    QCheckBox *m_normalize{nullptr};
    QCheckBox *m_keepGain{nullptr};
    QSpinBox *m_lufs{nullptr};
    QLabel *m_peak{nullptr};
    // Fichier
    QCheckBox *m_reveal{nullptr};
    QCheckBox *m_signature{nullptr};
    // Bas
    QLabel *m_estimate{nullptr};
    QLabel *m_warnings{nullptr};
    QPushButton *m_guides{nullptr};
    QPushButton *m_add{nullptr};
    QPushButton *m_now{nullptr};
};

/** @brief La « File de rendu » : une carte par rendu, la progression, « Tout exporter ». */
class KoraCutExportQueuePanel : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutExportQueuePanel(QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void rebuild();
    void refreshTotals();
    QScrollArea *m_scroll{nullptr};
    QWidget *m_list{nullptr};
    QVBoxLayout *m_listLayout{nullptr};
    QWidget *m_empty{nullptr};
    QLabel *m_status{nullptr};
    QWidget *m_bar{nullptr};
    QPushButton *m_all{nullptr};
    QPushButton *m_stop{nullptr};
    QPushButton *m_clear{nullptr};
    QTimer *m_tick{nullptr};
    int m_builtCount{-1};
};
