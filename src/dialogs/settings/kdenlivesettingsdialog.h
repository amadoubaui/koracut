/*
    SPDX-FileCopyrightText: 2008 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <KConfigDialog>
#include <KProcess>
#include <QListWidget>
#include <QMap>

#include "ui_configcapture_ui.h"
#include "ui_configcolors_ui.h"
#include "ui_configenv_ui.h"
#include "ui_configjogshuttle_ui.h"
#include "ui_configmisc_ui.h"
#include "ui_configproject_ui.h"
#include "ui_configproxy_ui.h"
#include "ui_configsdl_ui.h"
#include "ui_configtimeline_ui.h"
#include "ui_configtools_ui.h"
#include "ui_configtranscode_ui.h"

#include "dialogs/encodingprofilesdialog.h"

class ProfileWidget;
class QCheckBox;
class QLineEdit;
class GuideCategories;
class PluginsSettings;
class KJob;
class KoraCutPrefsShell;

class KdenliveSettingsDialog : public KConfigDialog
{
    Q_OBJECT

public:
    KdenliveSettingsDialog(QMap<QString, QString> mappable_actions, bool gpuAllowed, QWidget *parent = nullptr);
    ~KdenliveSettingsDialog() override;
    void showPage(Kdenlive::ConfigPage page, int option);
    /** @brief KoraCut : la page « Connexions IA » ; @p id un fournisseur ou une connexion à montrer. */
    void showKoraCutAiPage(const QString &id = QString());
    void checkProfile();
    /** @brief update kdenlive settings for external app path if they were changed outside config dialog */
    void updateExternalApps();
    /** @brief KoraCut : les sections de la fenêtre (identifiants stables : general, apparence, timeline…). */
    Q_INVOKABLE QStringList koraCutSections() const;
    /** @brief KoraCut : montre une section par son identifiant ; faux si elle n'existe pas. */
    Q_INVOKABLE bool koraCutShowSection(const QString &id);
    /** @brief KoraCut : filtre les réglages comme la recherche de la barre latérale ; rend le nombre trouvé. */
    Q_INVOKABLE int koraCutSearch(const QString &text);
    /** @brief KoraCut : la section affichée. */
    Q_INVOKABLE QString koraCutCurrentSection() const;

protected:
    void showHelp() override;

protected Q_SLOTS:
    void updateSettings() override;
    void updateWidgets() override;
    /** @brief KoraCut (chantier 10) : les moniteurs relisent l'aperçu adaptatif après un changement. */
    void koraCutRefreshMonitors();
    bool hasChanged() override;
    void accept() override;
    void reject() override;

private Q_SLOTS:
    void slotCheckShuttle(int state = 0);
    void slotUpdateShuttleDevice(int ix = 0);
    void slotEditImageApplication();
    void slotEditAudioApplication();
    void slotEditVideoApplication();
    void slotEditGlaxnimateApplication();
    void slotReadAudioDevices();
    void slotUpdateGrabRegionStatus();
    void slotCheckAlsaDriver();
    void slotCheckAudioBackend();
    void slotAddTranscode();
    void slotDeleteTranscode();
    /** @brief Update current transcoding profile. */
    void slotUpdateTranscodingProfile();
    /** @brief Enable / disable the update profile button. */
    void slotEnableTranscodeUpdate();
    /** @brief Update display of current transcoding profile parameters. */
    void slotSetTranscodeProfile();
    void slotShuttleModified();
    void slotDialogModified();
    void slotRevealCaptureFolder(int ix);
    void slotEnableLibraryFolder();
    void slotEnableVideoFolder(int ix);
    void slotReloadBlackMagic();
    void slotReloadShuttleDevices();
    /** @brief fill list of connected monitors */
    void fillMonitorData();
    /** @brief Open external proxies config dialog */
    void configureExternalProxies();
    /** @brief Open an external browser window */
    void openBrowserUrl(const QString &url);
    void loadExternalProxyProfiles();
    /** @brief Enable/disable Breeze drag window from empty areas behavior */
    void slotUpdateBreezeDrag(bool dragOnTitleBar);
    /** @brief Use current device's default recording profile */
    void useRecordDefault();

