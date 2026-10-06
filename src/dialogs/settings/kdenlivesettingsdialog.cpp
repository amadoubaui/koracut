/*
    SPDX-FileCopyrightText: 2021 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "kdenlivesettingsdialog.h"
#include "core.h"
#include <config-kdenlive.h>
#if KORACUT_AGENTS
#include "koracut/ai/koracutaiconnectionspage.h"
#endif
#include "dialogs/customcamcorderdialog.h"
#include "dialogs/wizard.h"
#include "doc/kdenlivedoc.h"
#include "filefilter.h"
#include "kdenlivesettings.h"
#include "koracut/koracutmetrics.h"
#include "koracut/koracutpreferences.h"
#include "koracut/koracutpreferencesappearance.h"
#include "koracut/koracutthemes.h"
#include "mainwindow.h"
#include "monitor/monitor.h"
#include "pluginssettings.h"
#include "profiles/profilemodel.hpp"
#include "profiles/profilerepository.hpp"
#include "project/dialogs/guidecategories.h"
#include "project/dialogs/profilewidget.h"
#include "project/projectmanager.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"

#include "KLocalizedString"
#include "kdenlive_debug.h"
#include <KIO/DesktopExecParser>
#include <KIO/FileCopyJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KConfigDialogManager>
#include <KContextualHelpButton>
#include <KLineEdit>
#include <KMessageWidget>
#include <KMessageBox>
#include <KOpenWithDialog>
#include <KService>
#include <KUrlRequesterDialog>
#include <kddockwidgets/Config.h>

#include <QAction>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QAudioDevice>
#include <QButtonGroup>
#include <QDir>
#include <QGuiApplication>
#include <QListView>
#include <QProxyStyle>
#include <QInputDialog>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QScreen>
#include <QSize>
#include <QThread>
#include <QFormLayout>
#include <QTextDocumentFragment>
#include <QTimer>
#include "koracut/koracutperformance.h"
#include "koracut/koracutlightcopies.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"
#include <QtConcurrent/QtConcurrentRun>

#include <fcntl.h>

#ifdef USE_JOGSHUTTLE
#include "jogshuttle/jogshuttle.h"
#include "jogshuttle/jogshuttleconfig.h"
#include <QStandardPaths>
#include <linux/input.h>
#include <memory>
#endif


KdenliveSettingsDialog::KdenliveSettingsDialog(QMap<QString, QString> mappable_actions, bool gpuAllowed, QWidget *parent)
    : KConfigDialog(parent, QStringLiteral("settings"), KdenliveSettings::self())
    , m_modified(false)
    , m_shuttleModified(false)
    , m_voskUpdated(false)
    , m_mappable_actions(std::move(mappable_actions))
{
    setWindowModality(Qt::ApplicationModal);

    initMiscPage();
    initProjectPage();
    initProxyPage();

    QWidget *p3 = new QWidget;
    m_configTimeline.setupUi(p3);
    m_rootTimeline = p3;

    QWidget *p4 = new QWidget;
    m_configTools.setupUi(p4);
    m_rootTools = p4;

    initEnviromentPage();

    QWidget *p11 = new QWidget;
    m_configColors.setupUi(p11);

    // Initialize clip color buttons with live computed colors
    if (auto *tc = pCore->window()->getCurrentTimeline()->controller()) {
        m_configColors.videoColor->setColor(tc->getTimelineClipColor(ClipType::Video));
        m_configColors.audioColor->setColor(tc->getTimelineClipColor(ClipType::Audio));
        m_configColors.titleColor->setColor(tc->getTimelineClipColor(ClipType::Text));
        m_configColors.imageColor->setColor(tc->getTimelineClipColor(ClipType::Image));
        m_configColors.slideshowColor->setColor(tc->getTimelineClipColor(ClipType::SlideShow));
        connect(m_configColors.resetClipColors, &QPushButton::clicked, this, [this, tc]() {
            m_configColors.videoColor->setColor(tc->getDefaultClipColor(ClipType::Video));
            m_configColors.audioColor->setColor(tc->getDefaultClipColor(ClipType::Audio));
            m_configColors.titleColor->setColor(tc->getDefaultClipColor(ClipType::Text));
            m_configColors.imageColor->setColor(tc->getDefaultClipColor(ClipType::Image));
            m_configColors.slideshowColor->setColor(tc->getDefaultClipColor(ClipType::SlideShow));
        });
    }

    m_rootColors = p11;
    m_guidesCategories = new GuideCategories(nullptr, this);
    QVBoxLayout *guidesLayout = new QVBoxLayout(m_configColors.guides_box);
    guidesLayout->addWidget(m_guidesCategories);

    m_pluginsPage = new PluginsSettings(this);
    connect(m_pluginsPage, &PluginsSettings::openBrowserUrl, this, &KdenliveSettingsDialog::openBrowserUrl);

    QWidget *p7 = new QWidget;
    m_configSdl.setupUi(p7);
    m_rootSdl = p7;

    QWidget *p5 = new QWidget;
    m_configCapture.setupUi(p5);
    m_decklinkProfiles = new EncodingProfilesChooser(this, EncodingProfilesManager::DecklinkCapture, false, QStringLiteral("decklink_profile"));
    m_configCapture.decklink_profile_box->addWidget(m_decklinkProfiles);
    m_grabProfiles = new EncodingProfilesChooser(this, EncodingProfilesManager::ScreenCapture, false, QStringLiteral("grab_profile"));
    m_configCapture.screen_grab_profile_box->addWidget(m_grabProfiles);
    m_rootCapture = p5;

    initDevices();
    initCapturePage();
    initJogShuttlePage();
    initTranscodePage();

    initSdlPage(gpuAllowed);


    // KoraCut, 25 septembre 2026 : la fenêtre refaite (barre latérale groupée, recherche, cartes).
    // Toutes les pages ci-dessus ont été préparées sans être ajoutées : buildKoraCutPages() les range.
    buildKoraCutPages();

    // Config dialog size
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup settingsGroup(config, "settings");
    QSize optimalSize;

    if (!settingsGroup.exists() || !settingsGroup.hasKey("dialogSize")) {
        const QSize screenSize = (QGuiApplication::primaryScreen()->availableSize() * 0.9);
        // KoraCut : de quoi tenir la barre latérale entière et une colonne de 760 px.
        const QSize targetSize = QSize(1120, 780);
        optimalSize = targetSize.boundedTo(screenSize);
    } else {
        optimalSize = settingsGroup.readEntry("dialogSize", QVariant(size())).toSize();
        // KoraCut : une taille gardée de l'ancienne fenêtre (1024 × 708) coupait la barre latérale.
        optimalSize = optimalSize.expandedTo(QSize(980, 680)).boundedTo(QGuiApplication::primaryScreen()->availableSize());
    }
    resize(optimalSize);
}

// static
bool KdenliveSettingsDialog::getBlackMagicDeviceList(QComboBox *devicelist, bool force)
{
    if (!force && !KdenliveSettings::decklink_device_found()) {
        return false;
    }
    Mlt::Profile profile;
    Mlt::Producer bm(profile, "decklink");
    int found_devices = 0;
    if (bm.is_valid()) {
        bm.set("list_devices", 1);
        found_devices = bm.get_int("devices");
    } else {
        KdenliveSettings::setDecklink_device_found(false);
    }
    if (found_devices <= 0) {
        devicelist->setEnabled(false);
        return false;
    }
    KdenliveSettings::setDecklink_device_found(true);
    for (int i = 0; i < found_devices; ++i) {
        char *tmp = qstrdup(QStringLiteral("device.%1").arg(i).toUtf8().constData());
        devicelist->addItem(bm.get(tmp));
        delete[] tmp;
    }
    return true;
}

// static
bool KdenliveSettingsDialog::initAudioRecDevice()
{
    QStringList audioDevices = pCore->getAudioCaptureDevices();

    // show a hint to the user to know what to check for in case the device list are empty (common issue)
    m_configCapture.labelNoAudioDevices->setVisible(audioDevices.empty());

    m_configCapture.defaultaudiocapture->addItems(audioDevices);
    QString selectedDevice = KdenliveSettings::defaultaudiocapture();
    int selectedIndex = m_configCapture.defaultaudiocapture->findText(selectedDevice);
    if (!selectedDevice.isEmpty() && selectedIndex > -1) {
        m_configCapture.defaultaudiocapture->setCurrentIndex(selectedIndex);
    }
    connect(m_configCapture.defaultaudiocapture, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [&]() {
        updateRecordProperties();
        updateButtons();
    });
    return true;
}

void KdenliveSettingsDialog::initMiscPage()
{
    QWidget *p1 = new QWidget;
    m_configMisc.setupUi(p1);
    m_rootMisc = p1;

    m_configMisc.kcfg_use_exiftool->setEnabled(!QStandardPaths::findExecutable(QStringLiteral("exiftool")).isEmpty());

    static const QRegularExpression reg(R"((\+|-)?\d{2}:\d{2}:\d{2}(:||,)\d{2,3})");
    QValidator *validator = new QRegularExpressionValidator(reg, this);
    const QString inputMask = pCore->timecode().mask();
    QSignalBlocker bk1(m_configMisc.kcfg_color_duration);
    QSignalBlocker bk2(m_configMisc.kcfg_title_duration);
    QSignalBlocker bk3(m_configMisc.kcfg_transition_duration);
    QSignalBlocker bk4(m_configMisc.kcfg_mix_duration);
    QSignalBlocker bk5(m_configMisc.kcfg_image_duration);
    QSignalBlocker bk6(m_configMisc.kcfg_sequence_duration);
    QSignalBlocker bk7(m_configMisc.kcfg_fade_duration);
    QSignalBlocker bk8(m_configMisc.kcfg_subtitle_duration);
    m_configMisc.kcfg_color_duration->setInputMask(inputMask);
    m_configMisc.kcfg_color_duration->setValidator(validator);
    m_configMisc.kcfg_title_duration->setInputMask(inputMask);
    m_configMisc.kcfg_title_duration->setValidator(validator);
    m_configMisc.kcfg_transition_duration->setInputMask(inputMask);
    m_configMisc.kcfg_transition_duration->setValidator(validator);
    m_configMisc.kcfg_mix_duration->setInputMask(inputMask);
    m_configMisc.kcfg_mix_duration->setValidator(validator);
    m_configMisc.kcfg_image_duration->setInputMask(inputMask);
    m_configMisc.kcfg_image_duration->setValidator(validator);
    m_configMisc.kcfg_sequence_duration->setInputMask(inputMask);
    m_configMisc.kcfg_sequence_duration->setValidator(validator);
    m_configMisc.kcfg_fade_duration->setInputMask(inputMask);
    m_configMisc.kcfg_fade_duration->setValidator(validator);
    m_configMisc.kcfg_subtitle_duration->setInputMask(inputMask);
    m_configMisc.kcfg_subtitle_duration->setValidator(validator);

    m_configMisc.preferredcomposite->clear();
    m_configMisc.preferredcomposite->addItem(i18n("auto"));
    m_configMisc.preferredcomposite->addItems(KdenliveSettings::compositingList());

    if (!KdenliveSettings::preferredcomposite().isEmpty()) {
        int ix = m_configMisc.preferredcomposite->findText(KdenliveSettings::preferredcomposite());
        if (ix > -1) {
            m_configMisc.preferredcomposite->setCurrentIndex(ix);
        }
    }
    m_configMisc.kcfg_enableBuiltInEffects->setWhatsThis(xi18nc(
        "@info:whatsthis", "When enabled, this will add basic effects (Flip, Transform, Volume) to all clips for convenience. These effects are disabled "
                           "and will only be applied if you change a parameter."));
    connect(m_configMisc.preferredcomposite, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [&]() {
        if (m_configMisc.preferredcomposite->currentText() != KdenliveSettings::preferredcomposite()) {
            KdenliveSettings::setPreferredcomposite(m_configMisc.preferredcomposite->currentText());
            int mode = pCore->currentDoc()->getSequenceProperty(pCore->window()->getCurrentTimeline()->getUuid(), QStringLiteral("compositing")).toInt();
            pCore->window()->getCurrentTimeline()->controller()->switchCompositing(mode);
            pCore->currentDoc()->setModified();
        }
    });

    // KoraCut, la porte de commande (document 18) : réglages du groupe KoraCut de koracutrc, pas du kcfg.
    const KConfigGroup koracut = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
    // Rangés dans la section « Pilotage extérieur » par buildKoraCutPages().
    m_koracutPorteOpen = new QCheckBox(i18nc("@option:check", "Allow external programs to control KoraCut"), p1);
    m_koracutPorteOpen->setToolTip(i18nc("@info:tooltip", "Opens a local command port (koracut-ctl, the koracut MCP server, Claude Code). Nothing leaves this computer."));
    m_koracutPorteOpen->setChecked(koracut.readEntry("porteOuverte", true));
    connect(m_koracutPorteOpen, &QCheckBox::toggled, this, &KdenliveSettingsDialog::slotDialogModified);
    m_koracutTerminalCommand = new QLineEdit(koracut.readEntry("terminalCommande", QString()), p1);
    m_koracutTerminalCommand->setPlaceholderText(i18nc("@info:placeholder", "claude (if Claude Code is installed), - for the shell only"));
    connect(m_koracutTerminalCommand, &QLineEdit::textChanged, this, &KdenliveSettingsDialog::slotDialogModified);
}

void KdenliveSettingsDialog::initProjectPage()
{
    QWidget *p9 = new QWidget;
    m_configProject.setupUi(p9);
    // Timeline preview
    QString currentPreviewData = KdenliveSettings::previewparams().isEmpty()
                                     ? QString()
                                     : QStringLiteral("%1;%2").arg(KdenliveSettings::previewparams(), KdenliveSettings::previewextension());
    m_tlPreviewProfiles = new EncodingTimelinePreviewProfilesChooser(p9, true, currentPreviewData, false);
    m_configProject.preview_profile_box->addWidget(m_tlPreviewProfiles);
    auto *vbox = new QVBoxLayout;
    m_pw = new ProfileWidget(this);
    vbox->addWidget(m_pw);
    connect(m_pw, &ProfileWidget::profileChanged, this, [this]() { m_tlPreviewProfiles->filterPreviewProfiles(m_pw->selectedProfile()); });
    m_configProject.profile_box->setLayout(vbox);
    m_configProject.profile_box->setTitle(i18n("Select the Default Profile (preset)"));
    // Select profile
    m_pw->loadProfile(KdenliveSettings::default_profile().isEmpty() ? pCore->getCurrentProfile()->path() : KdenliveSettings::default_profile());
    m_tlPreviewProfiles->filterPreviewProfiles(m_pw->selectedProfile());
    connect(m_tlPreviewProfiles, &EncodingTimelinePreviewProfilesChooser::currentIndexChanged, this, &KdenliveSettingsDialog::slotDialogModified);
    connect(m_pw, &ProfileWidget::profileChanged, this, &KdenliveSettingsDialog::slotDialogModified);
    m_configProject.projecturl->setMode(KFile::Directory);
    m_configProject.projecturl->setUrl(QUrl::fromLocalFile(KdenliveSettings::defaultprojectfolder()));
    connect(m_configProject.projecturl, &KUrlRequester::textChanged, this, &KdenliveSettingsDialog::slotDialogModified);
    connect(m_configProject.kcfg_customprojectfolder, &QCheckBox::toggled, m_configProject.projecturl, &KUrlRequester::setEnabled);
    connect(m_configProject.kcfg_videotracks, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this]() {
        if (m_configProject.kcfg_videotracks->value() + m_configProject.kcfg_audiotracks->value() <= 0) {
            m_configProject.kcfg_videotracks->setValue(1);
        }
    });
    connect(m_configProject.kcfg_audiotracks, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this]() {
        if (m_configProject.kcfg_videotracks->value() + m_configProject.kcfg_audiotracks->value() <= 0) {
            m_configProject.kcfg_audiotracks->setValue(1);
        }
    });

    m_rootProject = p9;
}

void KdenliveSettingsDialog::initProxyPage()
{
    QWidget *p10 = new QWidget;
    m_configProxy.setupUi(p10);
    m_proxyProfiles = new EncodingProfilesChooser(p10, EncodingProfilesManager::ProxyClips, true, QStringLiteral("proxy_profile"));
    m_configProxy.proxy_profile_box->addWidget(m_proxyProfiles);
    m_alphaProxyProfiles = new EncodingProfilesChooser(p10, EncodingProfilesManager::ProxyAlphaClips, true, QStringLiteral("alpha_profile"));
    m_configProxy.alpha_profile_box->addWidget(m_alphaProxyProfiles);
    m_rootProxy = p10;
    connect(m_configProxy.kcfg_generateproxy, &QAbstractButton::toggled, m_configProxy.kcfg_proxyminsize, &QWidget::setEnabled);
    m_configProxy.kcfg_proxyminsize->setEnabled(KdenliveSettings::generateproxy());
    connect(m_configProxy.kcfg_generateimageproxy, &QAbstractButton::toggled, m_configProxy.kcfg_proxyimageminsize, &QWidget::setEnabled);
    m_configProxy.kcfg_proxyimageminsize->setEnabled(KdenliveSettings::generateimageproxy());
    loadExternalProxyProfiles();
    connect(m_configProxy.button_external, &QToolButton::clicked, this, &KdenliveSettingsDialog::configureExternalProxies);
    QString allowedParams = KdenliveSettings::safeFFmpegParams().join(QLatin1Char(','));
    m_configProxy.allowedProxyParams->setPlainText(allowedParams);
    connect(m_configProxy.allowedProxyParams, &QPlainTextEdit::textChanged, this, &KdenliveSettingsDialog::slotDialogModified);
    connect(m_configProxy.resetProxyParams, &QPushButton::clicked, this, [this]() {
        QString defaults = KdenliveSettings::defaultSafeFFmpegParamsValue().join(QLatin1Char(','));
        m_configProxy.allowedProxyParams->setPlainText(defaults);
    });
}

void KdenliveSettingsDialog::configureExternalProxies()
{
    // We want to edit the profiles
    CustomCamcorderDialog cd;
    if (cd.exec() == QDialog::Accepted) {
        // reload profiles
        loadExternalProxyProfiles();
    }
}

void KdenliveSettingsDialog::initEnviromentPage()
{
    QWidget *p2 = new QWidget;
    m_configEnv.setupUi(p2);
    m_configEnv.mltpathurl->setMode(KFile::Directory);
    m_configEnv.mltpathurl->lineEdit()->setObjectName(QStringLiteral("kcfg_mltpath"));
    m_configEnv.rendererpathurl->lineEdit()->setObjectName(QStringLiteral("kcfg_meltpath"));
    m_configEnv.ffmpegurl->lineEdit()->setObjectName(QStringLiteral("kcfg_ffmpegpath"));
    m_configEnv.ffplayurl->lineEdit()->setObjectName(QStringLiteral("kcfg_ffplaypath"));
    m_configEnv.ffprobeurl->lineEdit()->setObjectName(QStringLiteral("kcfg_ffprobepath"));
    m_configEnv.tmppathurl->setMode(KFile::Directory);
    m_configEnv.tmppathurl->lineEdit()->setObjectName(QStringLiteral("kcfg_currenttmpfolder"));
    m_configEnv.capturefolderurl->setMode(KFile::Directory);
    m_configEnv.capturefolderurl->lineEdit()->setObjectName(QStringLiteral("kcfg_capturefolder"));
    KdenliveSettingsDialog::slotRevealCaptureFolder(KdenliveSettings::capturetoprojectfolder());
    m_configEnv.kcfg_capturetoprojectfolder->setItemText(0, i18n("Use default folder: %1", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)));
    if (KdenliveSettings::customprojectfolder()) {
        m_configEnv.kcfg_capturetoprojectfolder->setItemText(1, i18n("Always use project folder: %1", KdenliveSettings::defaultprojectfolder()));
    } else {
        m_configEnv.kcfg_capturetoprojectfolder->setItemText(1, i18n("Always use active project folder"));
    }
    connect(m_configEnv.kcfg_capturetoprojectfolder, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &KdenliveSettingsDialog::slotRevealCaptureFolder);

    // Library folder
    m_configEnv.libraryfolderurl->setMode(KFile::Directory);
    m_configEnv.libraryfolderurl->lineEdit()->setObjectName(QStringLiteral("kcfg_libraryfolder"));
    m_configEnv.libraryfolderurl->setEnabled(!KdenliveSettings::librarytodefaultfolder());
    m_configEnv.libraryfolderurl->setPlaceholderText(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/library"));
    m_configEnv.kcfg_librarytodefaultfolder->setToolTip(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/library"));
    connect(m_configEnv.kcfg_librarytodefaultfolder, &QAbstractButton::clicked, this, &KdenliveSettingsDialog::slotEnableLibraryFolder);

    m_configEnv.kcfg_proxythreads->setMaximum(qMax(1, QThread::idealThreadCount() - 1));

    // Script rendering files folder
    m_configEnv.videofolderurl->setMode(KFile::Directory);
    m_configEnv.videofolderurl->lineEdit()->setObjectName(QStringLiteral("kcfg_videofolder"));
    m_configEnv.videofolderurl->setEnabled(KdenliveSettings::videotodefaultfolder() == KdenliveDoc::SaveToCustomFolder);
    m_configEnv.videofolderurl->setPlaceholderText(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    m_configEnv.kcfg_videotodefaultfolder->setItemText(KdenliveDoc::SaveToVideoFolder,
                                                       i18n("Use default folder: %1", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)));
    if (KdenliveSettings::customprojectfolder()) {
        m_configEnv.kcfg_videotodefaultfolder->setItemText(KdenliveDoc::SaveToProjectFolder,
                                                           i18n("Always use project folder: %1", KdenliveSettings::defaultprojectfolder()));
    } else {
        m_configEnv.kcfg_videotodefaultfolder->setItemText(KdenliveDoc::SaveToProjectFolder, i18n("Always use active project folder"));
    }
    connect(m_configEnv.kcfg_videotodefaultfolder, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &KdenliveSettingsDialog::slotEnableVideoFolder);

    // Window drag behavior on non KDE desktops
    if (qgetenv("XDG_CURRENT_DESKTOP") == QLatin1String("KDE")) {
        m_configEnv.dragFromTitlebarOnly->setVisible(false);
    } else {
        auto cfg = KSharedConfig::openConfig(QStringLiteral("breezerc"), KConfig::SimpleConfig, QStandardPaths::GenericConfigLocation);
        KConfigGroup group(cfg, "Style");
        if (group.readEntry(QStringLiteral("WindowDragMode")) == QLatin1String("WD_NONE")) {
            m_configEnv.dragFromTitlebarOnly->setChecked(true);
            m_breezeDragFromTitlebarOnly = true;
        }
        KSharedConfigPtr config = KSharedConfig::openConfig();
        KConfigGroup settingsGroup(config, "KDE");
        const QString currentStyle = settingsGroup.readEntry(QStringLiteral("widgetStyle")).toLower();
        if (!currentStyle.isEmpty() && currentStyle != QLatin1String("breeze")) {
            m_configEnv.dragFromTitlebarOnly->setEnabled(false);
        }
    }

    // Mime types
    QStringList mimes = FileFilter::getExtensions();
    std::sort(mimes.begin(), mimes.end());
    m_configEnv.supportedmimes->setPlainText(mimes.join(QLatin1Char(' ')));

    connect(m_configEnv.kp_image, &QAbstractButton::clicked, this, &KdenliveSettingsDialog::slotEditImageApplication);
    connect(m_configEnv.kp_audio, &QAbstractButton::clicked, this, &KdenliveSettingsDialog::slotEditAudioApplication);
    connect(m_configEnv.kp_anim, &QAbstractButton::clicked, this, &KdenliveSettingsDialog::slotEditGlaxnimateApplication);
    connect(m_configEnv.kp_video, &QAbstractButton::clicked, this, &KdenliveSettingsDialog::slotEditVideoApplication);

    m_rootEnv = p2;
#if defined Q_OS_MAC
    // Power management not implemented on Mac
    m_configEnv.kcfg_usePowerManagement->setEnabled(false);
#endif
}

void KdenliveSettingsDialog::slotUpdateBreezeDrag(bool dragOnTitleBar)
{
    auto cfg = KSharedConfig::openConfig(QStringLiteral("breezerc"), KConfig::SimpleConfig, QStandardPaths::GenericConfigLocation);
    KConfigGroup group(cfg, "Style");
    if (!dragOnTitleBar) {
        // Revert to default behavior
        group.deleteEntry(QStringLiteral("WindowDragMode"));
    } else {
        // Drag through title bar only
        group.writeEntry(QStringLiteral("WindowDragMode"), QStringLiteral("WD_NONE"));
    }
    cfg->sync();
    // Ensure Breeze style directly updates its configuration
    QEvent event(QEvent::ApplicationPaletteChange);
    qApp->sendEvent(qApp, &event);
}

void KdenliveSettingsDialog::initCapturePage()
{
    m_configCapture.label->setVisible(false);
    m_configCapture.kcfg_defaultcapture->setVisible(false);
    m_configCapture.tabWidget->setCurrentIndex(KdenliveSettings::defaultcapture());
#ifdef Q_WS_MAC
    m_configCapture.tabWidget->setEnabled(false);
    m_configCapture.kcfg_defaultcapture->setEnabled(false);
    m_configCapture.label->setText(i18n("Capture is not yet available on Mac OS X."));
#endif

    connect(m_configCapture.kcfg_grab_capture_type, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &KdenliveSettingsDialog::slotUpdateGrabRegionStatus);

    slotUpdateGrabRegionStatus();

    // Init audio capture device
    initAudioRecDevice();

    // Fill channels and sample rate with supported values
    updateRecordProperties();

    // Ensure changes to channels / sample rate trigger a changed signal
    connect(m_configCapture.audiocapturechannels, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [&]() { updateButtons(); });
    connect(m_configCapture.audiocapturesamplerate, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [&]() { updateButtons(); });
    connect(m_configCapture.audiocapturesampleformat, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [&]() { updateButtons(); });

    m_configCapture.labelNoAudioDevices->setVisible(false);

    connect(m_configCapture.audiocapturedefaults, &QPushButton::clicked, this, &KdenliveSettingsDialog::useRecordDefault);

    getBlackMagicDeviceList(m_configCapture.kcfg_decklink_capturedevice);
}

void KdenliveSettingsDialog::useRecordDefault()
{
    const QString device = m_configCapture.defaultaudiocapture->currentText();
    QAudioDevice deviceInfo = QMediaDevices::defaultAudioInput();
    const auto deviceInfos = QMediaDevices::audioInputs();
    for (const QAudioDevice &devInfo : deviceInfos) {
        qDebug() << "Found device : " << devInfo.description();
        if (devInfo.description() == device) {
            deviceInfo = devInfo;
            qDebug() << "Monitoring using device : " << devInfo.description();
            break;
        }
    }
    if (deviceInfo.isNull()) {
        return;
    }
    QAudioFormat defaultFormat = deviceInfo.preferredFormat();
    int ix = m_configCapture.audiocapturechannels->findData(defaultFormat.channelCount());
    if (ix > -1) {
        m_configCapture.audiocapturechannels->setCurrentIndex(ix);
    }
    ix = m_configCapture.audiocapturesamplerate->findData(defaultFormat.sampleRate());
    if (ix > -1) {
        m_configCapture.audiocapturesamplerate->setCurrentIndex(ix);
    }
    ix = m_configCapture.audiocapturesampleformat->findData(defaultFormat.sampleFormat());
    if (ix > -1) {
        m_configCapture.audiocapturesampleformat->setCurrentIndex(ix);
    }
}

void KdenliveSettingsDialog::updateRecordProperties()
{
    // Find active device
    const QString device = m_configCapture.defaultaudiocapture->currentText();
    QAudioDevice deviceInfo = QMediaDevices::defaultAudioInput();
    const auto deviceInfos = QMediaDevices::audioInputs();
    for (const QAudioDevice &devInfo : deviceInfos) {
        qDebug() << "Found device : " << devInfo.description();
        if (devInfo.description() == device) {
            deviceInfo = devInfo;
            qDebug() << "Monitoring using device : " << devInfo.description();
            break;
        }
    }
    if (deviceInfo.isNull()) {
        m_configCapture.audiocapturechannels->setEnabled(false);
        m_configCapture.audiocapturesamplerate->setEnabled(false);
        m_configCapture.audiocapturesampleformat->setEnabled(false);
        m_configCapture.audiocapturedefaults->setEnabled(false);
    }
    m_configCapture.audiocapturechannels->setEnabled(true);
    m_configCapture.audiocapturesamplerate->setEnabled(true);
    m_configCapture.audiocapturesampleformat->setEnabled(true);
    m_configCapture.audiocapturedefaults->setEnabled(true);

    // Supported audio channels
    m_configCapture.audiocapturechannels->clear();
    int min = deviceInfo.minimumChannelCount();
    // Max channel count seems to frequently go up to 32, which is probably incorrect, limit to 6
    int max = qMin(6, deviceInfo.maximumChannelCount());
    if (min < 2) {
        m_configCapture.audiocapturechannels->addItem(i18n("Mono (1 channel)"), 1);
    }
    if (max > 1) {
        m_configCapture.audiocapturechannels->addItem(i18n("Stereo (2 channels)"), 2);
    }
    for (int i = 3; i <= max; i++) {
        m_configCapture.audiocapturechannels->addItem(i18n("%1 channels", i), i);
    }
    int channelsIndex = m_configCapture.audiocapturechannels->findData(KdenliveSettings::audiocapturechannels());
    m_configCapture.audiocapturechannels->setCurrentIndex(qMax(channelsIndex, 0));

    // Default audio capture sample rate
    m_configCapture.audiocapturesamplerate->clear();
    min = deviceInfo.minimumSampleRate();
    max = deviceInfo.maximumSampleRate();

    if (max < 44100) {
        m_configCapture.audiocapturesamplerate->addItem(i18n("%1 Hz", max), max);
    } else {
        if (min <= 44100) {
            m_configCapture.audiocapturesamplerate->addItem(i18n("44100 Hz"), 44100);
        }
        if (max >= 48000) {
            m_configCapture.audiocapturesamplerate->addItem(i18n("48000 Hz"), 48000);
        }
        if (max >= 96000) {
            m_configCapture.audiocapturesamplerate->addItem(i18n("96000 Hz"), 96000);
        }
    }

    int sampleRateIndex = m_configCapture.audiocapturesamplerate->findData(KdenliveSettings::audiocapturesamplerate());
    m_configCapture.audiocapturesamplerate->setCurrentIndex(qMax(sampleRateIndex, 0));

    // Default audio capture sample formats
    m_configCapture.audiocapturesampleformat->clear();
    QList<QAudioFormat::SampleFormat> sampleFormats = deviceInfo.supportedSampleFormats();
    for (auto &fm : sampleFormats) {
        switch (fm) {
        case QAudioFormat::UInt8:
            m_configCapture.audiocapturesampleformat->addItem(i18n("Unsigned 8 bit"), QAudioFormat::UInt8);
            break;
        case QAudioFormat::Int16:
            m_configCapture.audiocapturesampleformat->addItem(i18n("Signed 16 bit"), QAudioFormat::Int16);
            break;
        case QAudioFormat::Int32:
            m_configCapture.audiocapturesampleformat->addItem(i18n("Signed 32 bit"), QAudioFormat::Int32);
            break;
        case QAudioFormat::Float:
            m_configCapture.audiocapturesampleformat->addItem(i18n("Float"), QAudioFormat::Float);
            break;
        default:
            break;
        }
    }
    int sampleFormatIndex = m_configCapture.audiocapturesampleformat->findData(KdenliveSettings::audiocapturesampleformat());
    m_configCapture.audiocapturesampleformat->setCurrentIndex(qMax(sampleFormatIndex, 0));
}

void KdenliveSettingsDialog::initJogShuttlePage()
{
    QWidget *p6 = new QWidget;
    m_configShuttle.setupUi(p6);
#ifdef USE_JOGSHUTTLE
    connect(m_configShuttle.kcfg_enableshuttle, &QCheckBox::checkStateChanged, this, &KdenliveSettingsDialog::slotCheckShuttle);
    connect(m_configShuttle.shuttledevicelist, SIGNAL(activated(int)), this, SLOT(slotUpdateShuttleDevice(int)));
    connect(m_configShuttle.toolBtnReload, &QAbstractButton::clicked, this, &KdenliveSettingsDialog::slotReloadShuttleDevices);

    slotCheckShuttle(static_cast<int>(KdenliveSettings::enableshuttle()));
    m_configShuttle.shuttledisabled->hide();

    // Store the button pointers into an array for easier handling them in the other functions.
    // TODO: impl enumerator or live with cut and paste :-)))
    setupJogshuttleBtns(KdenliveSettings::shuttledevice());
#if false
    m_shuttle_buttons.push_back(m_configShuttle.shuttle1);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle2);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle3);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle4);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle5);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle6);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle7);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle8);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle9);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle10);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle11);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle12);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle13);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle14);
    m_shuttle_buttons.push_back(m_configShuttle.shuttle15);
#endif

#else  /* ! USE_JOGSHUTTLE */
    m_configShuttle.kcfg_enableshuttle->hide();
    m_configShuttle.kcfg_enableshuttle->setDisabled(true);
#endif /* USE_JOGSHUTTLE */
#ifdef USE_JOGSHUTTLE
    // KoraCut, 25 septembre 2026 : un nom qui dit ce que c'est, une molette USB de montage.
    m_rootJog = p6;
#else
    // KoraCut, 25 septembre 2026 : sans pilote (macOS, Windows), la page ne disait que « désactivée ».
    // On ne l'ajoute pas ; ses champs restent en vie, cachés, pour le code qui les lit.
    p6->setParent(this);
    p6->hide();
#endif
}

void KdenliveSettingsDialog::openBrowserUrl(const QString &url)
{
    qDebug() << "=== LINK CLICKED: " << url;
    auto *job = new KIO::OpenUrlJob(QUrl(url));
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
    // methods like setRunExecutables, setSuggestedFilename, setEnableExternalBrowser, setFollowRedirections
    // exist in both classes
    job->start();
}

void KdenliveSettingsDialog::initTranscodePage()
{
    QWidget *p8 = new QWidget;
    m_configTranscode.setupUi(p8);
    // KoraCut, 25 septembre 2026 : « Convertir » ne disait pas qu'il s'agit des formats proposés par
    // le menu Convertir du chutier.
    m_rootTranscode = p8;

    connect(m_configTranscode.button_add, &QAbstractButton::clicked, this, &KdenliveSettingsDialog::slotAddTranscode);
    connect(m_configTranscode.button_delete, &QAbstractButton::clicked, this, &KdenliveSettingsDialog::slotDeleteTranscode);
    connect(m_configTranscode.profiles_list, &QListWidget::itemChanged, this, &KdenliveSettingsDialog::slotDialogModified);
    connect(m_configTranscode.profiles_list, &QListWidget::currentRowChanged, this, &KdenliveSettingsDialog::slotSetTranscodeProfile);
    connect(m_configTranscode.profile_description, &QLineEdit::textChanged, this, &KdenliveSettingsDialog::slotEnableTranscodeUpdate);
    connect(m_configTranscode.profile_extension, &QLineEdit::textChanged, this, &KdenliveSettingsDialog::slotEnableTranscodeUpdate);
    connect(m_configTranscode.profile_parameters, &QPlainTextEdit::textChanged, this, &KdenliveSettingsDialog::slotEnableTranscodeUpdate);
    connect(m_configTranscode.profile_audioonly, &QCheckBox::checkStateChanged, this, &KdenliveSettingsDialog::slotEnableTranscodeUpdate);

    connect(m_configTranscode.button_update, &QAbstractButton::pressed, this, &KdenliveSettingsDialog::slotUpdateTranscodingProfile);

    m_configTranscode.profile_parameters->setMaximumHeight(QFontMetrics(font()).lineSpacing() * 5);
    loadTranscodeProfiles();
}