private:
    KPageWidgetItem *m_pageMisc;
    KPageWidgetItem *m_pageEnv;
    KPageWidgetItem *m_pageTimeline;
    KPageWidgetItem *m_pageTools;
    KPageWidgetItem *m_pageCapture;
    KPageWidgetItem *m_pageJog{nullptr};
    KPageWidgetItem *m_pagePlay;
    KPageWidgetItem *m_pageTranscode;
    KPageWidgetItem *m_pageProject;
    KPageWidgetItem *m_pageColors;
    KPageWidgetItem *m_pageSpeech;
    KPageWidgetItem *m_pageKoraCutAi{nullptr};
    // KoraCut, 25 septembre 2026 : les sections ajoutées par la nouvelle fenêtre, et les racines des
    // pages de Kdenlive, préparées puis vidées dans les cartes (elles restent en vie, cachées).
    KPageWidgetItem *m_pageImport{nullptr};
    KPageWidgetItem *m_pageAppearance{nullptr};
    KPageWidgetItem *m_pageProxy{nullptr};
    KPageWidgetItem *m_pagePorte{nullptr};
    KPageWidgetItem *m_pagePerformance{nullptr};
    KPageWidgetItem *m_pageFolders{nullptr};
    QWidget *m_rootMisc{nullptr};
    QWidget *m_rootProject{nullptr};
    QWidget *m_rootProxy{nullptr};
    QWidget *m_rootTimeline{nullptr};
    QWidget *m_rootTools{nullptr};
    QWidget *m_rootEnv{nullptr};
    QWidget *m_rootColors{nullptr};
    QWidget *m_rootSdl{nullptr};
    QWidget *m_rootCapture{nullptr};
    QWidget *m_rootJog{nullptr};
    QWidget *m_rootTranscode{nullptr};
    KoraCutPrefsShell *m_koracutShell{nullptr};

    Ui::ConfigEnv_UI m_configEnv;
    Ui::ConfigMisc_UI m_configMisc;
    Ui::ConfigColors_UI m_configColors;
    Ui::ConfigTimeline_UI m_configTimeline;
    Ui::ConfigTools_UI m_configTools;
    Ui::ConfigCapture_UI m_configCapture;
    Ui::ConfigJogShuttle_UI m_configShuttle;
    Ui::ConfigSdl_UI m_configSdl;
    Ui::ConfigTranscode_UI m_configTranscode;
    Ui::ConfigProject_UI m_configProject;
    Ui::ConfigProxy_UI m_configProxy;
    PluginsSettings *m_pluginsPage;
    GuideCategories *m_guidesCategories;
    ProfileWidget *m_pw;
    KProcess m_readProcess;
    bool m_modified;
    bool m_shuttleModified;
    bool m_voskUpdated;
    bool m_breezeDragFromTitlebarOnly{false};
    /** @brief KoraCut, porte de commande (document 18) : groupe KoraCut de koracutrc. */
    QCheckBox *m_koracutPorteOpen{nullptr};
    QLineEdit *m_koracutTerminalCommand{nullptr};

    QMap<QString, QString> m_mappable_actions;
    QVector<QComboBox *> m_shuttle_buttons;
    EncodingTimelinePreviewProfilesChooser *m_tlPreviewProfiles;
    EncodingProfilesChooser *m_proxyProfiles;
    EncodingProfilesChooser *m_alphaProxyProfiles;
    EncodingProfilesChooser *m_decklinkProfiles;
    EncodingProfilesChooser *m_grabProfiles;
    void initDevices();
    void loadTranscodeProfiles();
    void saveTranscodeProfiles();
    void setupJogshuttleBtns(const QString &device);
    /** @brief Fill a combobox with the found blackmagic devices */
    static bool getBlackMagicDeviceList(QComboBox *devicelist, bool force = false);
    static bool getBlackMagicOutputDeviceList(QComboBox *devicelist, bool force = false);
    /** @brief Init QtMultimedia audio record settings */
    bool initAudioRecDevice();
    void initMiscPage();
    void initProjectPage();
    void initProxyPage();
    void initTimelinePage();
    void initEnviromentPage();
    void initColorsPage();
    /** @brief Init Speech to text settings */
    void initSpeechPage();
    void initCapturePage();
    void initJogShuttlePage();
    void initSdlPage(bool gpuAllowed);
    void initTranscodePage();
    void updateRecordProperties();
    /** @brief KoraCut : range les réglages dans les sections de la nouvelle fenêtre (voir koracutpreferences.h). */
    void buildKoraCutPages();

Q_SIGNALS:
    void customChanged();
    void doResetConsumer(bool fullReset);
    void updateCaptureFolder();
    void updateLibraryFolder();
    /** @brief A settings changed that requires a Kdenlive restart, trigger it */
    void restartKdenlive(bool resetConfig = false);
    void checkTabPosition();
    /** @brief An important timeline property changed, prepare for a reset */
    void resetView();
};