void KdenliveSettingsDialog::initSdlPage(bool gpuAllowed)
{
    connect(m_configSdl.reload_blackmagic, &QAbstractButton::clicked, this, &KdenliveSettingsDialog::slotReloadBlackMagic);
    // m_configSdl.kcfg_openglmonitors->setHidden(true);
    connect(m_configSdl.fullscreen_monitor, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &KdenliveSettingsDialog::slotDialogModified);
    connect(m_configSdl.kcfg_audio_driver, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &KdenliveSettingsDialog::slotCheckAlsaDriver);
    connect(m_configSdl.kcfg_audio_backend, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &KdenliveSettingsDialog::slotCheckAudioBackend);

    // enable GPU accel only if Movit is found
    m_configSdl.kcfg_gpu_accel->setEnabled(gpuAllowed);
    m_configSdl.kcfg_gpu_accel->setToolTip(i18n("GPU processing needs MLT compiled with Movit and Rtaudio modules"));
    if (!getBlackMagicOutputDeviceList(m_configSdl.kcfg_blackmagic_output_device)) {
        // No blackmagic card found
        m_configSdl.kcfg_external_display->setEnabled(false);
    }
}

bool KdenliveSettingsDialog::getBlackMagicOutputDeviceList(QComboBox *devicelist, bool force)
{
    if (!force && !KdenliveSettings::decklink_device_found()) {
        return false;
    }
    Mlt::Profile profile;
    Mlt::Consumer bm(profile, "decklink");
    int found_devices = 0;
    if (bm.is_valid()) {
        bm.set("list_devices", 1);
        found_devices = bm.get_int("devices");
    } else {
        KdenliveSettings::setDecklink_device_found(false);
    }
    if (found_devices <= 0) {
        devicelist->setEnabled(false);
        return false;
    }
    KdenliveSettings::setDecklink_device_found(true);
    for (int i = 0; i < found_devices; ++i) {
        char *tmp = qstrdup(QStringLiteral("device.%1").arg(i).toUtf8().constData());
        devicelist->addItem(bm.get(tmp));
        delete[] tmp;
    }
    devicelist->addItem(QStringLiteral("test"));
    return true;
}

void KdenliveSettingsDialog::setupJogshuttleBtns(const QString &device)
{
    QList<QComboBox *> list;
    QList<QLabel *> list1;

    list << m_configShuttle.shuttle1;
    list << m_configShuttle.shuttle2;
    list << m_configShuttle.shuttle3;
    list << m_configShuttle.shuttle4;
    list << m_configShuttle.shuttle5;
    list << m_configShuttle.shuttle6;
    list << m_configShuttle.shuttle7;
    list << m_configShuttle.shuttle8;
    list << m_configShuttle.shuttle9;
    list << m_configShuttle.shuttle10;
    list << m_configShuttle.shuttle11;
    list << m_configShuttle.shuttle12;
    list << m_configShuttle.shuttle13;
    list << m_configShuttle.shuttle14;
    list << m_configShuttle.shuttle15;

    list1 << m_configShuttle.label_2;  // #1
    list1 << m_configShuttle.label_4;  // #2
    list1 << m_configShuttle.label_3;  // #3
    list1 << m_configShuttle.label_7;  // #4
    list1 << m_configShuttle.label_5;  // #5
    list1 << m_configShuttle.label_6;  // #6
    list1 << m_configShuttle.label_8;  // #7
    list1 << m_configShuttle.label_9;  // #8
    list1 << m_configShuttle.label_10; // #9
    list1 << m_configShuttle.label_11; // #10
    list1 << m_configShuttle.label_12; // #11
    list1 << m_configShuttle.label_13; // #12
    list1 << m_configShuttle.label_14; // #13
    list1 << m_configShuttle.label_15; // #14
    list1 << m_configShuttle.label_16; // #15

    for (int i = 0; i < list.count(); ++i) {
        list[i]->hide();
        list1[i]->hide();
    }
#ifdef USE_JOGSHUTTLE
    if (!m_configShuttle.kcfg_enableshuttle->isChecked()) {
        return;
    }
    int keysCount = JogShuttle::keysCount(device);

    for (int i = 0; i < keysCount; ++i) {
        m_shuttle_buttons.push_back(list[i]);
        list[i]->show();
        list1[i]->show();
    }

    // populate the buttons with the current configuration. The items are sorted
    // according to the user-selected language, so they do not appear in random order.
    QMap<QString, QString> mappable_actions(m_mappable_actions);
    QList<QString> action_names = mappable_actions.keys();

    std::sort(action_names.begin(), action_names.end());

    // Here we need to compute the action_id -> index-in-action_names. We iterate over the
    // action_names, as the sorting may depend on the user-language.
    QStringList actions_map = JogShuttleConfig::actionMap(KdenliveSettings::shuttlebuttons());
    QMap<QString, int> action_pos;
    for (const QString &action_id : std::as_const(actions_map)) {
        // This loop find out at what index is the string that would map to the action_id.
        for (int i = 0; i < action_names.size(); ++i) {
            if (mappable_actions[action_names.at(i)] == action_id) {
                action_pos[action_id] = i;
                break;
            }
        }
    }

    int i = 0;
    for (QComboBox *button : std::as_const(m_shuttle_buttons)) {
        button->addItems(action_names);
        connect(button, SIGNAL(activated(int)), this, SLOT(slotShuttleModified()));
        ++i;
        if (i < actions_map.size()) {
            button->setCurrentIndex(action_pos[actions_map[i]]);
        }
    }
#else
    Q_UNUSED(device)
#endif
}

KdenliveSettingsDialog::~KdenliveSettingsDialog() = default;

void KdenliveSettingsDialog::slotUpdateGrabRegionStatus()
{
    m_configCapture.region_group->setHidden(m_configCapture.kcfg_grab_capture_type->currentIndex() != 1);
}

void KdenliveSettingsDialog::slotRevealCaptureFolder(int ix)
{
    m_configEnv.capturefolderurl->setVisible(ix == 2);
    m_configEnv.kcfg_captureprojectsubfolder->setVisible(ix == 3);
}

void KdenliveSettingsDialog::slotEnableLibraryFolder()
{
    m_configEnv.libraryfolderurl->setEnabled(!m_configEnv.kcfg_librarytodefaultfolder->isChecked());
}

void KdenliveSettingsDialog::slotEnableVideoFolder(int ix)
{
    m_configEnv.videofolderurl->setEnabled(ix == KdenliveDoc::SaveToCustomFolder);
}

void KdenliveSettingsDialog::initDevices()
{
    // Fill audio drivers
    m_configSdl.kcfg_audio_driver->addItem(i18n("Automatic"), QString());
#if defined(Q_OS_WIN)
    // TODO: i18n
    m_configSdl.kcfg_audio_driver->addItem("DirectSound", "directsound");
    m_configSdl.kcfg_audio_driver->addItem("WinMM", "winmm");
    m_configSdl.kcfg_audio_driver->addItem("Wasapi", "wasapi");
#elif !defined(Q_WS_MAC)
    m_configSdl.kcfg_audio_driver->addItem(i18n("ALSA"), "alsa");
    m_configSdl.kcfg_audio_driver->addItem(i18n("PulseAudio"), "pulseaudio");
    m_configSdl.kcfg_audio_driver->addItem(i18n("PipeWire"), "pipewire");
    m_configSdl.kcfg_audio_driver->addItem(i18n("OSS"), "dsp");
    // m_configSdl.kcfg_audio_driver->addItem(i18n("OSS with DMA access"), "dma");
    m_configSdl.kcfg_audio_driver->addItem(i18n("Esound Daemon"), "esd");
    m_configSdl.kcfg_audio_driver->addItem(i18n("ARTS Daemon"), "artsc");
#endif

    if (!KdenliveSettings::audiodrivername().isEmpty())
        for (int i = 1; i < m_configSdl.kcfg_audio_driver->count(); ++i) {
            if (m_configSdl.kcfg_audio_driver->itemData(i).toString() == KdenliveSettings::audiodrivername()) {
                m_configSdl.kcfg_audio_driver->setCurrentIndex(i);
                KdenliveSettings::setAudio_driver(uint(i));
            }
        }

    // Fill the list of audio playback / recording devices
    m_configSdl.kcfg_audio_device->addItem(i18n("Default"), QString());
    if (!QStandardPaths::findExecutable(QStringLiteral("aplay")).isEmpty()) {
        m_readProcess.setOutputChannelMode(KProcess::OnlyStdoutChannel);
        m_readProcess.setProgram(QStringLiteral("aplay"), QStringList() << QStringLiteral("-l"));
        connect(&m_readProcess, &KProcess::readyReadStandardOutput, this, &KdenliveSettingsDialog::slotReadAudioDevices);
        m_readProcess.execute(5000);
    } else {
        // If aplay is not installed on the system, parse the /proc/asound/pcm file
        QFile file(QStringLiteral("/proc/asound/pcm"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            QString line = stream.readLine();
            QString deviceId;
            while (!line.isNull()) {
                if (line.contains(QStringLiteral("playback"))) {
                    deviceId = line.section(QLatin1Char(':'), 0, 0);
                    m_configSdl.kcfg_audio_device->addItem(line.section(QLatin1Char(':'), 1, 1), QStringLiteral("plughw:%1,%2")
                                                                                                     .arg(deviceId.section(QLatin1Char('-'), 0, 0).toInt())
                                                                                                     .arg(deviceId.section(QLatin1Char('-'), 1, 1).toInt()));
                }
                line = stream.readLine();
            }
            file.close();
        } else {
            qCDebug(KDENLIVE_LOG) << " / / / /CANNOT READ PCM";
        }
    }

    if (!KdenliveSettings::audiodevicename().isEmpty()) {
        // Select correct alsa device
        int ix = m_configSdl.kcfg_audio_device->findData(KdenliveSettings::audiodevicename());
        m_configSdl.kcfg_audio_device->setCurrentIndex(ix);
        KdenliveSettings::setAudio_device(ix);
    }

    m_configSdl.kcfg_audio_backend->addItem(i18n("SDL"), KdenliveSettings::sdlAudioBackend());
    if (KdenliveSettings::consumerslist().isEmpty()) {
        Mlt::Properties *consumers = pCore->getMltRepository()->consumers();
        QStringList consumersItemList;
        consumersItemList.reserve(consumers->count());
        for (int i = 0; i < consumers->count(); ++i) {
            consumersItemList << consumers->get_name(i);
        }
        delete consumers;
        KdenliveSettings::setConsumerslist(consumersItemList);
    }
    if (KdenliveSettings::consumerslist().contains("rtaudio")) {
        m_configSdl.kcfg_audio_backend->addItem(i18n("RtAudio"), "rtaudio");
    }

    if (!KdenliveSettings::audiobackend().isEmpty()) {
        int ix = m_configSdl.kcfg_audio_backend->findData(KdenliveSettings::audiobackend());
        m_configSdl.kcfg_audio_backend->setCurrentIndex(ix);
        KdenliveSettings::setAudio_backend(ix);
    }
    slotCheckAudioBackend();

    // Fill monitors data
    fillMonitorData();
    connect(qApp, &QApplication::screenAdded, this, &KdenliveSettingsDialog::fillMonitorData);
    connect(qApp, &QApplication::screenRemoved, this, &KdenliveSettingsDialog::fillMonitorData);
}

void KdenliveSettingsDialog::fillMonitorData()
{
    QSignalBlocker bk(m_configSdl.fullscreen_monitor);
    m_configSdl.fullscreen_monitor->clear();
    m_configSdl.fullscreen_monitor->addItem(i18n("auto"));
    int ix = 0;
    const QList<QScreen *> screens = qApp->screens();
    for (const QScreen *screen : screens) {
        QString screenName = screen->name().isEmpty() ? i18n("Monitor %1", ix + 1) : QStringLiteral("%1: %2").arg(ix + 1).arg(screen->name());
        if (!screen->model().isEmpty()) {
            screenName.append(QStringLiteral(" - %1").arg(screen->model()));
        }
        if (!screen->manufacturer().isEmpty()) {
            screenName.append(QStringLiteral(" (%1)").arg(screen->manufacturer()));
        }
        m_configSdl.fullscreen_monitor->addItem(screenName, QStringLiteral("%1:%2").arg(QString::number(ix), screen->serialNumber()));
        ix++;
    }
    if (!KdenliveSettings::fullscreen_monitor().isEmpty()) {
        int ix = m_configSdl.fullscreen_monitor->findData(KdenliveSettings::fullscreen_monitor());
        if (ix > -1) {
            m_configSdl.fullscreen_monitor->setCurrentIndex(ix);
        } else {
            // Monitor not found, reset
            m_configSdl.fullscreen_monitor->setCurrentIndex(0);
            KdenliveSettings::setFullscreen_monitor(QString());
        }
    }
}

void KdenliveSettingsDialog::slotReadAudioDevices()
{
    QString result = QString(m_readProcess.readAllStandardOutput());
    const QStringList lines = result.split(QLatin1Char('\n'));
    for (const QString &devicestr : lines) {
        if (!devicestr.startsWith(QLatin1Char(' ')) && devicestr.count(QLatin1Char(':')) > 1) {
            QString card = devicestr.section(QLatin1Char(':'), 0, 0).section(QLatin1Char(' '), -1);
            QString device = devicestr.section(QLatin1Char(':'), 1, 1).section(QLatin1Char(' '), -1);
            m_configSdl.kcfg_audio_device->addItem(devicestr.section(QLatin1Char(':'), -1).simplified(), QStringLiteral("plughw:%1,%2").arg(card, device));
        }
    }
}

void KdenliveSettingsDialog::showKoraCutAiPage(const QString &id)
{
    if (!m_pageKoraCutAi) {
        return;
    }
    setCurrentPage(m_pageKoraCutAi);
#if KORACUT_AGENTS
    if (!id.isEmpty()) {
        // KConfigDialog enveloppe la page dans un cadre : on la cherche dedans.
        auto *page = qobject_cast<KoraCutAiConnectionsPage *>(m_pageKoraCutAi->widget());
        if (!page && m_pageKoraCutAi->widget()) {
            page = m_pageKoraCutAi->widget()->findChild<KoraCutAiConnectionsPage *>();
        }
        if (page) {
            page->selectConnection(id);
        }
    }
#else
    Q_UNUSED(id)
#endif
}

void KdenliveSettingsDialog::showPage(Kdenlive::ConfigPage page, int option)
{
    switch (page) {
    case Kdenlive::PageMisc:
        setCurrentPage(m_pageMisc);
        break;
    case Kdenlive::PageEnv:
        setCurrentPage(m_pageEnv);
        break;
    case Kdenlive::PageTimeline:
        setCurrentPage(m_pageTimeline);
        break;
    case Kdenlive::PageTools:
        setCurrentPage(m_pageTools);
        break;
    case Kdenlive::PageCapture:
        setCurrentPage(m_pageCapture);
        m_configCapture.tabWidget->setCurrentIndex(option);
        break;
    case Kdenlive::PageJogShuttle:
        if (m_pageJog) {
            setCurrentPage(m_pageJog);
        }
        break;
    case Kdenlive::PagePlayback:
        setCurrentPage(m_pagePlay);
        break;
    case Kdenlive::PageTranscode:
        setCurrentPage(m_pageTranscode);
        break;
    case Kdenlive::PageProjectDefaults:
        setCurrentPage(m_pageProject);
        break;
    case Kdenlive::PageColorsGuides:
        setCurrentPage(m_pageColors);
        break;
    case Kdenlive::PageSpeech: {
        if (option == 0) {
            m_pluginsPage->checkSpeechDependencies();
        }
        setCurrentPage(m_pageSpeech);
        m_pluginsPage->setActiveTab(option);
        break;
    }
    default:
        setCurrentPage(m_pageMisc);
    }
}

void KdenliveSettingsDialog::slotEditAudioApplication()
{
    QUrl url = KUrlRequesterDialog::getUrl(QUrl::fromLocalFile(KdenliveSettings::defaultaudioapp()), this, i18n("Enter path to the audio editing application"));
    if (!url.isEmpty()) {
        m_configEnv.kcfg_defaultaudioapp->setText(url.toLocalFile());
    }
}

void KdenliveSettingsDialog::slotEditImageApplication()
{
    QUrl url = KUrlRequesterDialog::getUrl(QUrl::fromLocalFile(KdenliveSettings::defaultimageapp()), this, i18n("Enter path to the image editing application"));
    if (!url.isEmpty()) {
        m_configEnv.kcfg_defaultimageapp->setText(url.toLocalFile());
    }
}

void KdenliveSettingsDialog::slotEditGlaxnimateApplication()
{
    QUrl url = KUrlRequesterDialog::getUrl(QUrl::fromLocalFile(KdenliveSettings::glaxnimatePath()), this, i18n("Enter path to the Glaxnimate application"));
    if (!url.isEmpty()) {
        m_configEnv.kcfg_glaxnimatePath->setText(url.toLocalFile());
    }
}

void KdenliveSettingsDialog::slotEditVideoApplication()
{
    QUrl url = KUrlRequesterDialog::getUrl(QUrl::fromLocalFile(KdenliveSettings::defaultvideoapp()), this, i18n("Enter path to the video editing application"));
    if (!url.isEmpty()) {
        m_configEnv.kcfg_defaultvideoapp->setText(url.toLocalFile());
    }
}

void KdenliveSettingsDialog::slotCheckShuttle(int state)
{
#ifdef USE_JOGSHUTTLE
    m_configShuttle.config_group->setEnabled(state != Qt::Unchecked);
    m_configShuttle.shuttledevicelist->clear();

    QStringList devNames = KdenliveSettings::shuttledevicenames();
    QStringList devPaths = KdenliveSettings::shuttledevicepaths();

    if (devNames.count() != devPaths.count()) {
        return;
    }
    for (int i = 0; i < devNames.count(); ++i) {
        m_configShuttle.shuttledevicelist->addItem(devNames.at(i), devPaths.at(i));
    }
    if (state != Qt::Unchecked) {
        setupJogshuttleBtns(m_configShuttle.shuttledevicelist->itemData(m_configShuttle.shuttledevicelist->currentIndex()).toString());
    }
#else
    Q_UNUSED(state)
#endif /* USE_JOGSHUTTLE */
}

void KdenliveSettingsDialog::slotUpdateShuttleDevice(int ix)
{
#ifdef USE_JOGSHUTTLE
    QString device = m_configShuttle.shuttledevicelist->itemData(ix).toString();
    // KdenliveSettings::setShuttledevice(device);
    setupJogshuttleBtns(device);
    m_configShuttle.kcfg_shuttledevice->setText(device);
#else
    Q_UNUSED(ix)
#endif /* USE_JOGSHUTTLE */
}

void KdenliveSettingsDialog::updateWidgets()
{
// Revert widgets to last saved state (for example when user pressed "Cancel")
#ifdef USE_JOGSHUTTLE
    // revert jog shuttle device
    if (m_configShuttle.shuttledevicelist->count() > 0) {
        for (int i = 0; i < m_configShuttle.shuttledevicelist->count(); ++i) {
            if (m_configShuttle.shuttledevicelist->itemData(i) == KdenliveSettings::shuttledevice()) {
                m_configShuttle.shuttledevicelist->setCurrentIndex(i);
                break;
            }
        }
    }

    // Revert jog shuttle buttons
    QList<QString> action_names = m_mappable_actions.keys();
    std::sort(action_names.begin(), action_names.end());
    QStringList actions_map = JogShuttleConfig::actionMap(KdenliveSettings::shuttlebuttons());
    QMap<QString, int> action_pos;
    for (const QString &action_id : std::as_const(actions_map)) {
        // This loop find out at what index is the string that would map to the action_id.
        for (int i = 0; i < action_names.size(); ++i) {
            if (m_mappable_actions[action_names[i]] == action_id) {
                action_pos[action_id] = i;
                break;
            }
        }
    }
    int i = 0;
    for (QComboBox *button : std::as_const(m_shuttle_buttons)) {
        ++i;
        if (i < actions_map.size()) {
            button->setCurrentIndex(action_pos[actions_map[i]]);
        }
    }
#endif /* USE_JOGSHUTTLE */
}

void KdenliveSettingsDialog::accept()
{
    if (m_pw->selectedProfile().isEmpty()) {
        KMessageBox::error(this, i18n("Please select a video profile"));
        return;
    }
    Kdenlive::ConfigPage page = Kdenlive::NoPage;
    auto *current = currentPage();
    if (current == m_pageMisc) {
        page = Kdenlive::PageMisc;
    } else if (current == m_pageEnv) {
        page = Kdenlive::PageEnv;
    } else if (current == m_pageTimeline) {
        page = Kdenlive::PageTimeline;
    } else if (current == m_pageTools) {
        page = Kdenlive::PageTools;
    } else if (current == m_pageCapture) {
        page = Kdenlive::PageMisc;
    } else if (current == m_pageCapture) {
        page = Kdenlive::PageCapture;
    } else if (current == m_pageJog) {
        page = Kdenlive::PageJogShuttle;
    } else if (current == m_pagePlay) {
        page = Kdenlive::PagePlayback;
    } else if (current == m_pageTranscode) {
        page = Kdenlive::PageTranscode;
    } else if (current == m_pageProject) {
        page = Kdenlive::PageProjectDefaults;
    } else if (current == m_pageColors) {
        page = Kdenlive::PageColorsGuides;
    } else if (current == m_pageSpeech) {
        page = Kdenlive::PageSpeech;
    }

    pCore->window()->m_lastConfigPage = page;
    KConfigDialog::accept();
}

void KdenliveSettingsDialog::updateExternalApps()
{
    m_configEnv.kcfg_defaultimageapp->setText(KdenliveSettings::defaultimageapp());
    m_configEnv.kcfg_defaultaudioapp->setText(KdenliveSettings::defaultaudioapp());
    m_configEnv.kcfg_glaxnimatePath->setText(KdenliveSettings::glaxnimatePath());
    m_configEnv.kcfg_defaultvideoapp->setText(KdenliveSettings::defaultvideoapp());
}

void KdenliveSettingsDialog::updateSettings()
{
    // Save changes to settings (for example when user pressed "Apply" or "Ok")
    if (m_pw->selectedProfile().isEmpty()) {
        KMessageBox::error(this, i18n("Please select a video profile"));
        return;
    }
    KdenliveSettings::setDefault_profile(m_pw->selectedProfile());

    if (m_configEnv.ffmpegurl->text().isEmpty()) {
        QString infos;
        QString warnings;
        QString errors;
        Wizard::slotCheckPrograms(infos, warnings, errors);
        m_configEnv.ffmpegurl->setText(KdenliveSettings::ffmpegpath());
        m_configEnv.ffplayurl->setText(KdenliveSettings::ffplaypath());
        m_configEnv.ffprobeurl->setText(KdenliveSettings::ffprobepath());
    }

    if (m_configTimeline.kcfg_trackheight->value() == 0) {
        QFont ft = QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont);
        // Default unit for timeline.qml objects size
        int baseUnit = qMax(28, qRound(QFontInfo(ft).pixelSize() * 1.8));
        int trackHeight = qMax(50, int(2.2 * baseUnit + 6));
        m_configTimeline.kcfg_trackheight->setValue(trackHeight);
    } else if (m_configTimeline.kcfg_trackheight->value() != KdenliveSettings::trackheight()) {
        QFont ft = QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont);
        // Default unit for timeline.qml objects size
        int baseUnit = qMax(28, qRound(QFontInfo(ft).pixelSize() * 1.8));
        if (m_configTimeline.kcfg_trackheight->value() < baseUnit) {
            m_configTimeline.kcfg_trackheight->setValue(baseUnit);
        }
    }

    bool resetConsumer = false;
    bool fullReset = false;
    bool updateLibrary = false;

    // Guide categories
    const QStringList updatedGuides = m_guidesCategories->updatedGuides();
    if (KdenliveSettings::guidesCategories() != updatedGuides) {
        KdenliveSettings::setGuidesCategories(updatedGuides);
    }

    /*if (m_configShuttle.shuttledevicelist->count() > 0) {
    QString device = m_configShuttle.shuttledevicelist->itemData(m_configShuttle.shuttledevicelist->currentIndex()).toString();
    if (device != KdenliveSettings::shuttledevice()) KdenliveSettings::setShuttledevice(device);
    }*/
    m_tlPreviewProfiles->hideMessage();
    if (m_configProject.projecturl->url().toLocalFile() != KdenliveSettings::defaultprojectfolder()) {
        KdenliveSettings::setDefaultprojectfolder(m_configProject.projecturl->url().toLocalFile());
        if (!KdenliveSettings::sameprojectfolder()) {
            m_configEnv.kcfg_videotodefaultfolder->setItemText(KdenliveDoc::SaveToProjectFolder,
                                                               i18n("Always use project folder: %1", KdenliveSettings::defaultprojectfolder()));
            m_configEnv.kcfg_capturetoprojectfolder->setItemText(1, i18n("Always use project folder: %1", KdenliveSettings::defaultprojectfolder()));
        }
    }

    if (m_configMisc.kcfg_enableBuiltInEffects->isChecked() != KdenliveSettings::enableBuiltInEffects()) {
        KdenliveSettings::setEnableBuiltInEffects(m_configMisc.kcfg_enableBuiltInEffects->isChecked());
        pCore->window()->reloadAssetPanel();
    }

    if (m_configProject.kcfg_customprojectfolder->isChecked() != KdenliveSettings::customprojectfolder()) {
        if (KdenliveSettings::customprojectfolder()) {
            m_configEnv.kcfg_videotodefaultfolder->setItemText(KdenliveDoc::SaveToProjectFolder, i18n("Always use active project folder"));
            m_configEnv.kcfg_capturetoprojectfolder->setItemText(1, i18n("Always use active project folder"));
        } else {
            m_configEnv.kcfg_videotodefaultfolder->setItemText(KdenliveDoc::SaveToProjectFolder,
                                                               i18n("Always use project folder: %1", KdenliveSettings::defaultprojectfolder()));
            m_configEnv.kcfg_capturetoprojectfolder->setItemText(1, i18n("Always use project folder: %1", KdenliveSettings::defaultprojectfolder()));
        }
    }

    if (m_configSdl.fullscreen_monitor->currentData().toString() != KdenliveSettings::fullscreen_monitor()) {
        KdenliveSettings::setFullscreen_monitor(m_configSdl.fullscreen_monitor->currentData().toString());
    }

    if (m_configEnv.capturefolderurl->url().toLocalFile() != KdenliveSettings::capturefolder()) {
        KdenliveSettings::setCapturefolder(m_configEnv.capturefolderurl->url().toLocalFile());
    }

    if (m_configEnv.kcfg_captureprojectsubfolder->text() != KdenliveSettings::captureprojectsubfolder()) {
        KdenliveSettings::setCaptureprojectsubfolder(m_configEnv.kcfg_captureprojectsubfolder->text());
    }

    // Library default folder
    if (m_configEnv.kcfg_librarytodefaultfolder->isChecked() != KdenliveSettings::librarytodefaultfolder()) {
        KdenliveSettings::setLibrarytodefaultfolder(m_configEnv.kcfg_librarytodefaultfolder->isChecked());
        updateLibrary = true;
    }

    if (m_configEnv.libraryfolderurl->url().toLocalFile() != KdenliveSettings::libraryfolder()) {
        KdenliveSettings::setLibraryfolder(m_configEnv.libraryfolderurl->url().toLocalFile());
        if (!KdenliveSettings::librarytodefaultfolder()) {
            updateLibrary = true;
        }
    }

    // Check if screengrab is fullscreen
    if (m_configCapture.kcfg_grab_capture_type->currentIndex() != KdenliveSettings::grab_capture_type()) {
        KdenliveSettings::setGrab_capture_type(m_configCapture.kcfg_grab_capture_type->currentIndex());
    }

    bool audioRecDeviceChanged = false;
    if (m_configCapture.defaultaudiocapture->currentText() != KdenliveSettings::defaultaudiocapture()) {
        KdenliveSettings::setDefaultaudiocapture(m_configCapture.defaultaudiocapture->currentText());
        audioRecDeviceChanged = true;
    }

    // Check audio capture changes
    if (KdenliveSettings::audiocapturechannels() != m_configCapture.audiocapturechannels->currentData().toInt() ||
        KdenliveSettings::audiocapturevolume() != m_configCapture.kcfg_audiocapturevolume->value() ||
        KdenliveSettings::audiocapturesamplerate() != m_configCapture.audiocapturesamplerate->currentData().toInt() ||
        KdenliveSettings::audiocapturesampleformat() != m_configCapture.audiocapturesampleformat->currentData().toInt()) {
        KdenliveSettings::setAudiocapturechannels(m_configCapture.audiocapturechannels->currentData().toInt());
        KdenliveSettings::setAudiocapturevolume(m_configCapture.kcfg_audiocapturevolume->value());
        KdenliveSettings::setAudiocapturesamplerate(m_configCapture.audiocapturesamplerate->currentData().toInt());
        KdenliveSettings::setAudiocapturesampleformat(m_configCapture.audiocapturesampleformat->currentData().toInt());
        audioRecDeviceChanged = true;
    }

    if (audioRecDeviceChanged) {
        pCore->resetAudioMonitoring();
    }

    // screengrab
    QString string = m_grabProfiles->currentParams();
    if (string != KdenliveSettings::grab_parameters()) {
        KdenliveSettings::setGrab_parameters(string);
    }
    string = m_grabProfiles->currentExtension();
    if (string != KdenliveSettings::grab_extension()) {
        KdenliveSettings::setGrab_extension(string);
    }

    // decklink
    string = m_decklinkProfiles->currentParams();
    if (string != KdenliveSettings::decklink_parameters()) {
        KdenliveSettings::setDecklink_parameters(string);
    }
    string = m_decklinkProfiles->currentExtension();
    if (string != KdenliveSettings::decklink_extension()) {
        KdenliveSettings::setDecklink_extension(string);
    }

    // proxies
    string = m_proxyProfiles->currentParams();
    if (string != KdenliveSettings::proxyparams()) {
        KdenliveSettings::setProxyparams(string);
    }
    string = m_proxyProfiles->currentExtension();
    if (string != KdenliveSettings::proxyextension()) {
        KdenliveSettings::setProxyextension(string);
    }

    // proxies with alpha
    string = m_alphaProxyProfiles->currentParams();
    if (string != KdenliveSettings::proxyalphaparams()) {
        KdenliveSettings::setProxyalphaparams(string);
    }
    string = m_alphaProxyProfiles->currentExtension();
    if (string != KdenliveSettings::proxyalphaextension()) {
        KdenliveSettings::setProxyalphaextension(string);
    }

    // external proxies
    QString profilestr = m_configProxy.kcfg_external_proxy_profile->currentData().toString();
    if (!profilestr.isEmpty() && (profilestr != KdenliveSettings::externalProxyProfile())) {
        KdenliveSettings::setExternalProxyProfile(profilestr);
    }

    // timeline preview
    string = m_tlPreviewProfiles->currentParams();
    if (string != KdenliveSettings::previewparams()) {
        KdenliveSettings::setPreviewparams(string);
    }
    string = m_tlPreviewProfiles->currentExtension();
    if (string != KdenliveSettings::previewextension()) {
        KdenliveSettings::setPreviewextension(string);
    }

    // Breeze drag
    if (m_configEnv.dragFromTitlebarOnly->isChecked() != m_breezeDragFromTitlebarOnly) {
        slotUpdateBreezeDrag(m_configEnv.dragFromTitlebarOnly->isChecked());
    }

    // Autosave
    if (m_configMisc.kcfg_autosave_time->value() != KdenliveSettings::autosave_time()) {
        KdenliveSettings::setAutosave_time(m_configMisc.kcfg_autosave_time->value());
        pCore->projectManager()->updateAutoSaveTimer();
    }

    if (updateLibrary) {
        Q_EMIT updateLibraryFolder();
    }

    if (m_configSdl.kcfg_external_display->isChecked() != KdenliveSettings::external_display()) {
        KdenliveSettings::setExternal_display(m_configSdl.kcfg_external_display->isChecked());
        resetConsumer = true;
        fullReset = true;
    } else if (KdenliveSettings::external_display() &&
               KdenliveSettings::blackmagic_output_device() != m_configSdl.kcfg_blackmagic_output_device->currentIndex()) {
        resetConsumer = true;
        fullReset = true;
    }

    QString value = m_configSdl.kcfg_audio_driver->currentData().toString();
    if (value != KdenliveSettings::audiodrivername()) {
        KdenliveSettings::setAudiodrivername(value);
        resetConsumer = true;
    }

    if (value == QLatin1String("alsa")) {
        // Audio device setting is only valid for alsa driver
        value = m_configSdl.kcfg_audio_device->currentData().toString();
        if (value != KdenliveSettings::audiodevicename()) {
            KdenliveSettings::setAudiodevicename(value);
            resetConsumer = true;
        }
    } else if (!KdenliveSettings::audiodevicename().isEmpty()) {
        KdenliveSettings::setAudiodevicename(QString());
        resetConsumer = true;
    }

    value = m_configSdl.kcfg_audio_backend->currentData().toString();
    if (value != KdenliveSettings::audiobackend()) {
        KdenliveSettings::setAudiobackend(value);
        resetConsumer = true;
        fullReset = true;
    }

    if (m_configColors.kcfg_monitorGridH->value() != KdenliveSettings::monitorGridH()) {
        KdenliveSettings::setMonitorGridH(m_configColors.kcfg_monitorGridH->value());
    }
    if (m_configColors.kcfg_monitorGridV->value() != KdenliveSettings::monitorGridV()) {
        KdenliveSettings::setMonitorGridV(m_configColors.kcfg_monitorGridV->value());
    }

    if (m_configColors.kcfg_window_background->color() != KdenliveSettings::window_background()) {
        KdenliveSettings::setWindow_background(m_configColors.kcfg_window_background->color());
    }

    if (m_configColors.kcfg_thumbColor1->color() != KdenliveSettings::thumbColor1() ||
        m_configColors.kcfg_thumbColor2->color() != KdenliveSettings::thumbColor2()) {
        KdenliveSettings::setThumbColor1(m_configColors.kcfg_thumbColor1->color());
        KdenliveSettings::setThumbColor2(m_configColors.kcfg_thumbColor2->color());
    }

    bool colorsChanged = false;
    if (auto *tc = pCore->window()->getCurrentTimeline()->controller()) {
        QColor videoCol = m_configColors.videoColor->color();
        QColor videoDefault = tc->getDefaultClipColor(ClipType::Video);
        QColor videoToStore = (videoCol == videoDefault) ? QColor(0,0,0,0) : videoCol;
        if (videoToStore != KdenliveSettings::videoColor()) {
            KdenliveSettings::setVideoColor(videoToStore);
            colorsChanged = true;
        }

        QColor audioCol = m_configColors.audioColor->color();
        QColor audioDefault = tc->getDefaultClipColor(ClipType::Audio);
        QColor audioToStore = (audioCol == audioDefault) ? QColor(0,0,0,0) : audioCol;
        if (audioToStore != KdenliveSettings::audioColor()) {
            KdenliveSettings::setAudioColor(audioToStore);
            colorsChanged = true;
        }

        QColor titleCol = m_configColors.titleColor->color();
        QColor titleDefault = tc->getDefaultClipColor(ClipType::Text);
        QColor titleToStore = (titleCol == titleDefault) ? QColor(0,0,0,0) : titleCol;
        if (titleToStore != KdenliveSettings::titleColor()) {
            KdenliveSettings::setTitleColor(titleToStore);
            colorsChanged = true;
        }

        QColor imageCol = m_configColors.imageColor->color();
        QColor imageDefault = tc->getDefaultClipColor(ClipType::Image);
        QColor imageToStore = (imageCol == imageDefault) ? QColor(0,0,0,0) : imageCol;
        if (imageToStore != KdenliveSettings::imageColor()) {
            KdenliveSettings::setImageColor(imageToStore);
            colorsChanged = true;
        }

        QColor slideshowCol = m_configColors.slideshowColor->color();
        QColor slideshowDefault = tc->getDefaultClipColor(ClipType::SlideShow);
        QColor slideshowToStore = (slideshowCol == slideshowDefault) ? QColor(0,0,0,0) : slideshowCol;
        if (slideshowToStore != KdenliveSettings::slideshowColor()) {
            KdenliveSettings::setSlideshowColor(slideshowToStore);
            colorsChanged = true;
        }

        if (colorsChanged) {
            Q_EMIT tc->colorsChanged();
        }
    }

    if (m_configColors.kcfg_overlayColor->color() != KdenliveSettings::overlayColor()) {
        KdenliveSettings::setOverlayColor(m_configColors.kcfg_overlayColor->color());
    }

    if (m_configMisc.kcfg_tabposition->currentIndex() != KdenliveSettings::tabposition()) {
        KdenliveSettings::setTabposition(m_configMisc.kcfg_tabposition->currentIndex());
        // Reload layout to make it happen
        KDDockWidgets::LayoutSaver dockLayout(KDDockWidgets::RestoreOption_AbsoluteFloatingDockWindows);
        const QByteArray layoutData = dockLayout.serializeLayout();
        if (KdenliveSettings::tabposition() == 1) {
            KDDockWidgets::Config::self().setTabsAtBottom(true);
        } else {
            KDDockWidgets::Config::self().setTabsAtBottom(false);
        }
        dockLayout.restoreLayout(layoutData);
    }

    if (m_configTimeline.kcfg_displayallchannels->isChecked() != KdenliveSettings::displayallchannels()) {
        KdenliveSettings::setDisplayallchannels(m_configTimeline.kcfg_displayallchannels->isChecked());
    }

    if (m_modified) {
        // The transcoding profiles were modified, save.
        m_modified = false;
        saveTranscodeProfiles();
    }

    const QStringList proxyParams = m_configProxy.allowedProxyParams->toPlainText().split(QLatin1Char(','));
    if (proxyParams != KdenliveSettings::safeFFmpegParams()) {
        KdenliveSettings::setSafeFFmpegParams(proxyParams);
    }

#ifdef USE_JOGSHUTTLE
    m_shuttleModified = false;

    QStringList actions;
    actions << QStringLiteral("monitor_pause"); // the Job rest position action.
    for (QComboBox *button : std::as_const(m_shuttle_buttons)) {
        actions << m_mappable_actions[button->currentText()];
    }
    QString maps = JogShuttleConfig::actionMap(actions);
    // fprintf(stderr, "Shuttle config: %s\n", JogShuttleConfig::actionMap(actions).toLatin1().constData());
    if (KdenliveSettings::shuttlebuttons() != maps) {
        KdenliveSettings::setShuttlebuttons(maps);
    }
#endif

    bool restart = false;
    if (m_configSdl.kcfg_gpu_accel->isChecked() != KdenliveSettings::gpu_accel()) {
        // GPU setting was changed, we need to restart Kdenlive or everything will be corrupted
        if (KMessageBox::warningContinueCancel(this, i18n("KoraCut must be restarted to change this setting")) == KMessageBox::Continue) {
            restart = true;
        } else {
            m_configSdl.kcfg_gpu_accel->setChecked(KdenliveSettings::gpu_accel());
        }
    }

    if (m_configTimeline.kcfg_trackheight->value() != KdenliveSettings::trackheight()) {
        KdenliveSettings::setTrackheight(m_configTimeline.kcfg_trackheight->value());
        Q_EMIT resetView();
    }

    if (m_configTimeline.kcfg_autoscroll->isChecked() != KdenliveSettings::autoscroll()) {
        KdenliveSettings::setAutoscroll(m_configTimeline.kcfg_autoscroll->isChecked());
        Q_EMIT pCore->autoScrollChanged();
    }

    if (m_configTimeline.kcfg_centeredplayhead->isChecked() != KdenliveSettings::centeredplayhead()) {
        KdenliveSettings::setCenteredplayhead(m_configTimeline.kcfg_centeredplayhead->isChecked());
        Q_EMIT pCore->centeredPlayheadChanged();
    }

    if (m_configTimeline.kcfg_pauseonseek->isChecked() != KdenliveSettings::pauseonseek()) {
        KdenliveSettings::setPauseonseek(m_configTimeline.kcfg_pauseonseek->isChecked());
    }

    if (m_configTimeline.kcfg_seekonaddeffect->isChecked() != KdenliveSettings::seekonaddeffect()) {
        KdenliveSettings::setSeekonaddeffect(m_configTimeline.kcfg_seekonaddeffect->isChecked());
    }

    if (m_configTimeline.kcfg_scrollvertically->isChecked() != KdenliveSettings::scrollvertically()) {
        KdenliveSettings::setScrollvertically(m_configTimeline.kcfg_scrollvertically->isChecked());
    }

    m_pluginsPage->applySettings();

    // Mimes
    if (m_configEnv.kcfg_addedExtensions->text() != KdenliveSettings::addedExtensions()) {
        // Update list
        KdenliveSettings::setAddedExtensions(m_configEnv.kcfg_addedExtensions->text());
        QStringList mimes = FileFilter::getExtensions();
        std::sort(mimes.begin(), mimes.end());
        m_configEnv.supportedmimes->setPlainText(mimes.join(QLatin1Char(' ')));
    }

    // proxy/transcode max concurrent jobs
    if (m_configEnv.kcfg_proxythreads->value() != KdenliveSettings::proxythreads()) {
        KdenliveSettings::setProxythreads(m_configEnv.kcfg_proxythreads->value());
        pCore->taskManager.updateConcurrency();
    }

    // KoraCut, la porte de commande (document 18).
    if (m_koracutPorteOpen) {
        KConfigGroup koracut = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
        koracut.writeEntry("porteOuverte", m_koracutPorteOpen->isChecked());
        koracut.writeEntry("terminalCommande", m_koracutTerminalCommand->text().trimmed());
        koracut.sync();
        if (pCore->window()) {
            pCore->window()->koraCutApplyPorteSettings();
        }
    }

    KConfigDialog::settingsChangedSlot();
    // KConfigDialog::updateSettings();
    if (resetConsumer) {
        Q_EMIT doResetConsumer(fullReset);
    }
    if (restart) {
        Q_EMIT restartKdenlive();
    }
    Q_EMIT checkTabPosition();

    // remembering Config dialog size
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup settingsGroup(config, "settings");
    settingsGroup.writeEntry("dialogSize", QVariant(size()));
}

void KdenliveSettingsDialog::slotCheckAlsaDriver()
{
    QString value = m_configSdl.kcfg_audio_driver->currentData().toString();
    m_configSdl.kcfg_audio_device->setEnabled(value == QLatin1String("alsa"));
}

void KdenliveSettingsDialog::slotCheckAudioBackend()
{
    bool isSdl = m_configSdl.kcfg_audio_backend->currentData().toString().startsWith(QLatin1String("sdl"));
    m_configSdl.label_audio_driver->setEnabled(isSdl);
    m_configSdl.kcfg_audio_driver->setEnabled(isSdl);
    m_configSdl.label_audio_device->setEnabled(isSdl);
    m_configSdl.kcfg_audio_device->setEnabled(isSdl);
}

void KdenliveSettingsDialog::loadTranscodeProfiles()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("koracuttranscodingrc"), KConfig::CascadeConfig, QStandardPaths::AppDataLocation);
    KConfigGroup transConfig(config, "Transcoding");
    // read the entries
    m_configTranscode.profiles_list->blockSignals(true);
    m_configTranscode.profiles_list->clear();
    QMap<QString, QString> profiles = transConfig.entryMap();
    QMapIterator<QString, QString> i(profiles);
    while (i.hasNext()) {
        i.next();
        auto *item = new QListWidgetItem(i.key());
        QString profilestr = i.value();
        if (profilestr.contains(QLatin1Char(';'))) {
            item->setToolTip(profilestr.section(QLatin1Char(';'), 1, 1));
        }
        item->setData(Qt::UserRole, profilestr);
        m_configTranscode.profiles_list->addItem(item);
        // item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    }
    m_configTranscode.profiles_list->blockSignals(false);
    m_configTranscode.profiles_list->setCurrentRow(0);
}

void KdenliveSettingsDialog::saveTranscodeProfiles()
{
    QString transcodeFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/koracuttranscodingrc");
    KSharedConfigPtr config = KSharedConfig::openConfig(transcodeFile);
    KConfigGroup transConfig(config, "Transcoding");
    // read the entries
    transConfig.deleteGroup();
    int max = m_configTranscode.profiles_list->count();
    for (int i = 0; i < max; ++i) {
        QListWidgetItem *item = m_configTranscode.profiles_list->item(i);
        transConfig.writeEntry(item->text(), item->data(Qt::UserRole).toString());
    }
    config->sync();
}

void KdenliveSettingsDialog::slotAddTranscode()
{
    bool ok;
    QString presetName =
        QInputDialog::getText(this, i18nc("@title:window", "Enter Preset Name"), i18n("Enter the name of this preset:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    if (!m_configTranscode.profiles_list->findItems(presetName, Qt::MatchExactly).isEmpty()) {
        KMessageBox::error(this, i18n("A profile with that name already exists"));
        return;
    }
    QListWidgetItem *item = new QListWidgetItem(presetName);
    QString profilestr = m_configTranscode.profile_parameters->toPlainText();
    profilestr.append(" %1." + m_configTranscode.profile_extension->text());
    profilestr.append(';');
    if (!m_configTranscode.profile_description->text().isEmpty()) {
        profilestr.append(m_configTranscode.profile_description->text());
    }
    if (m_configTranscode.profile_audioonly->isChecked()) {
        profilestr.append(";audio");
    }
    item->setData(Qt::UserRole, profilestr);
    m_configTranscode.profiles_list->addItem(item);
    m_configTranscode.profiles_list->setCurrentItem(item);
    slotDialogModified();
}

void KdenliveSettingsDialog::slotUpdateTranscodingProfile()
{
    QListWidgetItem *item = m_configTranscode.profiles_list->currentItem();
    if (!item) {
        return;
    }
    m_configTranscode.button_update->setEnabled(false);
    QString profilestr = m_configTranscode.profile_parameters->toPlainText();
    profilestr.append(" %1." + m_configTranscode.profile_extension->text());
    profilestr.append(';');
    if (!m_configTranscode.profile_description->text().isEmpty()) {
        profilestr.append(m_configTranscode.profile_description->text());
    }
    if (m_configTranscode.profile_audioonly->isChecked()) {
        profilestr.append(QStringLiteral(";audio"));
    }
    item->setData(Qt::UserRole, profilestr);
    slotDialogModified();
}

void KdenliveSettingsDialog::slotDeleteTranscode()
{
    QListWidgetItem *item = m_configTranscode.profiles_list->currentItem();
    if (item == nullptr) {
        return;
    }
    delete item;
    slotDialogModified();
}

void KdenliveSettingsDialog::slotEnableTranscodeUpdate()
{
    if (!m_configTranscode.profile_box->isEnabled()) {
        return;
    }
    bool allow = true;
    if (m_configTranscode.profile_extension->text().isEmpty()) {
        allow = false;
    }
    m_configTranscode.button_update->setEnabled(allow);
}

void KdenliveSettingsDialog::slotSetTranscodeProfile()
{
    m_configTranscode.profile_box->setEnabled(false);
    m_configTranscode.button_update->setEnabled(false);
    m_configTranscode.profile_description->clear();
    m_configTranscode.profile_extension->clear();
    m_configTranscode.profile_parameters->clear();
    m_configTranscode.profile_audioonly->setChecked(false);
    QListWidgetItem *item = m_configTranscode.profiles_list->currentItem();
    if (!item) {
        return;
    }
    QString profilestr = item->data(Qt::UserRole).toString();
    if (profilestr.contains(QLatin1Char(';'))) {
        m_configTranscode.profile_description->setText(profilestr.section(QLatin1Char(';'), 1, 1));
        if (profilestr.section(QLatin1Char(';'), 2, 2) == QLatin1String("audio")) {
            m_configTranscode.profile_audioonly->setChecked(true);
        }
        profilestr = profilestr.section(QLatin1Char(';'), 0, 0).simplified();
    }
    m_configTranscode.profile_extension->setText(profilestr.section(QLatin1Char('.'), -1));
    m_configTranscode.profile_parameters->setPlainText(profilestr.section(QLatin1Char(' '), 0, -2));
    m_configTranscode.profile_box->setEnabled(true);
}

void KdenliveSettingsDialog::slotShuttleModified()
{
#ifdef USE_JOGSHUTTLE
    QStringList actions;
    actions << QStringLiteral("monitor_pause"); // the Job rest position action.
    for (QComboBox *button : std::as_const(m_shuttle_buttons)) {
        actions << m_mappable_actions[button->currentText()];
    }
    QString maps = JogShuttleConfig::actionMap(actions);
    m_shuttleModified = KdenliveSettings::shuttlebuttons() != maps;
#endif
    KConfigDialog::updateButtons();
}

void KdenliveSettingsDialog::slotDialogModified()
{
    m_modified = true;
    KConfigDialog::updateButtons();
}

// virtual
bool KdenliveSettingsDialog::hasChanged()
{
    if (m_modified || m_shuttleModified) {
        return true;
    }

    if (KdenliveSettings::defaultaudiocapture() != m_configCapture.defaultaudiocapture->currentText()) {
        return true;
    }
    if (KdenliveSettings::audiocapturechannels() != m_configCapture.audiocapturechannels->currentData().toInt()) {
        return true;
    }
    if (KdenliveSettings::audiocapturesamplerate() != m_configCapture.audiocapturesamplerate->currentData().toInt()) {
        return true;
    }
    if (KdenliveSettings::audiocapturesampleformat() != m_configCapture.audiocapturesampleformat->currentData().toInt()) {
        return true;
    }
    return KConfigDialog::hasChanged();
}

void KdenliveSettingsDialog::loadExternalProxyProfiles()
{
    // load proxy profiles
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("externalproxies.rc"), KConfig::CascadeConfig, QStandardPaths::AppDataLocation), "proxy");
    QMap<QString, QString> values = group.entryMap();
    QMapIterator<QString, QString> k(values);
    QString currentItem = KdenliveSettings::externalProxyProfile();
    m_configProxy.kcfg_external_proxy_profile->blockSignals(true);
    m_configProxy.kcfg_external_proxy_profile->clear();
    while (k.hasNext()) {
        k.next();
        if (!k.key().isEmpty()) {
            if (k.value().contains(QLatin1Char(';'))) {
                m_configProxy.kcfg_external_proxy_profile->addItem(k.key(), k.value());
            }
        }
    }
    if (!currentItem.isEmpty()) {
        m_configProxy.kcfg_external_proxy_profile->setCurrentIndex(m_configProxy.kcfg_external_proxy_profile->findData(currentItem));
    }
    m_configProxy.kcfg_external_proxy_profile->blockSignals(false);
}

void KdenliveSettingsDialog::slotReloadBlackMagic()
{
    getBlackMagicDeviceList(m_configCapture.kcfg_decklink_capturedevice, true);
    if (!getBlackMagicOutputDeviceList(m_configSdl.kcfg_blackmagic_output_device, true)) {
        // No blackmagic card found
        m_configSdl.kcfg_external_display->setEnabled(false);
    }
    m_configSdl.kcfg_external_display->setEnabled(KdenliveSettings::decklink_device_found());
}

void KdenliveSettingsDialog::checkProfile()
{
    m_pw->loadProfile(KdenliveSettings::default_profile().isEmpty() ? pCore->getCurrentProfile()->path() : KdenliveSettings::default_profile());
}

void KdenliveSettingsDialog::slotReloadShuttleDevices()
{
#ifdef USE_JOGSHUTTLE
    QString devDirStr = QStringLiteral("/dev/input/by-id");
    QDir devDir(devDirStr);
    if (!devDir.exists()) {
        devDirStr = QStringLiteral("/dev/input");
    }

    QStringList devNamesList;
    QStringList devPathList;
    m_configShuttle.shuttledevicelist->clear();

    DeviceMap devMap = JogShuttle::enumerateDevices(devDirStr);
    DeviceMapIter iter = devMap.begin();
    while (iter != devMap.end()) {
        m_configShuttle.shuttledevicelist->addItem(iter.key(), iter.value());
        devNamesList << iter.key();
        devPathList << iter.value();
        ++iter;
    }
    KdenliveSettings::setShuttledevicenames(devNamesList);
    KdenliveSettings::setShuttledevicepaths(devPathList);
    QTimer::singleShot(200, this, [&]() { slotUpdateShuttleDevice(); });
#endif // USE_JOGSHUTTLE
}

void KdenliveSettingsDialog::showHelp()
{
    pCore->window()->appHelpActivated();
}

// ---- KoraCut, 25 septembre 2026 : la fenêtre Préférences refaite ------------------------------
// Les pages de Kdenlive sont préparées comme avant (setupUi, init*Page), puis leurs champs sont
// rangés ici dans des sections et des cartes (src/koracut/koracutpreferences.h). Aucun réglage ne
// disparaît : ce qui n'aurait pas été rangé tombe dans « Autres réglages » (section Performances),
// avec un avertissement dans le journal.

void KdenliveSettingsDialog::buildKoraCutPages()
{
    setWindowTitle(i18nc("@title:window", "Preferences"));
    m_koracutShell = new KoraCutPrefsShell(this, pageWidget(), buttonBox());
    const auto help = [](QWidget *w) -> QString {
        if (auto *b = qobject_cast<KContextualHelpButton *>(w)) {
            const QString t = b->contextualHelpText().isEmpty() ? b->text() : b->contextualHelpText();
            return QTextDocumentFragment::fromHtml(t).toPlainText().simplified();
        }
        return QString();
    };
    // Rétablir une page : ses champs kcfg_ reprennent leur valeur par défaut, puis s'enregistrent
    // comme n'importe quel changement.
    const auto restoreFields = [](QWidget *page) {
        auto *manager = new KConfigDialogManager(page, KdenliveSettings::self());
        manager->updateWidgetsDefault();
        delete manager;
    };
    const auto section = [this, restoreFields](KoraCutPrefsPage *page, const QString &name, const QString &icon, bool restore = true) {
        if (restore) {
            page->setRestoreHandler([page, restoreFields]() { restoreFields(page); });
        }
        // Des mots de recherche en plus, dans les deux langues et avec les noms techniques.
        static const QHash<QString, QString> keywords = {
            {QStringLiteral("general"), QStringLiteral("startup demarrage autosave sauvegarde crash plantage veille sleep update mise a jour")},
            {QStringLiteral("projets"), QStringLiteral("project projet profile profil format fps resolution tracks pistes preview apercu")},
            {QStringLiteral("import"), QStringLiteral("import mime extension metadata metadonnees exif exiftool magic lantern streams")},
            {QStringLiteral("couleurs"), QStringLiteral("colors colours color couleur marker marqueur guide repere grid grille safe")},
            {QStringLiteral("timeline"), QStringLiteral("timeline scroll defilement thumbnails vignettes waveform playhead tete de lecture")},
            {QStringLiteral("edition"), QStringLiteral("effects effets tools outils durations durees keyframes images cles razor rasoir compositing")},
            {QStringLiteral("lecture"), QStringLiteral("playback audio sound son driver pilote gpu movit sdl fullscreen plein ecran blackmagic")},
            {QStringLiteral("enregistrement"), QStringLiteral("capture record recording micro microphone screen ecran blackmagic decklink")},
            {QStringLiteral("proxys"), QStringLiteral("proxy proxies mandataire copie legere camera")},
            {QStringLiteral("conversion"), QStringLiteral("transcode transcodage convert convertir format dnxhd")},
            {QStringLiteral("reconnaissance"), QStringLiteral("speech whisper vosk seamless sous-titres subtitles transcription objets objects sam")},
            {QStringLiteral("pilotage"), QStringLiteral("mcp koracut-ctl porte port claude code terminal agent")},
            {QStringLiteral("performances"), QStringLiteral("performance cpu processeur threads cache debug debogage diagnostic")},
            {QStringLiteral("dossiers"), QStringLiteral("folders dossier tmp temporary library bibliotheque export rendu")},
            {QStringLiteral("programmes"), QStringLiteral("programs ffmpeg ffplay ffprobe mlt melt glaxnimate gimp audacity")},
            {QStringLiteral("apparence"), QStringLiteral("appearance theme themes dark light sombre clair mode couleurs matrix contraste onglets tabs")},
            {QStringLiteral("connexions-ia"), QStringLiteral("ai ia llm api key cle claude anthropic openai gpt gemini mistral ollama modele model")},
        };
        page->setKeywords(keywords.value(page->id()));
        KPageWidgetItem *item = addPage(page, name, icon);
        m_koracutShell->addSection(page, icon, item);
        return item;
    };

    // ---- Général ----
    m_koracutShell->addGroup(QStringLiteral("general"), i18nc("@title preferences sidebar group", "General"));
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("general"), i18nc("@title preferences section", "General"),
                                          i18nc("@info preferences section help", "Startup, automatic saving and how the window behaves."));
        auto *start = page->addCard(i18nc("@title:group preferences", "At startup"));
        start->addToggle(m_configMisc.kcfg_openlastproject, QString(), QString(), i18nc("@option:check preferences", "Reopen the last project"));
        start->addToggle(m_configEnv.kcfg_showWelcome);
        start->addToggle(m_configEnv.kcfg_checkForUpdate);
        auto *save = page->addCard(i18nc("@title:group preferences", "Automatic saving"));
        save->addRow(i18nc("@label preferences", "Save a backup copy"),
                     KoraCutPrefsCard::line({m_configMisc.kcfg_crashrecovery, m_configMisc.kcfg_autosave_time, m_configMisc.label_13, m_configMisc.kcfg_autosave_ops}),
                     i18nc("@info preferences", "Every so many seconds, or after so many changes. This copy is what comes back after a crash."), QStringLiteral("autosave"));
        auto *system = page->addCard(i18nc("@title:group preferences", "System"));
        system->addToggle(m_configEnv.kcfg_usePowerManagement);
        system->addToggle(m_configEnv.dragFromTitlebarOnly, help(m_configEnv.kcontextualhelpbutton), QStringLiteral("dragFromTitlebarOnly"));
        m_pageMisc = section(page, i18nc("@title preferences section", "General"), QStringLiteral("configure"));
    }
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("projets"), i18nc("@title preferences section", "New Projects"),
                                          i18nc("@info preferences section help",
                                                "The starting values of every new project. For the project that is open, use File > Project Settings."));
        m_configProject.kmessagewidget->hide();
        auto *folder = page->addCard(i18nc("@title:group preferences", "Project folder"));
        auto *choices = new QWidget;
        auto *cl = new QVBoxLayout(choices);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(KoraCutMetrics::SpaceGroup);
        cl->addWidget(m_configProject.default_folder);
        cl->addWidget(m_configProject.kcfg_sameprojectfolder);
        cl->addWidget(m_configProject.kcfg_customprojectfolder);
        m_configProject.projecturl->setContentsMargins(24, 0, 0, 0);
        cl->addWidget(m_configProject.projecturl);
        folder->addStacked(i18nc("@label preferences", "Where the project files go"), choices, QString(), QStringLiteral("dossier"));
        auto *format = page->addCard(i18nc("@title:group preferences", "Default format"));
        m_configProject.profile_box->setTitle(QString());
        m_configProject.profile_box->setFlat(true);
        m_configProject.profile_box->setStyleSheet(QStringLiteral("QGroupBox { margin-top: 0; padding-top: 0; }"));
        format->addBlock(m_configProject.profile_box, QStringLiteral("format"));
        auto *tracks = page->addCard(i18nc("@title:group preferences", "Tracks and sound"));
        tracks->addRow(m_configProject.label_video_tracks->text(), m_configProject.kcfg_videotracks);
        tracks->addRow(m_configProject.label_audio_tracks->text(), m_configProject.kcfg_audiotracks);
        tracks->addRow(m_configProject.label_audio_channels->text(), m_configProject.kcfg_audio_channels);
        auto *preview = page->addCard(i18nc("@title:group preferences", "Timeline preview"));
        preview->addRow(m_configProject.label_preview_profile->text(), m_tlPreviewProfiles, QString(), QStringLiteral("apercu"));
        m_pageProject = section(page, i18nc("@title preferences section", "New Projects"), QStringLiteral("project-defaults"));
    }
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("import"), i18nc("@title preferences section", "Import"),
                                          i18nc("@info preferences section help", "What KoraCut does when you add videos, images and sounds to the project."));
        auto *clips = page->addCard(i18nc("@title:group preferences", "Videos and images"));
        clips->addToggle(m_configMisc.kcfg_checkfirstprojectclip, QString(), QString(), i18nc("@option:check preferences", "Check that the first clip matches the project format"));
        clips->addToggle(m_configMisc.kcfg_keep_original_frame_size);
        clips->addToggle(m_configMisc.kcfg_automultistreams);
        clips->addToggle(m_configMisc.kcfg_autoimagesequence);
        clips->addToggle(m_configMisc.kcfg_ignoresubdirstructure, help(m_configMisc.kcontextualhelpbutton));
        auto *meta = page->addCard(i18nc("@title:group preferences", "Metadata"));
        meta->addToggle(m_configMisc.kcfg_use_exiftool);
        meta->addToggle(m_configMisc.kcfg_use_magicLantern);
        auto *streams = page->addCard(i18nc("@title:group preferences", "Videos with several sound tracks"));
        streams->addRow(i18nc("@label preferences", "Tracks to enable on import"), m_configTimeline.kcfg_multistream);
        streams->addToggle(m_configTimeline.kcfg_multistream_checktrack);
        auto *types = page->addCard(i18nc("@title:group preferences", "File types"));
        types->addStacked(m_configEnv.label_5->text(), m_configEnv.kcfg_addedExtensions);
        m_configEnv.supportedmimes->setMaximumHeight(QFontMetrics(font()).lineSpacing() * 6);
        types->addStacked(m_configEnv.label_12->text(), m_configEnv.supportedmimes, QString(), QStringLiteral("types"));
        m_pageImport = section(page, i18nc("@title preferences section", "Import"), QStringLiteral("document-import"));
    }

    // ---- Apparence ----
    m_koracutShell->addGroup(QStringLiteral("apparence"), i18nc("@title preferences sidebar group", "Appearance"));
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("apparence"), i18nc("@title preferences section", "Appearance"),
                                          i18nc("@info preferences section help", "Light or dark, the theme, and where panel tabs sit. Everything applies at once."));
        new KoraCutAppearance(page, m_configMisc.kcfg_tabposition);
        page->setRestoreHandler([page]() {
            KoraCutThemes::self()->apply(QStringLiteral("systeme"));
            auto *manager = new KConfigDialogManager(page, KdenliveSettings::self());
            manager->updateWidgetsDefault();
            delete manager;
        });
        m_pageAppearance = section(page, i18nc("@title preferences section", "Appearance"), QStringLiteral("color-management"), false);
    }
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("couleurs"), i18nc("@title preferences section", "Colors and Markers"),
                                          i18nc("@info preferences section help", "The colors of clips in the timeline, of the monitor guides, and the marker categories."));
        auto *clips = page->addCard(i18nc("@title:group preferences", "Timeline clips"));
        clips->addRow(i18nc("@label preferences", "Video clips"), m_configColors.videoColor, QString(), QStringLiteral("clip_video"));
        clips->addRow(i18nc("@label preferences", "Audio clips"), m_configColors.audioColor, QString(), QStringLiteral("clip_audio"));
        clips->addRow(i18nc("@label preferences", "Title clips"), m_configColors.titleColor, QString(), QStringLiteral("clip_titre"));
        clips->addRow(i18nc("@label preferences", "Image clips"), m_configColors.imageColor, QString(), QStringLiteral("clip_image"));
        clips->addRow(i18nc("@label preferences", "Slideshows"), m_configColors.slideshowColor, QString(), QStringLiteral("clip_diaporama"));
        const QString resetText = i18nc("@label preferences", "Original clip colors");
        m_configColors.resetClipColors->setText(i18nc("@action:button put back the default value", "Restore"));
        clips->addRow(resetText, m_configColors.resetClipColors, QString(), QStringLiteral("clips_retablir"));
        auto *monitor = page->addCard(i18nc("@title:group preferences", "Monitor"));
        monitor->addRow(i18nc("@label preferences", "Framing guides"), m_configColors.kcfg_overlayColor);
        monitor->addRow(i18nc("@label preferences", "Safe area"), m_configColors.kcfg_safeColor);
        monitor->addRow(i18nc("@label preferences", "Monitor background"), m_configColors.kcfg_window_background);
        monitor->addRow(i18nc("@label preferences", "Grid"),
                        KoraCutPrefsCard::line({m_configColors.kcfg_monitorGridH, m_configColors.label_2, m_configColors.kcfg_monitorGridV}), QString(),
                        QStringLiteral("grille"));
        auto *waves = page->addCard(i18nc("@title:group preferences", "Waveforms"));
        waves->addRow(i18nc("@label preferences", "Waveform colors"), KoraCutPrefsCard::line({m_configColors.kcfg_thumbColor1, m_configColors.kcfg_thumbColor2}),
                      QString(), QStringLiteral("formes_onde"));
        auto *markers = page->addCard(i18nc("@title:group preferences", "Marker categories"));
        markers->addBlock(m_configColors.guides_box, QStringLiteral("categories"));
        page->setRestoreHandler([page, restoreFields, this]() {
            restoreFields(page);
            m_configColors.resetClipColors->click();
        });
        m_pageColors = section(page, i18nc("@title preferences section", "Colors and Markers"), QStringLiteral("color-picker"), false);
    }

    // ---- Montage ----
    m_koracutShell->addGroup(QStringLiteral("montage"), i18nc("@title preferences sidebar group", "Editing"));
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("timeline"), i18nc("@title preferences section", "Timeline"),
                                          i18nc("@info preferences section help", "How the timeline shows clips, plays and scrolls."));
        auto *thumbs = page->addCard(i18nc("@title:group preferences", "Thumbnails"));
        thumbs->addToggle(m_configTimeline.kcfg_videothumbnails, QString(), QString(), i18nc("@option:check preferences", "Thumbnails on video clips"));
        thumbs->addToggle(m_configTimeline.kcfg_audiothumbnails, QString(), QString(), i18nc("@option:check preferences", "Waveforms on audio clips"));
        thumbs->addToggle(m_configTimeline.kcfg_displayallchannels, QString(), QString(), i18nc("@option:check preferences", "One waveform per audio channel"));
        auto *play = page->addCard(i18nc("@title:group preferences", "Playback"));
        play->addToggle(m_configTimeline.kcfg_pauseonseek);
        play->addToggle(m_configTimeline.kcfg_jumptostart, QString(), QString(), i18nc("@option:check preferences", "Start over from the beginning when playing from the last frame"));
        play->addToggle(m_configTimeline.kcfg_seekonaddeffect, QString(), QString(), i18nc("@option:check preferences", "Go to the clip when adding an effect to it"));
        play->addToggle(m_configTimeline.kcfg_rewindOnStop);
        auto *scroll = page->addCard(i18nc("@title:group preferences", "Scrolling"));
        scroll->addToggle(m_configTimeline.kcfg_autoscroll, QString(), QString(), i18nc("@option:check preferences", "Follow the playhead during playback"));
        scroll->addToggle(m_configTimeline.kcfg_centeredplayhead, QString(), QString(), i18nc("@option:check preferences", "Keep the playhead in the middle"));
        scroll->addToggle(m_configTimeline.kcfg_scrollvertically, QString(), QString(), i18nc("@option:check preferences", "The wheel scrolls up and down, Shift + wheel left and right"));
        auto *selection = page->addCard(i18nc("@title:group preferences", "Selection"));
        selection->addRow(i18nc("@label preferences", "Open the settings panel when selecting"),
                          KoraCutPrefsCard::line({m_configTimeline.kcfg_raisepropsclips, m_configTimeline.kcfg_raisepropscompositions,
                                                  m_configTimeline.kcfg_raisepropsmixes, m_configTimeline.kcfg_raisepropstracks}),
                          QString(), QStringLiteral("selection"));
        auto *display = page->addCard(i18nc("@title:group preferences", "Display"));
        display->addToggle(m_configTimeline.kcfg_showmarkers);
        display->addRow(m_configTimeline.label->text(), m_configTimeline.kcfg_trackheight);
        m_pageTimeline = section(page, i18nc("@title preferences section", "Timeline"), QStringLiteral("timeline-insert"));
    }
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("edition"), i18nc("@title preferences section", "Effects and Tools"),
                                          i18nc("@info preferences section help", "Effects added to clips, default durations and how the tools behave."));
        auto *effects = page->addCard(i18nc("@title:group preferences", "Effects"));
        effects->addToggle(m_configMisc.kcfg_disable_effect_parameters);
        effects->addToggle(m_configMisc.kcfg_enableBuiltInEffects, help(m_configMisc.kcontextualhelpbutton_2));
        effects->addRow(i18nc("@label preferences", "Track compositing"), m_configMisc.preferredcomposite,
                        i18nc("@info preferences", "How a video track is laid over the one below."), QStringLiteral("composition"));
        auto *durations = page->addCard(i18nc("@title:group preferences", "Default durations"));
        const QList<QPair<QLabel *, QLineEdit *>> fields = {
            {m_configMisc.label, m_configMisc.kcfg_color_duration},       {m_configMisc.label_2, m_configMisc.kcfg_image_duration},
            {m_configMisc.label_8, m_configMisc.kcfg_title_duration},     {m_configMisc.label_3, m_configMisc.kcfg_sequence_duration},
            {m_configMisc.label_5, m_configMisc.kcfg_transition_duration}, {m_configMisc.label_6, m_configMisc.kcfg_fade_duration},
            {m_configMisc.label_7, m_configMisc.kcfg_mix_duration},       {m_configMisc.label_9, m_configMisc.kcfg_subtitle_duration}};
        for (const auto &f : fields) {
            f.second->setMinimumWidth(0);
            f.second->setFixedWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("00:00:00:000")) + 28);
            durations->addRow(f.first->text(), f.second);
        }
        auto *tools = page->addCard(i18nc("@title:group preferences", "Tools"));
        tools->addRow(m_configTools.label_2->text(), m_configTools.kcfg_subtitle_razor_mode);
        tools->addRow(m_configTools.label->text(), m_configTools.kcfg_limitAutoKeyframesInterval, help(m_configTools.kcontextualhelpbutton));
        m_pageTools = section(page, i18nc("@title preferences section", "Effects and Tools"), QStringLiteral("tools"));
    }

    // ---- Lecture et matériel ----
    m_koracutShell->addGroup(QStringLiteral("materiel"), i18nc("@title preferences sidebar group", "Playback and Devices"));
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("lecture"), i18nc("@title preferences section", "Playback"),
                                          i18nc("@info preferences section help", "The monitor, the sound output and graphics acceleration."));
        m_configSdl.kmessagewidget->hide();
        auto *monitor = page->addCard(i18nc("@title:group preferences", "Monitor"));
        monitor->addToggle(m_configSdl.kcfg_play_monitor_on_click, QString(), QString(), m_configSdl.label->text());
        monitor->addRow(m_configSdl.label_6->text(), m_configSdl.fullscreen_monitor, QString(), QStringLiteral("ecran_plein_ecran"));
        monitor->addToggle(m_configSdl.kcfg_mirrorMonitorOnFullscreen, QString(), QString(), m_configSdl.label_3->text());
        auto *sound = page->addCard(i18nc("@title:group preferences", "Sound"));
        sound->addRow(m_configSdl.label_audio_backend->text(), m_configSdl.kcfg_audio_backend,
                      i18nc("@info preferences", "Change these only if the sound does not work: a wrong driver can make KoraCut unstable."));
        sound->addRow(m_configSdl.label_audio_driver->text(), m_configSdl.kcfg_audio_driver);
        sound->addRow(m_configSdl.label_audio_device->text(), m_configSdl.kcfg_audio_device);
        auto *gpu = page->addCard(i18nc("@title:group preferences", "Graphics acceleration"));
        gpu->addToggle(m_configSdl.kcfg_gpu_accel, i18nc("@info preferences", "KoraCut restarts to apply it. Needs MLT built with Movit."), QString(),
                       m_configSdl.label_gpu->text());
        auto *external = page->addCard(i18nc("@title:group preferences", "External output"));
        external->addToggle(m_configSdl.kcfg_external_display, QString(), QString(), m_configSdl.label_2->text());
        external->addRow(m_configSdl.label_5->text(), KoraCutPrefsCard::line({m_configSdl.kcfg_blackmagic_output_device, m_configSdl.reload_blackmagic}),
                         QString(), QStringLiteral("sortie_blackmagic"));
        m_pagePlay = section(page, i18nc("@title preferences section", "Playback"), QStringLiteral("media-playback-start"));
    }
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("enregistrement"), i18nc("@title preferences section", "Recording"),
                                          i18nc("@info preferences section help", "Screen recording, Blackmagic capture cards and the microphone."));
        const QList<QPair<QWidget *, QFormLayout *>> tabs = {{m_configCapture.screen_grab_tab, m_configCapture.formLayout_3},
                                                             {m_configCapture.decklink_tab, m_configCapture.formLayout},
                                                             {m_configCapture.audio_tab, m_configCapture.formLayout_2}};
        for (const auto &t : tabs) {
            const int index = m_configCapture.tabWidget->indexOf(t.first);
            auto *card = page->addCard(index >= 0 ? m_configCapture.tabWidget->tabText(index).remove(QLatin1Char('&')) : QString());
            card->addForm(t.second);
        }
        m_configCapture.tabWidget->hide();
        m_pageCapture = section(page, i18nc("@title preferences section", "Recording"), QStringLiteral("audio-input-microphone"));
    }
#ifdef USE_JOGSHUTTLE
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("molette"), i18nc("@title:tab preferences page, USB jog/shuttle controller", "USB Jog Wheel"),
                                          i18nc("@info preferences section help", "A USB jog and shuttle wheel for editing, and what each of its buttons does."));
        page->addCard()->addBlock(m_rootJog, QStringLiteral("molette"));
        m_pageJog = section(page, i18nc("@title:tab preferences page, USB jog/shuttle controller", "USB Jog Wheel"), QStringLiteral("dialog-input-devices"), false);
    }
#endif

    // ---- Export et formats ----
    m_koracutShell->addGroup(QStringLiteral("export"), i18nc("@title preferences sidebar group", "Export and Formats"));
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("proxys"), i18nc("@title preferences section", "Proxy Clips"),
                                          i18nc("@info preferences section help",
                                                "Light copies of your videos, to edit without slowing down. The export always uses the originals. These values apply to new projects."));
        m_configProxy.kmessagewidget->hide();
        auto *main = page->addCard();
        main->addToggle(m_configProxy.kcfg_enableproxy);
        auto *videos = page->addCard(i18nc("@title:group preferences", "Videos"));
        videos->addToggle(m_configProxy.kcfg_generateproxy, QString(), QString(), i18nc("@option:check preferences", "Make proxies for large videos"));
        videos->addRow(i18nc("@label preferences, minimum size of a video or an image to get a proxy", "From a width of"), m_configProxy.kcfg_proxyminsize);
        videos->addRow(i18nc("@label preferences", "Width of the proxies"), m_configProxy.kcfg_proxyscale);
        videos->addRow(m_configProxy.label_24->text(), m_proxyProfiles, QString(), QStringLiteral("profil"));
        videos->addRow(i18nc("@label preferences", "Profile for videos with transparency"), m_alphaProxyProfiles, QString(), QStringLiteral("profil_alpha"));
        auto *images = page->addCard(i18nc("@title:group preferences", "Images"));
        images->addToggle(m_configProxy.kcfg_generateimageproxy, QString(), QString(), i18nc("@option:check preferences", "Make proxies for large images"));
        images->addRow(i18nc("@label preferences, minimum size of a video or an image to get a proxy", "From a width of"), m_configProxy.kcfg_proxyimageminsize);
        images->addRow(i18nc("@label preferences", "Width of the proxies"), m_configProxy.kcfg_proxyimagesize);
        auto *cameras = page->addCard(i18nc("@title:group preferences", "Camera proxies"));
        cameras->addToggle(m_configProxy.kcfg_externalproxy, i18nc("@info preferences", "Some cameras (GoPro, DJI, Akaso…) record a small copy next to each video."), QString(), i18nc("@option:check preferences", "Use the proxies made by the camera"));
        cameras->addRow(i18nc("@label preferences", "Camera model"), KoraCutPrefsCard::line({m_configProxy.kcfg_external_proxy_profile, m_configProxy.button_external}),
                        QString(), QStringLiteral("camera"));
        auto *advanced = page->addCard(i18nc("@title:group preferences", "Advanced"));
        advanced->addBlock(m_configProxy.kcollapsiblegroupbox, QStringLiteral("parametres_ffmpeg"));
        m_pageProxy = section(page, i18nc("@title preferences section", "Proxy Clips"), QStringLiteral("transform-scale"));
    }
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("conversion"),
                                          i18nc("@title:tab preferences page, formats offered by the Convert menu of the media bin", "Conversion Formats"),
                                          i18nc("@info preferences section help",
                                                "The formats offered by the Convert menu of the media bin. Choose one to see and change its settings."));
        page->addCard()->addBlock(m_rootTranscode, QStringLiteral("formats"));
        m_pageTranscode = section(page, i18nc("@title:tab preferences page, formats offered by the Convert menu of the media bin", "Conversion Formats"),
                                  QStringLiteral("edit-copy"), false);
    }

    // ---- IA et agents ----
    m_koracutShell->addGroup(QStringLiteral("ia"), i18nc("@title preferences sidebar group", "AI and Agents"));
#if KORACUT_AGENTS
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("connexions-ia"), i18nc("@title:tab preferences page", "AI Connections"),
                                          i18nc("@info preferences section help", "Connect your own models, with their key, in order of priority."), false);
        page->addWidget(new KoraCutAiConnectionsPage, 1);
        m_pageKoraCutAi = section(page, i18nc("@title:tab preferences page", "AI Connections"), QStringLiteral("link"), false);
    }
#endif
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("reconnaissance"), i18nc("@title preferences section", "Recognition"),
                                          i18nc("@info preferences section help",
                                                "The models that write subtitles from speech and that find objects in the image. They run on this computer."));
        page->addWidget(m_pluginsPage);
        m_pageSpeech = section(page, i18nc("@title preferences section", "Recognition"), QStringLiteral("text-speak"), false);
    }
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("pilotage"), i18nc("@title preferences section", "External Control"),
                                          i18nc("@info preferences section help",
                                                "Let other programs on this computer drive KoraCut: koracut-ctl, the MCP server, Claude Code. Nothing leaves this computer."));
        auto *access = page->addCard(i18nc("@title:group preferences", "Access"));
        access->addToggle(m_koracutPorteOpen, m_koracutPorteOpen->toolTip(), QStringLiteral("porte"));
        auto *terminal = page->addCard(i18nc("@title:group preferences", "Terminal"));
        terminal->addStacked(i18nc("@label:textbox", "Command of “Open a terminal here”:"), m_koracutTerminalCommand, QString(), QStringLiteral("terminal"));
        m_pagePorte = section(page, i18nc("@title preferences section", "External Control"), QStringLiteral("utilities-terminal"), false);
    }

    // ---- Avancé ----
    m_koracutShell->addGroup(QStringLiteral("avance"), i18nc("@title preferences sidebar group", "Advanced"));
    KoraCutPrefsPage *foldersPage = nullptr;
    KoraCutPrefsPage *performance = new KoraCutPrefsPage(QStringLiteral("performances"), i18nc("@title preferences section", "Performance"),
                                                         i18nc("@info preferences section help", "How KoraCut adapts to this computer, background work, the cache, and diagnostics."));
    {
        // Chantier 10 : le profil de la machine et ce qu'il règle (document 6, section 5).
        auto *machine = performance->addCard(i18nc("@title:group preferences", "This computer"));
        const MachineProfile::Level detected = MachineProfile::detectedLevel();
        auto *profile = new QComboBox;
        profile->setObjectName(QStringLiteral("profilMachine"));
        profile->addItem(i18nc("@item:inlistbox machine profile chosen automatically, %1 is the detected profile", "Automatic (%1)", MachineProfile::levelName(detected)),
                         QString());
        for (auto level : {MachineProfile::Level::Modest, MachineProfile::Level::Medium, MachineProfile::Level::Comfortable}) {
            profile->addItem(MachineProfile::levelName(level), MachineProfile::levelId(level));
        }
        const QString chosen = KoraCutPerformance::group().readEntry("profilMachine", QString());
        profile->setCurrentIndex(std::max(0, profile->findData(chosen)));
        machine->addRow(i18nc("@label preferences", "Computer profile"), profile,
                        i18nc("@info preferences", "%1. Changing it sets the preview size, proxies, cache and effects for this kind of computer.",
                              MachineProfile::describe(MachineProfile::detect())),
                        QStringLiteral("profil"));
        auto *adaptive = new QCheckBox(i18nc("@option:check preferences", "Lower the preview while playing if images are skipped"));
        adaptive->setObjectName(QStringLiteral("apercuAdaptatif"));
        adaptive->setChecked(KoraCutPerformance::adaptiveFloor() > 0);
        machine->addToggle(adaptive, i18nc("@info preferences", "The sharp image comes back as soon as you pause. Export is never affected."),
                           QStringLiteral("apercu_adaptatif"));
        auto *heavy = new QCheckBox(i18nc("@option:check preferences", "Simplified preview of heavy effects while playing"));
        heavy->setObjectName(QStringLiteral("effetsLourdsSimplifies"));
        heavy->setChecked(KoraCutPerformance::simplifiedHeavyEffects());
        machine->addToggle(heavy, i18nc("@info preferences", "Blur, noise reduction, stabilization… are paused during playback. The render and the export always use them."),
                           QStringLiteral("effets_lourds"));
        auto *copies = new QCheckBox(i18nc("@option:check preferences", "Light copies of heavy videos, for smooth scrubbing"));
        copies->setObjectName(QStringLiteral("copiesLegeres"));
        copies->setChecked(KoraCutLightCopies::enabled());
        machine->addToggle(copies,
                           i18nc("@info preferences, %1 a size in GB",
                                 "Phone and camera videos (HEVC, 10 bit, 4K) used in a timeline get a small copy, made in the background and paused while you "
                                 "work. At most %1 GB on the disk. The export always uses the original files.",
                                 QString::number(KoraCutLightCopies::budget().capBytes / 1073741824., 'f', 0)),
                           QStringLiteral("copies_legeres"));
        connect(copies, &QCheckBox::toggled, this, [](bool on) { KoraCutLightCopies::setEnabled(on); });
        auto *decoder = new QLabel;
        decoder->setWordWrap(true);
        const auto showDecoder = [decoder]() {
            QString tip;
            decoder->setText(KoraCutPerformance::decodingLabel(&tip));
            decoder->setToolTip(tip);
        };
        showDecoder();
        auto *tryDecoder = new QPushButton(i18nc("@action:button", "Test Again"));
        tryDecoder->setObjectName(QStringLiteral("essaiDecodage"));
        machine->addRow(i18nc("@label preferences", "Video decoding"), KoraCutPrefsCard::line({decoder, tryDecoder}),
                        i18nc("@info preferences", "Tested on this computer: the graphics chip is used only if it decodes correctly. It helps most with HEVC videos from phones."),
                        QStringLiteral("decodage"));
        connect(tryDecoder, &QPushButton::clicked, this, [tryDecoder, decoder, showDecoder]() {
            tryDecoder->setEnabled(false);
            decoder->setText(i18nc("@info", "Testing…"));
            KoraCutPerformance::testHwDecoding([tryDecoder, showDecoder](const QString &, const QString &) {
                showDecoder();
                tryDecoder->setEnabled(true);
            });
        });
        connect(profile, QOverload<int>::of(&QComboBox::activated), this, [this, profile, adaptive, heavy]() {
            KoraCutPerformance::chooseProfile(profile->currentData().toString());
            // Les champs des autres pages (proxys, cache, vignettes) relisent ce qui vient d'être posé.
            {
                KConfigDialogManager manager(this, KdenliveSettings::self());
                manager.updateWidgets();
            }
            QSignalBlocker b1(adaptive);
            QSignalBlocker b2(heavy);
            adaptive->setChecked(KoraCutPerformance::adaptiveFloor() > 0);
            heavy->setChecked(KoraCutPerformance::simplifiedHeavyEffects());
            koraCutRefreshMonitors();
        });
        connect(adaptive, &QCheckBox::toggled, this, [this](bool on) {
            const auto r = KoraCutPerformance::recommendation(KoraCutPerformance::level());
            // Sur un profil confortable, le plancher proposé est 540p.
            const int floor = r.adaptivePreviewFloorHeight > 0 ? MachineProfile::scalingForHeight(r.adaptivePreviewFloorHeight) : 4;
            KConfigGroup g = KoraCutPerformance::group();
            g.writeEntry("apercuAdaptatif", on ? floor : 0);
            g.sync();
            koraCutRefreshMonitors();
        });
        connect(heavy, &QCheckBox::toggled, this, [](bool on) {
            KConfigGroup g = KoraCutPerformance::group();
            g.writeEntry("effetsLourdsSimplifies", on);
            g.sync();
        });
        auto *tasks = performance->addCard(i18nc("@title:group preferences", "Background tasks"));
        tasks->addRow(m_configEnv.label_9->text(), m_configEnv.kcfg_proxythreads);
        tasks->addToggle(m_configEnv.kcfg_nice_tasks);
        auto *cache = performance->addCard(i18nc("@title:group preferences", "Cache"));
        cache->addRow(m_configEnv.label_17->text(), m_configEnv.kcfg_maxcachesize, m_configEnv.label_18->text());
        m_configEnv.label_18->hide();
        auto *diagnostic = performance->addCard(i18nc("@title:group preferences", "Diagnostics"));
        diagnostic->addToggle(m_configTimeline.kcfg_uiDebugMode);
    }
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("dossiers"), i18nc("@title preferences section", "Folders"),
                                          i18nc("@info preferences section help", "Where KoraCut writes its temporary files, recordings, the library and exports."));
        auto *tmp = page->addCard(i18nc("@title:group preferences", "Temporary files"));
        tmp->addStacked(i18nc("@label preferences, a folder", "Folder"), m_configEnv.tmppathurl, QString(), QStringLiteral("temporaires"));
        auto *capture = page->addCard(i18nc("@title:group preferences", "Recordings"));
        capture->addStacked(i18nc("@label preferences", "Where recordings go"), m_configEnv.kcfg_capturetoprojectfolder);
        capture->addStacked(i18nc("@label preferences, a folder", "Folder"), m_configEnv.capturefolderurl, QString(), QStringLiteral("dossier_enregistrements"));
        capture->addStacked(i18nc("@label preferences", "Subfolder in the project"), m_configEnv.kcfg_captureprojectsubfolder);
        auto *library = page->addCard(i18nc("@title:group preferences", "Library"));
        library->addToggle(m_configEnv.kcfg_librarytodefaultfolder);
        library->addStacked(i18nc("@label preferences, a folder", "Folder"), m_configEnv.libraryfolderurl, QString(), QStringLiteral("dossier_bibliotheque"));
        auto *exports = page->addCard(i18nc("@title:group preferences", "Exports, titles and scripts"));
        exports->addStacked(i18nc("@label preferences", "Where exports go"), m_configEnv.kcfg_videotodefaultfolder);
        exports->addStacked(i18nc("@label preferences, a folder", "Folder"), m_configEnv.videofolderurl, QString(), QStringLiteral("dossier_exports"));
        foldersPage = page;
    }
    {
        auto *page = new KoraCutPrefsPage(QStringLiteral("programmes"), i18nc("@title preferences section", "Programs"),
                                          i18nc("@info preferences section help",
                                                "The tools KoraCut calls (FFmpeg, MLT) and the programs that open your files for editing."));
        auto *engine = page->addCard(i18nc("@title:group preferences", "Video engine"));
        engine->addStacked(m_configEnv.label_8->text(), m_configEnv.ffmpegurl, QString(), QStringLiteral("ffmpeg"));
        engine->addStacked(m_configEnv.label_13->text(), m_configEnv.ffplayurl, QString(), QStringLiteral("ffplay"));
        engine->addStacked(m_configEnv.label_14->text(), m_configEnv.ffprobeurl, QString(), QStringLiteral("ffprobe"));
        engine->addStacked(m_configEnv.label->text(), m_configEnv.mltpathurl, QString(), QStringLiteral("profils_mlt"));
        engine->addStacked(m_configEnv.label_2->text(), m_configEnv.rendererpathurl, QString(), QStringLiteral("melt"));
        auto *apps = page->addCard(i18nc("@title:group preferences", "Edit with"));
        apps->addStacked(m_configEnv.label_4->text(), KoraCutPrefsCard::line({m_configEnv.kcfg_defaultimageapp, m_configEnv.kp_image}), QString(),
                         QStringLiteral("image"));
        apps->addStacked(m_configEnv.label_11->text(), KoraCutPrefsCard::line({m_configEnv.kcfg_defaultaudioapp, m_configEnv.kp_audio}), QString(),
                         QStringLiteral("audio"));
        apps->addStacked(m_configEnv.label_7->text(), KoraCutPrefsCard::line({m_configEnv.kcfg_glaxnimatePath, m_configEnv.kp_anim}), QString(),
                         QStringLiteral("animation"));
        apps->addStacked(m_configEnv.label_19->text(), KoraCutPrefsCard::line({m_configEnv.kcfg_defaultvideoapp, m_configEnv.kp_video}), QString(),
                         QStringLiteral("video"));
        m_configEnv.tabWidget->hide();

        // Filet de sécurité : un champ de réglage resté dans une ancienne page (un oubli, ou un champ
        // ajouté plus tard à un .ui) est rangé dans « Autres réglages », jamais perdu.
        KoraCutPrefsCard *others = nullptr;
        for (QWidget *root : {m_rootMisc, m_rootProject, m_rootProxy, m_rootTimeline, m_rootTools, m_rootEnv, m_rootColors, m_rootSdl, m_rootCapture}) {
            if (!root) {
                continue;
            }
            const auto leftovers = root->findChildren<QWidget *>(QRegularExpression(QStringLiteral("^kcfg_")));
            for (QWidget *w : leftovers) {
                bool hidden = false;
                for (QWidget *p = w; p && p != root; p = p->parentWidget()) {
                    hidden = hidden || (p->testAttribute(Qt::WA_WState_ExplicitShowHide) && p->testAttribute(Qt::WA_WState_Hidden));
                }
                if (hidden || qobject_cast<QLineEdit *>(w->parentWidget()) || w->parentWidget()->objectName().startsWith(QLatin1String("kcfg_"))) {
                    continue;
                }
                qWarning() << "KoraCut: réglage non rangé dans les Préférences" << w->objectName();
                if (!others) {
                    others = performance->addCard(i18nc("@title:group preferences", "Other settings"));
                }
                others->addRow(w->objectName().mid(5), w);
            }
            root->setParent(this);
            root->hide();
        }
        m_pagePerformance = section(performance, i18nc("@title preferences section", "Performance"), QStringLiteral("speedometer"));
        // Chantier 10 : « Rétablir » repose les défauts du profil de la machine, pas ceux de Kdenlive.
        performance->setRestoreHandler([this, performance, restoreFields]() {
            restoreFields(performance);
            KoraCutPerformance::applyProfile(KoraCutPerformance::level());
            KConfigDialogManager manager(this, KdenliveSettings::self());
            manager.updateWidgets();
            if (auto *box = performance->findChild<QCheckBox *>(QStringLiteral("apercuAdaptatif"))) {
                QSignalBlocker b(box);
                box->setChecked(KoraCutPerformance::adaptiveFloor() > 0);
            }
            if (auto *box = performance->findChild<QCheckBox *>(QStringLiteral("effetsLourdsSimplifies"))) {
                QSignalBlocker b(box);
                box->setChecked(KoraCutPerformance::simplifiedHeavyEffects());
            }
            koraCutRefreshMonitors();
        });
        m_pageFolders = section(foldersPage, i18nc("@title preferences section", "Folders"), QStringLiteral("folder"));
        m_pageEnv = section(page, i18nc("@title preferences section", "Programs"), QStringLiteral("application-x-executable-script"));
    }

    m_koracutShell->finish();
    m_koracutShell->setAutoApply(
        [this]() {
            QPushButton *apply = buttonBox()->button(QDialogButtonBox::Apply);
            return apply && apply->isEnabled();
        },
        [this]() {
            if (QPushButton *apply = buttonBox()->button(QDialogButtonBox::Apply)) {
                apply->click();
            }
        });
    connect(this, &KConfigDialog::widgetModified, m_koracutShell, &KoraCutPrefsShell::scheduleApply);
    connect(m_koracutShell, &KoraCutPrefsShell::sectionChanged, this, [](const QString &id) {
        KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("KoraCut"));
        group.writeEntry("prefsSection", id);
    });
    // On rouvre sur la dernière section vue (showPage() la remplace si on demande une page précise).
    const QString last = KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("KoraCut")).readEntry("prefsSection", QStringLiteral("general"));
    if (!m_koracutShell->showSection(last)) {
        m_koracutShell->showSection(QStringLiteral("general"));
    }
}

QStringList KdenliveSettingsDialog::koraCutSections() const
{
    return m_koracutShell ? m_koracutShell->sectionIds() : QStringList();
}

bool KdenliveSettingsDialog::koraCutShowSection(const QString &id)
{
    return m_koracutShell && m_koracutShell->showSection(id);
}

int KdenliveSettingsDialog::koraCutSearch(const QString &text)
{
    return m_koracutShell ? m_koracutShell->search(text) : 0;
}

QString KdenliveSettingsDialog::koraCutCurrentSection() const
{
    return m_koracutShell ? m_koracutShell->currentSection() : QString();
}

void KdenliveSettingsDialog::reject()
{
    // Échap ou la croix : les changements sont déjà enregistrés, on finit ce qui attend.
    if (m_koracutShell) {
        m_koracutShell->flush();
    }
    KConfigDialog::reject();
}

void KdenliveSettingsDialog::koraCutRefreshMonitors()
{
    if (pCore && pCore->monitorManager()) {
        if (Monitor *m = pCore->monitorManager()->projectMonitor()) {
            m->koraCutConfigureAdaptive();
        }
    }
}
