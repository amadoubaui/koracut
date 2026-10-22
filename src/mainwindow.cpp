/*
    SPDX-FileCopyrightText: 2007 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "mainwindow.h"
#include "koracut/koracutfonts.h"
#include "assets/assetpanel.hpp"
#include "audiomixer/mixermanager.hpp"
#include "bin/clipcreator.hpp"
#include "bin/generators/generators.h"
#include "bin/mediabrowser.h"
#include "bin/model/subtitlemodel.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "dialogs/clipcreationdialog.h"
#include "dialogs/clipjobmanager.h"
#include "dialogs/renderwidget.h"
#include "dialogs/settings/kdenlivesettingsdialog.h"
#include "dialogs/subtitleedit.h"
#include "dialogs/wizard.h"
#include "koracut/koracutfirstrun.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "effects/effectbasket.h"
#include "effects/effectlist/view/effectlistwidget.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "jobs/customjobtask.h"
#include "jobs/scenesplittask.h"
#include "jobs/speedtask.h"
#include "jobs/stabilizetask.h"
#include "jobs/transcodetask.h"
#include "kddocksetup.h"
#include "kdenlivesettings.h"
#include "keysequencehandler.h"
#include "koracut/koracutanimatemenu.h"
#include "koracut/koracuttimelinecolors.h"
#include "koracut/koracutthemes.h"
#include "koracut/koracutmodules.h"
#include "layouts/layoutswitcher.h"
#include "koracut/koracutplaceholder.h"
#include "koracut/koracuttopbar.h"
#include "koracut/koracutvisible.h"
#include "layouts/layoutmanagement.h"
#include "library/librarywidget.h"
#include "render/renderserver.h"
#include "koracut/koracutmetrics.h"

#ifndef NODBUS
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#endif

// #include "kdenlive_debug.h"
// #include "kdenlivecore_export.h"

#include "dialogs/markerdialog.h"
#include "dialogs/textbasededit.h"
#include "dialogs/timeremap.h"
#include "filefilter.h"
#include "lib/localeHandling.h"
#include "mltcontroller/clipcontroller.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"

#include <cmath>
#include "monitor/scopes/audiographspectrum.h"
#include "onlineresources/resourcewidget.hpp"
#include "profiles/profilemodel.hpp"
#include "profiles/profilerepository.hpp"
#include "project/cliptranscode.h"
#include "project/dialogs/archivewidget.h"
#include "project/dialogs/guideslist.h"
#include "project/dialogs/projectsettings.h"
#include "project/dialogs/temporarydata.h"
#include "project/projectmanager.h"
#include "koracut/koracutpanelfocus.h"
#include "koracut/koracutautocutdialog.h"
#include "koracut/koracutrender.h"
#include "koracut/koracutperformance.h"
#include "koracut/koracutlightcopies.h"
#include "koracut/koracutperfpanel.h"
#include "bin/model/markerlistmodel.hpp"
#include "koracut/koracuttranscriber.h"
#include "koracut/koracutduckingdialog.h"
#include "koracut/koracutextensionsdialog.h"
#include "koracut/koracutsupportdialog.h"
#include "koracut/koracutstyle.h"
#include "koracut/koracutworkspace.h"
#include "koracut/porte/koracutconsolepanel.h"
#include "koracut/koracutupdatecheck.h"
#include "koracut/terminal/koracutagentscli.h"
#include "koracut/terminal/koracutterminalpanel.h"
#include "koracut/script/koracutscriptpanel.h"
#include "koracut/color/koracutclipstrip.h"
#include "koracut/color/koracutcolorpage.h"
#include "koracut/export/koracutdeliverpage.h"
#include "koracut/export/koracutexport.h"
#include "koracut/export/koracutexportqueue.h"
#include "koracut/color/koracutnodeeditor.h"
#include "koracut/color/koracutpalettes.h"
#include "koracut/porte/koracutporte.h"
#include "koracut/porte/koracutportemethodes.h"
#include "koracut/motion/koracutmotioninspector.h"
#include "koracut/koracutcliptoolspanel.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutcliptools.h"
#include "koracut/koracutstabilizer.h"
#include "koracut/motion/koracutmotionmirror.h"
#include "koracut/motion/koracutmotionpanel.h"
#include "koracut/motion/koracutmotionrenderer.h"
#include "jobs/taskmanager.h"
#include "mltconnection.h"
#include "renderpresets/renderpresetrepository.hpp"
#include "scopes/scopemanager.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinetabs.hpp"
#include "timeline2/view/timelinewidget.h"
#include "titler/titlewidget.h"
#include "transitions/transitionlist/view/transitionlistwidget.hpp"
#include "transitions/transitionsrepository.hpp"
#include "utils/uiutils.h"
#include "widgets/progressbutton.h"
#include <config-kdenlive.h>
#if KORACUT_AGENTS
#include "koracut/koracutagentclient.h"
#include "koracut/koracutagentpanel.h"
#include "koracut/ai/koracutaibridge.h"
#include "koracut/ai/koracutaiconnectionspage.h"
#include "koracut/koracutagentsession.h"
#include "utils/machineprofile.h"
#include <KUser>
#endif
// Hors du bloc des agents : la voix off au micro existe aussi dans la version libre (KORACUT_AGENTS=OFF),
// qui ne compilait pas sans cet en-tête (vu le 12 octobre 2026).
#include "koracut/voix/koracutvoicerecord.h"

#ifdef USE_JOGSHUTTLE
#include "jogshuttle/jogmanager.h"
#endif

#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/TabBar.h>
#include <kddockwidgets/qtcommon/View.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/MainWindow.h>

#include <KAboutData>
#include <KActionCollection>
#include <KActionMenu>
#include <KColorScheme>
#include <KColorSchemeManager>
#include <KColorSchemeMenu>
#include <KConfigDialog>
#include <KCoreAddons>
#include <KDualAction>
#include <KEditToolBar>
#include <KIconEffect>
#include <KIconTheme>
#include <KLocalizedString>
#include <KMessageBox>
#include <KNSWidgets/Dialog>
#include <KNotifyConfigWidget>
#include <KRecentDirs>
#include <KShortcutsDialog>
#include <KStandardAction>
#include <KStyleManager>
#include <KToggleFullScreenAction>
#include <KToolBar>
#include <KXMLGUIFactory>

#include <KConfigGroup>
#include <QAction>
#include <QClipboard>
#include <QCollator>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QMenuBar>
#include <QProxyStyle>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleFactory>
#include <QUndoGroup>
#include <KCommandBar>
#include <QStackedWidget>
#include <QTimer>
#include <QTabWidget>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QFile>
#include <QVBoxLayout>
#include <array>
#include <QtConcurrent/QtConcurrentRun>

static const char version[] = KDENLIVE_VERSION;
namespace Mlt {
class Producer;
}

QMap<QString, QImage> MainWindow::m_lumacache;
QMap<QString, QStringList> MainWindow::m_lumaFiles;
KIO::filesize_t m_totalCacheSize = 0;
int m_totalCacheJobs = 0;

namespace {
// KoraCut, 23 septembre 2026 : la pastille d'état du moteur, à gauche de la barre d'état
// (« 11 - Refonte UI-UX », 3.8). Vert : prêt. Jaune : occupé (tâches de fond). Rouge : le moteur a
// signalé une erreur ; elle reste rouge jusqu'au clic, pour qu'une erreur passée pendant qu'on
// regardait ailleurs ne se perde pas. Couleurs de la charte produit.
class EngineDot : public QWidget
{
public:
    explicit EngineDot(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(18, 16);
        setCursor(Qt::PointingHandCursor);
        refresh();
    }
    void setBusy(int tasks)
    {
        m_tasks = tasks;
        refresh();
    }
    void setError(const QString &message)
    {
        m_error = message;
        refresh();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(!m_error.isEmpty() ? QStringLiteral("#FF7262") : m_tasks > 0 ? QStringLiteral("#FFD166") : QStringLiteral("#4ADE80")));
        painter.drawEllipse(QRectF((width() - 6) / 2., (height() - 6) / 2., 6, 6));
    }
    void mousePressEvent(QMouseEvent *) override
    {
        if (!m_error.isEmpty()) {
            m_error.clear();
            refresh();
        }
    }

private:
    void refresh()
    {
        if (!m_error.isEmpty()) {
            setToolTip(i18nc("@info:tooltip", "Engine error: %1\nClick to dismiss.", m_error));
        } else if (m_tasks > 0) {
            setToolTip(i18ncp("@info:tooltip", "Engine busy: %1 background task", "Engine busy: %1 background tasks", m_tasks));
        } else {
            setToolTip(i18nc("@info:tooltip", "Engine ready"));
        }
        update();
    }
    int m_tasks = 0;
    QString m_error;
};
}


MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
    , m_activeTool(ToolType::SelectTool)
    , m_mousePosition(0)
    , m_effectBasket(nullptr)
{
    // KoraCut, 23 septembre 2026 : le rôle de menu Mac de chaque action se fixe dès qu'elle entre dans la
    // collection, avant que la barre de menus native n'existe. Changé après coup, il ne ramène pas une
    // entrée que Qt a déjà déplacée dans le menu de l'application (vu avec « Réglages du projet… »).
    connect(actionCollection(), &KActionCollection::inserted, this, [](QAction *action) {
        static const QMap<QString, QAction::MenuRole> roles = {
            {KStandardAction::name(KStandardAction::Preferences), QAction::PreferencesRole},
            {KStandardAction::name(KStandardAction::Quit), QAction::QuitRole},
            {KStandardAction::name(KStandardAction::AboutApp), QAction::AboutRole},
        };
        action->setMenuRole(roles.value(action->objectName(), QAction::NoRole));
    });
    // Toute action de composition (« transition_<id> ») pose sa composition, d'où qu'on la lance :
    // menu Compositions, palette ⌘K ou raccourci. Plusieurs endroits créent ces actions sous le même
    // nom, et la dernière remplace les autres : on les branche donc toutes ici, à leur entrée.
    connect(actionCollection(), &KActionCollection::inserted, this, [this](QAction *action) {
        // Qt::UniqueConnection ne marche pas avec une lambda (Qt refuse alors la connexion, sans bruit) :
        // une propriété marque les actions déjà branchées.
        if (action->objectName().startsWith(QLatin1String("transition_")) && !action->property("koracutBranchee").toBool()) {
            action->setProperty("koracutBranchee", true);
            connect(action, &QAction::triggered, this, [this, action]() { slotAddTransition(action); });
        }
    });
    // Init all action categories that are used by other parts of the software
    // before we call MainWindow::init and therefore can't be initialized there
    KActionCategory *category = new KActionCategory(i18n("Monitor"), actionCollection());
    kdenliveCategoryMap.insert(QStringLiteral("monitor"), category);
    category = new KActionCategory(i18n("Add Clip"), actionCollection());
    kdenliveCategoryMap.insert(QStringLiteral("addclip"), category);
    category = new KActionCategory(i18n("Add Marker by Category Number"), actionCollection());
    kdenliveCategoryMap.insert(QStringLiteral("guidecategorynumber"), category);
    category = new KActionCategory(i18n("Navigation and Playback"), actionCollection());
    kdenliveCategoryMap.insert(QStringLiteral("navandplayback"), category);
    category = new KActionCategory(i18n("Bin Tags"), actionCollection());
    kdenliveCategoryMap.insert(QStringLiteral("bintags"), category);
    auto flags = KDDockWidgets::Config::self().flags();
    flags |= KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible;
    flags |= KDDockWidgets::Config::Flag_AllowReorderTabs;
    flags |= KDDockWidgets::Config::Flag_TitleBarShowAutoHide;
    // KoraCut : le double clic ne détache plus un panneau. Il volait les deux premiers clics du
    // triple clic qui agrandit le panneau (koracutpanelfocus.h). Détacher se fait en glissant.
    flags |= KDDockWidgets::Config::Flag_DisableDoubleClick;

    KDDockWidgets::Config::self().setFlags(flags);
    KDDockWidgets::Core::FloatingWindow::s_windowFlagsOverride = Qt::Tool;

    // Increase the separator size, just for demo
    KDDockWidgets::Config::self().setViewFactory(new CustomWidgetFactory());
    KDDockWidgets::Config::self().setLayoutSpacing(0); // SeparatorThickness(3);
    if (KdenliveSettings::tabposition() == 1) {
        KDDockWidgets::Config::self().setTabsAtBottom(true);
    }
    mainDockWindow = new KDDockWidgets::QtWidgets::MainWindow(QStringLiteral("KdenliveKDDock"));
    mainDockWindow->setCenterWidgetMargins(QMargins(1, 1, 1, 1));
}

void MainWindow::init()
{
    QString desktopStyle = QApplication::style()->objectName();
    // Load themes
    auto themeManager = KColorSchemeManager::instance();
    // KoraCut: thème par défaut
    // Au premier lancement, si l'utilisateur n'a jamais choisi de schéma de couleurs
    // (KColorSchemeManager n'a rien mémorisé dans [UiSettings] ColorScheme), on active
    // « KoraCut Dark » (data/color-schemes/KoraCutDark.colors). Le gestionnaire enregistre
    // ensuite le choix lui-même (autosave actif par défaut). Le drapeau koracut_theme_applied
    // évite de réimposer le thème si l'utilisateur revient plus tard au schéma système.
    // Débrayable avec la variable d'environnement KORACUT_NO_THEME.
    // KoraCut, 22 septembre 2026 : le thème suit le mode clair ou sombre du système par défaut,
    // et change à chaud quand le système change. Les deux choix explicites, clair et sombre,
    // ne sont jamais contredits. Voir « 14 - Themes, sauvegarde, marqueurs, extensions.md », § 1.
    UiUtils::applyThemeMode();
    UiUtils::followSystemColorScheme();
    KdenliveSettings::setKoracut_theme_applied(true);
    KActionMenu *colorSelectionMenu = KColorSchemeMenu::createMenu(themeManager, this);
    actionCollection()->addAction(QStringLiteral("themes_menu"), colorSelectionMenu);

    // KoraCut : le mode de thème, en trois états. Il précède la liste complète des schémas,
    // qui reste disponible pour qui veut un schéma particulier.
    auto *themeModeAction = new KSelectAction(i18nc("@title menu du mode de thème", "Theme"), this);
    themeModeAction->setToolBarMode(KSelectAction::MenuMode);
    const std::array<std::pair<const char *, QString>, 3> modes = {
        {{"system", i18nc("@item mode de thème", "Follow the system")},
         {"light", i18nc("@item mode de thème", "Light")},
         {"dark", i18nc("@item mode de thème", "Dark")}}};
    const QString modeCourant = KdenliveSettings::koracut_theme_mode();
    for (const auto &m : modes) {
        QAction *a = themeModeAction->addAction(m.second);
        a->setData(QString::fromLatin1(m.first));
        a->setCheckable(true);
        if (modeCourant == QLatin1String(m.first)) {
            themeModeAction->setCurrentAction(a);
        }
    }
    // KoraCut, 22 septembre 2026 : les thèmes en plus (Très sombre, Gris, Contraste élevé, et
    // ceux que l'utilisateur a installés). Choisir l'un d'eux arrête le suivi du système.
    const auto autresThemes = UiUtils::koraCutThemes();
    for (const auto &theme : autresThemes) {
        QAction *a = themeModeAction->addAction(theme.second);
        a->setData(theme.first);
        a->setCheckable(true);
        if (modeCourant == theme.first) {
            themeModeAction->setCurrentAction(a);
        }
    }
    connect(themeModeAction, &KSelectAction::actionTriggered, this, [](QAction *a) {
        if (!a) {
            return;
        }
        KdenliveSettings::setKoracut_theme_mode(a->data().toString());
        UiUtils::applyThemeMode();
    });
    actionCollection()->addAction(QStringLiteral("theme_mode"), themeModeAction);

    // KoraCut, 23 septembre 2026 : quand on suit le système, quel thème pour chaque cas. Choisir
    // ici repasse en « Suivre le système », puisque c'est de ce mode-là qu'il s'agit.
    auto systemThemeAction = [this, themeModeAction](const QString &name, const QString &text, const QString &base, const QString &current,
                                                     void (*save)(const QString &)) {
        auto *action = new KSelectAction(text, this);
        action->setToolBarMode(KSelectAction::MenuMode);
        const auto themes = UiUtils::koraCutThemesFor(base);
        for (const auto &theme : themes) {
            QAction *a = action->addAction(theme.second);
            a->setData(theme.first);
            a->setCheckable(true);
            if (theme.first == current) {
                action->setCurrentAction(a);
            }
        }
        connect(action, &KSelectAction::actionTriggered, this, [themeModeAction, save](QAction *a) {
            if (!a) {
                return;
            }
            save(a->data().toString());
            KdenliveSettings::setKoracut_theme_mode(QStringLiteral("system"));
            for (QAction *m : themeModeAction->actions()) {
                if (m->data().toString() == QLatin1String("system")) {
                    themeModeAction->setCurrentAction(m);
                }
            }
            UiUtils::applyThemeMode();
        });
        actionCollection()->addAction(name, action);
        return action;
    };
    auto *systemDark = systemThemeAction(QStringLiteral("koracut_theme_system_dark"), i18nc("@title:menu", "When the System Is Dark"),
                                         QStringLiteral("dark"), KdenliveSettings::koracut_theme_system_dark(), &KdenliveSettings::setKoracut_theme_system_dark);
    auto *systemLight = systemThemeAction(QStringLiteral("koracut_theme_system_light"), i18nc("@title:menu", "When the System Is Light"),
                                          QStringLiteral("light"), KdenliveSettings::koracut_theme_system_light(), &KdenliveSettings::setKoracut_theme_system_light);

    // Installer un thème : un fichier .colors, avec sa section [KoraCut] pour recolorer aussi la
    // feuille de style (modèle : Sources/koracut-themes/generer.py). Il apparaît au prochain lancement.
    auto *installTheme = new QAction(QIcon::fromTheme(QStringLiteral("document-import")), i18nc("@action", "Install a Theme…"), this);
    connect(installTheme, &QAction::triggered, this, [this, themeModeAction, systemDark, systemLight]() {
        const QString file = QFileDialog::getOpenFileName(this, i18nc("@title:window", "Install a Theme"), QDir::homePath(),
                                                          i18nc("@item:inlistbox filtre de fichiers", "Themes (*.colors)"));
        if (file.isEmpty()) {
            return;
        }
        QString error;
        const QString id = UiUtils::installTheme(file, error);
        if (id.isEmpty()) {
            KMessageBox::error(this, error);
            return;
        }
        // Appliqué tout de suite, depuis son fichier, et ajouté aux menus (23 septembre 2026 ;
        // avant, il fallait relancer KoraCut).
        const QString name = UiUtils::themeName(file, id);
        auto addTo = [id, name](KSelectAction *menu) {
            for (QAction *a : menu->actions()) {
                if (a->data().toString() == id) {
                    return a;
                }
            }
            QAction *a = menu->addAction(name);
            a->setData(id);
            a->setCheckable(true);
            return a;
        };
        themeModeAction->setCurrentAction(addTo(themeModeAction));
        addTo(UiUtils::themeBase(file) == QLatin1String("light") ? systemLight : systemDark);
        KdenliveSettings::setKoracut_theme_mode(id);
        UiUtils::applyThemeMode();
        pCore->displayMessage(i18nc("@info:status", "Theme installed and applied. It is in View > Theme."), InformationMessage);
    });
    actionCollection()->addAction(QStringLiteral("koracut_install_theme"), installTheme);
    auto *openThemes = new QAction(QIcon::fromTheme(QStringLiteral("folder")), i18nc("@action", "Open the Themes Folder"), this);
    connect(openThemes, &QAction::triggered, this, []() {
        QDir().mkpath(UiUtils::userThemesFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(UiUtils::userThemesFolder()));
    });
    actionCollection()->addAction(QStringLiteral("koracut_open_themes_folder"), openThemes);

    // KoraCut, 25 septembre 2026 : le moteur de thèmes (src/koracut/koracutthemes.*). Un thème créé,
    // modifié ou supprimé par un agent entre dans les menus (ou en sort) sans relancer, et le menu
    // coche le thème à l'écran, d'où qu'il vienne (menu, agent, porte de commande).
    auto syncThemeMenus = [themeModeAction, systemDark, systemLight]() {
        const auto themes = KoraCutThemes::self()->list();
        QSet<QString> ids;
        for (const auto &t : themes) {
            ids.insert(t.id);
        }
        const auto sync = [&themes, &ids](KSelectAction *menu, const QString &base, bool all) {
            for (QAction *a : menu->actions()) {
                const QString id = a->data().toString();
                if (id.startsWith(QLatin1String("KoraCut")) && !ids.contains(id)) {
                    menu->removeAction(a);
                    a->deleteLater();
                }
            }
            for (const auto &t : themes) {
                if ((!all && t.base != base) || (all && (t.id == QLatin1String("KoraCutDark") || t.id == QLatin1String("KoraCutLight")))) {
                    continue;
                }
                bool present = false;
                for (QAction *a : menu->actions()) {
                    if (a->data().toString() == t.id) {
                        a->setText(t.name);
                        present = true;
                    }
                }
                if (!present) {
                    QAction *a = menu->addAction(t.name);
                    a->setData(t.id);
                    a->setCheckable(true);
                }
            }
        };
        sync(themeModeAction, QString(), true);
        sync(systemDark, QStringLiteral("dark"), false);
        sync(systemLight, QStringLiteral("light"), false);
        const QString mode = KdenliveSettings::koracut_theme_mode();
        for (QAction *a : themeModeAction->actions()) {
            if (a->data().toString() == mode) {
                themeModeAction->setCurrentAction(a);
            }
        }
    };
    connect(KoraCutThemes::self(), &KoraCutThemes::listChanged, this, syncThemeMenus);
    connect(KoraCutThemes::self(), &KoraCutThemes::themeChanged, this, [this, syncThemeMenus]() {
        syncThemeMenus();
        // Les couleurs des clips (jetons piste_*) et le fond de la timeline suivent le thème.
        if (getCurrentTimeline() && getCurrentTimeline()->controller()) {
            getCurrentTimeline()->setClearColor(qApp->palette().window().color());
            getCurrentTimeline()->controller()->refreshTrackColors();
        }
    });

    // Handle communication with the renderer app
    new RenderServer(this);
    QString defaultProfile = KdenliveSettings::default_profile();

    pCore->setCurrentProfile(defaultProfile.isEmpty() ? ProjectManager::getDefaultProjectFormat() : defaultProfile);
    m_commandStack = new QUndoGroup();

    // If using a custom profile, make sure the file exists or fallback to default
    QString currentProfilePath = pCore->getCurrentProfile()->path();
    if (currentProfilePath.startsWith(QLatin1Char('/')) && !QFile::exists(currentProfilePath)) {
        KMessageBox::error(this, i18n("Cannot find your default profile, switching to ATSC 1080p 25"));
        pCore->setCurrentProfile(QStringLiteral("atsc_1080p_25"));
        KdenliveSettings::setDefault_profile(QStringLiteral("atsc_1080p_25"));
    }

    // Disable movit until it's stable
    m_gpuAllowed = false;
    KdenliveSettings::setGpu_accel(false);

    // m_gpuAllowed = EffectsRepository::get()->hasInternalEffect(QStringLiteral("glsl.manager"));

    // KoraCut : la barre du haut de 40 px est posée au-dessus de la zone des docks, dans le widget central.
    // Sous elle, une pile : la zone des docks, ou la page d'un module encore vide.
    auto *centralContainer = new QWidget(this);
    // La gouttière sous la barre des modules montre le fond de fenêtre, pas la surface : c'est ce
    // que la charte appelle une gouttière, et c'est ce qui fait lire les deux rangées d'onglets
    // comme deux zones et non comme une seule barre.
    centralContainer->setObjectName(QStringLiteral("koracutCentral"));
    centralContainer->setAutoFillBackground(true);
    centralContainer->setAttribute(Qt::WA_StyledBackground, true);
    auto *centralLayout = new QVBoxLayout(centralContainer);
    // Une gouttière de 8 px sous la barre des modules. Depuis que les onglets de panneaux sont
    // remontés en haut de leur groupe, deux rangées d'onglets se suivent : sans cet écart, elles
    // se touchent et se lisent comme une seule barre confuse. 8 px est le cran « entre deux
    // groupes » de la grille de la charte, et il laisse voir le fond de fenêtre entre les deux.
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(KoraCutMetrics::SpaceGroup);
    m_topBar = new KoraCutTopBar(centralContainer);
    centralLayout->addWidget(m_topBar);
    m_centralStack = new QStackedWidget(centralContainer);
    m_modulePlaceholder = new KoraCutPlaceholder(m_centralStack);
    m_centralStack->addWidget(mainDockWindow);
    m_centralStack->addWidget(m_modulePlaceholder);
    centralLayout->addWidget(m_centralStack, 1);
    setCentralWidget(centralContainer);
    m_centralStack->setCurrentWidget(mainDockWindow);
    // KoraCut, 23 septembre 2026 : le champ « Rechercher ⌘K » de la barre du haut ne faisait rien.
    // Dès la première lettre tapée, il ouvre la palette de commandes avec cette lettre, et la frappe
    // continue dans la palette. Le champ se vide : il ne sert que de porte d'entrée.
    connect(m_topBar->searchField(), &QLineEdit::textEdited, this, [this](const QString &text) {
        if (text.isEmpty()) {
            return;
        }
        m_topBar->searchField()->clear();
        m_topBar->searchField()->clearFocus();
        slotCommandPalette();
        const QList<KCommandBar *> bars = findChildren<KCommandBar *>(QString(), Qt::FindDirectChildrenOnly);
        if (!bars.isEmpty()) {
            if (auto *field = bars.last()->findChild<QLineEdit *>()) {
                field->setText(text);
                field->setFocus();
            }
        }
    });
    connect(m_topBar, &KoraCutTopBar::homeRequested, this, [this]() {
        // L'écran d'accueil est le chantier 2 : en attendant, on le dit franchement.
        Q_EMIT displayMessage(i18n("The home screen is not written yet."), InformationMessage, 4000);
    });
    mainDockWindow->show();

    m_shortcutRemoveFocus = new QShortcut(QKeySequence(QStringLiteral("Esc")), this);
    connect(m_shortcutRemoveFocus, &QShortcut::activated, this, &MainWindow::slotRemoveFocus);

    /// Add Widgets
    m_timelineToolBar = toolBar(QStringLiteral("timelineToolBar"));
    m_timelineToolBarContainer = new TimelineContainer(this);
    auto *ctnLay = new QVBoxLayout;
    ctnLay->setSpacing(0);
    ctnLay->setContentsMargins(0, 0, 0, 0);
    m_timelineToolBarContainer->setLayout(ctnLay);
    ctnLay->addWidget(m_timelineToolBar);
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup mainConfig(config, QStringLiteral("MainWindow"));
    KConfigGroup tbGroup(&mainConfig, QStringLiteral("Toolbar timelineToolBar"));
    m_timelineToolBar->applySettings(tbGroup);
    // KoraCut : sans réglage enregistré, KToolBar prend ToolbarDefault du thème d'icônes (22 px),
    // ce qui est le gros défaut vu à l'écran. On pose la valeur de la charte, et on respecte le
    // choix de l'utilisateur s'il est passé par le menu « Taille des icônes ».
    if (!tbGroup.hasKey("IconSize")) {
        m_timelineToolBar->setIconDimensions(KoraCutMetrics::IconToolbar);
    }
    // KoraCut, 25 septembre 2026 : quand la timeline est étroite (disposition verticale), les
    // boutons qui ne tiennent pas (Coupe auto, Rendu…) passent derrière le bouton « plus d'outils »
    // de Qt ; son icône venait vide et le bouton ne se voyait pas. On lui donne la nôtre.
    // Le bouton de Qt ne dessinait rien, même avec une icône valide (vérifié le 25 septembre
    // 2026) : une étiquette posée dessus porte la flèche et laisse passer les clics.
    if (auto *ext = m_timelineToolBar->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"))) {
        ext->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
        ext->setToolTip(i18nc("@info:tooltip toolbar overflow button", "More tools"));
        auto *fleche = new QLabel(ext);
        fleche->setObjectName(QStringLiteral("koracutToolbarMore"));
        fleche->setAttribute(Qt::WA_TransparentForMouseEvents);
        fleche->setAlignment(Qt::AlignCenter);
        fleche->setPixmap(QIcon::fromTheme(QStringLiteral("go-next")).pixmap(QSize(16, 16), ext->devicePixelRatioF()));
        auto *poseur = new QVBoxLayout(ext);
        poseur->setContentsMargins(0, 0, 0, 0);
        poseur->addWidget(fleche);
    }
    QFrame *fr = new QFrame(this);
    fr->setFrameShape(QFrame::HLine);
    fr->setMaximumHeight(1);
    fr->setLineWidth(1);
    ctnLay->addWidget(fr);
    // KoraCut, 27 septembre 2026 : la bande des onglets de séquences, toujours visible et posée sur
    // le fond, sépare déjà la barre d'outils de la timeline. Le trait ferait une deuxième limite.
    fr->hide();
    setupActions();
    auto *layoutManager = new LayoutManagement(this);
    connect(pCore.get(), &Core::loadLayoutById, layoutManager, &LayoutManagement::slotLoadLayoutById, Qt::DirectConnection);
    connect(pCore.get(), &Core::loadLayoutFromData, layoutManager, &LayoutManagement::slotLoadLayoutFromData, Qt::DirectConnection);
    connect(pCore.get(), &Core::adjustLayoutToDar, layoutManager, &LayoutManagement::adjustLayoutToDar, Qt::DirectConnection);
    connect(pCore.get(), &Core::recheckLayoutScreen, layoutManager, &LayoutManagement::recheckScreen, Qt::DirectConnection);
    pCore->buildDocks();

    m_clipMonitor = new Monitor(Kdenlive::ClipMonitor, pCore->monitorManager(), this);
    connect(m_clipMonitor, &Monitor::addMarker, this, &MainWindow::slotAddMarkerGuideQuickly);
    connect(m_clipMonitor, &Monitor::addMarker, this, &MainWindow::slotAddMarkerWithCategory);
    connect(m_clipMonitor, &Monitor::deleteMarker, this, &MainWindow::slotDeleteClipMarker);
    connect(m_clipMonitor, &Monitor::seekToPreviousSnap, this, &MainWindow::slotSnapRewind);
    connect(m_clipMonitor, &Monitor::seekToNextSnap, this, &MainWindow::slotSnapForward);
    connect(m_clipMonitor, &Monitor::passKeyPress, this, &MainWindow::triggerKey);

    m_projectMonitor = new Monitor(Kdenlive::ProjectMonitor, pCore->monitorManager(), this);
    connect(m_projectMonitor, &Monitor::passKeyPress, this, &MainWindow::triggerKey);
    connect(m_projectMonitor, &Monitor::addMarker, this, &MainWindow::slotAddMarkerGuideQuickly);
    connect(m_projectMonitor, &Monitor::addMarker, this, &MainWindow::slotAddMarkerWithCategory);
    connect(m_projectMonitor, &Monitor::deleteMarker, this, &MainWindow::slotDeleteGuide);
    connect(m_projectMonitor, &Monitor::seekToPreviousSnap, this, &MainWindow::slotSnapRewind);
    connect(m_projectMonitor, &Monitor::seekToNextSnap, this, &MainWindow::slotSnapForward);
    connect(m_loopClip, &QAction::triggered, this, [&]() {
        std::pair<int, int> inOut = getCurrentTimeline()->controller()->selectionInOut();
        m_projectMonitor->slotLoopClip(inOut);
    });
    installEventFilter(this);
    pCore->monitorManager()->initMonitors(m_clipMonitor, m_projectMonitor);

    m_timelineTabs = new TimelineTabs();
    // KoraCut, 24 septembre 2026 : la zone qui porte les onglets de séquences et les vues côte à
    // côte (document 13, § 5). Une seule vue tant qu'on ne glisse pas un onglet à côté.
    ctnLay->addWidget(m_timelineTabs->splitHost());

    // KoraCut, 23 septembre 2026 : flèches haut et bas = coupe précédente, coupe suivante (Premiere,
    // Resolve ; document 9). Seulement quand le clavier est dans la timeline ou un moniteur : partout
    // ailleurs, les flèches servent à parcourir les listes (chutier, effets), et un raccourci global
    // les leur volerait. Les actions nommées gardent Alt+gauche et Alt+droite, modifiables.
    for (const auto &[key, name] : {std::pair{Qt::Key_Up, "monitor_seek_snap_backward"}, std::pair{Qt::Key_Down, "monitor_seek_snap_forward"}}) {
        auto *arrow = new QAction(this);
        arrow->setShortcut(key);
        arrow->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(arrow, &QAction::triggered, this, [this, name = QString::fromLatin1(name)]() {
            if (QAction *target = actionCollection()->action(name)) {
                target->trigger();
            }
        });
        m_timelineTabs->splitHost()->addAction(arrow);
        m_clipMonitor->addAction(arrow);
        m_projectMonitor->addAction(arrow);
    }

    // Timeline dock
    const QSize firstWindowSize = mainDockWindow->size();
    m_timelineDock = addDock(i18n("Timeline"), QStringLiteral("timeline"), m_timelineToolBarContainer, KDDockWidgets::Location_OnBottom, nullptr);
    // La timeline n'a pas de barre d'onglets : le triple clic qui l'agrandit se fait sur sa barre d'outils.
    KoraCutPanelFocus::watchTripleClick(m_timelineToolBar, [this](const QPoint &) { return KoraCutPanelFocus::dockContaining(m_timelineToolBar); });

    // Project bin
    m_projectBinDock = addDock(i18nc("@title:window nom court d'un panneau", "Media"), QStringLiteral("project_bin"), pCore->bin(), KDDockWidgets::Location_OnTop, m_timelineDock);

    auto dockLib = addDock(i18n("Library"), QStringLiteral("library"), pCore->library(), KDDockWidgets::Location_OnRight);
    dockLib->close();

    auto dockSubs = addDock(i18n("Subtitles"), QStringLiteral("subtitles"), pCore->subtitleWidget(), KDDockWidgets::Location_OnRight);
    dockSubs->close();

    auto dockText = addDock(i18nc("@title:window nom court d'un panneau", "Voice"), QStringLiteral("textedit"), pCore->textEditWidget(), KDDockWidgets::Location_OnRight);
    dockText->close();

    auto dockRemap = addDock(i18nc("@title:window nom court d'un panneau", "Speed"), QStringLiteral("timeremap"), pCore->timeRemapWidget(), KDDockWidgets::Location_OnRight);
    dockRemap->close();

    connect(pCore.get(), &Core::remapClip, this, [&, dockRemap](int id) {
        if (id > -1) {
            dockRemap->open();
            dockRemap->setAsCurrentTab();
        }
        pCore->timeRemapWidget()->selectedClip(id, pCore->currentTimelineId());
    });

    auto dockGuides = addDock(i18n("Markers"), QStringLiteral("markers"), pCore->guidesList(), KDDockWidgets::Location_OnRight);
    dockGuides->close();

    // Screen grab widget
    QWidget *grabWidget = new QWidget(this);
    auto *grabLayout = new QVBoxLayout;
    grabWidget->setLayout(grabLayout);
    auto *recToolbar = new QToolBar(grabWidget);
    grabLayout->addWidget(recToolbar);
    grabLayout->addStretch(10);
    // Check number of monitors for FFmpeg screen capture
    int screens = QApplication::screens().count();
    if (screens > 1) {
        auto *screenCombo = new QComboBox(recToolbar);
        for (int ix = 0; ix < screens; ix++) {
            screenCombo->addItem(i18n("Monitor %1", ix));
        }
        connect(screenCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), m_clipMonitor, &Monitor::slotSetScreen);
        recToolbar->addWidget(screenCombo);
        // Update screen grab monitor choice in case we changed from fullscreen
        screenCombo->setEnabled(KdenliveSettings::grab_capture_type() == 0);
    }
    // Audio record action
    QAction *recAudio = new QAction(QIcon::fromTheme(QStringLiteral("media-record")), i18n("Record"), this);
    addAction(QStringLiteral("audio_record"), recAudio, Qt::Key_R);
    connect(recAudio, &QAction::triggered, [&]() {
        if (pCore->isMediaMonitoring() || pCore->isMediaCapturing()) {
            getCurrentTimeline()->controller()->switchRecording();
        } else {
            pCore->displayMessage(i18n("Enable audio monitoring from the Mixer to record"), ErrorMessage);
        }
    });

    // Screengrab record action
    QAction *recAction = m_clipMonitor->recAction();
    addAction(QStringLiteral("screengrab_record"), recAction);
    recToolbar->addAction(recAction);
    QAction *recConfig = new QAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Configure Recording"), this);
    recToolbar->addAction(recConfig);
    connect(recConfig, &QAction::triggered, [&]() { Q_EMIT pCore->showConfigDialog(Kdenlive::PageCapture, 0); });
    auto screenGrabDock = addDock(i18nc("@title:window nom court d'un panneau", "Capture"), QStringLiteral("screengrab"), grabWidget);
    screenGrabDock->close();

    // Sequence actions
    QAction *nextSequence = new QAction(QIcon::fromTheme(QStringLiteral("go-next")), i18n("Switch to next Sequence"), this);
    addAction(QStringLiteral("sequence_next"), nextSequence, QKeySequence::NextChild);
    connect(nextSequence, &QAction::triggered, m_timelineTabs, &TimelineTabs::slotNextSequence);

    QAction *prevSequence = new QAction(QIcon::fromTheme(QStringLiteral("go-previous")), i18n("Switch to previous Sequence"), this);
    addAction(QStringLiteral("sequence_previous"), prevSequence, QKeySequence::PreviousChild);
    connect(prevSequence, &QAction::triggered, m_timelineTabs, &TimelineTabs::slotPreviousSequence);

    // Audio spectrum scope
    m_audioSpectrum = new AudioGraphSpectrum(pCore->monitorManager());
    auto spectrumDock = addDock(i18nc("@title:window nom court d'un panneau", "Spectrum"), QStringLiteral("audiospectrum"), m_audioSpectrum);
    connect(spectrumDock, &KDDockWidgets::QtWidgets::DockWidget::isOpenChanged, this, [&](bool visible) { m_audioSpectrum->dockVisible(visible); });
    spectrumDock->close();

    // Add monitors here to keep them at the right of the window.
    // KoraCut : un seul moniteur par défaut, les deux onglets Source et Programme dans le même
    // groupe (« 9 - UX de l'application »). Deux moniteurs côte à côte laissaient la moitié haute
    // de la fenêtre à deux zones vides de plus de 600 px. Le second revient par ⌘⇧M.
    m_clipMonitorDock = addDock(i18nc("@title:window nom court d'un panneau", "Source"), QStringLiteral("clipmonitor"), m_clipMonitor, KDDockWidgets::Location_OnRight, m_projectBinDock);
    m_projectMonitorDock =
        addDock(i18nc("@title:window nom court d'un panneau", "Program"), QStringLiteral("projectmonitor"), m_projectMonitor,
                KdenliveSettings::singleMonitor() ? KDDockWidgets::Location_None : KDDockWidgets::Location_OnRight, m_clipMonitorDock);

    m_clipMonitorDock->addDockWidgetAsTab(dockLib);
    pCore->bin()->buildPropertiesDock(m_clipMonitorDock);
    m_projectMonitorDock->addDockWidgetAsTab(dockText);
    // Notes widget
    pCore->projectManager()->buildNotesWidget(m_projectMonitorDock);
    // Raise monitors
    m_clipMonitorDock->setAsCurrentTab();
    m_projectMonitorDock->setAsCurrentTab();

    // Media browser widget
    auto clipDockWidget = addDock(i18nc("@title:window nom court d'un panneau", "Files"), QStringLiteral("media_browser"), pCore->mediaBrowser());
    clipDockWidget->close();

    // Online resources widget
    auto *onlineResources = new ResourceWidget(this);
    m_onlineResourcesDock = addDock(i18nc("@title:window nom court d'un panneau", "Online"), QStringLiteral("onlineresources"), onlineResources);
    m_onlineResourcesDock->close();
    connect(onlineResources, &ResourceWidget::previewClip, this, [&](const QString &path, const QString &title) {
        m_clipMonitor->slotPreviewResource(path, title);
        m_clipMonitorDock->open();
        m_clipMonitorDock->setAsCurrentTab();
    });

    connect(onlineResources, &ResourceWidget::addClip, this, &MainWindow::slotAddProjectClip);
    connect(onlineResources, &ResourceWidget::addLicenseInfo, this, &MainWindow::slotAddTextNote);

    const QSize stackSize(firstWindowSize.width() * 0.3, 0);
    m_effectStackDock =
        addDock(i18nc("@title:window nom court d'un panneau", "Inspector"), QStringLiteral("effect_stack"), m_assetPanel, KDDockWidgets::Location_OnRight, m_timelineDock, stackSize);
    connect(pCore.get(), &Core::requestShowBinEffectStack, m_assetPanel, &AssetPanel::showEffectStack, Qt::QueuedConnection);
    connect(m_assetPanel, &AssetPanel::doSplitEffect, m_projectMonitor, &Monitor::slotSwitchCompare);
    connect(m_assetPanel, &AssetPanel::doSplitBinEffect, m_clipMonitor, &Monitor::slotSwitchCompare);
    connect(m_assetPanel, &AssetPanel::switchCurrentComposition, this,
            [&](int cid, const QString &compositionId) { getCurrentTimeline()->model()->switchComposition(cid, compositionId); });
    connect(pCore->bin(), &Bin::updateTabName, m_timelineTabs, &TimelineTabs::renameTab);
    connect(m_timelineTabs, &TimelineTabs::showMixModel, this, [&](int cid, std::shared_ptr<AssetParameterModel> model, bool refreshOnly) {
        m_assetPanel->showMix(cid, model, refreshOnly);
        if (m_effectStackDock->asDockWidgetController()->isTabbed() && m_effectStackDock->parent() == m_timelineDock->parent()) {
            // Don't raise if tabbed with timeline
            return;
        }
        if (KdenliveSettings::raisepropsmixes()) {
            m_effectStackDock->setAsCurrentTab();
        }
    });
    connect(m_timelineTabs, &TimelineTabs::showTransitionModel, this, [&](int tid, std::shared_ptr<AssetParameterModel> model) {
        m_assetPanel->showTransition(tid, model);
        if (m_effectStackDock->asDockWidgetController()->isTabbed() && m_effectStackDock->parent() == m_timelineDock->parent()) {
            // Don't raise if tabbed with timeline
            return;
        }
        if (KdenliveSettings::raisepropscompositions()) {
            m_effectStackDock->setAsCurrentTab();
        }
    });
    connect(m_timelineTabs, &TimelineTabs::showItemEffectStack, this,
            [&](const QString &clipName, std::shared_ptr<EffectStackModel> model, QSize size, bool showKeyframes) {
                if (model == nullptr && m_assetPanel->effectStackOwner().type == KdenliveObjectType::BinClip) {
                    // Effect stask is currently displaying a bin clip, do nothing
                    return;
                }
                m_assetPanel->showEffectStack(clipName, model, size, showKeyframes);
                if (m_motionInspector) {
                    m_motionInspector->showClip(model && model->getOwnerId().type == KdenliveObjectType::TimelineClip ? model->getOwnerId().itemId : -1);
                }
                if (m_clipTools) {
                    m_clipTools->showClip(model && model->getOwnerId().type == KdenliveObjectType::TimelineClip ? model->getOwnerId().itemId : -1);
                }
                if (m_effectStackDock->asDockWidgetController()->isTabbed() && m_effectStackDock->parent() == m_timelineDock->parent()) {
                    // Don't raise if tabbed with timeline
                    return;
                }
                // KoraCut : dans le module Étalonnage, les palettes restent devant (comme la page Color de
                // Resolve) ; choisir un plan ne fait plus passer l'Inspecteur au premier plan.
                if (property("koracut_etalonnage_actif").toBool()) {
                    return;
                }
                bool isClip = model && model->getOwnerId().type == KdenliveObjectType::TimelineClip;
                bool isTrack = model && model->getOwnerId().type == KdenliveObjectType::TimelineTrack;
                if ((isClip && KdenliveSettings::raisepropsclips()) || (isTrack && KdenliveSettings::raisepropstracks())) {
                    m_effectStackDock->setAsCurrentTab();
                }
            });

    connect(m_timelineTabs, &TimelineTabs::updateAssetPosition, m_assetPanel, &AssetPanel::updateAssetPosition);

    connect(m_timelineTabs, &TimelineTabs::showSubtitle, this, [&, dockSubs](int id) {
        if (id > -1) {
            dockSubs->open();
            dockSubs->setAsCurrentTab();
        }
        pCore->subtitleWidget()->setActiveSubtitle(id);
    });

    connect(pCore.get(), &Core::finalizeRecording, this, [this](const QUuid uuid, const QString &filename) {
        auto timeline = getTimeline(uuid);
        if (timeline) {
            timeline->controller()->finishRecording(filename);
        }
    });

    connect(m_timelineTabs, &TimelineTabs::updateZoom, this, &MainWindow::updateZoomSlider);
    connect(this, &MainWindow::clearAssetPanel, m_assetPanel, &AssetPanel::clearAssetPanel, Qt::DirectConnection);
    connect(this, &MainWindow::assetPanelWarning, m_assetPanel, &AssetPanel::assetPanelWarning);
    connect(m_assetPanel, &AssetPanel::seekToPos, this, [this](int pos) {
        ObjectId oId = m_assetPanel->effectStackOwner();
        switch (oId.type) {
        case KdenliveObjectType::TimelineTrack:
        case KdenliveObjectType::TimelineClip:
        case KdenliveObjectType::TimelineComposition:
        case KdenliveObjectType::Master:
        case KdenliveObjectType::TimelineMix:
            m_projectMonitor->requestSeek(pos);
            break;
        case KdenliveObjectType::BinClip:
            m_clipMonitor->requestSeek(pos);
            break;
        default:
            qDebug() << "ERROR unhandled object type";
            break;
        }
    });

    // Assets filter options
    QAction *includeList = new QAction(QIcon::fromTheme(QStringLiteral("games-solve")), i18n("Only show reviewed items"), this);
    includeList->setCheckable(true);
    includeList->setChecked(KdenliveSettings::enableAssetsIncludeList());
    // 10 bit support
    QAction *tenBit = new QAction(QIcon::fromTheme(QStringLiteral("colormanagement")), i18n("Only show 10 bit compatible items"), this);
    tenBit->setCheckable(true);
    tenBit->setChecked(KdenliveSettings::tenbitpipeline());

    m_effectList2 = new EffectListWidget(includeList, tenBit, this);
    connect(m_effectList2, &EffectListWidget::activateAsset, pCore->projectManager(), &ProjectManager::activateAsset);
    connect(m_assetPanel, &AssetPanel::reloadEffect, m_effectList2, &EffectListWidget::reloadCustomEffect);
    m_effectListDock = addDock(i18n("Effects"), QStringLiteral("effect_list"), m_effectList2, KDDockWidgets::Location_None, m_projectBinDock);

    m_compositionList = new TransitionListWidget(includeList, tenBit, this);
    m_compositionListDock = addDock(i18n("Compositions"), QStringLiteral("transition_list"), m_compositionList, KDDockWidgets::Location_None, m_projectBinDock);

    // Clear history action
    QAction *cleanHistory = new QAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18n("Clear Undo History"), this);
    addAction(QStringLiteral("clear_undo_history"), cleanHistory);
    connect(cleanHistory, &QAction::triggered, this, [this]() {
        if (KMessageBox::warningContinueCancel(this, i18n("This will clear all undo history, you will not be able to undo any previous action.")) !=
            KMessageBox::Continue) {
            return;
        }
        QList<QUndoStack *> stacks = m_commandStack->stacks();
        for (auto &s : stacks) {
            s->clear();
        }
    });

    m_undoView = new QUndoView();
    m_undoView->setCleanIcon(QIcon::fromTheme(QStringLiteral("edit-clear")));
    m_undoView->setEmptyLabel(i18n("Clean"));
    m_undoView->setGroup(m_commandStack);
    m_undoView->addAction(cleanHistory);

    m_undoView->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_undoViewDock = addDock(i18nc("@title:window nom court d'un panneau", "History"), QStringLiteral("undo_history"), m_undoView, KDDockWidgets::Location_None, m_projectBinDock);

    connect(m_commandStack, &QUndoGroup::cleanChanged, [this, cleanHistory](bool isClean) {
        m_saveAction->setDisabled(isClean);
        cleanHistory->setDisabled(isClean);
    });

    // Switch title bars
    // KoraCut : les panneaux sont verrouillés par défaut ; cette case rend la liberté de les déplacer.
    auto switchAction = new QAction(i18nc("@action:inmenu rend les panneaux déplaçables", "Unlock Panels"), this);
    switchAction->setCheckable(true);
    switchAction->setChecked(KdenliveSettings::showtitlebars());
    addAction(QStringLiteral("show_titlebars"), switchAction);
    connect(switchAction, &QAction::triggered, this, [](bool checked) {
        KdenliveSettings::setShowtitlebars(checked);
        Q_EMIT pCore->hideBars(!checked);
    });
    connect(pCore.get(), &Core::switchTitleBars, this, [switchAction]() { switchAction->trigger(); });

    QAction *stylesAction = KStyleManager::createConfigureAction(this);
    // stylesAction->menu() is only available on non KDE platform
    if (stylesAction->menu()) {
        // Color and icon theme stuff
        const QStringList stylesToHide = {
            QStringLiteral("windowsvista"), // recoloring does not work well
            QStringLiteral("Windows"),
            // QStringLiteral("macintosh")
        };
        for (QAction *child : stylesAction->menu()->actions()) {
            if (stylesToHide.contains(child->data().toString(), Qt::CaseInsensitive)) {
                child->setVisible(false);
            }
        }
    }
    addAction(QStringLiteral("styles_menu"), stylesAction);
    // KoraCut : le style de base est le même sur les trois systèmes (main.cpp) ; le menu Style
    // ferait croire qu'on peut en changer, et le choix n'aurait plus d'effet. Chantier 12.
    if (!qEnvironmentVariableIsSet("KORACUT_NATIVE_STYLE")) {
        stylesAction->setVisible(false);
    }

    m_mixerDock = addDock(i18nc("@title:window nom court d'un panneau", "Mixer"), QStringLiteral("mixer"), pCore->mixer(), KDDockWidgets::Location_None, m_effectStackDock);

    // Chantier 9 : le module Motion (document 19). Le miroir suit le document courant, le panneau est
    // un onglet du bloc Médias, l'inspecteur une section en tête de la pile d'effets.
    m_motionMirror = new KoraCutMotionMirror(this);
    m_motionStudio = new KoraCutMotionStudio(this);
    m_motionPanel = new KoraCutMotionPanel(m_motionMirror, m_motionStudio, this);
    m_motionDock = addDock(i18nc("@title:window nom court d'un panneau", "Motion"), QStringLiteral("motion"), m_motionPanel, KDDockWidgets::Location_None, m_projectBinDock);
    m_motionInspector = new KoraCutMotionInspector(m_motionMirror, m_motionStudio, m_assetPanel);
    m_assetPanel->koraCutInsertTopWidget(m_motionInspector);
    // Document 24 : la section Outils, en tête de la zone qui défile de l'inspecteur.
    m_clipTools = new KoraCutClipToolsPanel(m_assetPanel);
    m_assetPanel->koraCutInsertToolsWidget(m_clipTools);
    m_mixerDock->setWhatsThis(xi18nc("@info:whatsthis", "Toggles the audio mixer panel/widget."));
    QAction *showMixer = new QAction(QIcon::fromTheme(QStringLiteral("view-media-equalizer")), i18n("Audio Mixer"), this);
    showMixer->setCheckable(true);
    addAction(QStringLiteral("audiomixer_button"), showMixer);
    connect(m_mixerDock, &KDDockWidgets::QtWidgets::DockWidget::isOpenChanged, this, [&, showMixer](bool visible) {
        pCore->mixer()->connectMixer(visible);
        pCore->audioMixerVisible = visible;
        m_projectMonitor->displayAudioMonitor(m_projectMonitor->isActive());
        showMixer->setChecked(visible);
    });
    connect(showMixer, &QAction::triggered, this, [&]() {
        if (m_mixerDock->isVisible() && !m_mixerDock->visibleRegion().isEmpty()) {
            m_mixerDock->close();
        } else {
            m_mixerDock->open();
            m_mixerDock->setAsCurrentTab();
        }
    });

#if KORACUT_AGENTS
    // KoraCut, chantier 3 (23 septembre 2026) : le panneau Agents, onglet de l'inspecteur. Le service
    // décide, l'application exécute les outils ; tout un tour s'annule d'un seul Annuler.
    // L'écran de démarrage le dit, maintenant que les agents existent dans l'application (chantier 1, charte).
    Q_EMIT pCore->loadingMessageNewStage(
        i18nc("@info:progress splash screen, the six agents", "Waking the agents… Editor, Colorist, Sound designer, Mixer, Retoucher, Composer"));
    m_agentClient = new KoraCutAgentClient(this);
    m_agentSession = new KoraCutAgentSession(m_agentClient, this);
    m_agentSession->setCallBudget(KdenliveSettings::koracut_agents_budget());
    {
        const KUser user;
        QString firstName = user.property(KUser::FullName).toString().section(QLatin1Char(' '), 0, 0);
        if (firstName.isEmpty()) {
            firstName = user.loginName();
        }
        m_agentSession->setPerson(firstName);
        switch (MachineProfile::effectiveLevel()) {
        case MachineProfile::Level::Modest:
            m_agentSession->setMachineProfile(QStringLiteral("modeste"));
            break;
        case MachineProfile::Level::Medium:
            m_agentSession->setMachineProfile(QStringLiteral("moyenne"));
            break;
        case MachineProfile::Level::Comfortable:
            m_agentSession->setMachineProfile(QStringLiteral("confortable"));
            break;
        }
    }
    m_agentPanel = new KoraCutAgentPanel(m_agentSession, this);
    m_agentDock = addDock(i18nc("@title:window nom court d'un panneau", "Agents"), QStringLiteral("agents"), m_agentPanel, KDDockWidgets::Location_None, m_effectStackDock);
    // La pastille de la barre du haut suit la liaison et le tour : gris, vert, bleu qui respire, rouge.
    auto refreshAgentsPill = [this]() {
        KoraCutTopBar::AgentsState state = KoraCutTopBar::AgentsState::NoService;
        QString tip = i18nc("@info:tooltip", "No agents service");
        switch (m_agentSession->linkState()) {
        case KoraCutAgents::LinkState::NoService:
            break;
        case KoraCutAgents::LinkState::Connecting:
            tip = i18nc("@info:tooltip", "Connecting to the agents service…");
            break;
        case KoraCutAgents::LinkState::Error:
            state = KoraCutTopBar::AgentsState::Error;
            tip = i18nc("@info:tooltip", "Agents service: error");
            break;
        case KoraCutAgents::LinkState::Connected:
            state = m_agentSession->turnOpen() ? KoraCutTopBar::AgentsState::Working : KoraCutTopBar::AgentsState::Ready;
            tip = m_agentSession->turnOpen() ? i18nc("@info:tooltip", "An agent is working") : i18nc("@info:tooltip", "Agents ready");
            break;
        }
        m_topBar->setAgentsState(state, tip);
    };
    connect(m_agentSession, &KoraCutAgentSession::linkStateChanged, this, refreshAgentsPill);
    connect(m_agentSession, &KoraCutAgentSession::turnStarted, this, refreshAgentsPill);
    connect(m_agentSession, &KoraCutAgentSession::turnFinished, this, refreshAgentsPill);
    connect(m_agentSession, &KoraCutAgentSession::turnCancelled, this, refreshAgentsPill);
    connect(m_topBar, &KoraCutTopBar::agentsRequested, this, &MainWindow::slotShowAgentsPanel);
    refreshAgentsPill();
    // Connexions IA (25 septembre 2026) : les modèles de la personne, avec leurs clés, partent vers le
    // service local ; sans adresse réglée, l'application lance ce service à l'ouverture du panneau.
    auto *aiBridge = new KoraCutAiBridge(m_agentClient, m_agentPanel, this);
    aiBridge->start();
#endif

    // KoraCut, la porte de commande (document 18, lot 1, 24 septembre 2026) : n'importe quel programme
    // de la machine pilote l'application par une prise locale ; le panneau Console montre ce qu'elle
    // reçoit et demande les accords. La prise s'ouvre une fois l'interface prête (koraCutStartPorte).
    m_porte = new KoraCutPorte(this);
    // À la sortie, la porte se ferme et retire son fichier, même si la fenêtre n'est pas détruite (SIGTERM, Quitter).
    connect(qApp, &QCoreApplication::aboutToQuit, m_porte, &KoraCutPorte::close);
    // Une instance pilotée par un agent le dit dans son titre et ne se ferme pas sans prévenir (1er octobre 2026).
    connect(m_porte, &KoraCutPorte::turnsChanged, this, &MainWindow::koraCutRefreshPilotedTitle);
#if KORACUT_AGENTS
    connect(m_agentSession, &KoraCutAgentSession::turnStarted, this, &MainWindow::koraCutRefreshPilotedTitle);
    connect(m_agentSession, &KoraCutAgentSession::turnFinished, this, &MainWindow::koraCutRefreshPilotedTitle);
    connect(m_agentSession, &KoraCutAgentSession::turnCancelled, this, &MainWindow::koraCutRefreshPilotedTitle);
#endif
    KoraCutPorteContext porteContext;
    porteContext.window = this;
    KoraCutPorteMethodes::registerAll(m_porte, porteContext);
    m_consolePanel = new KoraCutConsolePanel(m_porte, this);
    m_consoleDock = addDock(i18nc("@title:window nom court d'un panneau", "Console"), QStringLiteral("console"), m_consolePanel, KDDockWidgets::Location_None, m_effectStackDock);
    // Chantier 10 : le panneau Diagnostic, fermé par défaut ; il ne mesure rien tant qu'il est caché.
    m_perfPanel = new KoraCutPerfPanel(this);
    m_perfDock = addDock(i18nc("@title:window nom court d'un panneau", "Diagnostic"), QStringLiteral("diagnostic"), m_perfPanel, KDDockWidgets::Location_None, m_effectStackDock);
    connect(m_consolePanel, &KoraCutConsolePanel::seekRequested, this, [this](int position) {
        if (getCurrentTimeline()) {
            pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
            getCurrentTimeline()->controller()->setPosition(position);
        }
    });

    // KoraCut, document 18 : le terminal intégré. Depuis le 4 octobre 2026, à droite de la timeline par défaut
    // (moitié-moitié), et la personne le met où elle veut (koraCutPlaceTerminal). Aucun processus ne tourne
    // tant qu'aucun onglet n'est ouvert.
    m_terminalPanel = new KoraCutTerminalPanel(m_porte, this);
    m_terminalDock = addDock(i18nc("@title:window nom court d'un panneau", "Terminal"), QStringLiteral("terminal"), m_terminalPanel,
                             KDDockWidgets::Location_OnRight, m_timelineDock);
    connect(m_terminalPanel, &KoraCutTerminalPanel::placeRequested, this, &MainWindow::koraCutPlaceTerminal);
    // Une disposition enregistrée avec le terminal ouvert sous la timeline (versions d'avant) : replacé une fois, au calme.
    QTimer::singleShot(4000, this, [this]() {
        if (m_terminalDock && m_terminalDock->isOpen() && KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("terminalPlaceGeneration", 0) < 1) {
            koraCutRevealTerminalDock();
        }
    });
    connect(m_consolePanel, &KoraCutConsolePanel::integratedTerminalRequested, this, &MainWindow::slotShowTerminalPanel);
    connect(m_consolePanel, &KoraCutConsolePanel::systemTerminalRequested, this, [this]() { m_terminalPanel->openInSystemTerminal(); });
    connect(m_terminalPanel, &KoraCutTerminalPanel::saveProjectRequested, this, [this]() {
        if (QAction *save = actionCollection()->action(KStandardAction::name(KStandardAction::Save))) {
            save->trigger();
        }
        // Le projet a maintenant un dossier : l'agent qui l'attendait part tout seul.
        QTimer::singleShot(0, m_terminalPanel, &KoraCutTerminalPanel::resumePending);
    });
#if KORACUT_AGENTS
    connect(m_agentPanel, &KoraCutAgentPanel::claudeCodeRequested, this, &MainWindow::slotEditWithClaudeCode);
    // La page de choix du terminal présente les deux voies : la première mène aux Connexions IA.
    m_terminalPanel->setChatWayAvailable(true);
    connect(m_terminalPanel, &KoraCutTerminalPanel::connectModelRequested, this, []() { KoraCutAiConnectionsPage::openInPreferences(); });
#endif

    // KoraCut, document 21 : le panneau Script, l'éditeur du script du projet (onglet du bloc de droite).
    auto *scriptPanel = new KoraCutScriptPanel(this);
    addDock(i18nc("@title:window nom court d'un panneau", "Script"), QStringLiteral("script"), scriptPanel, KDDockWidgets::Location_None, m_effectStackDock);
    connect(scriptPanel, &KoraCutScriptPanel::seekRequested, this, [this](int position) {
        if (getCurrentTimeline()) {
            pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
            getCurrentTimeline()->controller()->setPosition(position);
        }
    });

    // KoraCut, document 22 : le module Étalonnage comme la page Color de DaVinci Resolve (nœuds,
    // palettes, bande des plans, galerie). Ces panneaux ne vivent que dans la disposition Étalonnage.
    auto *colorNodes = new KoraCutNodeEditor(this);
    addDock(i18nc("@title:window nom court d'un panneau", "Nodes"), QStringLiteral("etal_noeuds"), colorNodes, KDDockWidgets::Location_None, m_effectStackDock);
    auto *colorPalettes = new KoraCutColorPalettes(this);
    addDock(i18nc("@title:window nom court d'un panneau", "Palettes"), QStringLiteral("etal_palettes"), colorPalettes, KDDockWidgets::Location_None, m_effectStackDock);
    addDock(i18nc("@title:window nom court d'un panneau, les plans à étalonner", "Clips"), QStringLiteral("etal_plans"), new KoraCutClipStrip(this),
            KDDockWidgets::Location_None, m_effectStackDock);
    addDock(i18nc("@title:window nom court d'un panneau, les stills de l'étalonnage", "Gallery"), QStringLiteral("etal_galerie"), new KoraCutGallery(this),
            KDDockWidgets::Location_None, m_effectStackDock);
    new KoraCutColorPage(this, colorNodes, colorPalettes);

    // KoraCut, document 23 : le module Export comme la page Deliver de DaVinci Resolve (modèles en tuiles
    // avec les vrais logos, paramètres en onglets Vidéo / Audio / Fichier, file de rendu). Ces panneaux ne
    // vivent que dans la disposition Export ; le moniteur Programme et la timeline sont ceux du Montage.
    auto *exportTiles = new KoraCutExportTiles(this);
    addDock(i18nc("@title:window nom court d'un panneau, les modèles d'export", "Models"), QStringLiteral("export_modeles"), exportTiles, KDDockWidgets::Location_None,
            m_effectStackDock);
    auto *exportSettings = new KoraCutExportSettingsPanel(this);
    addDock(i18nc("@title:window nom court d'un panneau", "Export Settings"), QStringLiteral("export_reglages"), exportSettings, KDDockWidgets::Location_None,
            m_effectStackDock);
    auto *exportQueue = new KoraCutExportQueuePanel(this);
    addDock(i18nc("@title:window nom court d'un panneau", "Render Queue"), QStringLiteral("export_file"), exportQueue, KDDockWidgets::Location_None, m_effectStackDock);
    new KoraCutDeliverPage(this, exportTiles, exportSettings, exportQueue);
    connect(exportSettings, &KoraCutExportSettingsPanel::advancedRequested, this, &MainWindow::slotRenderProject);
    connect(pCore.get(), &Core::loadLayoutById, this, [](const QString &id) {
        if (id == QLatin1String("export") && KoraCutDeliverPage::instance()) {
            KoraCutDeliverPage::instance()->reload();
        }
    });
    // Les playlists de rendu d'avant, laissées par une instance arrêtée (plus d'un jour).
    QTimer::singleShot(20000, this, []() { KoraCutExport::cleanTemporaryFiles(); });

    // Close non-general docks for the initial layout
    // only show important ones
    m_mixerDock->close();
    m_terminalDock->close();
    for (const QString &name : {QStringLiteral("etal_noeuds"), QStringLiteral("etal_palettes"), QStringLiteral("etal_plans"), QStringLiteral("etal_galerie"),
                                 QStringLiteral("export_modeles"), QStringLiteral("export_reglages"), QStringLiteral("export_file")}) {
        if (auto *d = KDDockWidgets::DockRegistry::self()->dockByName(name)) {
            d->forceClose();
        }
    }
    m_projectBinDock->setAsCurrentTab();

    bool firstRun = readOptions();
    if (KdenliveSettings::lastCacheCheck().isNull()) {
        // Define a date for first check
        KdenliveSettings::setLastCacheCheck(QDateTime::currentDateTime());
    }

    // Build effects menu
    m_effectsMenu = new QMenu(i18n("Add Effect"), this);
    m_effectActions = new KActionCategory(i18n("Effects"), actionCollection());
    m_effectList2->reloadEffectMenu(m_effectsMenu, m_effectActions);

    m_transitionsMenu = new QMenu(i18n("Add Transition"), this);
    m_transitionActions = new KActionCategory(i18n("Transitions"), actionCollection());

    m_scopesManager = new ScopeManager(this);

    m_extraFactory = new KXMLGUIClient(this);
    buildDynamicActions();

    // Create Effect Basket (dropdown list of favorites)
    m_effectBasket = new EffectBasket(this);
    connect(m_effectBasket, &EffectBasket::activateAsset, pCore->projectManager(), &ProjectManager::activateAsset);
    connect(m_effectList2, &EffectListWidget::reloadFavorites, m_effectBasket, &EffectBasket::slotReloadBasket);
    auto *widgetlist = new QWidgetAction(this);
    widgetlist->setDefaultWidget(m_effectBasket);
    // widgetlist->setText(i18n("Favorite Effects"));
    widgetlist->setToolTip(i18n("Favorite Effects"));
    widgetlist->setWhatsThis(xi18nc("@info:whatsthis", "Click to show a list of favorite effects. Double-click on an effect to add it to the selected clip."));
    widgetlist->setIcon(QIcon::fromTheme(QStringLiteral("favorite")));
    auto *menu = new QMenu(this);
    menu->addAction(widgetlist);

    auto *basketButton = new QToolButton(this);
    basketButton->setMenu(menu);
    basketButton->setToolButtonStyle(toolBar()->toolButtonStyle());
    basketButton->setDefaultAction(widgetlist);
    basketButton->setPopupMode(QToolButton::InstantPopup);
    // basketButton->setText(i18n("Favorite Effects"));
    basketButton->setToolTip(i18n("Favorite Effects"));
    basketButton->setWhatsThis(
        xi18nc("@info:whatsthis", "Click to show a list of favorite effects. Double-click on an effect to add it to the selected clip."));

    basketButton->setIcon(QIcon::fromTheme(QStringLiteral("favorite")));

    auto *toolButtonAction = new QWidgetAction(this);
    toolButtonAction->setText(i18n("Favorite Effects"));
    toolButtonAction->setIcon(QIcon::fromTheme(QStringLiteral("favorite")));
    toolButtonAction->setDefaultWidget(basketButton);
    addAction(QStringLiteral("favorite_effects"), toolButtonAction);
    connect(toolButtonAction, &QAction::triggered, basketButton, &QToolButton::showMenu);
    connect(m_effectBasket, &EffectBasket::activateAsset, menu, &QMenu::close);

    // Render button
    ProgressButton *timelineRender = new ProgressButton(i18n("Render…"), 100, this);
    auto *tlrMenu = new QMenu(this);
    timelineRender->setMenu(tlrMenu);
    connect(this, &MainWindow::setRenderProgress, timelineRender, &ProgressButton::setProgress);
    auto *renderButtonAction = new QWidgetAction(this);
    renderButtonAction->setText(i18nc("@intoolbar the name of the action to place an export/render button in a toolbar", "Render Button"));
    renderButtonAction->setIcon(QIcon::fromTheme(QStringLiteral("media-record")));
    renderButtonAction->setDefaultWidget(timelineRender);
    addAction(QStringLiteral("project_render_button"), renderButtonAction);

    // Timeline preview button
    // KoraCut (24 septembre 2026) : le bouton « Rendu », avec son état en toutes lettres (koracutrender.cpp).
    auto *timelinePreview = new KoraCutRenderButton(this);
    auto *tlMenu = new QMenu(this);
    timelinePreview->setMenu(tlMenu);
    auto *previewButtonAction = new QWidgetAction(this);
    previewButtonAction->setText(i18nc("@action:intoolbar", "Render Button"));
    previewButtonAction->setIcon(QIcon::fromTheme(QStringLiteral("preview-render-on")));
    previewButtonAction->setDefaultWidget(timelinePreview);
    addAction(QStringLiteral("timeline_preview_button"), previewButtonAction);

    // Since not all widgets are added yet, don't use the Save flag now
    setupGUI(KXmlGuiWindow::ToolBar | KXmlGuiWindow::StatusBar | KXmlGuiWindow::Create);

    // Remove secondary cut shortcut conflicting with extract action
    QAction *officialCut = actionCollection()->action(KStandardAction::name(KStandardAction::Cut));
    QList<QKeySequence> cutShortcuts = officialCut->shortcuts();
    if (cutShortcuts.size() > 1) {
        if (cutShortcuts.at(1) == QKeySequence(Qt::SHIFT | Qt::Key_Delete)) {
            cutShortcuts.takeLast();
            officialCut->setShortcuts(cutShortcuts);
        }
    }

    // Only start saving config once all GUI setup is done.
    connect(pCore.get(), &Core::GUISetupDone, this, &MainWindow::finishUiSetup, Qt::DirectConnection);

    LocaleHandling::resetLocale();

    m_timelineToolBar->setToolButtonStyle(Qt::ToolButtonFollowStyle);
    m_timelineToolBar->setProperty("otherToolbar", true);
    timelinePreview->setToolButtonStyle(m_timelineToolBar->toolButtonStyle());
    connect(m_timelineToolBar, &QToolBar::toolButtonStyleChanged, timelinePreview, &ProgressButton::setToolButtonStyle);

    timelineRender->setToolButtonStyle(toolBar()->toolButtonStyle());
    /*ScriptingPart* sp = new ScriptingPart(this, QStringList());
    guiFactory()->addClient(sp);*/

    loadDockActions();
    loadClipActions();
    loadContainerActions();

    KEditToolBar::setGlobalDefaultToolBar(QStringLiteral("timelineToolBar"));

    // Timeline composition menu
    auto *compositionMenu = new QMenu(this);
    compositionMenu->addAction(actionCollection()->action(QStringLiteral("edit_item_duration")));
    compositionMenu->addAction(actionCollection()->action(QStringLiteral("edit_copy")));
    compositionMenu->addAction(actionCollection()->action(QStringLiteral("delete_timeline_clip")));

    // Timeline main menu
    auto *timelineMenu = new QMenu(this);
    timelineMenu->addAction(actionCollection()->action(QStringLiteral("edit_paste")));
    timelineMenu->addAction(actionCollection()->action(QStringLiteral("insert_space")));
    timelineMenu->addAction(actionCollection()->action(QStringLiteral("delete_space")));
    timelineMenu->addAction(actionCollection()->action(QStringLiteral("delete_space_all_tracks")));
    timelineMenu->addAction(actionCollection()->action(QStringLiteral("add_sequence_marker")));
    timelineMenu->addAction(actionCollection()->action(QStringLiteral("edit_sequence_marker")));
    QMenu *identifyGapsTimelineMenu = new QMenu(i18n("Identify Gaps"), this);
    QAction *gapsAllTracksTimeline = new QAction(i18n("All Tracks"), this);
    connect(gapsAllTracksTimeline, &QAction::triggered, this, &MainWindow::slotAddMarkersAtGaps);
    identifyGapsTimelineMenu->addAction(gapsAllTracksTimeline);
    QAction *gapsActiveTrackTimeline = new QAction(i18n("Selected Track"), this);
    connect(gapsActiveTrackTimeline, &QAction::triggered, this, &MainWindow::slotAddMarkersAtGapsOnTrack);
    identifyGapsTimelineMenu->addAction(gapsActiveTrackTimeline);
    timelineMenu->addMenu(identifyGapsTimelineMenu);
    QMenu *guideMenu = new QMenu(i18n("Go to Marker…"), this);
    timelineMenu->addMenu(guideMenu);

    // Timeline ruler menu
    auto *timelineRulerMenu = new QMenu(this);
    timelineRulerMenu->addAction(actionCollection()->action(QStringLiteral("add_sequence_marker")));
    timelineRulerMenu->addAction(actionCollection()->action(QStringLiteral("edit_sequence_marker")));
    timelineRulerMenu->addAction(actionCollection()->action(QStringLiteral("lock_guides")));
    timelineRulerMenu->addAction(actionCollection()->action(QStringLiteral("export_guides")));
    timelineRulerMenu->addMenu(guideMenu);
    timelineRulerMenu->addAction(actionCollection()->action(QStringLiteral("mark_in")));
    timelineRulerMenu->addAction(actionCollection()->action(QStringLiteral("mark_out")));
    timelineRulerMenu->addAction(actionCollection()->action(QStringLiteral("select_timeline_zone")));

    auto *createZoneMarker = new QAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), i18n("Create Marker from Zone"), this);
    connect(createZoneMarker, &QAction::triggered, this, &MainWindow::slotCreateRangeMarkerFromZone);
    timelineRulerMenu->addAction(createZoneMarker);

    auto *createZoneMarkerQuick = new QAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), i18n("Create Marker from Zone Quickly"), this);
    connect(createZoneMarkerQuick, &QAction::triggered, this, &MainWindow::slotCreateRangeMarkerFromZoneQuick);
    timelineRulerMenu->addAction(createZoneMarkerQuick);

    timelineRulerMenu->addAction(actionCollection()->action(QStringLiteral("add_project_note")));
    timelineRulerMenu->addAction(actionCollection()->action(QStringLiteral("add_subtitle")));

    // Timeline subtitle menu
    auto *timelineSubtitleMenu = new QMenu(this);
    timelineSubtitleMenu->addAction(actionCollection()->action(QStringLiteral("edit_copy")));
    timelineSubtitleMenu->addAction(actionCollection()->action(QStringLiteral("delete_subtitle_clip")));

    // Timeline headers menu
    auto *timelineHeadersMenu = new QMenu(this);
    timelineHeadersMenu->addAction(actionCollection()->action(QStringLiteral("insert_track")));
    timelineHeadersMenu->addAction(actionCollection()->action(QStringLiteral("delete_track")));
    timelineHeadersMenu->addAction(actionCollection()->action(QStringLiteral("move_track_up")));
    timelineHeadersMenu->addAction(actionCollection()->action(QStringLiteral("move_track_down")));
    timelineHeadersMenu->addAction(actionCollection()->action(QStringLiteral("fit_all_tracks")));
    timelineHeadersMenu->addAction(actionCollection()->action(QStringLiteral("show_track_record")));
    connect(timelineHeadersMenu, &QMenu::aboutToShow, this, [this]() {
        auto moveUp = actionCollection()->action(QStringLiteral("move_track_up"));
        auto moveDown = actionCollection()->action(QStringLiteral("move_track_down"));
        auto timeline = getCurrentTimeline();
        if (!timeline || !timeline->controller()) {
            if (moveUp) {
                moveUp->setEnabled(false);
            }
            if (moveDown) {
                moveDown->setEnabled(false);
            }
            return;
        }
        if (moveUp) {
            moveUp->setEnabled(timeline->controller()->canMoveTrackUp());
        }
        if (moveDown) {
            moveDown->setEnabled(timeline->controller()->canMoveTrackDown());
        }
    });

    QAction *separate_channels = new QAction(QIcon(), i18n("Separate Channels"), this);
    separate_channels->setCheckable(true);
    separate_channels->setChecked(KdenliveSettings::displayallchannels());
    separate_channels->setData("separate_channels");
    connect(separate_channels, &QAction::triggered, this, &MainWindow::slotSeparateAudioChannel);
    timelineHeadersMenu->addAction(separate_channels);

    QAction *normalize_channels = new QAction(QIcon(), i18n("Normalize Audio Thumbnails"), this);
    normalize_channels->setCheckable(true);
    normalize_channels->setChecked(KdenliveSettings::normalizechannels());
    normalize_channels->setData("normalize_channels");
    connect(normalize_channels, &QAction::triggered, this, &MainWindow::slotNormalizeAudioChannel);
    timelineHeadersMenu->addAction(normalize_channels);

    // KoraCut : la baisse automatique de la musique, aussi depuis l'en-tête d'une piste audio.
    // L'action elle-même est créée plus tard (setupActions) : on la cherche au moment du clic.
    auto *duckingFromHeader = new QAction(QIcon::fromTheme(QStringLiteral("audio-volume-low")), i18nc("@action", "Automatic Music Ducking…"), this);
    duckingFromHeader->setData("koracut_auto_ducking");
    connect(duckingFromHeader, &QAction::triggered, this, [this]() {
        if (QAction *ducking = actionCollection()->action(QStringLiteral("koracut_auto_ducking"))) {
            ducking->trigger();
        }
    });
    timelineHeadersMenu->addAction(duckingFromHeader);

    // Identify Gaps submenu
    QMenu *identifyGapsMenu = new QMenu(i18n("Identify Gaps"), this);
    QAction *gapsAllTracks = new QAction(i18n("All Tracks"), this);
    connect(gapsAllTracks, &QAction::triggered, this, &MainWindow::slotAddMarkersAtGaps);
    identifyGapsMenu->addAction(gapsAllTracks);
    QAction *gapsSelectedTrack = new QAction(i18n("Selected Track"), this);
    connect(gapsSelectedTrack, &QAction::triggered, this, &MainWindow::slotAddMarkersAtGapsOnTrack);
    identifyGapsMenu->addAction(gapsSelectedTrack);
    timelineHeadersMenu->addMenu(identifyGapsMenu);

    // KoraCut : trois états, dans l'ordre d'usage. La bande complète est le défaut ; « début et
    // fin » reste là parce que c'est le mode utile sur une machine modeste, c'est le seul endroit
    // où il a un sens. Le quatrième état hérité, « image de début » seule, a été retiré du menu :
    // il reste lu dans les projets qui l'utilisent.
    QMenu *thumbsMenu = new QMenu(i18n("Thumbnails"), this);
    auto *thumbGroup = new QActionGroup(this);
    QAction *allFrame = new QAction(i18n("Full Strip"), thumbGroup);
    allFrame->setData(QStringLiteral("1"));
    allFrame->setCheckable(true);
    thumbsMenu->addAction(allFrame);
    QAction *inOutFrame = new QAction(i18n("Start and End"), thumbGroup);
    inOutFrame->setData(QStringLiteral("0"));
    inOutFrame->setCheckable(true);
    thumbsMenu->addAction(inOutFrame);
    QAction *noFrame = new QAction(i18n("No Thumbnails"), thumbGroup);
    noFrame->setData(QStringLiteral("3"));
    noFrame->setCheckable(true);
    thumbsMenu->addAction(noFrame);
    for (auto &bin : m_binWidgets) {
        bin->setupGeneratorMenu();
    }

    connect(pCore->monitorManager(), &MonitorManager::updateOverlayInfos, this, &MainWindow::slotUpdateMonitorOverlays);

    // Setup and fill effects and transitions menus.
    connect(m_effectsMenu, &QMenu::triggered, this, &MainWindow::slotAddEffect);
    // KoraCut : plus de branchement ici. Chaque action de composition est branchée à son entrée dans la
    // collection (constructeur) ; passer aussi par QMenu::triggered poserait la composition deux fois.

    m_timelineContextMenu = new QMenu(this);

    m_timelineContextMenu->addAction(actionCollection()->action(QStringLiteral("insert_space")));
    m_timelineContextMenu->addAction(actionCollection()->action(QStringLiteral("delete_space")));
    m_timelineContextMenu->addAction(actionCollection()->action(QStringLiteral("delete_space_all_tracks")));
    m_timelineContextMenu->addAction(actionCollection()->action(KStandardAction::name(KStandardAction::Paste)));
    slotConnectMonitors();

    m_timelineToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // KoraCut : la coupe automatique garde son libellé à côté de l'icône, pour qu'on la voie.
    if (QAction *autoCutAction = actionCollection()->action(QStringLiteral("koracut_auto_cut"))) {
        if (auto *button = qobject_cast<QToolButton *>(m_timelineToolBar->widgetForAction(autoCutAction))) {
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setObjectName(QStringLiteral("koracutAutoCutButton"));
        }
    }
    // KoraCut : les cinq outils exclusifs forment un contrôle segmenté, un seul corps visuel.
    // Apple, Segmented controls : un choix exclusif doit se lire comme un seul objet, pas comme
    // cinq boutons dont un est allumé. L'espacement de la barre tombe à zéro, les segments se
    // touchent, et les autres boutons retrouvent leurs 4 px par une marge de feuille de style.
    setupToolSegments();
    // TODO: let user select timeline toolbar toolbutton style
    // connect(toolBar(), &QToolBar::iconSizeChanged, m_timelineToolBar, &QToolBar::setToolButtonStyle);
    m_timelineToolBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_timelineToolBar, &QWidget::customContextMenuRequested, this, &MainWindow::showTimelineToolbarMenu);

    // KoraCut (24 septembre 2026) : le menu du bouton Rendu, en quatre gestes et deux réglages.
    QAction *prevRender = actionCollection()->action(QStringLiteral("prerender_timeline_zone"));
    QAction *stopPrevRender = actionCollection()->action(QStringLiteral("stop_prerender_timeline"));
    tlMenu->addAction(prevRender);
    tlMenu->addAction(actionCollection()->action(QStringLiteral("koracut_render_selection")));
    tlMenu->addSeparator();
    tlMenu->addAction(actionCollection()->action(QStringLiteral("koracut_render_auto")));
    tlMenu->addAction(actionCollection()->action(QStringLiteral("disable_preview")));
    tlMenu->addSeparator();
    tlMenu->addAction(stopPrevRender);
    tlMenu->addAction(actionCollection()->action(QStringLiteral("clear_render_timeline_zone")));
    tlMenu->addAction(actionCollection()->action(QStringLiteral("manage_cache")));
    timelinePreview->setActions(prevRender, stopPrevRender);
    // La barre passe ses boutons en icône seule : le bouton Rendu garde son libellé, qui dit l'état.
    timelinePreview->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_timelineToolBar, &QToolBar::toolButtonStyleChanged, timelinePreview, [timelinePreview]() {
        timelinePreview->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    });
    // Le bouton se range juste après la coupe automatique (le fichier .rc le met plus loin).
    if (QAction *autoCutAction = actionCollection()->action(QStringLiteral("koracut_auto_cut"))) {
        const QList<QAction *> toolbarActions = m_timelineToolBar->actions();
        QAction *renderButtonAction = actionCollection()->action(QStringLiteral("timeline_preview_button"));
        const int cutIndex = toolbarActions.indexOf(autoCutAction);
        if (renderButtonAction && cutIndex >= 0 && toolbarActions.contains(renderButtonAction)) {
            m_timelineToolBar->removeAction(renderButtonAction);
            const QList<QAction *> remaining = m_timelineToolBar->actions();
            const int at = remaining.indexOf(autoCutAction) + 1;
            m_timelineToolBar->insertAction(at < remaining.size() ? remaining.at(at) : nullptr, renderButtonAction);
        }
    }
    // Chantier 10 : les défauts selon la machine (proxys, aperçu, cache, décodage matériel).
    KoraCutPerformance::applyFirstLaunchDefaults(firstRun);
    KoraCutRender::applyFirstLaunchDefaults();
    if (QAction *autoAction = actionCollection()->action(QStringLiteral("koracut_render_auto"))) {
        autoAction->setChecked(KdenliveSettings::autopreview());
    }

    QAction *showRender = actionCollection()->action(QStringLiteral("project_render"));
    tlrMenu->addAction(showRender);
    tlrMenu->addAction(actionCollection()->action(QStringLiteral("stop_project_render")));
    timelineRender->defineDefaultAction(showRender, showRender);
    timelineRender->setAutoRaise(true);

    // Populate encoding profiles
    KConfig conf(QStringLiteral("encodingprofiles.rc"), KConfig::CascadeConfig, QStandardPaths::AppDataLocation);
    if (KdenliveSettings::grab_parameters().isEmpty() || KdenliveSettings::grab_extension().isEmpty()) {
        KConfigGroup group(&conf, "screengrab");
        QMap<QString, QString> values = group.entryMap();
        QMapIterator<QString, QString> i(values);
        if (i.hasNext()) {
            i.next();
            QString grabstring = i.value();
            KdenliveSettings::setGrab_parameters(grabstring.section(QLatin1Char(';'), 0, 0));
            KdenliveSettings::setGrab_extension(grabstring.section(QLatin1Char(';'), 1, 1));
        }
    }
    if (KdenliveSettings::decklink_parameters().isEmpty() || KdenliveSettings::decklink_extension().isEmpty()) {
        KConfigGroup group(&conf, "decklink");
        QMap<QString, QString> values = group.entryMap();
        QMapIterator<QString, QString> i(values);
        if (i.hasNext()) {
            i.next();
            QString decklinkstring = i.value();
            KdenliveSettings::setDecklink_parameters(decklinkstring.section(QLatin1Char(';'), 0, 0));
            KdenliveSettings::setDecklink_extension(decklinkstring.section(QLatin1Char(';'), 1, 1));
        }
    }
    if (!QDir(KdenliveSettings::currenttmpfolder()).isReadable())
        KdenliveSettings::setCurrenttmpfolder(QStandardPaths::writableLocation(QStandardPaths::TempLocation));

#ifdef USE_JOGSHUTTLE
    new JogManager(this);
#endif
    m_timelineTabs->setTimelineMenu(compositionMenu, timelineMenu, guideMenu, timelineRulerMenu, actionCollection()->action(QStringLiteral("edit_clip_marker")),
                                    timelineHeadersMenu, thumbsMenu, timelineSubtitleMenu, m_binWidgets.first()->addClipMenu());
    m_scopesManager->slotCheckActiveScopes();
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [&](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && getCurrentTimeline()) {
            getCurrentTimeline()->regainFocus();
        }
    });
    connect(this, &MainWindow::removeBinDock, this, &MainWindow::slotRemoveBinDock);
    // m_messageLabel->setMessage(QStringLiteral("This is a beta version. Always backup your data"), MltError);

    QAction *const showMenuBarAction = actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar));
    // FIXME: workaround for BUG 171080
    showMenuBarAction->setChecked(!menuBar()->isHidden());

    m_hamburgerMenu = KStandardAction::hamburgerMenu(nullptr, nullptr, actionCollection());
    // after the QMenuBar has been initialised
    m_hamburgerMenu->setMenuBar(menuBar());
    m_hamburgerMenu->setShowMenuBarAction(showMenuBarAction);
    // KoraCut : le chevron vit dans la barre du haut. Sur Mac la barre de menus reste native,
    // le chevron y est donc inutile et la méthode ne fait rien.
    m_topBar->setMainMenuAction(m_hamburgerMenu);

    // Detect shortcut conflicts bewtween mainwindow and media browser
    pCore->mediaBrowser()->detectShortcutConflicts();

    connect(toolBar(), &KToolBar::visibilityChanged, this, [&](bool visible) {
        if (visible && !toolBar()->actions().contains(m_hamburgerMenu)) {
            // hack to be able to insert the hamburger menu at the first position
            QAction *const firstChild = toolBar()->actionAt(toolBar()->height() / 2, toolBar()->height() / 2);
            QAction *const separator = toolBar()->insertSeparator(firstChild);
            toolBar()->insertAction(separator, m_hamburgerMenu);
            m_hamburgerMenu->hideActionsOf(toolBar());
        }
    });

    m_loadingDialog = new QProgressDialog(this);
    m_loadingDialog->setWindowFlags((m_loadingDialog->windowFlags() | Qt::CustomizeWindowHint) & ~Qt::WindowCloseButtonHint & ~Qt::WindowSystemMenuHint);
    m_loadingDialog->setMinimumDuration(0);
    m_loadingDialog->setMaximum(0);
    m_loadingDialog->setWindowTitle(i18nc("@title:window", "Loading Project"));
    m_loadingDialog->setCancelButton(nullptr);
    m_loadingDialog->setAutoClose(false);
    m_loadingDialog->setAutoReset(false);
    m_loadingDialog->setModal(true);
    m_loadingDialog->close();
    if (!KdenliveSettings::showtitlebars()) {
        Q_EMIT pCore->hideBars(true);
    }

    // fix for Bug 376053: intercept "Configure Toolbars" to prevent shortcut reset
    QAction *tbAction = actionCollection()->action(KStandardAction::name(KStandardAction::ConfigureToolbars));
    if (tbAction) {
        // disconnect EVERYTHING currently connected to it
        disconnect(tbAction, nullptr, nullptr, nullptr);

        // connect to a custom slot
        connect(tbAction, &QAction::triggered, this, &MainWindow::slotEditToolbars);
    }

}

void MainWindow::koraCutRelay()
{
    // Relais de version : KORACUT_RELAIS=<dossier>. Quand le fichier <dossier>/demande apparaît,
    // KoraCut enregistre le projet ouvert, écrit son chemin dans <dossier>/projet, puis quitte
    // proprement. Le script ~/CraftRoot/koracut-amadou.sh s'en sert pour remplacer l'instance
    // d'Amadou Ba par la dernière version compilée, sur le même projet, sans rien perdre.
    const QString dir = qEnvironmentVariable("KORACUT_RELAIS");
    if (dir.isEmpty()) {
        return;
    }
    QDir().mkpath(dir);
    QFile::remove(dir + QStringLiteral("/demande"));
    // Le bandeau « Une nouvelle version est prête » (demande d'Amadou Ba du 23 septembre 2026 au
    // soir : c'est lui qui choisit le moment de la mise à jour, plus le script). Toutes les 30 s,
    // on compare la date du binaire installé à celle du binaire qu'on exécute ; quand elle change,
    // un message dans le chutier propose « Mettre à jour » : koracut-amadou.sh --forcer fait alors
    // enregistrer le projet par ce relais, quitte et relance sur le même projet.
    const QString binary = QCoreApplication::applicationFilePath();
    const QDateTime started = QFileInfo(binary).lastModified();
    auto *versionTimer = new QTimer(this);
    versionTimer->setInterval(30000);
    connect(versionTimer, &QTimer::timeout, this, [this, binary, started, versionTimer]() {
        const QDateTime now = QFileInfo(binary).lastModified();
        if (!now.isValid() || now <= started) {
            return;
        }
        versionTimer->stop();
        auto *update = new QAction(i18nc("@action:button", "Update Now"), this);
        connect(update, &QAction::triggered, this, []() {
            QProcess::startDetached(QDir::homePath() + QStringLiteral("/CraftRoot/koracut-amadou.sh"), {QStringLiteral("--forcer")});
        });
        pCore->displayBinMessage(i18nc("@info", "A new version of KoraCut is ready (%1). Your project will be saved and reopened.",
                                       QLocale().toString(now, QLocale::ShortFormat)),
                                 KMessageWidget::Information, {update}, true);
    });
    versionTimer->start();
    auto *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this, dir, timer]() {
        if (!QFile::exists(dir + QStringLiteral("/demande"))) {
            return;
        }
        timer->stop();
        QFile::remove(dir + QStringLiteral("/demande"));
        QString saved;
        KdenliveDoc *doc = pCore->currentDoc();
        if (doc && !doc->url().isEmpty()) {
            if (!doc->isModified() || pCore->projectManager()->saveFile()) {
                saved = doc->url().toLocalFile();
            }
        } else if (doc && doc->isModified()) {
            // Projet jamais enregistré mais qui contient du travail : on lui donne un nom daté
            // dans Vidéos/KoraCut plutôt que d'ouvrir une fenêtre à laquelle personne ne répondra.
            const QString folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/KoraCut");
            QDir().mkpath(folder);
            const QString file =
                folder + QStringLiteral("/Projet ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH'h'mm")) + QStringLiteral(".koracut");
            if (pCore->projectManager()->saveFileAs(file)) {
                saved = file;
            }
        }
        QFile out(dir + QStringLiteral("/projet"));
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(saved.toUtf8());
            out.close();
        }
        close();
    });
    timer->start();
}

void MainWindow::koraCutDevScreenshot()
{
    // KORACUT_DUMP_ACTIONS=1 : liste les actions KoraCut et leurs raccourcis, tels que Qt les écrit.
    // Sert à contrôler les raccourcis de modules sans avoir à cliquer dans l'application.
    // KORACUT_DUMP_ACTIONS=all : toutes les actions, pour contrôler un jeu de raccourcis entier
    // (ajouté le 21 septembre 2026 avec les schémas de clavier).
    if (qEnvironmentVariableIsSet("KORACUT_DUMP_ACTIONS")) {
        const bool dumpAll = qEnvironmentVariable("KORACUT_DUMP_ACTIONS") == QLatin1String("all");
        const QList<QAction *> actions = actionCollection()->actions();
        for (QAction *a : actions) {
            if (!dumpAll && !a->objectName().startsWith(QLatin1String("koracut_")) && !a->objectName().startsWith(QLatin1String("project_render"))) {
                continue;
            }
            QStringList keys;
            const QList<QKeySequence> shortcuts = a->shortcuts();
            for (const QKeySequence &k : shortcuts) {
                keys << k.toString(QKeySequence::PortableText);
            }
            qWarning().noquote() << "KORACUT-ACTION" << a->objectName() << "|" << a->text() << "|" << keys.join(QLatin1String(" , "));
        }
    }

    // Outil de développement : KORACUT_SHOT=/chemin/capture.png enregistre la fenêtre principale
    // puis quitte. Sert à se relire quand l'écran du Mac est verrouillé et que screencapture ne
    // peut rien attraper. KORACUT_SHOT_DELAY (ms) et KORACUT_SHOT_SIZE (« 1366x768 ») sont optionnels.
    const QString path = qEnvironmentVariable("KORACUT_SHOT");
    if (path.isEmpty()) {
        return;
    }
    const QString size = qEnvironmentVariable("KORACUT_SHOT_SIZE");
    if (size.contains(QLatin1Char('x'))) {
        resize(size.section(QLatin1Char('x'), 0, 0).toInt(), size.section(QLatin1Char('x'), 1, 1).toInt());
    }
    const int delay = qEnvironmentVariableIsSet("KORACUT_SHOT_DELAY") ? qEnvironmentVariableIntValue("KORACUT_SHOT_DELAY") : 4000;
    QTimer::singleShot(delay, this, [this, path]() {
        // KORACUT_SHOT_EXTENSION=/chemin/effet.xml : simule l'installation d'un effet par la fenêtre
        // Extensions (copie dans le dossier des effets personnels, comme KNewStuff), puis le rechargement
        // sans relancer. Le journal dit si l'effet est entré dans le menu des effets. Le fichier copié
        // est retiré ensuite. 23 septembre 2026.
        const QString extension = qEnvironmentVariable("KORACUT_SHOT_EXTENSION");
        if (!extension.isEmpty()) {
            const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/effects/");
            QDir().mkpath(folder);
            const QString target = folder + QFileInfo(extension).fileName();
            QFile::remove(target);
            QFile::copy(extension, target);
            const int before = m_effectActions->actions().count();
            KNSCore::Entry entry;
            entry.setInstalledFiles({target});
            entry.setStatus(KNSCore::Entry::Status::Installed);
            koraCutReloadExtensions(QStringLiteral(":data/koracut_effects.knsrc"), {entry});
            QStringList names;
            for (QAction *a : m_effectActions->actions()) {
                names << a->text();
            }
            qWarning().noquote() << "KORACUT-EXTENSION actions avant" << before << "après" << m_effectActions->actions().count()
                                 << "présent" << names.contains(QStringLiteral("Essai extension KoraCut"));
            QFile::remove(target);
        }
        // Une capture par page des Préférences : KORACUT_SHOT_DIALOGS=prefs
        // Sur macOS, la barre de menus native ne laisse pas déclencher « Préférences… » par
        // l'accessibilité (essayé le 20/09/2026, AXPress répond « ok » mais rien ne s'ouvre).
        // C'est le seul moyen de relire les douze pages de réglages après un changement d'icônes.
        // KORACUT_SHOT_IMPORT=<fichiers> : importe et pose des fichiers dans la timeline (voir plus bas).
        auto importTestFiles = []() {
                // Un fichier précédé de « V: » va sur une piste vidéo (une caméra), les autres sur les pistes
                // audio (les micros). Les deux séries dans l'ordre des pistes.
                const QStringList files = qEnvironmentVariable("KORACUT_SHOT_IMPORT").split(QLatin1Char('|'), Qt::SkipEmptyParts);
                int audioIndex = 0;
                int videoIndex = 0;
                for (QString file : files) {
                    const bool video = file.startsWith(QLatin1String("V:"));
                    // « B: » : dans le chutier seulement, sans le poser (les agents le poseront eux-mêmes).
                    const bool binOnly = file.startsWith(QLatin1String("B:"));
                    if (video || binOnly) {
                        file = file.mid(2);
                    }
                    const int index = binOnly ? -1 : (video ? videoIndex++ : audioIndex++);
                    Fun undo = []() { return true; };
                    Fun redo = []() { return true; };
                    ClipCreator::createClipFromFile(file, pCore->projectItemModel()->getRootFolder()->clipId(), pCore->projectItemModel(), undo, redo,
                                                    [index, video](const QString &binId) {
                                                        if (index < 0) {
                                                            return;
                                                        }
                                                        auto timeline = pCore->projectManager()->getTimeline();
                                                        QList<int> tracks = timeline->getTracksIds(!video);
                                                        // Dans l'ordre de l'écran : V1, V2… et A1, A2…
                                                        std::sort(tracks.begin(), tracks.end(), [&timeline, video](int a, int b) {
                                                            return video ? timeline->getTrackPosition(a) < timeline->getTrackPosition(b)
                                                                         : timeline->getTrackPosition(a) > timeline->getTrackPosition(b);
                                                        });
                                                        int cid = -1;
                                                        if (index < tracks.size()) {
                                                            timeline->requestClipInsertion(binId, tracks.at(index), 0, cid, true, true, false);
                                                        }
                                                        qWarning() << "KoraCut: essai, clip posé" << binId << cid;
                                                    });
                }
        };
        // KORACUT_SHOT_DIALOGS=autocut-run avec KORACUT_SHOT_IMPORT=<fichier son> : essai de bout en bout
        // de la coupe automatique. Importe le fichier, le pose sur la première piste audio, attend
        // ses niveaux, clique « Analyser » (marquer pour relire), attend la transcription, écrit les
        // marques posées (lignes KORACUT-MARQUE) et capture la fenêtre et la timeline.
        if (qEnvironmentVariable("KORACUT_SHOT_DIALOGS") == QLatin1String("autocut-run")) {
            // Plusieurs fichiers séparés par « | » : le premier sur la première piste audio, le suivant
            // sur la deuxième, et ainsi de suite (essai de la synchronisation).
            importTestFiles();
            QTimer::singleShot(8000, this, [this, path]() {
                auto *dialog = new KoraCutAutoCutDialog(this);
                dialog->show();
                const QList<QPushButton *> buttons = dialog->findChildren<QPushButton *>();
                for (QPushButton *b : buttons) {
                    if (b->text().remove(QLatin1Char('&')) == i18nc("@action:button", "Analyze")) {
                        b->click();
                    }
                }
                auto *poll = new QTimer(this);
                auto *elapsed = new QElapsedTimer();
                elapsed->start();
                connect(poll, &QTimer::timeout, this, [this, dialog, poll, elapsed, path]() {
                    auto *transcriber = dialog->findChild<KoraCutTranscriber *>();
                    if (transcriber && transcriber->isRunning() && elapsed->elapsed() < 600000) {
                        return;
                    }
                    poll->stop();
                    const QList<QLabel *> labels = dialog->findChildren<QLabel *>();
                    for (QLabel *l : labels) {
                        qWarning().noquote() << "KORACUT-TEXTE" << l->text();
                    }
                    auto essai = pCore->projectManager()->getTimeline();
                    for (int tid : essai->getTracksIds(true)) {
                        for (int cid : essai->getItemsInRange(tid, 0, -1, false)) {
                            qWarning().noquote() << "KORACUT-CLIP" << essai->getTrackTagById(tid) << essai->getClipPosition(cid) << essai->getClipPlaytime(cid)
                                                 << pCore->projectItemModel()->getClipByBinID(essai->getClipBinId(cid))->name();
                        }
                    }
                    for (int tid : essai->getTracksIds(false)) {
                        for (int cid : essai->getItemsInRange(tid, 0, -1, false)) {
                            qWarning().noquote() << "KORACUT-CAMERA" << essai->getTrackTagById(tid) << pCore->projectItemModel()->getClipByBinID(essai->getClipBinId(cid))->name()
                                                 << essai->getClipPosition(cid) << essai->getClipPlaytime(cid)
                                                 << (essai->getClipState(cid).first == PlaylistState::Disabled ? "désactivé" : "actif");
                        }
                    }
                    const auto marks = pCore->projectManager()->getTimeline()->getGuideModel()->getAllMarkers();
                    const double fps = pCore->getCurrentFps();
                    for (const CommentedTime &m : marks) {
                        qWarning().noquote() << "KORACUT-MARQUE" << m.time().frames(fps) << m.duration().frames(fps) << m.markerType() << m.comment();
                    }
                    // KORACUT_SHOT_AUTOCUT_CUT=1 : on va jusqu'à « Couper ce qui est marqué ».
                    if (qEnvironmentVariableIsSet("KORACUT_SHOT_AUTOCUT_CUT")) {
                        qWarning() << "KORACUT-DUREE avant" << pCore->projectManager()->getTimeline()->duration();
                        for (QPushButton *b : dialog->findChildren<QPushButton *>()) {
                            if (b->text().remove(QLatin1Char('&')) == i18nc("@action:button", "Cut What Is Marked")) {
                                b->click();
                            }
                        }
                        qWarning() << "KORACUT-DUREE après" << pCore->projectManager()->getTimeline()->duration()
                                   << "marques restantes" << pCore->projectManager()->getTimeline()->getGuideModel()->getAllMarkers().size();
                    }
                    QString dialogShot = path;
                    dialogShot.replace(QLatin1String(".png"), QLatin1String("-autocut.png"));
                    dialog->grab().save(dialogShot);
                    grab().save(path);
                    // Projet d'essai : on ne demande pas s'il faut l'enregistrer, sinon l'essai reste
                    // bloqué sur la question.
                    if (pCore->currentDoc()) {
                        pCore->currentDoc()->setModified(false);
                    }
                    QCoreApplication::quit();
                });
                poll->start(1000);
            });
            return;
        }
        // KORACUT_SHOT_DIALOGS=ducking : la fenêtre de baisse automatique de la musique.
        if (qEnvironmentVariable("KORACUT_SHOT_DIALOGS") == QLatin1String("ducking") || qEnvironmentVariable("KORACUT_SHOT_DIALOGS") == QLatin1String("extensions") ||
            qEnvironmentVariable("KORACUT_SHOT_DIALOGS") == QLatin1String("autocut") || qEnvironmentVariable("KORACUT_SHOT_DIALOGS") == QLatin1String("soutien")) {
            const bool extensions = qEnvironmentVariable("KORACUT_SHOT_DIALOGS") == QLatin1String("extensions");
            QDialog *dialog = extensions ? static_cast<QDialog *>(new KoraCutExtensionsDialog(this)) : static_cast<QDialog *>(new KoraCutDuckingDialog(this));
            if (qEnvironmentVariable("KORACUT_SHOT_DIALOGS") == QLatin1String("autocut")) {
                delete dialog;
                dialog = new KoraCutAutoCutDialog(this);
            }
            if (qEnvironmentVariable("KORACUT_SHOT_DIALOGS") == QLatin1String("soutien")) {
                delete dialog;
                dialog = new KoraCutSupportDialog(this);
            }
            dialog->show();
            // KORACUT_SHOT_CLICK=<libellé> : clique ce bouton de la fenêtre ; KORACUT_SHOT_WAIT (ms) :
            // attend avant la capture (un téléchargement, une analyse), et écrit les textes affichés.
            const QString click = qEnvironmentVariable("KORACUT_SHOT_CLICK");
            if (!click.isEmpty()) {
                for (QPushButton *b : dialog->findChildren<QPushButton *>()) {
                    if (b->text().remove(QLatin1Char('&')) == click) {
                        b->click();
                    }
                }
            }
            const int wait = qEnvironmentVariableIsSet("KORACUT_SHOT_WAIT") ? qEnvironmentVariableIntValue("KORACUT_SHOT_WAIT") : 1200;
            QTimer::singleShot(wait, this, [dialog, path]() {
                for (QLabel *l : dialog->findChildren<QLabel *>()) {
                    qWarning().noquote() << "KORACUT-TEXTE" << l->text();
                }
                QString target = path;
                target.replace(QLatin1String(".png"), QStringLiteral("-%1.png").arg(qEnvironmentVariable("KORACUT_SHOT_DIALOGS")));
                dialog->grab().save(target);
                QCoreApplication::quit();
            });
            return;
        }
#if KORACUT_AGENTS
        // KORACUT_SHOT_DIALOGS=connexions-ia : la page Connexions IA des Préférences ; avec
        // KORACUT_SHOT_AI=<fournisseur ou connexion>, ce fournisseur choisi (-connexions-ia.png).
        if (qEnvironmentVariable("KORACUT_SHOT_DIALOGS") == QLatin1String("connexions-ia")) {
            KoraCutAiConnectionsPage::openInPreferences(qEnvironmentVariable("KORACUT_SHOT_AI"));
            QTimer::singleShot(qEnvironmentVariableIsSet("KORACUT_SHOT_WAIT") ? qEnvironmentVariableIntValue("KORACUT_SHOT_WAIT") : 1500, this, [path]() {
                if (QWidget *dialog = KConfigDialog::exists(QStringLiteral("settings"))) {
                    QString target = path;
                    target.replace(QLatin1String(".png"), QStringLiteral("-connexions-ia.png"));
                    dialog->grab().save(target);
                    for (QLabel *l : dialog->findChildren<QLabel *>()) {
                        if (l->isVisible() && !l->text().isEmpty()) {
                            qWarning().noquote() << "KORACUT-TEXTE" << l->text();
                        }
                    }
                }
                QCoreApplication::quit();
            });
            return;
        }
#endif
        // KORACUT_SHOT_DIALOGS=prefs : une capture par section des Préférences (-prefs-<section>.png) ;
        // prefs:apparence,timeline pour quelques sections ; KORACUT_SHOT_PREFS_SEARCH=<texte> filtre
        // d'abord les réglages. Écrit aussi ce que la porte de commande voit de la fenêtre (KORACUT-PREFS).
        const QString shotDialogs = qEnvironmentVariable("KORACUT_SHOT_DIALOGS");
        if (shotDialogs == QLatin1String("prefs") || shotDialogs.startsWith(QLatin1String("prefs:"))) {
            slotShowPreferencePage(Kdenlive::NoPage, 0);
            auto *prefs = qobject_cast<KdenliveSettingsDialog *>(KConfigDialog::exists(QStringLiteral("settings")));
            if (!prefs) {
                QCoreApplication::quit();
                return;
            }
            const QStringList ids = shotDialogs == QLatin1String("prefs") ? prefs->koraCutSections() : shotDialogs.mid(6).split(QLatin1Char(','), Qt::SkipEmptyParts);
            const QString search = qEnvironmentVariable("KORACUT_SHOT_PREFS_SEARCH");
            int step = 800;
            for (const QString &id : ids) {
                QTimer::singleShot(step, this, [prefs, id, search]() {
                    if (!search.isEmpty()) {
                        qWarning().noquote() << "KORACUT-PREFS recherche" << search << "trouvés" << prefs->koraCutSearch(search);
                    }
                    if (!prefs->koraCutShowSection(id)) {
                        qWarning().noquote() << "KORACUT-PREFS section inconnue" << id;
                    }
                });
                step += 900;
                QTimer::singleShot(step, this, [prefs, id, path]() {
                    QString target = path;
                    target.replace(QLatin1String(".png"), QStringLiteral("-prefs-%1.png").arg(id));
                    prefs->grab().save(target);
                    qWarning().noquote() << "KORACUT-PREFS" << id << "section" << prefs->koraCutCurrentSection() << "capture" << target;
                    qWarning().noquote() << "KORACUT-PREFS-FENETRE"
                                         << QString::fromUtf8(QJsonDocument(KoraCutPorte::describeWindow(prefs)).toJson(QJsonDocument::Compact));
                });
                step += 300;
            }
            QTimer::singleShot(step + 400, qApp, &QCoreApplication::quit);
            return;
        }
        // KORACUT_SHOT_ACTIONS=collapse_all_tracks,undo : déclenche ces actions l'une après l'autre
        // (1,5 s d'écart), puis capture. Sert à contrôler un geste sans souris ni écran déverrouillé.
        const QStringList actionsToRun = qEnvironmentVariable("KORACUT_SHOT_ACTIONS").split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (!actionsToRun.isEmpty()) {
            // Avec KORACUT_SHOT_IMPORT, les fichiers d'abord, et le temps de calculer leurs niveaux.
            int step = 0;
            if (qEnvironmentVariableIsSet("KORACUT_SHOT_IMPORT") && !actionsToRun.contains(QLatin1String("importer"))) {
                importTestFiles();
                step = 8000;
            }
            for (const QString &name : actionsToRun) {
                // « attendre:N » : N ms de plus avant l'action suivante (un rendu, une analyse).
                if (name.startsWith(QLatin1String("attendre:"))) {
                    step += name.mid(9).toInt();
                    continue;
                }
                QTimer::singleShot(step, this, [this, name, importTestFiles]() {
                    // « importer » : importe KORACUT_SHOT_IMPORT maintenant (après un file_new, par exemple).
                    if (name == QLatin1String("importer")) {
                        importTestFiles();
                        return;
                    }
                    // « type:texte » : comme si on tapait ce texte dans le champ Rechercher de la barre du haut.
                    // « onglet:N » : passe la fenêtre secondaire ouverte (Rendu, Préférences…) sur son onglet N.
                    if (name.startsWith(QLatin1String("onglet:"))) {
                        for (QWidget *w : QApplication::topLevelWidgets()) {
                            if (w != this && w->isVisible()) {
                                if (auto *tabs = w->findChild<QTabWidget *>()) {
                                    tabs->setCurrentIndex(name.mid(7).toInt());
                                }
                            }
                        }
                        return;
                    }
                    // « role:A2=music » : pose le rôle d'une piste audio, comme la baisse automatique de la musique.
                    if (name.startsWith(QLatin1String("role:"))) {
                        const QString tag = name.mid(5).section(QLatin1Char('='), 0, 0);
                        const QString role = name.section(QLatin1Char('='), 1);
                        auto timeline = pCore->projectManager()->getTimeline();
                        for (int tid : timeline->getTracksIds(true)) {
                            if (timeline->getTrackTagById(tid) == tag) {
                                timeline->setTrackProperty(tid, QStringLiteral("kdenlive:koracut_role"), role);
                                qWarning() << "KoraCut: rôle posé" << tag << role;
                            }
                        }
                        getCurrentTimeline()->controller()->refreshTrackColors();
                        return;
                    }
                    // « pistes » : écrit le profil du projet et chaque piste avec son nom (KORACUT-PISTE).
                    if (name == QLatin1String("pistes")) {
                        auto timeline = pCore->projectManager()->getTimeline();
                        const auto &profile = pCore->getCurrentProfile();
                        qWarning().noquote() << "KORACUT-PROFIL" << profile->width() << profile->height() << profile->fps()
                                             << (pCore->currentDoc() ? pCore->currentDoc()->getDocumentProperty(QStringLiteral("koracut:videotype")) : QString())
                                             << (pCore->currentDoc() ? pCore->currentDoc()->url().fileName() : QString());
                        for (bool audio : {false, true}) {
                            for (int tid : timeline->getTracksIds(audio)) {
                                qWarning().noquote() << "KORACUT-PISTE" << timeline->getTrackTagById(tid) << timeline->getTrackPosition(tid)
                                                     << timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString();
                            }
                        }
                        return;
                    }
                    // « apercu:mesure:N » lit N secondes depuis le début et écrit KORACUT-APERCU (images sautées) ;
                    // « apercu:rendre » rend toute la séquence (KORACUT-APERCU rendu_ms=… à la fin) ; « apercu:effacer »
                    // efface les rendus ; « apercu:etat » écrit l'état du rendu. Document 6, journal du 24 septembre 2026.
                    // « diagnostic:tester » lance l'essai du panneau Diagnostic (ligne KORACUT-DIAGNOSTIC à la fin) ;
                    // « diagnostic:etat » écrit ce que le panneau mesure. Chantier 10.
                    // « etalonnage:pause=oui|non » met l'étalonnage en pause dans l'aperçu (bouton du moniteur, ⌘⇧D) ;
                    // « etalonnage:etat » écrit KORACUT-ETALONNAGE (2 octobre 2026, document 6).
                    if (name.startsWith(QLatin1String("etalonnage:"))) {
                        const QString what = name.section(QLatin1Char(':'), 1);
                        if (what.startsWith(QLatin1String("pause="))) {
                            KoraCutPerformance::setGradePaused(what.section(QLatin1Char('='), 1) == QLatin1String("oui"));
                        }
                        QAction *a = actionCollection()->action(QStringLiteral("koracut_grade_pause"));
                        qWarning().noquote() << "KORACUT-ETALONNAGE pause=" + QString(KoraCutPerformance::gradePaused() ? QStringLiteral("oui") : QStringLiteral("non"))
                                             << "action=" + QString(a && a->isChecked() ? QStringLiteral("cochee") : QStringLiteral("decochee"))
                                             << "raccourci=" + (a ? a->shortcut().toString() : QString());
                        return;
                    }
                    if (name.startsWith(QLatin1String("diagnostic:")) && m_perfPanel) {
                        const auto printJson = [](const QJsonObject &o) {
                            qWarning().noquote() << "KORACUT-DIAGNOSTIC" << QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
                        };
                        if (name == QLatin1String("diagnostic:tester")) {
                            slotShowDiagnosticPanel();
                            connect(m_perfPanel, &KoraCutPerfPanel::testFinished, this, printJson, Qt::SingleShotConnection);
                            m_perfPanel->runTest();
                        } else {
                            printJson(KoraCutPerfPanel::snapshot());
                        }
                        return;
                    }
                    // « copies:etat » : les copies légères (document 6, journal du 4 octobre 2026), ligne KORACUT-COPIES.
                    // « copies:oui » et « copies:non » : comme l'interrupteur des Préférences.
                    if (name.startsWith(QLatin1String("copies:"))) {
                        const QString what = name.section(QLatin1Char(':'), 1, 1);
                        if (what == QLatin1String("oui") || what == QLatin1String("non")) {
                            KoraCutLightCopies::setEnabled(what == QLatin1String("oui"));
                        }
                        qWarning().noquote() << "KORACUT-COPIES" << QString::fromUtf8(QJsonDocument(KoraCutLightCopies::stateJson()).toJson(QJsonDocument::Compact));
                        return;
                    }
                    if (name.startsWith(QLatin1String("apercu:"))) {
                        const QString what = name.section(QLatin1Char(':'), 1, 1);
                        auto print = [](const QJsonObject &o) {
                            QStringList parts;
                            for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
                                const QJsonValue v = it.value();
                                QString text = v.toVariant().toString();
                                if (v.isObject()) {
                                    text = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
                                } else if (v.isArray()) {
                                    text = QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
                                }
                                parts << QStringLiteral("%1=%2").arg(it.key(), text);
                            }
                            qWarning().noquote() << "KORACUT-APERCU" << parts.join(QLatin1Char(' '));
                        };
                        if (what == QLatin1String("mesure")) {
                            const int seconds = std::max(1, name.section(QLatin1Char(':'), 2, 2).toInt());
                            // « apercu:mesure:N:D » : depuis la seconde D (banc d'essai du chantier 10).
                            const double from = name.section(QLatin1Char(':'), 3, 3).toDouble();
                            KoraCutRender::measurePlayback(int(from * pCore->getCurrentFps()), seconds, print);
                        } else if (what == QLatin1String("rendre") && getCurrentTimeline()) {
                            auto *clock = new QElapsedTimer;
                            clock->start();
                            getCurrentTimeline()->controller()->renderRange(QPoint(0, std::max(0, getCurrentTimeline()->model()->duration() - 1)));
                            auto *poll = new QTimer(this);
                            connect(poll, &QTimer::timeout, this, [poll, clock, print]() {
                                const QJsonObject state = KoraCutRender::stateJson();
                                if (!state.value(QStringLiteral("en_cours")).toBool() && state.value(QStringLiteral("a_rendre")).toArray().isEmpty()) {
                                    poll->stop();
                                    poll->deleteLater();
                                    print(QJsonObject{{QStringLiteral("rendu_ms"), clock->elapsed()}, {QStringLiteral("cache_mo"), state.value(QStringLiteral("cache")).toObject().value(QStringLiteral("mo"))}});
                                    delete clock;
                                }
                            });
                            poll->start(200);
                        } else if (what == QLatin1String("effacer") && getCurrentTimeline()) {
                            getCurrentTimeline()->controller()->clearRenders();
                        } else if (what == QLatin1String("etat")) {
                            print(KoraCutRender::stateJson());
                        } else if (what == QLatin1String("gestes")) {
                            // « apercu:gestes » : temps de réponse aux gestes (2 octobre 2026, document 6).
                            // « apercu:gestes:glisser:N » : seulement le glisser de la tête de lecture, N fois.
                            const bool dragOnly = name.section(QLatin1Char(':'), 2, 2) == QLatin1String("glisser");
                            KoraCutRender::measureGestures(
                                [](const QJsonObject &o) {
                                    qWarning().noquote() << "KORACUT-GESTES" << QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
                                },
                                dragOnly ? std::max(1, name.section(QLatin1Char(':'), 3, 3).toInt()) : 0);
                        }
                        return;
                    }
                    // « positions » : écrit la place de chaque clip (KORACUT-POSITION piste position durée).
                    if (name == QLatin1String("positions")) {
                        auto timeline = pCore->projectManager()->getTimeline();
                        for (bool audio : {false, true}) {
                            for (int tid : timeline->getTracksIds(audio)) {
                                for (int cid : timeline->getItemsInRange(tid, 0, -1, false)) {
                                    qWarning().noquote() << "KORACUT-POSITION" << timeline->getTrackTagById(tid) << timeline->getItemPosition(cid)
                                                         << timeline->getItemPlaytime(cid);
                                }
                            }
                        }
                        return;
                    }
                    // « export:capture » ou « export:rendre » : ouvre la fenêtre Export, écrit le préréglage
                    // choisi et le niveau sonore (KORACUT-EXPORT), capture la fenêtre (<capture>-export.png) et,
                    // avec « rendre », lance le rendu vers KORACUT_EXPORT_OUT puis attend qu'il finisse.
                    if (name.startsWith(QLatin1String("export:"))) {
                        slotRenderProject();
                        const bool rendre = name == QLatin1String("export:rendre");
                        QTimer::singleShot(1500, this, [this, rendre]() {
                            if (!m_renderWidget) {
                                qWarning() << "KORACUT-EXPORT pas de fenêtre";
                                return;
                            }
                            QString preset;
                            for (QLabel *l : m_renderWidget->findChildren<QLabel *>()) {
                                if (l->objectName() == QLatin1String("profile_name")) {
                                    preset = l->text();
                                }
                            }
                            QCheckBox *loud = nullptr;
                            QSpinBox *lufs = nullptr;
                            for (QCheckBox *c : m_renderWidget->findChildren<QCheckBox *>()) {
                                if (c->text() == i18nc("@option:check", "Level the sound at")) {
                                    loud = c;
                                }
                            }
                            for (QSpinBox *sp : m_renderWidget->findChildren<QSpinBox *>()) {
                                if (sp->suffix().contains(QLatin1String("LUFS"))) {
                                    lufs = sp;
                                }
                            }
                            qWarning().noquote() << "KORACUT-EXPORT préréglage" << m_renderWidget->currentPresetName() << "niveau"
                                                 << (loud ? (loud->isChecked() ? "coché" : "décoché") : "case introuvable") << (lufs ? lufs->value() : 0);
                            // « Plus d'options » déplié, pour voir la section Audio sur la capture.
                            for (QCheckBox *c : m_renderWidget->findChildren<QCheckBox *>()) {
                                if (c->objectName() == QLatin1String("options")) {
                                    c->setChecked(true);
                                }
                            }
                            QString shot = qEnvironmentVariable("KORACUT_SHOT");
                            shot.replace(QLatin1String(".png"), QLatin1String("-export.png"));
                            m_renderWidget->grab().save(shot);
                            if (rendre) {
                                const QString out = qEnvironmentVariable("KORACUT_EXPORT_OUT");
                                m_renderWidget->startRenderTo(out);
                                qWarning() << "KORACUT-EXPORT rendu lancé vers" << out;
                            }
                        });
                        return;
                    }
                    // « tailles » : écrit la largeur minimale de chaque panneau et des enfants qui l'imposent
                    // (KORACUT-MIN). C'est ce qui borne les dispositions sur un petit écran.
                    if (name == QLatin1String("barre-timeline")) {
                        // La barre d'outils de la timeline : sa largeur, celle de son conteneur, et le
                        // bouton « plus d'outils » de Qt (présent, visible, place).
                        QWidget *ext = m_timelineToolBar->findChild<QWidget *>(QStringLiteral("qt_toolbar_ext_button"));
                        qWarning().noquote() << "KORACUT-BARRE" << "barre" << m_timelineToolBar->geometry() << "min" << m_timelineToolBar->minimumSizeHint()
                                             << "conteneur" << m_timelineToolBarContainer->geometry() << "ext" << (ext ? ext->geometry() : QRect())
                                             << "ext_visible" << (ext && ext->isVisible());
                        if (auto *b = qobject_cast<QToolButton *>(ext)) {
                            qWarning().noquote() << "KORACUT-BARRE icone_nulle" << b->icon().isNull() << "tailles" << b->icon().availableSizes() << "iconSize" << b->iconSize()
                                                 << "style" << b->toolButtonStyle() << "feuille" << b->styleSheet();
                            b->grab().save(qEnvironmentVariable("KORACUT_SHOT").replace(QLatin1String(".png"), QLatin1String("-ext.png")));
                        }
                        return;
                    }
                    if (name == QLatin1String("tailles")) {
                        const auto docks = KDDockWidgets::DockRegistry::self()->dockwidgets();
                        for (KDDockWidgets::Core::DockWidget *d : docks) {
                            QWidget *w = KDDockWidgets::QtCommon::View_qt::asQWidget(d->guestView().get());
                            if (!w) {
                                continue;
                            }
                            qWarning().noquote() << "KORACUT-MIN" << d->uniqueName() << w->minimumSizeHint().width() << w->minimumSize().width();
                            for (QWidget *c : w->findChildren<QWidget *>()) {
                                const int m = qMax(c->minimumSizeHint().width(), c->minimumWidth());
                                if (m >= 200 && c->isVisibleTo(w)) {
                                    qWarning().noquote() << "KORACUT-MIN   " << c->metaObject()->className() << c->objectName() << m
                                                         << c->minimumSizeHint().width() << c->minimumWidth();
                                }
                            }
                        }
                        return;
                    }
                    // « disposition:compact » ou « disposition:large-vertical » : range les panneaux du module
                    // Montage pour un portable ou un moniteur, et écrit la disposition dans KORACUT_LAYOUT_OUT
                    // (c'est ainsi que sont produits data/layouts/montage*.json, 23 septembre 2026).
                    // Document 22 : le module Étalonnage (« etal:graphe-fichier=… », « etal:noeud=parallele »…).
                    if (name.startsWith(QLatin1String("etal:"))) {
                        KoraCutColorPage::shotAction(name.mid(5));
                        return;
                    }
                    // Document 23 : le module Export (« livraison:modele=tiktok », « livraison:exporter »…).
                    if (name.startsWith(QLatin1String("livraison:"))) {
                        KoraCutDeliverPage::shotAction(name.mid(10));
                        return;
                    }
                    // « theme:clair », « theme:sombre » : le thème de l'interface, pour capturer les deux.
                    if (name.startsWith(QLatin1String("theme:"))) {
                        QString why;
                        KoraCutThemes::self()->apply(name.mid(6), &why);
                        return;
                    }
                    if (name.startsWith(QLatin1String("disposition:"))) {
                        // « disposition:compact », « disposition:large-vertical » (Montage) ou
                        // « disposition:etalonnage », « disposition:audio-vertical »… (les autres modules).
                        const QString variant = name.mid(12);
                        const bool vertical = variant.endsWith(QLatin1String("-vertical"));
                        const bool montage = variant.startsWith(QLatin1String("compact")) || variant.startsWith(QLatin1String("large"));
                        const bool compact = variant.startsWith(QLatin1String("compact"));
                        const QString module = montage ? QStringLiteral("montage") : variant.section(QLatin1Char('-'), 0, 0);
                        KoraCutWorkspace::arrangeModule(this, module, vertical);
                        const QString out = qEnvironmentVariable("KORACUT_LAYOUT_OUT");
                        if (!out.isEmpty()) {
                            const KoraCutModules::Module *info = KoraCutModules::byId(module);
                            qWarning() << "KoraCut: disposition écrite" << out
                                       << KoraCutWorkspace::writeLayout(out, module, info ? info->name : module, vertical, montage && compact);
                        }
                        setSingleMonitor(KdenliveSettings::singleMonitor());
                        if (!KdenliveSettings::showtitlebars()) {
                            Q_EMIT pCore->hideBars(true);
                        }
                        return;
                    }
                    // « selection:video » : sélectionne le premier clip vidéo de la séquence, comme un clic.
                    if (name == QLatin1String("selection:video")) {
                        auto timeline = pCore->projectManager()->getTimeline();
                        for (int tid : timeline->getTracksIds(false)) {
                            for (int cid : timeline->getItemsInRange(tid, 0, -1, false)) {
                                if (timeline->isClip(cid)) {
                                    timeline->requestSetSelection({cid});
                                    // Comme le clic : l'inspecteur montre le plan (et le moniteur son cadre, document 20).
                                    if (getCurrentTimeline()) {
                                        getCurrentTimeline()->controller()->showAsset(cid);
                                    }
                                    qWarning() << "KoraCut: clip sélectionné" << cid;
                                    return;
                                }
                            }
                        }
                        return;
                    }
                    // « selection:audio » : sélectionne le premier clip audio de la séquence (document 24).
                    if (name == QLatin1String("selection:audio")) {
                        auto timeline = pCore->projectManager()->getTimeline();
                        for (int tid : timeline->getTracksIds(true)) {
                            for (int cid : timeline->getItemsInRange(tid, 0, -1, false)) {
                                if (timeline->isClip(cid)) {
                                    timeline->requestSetSelection({cid});
                                    if (getCurrentTimeline()) {
                                        getCurrentTimeline()->controller()->showAsset(cid);
                                    }
                                    qWarning() << "KoraCut: clip sélectionné" << cid;
                                    return;
                                }
                            }
                        }
                        return;
                    }
                    // Document 24, la section Outils sur le clip sélectionné : « outils:bruit=moyen »,
                    // « outils:voix=oui|non », « outils:effet=robot|aucun », « outils:stabiliser=fort|aucun »,
                    // « outils:etat » (ligne KORACUT-OUTILS : état des outils et de l'analyse).
                    if (name.startsWith(QLatin1String("outils:"))) {
                        auto timeline = pCore->projectManager()->getTimeline();
                        const auto selection = timeline->getCurrentSelection();
                        int cid = -1;
                        for (int id : selection) {
                            if (timeline->isClip(id)) {
                                cid = id;
                                break;
                            }
                        }
                        const QString verb = name.mid(7).section(QLatin1Char('='), 0, 0);
                        const QString value = name.section(QLatin1Char('='), 1);
                        QJsonObject result;
                        if (verb == QLatin1String("etat")) {
                            const int audio = KoraCutClipTools::audioTarget(timeline, cid);
                            auto *stab = KoraCutStabilizer::instance();
                            result = {{QStringLiteral("clip"), cid},
                                      {QStringLiteral("bruit"), audio < 0 ? QString() : KoraCutClipTools::current(timeline, audio, KoraCutClipTools::Tool::Denoise)},
                                      {QStringLiteral("voix"), audio < 0 ? QString() : KoraCutClipTools::current(timeline, audio, KoraCutClipTools::Tool::Enhance)},
                                      {QStringLiteral("effet"), audio < 0 ? QString() : KoraCutClipTools::current(timeline, audio, KoraCutClipTools::Tool::VoiceEffect)},
                                      {QStringLiteral("stabilise"), stab->currentLevel(timeline, cid)},
                                      {QStringLiteral("analyse"), stab->progress(timeline, cid)},
                                      {QStringLiteral("section_visible"), m_clipTools && m_clipTools->isVisible()}};
                        } else if (verb == QLatin1String("bruit")) {
                            result = KoraCutAgentTools::call(timeline, QStringLiteral("reduire_bruit"), {{QStringLiteral("clip_id"), cid}, {QStringLiteral("niveau"), value}});
                        } else if (verb == QLatin1String("voix")) {
                            result = KoraCutAgentTools::call(timeline, QStringLiteral("ameliorer_voix"), {{QStringLiteral("clip_id"), cid}, {QStringLiteral("actif"), value != QLatin1String("non")}});
                        } else if (verb == QLatin1String("effet")) {
                            result = KoraCutAgentTools::call(timeline, QStringLiteral("effet_voix"), {{QStringLiteral("clip_id"), cid}, {QStringLiteral("effet"), value}});
                        } else if (verb == QLatin1String("stabiliser")) {
                            result = KoraCutAgentTools::call(timeline, QStringLiteral("stabiliser"), {{QStringLiteral("clip_id"), cid}, {QStringLiteral("niveau"), value}});
                        }
                        qWarning().noquote() << "KORACUT-OUTILS" << QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
                        return;
                    }
                    // « bouton:libellé » : clique ce bouton dans une fenêtre secondaire ouverte.
                    if (name.startsWith(QLatin1String("bouton:"))) {
                        for (QWidget *w : QApplication::topLevelWidgets()) {
                            if (w != this && w->isVisible()) {
                                for (QAbstractButton *b : w->findChildren<QAbstractButton *>()) {
                                    if (b->isVisible() && b->text().remove(QLatin1Char('&')) == name.mid(7)) {
                                        qWarning() << "KoraCut: bouton cliqué" << name.mid(7);
                                        b->click();
                                        return;
                                    }
                                }
                            }
                        }
                        // Puis la fenêtre principale (onglets Vidéo, Audio, Fichier de la page Export), sans la barre des
                        // modules, dont les boutons portent les mêmes noms.
                        for (QAbstractButton *b : findChildren<QAbstractButton *>()) {
                            bool inTopBar = false;
                            for (QWidget *p = b->parentWidget(); p && !inTopBar; p = p->parentWidget()) {
                                inTopBar = p->inherits("KoraCutTopBar");
                            }
                            if (!inTopBar && b->isVisible() && b->text().remove(QLatin1Char('&')) == name.mid(7)) {
                                qWarning() << "KoraCut: bouton cliqué" << name.mid(7);
                                b->click();
                                return;
                            }
                        }
                        qWarning() << "KoraCut: bouton introuvable" << name.mid(7);
                        return;
                    }
#if KORACUT_AGENTS
                    // « agent:texte » : envoie une demande aux agents (sans virgule dans le texte).
                    // « agent-accord:oui|non » répond à la dernière demande d'accord, « agent-plan:appliquer|modifier|annuler »
                    // au dernier plan (« modifier:texte » joint la remarque), « agent-question:texte » à la dernière question,
                    // « agent-annuler » annule le tour ouvert.
                    if (name.startsWith(QLatin1String("agent:"))) {
                        m_agentSession->sendRequest(name.mid(6));
                        qWarning() << "KoraCut: demande aux agents" << name.mid(6);
                        return;
                    }
                    if (name.startsWith(QLatin1String("agent-saisie:"))) {
                        // Comme la personne : le texte dans le champ du panneau, puis Entrée (sans connexion, le choix s'affiche).
                        slotShowAgentsPanel();
                        m_agentPanel->debugType(name.mid(13));
                        return;
                    }
                    if (name.startsWith(QLatin1String("agent-accord:"))) {
                        m_agentPanel->answerPendingConfirmation(name.mid(13) == QLatin1String("oui"));
                        return;
                    }
                    if (name.startsWith(QLatin1String("agent-plan:"))) {
                        const QString decision = name.mid(11).section(QLatin1Char(':'), 0, 0);
                        const QString comment = name.mid(11).section(QLatin1Char(':'), 1);
                        m_agentPanel->decidePendingPlan(decision == QLatin1String("appliquer")  ? KoraCutAgentSession::PlanDecision::Apply
                                                        : decision == QLatin1String("modifier") ? KoraCutAgentSession::PlanDecision::Modify
                                                                                                : KoraCutAgentSession::PlanDecision::Cancel,
                                                        comment);
                        return;
                    }
                    if (name.startsWith(QLatin1String("agent-question:"))) {
                        // La dernière question posée est celle que le champ attend : on passe par le panneau.
                        m_agentPanel->focusInput();
                        m_agentSession->answerQuestion(QString(), name.mid(15));
                        return;
                    }
                    if (name == QLatin1String("agent-annuler")) {
                        m_agentSession->cancelTurn();
                        return;
                    }
#endif
                    // La Console (document 18) : « console:timeline etat niveau=resume » envoie la ligne comme si elle
                    // était tapée dans le champ (sans virgule), « console-accord:oui|non » répond à la demande d'accord
                    // la plus ancienne, « console-terminal » clique « Ouvrir un terminal ici ».
                    // Le script (document 21) : « script:modele », « script:ligne[=texte] », « script:colonne=Texte »,
                    // « script:bloc=tableau », « script:etat », « script:markdown=<fichier> », « script:csv=<fichier> ».
                    if (name.startsWith(QLatin1String("script:"))) {
                        if (auto *d = KDDockWidgets::DockRegistry::self()->dockByName(QStringLiteral("script"))) {
                            d->open();
                            d->setAsCurrentTab();
                        }
                        if (auto *panel = findChild<KoraCutScriptPanel *>()) {
                            qWarning().noquote() << "KORACUT-SCRIPT" << panel->debugCommand(name.mid(7));
                        }
                        return;
                    }
                    if (name.startsWith(QLatin1String("console:")) && m_consolePanel) {
                        slotShowConsolePanel();
                        m_consolePanel->runLine(name.mid(8));
                        qWarning() << "KoraCut: console" << name.mid(8);
                        return;
                    }
                    if (name.startsWith(QLatin1String("console-accord:")) && m_consolePanel) {
                        m_consolePanel->answerPendingConfirmation(name.mid(15) == QLatin1String("oui"));
                        return;
                    }
                    // Le terminal intégré (document 18, 25 septembre 2026) : « terminal:ouvrir », « terminal:choisir[=id] »,
                    // « terminal:shell », « terminal:agent=<id> », « terminal:installer=<id> », « terminal:taper=<texte> »,
                    // « terminal:touche=entree|echap », « terminal:fermer », « terminal:systeme[=id] », « terminal:etat » ;
                    // chaque réponse est une ligne KORACUT-TERMINAL (onglets, agents installés, texte de l'écran).
                    if (name.startsWith(QLatin1String("terminal:")) && m_terminalPanel) {
                        // « terminal:placer=droite|bas|haut|gauche|agents » : la place du panneau, comme le menu de sa barre.
                        if (name.startsWith(QLatin1String("terminal:placer="))) {
                            koraCutPlaceTerminal(name.mid(16));
                            qWarning().noquote() << "KORACUT-TERMINAL place" << koraCutTerminalPlace();
                            return;
                        }
                        if (name == QLatin1String("terminal:place")) {
                            qWarning().noquote() << "KORACUT-TERMINAL place" << koraCutTerminalPlace();
                            return;
                        }
                        koraCutRevealTerminalDock();
                        qWarning().noquote() << "KORACUT-TERMINAL" << m_terminalPanel->debugCommand(name.mid(9));
                        return;
                    }
                    if (name == QLatin1String("console-terminal") && m_consolePanel) {
                        slotShowConsolePanel();
                        qWarning() << "KoraCut: terminal" << m_consolePanel->openTerminal();
                        return;
                    }
                    // « menu-clip=N[=animer|transition[=P]] » : ouvre le menu du N-ième plan (1 au début) de la plus
                    // basse piste vidéo occupée (ou de la piste vidéo P), et le sous-menu demandé ; la capture <KORACUT_SHOT>-menu.png les
                    // montre posés sur la fenêtre (un menu ouvert n'apparaît pas dans la capture de la fenêtre).
                    // « menu-clip-choix=<outil>:<style> » applique ce choix au plan préparé, comme un clic.
                    if (name.startsWith(QLatin1String("menu-clip")) && getCurrentTimeline()) {
                        TimelineWidget *view = getCurrentTimeline();
                        KoraCutAnimateMenu *animate = view->koraCutAnimateMenu();
                        if (name.startsWith(QLatin1String("menu-clip-choix=")) && animate) {
                            const QString choice = name.mid(16);
                            const QJsonObject r = animate->apply(animate->find(choice.section(QLatin1Char(':'), 0, 0), choice.section(QLatin1Char(':'), 1)));
                            qWarning().noquote() << "KORACUT-MENU choix" << choice << QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Compact));
                            return;
                        }
                        const int wanted = name.section(QLatin1Char('='), 1, 1).toInt();
                        const QString sub = name.section(QLatin1Char('='), 2, 2);
                        auto timeline = view->model();
                        int cid = -1;
                        QList<int> videoTracks = timeline->getTracksIds(false);
                        std::sort(videoTracks.begin(), videoTracks.end(), [&](int a, int b) { return timeline->getTrackPosition(a) < timeline->getTrackPosition(b); });
                        // Un quatrième champ choisit la piste vidéo (1 = la plus basse) : « menu-clip=1=animer=4 ».
                        const int wantedTrack = name.section(QLatin1Char('='), 3, 3).toInt();
                        for (int tid : std::as_const(videoTracks)) {
                            if (wantedTrack > 0 && videoTracks.indexOf(tid) != wantedTrack - 1) {
                                continue;
                            }
                            QList<int> clips;
                            for (int c : timeline->getItemsInRange(tid, 0, -1, false)) {
                                if (timeline->isClip(c)) {
                                    clips << c;
                                }
                            }
                            if (clips.isEmpty()) {
                                continue;
                            }
                            std::sort(clips.begin(), clips.end(), [&](int a, int b) { return timeline->getClipPosition(a) < timeline->getClipPosition(b); });
                            cid = clips.value(std::max(1, wanted) - 1, -1);
                            break;
                        }
                        if (cid < 0) {
                            qWarning() << "KORACUT-MENU aucun plan";
                            return;
                        }
                        timeline->requestSetSelection({cid});
                        const QPoint at = view->mapToGlobal(QPoint(view->width() / 3, view->height() / 3));
                        QMenu *menu = view->koraCutShowClipMenu(cid, at);
                        QMenu *open = nullptr;
                        if (animate && (sub == QLatin1String("animer") || sub == QLatin1String("transition"))) {
                            open = sub == QLatin1String("animer") ? animate->animateMenu() : animate->transitionMenu();
                        }
                        // Document 13 : « menu-clip=N=couleur » ouvre Couleur du clip.
                        if (sub == QLatin1String("couleur") && view->koraCutColorMenu()) {
                            open = view->koraCutColorMenu()->clipMenu();
                        }
                        QTimer::singleShot(300, this, [this, menu, open]() {
                            if (open) {
                                menu->setActiveAction(open->menuAction());
                                open->popup(menu->mapToGlobal(menu->actionGeometry(open->menuAction()).topRight()));
                            }
                            QTimer::singleShot(400, this, [this, menu, open]() {
                                QPixmap shot = grab();
                                QPainter painter(&shot);
                                for (QMenu *m : {menu, open}) {
                                    if (m && m->isVisible()) {
                                        painter.drawPixmap(mapFromGlobal(m->mapToGlobal(QPoint(0, 0))), m->grab());
                                    }
                                }
                                painter.end();
                                QString path = qEnvironmentVariable("KORACUT_SHOT");
                                path.replace(QLatin1String(".png"), QLatin1String("-menu.png"));
                                qWarning() << "KORACUT-MENU capture" << path << shot.save(path);
                                if (open) {
                                    open->hide();
                                }
                                menu->hide();
                            });
                        });
                        return;
                    }
                    // Document 13 : « menu-piste=N » ouvre le menu de la N-ième piste vidéo (1 = la plus basse) et
                    // son sous-menu Couleur de la piste ; capture <KORACUT_SHOT>-menu-piste.png.
                    if (name.startsWith(QLatin1String("menu-piste=")) && getCurrentTimeline() && getCurrentTimeline()->koraCutColorMenu()) {
                        TimelineWidget *view = getCurrentTimeline();
                        auto timeline = view->model();
                        QList<int> videoTracks = timeline->getTracksIds(false);
                        std::sort(videoTracks.begin(), videoTracks.end(), [&](int a, int b) { return timeline->getTrackPosition(a) < timeline->getTrackPosition(b); });
                        const int tid = videoTracks.value(std::max(1, name.mid(11).toInt()) - 1, -1);
                        if (tid < 0) {
                            qWarning() << "KORACUT-MENU aucune piste";
                            return;
                        }
                        QMenu *menu = view->koraCutShowHeaderMenu(tid, view->mapToGlobal(QPoint(40, view->height() / 3)));
                        QMenu *open = view->koraCutColorMenu()->trackMenu();
                        QTimer::singleShot(300, this, [this, menu, open]() {
                            menu->setActiveAction(open->menuAction());
                            open->popup(menu->mapToGlobal(menu->actionGeometry(open->menuAction()).topRight()));
                            QTimer::singleShot(400, this, [this, menu, open]() {
                                QPixmap shot = grab();
                                QPainter painter(&shot);
                                for (QMenu *m : {menu, open}) {
                                    if (m->isVisible()) {
                                        painter.drawPixmap(mapFromGlobal(m->mapToGlobal(QPoint(0, 0))), m->grab());
                                    }
                                }
                                painter.end();
                                QString path = qEnvironmentVariable("KORACUT_SHOT");
                                path.replace(QLatin1String(".png"), QLatin1String("-menu-piste.png"));
                                qWarning() << "KORACUT-MENU capture" << path << shot.save(path);
                                open->hide();
                                menu->hide();
                            });
                        });
                        return;
                    }
                    // « tete:2.5 » : place la tête de lecture à cette seconde, moniteur Programme actif.
                    if (name.startsWith(QLatin1String("tete:"))) {
                        pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
                        if (m_projectMonitorDock) {
                            m_projectMonitorDock->open();
                            m_projectMonitorDock->setAsCurrentTab();
                        }
                        if (getCurrentTimeline()) {
                            getCurrentTimeline()->controller()->setPosition(int(std::lround(name.mid(5).toDouble() * pCore->getCurrentFps())));
                        }
                        return;
                    }
                    // Module Motion (document 19) : « motion:importer=<chemin> » importe un dossier ou un .html,
                    // « motion:rendre » relance les rendus, « motion:texte=<id>=<texte> » change le texte d'un
                    // élément, « motion:deplacer=<id>=<secondes> » déplace son clip, « motion:etat » écrit une
                    // ligne KORACUT-MOTION par élément (clip, place, rendu), « motion:code » ouvre l'onglet Code,
                    // « motion:inspecter=<id> » ouvre l'inspecteur sur le clip (comme un double-clic).
                    if (name.startsWith(QLatin1String("motion:")) && m_motionMirror) {
                        const QString verb = name.mid(7).section(QLatin1Char('='), 0, 0);
                        const QString rest = name.mid(7).section(QLatin1Char('='), 1);
                        // L'élément visé : « projet/id » ou juste « id », cherché dans toutes les compositions.
                        const QString elementId = rest.section(QLatin1Char('='), 0, 0).section(QLatin1Char('/'), -1);
                        QString project = rest.section(QLatin1Char('='), 0, 0).contains(QLatin1Char('/')) ? rest.section(QLatin1Char('/'), 0, 0)
                                                                                                             : m_motionMirror->projectOfElement(elementId);
                        if (project.isEmpty()) {
                            project = m_motionPanel->currentProject();
                        }
                        if (verb == QLatin1String("importer")) {
                            qWarning() << "KoraCut: motion importé" << m_motionPanel->importWithoutDialog(rest);
                        } else if (verb == QLatin1String("rendre")) {
                            m_motionPanel->renderAll();
                        } else if (verb == QLatin1String("texte")) {
                            qWarning() << "KoraCut: motion texte" << m_motionMirror->setElementText(project, elementId, rest.section(QLatin1Char('='), 1));
                        } else if (verb == QLatin1String("deplacer")) {
                            qWarning() << "KoraCut: motion déplacé"
                                       << m_motionMirror->moveElement(project, elementId, rest.section(QLatin1Char('='), 1).toDouble());
                        } else if (verb == QLatin1String("code")) {
                            m_motionPanel->showCode(project);
                        } else if (verb == QLatin1String("inspecter")) {
                            // Comme un double-clic sur le clip de cet élément dans la timeline.
                            koraCutEditMotionClip(m_motionMirror->clipFor(project, elementId));
                            return;
                        } else if (verb == QLatin1String("etat")) {
                            qWarning().noquote() << m_motionMirror->stateDump().trimmed();
                        }
                        slotShowMotionPanel();
                        return;
                    }
                    // « moniteur:zoom=150 », « moniteur:regles », « moniteur:repere=x:540 », « moniteur:plateforme=tiktok »,
                    // « moniteur:deplacer=100,80 », « moniteur:etat » : le moniteur Programme (document 20).
                    // « timelines:poser=2:droite », « timelines:onglet=2 », « timelines:activer=1 »,
                    // « timelines:glisser=2@0.9;0.6 », « timelines:lacher », « timelines:renommer=1 »,
                    // « timelines:etat » : les séquences côte à côte (document 13, § 5).
                    if (name.startsWith(QLatin1String("timelines:")) && m_timelineTabs) {
                        qWarning().noquote() << "KORACUT-TIMELINES" << m_timelineTabs->koraCutDebugCommand(name.mid(10));
                        return;
                    }
                    if (name.startsWith(QLatin1String("moniteur:"))) {
                        pCore->getMonitor(Kdenlive::ProjectMonitor)->koraCutDebugCommand(name.mid(9));
                        return;
                    }
                    if (name.startsWith(QLatin1String("type:"))) {
                        Q_EMIT m_topBar->searchField()->textEdited(name.mid(5));
                        qWarning() << "KoraCut: frappe simulée" << name.mid(5);
                        return;
                    }
                    if (QAction *a = actionCollection()->action(name.trimmed())) {
                        qWarning() << "KoraCut: action déclenchée" << name;
                        a->trigger();
                    } else {
                        qWarning() << "KoraCut: action introuvable" << name;
                    }
                });
                step += 1500;
            }
            QTimer::singleShot(step + 500, this, [this, path]() {
                // L'état plié ou déplié de chaque piste, pour ce que la capture ne montre pas, et le
                // nombre de compositions posées.
                if (auto timeline = pCore->projectManager()->getTimeline()) {
                    int compositions = 0;
                    for (bool audio : {false, true}) {
                        for (int tid : timeline->getTracksIds(audio)) {
                            for (int id : timeline->getItemsInRange(tid, 0, -1, true)) {
                                compositions += timeline->isComposition(id) ? 1 : 0;
                            }
                        }
                    }
                    qWarning().noquote() << "KORACUT-COMPOSITIONS" << compositions;
                    for (bool audio : {false, true}) {
                        const QList<int> tracks = timeline->getTracksIds(audio);
                        for (int tid : tracks) {
                            qWarning().noquote() << "KORACUT-TRACK" << timeline->getTrackTagById(tid)
                                                 << "collapsed=" + timeline->getTrackProperty(tid, QStringLiteral("kdenlive:collapsed")).toString();
                        }
                    }
                }
                grab().save(path);
                // Les fenêtres secondaires ouvertes par les actions : une capture chacune.
                int window = 0;
                for (QWidget *w : QApplication::topLevelWidgets()) {
                    if (w != this && w->isVisible() && w->isWindow() && w->width() > 200) {
                        QString target = path;
                        target.replace(QLatin1String(".png"), QStringLiteral("-fenetre-%1.png").arg(++window));
                        w->grab().save(target);
                    }
                }
                if (pCore->currentDoc()) {
                    pCore->currentDoc()->setModified(false);
                }
                QCoreApplication::quit();
            });
            return;
        }
        // KORACUT_SHOT_PANEL=timeline (nom interne d'un panneau) : agrandit ce panneau, capture
        // (-agrandi.png), le remet en place, capture (-rendu.png). Contrôle du triple clic sans souris.
        const QString panel = qEnvironmentVariable("KORACUT_SHOT_PANEL");
        if (!panel.isEmpty()) {
            QString grand = path;
            grand.replace(QLatin1String(".png"), QLatin1String("-agrandi.png"));
            QString rendu = path;
            rendu.replace(QLatin1String(".png"), QLatin1String("-rendu.png"));
            grab().save(path);
            KoraCutPanelFocus::toggle(KDDockWidgets::Core::DockWidget::byName(panel));
            QTimer::singleShot(1500, this, [this, grand]() {
                grab().save(grand);
                KoraCutPanelFocus::toggle(nullptr);
            });
            QTimer::singleShot(3000, this, [this, rendu]() {
                grab().save(rendu);
                QCoreApplication::quit();
            });
            return;
        }
        const QString modules = qEnvironmentVariable("KORACUT_SHOT_MODULES");
        if (!modules.isEmpty()) {
            // Une capture par module : KORACUT_SHOT_MODULES=montage,audio,photo
            const QStringList ids = modules.split(QLatin1Char(','), Qt::SkipEmptyParts);
            int step = 0;
            for (const QString &id : ids) {
                QTimer::singleShot(step, this, [this, id, path]() {
                    m_topBar->moduleTabs()->setCurrentLayout(id);
                    if (KoraCutModules::isUnderConstruction(id)) {
                        showModuleUnderConstruction(id);
                    } else {
                        showDockArea();
                        Q_EMIT pCore->loadLayoutById(id);
                    }
                });
                step += 1500;
                QTimer::singleShot(step, this, [this, id, path]() {
                    // KORACUT_SHOT_SELECT=bin : sélectionne tout le chutier avant la capture.
                    // Sans ça, aucune ligne n'est jamais sélectionnée sur une capture prise par
                    // grab(), et l'habillage de la sélection ne peut pas être contrôlé à l'écran
                    // (l'accessibilité ne sait pas sélectionner une ligne de liste Qt, et
                    // screencapture ne rend rien quand la session du Mac est verrouillée).
                    const QString quoiSelectionner = qEnvironmentVariable("KORACUT_SHOT_SELECT");
                    if (quoiSelectionner == QLatin1String("bin") && pCore->bin()) {
                        pCore->bin()->selectAll();
                    } else if (quoiSelectionner == QLatin1String("source")) {
                        // Contrôle de la bascule automatique : MonitorManager demande le moniteur
                        // de source, et l'onglet « Source » doit passer devant tout seul. C'est le
                        // chemin exact que prend un double clic sur un média du chutier.
                        pCore->monitorManager()->activateMonitor(Kdenlive::ClipMonitor, true, true);
                    } else if (quoiSelectionner == QLatin1String("clip") && pCore->bin()) {
                        // Sélectionne le premier média du chutier. Ne suffit pas à l'ouvrir dans le
                        // moniteur de source : essayé avec `slotOpenClip()` par le méta-objet, la
                        // vue du chutier n'a pas encore le focus à cet instant et rien ne s'ouvre.
                        // Contrôler qu'un média ouvert masque bien l'état vide du moniteur reste
                        // donc à faire à la main, session déverrouillée.
                        pCore->bin()->selectClipById(QStringLiteral("2"));
                    } else if (quoiSelectionner == QLatin1String("effets-vides") && m_effectListDock && m_effectList2) {
                        // Met la liste d'effets au premier plan avec une recherche sans résultat :
                        // c'est le seul moyen de regarder cet état vide sur une capture prise par
                        // grab(), l'accessibilité ne pouvant ni cliquer un onglet ni taper un mot
                        // quand la session du Mac est verrouillée.
                        m_effectListDock->raise();
                        m_effectList2->setFilterName(QStringLiteral("zzzzz"));
                    }
                    QString target = path;
                    target.replace(QLatin1String(".png"), QStringLiteral("-%1.png").arg(id));
                    grab().save(target);
                    qWarning() << "KoraCut: capture écrite dans" << target;
                });
                step += 600;
            }
            QTimer::singleShot(step + 400, qApp, &QCoreApplication::quit);
            return;
        }
        grab().save(path);
        qWarning() << "KoraCut: capture écrite dans" << path;
        QTimer::singleShot(200, qApp, &QCoreApplication::quit);
    });
}

void MainWindow::setupToolSegments()
{
    if (!m_timelineToolBar) {
        return;
    }
    // L'espacement passe de 4 px à 0 : ce sont les marges de « koracut.qss » qui redonnent
    // 4 px entre deux boutons ordinaires, et 0 entre deux segments.
    if (m_timelineToolBar->layout()) {
        m_timelineToolBar->layout()->setSpacing(0);
    }
    const QStringList outils = {QStringLiteral("select_tool"), QStringLiteral("razor_tool"), QStringLiteral("ripple_tool"),
                                QStringLiteral("slip_tool"), QStringLiteral("spacer_tool")};
    QList<QWidget *> segments;
    for (const QString &nom : outils) {
        QAction *a = actionCollection()->action(nom);
        if (!a) {
            continue;
        }
        if (QWidget *w = m_timelineToolBar->widgetForAction(a)) {
            segments << w;
        }
    }
    for (int i = 0; i < segments.count(); ++i) {
        const QLatin1String place = segments.count() == 1 ? QLatin1String("seul")
                                    : i == 0              ? QLatin1String("premier")
                                    : i == segments.count() - 1 ? QLatin1String("dernier")
                                                                : QLatin1String("milieu");
        segments.at(i)->setProperty("koracutSegment", QString(place));
        segments.at(i)->style()->polish(segments.at(i));
    }
}

void MainWindow::setSingleMonitor(bool single)
{
    if (!m_clipMonitorDock || !m_projectMonitorDock || !mainDockWindow) {
        return;
    }
    KdenliveSettings::setSingleMonitor(single);

    // Les deux moniteurs sont-ils déjà dans le même groupe ? On ne remanie que s'il le faut :
    // cette fonction est appelée après chaque chargement de disposition, et déplacer un dock qui
    // est déjà au bon endroit casserait l'arrangement enregistré (en mode deux moniteurs, cela
    // sortait « Programme » du groupe de « Voix » et « Notes », le laissant sans onglet visible).
    QList<KDDockWidgets::QtWidgets::DockWidget *> groupeProjet;
    bool ensemble = false;
    for (KDDockWidgets::Core::Group *group : KDDockWidgets::DockRegistry::self()->groups()) {
        bool contientProjet = false;
        bool contientSource = false;
        for (KDDockWidgets::Core::DockWidget *d : group->dockWidgets()) {
            contientProjet = contientProjet || d->uniqueName() == QLatin1String("projectmonitor");
            contientSource = contientSource || d->uniqueName() == QLatin1String("clipmonitor");
        }
        if (!contientProjet) {
            continue;
        }
        ensemble = contientSource;
        for (KDDockWidgets::Core::DockWidget *d : group->dockWidgets()) {
            if (auto *vue = qobject_cast<KDDockWidgets::QtWidgets::DockWidget *>(KDDockWidgets::QtCommon::View_qt::asQWidget(d->view()))) {
                groupeProjet << vue;
            }
        }
        break;
    }
    if (ensemble == single) {
        return;
    }

    if (single) {
        // Le moniteur de projet rejoint le groupe du moniteur de source : deux onglets, une image.
        // Il faut emmener tout son groupe, pas seulement lui : « Voix » et « Notes » sont des
        // onglets du même groupe, et les laisser derrière rendait la place libérée à un éditeur
        // vocal large de la moitié de la fenêtre (vu à l'écran le 21/09/2026).
        // Le moniteur de projet en premier, pour qu'il donne son nom au groupe fusionné.
        std::stable_sort(groupeProjet.begin(), groupeProjet.end(), [](auto *a, auto *b) {
            return a->uniqueName() == QLatin1String("projectmonitor") && b->uniqueName() != QLatin1String("projectmonitor");
        });
        for (auto *d : std::as_const(groupeProjet)) {
            m_clipMonitorDock->addDockWidgetAsTab(d);
        }
        // Source et Programme côte à côte. `addDockWidgetAsTab()` ajoute à la fin, ce qui donnait
        // Source, Bibliothèque, Programme : les deux moniteurs, entre lesquels on bascule cinquante
        // fois par heure, se retrouvaient séparés par la bibliothèque de médias. On ramène Programme
        // juste après Source, le reste suit.
        for (KDDockWidgets::Core::Group *group : KDDockWidgets::DockRegistry::self()->groups()) {
            KDDockWidgets::Core::DockWidget *source = nullptr;
            KDDockWidgets::Core::DockWidget *programme = nullptr;
            for (KDDockWidgets::Core::DockWidget *d : group->dockWidgets()) {
                if (d->uniqueName() == QLatin1String("clipmonitor")) {
                    source = d;
                } else if (d->uniqueName() == QLatin1String("projectmonitor")) {
                    programme = d;
                }
            }
            if (!source || !programme || !group->tabBar()) {
                continue;
            }
            const int depuis = group->tabBar()->indexOfDockWidget(programme);
            const int vers = group->tabBar()->indexOfDockWidget(source) + 1;
            if (depuis > vers && vers >= 0) {
                group->tabBar()->moveTabTo(depuis, vers);
            }
            break;
        }
    } else {
        // Retour à deux moniteurs : le groupe fusionné se scinde, et « Voix » et « Notes »
        // repartent avec le moniteur de projet, comme ils y étaient.
        mainDockWindow->addDockWidget(m_projectMonitorDock, KDDockWidgets::Location_OnRight, m_clipMonitorDock);
        for (const QLatin1String nom : {QLatin1String("textedit"), QLatin1String("notes_widget")}) {
            if (auto *d = KDDockWidgets::DockRegistry::self()->dockByName(nom)) {
                if (auto *vue = qobject_cast<KDDockWidgets::QtWidgets::DockWidget *>(KDDockWidgets::QtCommon::View_qt::asQWidget(d->view()))) {
                    m_projectMonitorDock->addDockWidgetAsTab(vue);
                }
            }
        }
    }
    // La bascule automatique Source / Programme ne change pas : MonitorManager appelle
    // raiseMonitor(), qui appelle setAsCurrentTab(), et un onglet se lève aussi bien qu'un dock.
    // On remet simplement en avant celui qui est actif.
    if (pCore->monitorManager()) {
        raiseMonitor(pCore->monitorManager()->isActive(Kdenlive::ClipMonitor), false);
    }
}

void MainWindow::showModuleUnderConstruction(const QString &moduleId)
{
    if (!m_centralStack || !m_modulePlaceholder) {
        return;
    }
    m_modulePlaceholder->setModule(moduleId);
    const bool fromDocks = m_centralStack->currentWidget() != m_modulePlaceholder;
    m_centralStack->setCurrentWidget(m_modulePlaceholder);
    // KoraCut, 23 septembre 2026 : la barre d'état ne porte que des réglages de timeline (zoom,
    // marqueurs, miniatures), sans objet sur la page d'un module en construction. On la cache, en
    // retenant si elle était visible, pour la rendre telle quelle au retour.
    if (fromDocks) {
        statusBar()->setProperty("koracutVisibleAvant", statusBar()->isVisible());
        statusBar()->hide();
    }
}

void MainWindow::showDockArea()
{
    if (m_centralStack) {
        const bool fromPlaceholder = m_centralStack->currentWidget() == m_modulePlaceholder;
        m_centralStack->setCurrentWidget(mainDockWindow);
        if (fromPlaceholder && statusBar()->property("koracutVisibleAvant").toBool()) {
            statusBar()->show();
        }
    }
}

void MainWindow::updateTopBarProject()
{
    if (!m_topBar) {
        return;
    }
    KdenliveDoc *project = pCore->currentDoc();
    QString name;
    QString path;
    bool modified = false;
    if (project) {
        const QUrl url = project->url();
        name = url.isEmpty() ? QString() : url.fileName();
        path = url.isLocalFile() ? url.toLocalFile() : QString();
        modified = project->isModified();
    }
    m_topBar->setProject(name, modified, path);
}

void MainWindow::finishUiSetup()
{
    pCore->restoreLayout();
    Q_EMIT pCore->closeSplash();
    setAutoSaveSettings();

    // KoraCut : la barre du haut remplace la barre d'outils standard (Nouveau, Ouvrir, Enregistrer, Rendu...).
    // On la masque une seule fois, au premier lancement : l'utilisateur reste libre de la réafficher.
    if (!KdenliveSettings::koracut_topbar_applied()) {
        if (KToolBar *main = toolBar()) {
            main->hide();
        }
#ifndef Q_OS_MAC
        // Sous Windows et Linux, tout le menu passe par le chevron de la barre du haut.
        // Sur Mac la barre de menus est native, en haut de l'écran : on n'y touche pas.
        menuBar()->hide();
        if (QAction *showMenu = actionCollection()->action(KStandardAction::name(KStandardAction::ShowMenubar))) {
            showMenu->setChecked(false);
        }
#endif
        KdenliveSettings::setKoracut_topbar_applied(true);
    }
    updateTopBarProject();

    // Si la session précédente s'est terminée sur un module en construction, on y revient,
    // sinon l'onglet actif et ce qui est à l'écran ne diraient pas la même chose.
    const QString lastModule = KdenliveSettings::koracut_module();
    if (KoraCutModules::isUnderConstruction(lastModule)) {
        m_topBar->moduleTabs()->setCurrentLayout(lastModule);
        showModuleUnderConstruction(lastModule);
    }
    koraCutDevScreenshot();
    koraCutRelay();
    // Le paquet téléchargé dit quand une version plus récente est publiée (KC-009) ; KORACUT_MAJ_FORCER=1 pour les essais.
    KoraCutUpdateCheck::schedule(this, qEnvironmentVariableIntValue("KORACUT_MAJ_FORCER") > 0);
    koraCutStartPorte();
    // Premier lancement : deux questions simples, une fois la fenêtre ouverte (4 octobre 2026).
    KoraCutFirstRun::schedule(this);
    QObject::disconnect(pCore.get(), &Core::GUISetupDone, this, nullptr);
    // This should connect only after splash is done
    connect(pCore.get(), &Core::loadingMessageNewStage, this, [&](const QString &message, int max = -1) {
        if (max > -1) {
            m_loadingDialog->reset();
            m_loadingDialog->setMaximum(max);
        }
        if (!message.isEmpty()) {
            m_loadingDialog->setLabelText(message);
        }
        m_loadingDialog->show();
    });
    connect(pCore.get(), &Core::loadingMessageIncrease, this, [&]() { m_loadingDialog->setValue(m_loadingDialog->value() + 1); });
    connect(pCore.get(), &Core::loadingMessageHide, this, [&]() {
        m_loadingDialog->reset();
        m_loadingDialog->setMaximum(0);
        m_loadingDialog->close();
    });
}

void MainWindow::loadBins(QStringList binInfo)
{
    // Delete all secondary bins
    while (m_binWidgets.size() > 1) {
        int ix = 0;
        auto bin = m_binWidgets.at(ix);
        if (bin->isMainBin()) {
            ix = 1;
            bin = m_binWidgets.at(ix);
        }
        auto toDelete = bin->parentWidget();
        m_binWidgets.takeAt(ix);
        delete toDelete;
    }

    // Create missing bins
    Bin *mainBin = nullptr;
    if (!m_binWidgets.isEmpty()) {
        // Main bin still there
        mainBin = m_binWidgets.first();
    }

    QStringList existingNames;
    for (const QString &info : binInfo) {
        if (mainBin && info.startsWith("project_bin:")) {
            // Main Bin, don't recreate
            continue;
        }
        auto bin = new Bin(pCore->projectItemModel(), this, false);
        addBin(bin, QString(), false, info.section(QLatin1Char(':'), 0, 0));
    }
}

void MainWindow::loadContainerActions()
{
    // Build all actions using the static_cast<QMenu *>(factory()->container method
    // That need to be rebuild when updating the ui.rc file

    // Redirect help entry to our own function
    // First delete the default help action
    QAction *officialHelp = actionCollection()->action(KStandardAction::name(KStandardAction::HelpContents));
    // Save and later restore any custom shortcuts
    QList<QKeySequence> helpShortcuts = officialHelp->shortcuts();
    actionCollection()->removeAction(officialHelp);
    // Now recreate our own
    KStandardAction::helpContents(this, &MainWindow::appHelpActivated, actionCollection());
    officialHelp = actionCollection()->action(KStandardAction::name(KStandardAction::HelpContents));
    officialHelp->setShortcuts(helpShortcuts);
    // Replug it in the Help menu
    QMenu *helpMenu = static_cast<QMenu *>(factory()->container(QStringLiteral("help"), this));
    if (helpMenu) {
        QAction *whatsThis = actionCollection()->action(KStandardAction::name(KStandardAction::WhatsThis));
        helpMenu->insertAction(whatsThis, officialHelp);
    }

    // KoraCut : le menu Aide ne renvoie plus vers d'autres sites.
    // Le signalement de bogue ouvre notre page.
    if (QAction *bugAction = actionCollection()->action(KStandardAction::name(KStandardAction::ReportBug))) {
        disconnect(bugAction, &QAction::triggered, nullptr, nullptr);
        // 4 octobre 2026 : le mot de tous les jours, pas « bogue ».
        bugAction->setText(i18nc("@action", "Report a Defect…"));
        connect(bugAction, &QAction::triggered, this,
                [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://koracut.org/bogues"))); });
    }
    // KMainWindow remplit tout seul la liste des traducteurs à partir du catalogue de traduction,
    // ce qui rouvre l'onglet « Traduction » et son bandeau vers un autre projet, que les
    // bibliothèques ne laissent pas modifier. On vide donc la liste une fois la fenêtre construite.
    // Les traducteurs restent crédités dans les en-têtes des fichiers po/ et dans AUTHORS.
    {
        KAboutData about = KAboutData::applicationData();
        about.setTranslator(QString(), QString());
        KAboutData::setApplicationData(about);
    }

    // L'entrée « À propos de KDE » n'a pas sa place ici : la licence et les détenteurs de droits
    // d'auteur restent dans la fenêtre « À propos de KoraCut ». Même chose pour le don,
    // qui renvoie vers un site qui n'est pas le nôtre.
    for (auto standardAction : {KStandardAction::AboutKDE, KStandardAction::Donate}) {
        if (QAction *action = actionCollection()->action(KStandardAction::name(standardAction))) {
            if (helpMenu) {
                helpMenu->removeAction(action);
            }
            action->setVisible(false);
            // 25 septembre 2026 : retirée aussi de la collection, sinon elle restait dans la fenêtre des
            // raccourcis et la palette, avec le logo de KDE. takeAction la garde en vie (KHelpMenu la tient).
            actionCollection()->takeAction(action);
        }
    }

    // rebuild timeline clip menu
    m_timelineTabs->buildClipMenu();

    // Ensure the "Monitor Config" menu doesn't replace the settings dialog on Mac because of text heuristics
    auto *monitorConfig = qobject_cast<QMenu *>(factory()->container(QStringLiteral("monitor_config"), this));
    if (monitorConfig) {
        monitorConfig->menuAction()->setMenuRole(QAction::NoRole);
    }

    // KoraCut, 23 septembre 2026 : sur Mac, Qt déplace vers le menu de l'application toute action dont
    // le texte ressemble à « Settings », « Preferences », « About » ou « Quit » (rôle par défaut
    // TextHeuristicRole). « Réglages du projet… » et « Configurer KoraCut… » finissaient fusionnés en
    // une seule entrée « Préférences… » qui n'ouvrait rien. On fixe le rôle de chacune : les trois
    // entrées propres au Mac vont dans le menu de l'application, tout le reste reste à sa place.
    {
        const QMap<QString, QAction::MenuRole> roles = {
            {KStandardAction::name(KStandardAction::Preferences), QAction::PreferencesRole},
            {KStandardAction::name(KStandardAction::Quit), QAction::QuitRole},
            {KStandardAction::name(KStandardAction::AboutApp), QAction::AboutRole},
        };
        const QList<QAction *> all = actionCollection()->actions();
        for (QAction *action : all) {
            action->setMenuRole(roles.value(action->objectName(), QAction::NoRole));
        }
        // Les sous-menus aussi (leur action de menu porte le même risque).
        std::function<void(QMenu *)> walk = [&walk](QMenu *menu) {
            for (QAction *action : menu->actions()) {
                if (action->menu()) {
                    if (action->menuRole() == QAction::TextHeuristicRole) {
                        action->setMenuRole(QAction::NoRole);
                    }
                    walk(action->menu());
                }
            }
        };
        for (QAction *top : menuBar()->actions()) {
            if (top->menu()) {
                walk(top->menu());
            }
        }
    }

    // Connect monitor overlay info menu.
    QMenu *monitorOverlay = static_cast<QMenu *>(factory()->container(QStringLiteral("monitor_config_overlay"), this));
    if (monitorOverlay) {
        connect(monitorOverlay, &QMenu::triggered, this, &MainWindow::slotSwitchMonitorOverlay);

        m_projectMonitor->setupMenu(static_cast<QMenu *>(factory()->container(QStringLiteral("monitor_go"), this)), monitorOverlay, m_playZone,
                                    m_playZoneFromCursor, m_loopZone, m_loopClip);
        m_clipMonitor->setupMenu(static_cast<QMenu *>(factory()->container(QStringLiteral("monitor_go"), this)), monitorOverlay, m_playZone,
                                 m_playZoneFromCursor, m_loopZone, nullptr);
    }

    QMenu *clipInTimeline = static_cast<QMenu *>(factory()->container(QStringLiteral("clip_in_timeline"), this));
    clipInTimeline->setIcon(QIcon::fromTheme(QStringLiteral("go-jump")));

    QMenu *m = static_cast<QMenu *>(factory()->container(QStringLiteral("video_effects_menu"), this));
    connect(m, &QMenu::triggered, this, &MainWindow::slotAddEffect);

    QMenu *addMenu = static_cast<QMenu *>(factory()->container(QStringLiteral("generators"), this));
    Generators::getGenerators(KdenliveSettings::producerslist(), addMenu);
    connect(addMenu, &QMenu::triggered, this, &MainWindow::buildGenerator);
}

MainWindow::~MainWindow()
{
    if (m_motionStudio) {
        m_motionStudio->stopAll();
    }
#if KORACUT_AGENTS
    // Le service d'agents se ferme pendant que la barre du haut et le panneau existent encore
    // (voir KoraCutAgentClient::~KoraCutAgentClient).
    if (m_agentClient) {
        m_agentClient->stop();
    }
#endif
    pCore->prepareShutdown();
    delete m_timelineTabs;
    if (m_audioSpectrum) {
        delete m_audioSpectrum;
    }
    if (m_projectMonitor) {
        m_projectMonitor->stop();
    }
    if (m_clipMonitor) {
        m_clipMonitor->stop();
    }
    // Core::mediaUnavailable.reset();
    delete m_projectMonitor;
    delete m_clipMonitor;
    delete m_shortcutRemoveFocus;
    delete m_effectList2;
    delete m_compositionList;
    delete m_loadingDialog;
    pCore->finishShutdown();
    qDeleteAll(m_transitions);
    // KoraCut (4 octobre 2026) : Mlt::Factory::close() retiré d'ici. Il décharge les modules de MLT alors que des
    // objets MLT vivent encore (les playlists du chutier, un fil de vignettes qui décode) : six rapports de plantage
    // à la fermeture, le destructeur d'un Tractor sautant dans un module déjà déchargé. Le système rend tout à la
    // fin du processus (Recherche/Stabilite - releve des plantages).
}

// virtual
void MainWindow::koraCutTerminate()
{
    // Un SIGTERM (kill, un script, un agent) ferme KoraCut proprement, sans fenêtre à laquelle personne ne
    // répondrait (28 septembre 2026) : le travail non enregistré part dans une copie de récupération, jamais
    // par-dessus le fichier de la personne ; puis la fermeture normale retire la porte et le témoin.
    if (qApp->property("koracut_terminer").toBool()) {
        return;
    }
    qApp->setProperty("koracut_terminer", true);
    qApp->setProperty("koracut_sans_fenetre", true);
    KdenliveDoc *doc = pCore->currentDoc();
    if (doc && doc->isModified()) {
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH'h'mm"));
        QString file;
        const QFileInfo current(doc->url().toLocalFile());
        if (!doc->url().isEmpty() && current.exists()) {
            file = current.absoluteDir().absoluteFilePath(QStringLiteral("%1 (récupération %2).koracut").arg(current.completeBaseName(), stamp));
        } else {
            const QString folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/KoraCut");
            QDir().mkpath(folder);
            file = folder + QStringLiteral("/Projet ") + stamp + QStringLiteral(".koracut");
        }
        if (pCore->projectManager()->saveFileAs(file, false, true)) {
            qWarning().noquote() << "KoraCut : arrêt demandé, copie de récupération" << file;
            doc->setModified(false);
        }
    }
    // Une fenêtre modale ouverte (une question) bloquerait la fermeture : on la referme d'abord.
    while (QWidget *modal = QApplication::activeModalWidget()) {
        if (auto *dialog = qobject_cast<QDialog *>(modal)) {
            dialog->reject();
        } else {
            modal->close();
        }
        if (QApplication::activeModalWidget() == modal) {
            break;
        }
    }
    // Filet : si la fermeture n'aboutit pas quand même, on sort, porte et témoin retirés.
    QPointer<KoraCutPorte> porte = m_porte;
    QTimer::singleShot(15000, qApp, [porte]() {
        if (porte) {
            porte->close();
        }
        QFile::remove(Core::lockFilePath());
        std::_Exit(0);
    });
    close();
}

bool MainWindow::queryClose()
{
    if (qApp->property("koracut_terminer").toBool()) {
        // Arrêt par signal : les rendus en cours continuent seuls (koracut_render est un autre processus).
        // Le document est fermé ici, sans question, tant que tout vit encore (4 octobre 2026) : sinon le chutier
        // vide ses clips dans ~MainWindow, après la destruction du gestionnaire de projet, et l'application
        // finit en erreur de segmentation (la copie de récupération est déjà écrite à ce moment-là).
        if (pCore->projectManager()) {
            pCore->projectManager()->closeCurrentDocument(false, true);
        }
        m_windowClosing = true;
        return true;
    }
    if (const QStringList atWork = koraCutAgentsAtWork(); !atWork.isEmpty() && !qEnvironmentVariableIsSet("KORACUT_SHOT") &&
        !qApp->property("koracut_sans_fenetre").toBool() && !qApp->property("koracut_quitter_porte").toBool()) {
        // Un agent monte en ce moment : fermer arrêterait son tour à moitié (instances fermées sans prévenir, 1er octobre 2026).
        // app.quitter, demandé par l'agent lui-même, passe sans question.
        if (KMessageBox::warningContinueCancel(this,
                                               i18nc("@info %1 is the list of agents at work",
                                                     "An agent is editing this project right now (%1). If KoraCut closes, its turn stops halfway. Close anyway?",
                                                     atWork.join(QStringLiteral(", "))),
                                               QString(), KGuiItem(i18nc("@action:button", "Close anyway")), KStandardGuiItem::cancel()) != KMessageBox::Continue) {
            return false;
        }
    }
    if (KoraCutExportQueue::self().count(QStringLiteral("en_cours")) > 0 && !qEnvironmentVariableIsSet("KORACUT_SHOT") &&
        !qApp->property("koracut_sans_fenetre").toBool()) {
        // Les rendus du module Export : ils continuent après la fermeture, ou s'arrêtent.
        const int answer = KMessageBox::warningTwoActionsCancel(this, i18nc("@info", "An export is still running. What should happen to it?"), QString(),
                                                                KGuiItem(i18nc("@action:button", "Keep exporting in the background")),
                                                                KGuiItem(i18nc("@action:button", "Stop the export")), KStandardGuiItem::cancel(),
                                                                QStringLiteral("koracutWarnOnExportExit"));
        if (answer == KMessageBox::Cancel) {
            return false;
        }
        if (answer == KMessageBox::SecondaryAction) {
            for (const auto &j : KoraCutExportQueue::self().jobs()) {
                KoraCutExportQueue::self().abort(j);
            }
        }
    }
    if (m_renderWidget) {
        int waitingJobs = m_renderWidget->waitingJobsCount();
        int runningJobs = m_renderWidget->runningJobsCount();
        if (runningJobs > 0) {
            switch (KMessageBox::warningTwoActionsCancel(this,
                                                         i18np("You have 1 rendering job running.\nWhat do you want to do with this job?",
                                                               "You have %1 rendering jobs running.\nWhat do you want to do with these jobs?", runningJobs),
                                                         QString(), KGuiItem(i18n("Continue rendering in the background")), KGuiItem(i18n("Abort rendering")),
                                                         KStandardGuiItem::cancel(), QStringLiteral("warnOnRenderExit"))) {
            case KMessageBox::PrimaryAction:
                // create script with waiting jobs and start it
                if (waitingJobs > 0 && !m_renderWidget->startWaitingRenderJobs()) {
                    return false;
                }
                break;
            case KMessageBox::SecondaryAction:
                // Abort the jobs
                Q_EMIT abortAllRenderJobs();
                break;
            default:
                return false;
            }
        } else if (waitingJobs > 0) {
            switch (KMessageBox::warningTwoActionsCancel(this,
                                                         i18np("You have 1 rendering job waiting in the queue.\nWhat do you want to do with this job?",
                                                               "You have %1 rendering jobs waiting in the queue.\nWhat do you want to do with these jobs?",
                                                               waitingJobs),
                                                         QString(), KGuiItem(i18n("Start them now")), KGuiItem(i18n("Delete them")))) {
            case KMessageBox::PrimaryAction:
                // create script with waiting jobs and start it
                if (!m_renderWidget->startWaitingRenderJobs()) {
                    return false;
                }
                break;
            case KMessageBox::SecondaryAction:
                // Don't do anything, jobs will be deleted
                break;
            default:
                return false;
            }
        }
    }
    // WARNING: According to KMainWindow::queryClose documentation we are not supposed to close the document here?
    KDDockWidgets::LayoutSaver dockLayout(KDDockWidgets::RestoreOption_AbsoluteFloatingDockWindows);
    KdenliveSettings::setKdockLayout(QString(dockLayout.serializeLayout()));
    // setAutoSaveSettings(QStringLiteral("MainWindow"), false);
    if (!pCore->projectManager()->closeCurrentDocument(true, true)) {
        return false;
    }
    saveOptions();
    m_windowClosing = true;
    return true;
}

void MainWindow::buildGenerator(QAction *action)
{
    Generators gen(action->data().toString(), this);
    if (gen.exec() == QDialog::Accepted) {
        pCore->activeBin()->slotAddClipToProject(gen.getSavedClip());
    }
}

void MainWindow::saveProperties(KConfigGroup &config)
{
    // save properties here
    KXmlGuiWindow::saveProperties(config);
    // TODO: fix session management
    if (qApp->isSavingSession() && pCore->projectManager()) {
        if (pCore->currentDoc() && !pCore->currentDoc()->url().isEmpty()) {
            config.writeEntry("kdenlive_lastUrl", pCore->currentDoc()->url().toLocalFile());
        }
    }
}

void MainWindow::saveNewToolbarConfig()
{
    // Sync current changes
    KXmlGuiWindow::saveNewToolbarConfig();
    // All dynamically inserted actions are removed by the save toolbar
    // So re-add them manually....
    loadDockActions();
    loadClipActions();
    loadContainerActions();
    for (auto &bin : m_binWidgets) {
        bin->setupGeneratorMenu();
    }

    // hack to be able to insert the hamburger menu at the first position
    QAction *const firstChild = toolBar()->actionAt(toolBar()->height() / 2, toolBar()->height() / 2);
    QAction *const separator = toolBar()->insertSeparator(firstChild);
    toolBar()->insertAction(separator, m_hamburgerMenu);
    m_hamburgerMenu->hideActionsOf(toolBar());
}

void MainWindow::slotReloadEffects(const QStringList &paths)
{
    for (const QString &p : paths) {
        EffectsRepository::get()->reloadCustom(p);
    }
    m_effectList2->reloadEffectMenu(m_effectsMenu, m_effectActions);
}

void MainWindow::configureNotifications()
{
    KNotifyConfigWidget::configure(this);
}

void MainWindow::slotFullScreen()
{
    KToggleFullScreenAction::setFullScreen(this, actionCollection()->action(QStringLiteral("fullscreen"))->isChecked());
}

void MainWindow::slotConnectMonitors()
{
    // connect(m_projectList, SIGNAL(deleteProjectClips(QStringList,QMap<QString,QString>)), this,
    // SLOT(slotDeleteProjectClips(QStringList,QMap<QString,QString>)));
    connect(m_projectMonitor, &Monitor::createSplitOverlay, this, &MainWindow::createSplitOverlay, Qt::DirectConnection);
    connect(m_projectMonitor, &Monitor::removeSplitOverlay, this, &MainWindow::removeSplitOverlay, Qt::DirectConnection);
}

void MainWindow::createSplitOverlay(std::shared_ptr<Mlt::Filter> filter)
{
    if (m_assetPanel->effectStackOwner().type == KdenliveObjectType::TimelineClip) {
        getCurrentTimeline()->controller()->createSplitOverlay(m_assetPanel->effectStackOwner().itemId, filter);
        m_projectMonitor->activateSplit();
    } else {
        pCore->displayMessage(i18n("Select a clip to compare effect"), ErrorMessage);
    }
}

void MainWindow::removeSplitOverlay()
{
    getCurrentTimeline()->controller()->removeSplitOverlay();
}

void MainWindow::addAction(const QString &name, QAction *action, const QKeySequence &shortcut, KActionCategory *category)
{
    m_actionNames.append(name);
    if (category) {
        category->addAction(name, action);
    } else {
        actionCollection()->addAction(name, action);
    }
    actionCollection()->setDefaultShortcut(action, shortcut);
}

void MainWindow::addAction(const QString &name, QAction *action, const QKeySequence &shortcut, const QString &category)
{
    addAction(name, action, shortcut, kdenliveCategoryMap.value(category, nullptr));
}

QAction *MainWindow::addAction(const QString &name, const QString &text, const QObject *receiver, const char *member, const QIcon &icon,
                               const QKeySequence &shortcut, KActionCategory *category)
{
    auto *action = new QAction(text, this);
    if (!icon.isNull()) {
        action->setIcon(icon);
    }
    addAction(name, action, shortcut, category);
    connect(action, SIGNAL(triggered(bool)), receiver, member);

    return action;
}

QAction *MainWindow::addAction(const QString &name, const QString &text, const QObject *receiver, const char *member, const QIcon &icon,
                               const QKeySequence &shortcut, const QString &category)
{
    return addAction(name, text, receiver, member, icon, shortcut, kdenliveCategoryMap.value(category, nullptr));
}

void MainWindow::addActionWithShortcuts(const QString &name, QAction *action, const QList<QKeySequence> &shortcuts, KActionCategory *category)
{
    m_actionNames.append(name);
    if (category) {
        category->addAction(name, action);
    } else {
        actionCollection()->addAction(name, action);
    }
    actionCollection()->setDefaultShortcuts(action, shortcuts);
}

QAction *MainWindow::addActionWithShortcuts(const QString &name, const QString &text, const QObject *receiver, const char *member, const QIcon &icon,
                                            const QList<QKeySequence> &shortcuts, KActionCategory *category)
{
    auto *action = new QAction(text, this);
    if (!icon.isNull()) {
        action->setIcon(icon);
    }
    addActionWithShortcuts(name, action, shortcuts, category);
    connect(action, SIGNAL(triggered(bool)), receiver, member);
    return action;
}

void MainWindow::setupActions()
{
    // create edit mode buttons
    // TODO: remove icon check ones we require KF > 6.1
    QString normalEditIconName =
        QIcon::hasThemeIcon(QStringLiteral("timeline-mode-normal")) ? QStringLiteral("timeline-mode-normal") : QStringLiteral("kdenlive-normal-edit");
    m_normalEditTool = new QAction(QIcon::fromTheme(normalEditIconName), i18n("Normal Mode"), this);
    m_normalEditTool->setCheckable(true);
    m_normalEditTool->setChecked(true);

    // TODO: remove icon check ones we require KF > 6.1
    QString overwriteEditIconName =
        QIcon::hasThemeIcon(QStringLiteral("timeline-mode-overwrite")) ? QStringLiteral("timeline-mode-overwrite") : QStringLiteral("kdenlive-overwrite-edit");
    m_overwriteEditTool = new QAction(QIcon::fromTheme(overwriteEditIconName), i18n("Overwrite Mode"), this);
    m_overwriteEditTool->setCheckable(true);
    m_overwriteEditTool->setChecked(false);

    // TODO: remove icon check ones we require KF > 6.1
    QString insertEditIconName =
        QIcon::hasThemeIcon(QStringLiteral("timeline-mode-insert")) ? QStringLiteral("timeline-mode-insert") : QStringLiteral("kdenlive-insert-edit");
    m_insertEditTool = new QAction(QIcon::fromTheme(insertEditIconName), i18n("Insert Mode"), this);
    m_insertEditTool->setCheckable(true);
    m_insertEditTool->setChecked(false);

    KSelectAction *sceneMode = new KSelectAction(i18n("Timeline Edit Mode"), this);
    sceneMode->setWhatsThis(
        xi18nc("@info:whatsthis", "Switches between Normal, Overwrite and Insert Mode. Determines the default action when handling clips in the timeline."));
    sceneMode->addAction(m_normalEditTool);
    sceneMode->addAction(m_overwriteEditTool);
    sceneMode->addAction(m_insertEditTool);
    sceneMode->setCurrentItem(0);
    connect(sceneMode, &KSelectAction::actionTriggered, this, &MainWindow::slotChangeEdit);
    addAction(QStringLiteral("timeline_mode"), sceneMode);
    actionCollection()->setShortcutsConfigurable(sceneMode, false);

    m_useTimelineZone = new KDualAction(i18n("Do not Use Timeline Zone for Insert"), i18n("Use Timeline Zone for Insert"), this);
    m_useTimelineZone->setWhatsThis(xi18nc("@info:whatsthis", "Toggles between using the timeline zone for inserting (on) or not (off)."));
    m_useTimelineZone->setActiveIcon(QIcon::fromTheme(QStringLiteral("timeline-use-zone-on")));
    m_useTimelineZone->setInactiveIcon(QIcon::fromTheme(QStringLiteral("timeline-use-zone-off")));
    m_useTimelineZone->setAutoToggle(true);
    connect(m_useTimelineZone, &KDualAction::activeChangedByUser, this, &MainWindow::slotSwitchTimelineZone);
    addAction(QStringLiteral("use_timeline_zone_in_edit"), m_useTimelineZone);

    m_compositeAction = new QAction(i18n("Enable Track Compositing"), this);
    m_compositeAction->setCheckable(true);
    connect(m_compositeAction, &QAction::triggered, this, &MainWindow::slotUpdateCompositing);
    addAction(QStringLiteral("timeline_compositing"), m_compositeAction);
    actionCollection()->setShortcutsConfigurable(m_compositeAction, false);

    QAction *splitView = new QAction(QIcon::fromTheme(QStringLiteral("view-split-top-bottom")), i18n("Split Audio Tracks"), this);
    addAction(QStringLiteral("timeline_view_split"), splitView);
    splitView->setData(QVariant::fromValue(1));
    splitView->setCheckable(true);
    splitView->setChecked(KdenliveSettings::audiotracksbelow() == 1);

    QAction *splitView2 = new QAction(QIcon::fromTheme(QStringLiteral("view-split-top-bottom")), i18n("Split Audio Tracks (reverse)"), this);
    addAction(QStringLiteral("timeline_view_split_reverse"), splitView2);
    splitView2->setData(QVariant::fromValue(2));
    splitView2->setCheckable(true);
    splitView2->setChecked(KdenliveSettings::audiotracksbelow() == 2);

    QAction *mixedView = new QAction(QIcon::fromTheme(QStringLiteral("document-new")), i18n("Mixed Audio tracks"), this);
    addAction(QStringLiteral("timeline_mixed_view"), mixedView);
    mixedView->setData(QVariant::fromValue(0));
    mixedView->setCheckable(true);
    mixedView->setChecked(KdenliveSettings::audiotracksbelow() == 0);

    auto *clipTypeGroup = new QActionGroup(this);
    clipTypeGroup->addAction(mixedView);
    clipTypeGroup->addAction(splitView);
    clipTypeGroup->addAction(splitView2);
    connect(clipTypeGroup, &QActionGroup::triggered, this, &MainWindow::slotUpdateTimelineView);

    // Create audio waveform zoom actions
    m_audioZoomIn = new QAction(QIcon::fromTheme(QStringLiteral("zoom-in")), i18n("Zoom In Audio Waveforms"), this);
    m_audioZoomOut = new QAction(QIcon::fromTheme(QStringLiteral("zoom-out")), i18n("Zoom Out Audio Waveforms"), this);
    m_audioZoomReset = new QAction(QIcon::fromTheme(QStringLiteral("zoom-original")), i18n("Reset Audio Waveform Zoom"), this);
    m_audioZoomCycle = new QAction(QIcon::fromTheme(QStringLiteral("zoom-1-to-2")), i18n("Cycle Audio Waveform Zoom Levels"), this);
    connect(m_audioZoomIn, &QAction::triggered, this, &MainWindow::slotAudioZoomIn);
    connect(m_audioZoomOut, &QAction::triggered, this, &MainWindow::slotAudioZoomOut);
    connect(m_audioZoomReset, &QAction::triggered, this, &MainWindow::slotAudioZoomReset);
    connect(m_audioZoomCycle, &QAction::triggered, this, &MainWindow::slotAudioZoomCycle);
    addAction(QStringLiteral("zoom_audio_in"), m_audioZoomIn);
    addAction(QStringLiteral("zoom_audio_out"), m_audioZoomOut);
    addAction(QStringLiteral("zoom_audio_reset"), m_audioZoomReset);
    addAction(QStringLiteral("zoom_audio_thumbs"),
              m_audioZoomCycle); // kept action name zoom_audio_thumbs for backwards compatibility before in/out/reset were introduced

    m_timeFormatButton = new KSelectAction(QStringLiteral("00:00:00:00 / 00:00:00:00"), this);
    m_timeFormatButton->setFont(KoraCutFonts::numbers());
    m_timeFormatButton->addAction(i18n("hh:mm:ss:ff"));
    m_timeFormatButton->addAction(i18n("Frames"));
    if (KdenliveSettings::frametimecode()) {
        m_timeFormatButton->setCurrentItem(1);
    } else {
        m_timeFormatButton->setCurrentItem(0);
    }
    connect(m_timeFormatButton, &KSelectAction::indexTriggered, this, &MainWindow::slotUpdateTimecodeFormat);
    m_timeFormatButton->setToolBarMode(KSelectAction::MenuMode);
    m_timeFormatButton->setToolButtonPopupMode(QToolButton::InstantPopup);
    addAction(QStringLiteral("timeline_timecode"), m_timeFormatButton);
    actionCollection()->setShortcutsConfigurable(m_timeFormatButton, false);

    m_buttonSubtitleEditTool = new QAction(QIcon::fromTheme(QStringLiteral("add-subtitle")), i18n("Edit Subtitle Tool"), this);
    m_buttonSubtitleEditTool->setWhatsThis(xi18nc("@info:whatsthis", "Toggles the subtitle track in the timeline."));
    m_buttonSubtitleEditTool->setCheckable(true);
    m_buttonSubtitleEditTool->setChecked(false);
    addAction(QStringLiteral("subtitle_tool"), m_buttonSubtitleEditTool);
    connect(m_buttonSubtitleEditTool, &QAction::triggered, this, &MainWindow::slotShowSubtitles);

    // create tools buttons
    m_buttonSelectTool = new QAction(QIcon::fromTheme(QStringLiteral("cursor-arrow")), i18n("Selection Tool"), this);
    // toolbar->addAction(m_buttonSelectTool);
    m_buttonSelectTool->setCheckable(true);
    m_buttonSelectTool->setChecked(true);

    m_buttonRazorTool = new QAction(QIcon::fromTheme(QStringLiteral("edit-cut")), i18n("Razor Tool"), this);
    // toolbar->addAction(m_buttonRazorTool);
    m_buttonRazorTool->setCheckable(true);
    m_buttonRazorTool->setChecked(false);

    m_buttonSpacerTool = new QAction(QIcon::fromTheme(QStringLiteral("distribute-horizontal-x")), i18n("Spacer Tool"), this);
    m_buttonSpacerTool->setWhatsThis(
        xi18nc("@info:whatsthis",
               "When selected, clicking and dragging the mouse in the timeline temporarily groups separate clips and creates or removes space between clips."));
    // toolbar->addAction(m_buttonSpacerTool);
    m_buttonSpacerTool->setCheckable(true);
    m_buttonSpacerTool->setChecked(false);

    m_buttonRippleTool = new QAction(QIcon::fromTheme(QStringLiteral("kdenlive-ripple")), i18n("Ripple Tool"), this);
    m_buttonRippleTool->setWhatsThis(
        xi18nc("@info:whatsthis",
               "When selected, dragging the edges of a clip lengthens or shortens the clip and moves adjacent clips back and forth while doing that."));
    m_buttonRippleTool->setCheckable(true);
    m_buttonRippleTool->setChecked(false);

    /* TODO Implement Roll
    m_buttonRollTool = new QAction(QIcon::fromTheme(QStringLiteral("kdenlive-rolling")), i18n("Roll Tool"), this);

    m_buttonRollTool->setCheckable(true);
    m_buttonRollTool->setChecked(false);*/

    m_buttonSlipTool = new QAction(QIcon::fromTheme(QStringLiteral("kdenlive-slip")), i18n("Slip Tool"), this);
    m_buttonSlipTool->setWhatsThis(xi18nc("@info:whatsthis", "When selected, dragging a clip slips the clip beneath the given window back and forth."));
    m_buttonSlipTool->setCheckable(true);
    m_buttonSlipTool->setChecked(false);

    m_buttonMulticamTool = new QAction(QIcon::fromTheme(QStringLiteral("view-split-left-right")), i18n("Multicam Tool"), this);
    m_buttonMulticamTool->setCheckable(true);
    m_buttonMulticamTool->setChecked(false);

    /* TODO Implement Slide
    m_buttonSlideTool = new QAction(QIcon::fromTheme(QStringLiteral("kdenlive-slide")), i18n("Slide Tool"), this);
    m_buttonSlideTool->setCheckable(true);
    m_buttonSlideTool->setChecked(false);*/

    auto *toolGroup = new QActionGroup(this);
    toolGroup->addAction(m_buttonSelectTool);
    toolGroup->addAction(m_buttonRazorTool);
    toolGroup->addAction(m_buttonSpacerTool);
    toolGroup->addAction(m_buttonRippleTool);
    // toolGroup->addAction(m_buttonRollTool);
    toolGroup->addAction(m_buttonSlipTool);
    // toolGroup->addAction(m_buttonSlideTool);
    toolGroup->addAction(m_buttonMulticamTool);

    toolGroup->setExclusive(true);

    QAction *collapseItem = new QAction(QIcon::fromTheme(QStringLiteral("collapse-all")), i18n("Collapse/Expand Item"), this);
    addAction(QStringLiteral("collapse_expand"), collapseItem, Qt::Key_Less);
    connect(collapseItem, &QAction::triggered, this, &MainWindow::slotCollapse);

    QAction *collapseAllItems = new QAction(QIcon::fromTheme(QStringLiteral("collapse-all")), i18n("Collapse/Expand All Items"), this);
    addAction(QStringLiteral("collapse_expand_all"), collapseAllItems, QKeySequence(Qt::SHIFT | Qt::Key_Less));
    connect(collapseAllItems, &QAction::triggered, this, &MainWindow::slotCollapseAll);

    QAction *sameTrack = new QAction(QIcon::fromTheme(QStringLiteral("composite-track-preview")), i18n("Mix Clips"), this);
    sameTrack->setWhatsThis(
        xi18nc("@info:whatsthis", "Creates a same-track transition between the selected clip and the adjacent one closest to the playhead."));
    addAction(QStringLiteral("mix_clip"), sameTrack, Qt::Key_U);
    connect(sameTrack, &QAction::triggered, this, [this]() { getCurrentTimeline()->controller()->mixClip(); });

    // toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    connect(toolGroup, &QActionGroup::triggered, this, &MainWindow::slotChangeTool);

    m_buttonVideoThumbs = new QAction(QIcon::fromTheme(QStringLiteral("kdenlive-show-video")), i18n("Show Video Thumbnails"), this);
    m_buttonVideoThumbs->setWhatsThis(xi18nc("@info:whatsthis", "Toggles the display of video thumbnails for the clips in the timeline (default is On)."));

    m_buttonVideoThumbs->setCheckable(true);
    m_buttonVideoThumbs->setChecked(KdenliveSettings::videothumbnails());
    connect(m_buttonVideoThumbs, &QAction::triggered, this, &MainWindow::slotSwitchVideoThumbs);

    // TODO: remove icon check ones we require KF > 6.1
    QString waveformIconName = QIcon::hasThemeIcon(QStringLiteral("waveform")) ? QStringLiteral("waveform") : QStringLiteral("kdenlive-show-audiothumb");
    m_buttonAudioThumbs = new QAction(QIcon::fromTheme(waveformIconName), i18n("Show Audio Thumbnails"), this);
    m_buttonAudioThumbs->setWhatsThis(xi18nc("@info:whatsthis", "Toggles the display of audio thumbnails for the clips in the timeline (default is On)."));

    m_buttonAudioThumbs->setCheckable(true);
    m_buttonAudioThumbs->setChecked(KdenliveSettings::audiothumbnails());
    connect(m_buttonAudioThumbs, &QAction::triggered, this, &MainWindow::slotSwitchAudioThumbs);

    // Create split button for audio thumbnails with zoom controls
    auto *audioThumbsButton = new QToolButton(this);
    audioThumbsButton->setDefaultAction(m_buttonAudioThumbs);
    audioThumbsButton->setPopupMode(QToolButton::MenuButtonPopup);
    audioThumbsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // Set initial icon colorization based on current zoom level
    updateAudioWaveformActionIcon();

    // Create menu for zoom controls
    m_audioThumbsMenu = new QMenu(this);
    QWidget *statusAudioZoomWidget = new QWidget();
    QVBoxLayout *statusAudioZoomLayout = new QVBoxLayout(statusAudioZoomWidget);
    statusAudioZoomLayout->setContentsMargins(2, 2, 2, 2);
    statusAudioZoomLayout->setSpacing(2);

    // Current zoom level (top)
    m_statusZoomLevelButton = new QPushButton(i18n("%1×", KdenliveSettings::waveformScaler()));
    QFont statusBoldFont = m_statusZoomLevelButton->font();
    statusBoldFont.setBold(true);
    m_statusZoomLevelButton->setFont(statusBoldFont);
    applyZoomLevelButtonStyling();
    statusAudioZoomLayout->addWidget(m_statusZoomLevelButton);

    // Separator between label and buttons
    QFrame *statusZoomSeparator = new QFrame();
    statusZoomSeparator->setFrameShape(QFrame::HLine);
    statusZoomSeparator->setFixedHeight(1);
    statusZoomSeparator->setLineWidth(1);
    statusAudioZoomLayout->addWidget(statusZoomSeparator);

    // Zoom in button
    QToolButton *statusZoomInButton = new QToolButton();
    statusZoomInButton->setIcon(QIcon::fromTheme(QStringLiteral("zoom-in")));
    statusZoomInButton->setToolTip(i18n("Zoom In Audio Waveforms"));
    statusZoomInButton->setAutoRaise(true);
    connect(statusZoomInButton, &QToolButton::clicked, this, &MainWindow::slotAudioZoomIn);
    statusAudioZoomLayout->addWidget(statusZoomInButton);

    // Zoom out button (bottom)
    QToolButton *statusZoomOutButton = new QToolButton();
    statusZoomOutButton->setIcon(QIcon::fromTheme(QStringLiteral("zoom-out")));
    statusZoomOutButton->setToolTip(i18n("Zoom Out Audio Waveforms"));
    statusZoomOutButton->setAutoRaise(true);
    connect(statusZoomOutButton, &QToolButton::clicked, this, &MainWindow::slotAudioZoomOut);
    statusAudioZoomLayout->addWidget(statusZoomOutButton);

    // Set initial button states
    statusZoomInButton->setEnabled(KdenliveSettings::waveformScaler() < 8);
    statusZoomOutButton->setEnabled(KdenliveSettings::waveformScaler() > 1);
    // Helper function to update button state based on zoom level (click to reset zoom)
    auto updateZoomButtonState = [this]() {
        if (KdenliveSettings::waveformScaler() > 1) {
            m_statusZoomLevelButton->setToolTip(i18n("Click to reset Audio Waveform Zoom"));
            m_statusZoomLevelButton->setEnabled(true);
            // Ensure the connection exists
            disconnect(m_statusZoomLevelButton, &QPushButton::clicked, this, &MainWindow::slotAudioZoomReset);
            connect(m_statusZoomLevelButton, &QPushButton::clicked, this, &MainWindow::slotAudioZoomReset);
        } else {
            m_statusZoomLevelButton->setToolTip(i18n("Current Audio Waveform Zoom Level"));
            m_statusZoomLevelButton->setEnabled(false);
            disconnect(m_statusZoomLevelButton, &QPushButton::clicked, this, &MainWindow::slotAudioZoomReset);
        }
    };
    updateZoomButtonState();

    auto *statusAudioZoomAction = new QWidgetAction(this);
    statusAudioZoomAction->setDefaultWidget(statusAudioZoomWidget);
    m_audioThumbsMenu->addAction(statusAudioZoomAction);

    // Connect to update zoom level display when it changes
    connect(KdenliveSettings::self(), &KdenliveSettings::waveformScalerChanged, this, [this, statusZoomInButton, statusZoomOutButton, updateZoomButtonState]() {
        m_statusZoomLevelButton->setText(i18n("%1×", KdenliveSettings::waveformScaler()));
        // Update button states based on zoom level
        statusZoomInButton->setEnabled(KdenliveSettings::waveformScaler() < 8);
        statusZoomOutButton->setEnabled(KdenliveSettings::waveformScaler() > 1);
        // Enable or disable the reset zoom button
        updateZoomButtonState();
        updateAudioWaveformActionIcon();
    });

    audioThumbsButton->setMenu(m_audioThumbsMenu);

    // Create widget action for the split button
    auto *audioThumbsButtonAction = new QWidgetAction(this);
    audioThumbsButtonAction->setDefaultWidget(audioThumbsButton);
    audioThumbsButtonAction->setText(i18n("Audio Thumbnails"));
    audioThumbsButtonAction->setIcon(QIcon::fromTheme(waveformIconName));

    m_buttonShowMarkers = new QAction(QIcon::fromTheme(QStringLiteral("kdenlive-show-markers")), i18n("Show Markers Comments"), this);

    m_buttonShowMarkers->setCheckable(true);
    m_buttonShowMarkers->setChecked(KdenliveSettings::showmarkers());
    connect(m_buttonShowMarkers, &QAction::triggered, this, &MainWindow::slotSwitchMarkersComments);

    m_buttonSnap = new QAction(QIcon::fromTheme(QStringLiteral("snap")), i18n("Snap"), this);
    m_buttonSnap->setWhatsThis(xi18nc("@info:whatsthis", "Toggles the snap function (clips snap to playhead, edges, markers, guides and others)."));
    m_buttonSnap->setCheckable(true);
    m_buttonSnap->setChecked(KdenliveSettings::snaptopoints());
    connect(m_buttonSnap, &QAction::triggered, this, &MainWindow::slotSwitchSnap);

    m_buttonHideClipOverlays = new QAction(QIcon::fromTheme(QStringLiteral("draw-text")), i18n("Show Clip Names"), this);
    m_buttonHideClipOverlays->setWhatsThis(xi18nc("@info:whatsthis", "Toggles the display of clip names and info in timeline."));
    m_buttonHideClipOverlays->setCheckable(true);
    m_buttonHideClipOverlays->setChecked(KdenliveSettings::showClipOverlays());
    connect(m_buttonHideClipOverlays, &QAction::triggered, this, &MainWindow::slotSwitchClipOverlays);

    m_buttonMouseZoomOnPlayhead = new QAction(QIcon::fromTheme(QStringLiteral("zoom-original")), i18n("Mouse Zoom on Playhead"), this);
    m_buttonMouseZoomOnPlayhead->setWhatsThis(xi18nc("@info:whatsthis", "Toggles the mouse zoom on playhead feature (default is Off)."));

    m_buttonMouseZoomOnPlayhead->setCheckable(true);
    m_buttonMouseZoomOnPlayhead->setChecked(KdenliveSettings::timelinemousezoomonplayhead());
    connect(m_buttonMouseZoomOnPlayhead, &QAction::triggered, this, &MainWindow::slotMouseZoomOnPlayhead);

    m_buttonTimelineTags = new QAction(QIcon::fromTheme(QStringLiteral("tag")), i18n("Show Color Tags in Timeline"), this);
    m_buttonTimelineTags->setWhatsThis(xi18nc("@info:whatsthis", "Toggles the display of clip tags in the timeline (default is On)."));

    m_buttonTimelineTags->setCheckable(true);
    m_buttonTimelineTags->setChecked(KdenliveSettings::tagsintimeline());
    connect(m_buttonTimelineTags, &QAction::triggered, this, &MainWindow::slotShowTimelineTags);

    m_buttonFitZoom = new QAction(QIcon::fromTheme(QStringLiteral("zoom-fit-best")), i18n("Fit Zoom to Project"), this);
    m_buttonFitZoom->setWhatsThis(xi18nc("@info:whatsthis", "Adjusts the zoom level to fit the entire project into the timeline windows."));

    m_buttonFitZoom->setCheckable(false);

    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(0, 20);
    m_zoomSlider->setPageStep(1);
    m_zoomSlider->setInvertedAppearance(true);
    m_zoomSlider->setInvertedControls(true);

    m_zoomSlider->setMaximumWidth(150);
    m_zoomSlider->setMinimumWidth(100);

    m_zoomIn = KStandardAction::zoomIn(this, SLOT(slotZoomIn()), actionCollection());
    m_zoomOut = KStandardAction::zoomOut(this, SLOT(slotZoomOut()), actionCollection());

    connect(m_zoomSlider, &QSlider::valueChanged, this, [&](int value) { slotSetZoom(value); });
    connect(m_zoomSlider, &QAbstractSlider::sliderMoved, this, &MainWindow::slotShowZoomSliderToolTip);
    connect(m_buttonFitZoom, &QAction::triggered, this, &MainWindow::slotFitZoom);

    KToolBar *toolbar = new KToolBar(QStringLiteral("statusToolBar"), this, Qt::BottomToolBarArea);
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    if (KdenliveSettings::gpu_accel()) {
        QLabel *warnLabel = new QLabel(i18n("Experimental GPU processing enabled - not for production"), this);
        warnLabel->setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
        warnLabel->setAlignment(Qt::AlignHCenter);
        warnLabel->setStyleSheet(QStringLiteral("QLabel { background-color :red; color:black;padding-left:2px;padding-right:2px}"));
        toolbar->addWidget(warnLabel);
    }

    m_trimLabel = new QLabel(QString(), this);
    m_trimLabel->setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    m_trimLabel->setAlignment(Qt::AlignHCenter);
    m_trimLabel->setMinimumWidth(m_trimLabel->fontMetrics().boundingRect(i18n("Multicam")).width() + 8);
    m_trimLabel->setToolTip(i18n("Active tool and editing mode"));

    toolbar->addWidget(m_trimLabel);
    // L'aide de l'outil en cours, à gauche, dès le lancement : avant, elle n'apparaissait qu'au
    // premier changement d'outil.
    QTimer::singleShot(0, this, &MainWindow::showToolMessage);
    toolbar->addSeparator();
    toolbar->addAction(m_buttonMouseZoomOnPlayhead);
    toolbar->addAction(m_buttonTimelineTags);
    toolbar->addAction(m_buttonVideoThumbs);
    toolbar->addAction(audioThumbsButtonAction);
    toolbar->addAction(m_buttonShowMarkers);
    toolbar->addAction(m_buttonHideClipOverlays);
    toolbar->addAction(m_buttonSnap);
    toolbar->addSeparator();
    toolbar->addAction(m_buttonFitZoom);
    toolbar->addAction(m_zoomOut);
    toolbar->addWidget(m_zoomSlider);
    toolbar->addAction(m_zoomIn);

    // KoraCut : barre d'état de 24 px (charte), icônes de barre d'outils à 14 px.
    statusBar()->setMaximumHeight(KoraCutMetrics::IconToolbar + 10);
    m_messageLabel = new StatusBarMessageLabel(this);
    m_messageLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    connect(this, &MainWindow::displayMessage, m_messageLabel, &StatusBarMessageLabel::setMessage);
    connect(this, &MainWindow::displaySelectionMessage, m_messageLabel, &StatusBarMessageLabel::setSelectionMessage);
    connect(this, &MainWindow::displayProgressMessage, m_messageLabel, &StatusBarMessageLabel::setProgressMessage);
    auto *engineDot = new EngineDot(this);
    statusBar()->addWidget(engineDot);
    connect(&pCore->taskManager, &TaskManager::jobCount, engineDot, [engineDot](int count) { engineDot->setBusy(count); }, Qt::QueuedConnection);
    connect(this, &MainWindow::displayMessage, engineDot, [engineDot](const QString &message, MessageType type, int) {
        if (type == MltError) {
            engineDot->setError(message);
        }
    });
    statusBar()->addWidget(m_messageLabel, 10);
    // KoraCut, 25 septembre 2026 : le montage visible. Pendant qu'un agent ou un programme monte, une
    // ligne dit ce qui se passe, avec « Accélérer » et la vitesse ; rien ne s'affiche le reste du temps.
    statusBar()->addPermanentWidget(KoraCutVisible::self()->createStatusWidget(this));
    // KoraCut, 23 septembre 2026 : la file de rendu dans la barre d'état, seulement quand elle n'est
    // pas vide (« 11 - Refonte UI-UX »). Un clic ouvre la fenêtre de rendu sur la file. RenderWidget
    // ne signale pas les changements de file : on la relit toutes les 1,5 s, ce qui ne coûte rien.
    // KoraCut, 23 septembre 2026 : les tâches de fond (proxys, analyses audio, transcodages) dans la
    // barre d'état, seulement quand il y en a (« 11 - Refonte UI-UX »). Le chutier les comptait déjà,
    // mais seulement quand son panneau est ouvert. Le menu permet de toutes les arrêter.
    auto *backgroundJobs = new QToolButton(this);
    backgroundJobs->setObjectName(QStringLiteral("koracutBackgroundJobs"));
    backgroundJobs->setAutoRaise(true);
    backgroundJobs->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    backgroundJobs->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    backgroundJobs->setPopupMode(QToolButton::InstantPopup);
    auto *jobsMenu = new QMenu(backgroundJobs);
    jobsMenu->addAction(QIcon::fromTheme(QStringLiteral("process-stop")), i18nc("@action", "Stop All Background Tasks"), this,
                        []() { pCore->taskManager.slotCancelJobs(); });
    backgroundJobs->setMenu(jobsMenu);
    backgroundJobs->hide();
    statusBar()->addPermanentWidget(backgroundJobs);
    connect(&pCore->taskManager, &TaskManager::jobCount, backgroundJobs, [backgroundJobs](int count) {
        backgroundJobs->setVisible(count > 0);
        backgroundJobs->setText(i18ncp("@info:status", "%1 task", "%1 tasks", count));
        backgroundJobs->setToolTip(i18ncp("@info:tooltip", "%1 background task in progress (proxies, analysis, transcoding)",
                                          "%1 background tasks in progress (proxies, analysis, transcoding)", count));
    }, Qt::QueuedConnection);

    auto *renderQueue = new QToolButton(this);
    renderQueue->setObjectName(QStringLiteral("koracutRenderQueue"));
    renderQueue->setAutoRaise(true);
    renderQueue->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    renderQueue->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    renderQueue->setToolTip(i18nc("@info:tooltip", "Open the render queue"));
    renderQueue->hide();
    statusBar()->addPermanentWidget(renderQueue);
    connect(renderQueue, &QToolButton::clicked, this, [this]() {
        slotRenderProject();
        if (m_renderWidget) {
            if (auto *tabs = m_renderWidget->findChild<QTabWidget *>()) {
                tabs->setCurrentIndex(1);
            }
        }
    });
    // KoraCut, 23 septembre 2026 : le format de la séquence, à droite (« 11 - Refonte UI-UX »,
    // barre d'état). Relu avec la file de rendu : il change rarement, et aucun signal ne l'annonce.
    auto *sequenceFormat = new QLabel(this);
    sequenceFormat->setObjectName(QStringLiteral("koracutSequenceFormat"));
    sequenceFormat->setContentsMargins(8, 0, 8, 0);
    sequenceFormat->setEnabled(false);
    statusBar()->addPermanentWidget(sequenceFormat);
    auto refreshFormat = [sequenceFormat]() {
        const std::unique_ptr<ProfileModel> &profile = pCore->getCurrentProfile();
        if (!profile) {
            sequenceFormat->clear();
            return;
        }
        const QString fps = QLocale().toString(profile->fps(), 'f', qFuzzyCompare(profile->fps(), qRound(profile->fps())) ? 0 : 2);
        const QString text = i18nc("@info:status width × height · frames per second", "%1 × %2 · %3 fps", QString::number(profile->width()), QString::number(profile->height()), fps);
        if (sequenceFormat->text() != text) {
            sequenceFormat->setText(text);
            sequenceFormat->setToolTip(profile->description());
        }
    };
    refreshFormat();
    auto *renderQueuePoll = new QTimer(this);
    renderQueuePoll->setInterval(1500);
    connect(renderQueuePoll, &QTimer::timeout, this, refreshFormat);
    connect(renderQueuePoll, &QTimer::timeout, this, [this, renderQueue]() {
        const int running = m_renderWidget ? m_renderWidget->runningJobsCount() : 0;
        const int waiting = m_renderWidget ? m_renderWidget->waitingJobsCount() : 0;
        if (running + waiting == 0) {
            renderQueue->hide();
            return;
        }
        QString text = i18ncp("@info barre d'état", "Rendering: %1 in progress", "Rendering: %1 in progress", running);
        if (waiting > 0) {
            text += i18ncp("@info barre d'état", ", %1 waiting", ", %1 waiting", waiting);
        }
        renderQueue->setText(text);
        renderQueue->show();
    });
    renderQueuePoll->start();
    statusBar()->addPermanentWidget(toolbar);
    // KoraCut, 23 septembre 2026 : le « ? » au bout de la barre d'état (« 11 - Refonte UI-UX »),
    // un accès court à l'aide. Le menu se remplit à l'ouverture : certaines de ces actions (aide,
    // raccourcis, « À propos ») ne sont créées qu'après la barre d'état, par KXMLGUI.
    auto *helpButton = new QToolButton(this);
    helpButton->setObjectName(QStringLiteral("koracutHelpButton"));
    helpButton->setAutoRaise(true);
    helpButton->setText(QStringLiteral("?"));
    helpButton->setToolTip(i18nc("@info:tooltip", "Help and keyboard shortcuts"));
    helpButton->setPopupMode(QToolButton::InstantPopup);
    auto *helpMenu = new QMenu(helpButton);
    helpButton->setMenu(helpMenu);
    connect(helpMenu, &QMenu::aboutToShow, this, [this, helpMenu]() {
        helpMenu->clear();
        const QStringList names = {QStringLiteral("help_contents"), QStringLiteral("koracut_command_palette"),
                                   QStringLiteral("options_configure_keybinding"), QStringLiteral("help_whats_this"), QString(),
                                   QStringLiteral("help_report_bug"), QStringLiteral("koracut_feedback"), QString(),
                                   QStringLiteral("koracut_support"), QStringLiteral("help_about_app")};
        for (const QString &name : names) {
            if (name.isEmpty()) {
                helpMenu->addSeparator();
            } else if (QAction *action = actionCollection()->action(name)) {
                helpMenu->addAction(action);
            }
        }
    });
    statusBar()->addPermanentWidget(helpButton);
    // KoraCut : 13 px et non 14 dans la barre d'état. Le bouton n'y fait que 15 px de haut (la
    // barre est bornée à 24 px par la charte) : avec une icône de 14, il ne reste pas la place du
    // trait d'accent de 2 px qui marque une bascule active, et Qt le laisse tomber sans rien dire.
    // 13 + 2 = 15, le trait s'affiche. Le cran 13 px existe déjà dans la charte.
    toolbar->setIconSize(KoraCutMetrics::iconInspector());
    toolbar->layout()->setContentsMargins(4, 0, 4, 0);
    // Le trait d'accent de 2 px sous une bascule active (23 septembre 2026).
    KoraCutStyle::addCheckedAccent(toolbar);
    statusBar()->setContentsMargins(0, 0, 0, 0);

    // KoraCut : ⌘⇧M bascule entre un moniteur à deux onglets et deux moniteurs côte à côte
    // (« 9 - UX de l'application »). L'action est une case à cocher, son état suit le réglage.
    QAction *dualMonitors = new QAction(i18nc("@action", "Two Monitors"), this);
    dualMonitors->setCheckable(true);
    dualMonitors->setChecked(!KdenliveSettings::singleMonitor());
    connect(dualMonitors, &QAction::triggered, this, [this](bool deux) { setSingleMonitor(!deux); });
    addAction(QStringLiteral("dual_monitors"), dualMonitors, Qt::CTRL | Qt::SHIFT | Qt::Key_M);
    // Sur un écran large, on propose une fois les deux moniteurs, sans les imposer (document 9).
    if (KdenliveSettings::singleMonitor() && !KdenliveSettings::koracut_dual_monitor_hint()) {
        QTimer::singleShot(6000, this, [this, dualMonitors]() {
            const QScreen *screen = this->screen();
            if (!screen || screen->availableGeometry().width() < 1600 || !KdenliveSettings::singleMonitor()) {
                return;
            }
            KdenliveSettings::setKoracut_dual_monitor_hint(true);
            pCore->displayMessage(i18nc("@info:status %1 is a keyboard shortcut", "Your screen is wide: %1 shows the two monitors side by side.",
                                        dualMonitors->shortcut().toString(QKeySequence::NativeText)),
                                  InformationMessage, 10000);
        });
    }

    addAction(QStringLiteral("normal_mode"), m_normalEditTool);
    addAction(QStringLiteral("overwrite_mode"), m_overwriteEditTool);
    addAction(QStringLiteral("insert_mode"), m_insertEditTool);

    KActionCategory *toolsActionCategory = new KActionCategory(i18n("Tools"), actionCollection());
    // KoraCut, 21 septembre 2026 : les outils prennent les touches de Resolve et de Premiere
    // (§ 2 de « 12 - Passe montage »). L'ancien jeu reste disponible dans le schéma « Classique ».
    addAction(QStringLiteral("select_tool"), m_buttonSelectTool, Qt::Key_A, toolsActionCategory);
    addAction(QStringLiteral("razor_tool"), m_buttonRazorTool, Qt::Key_B, toolsActionCategory);
    // KoraCut : « E » comme Espaceur, puisque « M » est passé au marqueur.
    addAction(QStringLiteral("spacer_tool"), m_buttonSpacerTool, Qt::Key_E, toolsActionCategory);
    addAction(QStringLiteral("ripple_tool"), m_buttonRippleTool, Qt::Key_T, toolsActionCategory);
    // addAction(QStringLiteral("roll_tool"), m_buttonRollTool, QKeySequence(), toolsActionCategory);
    addAction(QStringLiteral("slip_tool"), m_buttonSlipTool, Qt::Key_S, toolsActionCategory);
    addAction(QStringLiteral("multicam_tool"), m_buttonMulticamTool, {}, toolsActionCategory);
    // addAction(QStringLiteral("slide_tool"), m_buttonSlideTool);

    addAction(QStringLiteral("automatic_transition"), m_buttonTimelineTags);
    addAction(QStringLiteral("show_video_thumbs"), m_buttonVideoThumbs);
    addAction(QStringLiteral("show_audio_thumbs"), m_buttonAudioThumbs);
    addAction(QStringLiteral("show_markers"), m_buttonShowMarkers);
    addAction(QStringLiteral("snap"), m_buttonSnap, Qt::Key_N);
    addAction(QStringLiteral("hide_overlay"), m_buttonHideClipOverlays);
    // KoraCut : Maj+Z montre toute la séquence, comme Resolve et Final Cut (document 9).
    addAction(QStringLiteral("zoom_fit"), m_buttonFitZoom, Qt::SHIFT | Qt::Key_Z);

#if defined(Q_OS_WIN)
    int glBackend = KdenliveSettings::opengl_backend();
    QAction *openGLAuto = new QAction(i18n("Auto"), this);
    openGLAuto->setData(0);
    openGLAuto->setCheckable(true);
    openGLAuto->setChecked(glBackend == 0);

    QAction *openGLDesktop = new QAction(i18n("OpenGL"), this);
    openGLDesktop->setData(Qt::AA_UseDesktopOpenGL);
    openGLDesktop->setCheckable(true);
    openGLDesktop->setChecked(glBackend == Qt::AA_UseDesktopOpenGL);

    QAction *openGLES = new QAction(i18n("DirectX (ANGLE)"), this);
    openGLES->setData(Qt::AA_UseOpenGLES);
    openGLES->setCheckable(true);
    openGLES->setChecked(glBackend == Qt::AA_UseOpenGLES);

    QAction *openGLSoftware = new QAction(i18n("Software OpenGL"), this);
    openGLSoftware->setData(Qt::AA_UseSoftwareOpenGL);
    openGLSoftware->setCheckable(true);
    openGLSoftware->setChecked(glBackend == Qt::AA_UseSoftwareOpenGL);
    addAction(QStringLiteral("opengl_auto"), openGLAuto);
    addAction(QStringLiteral("opengl_desktop"), openGLDesktop);
    addAction(QStringLiteral("opengl_es"), openGLES);
    addAction(QStringLiteral("opengl_software"), openGLSoftware);
#endif

    addAction(QStringLiteral("run_wizard"), i18n("Run Config Wizard…"), this, SLOT(slotRunWizard()), QIcon::fromTheme(QStringLiteral("tools-wizard")));
    addAction(QStringLiteral("project_settings"), i18n("Project Settings…"), this, SLOT(slotEditProjectSettings()),
              QIcon::fromTheme(QStringLiteral("configure")));

    // KoraCut : l'export n'est plus un bouton de barre d'outils, c'est une commande de menu.
    // Ctrl+Entrée ne lance donc plus un rendu depuis n'importe où (décision du 20/09/2026, « 9 - UX de l'application.md »).
    addAction(QStringLiteral("project_render"), i18nc("@action ouvre la fenêtre d'export", "Export…"), this, SLOT(slotShowExportModule()),
              QIcon::fromTheme(QStringLiteral("document-export")), Qt::CTRL | Qt::SHIFT | Qt::Key_E);
    // Document 23 : la fenêtre d'export de toujours (tous les préréglages MLT, scripts), derrière « Avancé… ».
    addAction(QStringLiteral("koracut_export_avance"), i18nc("@action the full export window with every render preset", "Advanced Export…"), this,
              SLOT(slotRenderProject()), QIcon::fromTheme(QStringLiteral("document-export")));

    // Relance le dernier préréglage sans rouvrir la fenêtre.
    addAction(QStringLiteral("project_render_again"), i18nc("@action relance le dernier export", "Export Again"), this,
              SLOT(slotRenderAgain()), QIcon::fromTheme(QStringLiteral("document-export")), Qt::CTRL | Qt::ALT | Qt::Key_E);

    addAction(QStringLiteral("stop_project_render"), i18n("Stop Render"), this, SLOT(slotStopRenderProject()),
              QIcon::fromTheme(QStringLiteral("media-record")));

    addAction(QStringLiteral("project_clean"), i18n("Remove Unused Media"), this, SLOT(slotCleanProject()), QIcon::fromTheme(QStringLiteral("edit-clear-all")));

    QAction *resetAction = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Reset Configuration…"), this);
    addAction(QStringLiteral("reset_config"), resetAction);
    connect(resetAction, &QAction::triggered, this, [&]() { slotRestart(true); });

    m_playZoneFromCursor =
        addAction(QStringLiteral("monitor_play_zone_cursor"), i18n("Play Zone From Cursor"), pCore->monitorManager(), SLOT(slotPlayZoneFromCursor()),
                  QIcon::fromTheme(QStringLiteral("media-playback-start")), QKeySequence(), QStringLiteral("navandplayback"));

    m_playZone = addAction(QStringLiteral("monitor_play_zone"), i18n("Play Zone"), pCore->monitorManager(), SLOT(slotPlayZone()),
                           // KoraCut : Maj+Espace et non plus Ctrl+Espace, qui devient ⌘Espace sur Mac, c'est-à-dire Spotlight.
                           QIcon::fromTheme(QStringLiteral("media-playback-start")), Qt::SHIFT | Qt::Key_Space, QStringLiteral("navandplayback"));
    m_loopZone = addAction(QStringLiteral("monitor_loop_zone"), i18n("Loop Zone"), pCore->monitorManager(), SLOT(slotLoopZone()),
                           QIcon::fromTheme(QStringLiteral("media-playback-start")), Qt::CTRL | Qt::SHIFT | Qt::Key_Space, QStringLiteral("navandplayback"));
    m_loopClip = new QAction(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Loop Selected Clip"), this);
    addAction(QStringLiteral("monitor_loop_clip"), m_loopClip);
    m_loopClip->setEnabled(false);

    addAction(QStringLiteral("transcode_clip"), i18n("Transcode Clips…"), this, SLOT(slotTranscodeClip()), QIcon::fromTheme(QStringLiteral("edit-copy")));
    QAction *exportAction = new QAction(QIcon::fromTheme(QStringLiteral("document-export")), i18n("OpenTimelineIO E&xport…"), this);
    m_otioExport = new OtioExport(this);
    connect(exportAction, &QAction::triggered, m_otioExport, &OtioExport::slotExport);
    addAction(QStringLiteral("export_project"), exportAction);
    QAction *importAction = new QAction(QIcon::fromTheme(QStringLiteral("document-import")), i18n("OpenTimelineIO &Import…"), this);
    m_otioImport = new OtioImport(this);
    connect(importAction, &QAction::triggered, m_otioImport, &OtioImport::slotImport);
    addAction(QStringLiteral("import_project"), importAction);

    addAction(QStringLiteral("archive_project"), i18n("Archive Project…"), this, SLOT(slotArchiveProject()),
              QIcon::fromTheme(QStringLiteral("document-save-all")));
    // KoraCut : T passe à l'outil Trim.
    addAction(QStringLiteral("switch_monitor"), i18n("Switch Monitor"), this, SLOT(slotSwitchMonitors()));
    addAction(QStringLiteral("focus_timecode"), i18n("Focus Timecode"), this, SLOT(slotFocusTimecode()), QIcon(), Qt::Key_Equal);
    addAction(QStringLiteral("expand_timeline_clip"), i18n("Expand Clip"), this, SLOT(slotExpandClip()), QIcon::fromTheme(QStringLiteral("document-open")));

    QAction *overlayInfo = new QAction(QIcon::fromTheme(QStringLiteral("help-hint")), i18n("Monitor Info Overlay"), this);
    addAction(QStringLiteral("monitor_overlay"), overlayInfo, {}, QStringLiteral("monitor"));
    overlayInfo->setCheckable(true);
    overlayInfo->setData(Monitor::InfoOverlay);

    QAction *overlayTCInfo = new QAction(QIcon::fromTheme(QStringLiteral("help-hint")), i18n("Monitor Overlay Timecode"), this);
    addAction(QStringLiteral("monitor_overlay_tc"), overlayTCInfo, {}, QStringLiteral("monitor"));
    overlayTCInfo->setCheckable(true);
    overlayTCInfo->setData(Monitor::TimecodeOverlay);

    QAction *overlayFpsInfo = new QAction(QIcon::fromTheme(QStringLiteral("help-hint")), i18n("Monitor Overlay Playback Fps"), this);
    addAction(QStringLiteral("monitor_overlay_fps"), overlayFpsInfo, {}, QStringLiteral("monitor"));
    overlayFpsInfo->setCheckable(true);
    overlayFpsInfo->setData(Monitor::PlaybackFpsOverlay);

    QAction *overlayMarkerInfo = new QAction(QIcon::fromTheme(QStringLiteral("help-hint")), i18n("Monitor Overlay Markers"), this);
    addAction(QStringLiteral("monitor_overlay_markers"), overlayMarkerInfo, {}, QStringLiteral("monitor"));
    overlayMarkerInfo->setCheckable(true);
    overlayMarkerInfo->setData(Monitor::MarkersOverlay);

    QAction *overlayAudioInfo = new QAction(QIcon::fromTheme(QStringLiteral("help-hint")), i18n("Monitor Overlay Audio Waveform"), this);
    addAction(QStringLiteral("monitor_overlay_audiothumb"), overlayAudioInfo, {}, QStringLiteral("monitor"));
    overlayAudioInfo->setCheckable(true);
    overlayAudioInfo->setData(Monitor::AudioWaveformOverlay);

    QAction *overlayClipJobs = new QAction(QIcon::fromTheme(QStringLiteral("help-hint")), i18n("Monitor Overlay Clip Jobs"), this);
    addAction(QStringLiteral("monitor_overlay_clipjobs"), overlayClipJobs, {}, QStringLiteral("monitor"));
    overlayClipJobs->setCheckable(true);
    overlayClipJobs->setData(Monitor::ClipJobsOverlay);

    connect(overlayInfo, &QAction::toggled, this, [&, overlayTCInfo, overlayFpsInfo, overlayMarkerInfo, overlayAudioInfo, overlayClipJobs](bool toggled) {
        overlayTCInfo->setEnabled(toggled);
        overlayFpsInfo->setEnabled(toggled);
        overlayMarkerInfo->setEnabled(toggled);
        overlayAudioInfo->setEnabled(toggled);
        overlayClipJobs->setEnabled(toggled);
    });

    // Monitor resolution scaling
    KActionCategory *resolutionActionCategory = new KActionCategory(i18n("Preview Resolution"), actionCollection());
    m_scaleGroup = new QActionGroup(this);
    m_scaleGroup->setExclusive(true);
    m_scaleGroup->setEnabled(!KdenliveSettings::external_display());
    QAction *scale_no = new QAction(i18n("Full Resolution (1:1)"), m_scaleGroup);
    addAction(QStringLiteral("scale_no_preview"), scale_no, QKeySequence(), resolutionActionCategory);
    scale_no->setCheckable(true);
    scale_no->setData(0);
    QAction *scale_1 = new QAction(i18n("1080p"), m_scaleGroup);
    addAction(QStringLiteral("scale_1_preview"), scale_1, QKeySequence(), resolutionActionCategory);
    scale_1->setCheckable(true);
    scale_1->setData(1);
    QAction *scale_2 = new QAction(i18n("720p"), m_scaleGroup);
    addAction(QStringLiteral("scale_2_preview"), scale_2, QKeySequence(), resolutionActionCategory);
    scale_2->setCheckable(true);
    scale_2->setData(2);
    QAction *scale_4 = new QAction(i18n("540p"), m_scaleGroup);
    addAction(QStringLiteral("scale_4_preview"), scale_4, QKeySequence(), resolutionActionCategory);
    scale_4->setCheckable(true);
    scale_4->setData(4);
    QAction *scale_8 = new QAction(i18n("360p"), m_scaleGroup);
    addAction(QStringLiteral("scale_8_preview"), scale_8, QKeySequence(), resolutionActionCategory);
    scale_8->setCheckable(true);
    scale_8->setData(8);
    QAction *scale_16 = new QAction(i18n("270p"), m_scaleGroup);
    addAction(QStringLiteral("scale_16_preview"), scale_16, QKeySequence(), resolutionActionCategory);
    scale_16->setCheckable(true);
    scale_16->setData(16);
    connect(pCore->monitorManager(), &MonitorManager::scalingChanged, this, [scale_1, scale_2, scale_4, scale_8, scale_16, scale_no]() {
        switch (KdenliveSettings::previewScaling()) {
        case 1:
            scale_1->setChecked(true);
            break;
        case 2:
            scale_2->setChecked(true);
            break;
        case 4:
            scale_4->setChecked(true);
            break;
        case 8:
            scale_8->setChecked(true);
            break;
        case 16:
            scale_16->setChecked(true);
            break;
        default:
            scale_no->setChecked(true);
            break;
        }
    });
    Q_EMIT pCore->monitorManager()->scalingChanged();
    connect(m_scaleGroup, &QActionGroup::triggered, this, [](QAction *ac) {
        int scaling = ac->data().toInt();
        KdenliveSettings::setPreviewScaling(scaling);
        // Clear timeline selection so that any qml monitor scene is reset
        Q_EMIT pCore->monitorManager()->updatePreviewScaling();
    });

    QAction *dropFrames = new QAction(QIcon(), i18n("Real Time (drop frames)"), this);
    dropFrames->setCheckable(true);
    dropFrames->setChecked(KdenliveSettings::monitor_dropframes());
    addAction(QStringLiteral("mlt_realtime"), dropFrames);
    connect(dropFrames, &QAction::toggled, this, &MainWindow::slotSwitchDropFrames);

    QAction *insertBinZone = addAction(QStringLiteral("insert_project_tree"), i18n("Insert Zone in Project Bin"), this, SLOT(slotInsertZoneToTree()),
                                       QIcon::fromTheme(QStringLiteral("kdenlive-add-clip")), Qt::CTRL | Qt::Key_I);
    insertBinZone->setWhatsThis(xi18nc("@info:whatsthis", "Creates a new clip in the project bin from the defined zone."));

    // TODO: Make these 2 shortcuts context aware to avoid conflict with media browser
    addAction(QStringLiteral("monitor_seek_snap_backward"), i18n("Go to Previous Snap Point"), this, SLOT(slotSnapRewind()),
              QIcon::fromTheme(QStringLiteral("media-seek-backward")), Qt::ALT | Qt::Key_Left, QStringLiteral("navandplayback"));
    addAction(QStringLiteral("monitor_seek_snap_forward"), i18n("Go to Next Snap Point"), this, SLOT(slotSnapForward()),
              QIcon::fromTheme(QStringLiteral("media-seek-forward")), Qt::ALT | Qt::Key_Right, QStringLiteral("navandplayback"));

    addAction(QStringLiteral("seek_clip_start"), i18n("Go to Clip Start"), this, SLOT(slotClipStart()), QIcon::fromTheme(QStringLiteral("media-seek-backward")),
              Qt::Key_Home, QStringLiteral("navandplayback"));
    addAction(QStringLiteral("seek_clip_end"), i18n("Go to Clip End"), this, SLOT(slotClipEnd()), QIcon::fromTheme(QStringLiteral("media-seek-forward")),
              Qt::Key_End, QStringLiteral("navandplayback"));
    addAction(QStringLiteral("monitor_seek_guide_backward"), i18n("Go to Previous Marker"), this, SLOT(slotGuideRewind()),
              QIcon::fromTheme(QStringLiteral("media-seek-backward")), Qt::CTRL | Qt::Key_Left, QStringLiteral("navandplayback"));
    addAction(QStringLiteral("monitor_seek_guide_forward"), i18n("Go to Next Marker"), this, SLOT(slotGuideForward()),
              QIcon::fromTheme(QStringLiteral("media-seek-forward")), Qt::CTRL | Qt::Key_Right, QStringLiteral("navandplayback"));
    addAction(QStringLiteral("align_playhead"), i18n("Align Playhead to Mouse Position"), this, SLOT(slotAlignPlayheadToMousePos()), QIcon(), Qt::Key_P,
              QStringLiteral("navandplayback"));

    addAction(QStringLiteral("grab_item"), i18n("Grab Current Item"), this, SLOT(slotGrabItem()), QIcon::fromTheme(QStringLiteral("transform-move")),
              Qt::SHIFT | Qt::Key_G);

    // KoraCut : B passe à l'outil Lame. Écraser prend le point, et le point-virgule, qui est en
    // accès direct sur AZERTY (le point y demande Maj) ; insérer prend la virgule en plus du V.
    // La paire virgule / point remplace les crochets des logiciels américains (document 9).
    QAction *overwriteZone = addActionWithShortcuts(QStringLiteral("overwrite_to_in_point"), i18n("Overwrite Clip Zone in Timeline"), this,
                                                    SLOT(slotInsertClipOverwrite()), QIcon::fromTheme(QStringLiteral("timeline-overwrite")),
                                                    {QKeySequence(Qt::Key_Period), QKeySequence(Qt::Key_Semicolon)});
    overwriteZone->setWhatsThis(xi18nc("@info:whatsthis", "When clicked the zone of the clip currently selected in the project bin is inserted at the playhead "
                                                          "position in the active timeline. Clips at the insert position are cut and overwritten."));
    QAction *insertZone = addActionWithShortcuts(QStringLiteral("insert_to_in_point"), i18n("Insert Clip Zone in Timeline"), this, SLOT(slotInsertClipInsert()),
                                                 QIcon::fromTheme(QStringLiteral("timeline-insert")), {QKeySequence(Qt::Key_V), QKeySequence(Qt::Key_Comma)});
    insertZone->setWhatsThis(xi18nc("@info:whatsthis", "When clicked the zone of the clip currently selected in the project bin is inserted at the playhead "
                                                       "position in the active timeline. Clips at the insert position are cut and shifted to the right."));
    QAction *extractZone = addAction(QStringLiteral("remove_extract"), i18n("Extract Timeline Zone"), this, SLOT(slotExtractZone()),
                                     QIcon::fromTheme(QStringLiteral("timeline-extract")), Qt::SHIFT | Qt::Key_X);
    extractZone->setWhatsThis(xi18nc("@info:whatsthis", "Click to delete the timeline zone from the timeline. All clips to the right are shifted left."));
    QAction *liftZone = addAction(QStringLiteral("remove_lift"), i18n("Lift Timeline Zone"), this, SLOT(slotLiftZone()),
                                  QIcon::fromTheme(QStringLiteral("timeline-lift")), Qt::Key_Z);
    liftZone->setWhatsThis(xi18nc("@info:whatsthis", "Click to delete the timeline zone from the timeline. All clips to the right stay in position."));
    QAction *addPreviewZone = addAction(QStringLiteral("set_render_timeline_zone"), i18nc("@action", "Add Zone to Render"), this, SLOT(slotDefinePreviewRender()),
                                        QIcon::fromTheme(QStringLiteral("preview-add-zone")));
    addPreviewZone->setWhatsThis(xi18nc("@info:whatsthis", "Add the currently defined timeline/selection zone as a preview render zone"));
    QAction *removePreviewZone = addAction(QStringLiteral("unset_render_timeline_zone"), i18nc("@action", "Remove Zone from Render"), this, SLOT(slotRemovePreviewRender()),
                                           QIcon::fromTheme(QStringLiteral("preview-remove-zone")));
    removePreviewZone->setWhatsThis(xi18nc(
        "@info:whatsthis",
        "Removes the currently defined timeline/selection zone from the preview render zone. Note that this can leave gaps in the preview render zones."));
    // KoraCut (24 septembre 2026) : le rendu d'aperçu parle simplement, « Rendu », pas « prévisualisation ».
    QAction *removeAllPreviewZone = addAction(QStringLiteral("clear_render_timeline_zone"), i18nc("@action", "Clear Renders"), this,
                                              SLOT(slotClearPreviewRender()), QIcon::fromTheme(QStringLiteral("preview-remove-all")));
    removeAllPreviewZone->setWhatsThis(xi18nc("@info:whatsthis", "Deletes the rendered passages of this sequence from the disk."));
    QAction *startPreviewRender = addAction(QStringLiteral("prerender_timeline_zone"), i18nc("@action", "Render Zone"), this, SLOT(slotPreviewRender()),
                                            QIcon::fromTheme(QStringLiteral("preview-render-on")), QKeySequence(Qt::SHIFT | Qt::Key_Return));
    startPreviewRender->setWhatsThis(xi18nc("@info:whatsthis", "Renders the zone of the Program monitor, or the whole sequence if there is no zone, so that "
                                                               "effects and grading play smoothly. The render is redone by itself after each change."));
    addAction(QStringLiteral("stop_prerender_timeline"), i18nc("@action", "Stop Render"), this, SLOT(slotStopPreviewRender()),
              QIcon::fromTheme(QStringLiteral("preview-render-off")));
    QAction *renderSelection = new QAction(QIcon::fromTheme(QStringLiteral("preview-render-on")), i18nc("@action", "Render Selection"), this);
    renderSelection->setWhatsThis(xi18nc("@info:whatsthis", "Renders the passage covered by the selected clips."));
    connect(renderSelection, &QAction::triggered, this, [this]() {
        if (pCore->currentDoc() && getCurrentTimeline()) {
            TimelineController *controller = getCurrentTimeline()->controller();
            if (!controller->renderRange(controller->selectionZone())) {
                pCore->displayMessage(i18nc("@info:status", "Select one or more clips to render."), InformationMessage, 3000);
            }
        }
    });
    addAction(QStringLiteral("koracut_render_selection"), renderSelection);
    QAction *autoRenderAction = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action", "Automatic Render"), this);
    autoRenderAction->setCheckable(true);
    autoRenderAction->setChecked(KdenliveSettings::autopreview());
    autoRenderAction->setWhatsThis(xi18nc("@info:whatsthis", "One second after each change, the passages with effects, grading, compositions or "
                                                             "Motion are rendered again, from the playhead onwards. Simple cuts are not rendered."));
    connect(autoRenderAction, &QAction::triggered, this, &MainWindow::slotToggleAutoPreview);
    addAction(QStringLiteral("koracut_render_auto"), autoRenderAction);

    addAction(QStringLiteral("select_timeline_zone"), i18n("Adjust Timeline Zone to Selection"), this, SLOT(slotSelectTimelineZone()),
              // KoraCut : Maj+Z est passé à « Voir toute la séquence » ; l'action reste dans le menu de la règle.
              QIcon::fromTheme(QStringLiteral("edit-select")));
    addAction(QStringLiteral("select_timeline_clip"), i18n("Select Clip"), this, SLOT(slotSelectTimelineClip()),
              QIcon::fromTheme(QStringLiteral("edit-select")), Qt::Key_Plus);
    addAction(QStringLiteral("deselect_timeline_clip"), i18n("Deselect Clip"), this, SLOT(slotDeselectTimelineClip()),
              QIcon::fromTheme(QStringLiteral("edit-select")), Qt::Key_Minus);
    addAction(QStringLiteral("select_add_timeline_clip"), i18n("Add Clip to Selection"), this, SLOT(slotSelectAddTimelineClip()),
              QIcon::fromTheme(QStringLiteral("edit-select")), Qt::ALT | Qt::Key_Plus);
    addAction(QStringLiteral("select_timeline_transition"), i18n("Select Transition"), this, SLOT(slotSelectTimelineTransition()),
              QIcon::fromTheme(QStringLiteral("edit-select")), Qt::SHIFT | Qt::Key_Plus);
    addAction(QStringLiteral("deselect_timeline_transition"), i18n("Deselect Transition"), this, SLOT(slotDeselectTimelineTransition()),
              QIcon::fromTheme(QStringLiteral("edit-select")), Qt::SHIFT | Qt::Key_Minus);
    addAction(QStringLiteral("select_add_timeline_transition"), i18n("Add Transition to Selection"), this, SLOT(slotSelectAddTimelineTransition()),
              QIcon::fromTheme(QStringLiteral("edit-select")), Qt::ALT | Qt::SHIFT | Qt::Key_Plus);

    addAction(QStringLiteral("delete_all_clip_markers"), i18n("Delete All Markers"), this, SLOT(slotDeleteAllClipMarkers()),
              QIcon::fromTheme(QStringLiteral("edit-delete")));
    // KoraCut, 22 septembre 2026 : « M » pose un marqueur, comme dans CapCut et DaVinci.
    // L'ancien raccourci était « * » du pavé numérique, que les portables n'ont pas : il était
    // donc inatteignable sur un MacBook. On le garde en second, on ne retire rien.
    addActionWithShortcuts(QStringLiteral("add_marker_guide_quickly"), i18n("Add Marker Quickly"), this, SLOT(slotAddMarkerGuideQuickly()),
                           QIcon::fromTheme(QStringLiteral("bookmark-new")),
                           {QKeySequence(Qt::Key_M), QKeySequence(Qt::KeypadModifier | Qt::Key_Asterisk)});

    QAction *addMarkerWithCategory1 =
        addAction(QStringLiteral("add_marker_guide_1"), i18n("Add Marker Category 1"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_1), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory1->setData(1);
    QAction *addMarkerWithCategory2 =
        addAction(QStringLiteral("add_marker_guide_2"), i18n("Add Marker Category 2"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_2), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory2->setData(2);
    QAction *addMarkerWithCategory3 =
        addAction(QStringLiteral("add_marker_guide_3"), i18n("Add Marker Category 3"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_3), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory3->setData(3);
    QAction *addMarkerWithCategory4 =
        addAction(QStringLiteral("add_marker_guide_4"), i18n("Add Marker Category 4"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_4), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory4->setData(4);
    QAction *addMarkerWithCategory5 =
        addAction(QStringLiteral("add_marker_guide_5"), i18n("Add Marker Category 5"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_5), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory5->setData(5);
    QAction *addMarkerWithCategory6 =
        addAction(QStringLiteral("add_marker_guide_6"), i18n("Add Marker Category 6"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_6), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory6->setData(6);
    QAction *addMarkerWithCategory7 =
        addAction(QStringLiteral("add_marker_guide_7"), i18n("Add Marker Category 7"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_7), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory7->setData(7);
    QAction *addMarkerWithCategory8 =
        addAction(QStringLiteral("add_marker_guide_8"), i18n("Add Marker Category 8"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_8), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory8->setData(8);
    QAction *addMarkerWithCategory9 =
        addAction(QStringLiteral("add_marker_guide_9"), i18n("Add Marker Category 9"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_9), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory9->setData(9);
    QAction *addMarkerWithCategory10 =
        addAction(QStringLiteral("add_marker_guide_10"), i18n("Add Marker Category 10"), this, SLOT(slotAddMarkerWithCategory()),
                  QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(Qt::KeypadModifier | Qt::Key_0), QStringLiteral("guidecategorynumber"));
    addMarkerWithCategory10->setData(10);

    // Clip actions. We set some category info on the action data to enable/disable it contextually in timelinecontroller
    KActionCategory *clipActionCategory = new KActionCategory(i18n("Current Selection"), actionCollection());

    QAction *addMarker = addAction(QStringLiteral("add_clip_marker"), i18n("Add Marker"), this, SLOT(slotAddClipMarker()),
                                   QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(), clipActionCategory);
    addMarker->setData('P');

    QAction *delMarker = addAction(QStringLiteral("delete_clip_marker"), i18n("Delete Marker"), this, SLOT(slotDeleteClipMarker()),
                                   QIcon::fromTheme(QStringLiteral("bookmark-remove")), QKeySequence(), clipActionCategory);
    delMarker->setData('P');

    // KoraCut : « Maj+M » ouvre le marqueur sous la tête de lecture, le pendant du « M » qui le pose.
    QAction *editClipMarker = addAction(QStringLiteral("edit_clip_marker"), i18n("Edit Marker…"), this, SLOT(slotEditClipMarker()),
                                        QIcon::fromTheme(QStringLiteral("bookmark-edit")), QKeySequence(Qt::SHIFT | Qt::Key_M),
                                        clipActionCategory);
    editClipMarker->setObjectName(QStringLiteral("edit_marker"));
    editClipMarker->setData('P');

    QAction *splitAudio = addAction(QStringLiteral("clip_split"), i18n("Restore Audio"), this, SLOT(slotSplitAV()),
                                    QIcon::fromTheme(QStringLiteral("document-new")), QKeySequence(), clipActionCategory);
    // "S" will be handled specifically to change the action name depending on current selection
    splitAudio->setData('S');
    splitAudio->setEnabled(false);

    // KoraCut : ⇧⌫ autant que ⇧⌦, même raison que ci-dessus.
    QAction *extractClip = addActionWithShortcuts(
        QStringLiteral("extract_clip"), i18n("Extract Clip"), this, SLOT(slotExtractClip()), QIcon::fromTheme(QStringLiteral("timeline-extract")),
        {QKeySequence(Qt::SHIFT | Qt::Key_Delete), QKeySequence(Qt::SHIFT | Qt::Key_Backspace)}, clipActionCategory);
    extractClip->setData('C');
    extractClip->setEnabled(false);

    QAction *extractToBin =
        addAction(QStringLiteral("save_to_bin"), i18n("Save Clip Part to Bin"), this, SLOT(slotSaveZoneToBin()), QIcon(), QKeySequence(), clipActionCategory);
    extractToBin->setData('C');
    extractToBin->setEnabled(false);

    QAction *switchEnable =
        addAction(QStringLiteral("clip_switch"), i18n("Disable Clip"), this, SLOT(slotSwitchClip()), QIcon(), Qt::Key_D, clipActionCategory);
    // "W" will be handled specifically to change the action name depending on current selection
    switchEnable->setData('W');
    switchEnable->setEnabled(false);

    // Audio reference
    QAction *setAudioAlignReference = addAction(QStringLiteral("set_audio_align_ref"), i18n("Set Audio Reference"), this, SLOT(slotSetAudioAlignReference()),
                                                QIcon(), QKeySequence(), clipActionCategory);
    // "A" as data means this action should only be available for clips with audio
    setAudioAlignReference->setData('A');
    setAudioAlignReference->setEnabled(false);

    QAction *alignAudio =
        addAction(QStringLiteral("align_audio"), i18n("Align Audio to Reference"), this, SLOT(slotAlignAudio()), QIcon(), QKeySequence(), clipActionCategory);
    // "A" as data means this action should only be available for clips with audio
    // alignAudio->setData('A');
    alignAudio->setEnabled(false);

    // Timecode reference
    QAction *setTimecodeReference = addAction(QStringLiteral("set_timecode_ref"), i18n("Set Timecode Reference"), this, SLOT(slotSetTimecodeReference()),
                                              QIcon(), QKeySequence(), clipActionCategory);
    setTimecodeReference->setEnabled(false);
    QAction *alignTimecode = addAction(QStringLiteral("align_timecode"), i18n("Align Timecode to Reference"), this, SLOT(slotAlignTimecode()), QIcon(),
                                       QKeySequence(), clipActionCategory);
    alignTimecode->setEnabled(false);

    QAction *act = addAction(QStringLiteral("edit_item_duration"), i18n("Edit Duration"), this, SLOT(slotEditItemDuration()),
                             QIcon::fromTheme(QStringLiteral("measure")), QKeySequence(), clipActionCategory);
    act->setEnabled(false);

    act = addAction(QStringLiteral("edit_item_speed"), i18n("Change Speed"), this, SLOT(slotEditItemSpeed()), QIcon::fromTheme(QStringLiteral("speedometer")),
                    QKeySequence(), clipActionCategory);
    // "Q" as data means this action should only be available if the item is not endless and has no time remap
    act->setData('Q');
    act->setEnabled(false);

    act = addAction(QStringLiteral("edit_item_remap"), i18n("Time Remap"), this, SLOT(slotRemapItemTime()), QIcon::fromTheme(QStringLiteral("speedometer")),
                    QKeySequence(), clipActionCategory);
    // "R" as data means this action should only be available if the item is not endless and has no speed effect
    act->setData('R');
    act->setCheckable(true);
    act->setEnabled(false);

    act = addAction(QStringLiteral("clip_in_project_tree"), i18n("Clip in Project Bin"), this, SLOT(slotClipInProjectTree()),
                    QIcon::fromTheme(QStringLiteral("find-location")), QKeySequence(), clipActionCategory);
    act->setEnabled(false);
    // "C" as data means this action should only be available for clips - not for compositions
    act->setData('C');
    addAction(QStringLiteral("search_bin"), i18n("Search Bin Clip…"), this, SLOT(slotSearchBin()), QIcon::fromTheme(QStringLiteral("edit-find")));

    QAction *duplicateClip = addAction(QStringLiteral("duplicate_timeline_clip"), i18n("Duplicate Clip"), this, SLOT(slotDuplicateTimelineClip()),
                                       QIcon::fromTheme(QStringLiteral("edit-copy")), QKeySequence(Qt::CTRL | Qt::Key_D), clipActionCategory);
    duplicateClip->setData('C');
    duplicateClip->setEnabled(false);

    addAction(QStringLiteral("cut_timeline_clip"), i18n("Cut Clip"), this, SLOT(slotCutTimelineClip()), QIcon::fromTheme(QStringLiteral("edit-cut")),
              Qt::CTRL | Qt::Key_B);

    addAction(QStringLiteral("replace_timeline_clip"), i18n("Replace with Bin Selection"), this, SLOT(slotReplaceTimelineClip()),
              QIcon::fromTheme(QStringLiteral("edit-find-replace")), QKeySequence());
    act->setData('C');

    addAction(QStringLiteral("cut_timeline_all_clips"), i18n("Cut All Clips"), this, SLOT(slotCutTimelineAllClips()),
              QIcon::fromTheme(QStringLiteral("edit-cut")), Qt::CTRL | Qt::SHIFT | Qt::Key_B);

    // KoraCut, 22 septembre 2026 : les deux touches, pas une seule. Sur un clavier de MacBook,
    // la touche marquée « delete » est ⌫ (`Key_Backspace`) ; `Key_Delete` c'est ⌦, qui n'existe
    // qu'en fn + ⌫. L'action était donc inerte sous les doigts d'Amadou Ba.
    addActionWithShortcuts(QStringLiteral("delete_timeline_clip"), i18n("Delete Selected Item"), this, SLOT(slotDeleteItem()),
                           QIcon::fromTheme(QStringLiteral("edit-delete")), {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});

    // KoraCut, 23 septembre 2026 : décaler la sélection d'une image, à gauche ou à droite (⌥⇧←,
    // ⌥⇧→). Les autres logiciels prennent la virgule et le point (Resolve, Final Cut, pris ici par
    // insérer et écraser) ou ⌥← (Premiere, pris ici par « nœud précédent ») : on garde la flèche et
    // on ajoute Maj. Document 12, journal.
    for (int delta : {-1, 1}) {
        auto *nudge = new QAction(QIcon::fromTheme(delta < 0 ? QStringLiteral("go-previous") : QStringLiteral("go-next")),
                                  delta < 0 ? i18nc("@action", "Nudge Selection One Frame Left") : i18nc("@action", "Nudge Selection One Frame Right"), this);
        addAction(delta < 0 ? QStringLiteral("koracut_nudge_left") : QStringLiteral("koracut_nudge_right"), nudge,
                  QKeySequence(Qt::ALT | Qt::SHIFT | (delta < 0 ? Qt::Key_Left : Qt::Key_Right)), QStringLiteral("navandplayback"));
        connect(nudge, &QAction::triggered, this, [this, delta]() { koraCutNudgeSelection(delta); });
    }

    QAction *resizeStart = new QAction(QIcon(), i18n("Resize Item Start"), this);
    addAction(QStringLiteral("resize_timeline_clip_start"), resizeStart, QKeySequence(Qt::Key_ParenLeft));
    connect(resizeStart, &QAction::triggered, this, &MainWindow::slotResizeItemStart);

    // KoraCut, 21 septembre 2026, amendé le 22 : Q et W rognent jusqu'à la tête de lecture sans
    // refermer le trou, et ⇧Q / ⇧W referment. C'est un écart volontaire avec Premiere, où Q et W
    // referment toujours : Amadou Ba a essayé et n'en veut pas par défaut, « le reste de la
    // timeline se décale et vient se coller, alors que tu ne l'as pas déplacé manuellement ».
    // La règle de KoraCut devient : un geste simple ne déplace jamais ce qu'on n'a pas touché,
    // il faut ⇧ pour refermer. Voir « 13 - Deuxième passe montage », § 2.
    addAction(QStringLiteral("trim_start_to_playhead"), i18n("Trim Start to Playhead"), this, SLOT(slotTrimStartToPlayhead()),
              QIcon::fromTheme(QStringLiteral("edit-cut")), Qt::Key_Q);
    addAction(QStringLiteral("trim_end_to_playhead"), i18n("Trim End to Playhead"), this, SLOT(slotTrimEndToPlayhead()),
              QIcon::fromTheme(QStringLiteral("edit-cut")), Qt::Key_W);
    addAction(QStringLiteral("ripple_trim_start"), i18n("Ripple Trim Start to Playhead"), this, SLOT(slotRippleTrimStart()),
              QIcon::fromTheme(QStringLiteral("edit-cut")), Qt::SHIFT | Qt::Key_Q);
    addAction(QStringLiteral("ripple_trim_end"), i18n("Ripple Trim End to Playhead"), this, SLOT(slotRippleTrimEnd()),
              QIcon::fromTheme(QStringLiteral("edit-cut")), Qt::SHIFT | Qt::Key_W);

    QAction *resizeEnd = new QAction(QIcon(), i18n("Resize Item End"), this);
    addAction(QStringLiteral("resize_timeline_clip_end"), resizeEnd, QKeySequence(Qt::Key_ParenRight));
    connect(resizeEnd, &QAction::triggered, this, &MainWindow::slotResizeItemEnd);

    QAction *pasteEffects = addAction(QStringLiteral("paste_effects"), i18n("Paste Effects"), this, SLOT(slotPasteEffects()),
                                      QIcon::fromTheme(QStringLiteral("edit-paste")), QKeySequence(), clipActionCategory);
    pasteEffects->setEnabled(false);
    // "C" as data means this action should only be available for clips - not for compositions
    pasteEffects->setData('C');

    QAction *delEffects = new QAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Remove Effects"), this);
    addAction(QStringLiteral("delete_effects"), delEffects, QKeySequence(), clipActionCategory);
    delEffects->setEnabled(false);
    // "C" as data means this action should only be available for clips - not for compositions
    delEffects->setData('C');
    connect(delEffects, &QAction::triggered, this, [this]() { getCurrentTimeline()->controller()->deleteEffects(); });

    QAction *groupClip = addAction(QStringLiteral("group_clip"), i18n("Group Clips"), this, SLOT(slotGroupClips()),
                                   QIcon::fromTheme(QStringLiteral("object-group")), Qt::CTRL | Qt::Key_G, clipActionCategory);
    // "G" as data means this action should only be available for multiple items selection
    groupClip->setData('G');
    groupClip->setEnabled(false);

    QAction *ungroupClip = addAction(QStringLiteral("ungroup_clip"), i18n("Ungroup Clips"), this, SLOT(slotUnGroupClips()),
                                     QIcon::fromTheme(QStringLiteral("object-ungroup")), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), clipActionCategory);
    // "U" as data means this action should only be available if selection is a group
    ungroupClip->setData('U');
    ungroupClip->setEnabled(false);

    QAction *sentToSequence = addAction(QStringLiteral("send_sequence"), i18n("Create Sequence from Selection"), this, SLOT(slotCreateSequenceFromSelection()),
                                        QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(), clipActionCategory);
    sentToSequence->setWhatsThis(
        xi18nc("@info:whatsthis", "Adds the clip(s) currently selected in the timeline to a new sequence clip that can be opened in another timeline tab."));
    sentToSequence->setData('G');
    sentToSequence->setEnabled(false);

    QAction *copyToSequence = addAction(QStringLiteral("copy_to_sequence"), i18n("Copy Selection to New Sequence"), this, SLOT(slotCopyAndCreateSequenceFromSelection()),
                                        QIcon::fromTheme(QStringLiteral("bookmark-new")), QKeySequence(), clipActionCategory);
    copyToSequence->setWhatsThis(
        xi18nc("@info:whatsthis", "Copy the clip(s) currently selected in the timeline to a new sequence clip that can be opened in another timeline tab."));
    copyToSequence->setData('G');
    copyToSequence->setEnabled(false);

    act = clipActionCategory->addAction(KStandardActions::Cut, this, &MainWindow::slotCut);
    act->setEnabled(false);

    act = clipActionCategory->addAction(KStandardActions::Copy, this, &MainWindow::slotCopy);
    act->setEnabled(false);

    actionCollection()->addAction(KStandardActions::Paste, this, &MainWindow::slotPaste);

    // Keyframe actions
    m_assetPanel = new AssetPanel(this);
    KActionCategory *kfActions = new KActionCategory(i18n("Effect Keyframes"), actionCollection());
    addAction(QStringLiteral("keyframe_add"), i18n("Add/Remove Keyframe"), m_assetPanel, SLOT(slotAddRemoveKeyframe()),
              QIcon::fromTheme(QStringLiteral("keyframe-add")), QKeySequence(), kfActions);
    addAction(QStringLiteral("keyframe_next"), i18n("Go to next keyframe"), m_assetPanel, SLOT(slotNextKeyframe()),
              QIcon::fromTheme(QStringLiteral("keyframe-next")), QKeySequence(), kfActions);
    addAction(QStringLiteral("keyframe_previous"), i18n("Go to previous keyframe"), m_assetPanel, SLOT(slotPreviousKeyframe()),
              QIcon::fromTheme(QStringLiteral("keyframe-previous")), QKeySequence(), kfActions);

    kdenliveCategoryMap.insert(QStringLiteral("timelineselection"), clipActionCategory);

    addAction(QStringLiteral("insert_space"), i18n("Insert Space…"), this, SLOT(slotInsertSpace()));
    addAction(QStringLiteral("delete_space"), i18n("Remove Space"), this, SLOT(slotRemoveSpace()));
    addAction(QStringLiteral("delete_all_spaces"), i18n("Remove All Spaces After Cursor"), this, SLOT(slotRemoveAllSpacesInTrack()));
    addAction(QStringLiteral("delete_all_clips"), i18n("Remove All Clips After Cursor"), this, SLOT(slotRemoveAllClipsInTrack()));
    addAction(QStringLiteral("delete_space_all_tracks"), i18n("Remove Space in All Tracks"), this, SLOT(slotRemoveSpaceInAllTracks()));

    KActionCategory *timelineActions = new KActionCategory(i18n("Tracks"), actionCollection());
    QAction *insertTrack = new QAction(QIcon(), i18nc("@action", "Insert Track…"), this);
    connect(insertTrack, &QAction::triggered, this, &MainWindow::slotInsertTrack);
    addAction(QStringLiteral("insert_track"), insertTrack, Qt::CTRL | Qt::SHIFT | Qt::Key_N, timelineActions);

    QAction *autoTrackHeight = new QAction(QIcon(), i18n("Fit all Tracks in View"), this);
    autoTrackHeight->setCheckable(true);
    autoTrackHeight->setChecked(KdenliveSettings::autotrackheight());
    connect(autoTrackHeight, &QAction::triggered, this, &MainWindow::slotAutoTrackHeight);
    timelineActions->addAction(QStringLiteral("fit_all_tracks"), autoTrackHeight);

    // KoraCut, 22 septembre 2026 : une bascule unique « Tout plier / Tout déplier », avec son
    // bouton dans la barre d'outils de la timeline. La mécanique existait déjà
    // (`TimelineController::collapseAllTracks()`), elle n'était atteignable que par ⇧< et
    // seulement quand la timeline avait le focus. Avec dix pistes vidéo et dix pistes audio,
    // c'est le geste qu'on fait le plus souvent.
    QAction *collapseTracks = new QAction(QIcon::fromTheme(QStringLiteral("collapse-all")), i18n("Collapse/Expand All Tracks"), this);
    connect(collapseTracks, &QAction::triggered, this, [this]() {
        if (getCurrentTimeline() && getCurrentTimeline()->controller()) {
            // Toutes les pistes, pas seulement celles du type de la piste active.
            getCurrentTimeline()->controller()->collapseAllTracks(true);
        }
    });
    timelineActions->addAction(QStringLiteral("collapse_all_tracks"), collapseTracks);

    QAction *masterEffectStack = new QAction(QIcon::fromTheme(QStringLiteral("composite-track-on")), i18n("Sequence effects"), this);
    connect(masterEffectStack, &QAction::triggered, this, [&]() {
        pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
        getCurrentTimeline()->controller()->showMasterEffects();
    });
    timelineActions->addAction(QStringLiteral("master_effects"), masterEffectStack);

    QAction *switchTrackTarget = new QAction(QIcon(), i18n("Switch Track Target Audio Stream"), this);
    connect(switchTrackTarget, &QAction::triggered, this, &MainWindow::slotSwitchTrackAudioStream);
    timelineActions->addAction(QStringLiteral("switch_target_stream"), switchTrackTarget);
    actionCollection()->setDefaultShortcut(switchTrackTarget, Qt::Key_Apostrophe);

    QAction *deleteTrack = new QAction(QIcon(), i18n("Delete Track…"), this);
    connect(deleteTrack, &QAction::triggered, this, &MainWindow::slotDeleteTrack);
    timelineActions->addAction(QStringLiteral("delete_track"), deleteTrack);
    deleteTrack->setData("delete_track");

    QAction *moveTrackUp = new QAction(QIcon::fromTheme(QStringLiteral("go-up")), i18n("Move Track Up"), this);
    connect(moveTrackUp, &QAction::triggered, this, &MainWindow::slotMoveTrackUp);
    timelineActions->addAction(QStringLiteral("move_track_up"), moveTrackUp);

    QAction *moveTrackDown = new QAction(QIcon::fromTheme(QStringLiteral("go-down")), i18n("Move Track Down"), this);
    connect(moveTrackDown, &QAction::triggered, this, &MainWindow::slotMoveTrackDown);
    timelineActions->addAction(QStringLiteral("move_track_down"), moveTrackDown);

    QAction *showAudio = new QAction(QIcon(), i18n("Show Record Controls"), this);
    connect(showAudio, &QAction::triggered, this, &MainWindow::slotShowTrackRec);
    timelineActions->addAction(QStringLiteral("show_track_record"), showAudio);
    showAudio->setCheckable(true);
    showAudio->setData("show_track_record");

    QAction *selectTrack = new QAction(QIcon(), i18n("Select All in Current Track"), this);
    connect(selectTrack, &QAction::triggered, this, &MainWindow::slotSelectTrack);
    timelineActions->addAction(QStringLiteral("select_track"), selectTrack);

    QAction *selectAll = KStandardAction::selectAll(this, SLOT(slotSelectAllTracks()), this);
    selectAll->setIcon(QIcon::fromTheme(QStringLiteral("edit-select-all")));
    selectAll->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    timelineActions->addAction(QStringLiteral("select_all_tracks"), selectAll);

    QAction *unselectAll = KStandardAction::deselect(this, SLOT(slotUnselectAllTracks()), this);
    unselectAll->setIcon(QIcon::fromTheme(QStringLiteral("edit-select-none")));
    unselectAll->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    timelineActions->addAction(QStringLiteral("unselect_all_tracks"), unselectAll);

    kdenliveCategoryMap.insert(QStringLiteral("timeline"), timelineActions);

    // Cached data management
    addAction(QStringLiteral("manage_cache"), i18n("Manage Cached Data…"), this, SLOT(slotManageCache()),
              QIcon::fromTheme(QStringLiteral("network-server-database")));

    // KoraCut : pour comparer, la lecture directe sans les passages rendus.
    QAction *disablePreview = new QAction(i18nc("@action", "Play Without Renders"), this);
    disablePreview->setCheckable(true);
    addAction(QStringLiteral("disable_preview"), disablePreview);

    addAction(QStringLiteral("add_sequence_marker"), i18n("Add/Remove Timeline Marker"), this, SLOT(slotAddGuide()),
              QIcon::fromTheme(QStringLiteral("bookmarks")), Qt::Key_G);
    addAction(QStringLiteral("delete_sequence_marker"), i18n("Delete Timeline Marker"), this, SLOT(slotDeleteGuide()),
              QIcon::fromTheme(QStringLiteral("bookmark-remove")));
    addAction(QStringLiteral("delete_all_sequence_markers"), i18n("Delete All Timeline Markers"), this, SLOT(slotDeleteAllSequenceMarkers()),
              QIcon::fromTheme(QStringLiteral("edit-delete")));
    addAction(QStringLiteral("add_markers_at_gaps"), i18n("Identify Gaps"), this, SLOT(slotAddMarkersAtGaps()),
              QIcon::fromTheme(QStringLiteral("bookmark-new")));
    addAction(QStringLiteral("edit_sequence_marker"), i18n("Edit Timeline Marker…"), this, SLOT(slotEditGuide()),
              QIcon::fromTheme(QStringLiteral("bookmark-edit")));

    addAction(QStringLiteral("export_guides"), i18n("Export Markers…"), this, SLOT(slotExportGuides()), QIcon::fromTheme(QStringLiteral("document-export")));
    addAction(QStringLiteral("search_guide"), i18n("Search Marker…"), this, SLOT(slotSearchGuide()), QIcon::fromTheme(QStringLiteral("edit-find")));

    QAction *lockGuides =
        addAction(QStringLiteral("lock_guides"), i18n("Lock Timeline Markers"), this, SLOT(slotLockGuides(bool)), QIcon::fromTheme(QStringLiteral("lock")));
    lockGuides->setCheckable(true);
    lockGuides->setChecked(KdenliveSettings::lockedGuides());
    lockGuides->setToolTip(i18n("Lock Timeline Markers"));
    lockGuides->setWhatsThis(xi18nc(
        "@info:whatsthis", "Lock Timeline Markers. When locked, the markers won't move when using the spacer tool or inserting/removing blank in tracks."));

    addAction(QStringLiteral("add_subtitle"), i18n("Add Subtitle"), this, SLOT(slotAddSubtitle()), QIcon::fromTheme(QStringLiteral("list-add")),
              Qt::SHIFT | Qt::Key_S);
    addAction(QStringLiteral("disable_subtitle"), i18n("Disable Subtitle"), this, SLOT(slotDisableSubtitle()), QIcon::fromTheme(QStringLiteral("view-hidden")));
    addAction(QStringLiteral("lock_subtitle"), i18n("Lock Subtitle"), this, SLOT(slotLockSubtitle()), QIcon::fromTheme(QStringLiteral("lock")));

    addAction(QStringLiteral("manage_subtitle"), i18n("Manage Subtitles"), this, SLOT(slotManageSubtitle()),
              QIcon::fromTheme(QStringLiteral("settings-configure")));
    addAction(QStringLiteral("import_subtitle"), i18n("Import Subtitle File…"), this, SLOT(slotImportSubtitle()),
              QIcon::fromTheme(QStringLiteral("document-import")));
    addAction(QStringLiteral("export_subtitle"), i18n("Export Subtitle File…"), this, SLOT(slotExportSubtitle()),
              QIcon::fromTheme(QStringLiteral("document-export")));
    addAction(QStringLiteral("delete_subtitle_clip"), i18n("Delete Subtitle"), this, SLOT(slotDeleteItem()), QIcon::fromTheme(QStringLiteral("edit-delete")));
    addAction(QStringLiteral("audio_recognition"), i18n("Speech Recognition…"), this, SLOT(slotSpeechRecognition()),
              QIcon::fromTheme(QStringLiteral("autocorrection")));

    m_saveAction = KStandardAction::save(pCore->projectManager(), SLOT(saveFile()), actionCollection());
    m_saveAction->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));

    QAction *const showMenuBarAction = KStandardAction::showMenubar(this, &MainWindow::showMenuBar, actionCollection());
    showMenuBarAction->setWhatsThis(xi18nc("@info:whatsthis", "This switches between having a <emphasis>Menubar</emphasis> "
                                                              "and having a <interface>Hamburger Menu</interface> button in the main Toolbar."));

    KStandardAction::quit(this, &MainWindow::close, actionCollection());
    KStandardAction::keyBindings(this, &MainWindow::slotEditKeys, actionCollection());
    QAction *preferences = KStandardAction::preferences(this, &MainWindow::slotPreferences, actionCollection());
    // KoraCut : ⌘, sur Mac (Ctrl+, ailleurs), la convention de toutes les applications. Celle de KDE,
    // Ctrl+Maj+virgule, reste en second. Libre dans les 834 actions (KORACUT_DUMP_ACTIONS=all).
    actionCollection()->setDefaultShortcuts(preferences, {QKeySequence(Qt::CTRL | Qt::Key_Comma), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma)});
    KStandardAction::configureNotifications(this, &MainWindow::configureNotifications, actionCollection());
    QAction *fullScreenAction = KStandardAction::fullScreen(this, &MainWindow::slotFullScreen, this, actionCollection());
#ifndef Q_OS_MACOS
    // KoraCut, chantier 12 : sous Windows et Linux, le plein écran standard de KDE (Ctrl+Maj+F) prenait la
    // touche de « Agrandir le panneau actif » (⌘⇧F sur Mac, où le plein écran est ⌃⌘F), et KDE ouvrait au
    // démarrage une fenêtre « raccourcis ambigus » (vu sous Linux le 30 septembre 2026). F11 est déjà le
    // moniteur en plein écran : Ctrl+Alt+F, la même lettre que sur Mac.
    actionCollection()->setDefaultShortcut(fullScreenAction, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F));
#else
    Q_UNUSED(fullScreenAction)
#endif

    // cppcheck-suppress legacyUninitvar
    QAction *undo = KStandardAction::undo(m_commandStack, SLOT(undo()), actionCollection());
    undo->setEnabled(false);
    connect(m_commandStack, &QUndoGroup::canUndoChanged, undo, &QAction::setEnabled);

    // cppcheck-suppress legacyUninitvar
    QAction *redo = KStandardAction::redo(m_commandStack, SLOT(redo()), actionCollection());
    redo->setEnabled(false);
    connect(m_commandStack, &QUndoGroup::canRedoChanged, redo, &QAction::setEnabled);
    connect(this, &MainWindow::enableUndo, this, [this, undo, redo](bool enable) {
        bool undoEnabled = enable;
        if (enable && m_commandStack->activeStack()) {
            enable = m_commandStack->activeStack()->canRedo();
            undoEnabled = m_commandStack->activeStack()->canUndo();
        }
        redo->setEnabled(enable);
        undo->setEnabled(undoEnabled);
    });

    addAction(QStringLiteral("copy_debuginfo"), i18n("Copy Debug Information"), this, SLOT(slotCopyDebugInfo()), QIcon::fromTheme(QStringLiteral("edit-copy")));

    QAction *disableEffects = addAction(QStringLiteral("disable_timeline_effects"), i18n("Disable Timeline Effects"), pCore->projectManager(),
                                        SLOT(slotDisableTimelineEffects(bool)), QIcon::fromTheme(QStringLiteral("tools-wizard")));
    disableEffects->setData("disable_timeline_effects");
    disableEffects->setCheckable(true);
    disableEffects->setChecked(false);

    // Timeline hamburger menu
    auto tlsettings = new QMenu(this);
    tlsettings->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    tlsettings->addAction(m_compositeAction);
    tlsettings->addAction(disableEffects);
    // KoraCut : ce réglage quitte la barre d'outils. Une barre porte des commandes, pas des
    // réglages : c'est ce qui la faisait monter à dix-huit boutons.
    tlsettings->addAction(m_useTimelineZone);
    tlsettings->addSeparator();
    tlsettings->addAction(mixedView);
    tlsettings->addAction(splitView);
    tlsettings->addAction(splitView2);

    auto *timelineSett = new QToolButton(this);
    timelineSett->setPopupMode(QToolButton::InstantPopup);
    timelineSett->setMenu(tlsettings);
    timelineSett->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    auto *tlButtonAction = new QWidgetAction(this);
    tlButtonAction->setDefaultWidget(timelineSett);
    tlButtonAction->setText(i18n("Track menu"));
    addAction(QStringLiteral("timeline_settings"), tlButtonAction);
    // end of new place for the TL hmbg menu

    addAction(QStringLiteral("switch_track_disabled"), i18n("Toggle Track Disabled"), pCore->projectManager(), SLOT(slotSwitchTrackDisabled()), QIcon(),
              Qt::SHIFT | Qt::Key_H, timelineActions);
    addAction(QStringLiteral("switch_all_track_disabled"), i18n("Toggle All Tracks Disabled"), pCore->projectManager(), SLOT(slotSwitchAllTrackDisabled()),
              QIcon(), Qt::CTRL | Qt::SHIFT | Qt::Key_H, timelineActions);
    addAction(QStringLiteral("switch_track_lock"), i18n("Toggle Track Lock"), pCore->projectManager(), SLOT(slotSwitchTrackLock()), QIcon(),
              Qt::SHIFT | Qt::Key_L, timelineActions);
    addAction(QStringLiteral("switch_all_track_lock"), i18n("Toggle All Tracks Lock"), pCore->projectManager(), SLOT(slotSwitchAllTrackLock()), QIcon(),
              Qt::CTRL | Qt::SHIFT | Qt::Key_L, timelineActions);
    addAction(QStringLiteral("switch_track_target"), i18n("Toggle Track Target"), pCore->projectManager(), SLOT(slotSwitchTrackTarget()), QIcon(),
              Qt::SHIFT | Qt::Key_T, timelineActions);
    // KoraCut : A passe à l'outil Sélection.
    addAction(QStringLiteral("switch_active_target"), i18n("Toggle Track Active"), pCore->projectManager(), SLOT(slotSwitchTrackActive()), QIcon(), {},
              timelineActions);
    addAction(QStringLiteral("switch_all_targets"), i18n("Toggle All Tracks Active"), pCore->projectManager(), SLOT(slotSwitchAllTrackActive()), QIcon(),
              Qt::SHIFT | Qt::Key_A, timelineActions);
    addAction(QStringLiteral("activate_all_targets"), i18n("Switch All Tracks Active"), pCore->projectManager(), SLOT(slotMakeAllTrackActive()), QIcon(),
              Qt::SHIFT | Qt::ALT | Qt::Key_A, timelineActions);
    addAction(QStringLiteral("restore_all_sources"), i18n("Restore Current Clip Target Tracks"), pCore->projectManager(), SLOT(slotRestoreTargetTracks()), {},
              {}, timelineActions);
    addAction(QStringLiteral("add_project_note"), i18n("Add Project Note"), pCore->projectManager(), SLOT(slotAddProjectNote()),
              QIcon::fromTheme(QStringLiteral("list-add")), {}, timelineActions);

    // Build activate track shortcut sequences
    QList<int> keysequence{Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5, Qt::Key_6, Qt::Key_7, Qt::Key_8, Qt::Key_9};
    for (int i = 1; i < 10; i++) {
        QAction *ac = new QAction(QIcon(), i18n("Select Audio Track %1", i), this);
        ac->setData(i - 1);
        connect(ac, &QAction::triggered, this, &MainWindow::slotActivateAudioTrackSequence);
        addAction(QStringLiteral("activate_audio_%1").arg(i), ac, QKeySequence(Qt::ALT | keysequence[i - 1]), timelineActions);
        QAction *ac2 = new QAction(QIcon(), i18n("Select Video Track %1", i), this);
        ac2->setData(i - 1);
        connect(ac2, &QAction::triggered, this, &MainWindow::slotActivateVideoTrackSequence);
        // KoraCut : les chiffres seuls sont rendus aux modules (Maj+1 à Maj+8). Sur un clavier AZERTY,
        // la rangée du haut ne donne un chiffre qu'avec Maj : garder « 1 » ici bloquait « Maj+1 ».
        // La sélection de piste vidéo reste accessible par Alt+chiffre et par Configurer les raccourcis.
        addAction(QStringLiteral("activate_video_%1").arg(i), ac2, QKeySequence(), timelineActions);
        QAction *ac3 = new QAction(QIcon(), i18n("Select Target %1", i), this);
        ac3->setData(i - 1);
        connect(ac3, &QAction::triggered, this, &MainWindow::slotActivateTarget);
        addAction(QStringLiteral("activate_target_%1").arg(i), ac3, QKeySequence(Qt::CTRL | keysequence[i - 1]), timelineActions);
    }

    // Setup effects and transitions actions.
    KActionCategory *transitionActions = new KActionCategory(i18n("Transitions"), actionCollection());
    auto allTransitions = TransitionsRepository::get()->getNames();
    // KoraCut, 23 septembre 2026 : getNames() rend (identifiant, nom). Le texte affiché recevait
    // l'identifiant (« frei0r.sleid0r_wipe-right ») et le nom d'action le nom traduit, qui change avec
    // la langue et casse les raccourcis. Et ces actions n'étaient branchées sur rien : lancées depuis
    // la palette ⌘K ou un raccourci, elles ne faisaient rien. Elles posent maintenant la composition
    // sur le clip sélectionné, par slotAddTransition (le même chemin que le menu Compositions).
    for (const auto &transition : std::as_const(allTransitions)) {
        auto *transAction = new QAction(transition.second, this);
        transAction->setData(transition.first);
        transAction->setIconVisibleInMenu(false);
        transitionActions->addAction("transition_" + transition.first, transAction);
    }

    // monitor actions
    addAction(QStringLiteral("extract_frame"), i18n("Extract Frame…"), pCore->monitorManager(), SLOT(slotExtractCurrentFrame()),
              QIcon::fromTheme(QStringLiteral("insert-image")));

    addAction(QStringLiteral("extract_frame_to_project"), i18n("Extract Frame to Project…"), pCore->monitorManager(), SLOT(slotExtractCurrentFrameToProject()),
              QIcon::fromTheme(QStringLiteral("insert-image")));

    addAction(QStringLiteral("extract_frame_to_clipboard"), i18n("Extract Frame to Clipboard"), pCore->monitorManager(),
              SLOT(slotExtractCurrentFrameToClipboard()), QIcon::fromTheme(QStringLiteral("insert-image")));

    // ----- KoraCut -----
    auto *koraCutActions = new KActionCategory(i18nc("@title catégorie de raccourcis", "KoraCut"), actionCollection());
    kdenliveCategoryMap.insert(QStringLiteral("koracut"), koraCutActions);

    // Les modules : Maj+2 à Maj+8, plus la variante sans Maj (jouable en QWERTY).
    // Maj+1 est réservé à l'Accueil, qui n'est pas encore écrit.
    auto shortcutsFor = [](int number) {
        const Qt::Key key = static_cast<Qt::Key>(Qt::Key_0 + number);
        return QList<QKeySequence>{QKeySequence(Qt::SHIFT | key), QKeySequence(key)};
    };
    addActionWithShortcuts(QStringLiteral("koracut_home"), i18nc("@action va à l'écran d'accueil", "Home"), this, SLOT(slotShowHome()), QIcon(),
                           shortcutsFor(1), koraCutActions);
    const QList<KoraCutModules::Module> &koraCutModules = KoraCutModules::all();
    for (const KoraCutModules::Module &module : koraCutModules) {
        auto *moduleAction = new QAction(module.name, this);
        moduleAction->setData(module.id);
        connect(moduleAction, &QAction::triggered, this, &MainWindow::slotSwitchModule);
        addActionWithShortcuts(QStringLiteral("koracut_module_%1").arg(module.id), moduleAction, shortcutsFor(module.number), koraCutActions);
    }

    // Palette de commandes : le « Find Action » de KXmlGui, sur ⌘K comme chez Figma et Resolve.
    addAction(QStringLiteral("koracut_command_palette"), i18nc("@action ouvre la palette de commandes", "Command Palette"), this,
              SLOT(slotCommandPalette()), QIcon::fromTheme(QStringLiteral("search")), QKeySequence(Qt::CTRL | Qt::Key_K), koraCutActions);

    // Baisse automatique de la musique sous la voix (boîte aux lettres, 22 septembre 2026).
    auto *ducking = new QAction(QIcon::fromTheme(QStringLiteral("audio-volume-low")), i18nc("@action", "Automatic Music Ducking…"), this);
    ducking->setToolTip(i18nc("@info:tooltip", "Lowers the music where someone speaks, with keyframes you can adjust"));
    connect(ducking, &QAction::triggered, this, [this]() {
        auto *dialog = new KoraCutDuckingDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    addActionWithShortcuts(QStringLiteral("koracut_auto_ducking"), ducking, {}, koraCutActions);

    // Coupe automatique, type AutoCut : les silences (document 15, lot 1).
    // Icône : la baguette à étincelles (et non les ciseaux, qui sont déjà la Lame), texte court pour
    // la barre d'outils de la timeline, où il est posé depuis le 23 septembre 2026 (Amadou Ba : « je
    // ne vois pas où tu l'as mis, si j'en ai besoin il faut que je cherche »).
    auto *autoCut = new QAction(QIcon::fromTheme(QStringLiteral("tools-wizard")), i18nc("@action", "Automatic Cut…"), this);
    autoCut->setIconText(i18nc("@action:intoolbar short label", "Auto Cut"));
    autoCut->setToolTip(i18nc("@info:tooltip", "Automatic cut: syncs the sources, picks the cameras, then finds silences, filler words and retakes, with its settings"));
    connect(autoCut, &QAction::triggered, this, [this]() {
        auto *dialog = new KoraCutAutoCutDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    addActionWithShortcuts(QStringLiteral("koracut_auto_cut"), autoCut, {}, koraCutActions);

    // La voix off rapide, comme CapCut (29 septembre 2026, demande d'Amadou Ba) : un micro dans la barre
    // de la timeline, en tête du groupe de droite, qui ouvre le petit panneau d'enregistrement. Pendant
    // qu'on enregistre, le bouton reste enfoncé et un clic arrête. « Couper le son du projet » est coché
    // par défaut : le micro ne reprend pas ce qui joue dans la timeline.
    auto *voiceRecord = new QAction(QIcon::fromTheme(QStringLiteral("audio-input-microphone")), i18nc("@action", "Record Voice-over…"), this);
    voiceRecord->setIconText(i18nc("@action:intoolbar short label", "Voice-over"));
    voiceRecord->setToolTip(i18nc("@info:tooltip", "Record a voice-over at the playhead with the microphone (the project sound is muted while you talk)"));
    voiceRecord->setCheckable(true);
    connect(voiceRecord, &QAction::triggered, this, [this, voiceRecord]() {
        KoraCutVoiceRecord *panel = KoraCutVoiceRecord::instance(this);
        if (panel->isRecording()) {
            panel->startOrStop();
        } else {
            panel->toggleNear(m_timelineToolBar ? m_timelineToolBar->widgetForAction(voiceRecord) : nullptr);
        }
        voiceRecord->setChecked(panel->isRecording());
    });
    connect(KoraCutVoiceRecord::instance(this), &KoraCutVoiceRecord::recordingChanged, voiceRecord, &QAction::setChecked);
    addActionWithShortcuts(QStringLiteral("koracut_voice_record"), voiceRecord, {}, koraCutActions);
    // Sans panneau : démarre ou arrête tout de suite (palette de commandes, raccourci à poser, agents et porte de commande).
    auto *voiceNow = new QAction(QIcon::fromTheme(QStringLiteral("audio-input-microphone")), i18nc("@action", "Start or Stop Voice-over Recording"), this);
    connect(voiceNow, &QAction::triggered, this, [this]() { KoraCutVoiceRecord::instance(this)->startOrStop(); });
    addActionWithShortcuts(QStringLiteral("koracut_voice_record_now"), voiceNow, {}, koraCutActions);

    // Le panneau Agents (chantier 3) : ⌘J, libre dans les 839 actions (KORACUT_DUMP_ACTIONS=all,
    // 23 septembre 2026 ; ⌘⇧A est « Tout désélectionner »).
    auto *agentsPanel = new QAction(QIcon::fromTheme(QStringLiteral("dialog-messages")), i18nc("@action", "Agents Panel"), this);
    agentsPanel->setToolTip(i18nc("@info:tooltip", "Talk to the agents: they edit for you, one Undo takes back a whole turn"));
    connect(agentsPanel, &QAction::triggered, this, &MainWindow::slotShowAgentsPanel);
    addActionWithShortcuts(QStringLiteral("koracut_agents_panel"), agentsPanel, {QKeySequence(Qt::CTRL | Qt::Key_J)}, koraCutActions);

    // L'étalonnage en pause dans l'aperçu (2 octobre 2026, document 6) : ⌘⇧D, libre dans les 915 actions
    // (KORACUT_DUMP_ACTIONS=all) ; Maj+D est « Couper l'étalonnage » du plan, dans le module Étalonnage.
    addActionWithShortcuts(QStringLiteral("koracut_grade_pause"), KoraCutPerformance::gradePauseAction(), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D)},
                           koraCutActions);
    KoraCutPerformance::initGradePause();

    // Le panneau Diagnostic (chantier 10) : pas de raccourci, il est dans Réglages.
    auto *diagnosticPanel = new QAction(QIcon::fromTheme(QStringLiteral("speedometer")), i18nc("@action", "Diagnostic Panel"), this);
    diagnosticPanel->setToolTip(i18nc("@info:tooltip", "How smoothly this computer plays the project, and a test that suggests the right settings"));
    connect(diagnosticPanel, &QAction::triggered, this, &MainWindow::slotShowDiagnosticPanel);
    addAction(QStringLiteral("koracut_diagnostic_panel"), diagnosticPanel, QKeySequence(), koraCutActions);

    // Le panneau Console (document 18) : ⌘⇧J, à côté des agents.
    auto *consolePanel = new QAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), i18nc("@action", "Console Panel"), this);
    consolePanel->setToolTip(i18nc("@info:tooltip", "What external programs ask KoraCut to do, and a terminal in the project folder"));
    connect(consolePanel, &QAction::triggered, this, &MainWindow::slotShowConsolePanel);
    addActionWithShortcuts(QStringLiteral("koracut_console_panel"), consolePanel, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J)}, koraCutActions);

    // Le terminal intégré (document 18, 25 septembre 2026) : ⌃` comme VS Code (Ctrl+` sous Windows et Linux).
    // Sur un clavier AZERTY, ` est une touche morte et ⌃` ne part jamais : ⌃ù en second, comme VS Code
    // en AZERTY (même touche physique sur un Mac français). Libre dans toutes les actions (dump du 25/09/2026).
    auto *terminalPanel = new QAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), i18nc("@action", "Terminal"), this);
    terminalPanel->setToolTip(i18nc("@info:tooltip", "A terminal inside KoraCut, in the project folder, where a command line agent edits with you"));
    connect(terminalPanel, &QAction::triggered, this, &MainWindow::slotToggleTerminalPanel);
    // « Monter avec un agent » (document 18 ; « avec Claude Code » jusqu'au 4 octobre 2026, le nom interne de
    // l'action est resté) : l'agent de la personne dans le terminal, ou la page de choix. Sans raccourci.
    auto *claudeCode = new QAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), i18nc("@action", "Edit with an Agent"), this);
    claudeCode->setToolTip(i18nc("@info:tooltip",
                                 "Prepares the project and launches your agent (Claude Code, Codex, Gemini, OpenCode and others) in a terminal window of its own, "
                                 "which stays open if KoraCut closes, connected to KoraCut, with your own account"));
    connect(claudeCode, &QAction::triggered, this, &MainWindow::slotEditWithClaudeCode);
    addAction(QStringLiteral("koracut_claude_code"), claudeCode, QKeySequence(), koraCutActions);
#ifdef Q_OS_MACOS
    addActionWithShortcuts(QStringLiteral("koracut_terminal_panel"), terminalPanel,
                           {QKeySequence(Qt::META | Qt::Key_QuoteLeft), QKeySequence(Qt::META | Qt::Key_Ugrave)}, koraCutActions);
#else
    addActionWithShortcuts(QStringLiteral("koracut_terminal_panel"), terminalPanel,
                           {QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), QKeySequence(Qt::CTRL | Qt::Key_Ugrave)}, koraCutActions);
#endif

    // Le panneau Motion (chantier 9, document 19) : ⌘⌥M, libre (⌘⇧M est « deux moniteurs », ⇧M un marqueur).
    auto *motionPanel = new QAction(QIcon::fromTheme(QStringLiteral("motion_path_animations")), i18nc("@action", "Motion Panel"), this);
    motionPanel->setToolTip(i18nc("@info:tooltip", "HyperFrames compositions in the timeline: titles, badges, animated overlays"));
    connect(motionPanel, &QAction::triggered, this, &MainWindow::slotShowMotionPanel);
    addActionWithShortcuts(QStringLiteral("koracut_motion_panel"), motionPanel, {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_M)}, koraCutActions);

    // Le panneau Script (document 21) : ⌘⌥S, libre dans les 850 actions (KORACUT_DUMP_ACTIONS=all, 24 septembre 2026).
    auto *scriptAction = new QAction(QIcon::fromTheme(QStringLiteral("text-plain")), i18nc("@action", "Script Panel"), this);
    scriptAction->setToolTip(i18nc("@info:tooltip", "The script of the project: text and tables, ready-made for the video type, shared with the agents"));
    connect(scriptAction, &QAction::triggered, this, [this]() {
        if (auto *d = KDDockWidgets::DockRegistry::self()->dockByName(QStringLiteral("script"))) {
            d->open();
            d->setAsCurrentTab();
        }
        if (auto *panel = findChild<KoraCutScriptPanel *>()) {
            panel->setFocus();
        }
    });
    addActionWithShortcuts(QStringLiteral("koracut_script_panel"), scriptAction, {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S)}, koraCutActions);

    // Extensions : les six sources de téléchargement au même endroit (document 14, § 3).
    auto *extensions = new QAction(QIcon::fromTheme(QStringLiteral("get-hot-new-stuff")), i18nc("@action", "Extensions…"), this);
    extensions->setToolTip(i18nc("@info:tooltip", "Download effects, transitions, titles, LUTs, export presets and shortcuts"));
    connect(extensions, &QAction::triggered, this, [this]() {
        auto *dialog = new KoraCutExtensionsDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &KoraCutExtensionsDialog::installed, this, &MainWindow::koraCutReloadExtensions);
        dialog->show();
    });
    addActionWithShortcuts(QStringLiteral("koracut_extensions"), extensions, {}, koraCutActions);

    // Soutenir KoraCut (4 octobre 2026) : une fenêtre qui ne s'ouvre que par cette action. Aucun rappel,
    // rien au démarrage ni après un export. Pas modale : la porte de commande reste libre.
    auto *support = new QAction(QIcon::fromTheme(QStringLiteral("donate")), i18nc("@action", "Support KoraCut…"), this);
    support->setToolTip(i18nc("@info:tooltip", "Why and how to support the project"));
    connect(support, &QAction::triggered, this, [this]() { KoraCutSupportDialog::open(this); });
    addActionWithShortcuts(QStringLiteral("koracut_support"), support, {}, koraCutActions);

    // Donner son avis : la page de contact du site, déjà réglée sur « avis ».
    auto *feedback = new QAction(QIcon::fromTheme(QStringLiteral("dialog-messages")), i18nc("@action", "Give Your Feedback"), this);
    feedback->setToolTip(i18nc("@info:tooltip", "Open the feedback page of koracut.org in the browser"));
    connect(feedback, &QAction::triggered, this,
            [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://koracut.org/contact/?genre=avis"))); });
    addActionWithShortcuts(QStringLiteral("koracut_feedback"), feedback, {}, koraCutActions);
}

static void koraCutConvertShortcutSchemes(const QList<KNSCore::Entry> &changedEntries);

void MainWindow::koraCutReloadExtensions(const QString &knsrc, const QList<KNSCore::Entry> &entries)
{
    // Ce que chaque panneau fait déjà après son propre bouton de téléchargement, rassemblé ici. Les
    // modèles de titres et les LUT se relisent d'eux-mêmes à l'ouverture de leur fenêtre ou réglage.
    if (knsrc.contains(QLatin1String("koracut_effects"))) {
        for (const auto &entry : entries) {
            if (entry.status() == KNSCore::Entry::Status::Deleted) {
                m_effectList2->reloadTemplates();
                continue;
            }
            const QStringList files = entry.installedFiles();
            for (const QString &f : files) {
                m_effectList2->reloadCustomEffect(f);
            }
        }
        m_effectList2->reloadEffectMenu(m_effectsMenu, m_effectActions);
    } else if (knsrc.contains(QLatin1String("koracut_wipes"))) {
        MltConnection::refreshLumas();
    } else if (knsrc.contains(QLatin1String("koracut_renderprofiles"))) {
        if (m_renderWidget) {
            m_renderWidget->reloadPresets();
        } else {
            RenderPresetRepository::get()->refresh();
        }
    } else if (knsrc.contains(QLatin1String("koracut_keyboardschemes"))) {
        koraCutConvertShortcutSchemes(entries);
    }
}

void MainWindow::koraCutNudgeSelection(int delta)
{
    TimelineWidget *timeline = getCurrentTimeline();
    if (!timeline || !timeline->model()) {
        return;
    }
    const auto model = timeline->model();
    const std::unordered_set<int> selection = model->getCurrentSelection();
    if (selection.empty()) {
        pCore->displayMessage(i18nc("@info:status", "Select a clip to nudge it."), InformationMessage);
        return;
    }
    // Un seul appel suffit : si l'élément fait partie d'un groupe, et une sélection de plusieurs
    // éléments en est un, le modèle déplace tout le groupe du même écart, en une étape d'annulation.
    int id = -1;
    for (int item : selection) {
        if (model->isClip(item) || model->isComposition(item)) {
            id = item;
            break;
        }
    }
    bool moved = false;
    if (id != -1) {
        const int track = model->getItemTrackId(id);
        const int position = model->getItemPosition(id) + delta;
        if (position >= 0) {
            moved = model->isClip(id) ? model->requestClipMove(id, track, position, true, true, true)
                                      : model->requestCompositionMove(id, track, position, true, true);
        }
    }
    if (!moved) {
        pCore->displayMessage(i18nc("@info:status", "Cannot nudge: another clip is in the way, or the selection is at the start."), ErrorMessage);
    }
}

void MainWindow::slotShowHome()
{
    Q_EMIT displayMessage(i18n("The home screen is not written yet."), InformationMessage, 4000);
}

void MainWindow::slotSwitchModule()
{
    auto *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }
    const QString moduleId = action->data().toString();
    if (moduleId.isEmpty() || !m_topBar) {
        return;
    }
    m_topBar->moduleTabs()->setCurrentLayout(moduleId);
    if (KoraCutModules::isUnderConstruction(moduleId)) {
        showModuleUnderConstruction(moduleId);
        return;
    }
    showDockArea();
    Q_EMIT pCore->loadLayoutById(moduleId);
}

void MainWindow::slotCommandPalette()
{
    // On donne à KCommandBar tout ce que la barre de menus contient : c'est déjà toute l'application.
    auto *bar = new KCommandBar(this);
    QList<KCommandBar::ActionGroup> groups;
    const QList<QAction *> menus = menuBar()->actions();
    for (QAction *menuAction : menus) {
        if (QMenu *menu = menuAction->menu()) {
            KCommandBar::ActionGroup group;
            group.name = KLocalizedString::removeAcceleratorMarker(menuAction->text());
            group.actions = menu->actions();
            groups << group;
        }
    }
    // KoraCut, 23 septembre 2026 : les médias du chutier aussi, pour ouvrir un rush par son nom dans le
    // moniteur de source. Des actions éphémères, détruites avec la palette.
    KCommandBar::ActionGroup medias;
    medias.name = i18nc("@title groupe de la palette de commandes", "Media");
    const std::vector<QString> ids = pCore->projectItemModel()->getAllClipIds();
    for (const QString &id : ids) {
        std::shared_ptr<ProjectClip> clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip || clip->clipType() == ClipType::Timeline) {
            continue;
        }
        auto *open = new QAction(clip->icon(), clip->name(), bar);
        connect(open, &QAction::triggered, this, [id]() { pCore->selectBinClip(id, true); });
        medias.actions << open;
    }
    if (!medias.actions.isEmpty()) {
        groups.prepend(medias);
    }
    bar->setActions(groups);
    bar->show();
}

void MainWindow::slotShowAgentsPanel()
{
#if KORACUT_AGENTS
    if (!m_agentDock) {
        return;
    }
    m_agentDock->open();
    m_agentDock->setAsCurrentTab();
    m_agentPanel->focusInput();
#else
    Q_EMIT displayMessage(i18n("This build of KoraCut has no agents."), InformationMessage, 4000);
#endif
}

void MainWindow::slotShowMotionPanel()
{
    if (!m_motionDock) {
        return;
    }
    m_motionDock->open();
    m_motionDock->setAsCurrentTab();
    m_motionPanel->setFocus();
}

void MainWindow::koraCutEditMotionClip(int clipId)
{
    if (!m_motionInspector || !m_effectStackDock) {
        return;
    }
    m_motionInspector->showClip(clipId);
    m_effectStackDock->open();
    m_effectStackDock->setAsCurrentTab();
    m_motionInspector->focusText();
}

void MainWindow::saveOptions()
{
    KdenliveSettings::self()->save();
    pCore->projectManager()->saveRecentFiles();
}

bool MainWindow::readOptions()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    pCore->projectManager()->recentFilesAction()->loadEntries(KConfigGroup(config, "Recent Files"));

    if (KdenliveSettings::defaultprojectfolder().isEmpty()) {
        QDir dir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
        dir.mkpath(QStringLiteral("."));
        KdenliveSettings::setDefaultprojectfolder(dir.absolutePath());
    }

    if (KdenliveSettings::trackheight() == 0) {
        QFont ft = QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont);
        // Height of the icon row
        int baseUnit = qMax(28, qCeil(QFontInfo(ft).pixelSize() * 1.8));
        int trackHeight = baseUnit + qMax(22, qCeil(QFontInfo(ft).pixelSize() * 2.5) + 6);
        KdenliveSettings::setTrackheight(trackHeight);
    }
    bool firstRun = false;
    KConfigGroup initialGroup(config, "version");
    if (!initialGroup.exists() || KdenliveSettings::sdlAudioBackend().isEmpty() || KdenliveSettings::koracutrendererpath().isEmpty()) {
        // First run, check if user is on a KDE Desktop
        firstRun = true;
        // Define default video location for first run
        KRecentDirs::add(QStringLiteral(":KdenliveClipFolder"), QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
        KRecentDirs::add(QStringLiteral(":KdenliveProjectsFolder"), QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));

        // this is our first run, show Wizard
        QPointer<Wizard> w = new Wizard(true);
        if (w->exec() == QDialog::Accepted && w->isOk()) {
            w->adjustSettings();
            delete w;
        } else {
            delete w;
            ::exit(1);
        }
    } else if (!KdenliveSettings::ffmpegpath().isEmpty() && !QFile::exists(KdenliveSettings::ffmpegpath())) {
        // Invalid entry for FFmpeg, check system
        QPointer<Wizard> w = new Wizard(true);
        if (w->exec() == QDialog::Accepted && w->isOk()) {
            w->adjustSettings();
        }
        delete w;
    }
    if (firstRun) {
        if (TransitionsRepository::get()->exists(QStringLiteral("qtblend")) && TransitionsRepository::get()->getVersion(QStringLiteral("qtblend")) > 200) {
            KdenliveSettings::setPreferredcomposite(QStringLiteral("qtblend"));
        }
    }
    initialGroup.writeEntry("version", version);
    if (KdenliveSettings::guidesCategories().isEmpty()) {
        KdenliveSettings::setGuidesCategories(KdenliveDoc::getDefaultGuideCategories());
    }
    return firstRun;
}

void MainWindow::slotRunWizard()
{
    QPointer<Wizard> w = new Wizard(false, this);
    if (w->exec() == QDialog::Accepted && w->isOk()) {
        w->adjustSettings();
    }
    delete w;
}

void MainWindow::slotRefreshProfiles()
{
    KdenliveSettingsDialog *d = static_cast<KdenliveSettingsDialog *>(KConfigDialog::exists(QStringLiteral("settings")));
    if (d) {
        d->checkProfile();
    }
}

void MainWindow::slotEditProjectSettings(int ix)
{
    KdenliveDoc *project = pCore->currentDoc();
    QPair<int, int> p = getCurrentTimeline()->getAvTracksCount();
    int channels = project->getDocumentProperty(QStringLiteral("audioChannels"), QStringLiteral("2")).toInt();
    ProjectSettings *w =
        new ProjectSettings(project, project->metadata(), p.second, p.first, channels, project->projectTempFolder(), true, !project->isModified(), this);
    if (ix > 0) {
        w->tabWidget->setCurrentIndex(ix);
    }
    connect(w, &ProjectSettings::disableProxies, this, &MainWindow::slotDisableProxies);
    // connect(w, SIGNAL(disablePreview()), pCore->projectManager()->currentTimeline(), SLOT(invalidateRange()));
    connect(w, &ProjectSettings::refreshProfiles, this, &MainWindow::slotRefreshProfiles);

    if (w->exec() == QDialog::Accepted) {
        QString profile = w->selectedProfile();
        bool modified = false;
        if (m_renderWidget) {
            m_renderWidget->updateDocumentPath();
        }
        const QStringList guidesCat = w->guidesCategories();
        if (guidesCat != project->guidesCategories()) {
            project->updateGuideCategories(guidesCat, w->remapGuidesCategories());
        }
        if (KdenliveSettings::videothumbnails() != w->enableVideoThumbs()) {
            slotSwitchVideoThumbs();
        }
        if (KdenliveSettings::audiothumbnails() != w->enableAudioThumbs()) {
            slotSwitchAudioThumbs();
        }
        if (project->getDocumentProperty(QStringLiteral("previewparameters")) != w->previewParams() ||
            project->getDocumentProperty(QStringLiteral("previewextension")) != w->previewExtension()) {
            modified = true;
            project->setDocumentProperty(QStringLiteral("previewparameters"), w->previewParams());
            project->setDocumentProperty(QStringLiteral("previewextension"), w->previewExtension());
            slotClearPreviewRender(false);
        }

        bool proxiesChanged = false;
        if (project->getDocumentProperty(QStringLiteral("proxyparams")) != w->proxyParams() ||
            project->getDocumentProperty(QStringLiteral("proxyextension")) != w->proxyExtension()) {
            modified = true;
            proxiesChanged = true;
            project->setDocumentProperty(QStringLiteral("proxyparams"), w->proxyParams());
            project->setDocumentProperty(QStringLiteral("proxyextension"), w->proxyExtension());
        }
        if (project->getDocumentProperty(QStringLiteral("externalproxyparams")) != w->externalProxyParams()) {
            modified = true;
            proxiesChanged = true;
            project->setDocumentProperty(QStringLiteral("externalproxyparams"), w->externalProxyParams());
        }
        if (proxiesChanged && pCore->projectItemModel()->hasProxies() &&
            KMessageBox::questionTwoActions(this, i18n("You have changed the proxy parameters. Do you want to recreate all proxy clips for this project?"), {},
                                            KGuiItem(i18nc("@action:button", "Recreate")),
                                            KGuiItem(i18nc("@action:button", "Continue without"))) == KMessageBox::PrimaryAction) {
            pCore->bin()->rebuildProxies();
        }

        if (project->getDocumentProperty(QStringLiteral("generateproxy")) != QString::number(int(w->generateProxy()))) {
            modified = true;
            project->setDocumentProperty(QStringLiteral("generateproxy"), QString::number(int(w->generateProxy())));
        }
        if (project->getDocumentProperty(QStringLiteral("proxyminsize")) != QString::number(w->proxyMinSize())) {
            modified = true;
            project->setDocumentProperty(QStringLiteral("proxyminsize"), QString::number(w->proxyMinSize()));
        }
        if (project->getDocumentProperty(QStringLiteral("generateimageproxy")) != QString::number(int(w->generateImageProxy()))) {
            modified = true;
            project->setDocumentProperty(QStringLiteral("generateimageproxy"), QString::number(int(w->generateImageProxy())));
        }
        if (project->getDocumentProperty(QStringLiteral("proxyimageminsize")) != QString::number(w->proxyImageMinSize())) {
            modified = true;
            project->setDocumentProperty(QStringLiteral("proxyimageminsize"), QString::number(w->proxyImageMinSize()));
        }
        if (project->getDocumentProperty(QStringLiteral("proxyimagesize")) != QString::number(w->proxyImageSize())) {
            modified = true;
            project->setDocumentProperty(QStringLiteral("proxyimagesize"), QString::number(w->proxyImageSize()));
        }
        if (project->getDocumentProperty(QStringLiteral("proxyresize")) != QString::number(w->proxyResize())) {
            modified = true;
            project->setDocumentProperty(QStringLiteral("proxyresize"), QString::number(w->proxyResize()));
        }
        if (QString::number(int(w->useProxy())) != project->getDocumentProperty(QStringLiteral("enableproxy"))) {
            project->setDocumentProperty(QStringLiteral("enableproxy"), QString::number(int(w->useProxy())));
            modified = true;
            slotUpdateProxySettings();
        }
        if (QString::number(int(w->useExternalProxy())) != project->getDocumentProperty(QStringLiteral("enableexternalproxy"))) {
            project->setDocumentProperty(QStringLiteral("enableexternalproxy"), QString::number(int(w->useExternalProxy())));
            modified = true;
        }
        if (w->metadata() != project->metadata()) {
            project->setMetadata(w->metadata());
            if (m_renderWidget) {
                m_renderWidget->updateMetadataToolTip();
            }
        }
        QString newProjectFolder = w->storageFolder();

        if (w->docFolderAsStorageFolder()) {
            newProjectFolder = QFileInfo(project->url().toLocalFile()).absolutePath() + QStringLiteral("/cachefiles");
        }
        if (newProjectFolder.isEmpty()) {
            newProjectFolder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        }
        if (newProjectFolder != project->projectTempFolder()) {
            KMessageBox::ButtonCode answer;
            // Project folder changed:
            if (project->isModified()) {
                answer = KMessageBox::warningContinueCancel(
                    this, i18n("The current project has not been saved.<br/>This will first save the project, then move "
                               "all temporary files from <br/><b>%1</b> to <b>%2</b>,<br>and the project file will be reloaded",
                               project->projectTempFolder(), newProjectFolder));
                if (answer == KMessageBox::Continue) {
                    pCore->projectManager()->saveFile();
                }
            } else {
                answer = KMessageBox::warningContinueCancel(
                    this, i18n("This will move all temporary files from<br/><b>%1</b> to <b>%2</b>,<br/>the project file will then be reloaded",
                               project->projectTempFolder(), newProjectFolder));
            }
            if (answer == KMessageBox::Continue) {
                // Proceed with move
                QString documentId = QDir::cleanPath(project->getDocumentProperty(QStringLiteral("documentid")));
                bool ok;
                documentId.toLongLong(&ok, 10);
                if (!ok || documentId.isEmpty()) {
                    KMessageBox::error(this, i18n("Cannot perform operation, invalid document id: %1", documentId));
                } else {
                    QDir newDir(newProjectFolder);
                    QDir oldDir(project->projectTempFolder());
                    if (newDir.exists(documentId)) {
                        KMessageBox::error(this, i18n("Cannot perform operation, target directory already exists: %1", newDir.absoluteFilePath(documentId)));
                    } else {
                        // Proceed with the move
                        pCore->projectManager()->moveProjectData(oldDir.absoluteFilePath(documentId), newDir.absolutePath());
                    }
                }
            }
        }
        if (pCore->getCurrentProfile()->path() != profile || project->profileChanged(profile)) {
            if (!qFuzzyCompare(pCore->getCurrentProfile()->fps() - ProfileRepository::get()->getProfile(profile)->fps(), 0.)) {
                // Fps was changed, we save the project to an xml file with updated profile and reload project
                // Check if blank project
                if ((project->url().fileName().isEmpty() && !project->isModified()) || !pCore->bin()->hasUserClip()) {
                    // Trying to switch project profile from an empty project
                    pCore->setCurrentProfile(profile);
                    pCore->projectManager()->newFile(profile, false);
                    return;
                }
                pCore->projectManager()->saveWithUpdatedProfile(profile);
            } else {
                bool darChanged = !qFuzzyCompare(pCore->getCurrentProfile()->dar(), ProfileRepository::get()->getProfile(profile)->dar());
                pCore->setCurrentProfile(profile);
                pCore->projectManager()->slotResetProfiles(darChanged);
                slotUpdateDocumentState(true);
            }
        } else if (modified) {
            project->setModified();
        }
    }
    delete w;
}

void MainWindow::slotDisableProxies()
{
    pCore->currentDoc()->setDocumentProperty(QStringLiteral("enableproxy"), QString::number(false));
    pCore->currentDoc()->setModified();
    slotUpdateProxySettings();
}

void MainWindow::slotStopRenderProject()
{
    if (m_renderWidget) {
        m_renderWidget->slotAbortCurrentJob();
    }
}

void MainWindow::updateProjectPath(const QString &path)
{
    if (m_renderWidget) {
        m_renderWidget->resetRenderPath(path);
    } else {
        // Clear render name as project url changed
        QMap<QString, QString> renderProps;
        renderProps.insert(QStringLiteral("renderurl"), QString());
        slotSetDocumentRenderProfile(renderProps);
    }
}

void MainWindow::slotShowExportModule()
{
    // Document 23 : « Exporter… » (⌘⇧E) ouvre le module Export, comme la page Deliver de Resolve.
    // L'ancienne fenêtre reste derrière le bouton « Avancé… » du module et dans le menu.
    if (!pCore->currentDoc() || !m_topBar) {
        slotRenderProject();
        return;
    }
    koraCutSetModule(QStringLiteral("export"));
}

void MainWindow::slotRenderProject()
{
    KdenliveDoc *project = pCore->currentDoc();

    if (!m_renderWidget && project) {
        m_renderWidget = new RenderWidget(project->useProxy(), this);
        connect(m_renderWidget, &RenderWidget::shutdown, this, &MainWindow::slotShutdown);
        connect(m_renderWidget, &RenderWidget::selectedRenderProfile, this, &MainWindow::slotSetDocumentRenderProfile);
        connect(m_renderWidget, &RenderWidget::abortProcess, this, &MainWindow::abortRenderJob);
        connect(pCore.get(), &Core::gotMissingClipsCount, m_renderWidget, &RenderWidget::updateMissingClipsCount);
        connect(pCore.get(), &Core::updateRenderOffset, m_renderWidget, &RenderWidget::updateRenderOffset);
        connect(this, &MainWindow::updateRenderWidgetProfile, m_renderWidget, &RenderWidget::adjustViewToProfile);
        m_renderWidget->setGuides(project->getGuideModel(getCurrentTimeline()->getUuid()));
        m_renderWidget->updateDocumentPath();
        m_renderWidget->setRenderProfile(project->getRenderProperties());
    }

    slotCheckRenderStatus();
    if (m_renderWidget) {
        m_renderWidget->showNormal();
        m_renderWidget->activateWindow();
        m_renderWidget->raise();
    }

    // What are the following lines supposed to do?
    // m_renderWidget->enableAudio(false);
    // m_renderWidget->export_audio;
}

void MainWindow::slotRenderAgain()
{
    // KoraCut : « Exporter de nouveau ». On prépare la fenêtre d'export si besoin, mais on ne la montre pas :
    // le rendu part avec le préréglage et les réglages retenus la dernière fois.
    if (!m_renderWidget) {
        slotRenderProject();
        if (m_renderWidget) {
            m_renderWidget->hide();
        }
    }
    if (!m_renderWidget) {
        return;
    }
    m_renderWidget->slotPrepareExport();
}

void MainWindow::slotCheckRenderStatus()
{
    // Make sure there are no missing clips
    // TODO
    /*if (m_renderWidget)
        m_renderWidget->missingClips(pCore->bin()->hasMissingClips());*/
}

void MainWindow::setRenderingProgress(const QString &url, int progress, int frame)
{
    Q_EMIT setRenderProgress(progress);
    if (m_renderWidget) {
        m_renderWidget->setRenderProgress(url, progress, frame);
    }
}

void MainWindow::setRenderingFinished(const QString &url, int status, const QString &error)
{
    Q_EMIT setRenderProgress(100);
    if (m_renderWidget) {
        m_renderWidget->setRenderStatus(url, status, error);
    }
}

void MainWindow::addProjectClip(const QString &url, const QString &folder)
{
    if (pCore->currentDoc()) {
        QStringList ids = pCore->projectItemModel()->getClipByUrl(QFileInfo(url));
        if (!ids.isEmpty()) {
            // Clip is already in project bin, abort
            return;
        }
        ClipCreator::createClipFromFile(url, folder, pCore->projectItemModel());
    }
}

void MainWindow::addTimelineClip(const QString &url)
{
    if (pCore->currentDoc()) {
        QStringList ids = pCore->projectItemModel()->getClipByUrl(QFileInfo(url));
        if (!ids.isEmpty()) {
            pCore->selectBinClip(ids.constFirst());
            slotInsertClipInsert();
        }
    }
}

void MainWindow::scriptRender(const QString &url)
{
    Q_UNUSED(url)
    slotRenderProject();
    m_renderWidget->slotPrepareExport(true);
}

#ifndef NODBUS
void MainWindow::exitApp()
{
    QApplication::exit(0);
}
#endif

void MainWindow::slotCleanProject()
{
    if (KMessageBox::warningContinueCancel(this, i18n("This will remove all unused clips from your project."), i18n("Clean up project")) ==
        KMessageBox::Cancel) {
        return;
    }
    pCore->projectItemModel()->requestCleanupUnused();
}

void MainWindow::slotUpdateMousePosition(int pos, int duration)
{
    if (!pCore->currentDoc()) {
        return;
    }
    if (duration < 0) {
        duration = getCurrentTimeline()->controller()->duration();
    }
    if (pos >= 0) {
        m_mousePosition = pos;
    }
    switch (m_timeFormatButton->currentItem()) {
    case 0:
        m_timeFormatButton->setText(pCore->currentDoc()->timecode().getTimecodeFromFrames(m_mousePosition) + QStringLiteral(" / ") +
                                    pCore->currentDoc()->timecode().getTimecodeFromFrames(duration));
        break;
    default:
        m_timeFormatButton->setText(QStringLiteral("%1 / %2").arg(m_mousePosition, 6, 10, QLatin1Char('0')).arg(duration, 6, 10, QLatin1Char('0')));
    }
}

void MainWindow::slotUpdateProjectDuration(int duration)
{
    if (pCore->currentDoc()) {
        slotUpdateMousePosition(-1, duration);
    }
    if (m_renderWidget) {
        m_renderWidget->projectDurationChanged(duration);
    }
}

void MainWindow::slotUpdateZoneDuration()
{
    if (m_renderWidget) {
        m_renderWidget->zoneDurationChanged();
    }
}

void MainWindow::slotUpdateDocumentState(bool modified)
{
    m_timelineTabs->updateWindowTitle();
    setWindowModified(modified);
    m_saveAction->setEnabled(modified);
    updateTopBarProject();
    if (m_porte) {
        m_porte->publish(QStringLiteral("projet"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("modifie")}, {QStringLiteral("modifie"), modified}});
    }
}

// ---- La porte de commande (document 18)

void MainWindow::koraCutStartPorte()
{
    if (!m_porte) {
        return;
    }
    koraCutApplyPorteSettings();
    QString reglage = QFileInfo(KSharedConfig::openConfig()->name()).fileName();
    if (reglage.isEmpty()) {
        reglage = QStringLiteral("koracutrc");
    }
    m_porte->listen(reglage, KoraCutPorte::defaultConfigDir());
}

void MainWindow::koraCutApplyPorteSettings()
{
    if (m_porte) {
        m_porte->setEnabled(KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("porteOuverte", true));
    }
}

RenderWidget *MainWindow::koraCutRenderWidget(bool create)
{
    if (!m_renderWidget && create) {
        slotRenderProject();
    }
    return m_renderWidget;
}

int MainWindow::koraCutZoomLevel() const
{
    return m_zoomSlider ? m_zoomSlider->value() : 0;
}

void MainWindow::koraCutSetZoom(int level, bool fit)
{
    if (fit) {
        slotFitZoom();
    } else {
        slotSetZoom(level);
    }
}

QString MainWindow::koraCutCurrentModule() const
{
    return m_topBar ? m_topBar->moduleTabs()->currentLayout() : QString();
}

void MainWindow::koraCutSetModule(const QString &id)
{
    if (!m_topBar) {
        return;
    }
    m_topBar->moduleTabs()->setCurrentLayout(id);
    if (KoraCutModules::isUnderConstruction(id)) {
        showModuleUnderConstruction(id);
    } else {
        showDockArea();
        Q_EMIT pCore->loadLayoutById(id);
    }
}

void MainWindow::slotShowTerminalPanel()
{
    if (!m_terminalDock) {
        return;
    }
    koraCutRevealTerminalDock();
    // Au tour de boucle suivant, pour la même raison que « Monter avec Claude Code » plus bas.
    QTimer::singleShot(0, m_terminalPanel, [this]() { m_terminalPanel->openHere(); });
}

void MainWindow::slotEditWithClaudeCode()
{
    if (!m_terminalDock) {
        return;
    }
    // Au tour de boucle suivant : déclenchée par la porte (actions.declencher), l'action doit d'abord
    // rendre la main, sinon la porte répond « occupé » quand le panneau lit le projet.
    // Depuis le 21 octobre 2026 (KC-008), l'agent part dans une fenêtre de terminal à part, qui survit à
    // KoraCut ; le panneau ne s'ouvre que s'il faut choisir ou installer l'agent (réglage agentFenetre).
    QTimer::singleShot(0, m_terminalPanel, [this]() {
        if (KoraCutAgentsCli::separateWindowWanted() && m_terminalPanel->editInSeparateWindow()) {
            return;
        }
        koraCutRevealTerminalDock();
        m_terminalPanel->editWithAgent();
    });
}

QString MainWindow::koraCutTerminalPlace() const
{
    const QString place = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("terminalPlace", QStringLiteral("droite"));
    static const QStringList known{QStringLiteral("droite"), QStringLiteral("bas"), QStringLiteral("haut"), QStringLiteral("gauche"), QStringLiteral("agents")};
    return known.contains(place) ? place : QStringLiteral("droite");
}

void MainWindow::koraCutDockTerminal(const QString &place)
{
    // Toujours par rapport à la timeline : à côté (moitié-moitié), dessous, dessus ; ou en onglet avec le panneau Agents.
    if (m_terminalDock->isOpen()) {
        m_terminalDock->close();
    }
#if KORACUT_AGENTS
    if (place == QLatin1String("agents") && m_agentDock) {
        if (!m_agentDock->isOpen()) {
            m_agentDock->open();
        }
        m_agentDock->addDockWidgetAsTab(m_terminalDock);
        return;
    }
#endif
    const QSize timeline = m_timelineDock->size();
    KDDockWidgets::Location location = KDDockWidgets::Location_OnRight;
    QSize size(qMax(360, timeline.width() / 2), 0);
    if (place == QLatin1String("gauche")) {
        location = KDDockWidgets::Location_OnLeft;
    } else if (place == QLatin1String("bas")) {
        location = KDDockWidgets::Location_OnBottom;
        size = QSize(0, qBound(140, timeline.height() / 2, 320));
    } else if (place == QLatin1String("haut")) {
        location = KDDockWidgets::Location_OnTop;
        size = QSize(0, qBound(140, timeline.height() / 2, 320));
    }
    mainDockWindow->addDockWidget(m_terminalDock, location, m_timelineDock, size);
}

void MainWindow::koraCutPlaceTerminal(const QString &place)
{
    if (!m_terminalDock) {
        return;
    }
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
    group.writeEntry("terminalPlace", place);
    group.writeEntry("terminalPlaceGeneration", 1);
    group.sync();
    koraCutDockTerminal(koraCutTerminalPlace());
    if (!KdenliveSettings::showtitlebars()) {
        QTimer::singleShot(0, this, []() { Q_EMIT pCore->hideBars(true); });
    }
    m_terminalDock->setAsCurrentTab();
}

void MainWindow::koraCutRevealTerminalDock()
{
    // Un terminal resté ouvert sous la timeline par une version d'avant : la nouvelle place par défaut s'applique une fois.
    if (m_terminalDock->isOpen() && KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("terminalPlaceGeneration", 0) < 1) {
        koraCutPlaceTerminal(koraCutTerminalPlace());
        return;
    }
    if (!m_terminalDock->isOpen()) {
        // La place que KDDockWidgets a gardée (la personne l'a glissé quelque part, ou choisi dans le menu) ; sinon,
        // et la première fois depuis que la place par défaut a changé (4 octobre 2026), la place réglée :
        // à droite de la timeline, moitié-moitié.
        KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
        const bool placedOnce = group.readEntry("terminalPlaceGeneration", 0) >= 1;
        if (placedOnce && m_terminalDock->dockWidget()->hasPreviousDockedLocation()) {
            m_terminalDock->open();
        } else {
            koraCutDockTerminal(koraCutTerminalPlace());
            group.writeEntry("terminalPlaceGeneration", 1);
            group.sync();
        }
        // 25 septembre 2026 : un panneau posé fait réapparaître les barres de titre (« Montage »,
        // « Terminal ») pendant cinq secondes, le délai de Kdenlive. On les retire tout de suite.
        if (!KdenliveSettings::showtitlebars()) {
            QTimer::singleShot(0, this, []() { Q_EMIT pCore->hideBars(true); });
        }
    }
    m_terminalDock->setAsCurrentTab();
}

void MainWindow::slotToggleTerminalPanel()
{
    if (!m_terminalDock) {
        return;
    }
    // Ouvert et le terminal a le focus : on le ferme ; sinon on l'ouvre et on lui donne le focus.
    if (m_terminalDock->isOpen() && m_terminalPanel->isAncestorOf(QApplication::focusWidget())) {
        m_terminalDock->close();
        if (getCurrentTimeline()) {
            getCurrentTimeline()->setFocus();
        }
        return;
    }
    slotShowTerminalPanel();
}

void MainWindow::slotShowDiagnosticPanel()
{
    if (!m_perfDock) {
        return;
    }
    // Fermé, il revient en onglet à côté de l'inspecteur plutôt qu'en petite fenêtre flottante.
    if ((!m_perfDock->isOpen() || m_perfDock->isFloating()) && m_effectStackDock && m_effectStackDock->isOpen()) {
        m_effectStackDock->addDockWidgetAsTab(m_perfDock);
    }
    m_perfDock->open();
    m_perfDock->setAsCurrentTab();
}

void MainWindow::slotShowConsolePanel()
{
    if (!m_consoleDock) {
        return;
    }
    m_consoleDock->open();
    m_consoleDock->setAsCurrentTab();
    m_consolePanel->focusInput();
}

void MainWindow::connectDocument()
{
    KdenliveDoc *project = pCore->currentDoc();
    connect(project, &KdenliveDoc::startAutoSave, pCore->projectManager(), &ProjectManager::slotStartAutoSave);
    connect(project, &KdenliveDoc::reloadEffects, this, &MainWindow::slotReloadEffects);
    KdenliveSettings::setProject_fps(pCore->getCurrentFps());
    slotSwitchTimelineZone(project->getDocumentProperty(QStringLiteral("enableTimelineZone")).toInt() == 1);
    slotUpdateProjectDuration(getCurrentTimeline()->model()->duration() - 1);
    const QUuid uuid = getCurrentTimeline()->getUuid();
    if (m_motionMirror) {
        // Le miroir Motion suit ce document : ses compositions, leurs clips, leurs rendus.
        m_motionMirror->setDocument(project, getCurrentTimeline()->model());
    }
    if (m_porte) {
        m_porte->watchTimeline(getCurrentTimeline()->model());
    }
    m_clipMonitor->updateDocumentUuid();
    pCore->guidesList()->refreshDar();
    connect(m_projectMonitor, &Monitor::multitrackView, getCurrentTimeline()->controller(), &TimelineController::slotMultitrackView, Qt::UniqueConnection);
    connect(m_projectMonitor, &Monitor::activateTrack, getCurrentTimeline()->controller(), &TimelineController::activateTrackAndSelect, Qt::UniqueConnection);
    connect(getCurrentTimeline()->controller(), &TimelineController::timelineClipSelected, this, [&](bool selected) {
        m_loopClip->setEnabled(selected);
        Q_EMIT pCore->library()->enableAddSelection(selected);
    });
    connect(pCore->library(), &LibraryWidget::saveTimelineSelection, getCurrentTimeline()->controller(), &TimelineController::saveTimelineSelection,
            Qt::UniqueConnection);
    connect(pCore->mixer(), &MixerManager::purgeCache, m_projectMonitor, &Monitor::purgeCache);
    connect(m_projectMonitor, &Monitor::zoneUpdated, project, [project](const QPoint &) { project->setModified(); });
    connect(m_clipMonitor, &Monitor::zoneUpdated, project, [project](const QPoint &) { project->setModified(); });
    connect(project, &KdenliveDoc::docModified, this, &MainWindow::slotUpdateDocumentState);

    if (m_renderWidget) {
        slotCheckRenderStatus();
        m_renderWidget->setGuides(pCore->currentDoc()->getGuideModel(uuid));
        m_renderWidget->updateDocumentPath();
        m_renderWidget->setRenderProfile(project->getRenderProperties());
        m_renderWidget->updateMetadataToolTip();
    }

    m_commandStack->setActiveStack(project->commandStack().get());
    m_timelineTabs->updateWindowTitle();
    setWindowModified(project->isModified());
    m_saveAction->setEnabled(project->isModified());
    m_normalEditTool->setChecked(true);
    connect(m_projectMonitor, &Monitor::durationChanged, this, &MainWindow::slotUpdateProjectDuration);
    connect(m_projectMonitor, &Monitor::zoneDurationChanged, this, &MainWindow::slotUpdateZoneDuration);
    connect(m_effectList2, &EffectListWidget::reloadFavorites, getCurrentTimeline(), &TimelineWidget::updateEffectFavorites);
    connect(m_compositionList, &TransitionListWidget::reloadFavorites, getCurrentTimeline(), &TimelineWidget::updateTransitionFavorites);
    connect(pCore.get(), &Core::processDragEnd, getCurrentTimeline(), &TimelineWidget::endDrag);

    // Load master effect zones
    getCurrentTimeline()->controller()->updateMasterZones(getCurrentTimeline()->model()->getMasterEffectZones());

    // Set bin effects status
    bool binEffectsDisabled = project->getDocumentProperty(QStringLiteral("disablebineffects")).toInt() == 1;
    if (binEffectsDisabled) {
        pCore->projectItemModel()->setBinEffectsEnabled(!binEffectsDisabled);
    }

    // Set timeline effects status
    bool disabled = project->getDocumentProperty(QStringLiteral("disabletimelineeffects")).toInt() == 1;
    QAction *disableEffects = actionCollection()->action(QStringLiteral("disable_timeline_effects"));
    if (disableEffects) {
        if (disabled != disableEffects->isChecked()) {
            disableEffects->blockSignals(true);
            disableEffects->setChecked(disabled);
            disableEffects->blockSignals(false);
        }
    }

    m_buttonSelectTool->setChecked(true);
    connect(m_projectMonitorDock, &KDDockWidgets::QtWidgets::DockWidget::isOpenChanged, m_projectMonitor, &Monitor::slotRefreshMonitor, Qt::UniqueConnection);
    connect(m_clipMonitorDock, &KDDockWidgets::QtWidgets::DockWidget::isOpenChanged, m_clipMonitor, &Monitor::slotRefreshMonitor, Qt::UniqueConnection);
    // Initialize audio mixer
    getCurrentTimeline()->model()->rebuildMixer();
    getCurrentTimeline()->focusTimeline();
}

// Les schémas de raccourcis téléchargés sont parfois au format .rc de KDE (groupe [Shortcuts]) :
// on les réécrit dans le format XML que lit KShortcutsDialog. Sorti de slotEditKeys le
// 23 septembre 2026 pour servir aussi à la fenêtre Extensions.
static void koraCutConvertShortcutSchemes(const QList<KNSCore::Entry> &changedEntries)
{
    for (auto &entry : changedEntries) {
        const QStringList files = entry.installedFiles();
        for (auto &file : files) {
            KConfig conf(file);
            KConfigGroup group(&conf, "Shortcuts");
            if (group.exists()) {
                QMap<QString, QString> values = group.entryMap();
                // This is an ui.rc file, convert to xml
                QDomDocument doc;
                auto node = doc.createElement(QStringLiteral("gui"));
                node.setAttribute(QStringLiteral("name"), QStringLiteral("koracut"));
                node.setAttribute(QStringLiteral("version"), QStringLiteral("1"));
                doc.appendChild(node);
                auto list = doc.createElement(QStringLiteral("ActionProperties"));
                node.appendChild(list);
                for (auto i = values.cbegin(), end = values.cend(); i != end; ++i) {
                    if (i.value() == QLatin1String("none")) {
                        continue;
                    }
                    auto e = doc.createElement(QStringLiteral("Action"));
                    e.setAttribute(QStringLiteral("name"), i.key());
                    e.setAttribute(QStringLiteral("shortcut"), i.value());
                    list.appendChild(e);
                }
                QFile f(file);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    qDebug() << "/// COULD NOT open shortcut file : " << file;
                    continue;
                }
                QTextStream stream(&f);
                stream << doc.toString();
                f.close();
            }
        }
    }
}

void MainWindow::slotEditKeys()
{
    KShortcutsDialog *dialog = new KShortcutsDialog(KShortcutsEditor::AllActions, KShortcutsEditor::LetterShortcutsAllowed, this);

    KNSWidgets::Action *downloadKeybordSchemes =
        new KNSWidgets::Action(i18n("Download New Keyboard Schemes…"), QStringLiteral(":data/koracut_keyboardschemes.knsrc"), this);
    connect(downloadKeybordSchemes, &KNSWidgets::Action::dialogFinished, this, [dialog](const QList<KNSCore::Entry> &changedEntries) {
        if (changedEntries.count() > 0) {
            koraCutConvertShortcutSchemes(changedEntries);
            dialog->refreshSchemes();
        }
    });

    dialog->addActionToSchemesMoreButton(downloadKeybordSchemes);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->addCollection(actionCollection(), i18nc("general keyboard shortcuts", "General"));

    // Update the shortcut conflicts list bewtween mainwindow and media browser
    connect(dialog, &KShortcutsDialog::saved, this, [this] {
        pCore->mediaBrowser()->detectShortcutConflicts();
        factory()->refreshActionProperties();
    });
    dialog->configure(true);
}

void MainWindow::slotPreferences()
{
    slotShowPreferencePage(m_lastConfigPage);
}

void MainWindow::slotShowPreferencePage(Kdenlive::ConfigPage page, int option)
{
    /*
     * An instance of your dialog could be already created and could be
     * cached, in which case you want to display the cached dialog
     * instead of creating another one
     */
    if (KConfigDialog::showDialog(QStringLiteral("settings"))) {
        if (page != Kdenlive::NoPage) {
            KdenliveSettingsDialog *d = static_cast<KdenliveSettingsDialog *>(KConfigDialog::exists(QStringLiteral("settings")));
            d->showPage(page, option);
        }
        return;
    }

    // KConfigDialog didn't find an instance of this dialog, so lets
    // create it :

    // Get the mappable actions in localized form
    QMap<QString, QString> actions;
    KActionCollection *collection = actionCollection();
    for (const QString &action_name : std::as_const(m_actionNames)) {
        const QString action_text = KLocalizedString::removeAcceleratorMarker(collection->action(action_name)->text());
        actions[action_text] = action_name;
    }

    auto *dialog = new KdenliveSettingsDialog(actions, m_gpuAllowed, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &KConfigDialog::settingsChanged, this, &MainWindow::updateConfiguration);
    connect(dialog, &KConfigDialog::settingsChanged, this, &MainWindow::configurationChanged);
    connect(dialog, &KdenliveSettingsDialog::doResetConsumer, this, [this](bool fullReset) {
        m_scaleGroup->setEnabled(!KdenliveSettings::external_display());
        pCore->projectManager()->slotResetConsumers(fullReset);
    });
    connect(dialog, &KdenliveSettingsDialog::checkTabPosition, this, &MainWindow::slotCheckTabPosition);
    connect(dialog, &KdenliveSettingsDialog::restartKdenlive, this, &MainWindow::slotRestart);
    connect(dialog, &KdenliveSettingsDialog::updateLibraryFolder, pCore.get(), &Core::updateLibraryPath);
    connect(dialog, &KdenliveSettingsDialog::resetView, this, &MainWindow::resetTimelineTracks);
    connect(KdenliveSettings::self(), &KdenliveSettings::window_backgroundChanged, pCore->monitorManager(), &MonitorManager::updateBgColor);

    dialog->show();
    if (page != Kdenlive::NoPage) {
        dialog->showPage(page, option);
    }
}

void MainWindow::slotCheckTabPosition()
{
    int pos = KDDockWidgets::Config::self().tabsAtBottom() ? 1 : 0;
    if (KdenliveSettings::tabposition() != pos) {
        KDDockWidgets::Config::self().setTabsAtBottom(pos == 1);
    }
}

void MainWindow::slotRestart(bool clean)
{
    if (clean) {
        if (KMessageBox::warningContinueCancel(this,
                                               i18n("This will delete KoraCut's configuration file and restart the application. Do you want to proceed?"),
                                               i18nc("@title:window", "Reset Configuration")) != KMessageBox::Continue) {
            return;
        }
    }
    cleanRestart(clean, false);
}

void MainWindow::cleanRestart(bool clean, bool forceQuit)
{
    m_exitCode = clean ? EXIT_CLEAN_RESTART : EXIT_RESTART;
    QApplication::closeAllWindows();
    if (forceQuit) {
        // qApp->quit();
        qApp->exit(m_exitCode);
    }
}

QStringList MainWindow::koraCutAgentsAtWork() const
{
    QStringList atWork = m_porte ? m_porte->openTurns() : QStringList();
#if KORACUT_AGENTS
    if (m_agentSession && m_agentSession->turnOpen()) {
        atWork.prepend(i18nc("@info the Agents panel, in the list of agents at work", "Agents panel"));
    }
#endif
    return atWork;
}

void MainWindow::koraCutRefreshPilotedTitle()
{
    const QString suffix = QStringLiteral(" · ") + i18nc("@title:window added at the end of the window title while an agent edits", "driven by an agent, do not close");
    QString title = windowTitle();
    if (title.endsWith(suffix)) {
        title.chop(suffix.size());
    }
    if (!koraCutAgentsAtWork().isEmpty()) {
        title += suffix;
    }
    if (title != windowTitle()) {
        m_koraCutTitling = true;
        setWindowTitle(title);
        m_koraCutTitling = false;
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    KXmlGuiWindow::changeEvent(event);
    if (event->type() == QEvent::WindowTitleChange && !m_koraCutTitling) {
        koraCutRefreshPilotedTitle();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << ":::: CLOSE EVENT REQUESTED....";
    KXmlGuiWindow::closeEvent(event);
    if (event->isAccepted()) {
        QApplication::exit(m_exitCode);
        return;
    }
}

void MainWindow::updateConfiguration()
{
    // TODO: we should apply settings to all projects, not only the current one
    m_buttonAudioThumbs->setChecked(KdenliveSettings::audiothumbnails());
    m_buttonVideoThumbs->setChecked(KdenliveSettings::videothumbnails());
    m_buttonShowMarkers->setChecked(KdenliveSettings::showmarkers());

    // Update list of transcoding profiles
    buildDynamicActions();
    loadClipActions();
}

void MainWindow::slotSwitchVideoThumbs()
{
    KdenliveSettings::setVideothumbnails(!KdenliveSettings::videothumbnails());
    m_buttonVideoThumbs->setChecked(KdenliveSettings::videothumbnails());
}

void MainWindow::slotSwitchAudioThumbs()
{
    KdenliveSettings::setAudiothumbnails(!KdenliveSettings::audiothumbnails());
    pCore->bin()->checkAudioThumbs();
    m_buttonAudioThumbs->setChecked(KdenliveSettings::audiothumbnails());
    // Preserve icon colorization based on zoom level after toggle
    updateAudioWaveformActionIcon();
}

void MainWindow::slotSwitchMarkersComments()
{
    KdenliveSettings::setShowmarkers(!KdenliveSettings::showmarkers());
    m_buttonShowMarkers->setChecked(KdenliveSettings::showmarkers());
}

void MainWindow::slotSwitchSnap()
{
    KdenliveSettings::setSnaptopoints(!KdenliveSettings::snaptopoints());
    m_buttonSnap->setChecked(KdenliveSettings::snaptopoints());
}

void MainWindow::slotSwitchClipOverlays()
{
    KdenliveSettings::setShowClipOverlays(!KdenliveSettings::showClipOverlays());
    m_buttonHideClipOverlays->setChecked(KdenliveSettings::showClipOverlays());
}

void MainWindow::slotShowTimelineTags()
{
    KdenliveSettings::setTagsintimeline(!KdenliveSettings::tagsintimeline());
    m_buttonTimelineTags->setChecked(KdenliveSettings::tagsintimeline());
    // Reset view to update timeline colors
    getCurrentTimeline()->model()->_resetView();
}

void MainWindow::slotMouseZoomOnPlayhead()
{
    KdenliveSettings::setTimelinemousezoomonplayhead(!KdenliveSettings::timelinemousezoomonplayhead());
    m_buttonMouseZoomOnPlayhead->setChecked(KdenliveSettings::timelinemousezoomonplayhead());
}

void MainWindow::slotDeleteItem()
{
    if (QApplication::focusWidget() != nullptr) {
        for (auto &bin : m_binWidgets) {
            if (bin->isAncestorOf(QApplication::focusWidget())) {
                bin->slotDeleteClip();
                return;
            }
        }
    }
    if (QApplication::focusWidget() != nullptr && pCore->textEditWidget()->isAncestorOf(QApplication::focusWidget())) {
        pCore->textEditWidget()->deleteItem();
    } else {
        QWidget *widget = QApplication::focusWidget();
        while ((widget != nullptr) && widget != this) {
            if (widget == m_effectStackDock) {
                m_assetPanel->deleteCurrentEffect();
                return;
            }
            if (widget == pCore->guidesList()) {
                pCore->guidesList()->removeGuide();
                return;
            }
            widget = widget->parentWidget();
        }

        // effect stack has no focus
        // KoraCut, 22 septembre 2026 : le geste de DaVinci. Quand aucun clip n'est sélectionné
        // mais qu'une zone est posée dans la timeline, Supprimer retire cette zone. Sans cela,
        // « je sélectionne un bout de la timeline et j'appuie sur supprimer » ne faisait rien.
        // La zone part sans décaler le reste (relever) : c'est la même règle que Q et W, le
        // geste simple ne déplace jamais ce qu'on n'a pas touché. ⇧ referme le trou.
        TimelineController *controleur = getCurrentTimeline()->controller();
        if (controleur->zoneOut() > controleur->zoneIn() && controleur->getModel() && controleur->getModel()->getCurrentSelection().empty()) {
            controleur->extractZone(QPoint(controleur->zoneIn(), controleur->zoneOut()), true);
            return;
        }
        controleur->deleteSelectedClips();
    }
}

void MainWindow::slotAddClipMarker()
{
    std::shared_ptr<ProjectClip> clip(nullptr);
    GenTime pos;
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->addMarker();
        return;
    } else {
        clip = m_clipMonitor->currentController();
        pos = GenTime(m_clipMonitor->position(), pCore->getCurrentFps());
    }
    if (!clip) {
        m_messageLabel->setMessage(i18n("Cannot find clip to add marker"), ErrorMessage);
        return;
    }
    clip->getMarkerModel()->editMarkerGui(pos, this, true, clip.get());
}

void MainWindow::slotDeleteClipMarker(bool allowGuideDeletion)
{
    std::shared_ptr<ProjectClip> clip(nullptr);
    GenTime pos;
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->deleteMarker();
        return;
    } else {
        clip = m_clipMonitor->currentController();
        pos = GenTime(m_clipMonitor->position(), pCore->getCurrentFps());
    }
    if (!clip) {
        m_messageLabel->setMessage(i18n("Cannot find clip to remove marker"), ErrorMessage);
        return;
    }

    bool markerFound = false;
    clip->getMarkerModel()->getMarker(pos, &markerFound);
    if (!markerFound) {
        if (allowGuideDeletion && m_projectMonitor->isActive()) {
            slotDeleteGuide();
        } else {
            m_messageLabel->setMessage(i18n("No marker found at cursor time"), ErrorMessage);
        }
        return;
    }
    clip->getMarkerModel()->removeMarker(pos);
}

void MainWindow::slotDeleteAllClipMarkers()
{
    std::shared_ptr<ProjectClip> clip(nullptr);
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->deleteAllMarkers();
        return;
    } else {
        clip = m_clipMonitor->currentController();
    }
    if (!clip) {
        m_messageLabel->setMessage(i18n("Cannot find clip to remove marker"), ErrorMessage);
        return;
    }
    bool ok = clip->getMarkerModel()->removeAllMarkers();
    if (!ok) {
        m_messageLabel->setMessage(i18n("An error occurred while deleting markers"), ErrorMessage);
        return;
    }
}

void MainWindow::slotDeleteAllSequenceMarkers()
{
    auto model = pCore->currentDoc()->getGuideModel(pCore->currentTimelineId());
    bool ok = model->removeAllMarkers();
    if (!ok) {
        m_messageLabel->setMessage(i18n("An error occurred while deleting markers"), ErrorMessage);
        return;
    }
}

void MainWindow::slotEditClipMarker()
{
    std::shared_ptr<ProjectClip> clip(nullptr);
    GenTime pos;
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->editMarker();
        return;
    } else {
        clip = m_clipMonitor->currentController();
        pos = GenTime(m_clipMonitor->position(), pCore->getCurrentFps());
    }
    if (!clip) {
        m_messageLabel->setMessage(i18n("Cannot find clip to edit marker"), ErrorMessage);
        return;
    }

    bool markerFound = false;
    clip->getMarkerModel()->getMarker(pos, &markerFound);
    if (!markerFound) {
        m_messageLabel->setMessage(i18n("No marker found at cursor time"), ErrorMessage);
        return;
    }

    clip->getMarkerModel()->editMarkerGui(pos, this, false, clip.get());
    // Focus back clip monitor
    m_clipMonitor->setFocus();
}

void MainWindow::slotAddMarkerGuideQuickly()
{
    if (!getCurrentTimeline() || !pCore->currentDoc()) {
        return;
    }
    if (m_clipMonitor->isActive()) {
        QMap<int, QString> marker;
        marker.insert(m_clipMonitor->position(), QString());
        pCore->bin()->addClipMarker(m_clipMonitor->activeClipId(), marker);
    } else {
        int selectedClip = getCurrentTimeline()->controller()->getMainSelectedItem();
        if (selectedClip == -1) {
            // Add timeline guide
            getCurrentTimeline()->controller()->switchGuide();
        } else {
            // Add marker to main clip
            getCurrentTimeline()->controller()->addQuickMarker(selectedClip);
        }
    }
}

void MainWindow::slotAddMarkerWithCategory()
{
    if (!getCurrentTimeline() || !pCore->currentDoc()) {
        return;
    }

    auto *caller = qobject_cast<QAction *>(QObject::sender());
    if (!caller) {
        return;
    }
    // subtract 1 due to default category descriptions being 1-indexed
    int category = caller->data().toInt() - 1;

    // check if category exists
    if (!pCore->markerTypes.contains(category)) {
        pCore->displayMessage(i18n("Marker category does not exist"), ErrorMessage);
        return;
    }
    int currentCategory = KdenliveSettings::default_marker_type();

    KdenliveSettings::setDefault_marker_type(category);
    if (m_clipMonitor->isActive()) {
        QMap<int, QString> marker;
        marker.insert(m_clipMonitor->position(), QString());
        pCore->bin()->addClipMarker(m_clipMonitor->activeClipId(), marker);
    } else {
        int selectedClip = getCurrentTimeline()->controller()->getMainSelectedItem();
        if (selectedClip == -1) {
            // Add timeline guide
            getCurrentTimeline()->controller()->switchGuide();
        } else {
            // Add marker to main clip
            getCurrentTimeline()->controller()->addQuickMarker(selectedClip);
        }
    }
    // return to previously-chosen default category
    KdenliveSettings::setDefault_marker_type(currentCategory);
}

void MainWindow::slotAddMarkersAtGaps()
{
    getCurrentTimeline()->controller()->addMarkersAtGaps();
}

void MainWindow::slotAddMarkersAtGapsOnTrack()
{
    getCurrentTimeline()->controller()->addMarkersAtGapsOnTrack();
}

void MainWindow::slotAddGuide()
{
    getCurrentTimeline()->controller()->switchGuide(-1, false, true);
}

void MainWindow::slotInsertSpace()
{
    getCurrentTimeline()->controller()->insertSpace();
}

void MainWindow::slotRemoveSpace()
{
    getCurrentTimeline()->controller()->removeSpace(-1, -1, false);
}

void MainWindow::slotRemoveSpaceInAllTracks()
{
    getCurrentTimeline()->controller()->removeSpace(-1, -1, true);
}

void MainWindow::slotRemoveAllSpacesInTrack()
{
    getCurrentTimeline()->controller()->removeTrackSpaces(-1, -1);
}

void MainWindow::slotRemoveAllClipsInTrack()
{
    getCurrentTimeline()->controller()->removeTrackClips(-1, -1);
}

void MainWindow::slotSeparateAudioChannel()
{
    KdenliveSettings::setDisplayallchannels(!KdenliveSettings::displayallchannels());
}

void MainWindow::slotAutoTrackHeight(bool enable)
{
    KdenliveSettings::setAutotrackheight(enable);
    Q_EMIT pCore->autoTrackHeight(enable);
}

void MainWindow::slotNormalizeAudioChannel(bool normalize)
{
    KdenliveSettings::setNormalizechannels(normalize);
}

void MainWindow::slotInsertTrack()
{
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
    getCurrentTimeline()->controller()->beginAddTrack(-1);
}

void MainWindow::slotDeleteTrack()
{
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
    getCurrentTimeline()->controller()->deleteMultipleTracks(-1);
}

void MainWindow::slotMoveTrackUp()
{
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
    getCurrentTimeline()->controller()->moveTrackUp();
}

void MainWindow::slotMoveTrackDown()
{
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
    getCurrentTimeline()->controller()->moveTrackDown();
}

void MainWindow::slotSwitchTrackAudioStream()
{
    getCurrentTimeline()->showTargetMenu();
}

void MainWindow::slotShowTrackRec(bool checked)
{
    if (checked) {
        pCore->mixer()->monitorAudio(getCurrentTimeline()->controller()->activeTrack(), checked);
    } else {
        pCore->mixer()->monitorAudio(pCore->mixer()->recordTrack(), false);
    }
}

void MainWindow::slotSelectTrack()
{
    getCurrentTimeline()->controller()->selectCurrentTrack();
}

void MainWindow::slotSelectAllTracks()
{
    if (QApplication::focusWidget() != nullptr) {
        if (QApplication::focusWidget()->parentWidget() != nullptr) {
            for (auto &bin : m_binWidgets) {
                if (bin->isAncestorOf(QApplication::focusWidget())) {
                    bin->selectAll();
                    return;
                }
            }
        }
        if (QApplication::focusWidget()->objectName() == QLatin1String("guides_list")) {
            pCore->guidesList()->selectAll();
            return;
        }
    }
    getCurrentTimeline()->controller()->selectAll();
}

void MainWindow::slotUnselectAllTracks()
{
    getCurrentTimeline()->model()->requestClearSelection();
}

void MainWindow::slotEditGuide()
{
    getCurrentTimeline()->controller()->editGuide();
}

void MainWindow::slotSearchGuide()
{
    KDDockWidgets::QtWidgets::DockWidget *dock = qobject_cast<KDDockWidgets::QtWidgets::DockWidget *>(pCore->guidesList()->parentWidget());
    if (dock) {
        dock->open();
        dock->setAsCurrentTab();
    }
    pCore->guidesList()->filter_line->setFocus();
}

void MainWindow::slotSearchBin()
{
    pCore->activeBin()->searchLine()->setFocus();
}

void MainWindow::slotExportGuides()
{
    pCore->currentDoc()
        ->getGuideModel(getCurrentTimeline()->getUuid())
        ->exportGuidesGui(this, GenTime(getCurrentTimeline()->controller()->duration() - 1, pCore->getCurrentFps()));
}

void MainWindow::slotLockGuides(bool lock)
{
    KdenliveSettings::setLockedGuides(lock);
}

void MainWindow::slotDeleteGuide()
{
    getCurrentTimeline()->controller()->switchGuide(-1, true);
}

void MainWindow::slotDeleteAllGuides()
{
    pCore->currentDoc()->getGuideModel(getCurrentTimeline()->getUuid())->removeAllMarkers();
}

void MainWindow::slotDuplicateTimelineClip()
{
    getCurrentTimeline()->controller()->duplicateClip();
}

void MainWindow::slotCutTimelineClip()
{
    getCurrentTimeline()->controller()->cutClipUnderCursor();
}

void MainWindow::slotReplaceTimelineClip()
{
    getCurrentTimeline()->controller()->replaceClip();
}

void MainWindow::slotCutTimelineAllClips()
{
    getCurrentTimeline()->controller()->cutAllClipsUnderCursor();
}

void MainWindow::slotInsertClipOverwrite()
{
    const QString &binId = m_clipMonitor->activeClipId();
    if (binId.isEmpty()) {
        // No clip in monitor
        return;
    }
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    bool res = getCurrentTimeline()->controller()->insertZone(binId, m_clipMonitor->getZoneInfo(), true, undo, redo);
    if (res) {
        pCore->pushUndo(undo, redo, i18n("Overwrite zone"));
    } else {
        pCore->displayMessage(i18n("Could not insert zone"), ErrorMessage);
        undo();
    }
}

void MainWindow::slotInsertClipInsert()
{
    const QString &binId = m_clipMonitor->activeClipId();
    if (binId.isEmpty()) {
        // No clip in monitor
        pCore->displayMessage(i18n("No clip selected in project bin"), ErrorMessage);
        return;
    }
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    bool res = getCurrentTimeline()->controller()->insertZone(binId, m_clipMonitor->getZoneInfo(), false, undo, redo);
    if (res) {
        pCore->pushUndo(undo, redo, i18n("Insert zone"));
    } else {
        pCore->displayMessage(i18n("Could not insert zone"), ErrorMessage);
        undo();
    }
}

void MainWindow::slotExtractZone()
{
    getCurrentTimeline()->controller()->extractZone(m_clipMonitor->getZoneInfo());
}

void MainWindow::slotExtractClip()
{
    // KoraCut : sans sélection mais avec une zone, ⇧⌫ retire la zone et referme le trou.
    TimelineController *controleur = getCurrentTimeline()->controller();
    if (controleur->zoneOut() > controleur->zoneIn() && controleur->getModel() && controleur->getModel()->getCurrentSelection().empty()) {
        controleur->extractZone(QPoint(controleur->zoneIn(), controleur->zoneOut()));
        return;
    }
    controleur->extract();
}

void MainWindow::slotSaveZoneToBin()
{
    getCurrentTimeline()->controller()->saveZone();
}

void MainWindow::slotLiftZone()
{
    getCurrentTimeline()->controller()->extractZone(m_clipMonitor->getZoneInfo(), true);
}

void MainWindow::slotPreviewRender()
{
    if (pCore->currentDoc()) {
        // KoraCut : la zone du moniteur Programme, ou toute la séquence.
        getCurrentTimeline()->controller()->renderMonitorZone();
    }
}

void MainWindow::slotStopPreviewRender()
{
    if (pCore->currentDoc()) {
        getCurrentTimeline()->controller()->stopPreviewRender();
    }
}

void MainWindow::slotDefinePreviewRender()
{
    if (pCore->currentDoc()) {
        getCurrentTimeline()->controller()->addPreviewRange(true);
    }
}

void MainWindow::slotRemovePreviewRender()
{
    if (pCore->currentDoc()) {
        getCurrentTimeline()->controller()->addPreviewRange(false);
    }
}

void MainWindow::slotClearPreviewRender(bool resetZones)
{
    if (pCore->currentDoc()) {
        if (resetZones) {
            // KoraCut : « Effacer les rendus » libère aussi le disque.
            getCurrentTimeline()->controller()->clearRenders();
        } else {
            getCurrentTimeline()->controller()->clearPreviewRange(resetZones);
        }
    }
}

void MainWindow::slotSelectTimelineClip()
{
    getCurrentTimeline()->controller()->selectCurrentItem(KdenliveObjectType::TimelineClip, true);
}

void MainWindow::slotSelectTimelineZone()
{
    getCurrentTimeline()->controller()->setZoneToSelection();
}
void MainWindow::slotSelectTimelineTransition()
{
    bool res = getCurrentTimeline()->controller()->selectCurrentItem(KdenliveObjectType::TimelineComposition, true, false, false);
    if (!res) {
        getCurrentTimeline()->controller()->selectCurrentItem(KdenliveObjectType::TimelineMix, true);
    }
}

void MainWindow::slotDeselectTimelineClip()
{
    getCurrentTimeline()->controller()->selectCurrentItem(KdenliveObjectType::TimelineClip, false);
}

void MainWindow::slotDeselectTimelineTransition()
{
    bool res = getCurrentTimeline()->controller()->selectCurrentItem(KdenliveObjectType::TimelineComposition, false, false, false);
    if (!res) {
        getCurrentTimeline()->controller()->selectCurrentItem(KdenliveObjectType::TimelineMix, false);
    }
}

void MainWindow::slotSelectAddTimelineClip()
{
    getCurrentTimeline()->controller()->selectCurrentItem(KdenliveObjectType::TimelineClip, true, true);
}

void MainWindow::slotSelectAddTimelineTransition()
{
    getCurrentTimeline()->controller()->selectCurrentItem(KdenliveObjectType::TimelineComposition, true, true);
}

void MainWindow::slotGroupClips()
{
    getCurrentTimeline()->controller()->groupSelection();
}

void MainWindow::slotUnGroupClips()
{
    getCurrentTimeline()->controller()->unGroupSelection();
}

void MainWindow::slotEditItemDuration()
{
    getCurrentTimeline()->controller()->editItemDuration();
}

void MainWindow::slotAddProjectClip(const QUrl &url, const QString &folderInfo)
{
    pCore->activeBin()->droppedUrls(QList<QUrl>() << url, folderInfo);
}

void MainWindow::slotAddTextNote(const QString &text)
{
    pCore->projectManager()->slotAddTextNote(text);
}

void MainWindow::slotAddProjectClipList(const QList<QUrl> &urls)
{
    pCore->activeBin()->droppedUrls(urls);
}

void MainWindow::slotAddTransition(QAction *result)
{
    // KoraCut, 23 septembre 2026 : cette fonction était vide (un « TODO refac » hérité, code commenté).
    // Le menu Compositions, la palette ⌘K et les raccourcis de composition ne faisaient donc rien. Elle
    // pose maintenant la composition sur le clip vidéo sélectionné ; dans une sélection vidéo + son
    // (un clip de caméra), on prend la partie vidéo.
    if (!result) {
        return;
    }
    const QString id = result->data().toString();
    TimelineWidget *timeline = getCurrentTimeline();
    if (id.isEmpty() || !timeline || !timeline->controller()) {
        return;
    }
    auto model = timeline->model();
    int target = -1;
    for (int item : timeline->controller()->selection()) {
        if (model->isClip(item) && !model->isAudioTrack(model->getClipTrackId(item))) {
            target = item;
            break;
        }
    }
    // Sinon le clip principal, celui que Kdenlive prend pour cible partout ailleurs (le dernier cliqué).
    if (target < 0) {
        const int main = timeline->controller()->getMainSelectedClip();
        if (main > -1 && !model->isAudioTrack(model->getClipTrackId(main))) {
            target = main;
        }
    }
    if (target < 0) {
        pCore->displayMessage(i18nc("@info:status", "First select the video clip that will receive the composition."), ErrorMessage, 3000);
        return;
    }
    timeline->controller()->addCompositionToClip(id, target, 0);
}

void MainWindow::slotAddEffect(QAction *result)
{
    if (!result) {
        return;
    }
    QString effectId = result->data().toString();
    addEffect(effectId);
}

void MainWindow::addEffect(const QString &effectId)
{
    if (m_assetPanel->effectStackOwner().type == KdenliveObjectType::BinClip || m_clipMonitor->isActive()) {
        // Pass the command to bin
        pCore->activeBin()->slotAddEffect({}, {effectId});
    } else if (m_assetPanel->effectStackOwner().type == KdenliveObjectType::TimelineTrack ||
               m_assetPanel->effectStackOwner().type == KdenliveObjectType::Master) {
        if (!m_assetPanel->addEffect(effectId)) {
            pCore->displayMessage(i18n("Cannot add effect to active item"), ErrorMessage);
        }
    } else {
        // Add effect to the current timeline selection
        QVariantMap effectData;
        effectData.insert(QStringLiteral("kdenlive/effect"), effectId);
        getCurrentTimeline()->controller()->addAsset(effectData);
    }
}

void MainWindow::slotZoomIn(bool zoomOnMouse)
{
    slotSetZoom(m_zoomSlider->value() - 1, KdenliveSettings::timelinemousezoomonplayhead() ? false : zoomOnMouse);
    slotShowZoomSliderToolTip();
}

void MainWindow::slotZoomOut(bool zoomOnMouse)
{
    slotSetZoom(m_zoomSlider->value() + 1, KdenliveSettings::timelinemousezoomonplayhead() ? false : zoomOnMouse);
    slotShowZoomSliderToolTip();
}

void MainWindow::slotFitZoom()
{
    Q_EMIT m_timelineTabs->fitZoom();
}

void MainWindow::slotSetZoom(int value, bool zoomOnMouse)
{
    value = qBound(m_zoomSlider->minimum(), value, m_zoomSlider->maximum());
    Q_EMIT m_timelineTabs->changeZoom(value, zoomOnMouse);
    updateZoomSlider(value);
}

void MainWindow::updateZoomSlider(int value)
{
    slotUpdateZoomSliderToolTip(value);
    KdenliveDoc *project = pCore->currentDoc();
    if (project) {
        project->setZoom(pCore->currentTimelineId(), value);
    }
    m_zoomOut->setEnabled(value < m_zoomSlider->maximum());
    m_zoomIn->setEnabled(value > m_zoomSlider->minimum());
    QSignalBlocker blocker(m_zoomSlider);
    m_zoomSlider->setValue(value);
}

void MainWindow::slotShowZoomSliderToolTip(int zoomlevel)
{
    if (zoomlevel != -1) {
        slotUpdateZoomSliderToolTip(zoomlevel);
    }

    QPoint global = m_zoomSlider->rect().topLeft();
    global.ry() += m_zoomSlider->height() / 2;
    QHelpEvent toolTipEvent(QEvent::ToolTip, QPoint(0, 0), m_zoomSlider->mapToGlobal(global));
    QApplication::sendEvent(m_zoomSlider, &toolTipEvent);
}

void MainWindow::slotUpdateZoomSliderToolTip(int zoomlevel)
{
    int max = m_zoomSlider->maximum() + 1;
    m_zoomSlider->setToolTip(i18n("Zoom Level: %1/%2", max - zoomlevel, max));
}

void MainWindow::customEvent(QEvent *e)
{
    if (e->type() == QEvent::User) {
        m_messageLabel->setMessage(static_cast<MltErrorEvent *>(e)->message(), MltError);
    }
}

void MainWindow::slotSnapRewind()
{
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->gotoPreviousSnap();
    } else {
        m_clipMonitor->slotSeekToPreviousSnap();
    }
}

void MainWindow::slotSnapForward()
{
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->gotoNextSnap();
    } else {
        m_clipMonitor->slotSeekToNextSnap();
    }
}

void MainWindow::slotGuideRewind()
{
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->gotoPreviousGuide();
    } else {
        m_clipMonitor->slotSeekToPreviousSnap();
    }
}

void MainWindow::slotGuideForward()
{
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->gotoNextGuide();
    } else {
        m_clipMonitor->slotSeekToNextSnap();
    }
}

void MainWindow::slotClipStart()
{
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->seekCurrentClip(false);
    } else {
        m_clipMonitor->slotStart();
    }
}

void MainWindow::slotClipEnd()
{
    if (m_projectMonitor->isActive()) {
        getCurrentTimeline()->controller()->seekCurrentClip(true);
    } else {
        m_clipMonitor->slotEnd();
    }
}

void MainWindow::slotChangeTool(QAction *action)
{
    ToolType::ProjectTool activeTool = ToolType::SelectTool;

    // if(action == m_buttonSelectTool) covered by default value
    if (action == m_buttonRazorTool) {
        activeTool = ToolType::RazorTool;
    } else if (action == m_buttonSpacerTool) {
        activeTool = ToolType::SpacerTool;
    }
    if (action == m_buttonRippleTool) {
        activeTool = ToolType::RippleTool;
    }
    if (action == m_buttonRollTool) {
        activeTool = ToolType::RollTool;
    }
    if (action == m_buttonSlipTool) {
        activeTool = ToolType::SlipTool;
    }
    if (action == m_buttonSlideTool) {
        activeTool = ToolType::SlideTool;
    }
    if (action == m_buttonMulticamTool) {
        activeTool = ToolType::MulticamTool;
    };
    slotSetTool(activeTool);
}

void MainWindow::slotChangeEdit(QAction *action)
{
    TimelineMode::EditMode mode = TimelineMode::NormalEdit;
    if (action == m_overwriteEditTool) {
        mode = TimelineMode::OverwriteEdit;
    } else if (action == m_insertEditTool) {
        mode = TimelineMode::InsertEdit;
    }
    getCurrentTimeline()->model()->setEditMode(mode);
    showToolMessage();
    if (mode == TimelineMode::InsertEdit) {
        // Disable spacer tool in insert mode
        if (m_buttonSpacerTool->isChecked()) {
            m_buttonSelectTool->setChecked(true);
            slotSetTool(ToolType::SelectTool);
        }
        m_buttonSpacerTool->setEnabled(false);
    } else {
        m_buttonSpacerTool->setEnabled(true);
    }
}

void MainWindow::disableMulticam()
{
    if (m_activeTool == ToolType::MulticamTool) {
        m_buttonSelectTool->setChecked(true);
        slotSetTool(ToolType::SelectTool);
    }
}

ToolType::ProjectTool MainWindow::activeTool()
{
    return m_activeTool;
}

void MainWindow::slotSetTool(ToolType::ProjectTool tool)
{
    if (m_activeTool == ToolType::MulticamTool) {
        // End multicam operation
        pCore->monitorManager()->switchMultiTrackView(false);
        pCore->monitorManager()->slotStopMultiTrackMode();
    }
    m_activeTool = tool;
    Q_EMIT pCore->activeToolChanged();
    if (pCore->currentDoc()) {
        showToolMessage();
        getCurrentTimeline()->controller()->updateTrimmingMode();
    }
    if (m_activeTool == ToolType::MulticamTool) {
        // Start multicam operation
        pCore->monitorManager()->switchMultiTrackView(true);
        pCore->monitorManager()->slotStartMultiTrackMode();
    }
}

void MainWindow::showToolMessage()
{
    // KoraCut, 23 septembre 2026 : l'aide de l'outil réécrite, courte, avec le nom des touches tel
    // que le système l'écrit (⇧ ⌥ ⌘ ⌃ sur Mac, Maj Alt Ctrl ailleurs). L'ancienne, traduite mot à
    // mot, disait « Windows + ALT » et « Ctrl » sur un Mac, et poussait la barre hors de la fenêtre.
    // Une séquence sans touche ne s'écrit pas : on l'écrit avec « A », puis on retire le « A ».
    auto key = [](Qt::KeyboardModifiers modifiers) {
        QString text = QKeySequence(QKeyCombination(modifiers, Qt::Key_A)).toString(QKeySequence::NativeText);
        text.chop(1);
        while (text.endsWith(QLatin1Char('+'))) {
            text.chop(1);
        }
        return text;
    };
    const QString shift = key(Qt::ShiftModifier);
    const QString ctrl = key(Qt::ControlModifier);
#ifdef Q_OS_WIN
    const QString moveGrouped = key(Qt::MetaModifier);
#else
    const QString moveGrouped = key(Qt::MetaModifier | Qt::AltModifier);
#endif
    QString message;
    QString toolLabel;
    if (m_buttonSelectTool->isChecked() || m_buttonRippleTool->isChecked()) {
        message = i18nc("@info:status tool help, %1 %2 %3 are modifier keys",
                        "%1 drag: lasso · %1 click: add to selection · %2 drag: grouped clip to another track · %3 drag: scroll", shift, moveGrouped,
                        ctrl);
    } else if (m_buttonRazorTool->isChecked()) {
        message = i18nc("@info:status tool help, %1 is a modifier key", "Click: cut · %1: preview the cut frame", shift);
    } else if (m_buttonSpacerTool->isChecked()) {
        message = i18nc("@info:status tool help, %1 %2 are modifier keys", "%1: current track only · %2: move the guides too", ctrl, shift);
    } else if (m_buttonSlipTool->isChecked()) {
        message = i18nc("@info:status tool help, %1 is a modifier key", "Drag a clip: slip its content · %1 click: add to selection", shift);
    } else if (m_buttonMulticamTool->isChecked()) {
        message = i18nc("@info:status tool help", "Click a view in the project monitor: keep that camera");
    }
    TimelineMode::EditMode mode = TimelineMode::NormalEdit;
    if (getCurrentTimeline() && getCurrentTimeline()->model()) {
        mode = getCurrentTimeline()->model()->editMode();
    }

    // Store the current edit mode for palette updates
    m_currentEditMode = mode;

    // KoraCut, 23 septembre 2026 : la barre d'état ne répète plus le nom de l'outil, que la barre
    // d'outils de la timeline montre déjà (« 11 - Refonte UI-UX », barre d'état). Elle ne garde que
    // le mode d'édition quand il n'est pas le mode normal : Insertion ou Écrasement change ce que
    // fait un dépôt, ça doit se voir.
    Q_UNUSED(toolLabel)
    QString modeLabel;
    if (mode == TimelineMode::InsertEdit) {
        modeLabel = i18n("Insert");
    } else if (mode == TimelineMode::OverwriteEdit) {
        modeLabel = i18n("Overwrite");
    }

    applyToolMessageStyling();

    m_trimLabel->setText(modeLabel);
    m_trimLabel->setVisible(!modeLabel.isEmpty());
    m_messageLabel->setKeyMap(message);
}

void MainWindow::setWidgetKeyBinding(const QString &mess)
{
    m_messageLabel->setKeyMap(mess);
}

void MainWindow::showKeyBinding(const QString &text)
{
    m_messageLabel->setTmpKeyMap(text);
}

void MainWindow::slotCopy()
{
    QWidget *widget = QApplication::focusWidget();
    while ((widget != nullptr) && widget != this) {
        if (widget == m_effectStackDock) {
            m_assetPanel->sendStandardCommand(KStandardAction::Copy);
            return;
        } else if (widget == m_effectList2 && m_effectList2->infoPanelIsFocused()) {
            m_effectList2->processCopy();
            return;
        } else if (widget == m_compositionList && m_compositionList->infoPanelIsFocused()) {
            m_compositionList->processCopy();
            return;
        }
        widget = widget->parentWidget();
    }
    getCurrentTimeline()->controller()->copyItem();
}

void MainWindow::slotCut()
{
    QWidget *widget = QApplication::focusWidget();
    while ((widget != nullptr) && widget != this) {
        if (widget == m_effectStackDock) {
            // Todo: cut effect?
            // m_assetPanel->sendStandardCommand(KStandardAction::Copy);
            return;
        }
        widget = widget->parentWidget();
    }
    getCurrentTimeline()->controller()->cutItem();
}

void MainWindow::slotPaste()
{
    QWidget *widget = QApplication::focusWidget();
    while ((widget != nullptr) && widget != this) {
        if (widget == m_effectStackDock) {
            m_assetPanel->sendStandardCommand(KStandardAction::Paste);
            return;
        }
        widget = widget->parentWidget();
    }
    getCurrentTimeline()->controller()->pasteItem();
}

void MainWindow::slotPasteEffects()
{
    getCurrentTimeline()->controller()->pasteEffects();
}

void MainWindow::slotClipInTimeline(const QString &clipId, const QList<int> &ids)
{
    Q_UNUSED(clipId)
    QMenu *inTimelineMenu = static_cast<QMenu *>(factory()->container(QStringLiteral("clip_in_timeline"), this));
    QList<QAction *> actionList;
    for (int i = 0; i < ids.count(); ++i) {
        ObjectId oid(KdenliveObjectType::TimelineClip, ids.at(i), pCore->currentTimelineId());
        QString track = getCurrentTimeline()->controller()->getTrackNameFromIndex(pCore->getItemTrack(oid));
        QString start = pCore->currentDoc()->timecode().getTimecodeFromFrames(pCore->getItemPosition(oid));
        int j = 0;
        QAction *a = new QAction(track + QStringLiteral(": ") + start, inTimelineMenu);
        a->setData(ids.at(i));
        connect(a, &QAction::triggered, this, &MainWindow::slotSelectClipInTimeline);
        while (j < actionList.count()) {
            if (actionList.at(j)->text() > a->text()) {
                break;
            }
            j++;
        }
        actionList.insert(j, a);
    }
    QList<QAction *> list = inTimelineMenu->actions();
    unplugActionList(QStringLiteral("timeline_occurences"));
    qDeleteAll(list);
    plugActionList(QStringLiteral("timeline_occurences"), actionList);

    if (actionList.isEmpty()) {
        inTimelineMenu->setEnabled(false);
    } else {
        inTimelineMenu->setEnabled(true);
    }
}

void MainWindow::raiseBin(bool unconditionally)
{
    Bin *bin = activeBin();
    if (bin) {
        KDDockWidgets::QtWidgets::DockWidget *dock = qobject_cast<KDDockWidgets::QtWidgets::DockWidget *>(bin->parentWidget());
        if (!unconditionally) {
            if (dock && dock->asDockWidgetController()->isTabbed()) {
                if (dock->asGroupController()->containsDockWidget(m_clipMonitorDock->asDockWidgetController())) {
                    return;
                }
            }
        }
        bin->focusBinView();
        dock->open();
        dock->setAsCurrentTab();
    }
}

void MainWindow::focusTimeline()
{
    auto tl = getCurrentTimeline();
    if (tl) {
        tl->setFocus();
        Q_EMIT tl->controller()->selectionChanged();
    }
}

void MainWindow::slotClipInProjectTree(ObjectId ownerId, bool seekToStart)
{
    QString binId;
    if (ownerId.type != KdenliveObjectType::TimelineClip) {
        int cid = getCurrentTimeline()->controller()->getMainSelectedClip();
        if (cid == -1) {
            return;
        }
        ownerId = ObjectId(KdenliveObjectType::TimelineClip, cid, pCore->currentTimelineId());
        binId = getCurrentTimeline()->controller()->getClipBinId(cid);
    } else {
        binId = pCore->currentDoc()->getTimeline(ownerId.uuid)->getClipBinId(ownerId.itemId);
    }
    // If we have multiple bins, check first if a visible bin contains it
    raiseBin();
    int start = pCore->getItemIn(ownerId);
    int duration = pCore->getItemDuration(ownerId);
    int pos = m_projectMonitor->position();
    int itemPos = pCore->getItemPosition(ownerId);
    bool containsPos = (pos >= itemPos && pos < itemPos + duration);
    double speed = pCore->getClipSpeed(ownerId);
    if (containsPos) {
        pos -= itemPos - start;
    }
    if (!qFuzzyCompare(speed, 1.)) {
        if (speed > 0.) {
            // clip has a speed effect, adjust zone
            start = qRound(start * speed);
            duration = qRound(duration * speed);
            if (containsPos) {
                pos = qRound(pos * speed);
            }
        } else if (speed < 0.) {
            int max = getCurrentTimeline()->controller()->clipMaxDuration(ownerId.itemId);
            if (max > 0) {
                int invertedPos = itemPos + duration - m_projectMonitor->position();
                start = qRound((max - (start + duration)) * -speed);
                duration = qRound(duration * -speed);
                if (containsPos) {
                    pos = start + qRound(invertedPos * -speed);
                }
            }
        }
    }
    QPoint zone(start, start + duration - 1);
    if (!containsPos || seekToStart) {
        pos = start;
    }
    activeBin()->selectClipById(binId, pos, zone, true);
}

void MainWindow::slotSelectClipInTimeline()
{
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
    auto *action = qobject_cast<QAction *>(sender());
    int clipId = action->data().toInt();
    getCurrentTimeline()->controller()->focusItem(clipId);
}

/** Gets called when the window gets hidden */
void MainWindow::hideEvent(QHideEvent * /*event*/)
{
    if (isMinimized() && pCore->monitorManager()) {
        pCore->monitorManager()->pauseActiveMonitor();
    }
}

void MainWindow::slotResizeItemStart()
{
    getCurrentTimeline()->controller()->setInPoint(m_activeTool == ToolType::RippleTool);
}

void MainWindow::slotResizeItemEnd()
{
    getCurrentTimeline()->controller()->setOutPoint(m_activeTool == ToolType::RippleTool);
}

// KoraCut : Q, rogne du bord gauche du clip jusqu'au curseur, sans toucher au reste.
void MainWindow::slotTrimStartToPlayhead()
{
    getCurrentTimeline()->controller()->setInPoint(false);
}

// KoraCut : W, rogne du curseur jusqu'au bord droit du clip, sans toucher au reste.
void MainWindow::slotTrimEndToPlayhead()
{
    getCurrentTimeline()->controller()->setOutPoint(false);
}

// KoraCut : ⇧Q, comme Q mais le reste de la piste vient combler le trou.
void MainWindow::slotRippleTrimStart()
{
    getCurrentTimeline()->controller()->setInPoint(true);
}

// KoraCut : ⇧W, comme W mais le reste de la piste vient combler le trou.
void MainWindow::slotRippleTrimEnd()
{
    getCurrentTimeline()->controller()->setOutPoint(true);
}

void MainWindow::slotSplitAV()
{
    getCurrentTimeline()->controller()->splitAV();
}

void MainWindow::slotSwitchClip()
{
    getCurrentTimeline()->controller()->switchEnableState();
}

void MainWindow::slotSetAudioAlignReference()
{
    getCurrentTimeline()->controller()->setAudioRef();
}

void MainWindow::slotAlignAudio()
{
    getCurrentTimeline()->controller()->alignAudio();
}

void MainWindow::slotSetTimecodeReference()
{
    getCurrentTimeline()->controller()->setTimecodeRef();
}

void MainWindow::slotAlignTimecode()
{
    getCurrentTimeline()->controller()->alignTimecode();
}

void MainWindow::slotUpdateTimelineView(QAction *action)
{
    int viewMode = action->data().toInt();
    KdenliveSettings::setAudiotracksbelow(viewMode);
    getCurrentTimeline()->model()->_resetView();
}

void MainWindow::loadClipActions()
{
    unplugActionList(QStringLiteral("add_effect"));
    plugActionList(QStringLiteral("add_effect"), m_effectsMenu->actions());

    QList<QAction *> clipJobActions = getExtraActions(QStringLiteral("clipjobs"));
    unplugActionList(QStringLiteral("clip_jobs"));
    plugActionList(QStringLiteral("clip_jobs"), clipJobActions);

    QList<QAction *> atcActions = getExtraActions(QStringLiteral("audiotranscoderslist"));
    unplugActionList(QStringLiteral("audio_transcoders_list"));
    plugActionList(QStringLiteral("audio_transcoders_list"), atcActions);

    QList<QAction *> tcActions = getExtraActions(QStringLiteral("transcoderslist"));
    unplugActionList(QStringLiteral("transcoders_list"));
    plugActionList(QStringLiteral("transcoders_list"), tcActions);
}

void MainWindow::loadDockActions()
{
    QList<QAction *> list = kdenliveCategoryMap.value(QStringLiteral("interface"))->actions();
    // Sort actions
    QMap<QString, QAction *> sorted;
    QMap<QString, QAction *> bins;
    QMap<QString, QAction *> scopes;
    QStringList sortedList;
    QStringList binsList;
    QStringList scopesList;
    delete m_binsListMenu;
    delete m_scopesListMenu;
    m_scopesListMenu = new QMenu(i18n("Scopes"));
    QAction *a = m_scopesListMenu->menuAction();
    a->setData(i18n("Scopes"));
    list << a;

    // Group bins
    if (m_binWidgets.size() > 1) {
        m_binsListMenu = new QMenu(i18n("Project Bins"));
        a = m_binsListMenu->menuAction();
        a->setData(m_binsListMenu->title());
        list << a;
    } else {
        m_binsListMenu = nullptr;
    }

    // Group scopes
    QStringList scopesNames = m_scopesManager->getScopesNames();
    // TODO: move audiospectrum's creation to ScopeManager
    scopesNames << QStringLiteral("audiospectrum");

    for (QAction *a : std::as_const(list)) {
        if (a->objectName().startsWith(QStringLiteral("raise_"))) {
            continue;
        }
        const QString actionName = a->data().toString();
        if (m_binsListMenu && actionName.contains(QLatin1String("project_bin"))) {
            bins.insert(actionName, a);
            binsList << actionName;
            continue;
        }
        if (scopesNames.contains(actionName.section(QLatin1Char('#'), 1))) {
            scopes.insert(actionName, a);
            scopesList << actionName;
            continue;
        }
        sorted.insert(actionName, a);
        sortedList << actionName;
    }
    QList<QAction *> orderedList;
    QCollator order;
    std::sort(sortedList.begin(), sortedList.end(), order);
    for (const QString &text : std::as_const(sortedList)) {
        orderedList << sorted.value(text);
    }
    QList<QAction *> orderedBinList;
    binsList.sort(Qt::CaseInsensitive);
    for (const QString &text : std::as_const(binsList)) {
        orderedBinList << bins.value(text);
    }
    if (m_binsListMenu) {
        m_binsListMenu->addActions(orderedBinList);
    }
    QList<QAction *> orderedScopesList;
    scopesList.sort(Qt::CaseInsensitive);
    for (const QString &text : std::as_const(scopesList)) {
        orderedScopesList << scopes.value(text);
    }
    m_scopesListMenu->addActions(orderedScopesList);

    unplugActionList(QStringLiteral("dock_actions"));
    plugActionList(QStringLiteral("dock_actions"), orderedList);
}

void MainWindow::buildDynamicActions()
{
    if (kdenliveCategoryMap.contains(QStringLiteral("clipjobs"))) {
        auto ts = kdenliveCategoryMap.take(QStringLiteral("clipjobs"));
        QList<QAction *> acs = ts->actions();
        qDeleteAll(acs);
        delete ts;
    }
    if (kdenliveCategoryMap.contains(QStringLiteral("transcoderslist"))) {
        auto ts = kdenliveCategoryMap.take(QStringLiteral("transcoderslist"));
        QList<QAction *> acs = ts->actions();
        qDeleteAll(acs);
        delete ts;
    }
    if (kdenliveCategoryMap.contains(QStringLiteral("audiotranscoderslist"))) {
        auto ts = kdenliveCategoryMap.take(QStringLiteral("audiotranscoderslist"));
        QList<QAction *> acs = ts->actions();
        qDeleteAll(acs);
        delete ts;
    }

    auto cjobs = new KActionCategory(i18n("Clip Jobs"), m_extraFactory->actionCollection());
    QAction *action;
    QMap<QString, QString> jobValues = ClipJobManager::getClipJobNames();
    QMapIterator<QString, QString> k(jobValues);
    while (k.hasNext()) {
        k.next();
        action = new QAction(k.value(), m_extraFactory->actionCollection());
        action->setData(k.key());
        if (k.key() == QLatin1String("stabilize;v")) {
            connect(action, &QAction::triggered, this, [this]() { StabilizeTask::start(this); });
        } else if (k.key() == QLatin1String("scenesplit;v")) {
            connect(action, &QAction::triggered, this, [&]() { SceneSplitTask::start(this); });
        } else if (k.key() == QLatin1String("timewarp;av")) {
            connect(action, &QAction::triggered, this, [&]() { SpeedTask::start(this); });
        } else {
            connect(action, &QAction::triggered, this, [&, jobId = k.key().section(QLatin1Char(';'), 0, 0)]() { CustomJobTask::start(this, jobId); });
        }
        cjobs->addAction(action->text(), action);
    }
    action = new QAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Configure Clip Jobs…"), m_extraFactory->actionCollection());
    cjobs->addAction(action->text(), action);
    connect(action, &QAction::triggered, this, [this]() { manageClipJobs(); });
    kdenliveCategoryMap.insert(QStringLiteral("clipjobs"), cjobs);

    // transcoders
    auto ts = new KActionCategory(i18n("Transcoders"), m_extraFactory->actionCollection());
    auto ats = new KActionCategory(i18n("Extract Audio"), m_extraFactory->actionCollection());
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("koracuttranscodingrc"), KConfig::CascadeConfig, QStandardPaths::AppDataLocation);
    KConfigGroup transConfig(config, "Transcoding");
    // read the entries
    QMap<QString, QString> profiles = transConfig.entryMap();
    QMapIterator<QString, QString> i(profiles);
    while (i.hasNext()) {
        i.next();
        QStringList transList;
        transList << i.value().split(QLatin1Char(';'));
        auto *a = new QAction(i.key(), m_extraFactory->actionCollection());
        a->setData(transList);
        if (transList.count() > 1) {
            a->setToolTip(transList.at(1));
        }
        connect(a, &QAction::triggered, [&, a]() {
            QStringList transcodeData = a->data().toStringList();
            std::vector<QString> ids = pCore->activeBin()->selectedClipsIds(true);
            QMap<QString, QVector<int>> clipStreamSelection;
            QString clipId;
            if (transcodeData.count() > 2 && transcodeData.at(2) == QLatin1String("audio")) {
                // Audio extract, check if we have multi stream clips
                QMap<QString, int> clipStreamCount;
                for (const QString &id : ids) {
                    if (id.contains(QLatin1Char('/'))) {
                        clipId = id.section(QLatin1Char('/'), 0, 0);
                    } else {
                        clipId = id;
                    }
                    std::shared_ptr<ProjectClip> clip = pCore->projectItemModel()->getClipByBinID(clipId);
                    if (clip->audioStreamsCount() > 1) {
                        clipStreamSelection.insert(clipId, clip->activeFfmpegStreams());
                    }
                }
            }
            const QString tData = transcodeData.first();
            int clipIn = -1;
            int clipOut = -1;
            for (const QString &id : ids) {
                if (id.contains(QLatin1Char('/'))) {
                    clipId = id.section(QLatin1Char('/'), 0, 0);
                    clipIn = id.section(QLatin1Char('/'), 1, 1).toInt();
                    clipOut = id.section(QLatin1Char('/'), 2, 2).toInt();
                } else {
                    clipId = id;
                    clipIn = -1;
                    clipOut = -1;
                }
                std::shared_ptr<ProjectClip> clip = pCore->projectItemModel()->getClipByBinID(clipId);
                TranscodeSeek::TranscodeInfo info;
                info.url = clip->clipUrl();
                info.type = clip->clipType();
                if (clip->statusReady()) {
                    info.vCodec = clip->videoCodecProperty(QStringLiteral("pix_fmt"));
                }
                info.fps_info = clip->fpsInfo();
                if (clipStreamSelection.contains(clipId)) {
                    // Extract selected audio streams only, create one task per stream
                    QVector<int> selectedStreams = clipStreamSelection.value(clipId);
                    for (auto &ix : selectedStreams) {
                        QString args;
                        QString suffix;
                        if (ix == -1) {
                            // Merge all audio streams
                            args = QStringLiteral("-filter_complex amerge=inputs=%1 ").arg(clip->audioStreamsCount());
                            args.append(QStringLiteral("-ac %1 ").arg(clip->audioChannels()));
                            suffix = i18n("-merged");
                        } else {
                            args = QStringLiteral("-map 0:a:%1 ").arg(ix);
                            suffix = i18n("-stream-%1", ix);
                        }
                        args.append(tData);
                        TranscodeTask::start(ObjectId(KdenliveObjectType::BinClip, clipId.toInt(), QUuid()), suffix, QString(), args, info, clipIn, clipOut,
                                             false, clip.get());
                    }
                } else {
                    TranscodeTask::start(ObjectId(KdenliveObjectType::BinClip, clipId.toInt(), QUuid()), QString(), QString(), tData, info, clipIn, clipOut,
                                         false, clip.get());
                }
            }
        });
        if (transList.count() > 2 && transList.at(2) == QLatin1String("audio")) {
            // This is an audio transcoding action
            ats->addAction(i.key(), a);
        } else {
            ts->addAction(i.key(), a);
        }
    }
    kdenliveCategoryMap.insert(QStringLiteral("transcoderslist"), ts);
    kdenliveCategoryMap.insert(QStringLiteral("audiotranscoderslist"), ats);
    updateDockMenu();
}

void MainWindow::updateDockMenu()
{
    // Populate View menu with show / hide actions for dock widgets
    KActionCategory *guiActions = nullptr;
    QList<QAction *> existing;
    if (kdenliveCategoryMap.contains(QStringLiteral("interface"))) {
        guiActions = kdenliveCategoryMap.value(QStringLiteral("interface"));
        existing = guiActions->actions();
    } else {
        guiActions = new KActionCategory(i18n("Interface"), actionCollection());
    }

    QList<KDDockWidgets::QtWidgets::DockWidget *> docks = findChildren<KDDockWidgets::QtWidgets::DockWidget *>();
    for (auto dock : std::as_const(docks)) {
        QAction *dockInfo = dock->toggleAction();
        if (!dockInfo || existing.contains(dockInfo)) {
            continue;
        }
        guiActions->addAction(dock->objectName(), dockInfo);
    }
    kdenliveCategoryMap.insert(QStringLiteral("interface"), guiActions);
    loadDockActions();
}

QList<QAction *> MainWindow::getExtraActions(const QString &name)
{
    if (!kdenliveCategoryMap.contains(name)) {
        return QList<QAction *>();
    }
    return kdenliveCategoryMap.value(name)->actions();
}

void MainWindow::slotTranscode(const QStringList &urls)
{
    Q_ASSERT(!urls.isEmpty());
    QString params;
    QString desc;
    ClipTranscode *d = new ClipTranscode(urls, params, QStringList(), desc, pCore->activeBin()->getCurrentFolder());
    connect(d, &ClipTranscode::addClip, this, &MainWindow::slotAddProjectClip);
    d->show();
}

void MainWindow::slotFriendlyTranscode(const QString &binId, bool checkProfile)
{
    QString params;
    QString desc;
    std::shared_ptr<ProjectClip> clip = pCore->projectItemModel()->getClipByBinID(binId);
    if (clip == nullptr) {
        qDebug() << "// NO CLIP FOUND FOR BIN ID: " << binId;
        return;
    }
    QStringList urls = {clip->url()};
    // Prepare clip properties
    QMap<QString, QString> sourceProps;
    sourceProps.insert(QStringLiteral("resource"), clip->url());
    sourceProps.insert(QStringLiteral("kdenlive:originalurl"), clip->url());
    sourceProps.insert(QStringLiteral("kdenlive:clipname"), clip->clipName());
    sourceProps.insert(QStringLiteral("kdenlive:proxy"), clip->getProducerProperty(QStringLiteral("kdenlive:proxy")));
    sourceProps.insert(QStringLiteral("_fullreload"), QStringLiteral("1"));
    ClipTranscode *d = new ClipTranscode(urls, params, QStringList(), desc, pCore->activeBin()->getCurrentFolder());
    connect(d, &ClipTranscode::addClip, [&, binId, sourceProps](const QUrl &url, const QString & /*folderInfo*/) {
        QMap<QString, QString> newProps;
        newProps.insert(QStringLiteral("resource"), url.toLocalFile());
        newProps.insert(QStringLiteral("kdenlive:originalurl"), url.toLocalFile());
        newProps.insert(QStringLiteral("kdenlive:clipname"), url.fileName());
        newProps.insert(QStringLiteral("kdenlive:proxy"), QStringLiteral("-"));
        newProps.insert(QStringLiteral("_fullreload"), QStringLiteral("1"));
        QMetaObject::invokeMethod(pCore->activeBin(), "slotEditClipCommand", Qt::QueuedConnection, Q_ARG(QString, binId), Q_ARG(stringMap, sourceProps),
                                  Q_ARG(stringMap, newProps));
    });
    d->exec();
    if (checkProfile) {
        pCore->bin()->slotCheckProfile(binId);
    }
}

void MainWindow::slotTranscodeClip()
{
    const QString dialogFilter = FileFilter::Builder().defaultCategories().toQFilter();
    QString clipFolder = KRecentDirs::dir(QStringLiteral(":KdenliveClipFolder"));
    QStringList urls = QFileDialog::getOpenFileNames(this, i18nc("@title:window", "Files to Transcode"), clipFolder, dialogFilter);
    if (urls.isEmpty()) {
        return;
    }
    slotTranscode(urls);
}

void MainWindow::slotSetDocumentRenderProfile(const QMap<QString, QString> &props)
{
    KdenliveDoc *project = pCore->currentDoc();
    bool modified = false;
    QMapIterator<QString, QString> i(props);
    while (i.hasNext()) {
        i.next();
        if (project->getDocumentProperty(i.key()) == i.value()) {
            continue;
        }
        project->setDocumentProperty(i.key(), i.value());
        modified = true;
    }
    if (modified) {
        project->setModified();
    }
}

void MainWindow::slotUpdateTimecodeFormat(int ix)
{
    KdenliveSettings::setFrametimecode(ix == 1);
    Q_EMIT pCore->updateProjectTimecode();
    m_clipMonitor->updateTimecodeFormat();
    m_projectMonitor->updateTimecodeFormat();
    Q_EMIT getCurrentTimeline()->controller()->frameFormatChanged();
    m_timeFormatButton->setFont(KoraCutFonts::numbers());
}

void MainWindow::applyToolMessageStyling()
{
    if (!m_trimLabel) {
        return;
    }

    KColorScheme scheme(QApplication::palette().currentColorGroup());

    switch (m_currentEditMode) {
    case TimelineMode::InsertEdit:
        // Use a red color from the palette for insert mode
        m_trimLabel->setStyleSheet(QStringLiteral("QLabel { padding-left: 2; padding-right: 2; background-color :%1; }")
                                       .arg(scheme.foreground(KColorScheme::NegativeText).color().name()));
        break;
    case TimelineMode::OverwriteEdit:
        // Use a green color from the palette for overwrite mode
        m_trimLabel->setStyleSheet(QStringLiteral("QLabel { padding-left: 2; padding-right: 2; background-color :%1; }")
                                       .arg(scheme.foreground(KColorScheme::PositiveText).color().name()));
        break;
    default:
        // Use normal window background color for normal edit mode
        m_trimLabel->setStyleSheet(
            QStringLiteral("QLabel { padding-left: 2; padding-right: 2; background-color :%1; }").arg(QApplication::palette().window().color().name()));
        break;
    }
}

void MainWindow::applyZoomLevelButtonStyling()
{
    if (!m_statusZoomLevelButton) {
        return;
    }

    m_statusZoomLevelButton->setStyleSheet(
        QStringLiteral("QPushButton { padding: 2px; background-color: rgba(255, 0, 0, 0.25); border: none; border-radius: 4px; } "
                       "QPushButton:hover { border: 1px solid palette(highlight); } "
                       "QPushButton:disabled { color: palette(text); background-color: transparent; }"));
}

void MainWindow::slotRemoveFocus()
{
    getCurrentTimeline()->setFocus();
}

void MainWindow::slotShutdown()
{
    pCore->currentDoc()->setModified(false);
    // Call shutdown
#ifndef NODBUS
    QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface();
    // org.kde.Shutdown is DBus activatable, so we can't query for it running
    if (qgetenv("XDG_CURRENT_DESKTOP") == QLatin1String("KDE")) {
        QDBusInterface kdeShutdown(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"), QStringLiteral("org.kde.LogoutPrompt"));
        kdeShutdown.call(QStringLiteral("promptShutDown"));
    } else if ((interface != nullptr) && interface->isServiceRegistered(QStringLiteral("org.gnome.SessionManager"))) {
        QDBusInterface smserver(QStringLiteral("org.gnome.SessionManager"), QStringLiteral("/org/gnome/SessionManager"),
                                QStringLiteral("org.gnome.SessionManager"));
        smserver.call(QStringLiteral("Shutdown"));
    }
#endif
}

void MainWindow::slotSwitchMonitors()
{
    pCore->monitorManager()->slotSwitchMonitors(!m_clipMonitor->isActive());
    if (m_projectMonitor->isActive()) {
        focusTimeline();
    } else {
        Bin *bin = activeBin();
        if (bin) {
            bin->focusBinView();
        }
    }
}

void MainWindow::slotFocusTimecode()
{
    if (m_clipMonitor->isActive()) {
        m_clipMonitor->focusTimecode();
    } else if (m_projectMonitor) {
        m_projectMonitor->focusTimecode();
    }
}

void MainWindow::slotSwitchMonitorOverlay(QAction *action)
{
    if (pCore->monitorManager()->isActive(Kdenlive::ClipMonitor)) {
        m_clipMonitor->switchMonitorInfo(action->data().toInt());
    } else {
        m_projectMonitor->switchMonitorInfo(action->data().toInt());
    }
}

void MainWindow::slotSwitchDropFrames(bool drop)
{
    KdenliveSettings::setMonitor_dropframes(drop);
    m_clipMonitor->restart();
    m_projectMonitor->restart();
}

void MainWindow::slotInsertZoneToTree()
{
    if (!m_clipMonitor->isActive() || m_clipMonitor->currentController() == nullptr) {
        return;
    }
    QPoint info = m_clipMonitor->getZoneInfo();
    QString id;
    // clip monitor counts the frame after the out point as the zone out, so we
    // need to subtract 1 to get the actual last frame
    pCore->projectItemModel()->requestAddBinSubClip(id, info.x(), info.y() - 1, {}, m_clipMonitor->activeClipId());
}

void MainWindow::slotUpdateProxySettings()
{
    KdenliveDoc *project = pCore->currentDoc();
    if (m_renderWidget) {
        m_renderWidget->updateProxyConfig(project->useProxy());
    }
    for (auto &b : m_binWidgets) {
        b->refreshProxySettings();
    }
}

void MainWindow::slotArchiveProject()
{
    KdenliveDoc *doc = pCore->currentDoc();
    pCore->projectManager()->prepareSave();
    QString sceneData = pCore->projectManager()->projectSceneList(doc->url().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toLocalFile()).first;
    if (sceneData.isEmpty()) {
        KMessageBox::error(this, i18n("Project file could not be saved for archiving."));
        return;
    }
    QStringList compositionLumas = doc->extractCompositionLumas();
    QStringList externalEffectFiles = doc->extractExternalEffectFiles();

    QPointer<ArchiveWidget> d(new ArchiveWidget(doc->url().fileName(), sceneData, compositionLumas, externalEffectFiles, this));
    if (d->exec() != 0) {
        m_messageLabel->setMessage(i18n("Archiving project"), OperationCompletedMessage);
    }
}

void MainWindow::slotDownloadResources()
{
    m_onlineResourcesDock->open();
    m_onlineResourcesDock->setAsCurrentTab();
}

void MainWindow::slotProcessImportKeyframes(GraphicsRectItem type, const QString &tag, const QString &keyframes)
{
    Q_UNUSED(keyframes)
    Q_UNUSED(tag)
    if (type == AVWidget) {
        // This data should be sent to the effect stack
        // TODO REFAC reimplement
        // m_effectStack->setKeyframes(tag, data);
    } else if (type == TransitionWidget) {
        // This data should be sent to the transition stack
        // TODO REFAC reimplement
        // m_effectStack->transitionConfig()->setKeyframes(tag, data);
    } else {
        // Error
    }
}

void MainWindow::slotAlignPlayheadToMousePos()
{
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
    getCurrentTimeline()->controller()->seekToMouse();
}

void MainWindow::triggerKey(QKeyEvent *ev)
{
    // Hack: The QQuickWindow that displays fullscreen monitor does not integrate with QActions.
    // So on keypress events we parse keys and check for shortcuts in all existing actions
    QKeySequence seq;
    // Remove the Num modifier or some shortcuts like "*" will not work
    auto mods = ev->modifiers() & ~Qt::KeypadModifier;
    // Some shortcuts are translated, for example Shift+5 is equal to '%' in a swiss keyboard
    // So we need to remove the Shift Modifier to match. Logic copied from KKeySequenceRecorder
    if (KdenliveKeySequence::isShiftAsModifierAllowed(ev->key())) {
        seq = QKeySequence(static_cast<int>(mods) + ev->key());
    } else {
        seq = QKeySequence(static_cast<int>(mods & ~Qt::ShiftModifier) + ev->key());
    }
    QList<KActionCollection *> collections = KActionCollection::allCollections();
    for (int i = 0; i < collections.count(); ++i) {
        KActionCollection *coll = collections.at(i);
        for (QAction *tempAction : coll->actions()) {
            if (tempAction->shortcuts().contains(seq)) {
                // Trigger action
                tempAction->trigger();
                ev->accept();
                return;
            }
        }
    }
    QWidget::keyPressEvent(ev);
}

KDDockWidgets::QtWidgets::DockWidget *MainWindow::addDock(const QString &title, const QString &objectName, QWidget *widget, KDDockWidgets::Location area,
                                                          KDDockWidgets::QtWidgets::DockWidget *otherDockWidget, const QSize preferredSize)
{
    auto dock = new KDDockWidgets::QtWidgets::DockWidget(objectName);
    qDebug() << "Building dock: " << objectName;
    Q_ASSERT(widget != nullptr);
    widget->setProperty("_breeze_force_frame", false);
    dock->setTitle(title);
    dock->setWidget(widget);
    dock->setObjectName(objectName);
    dock->toggleAction()->setData(QStringLiteral("%1#%2").arg(title, objectName));
    // KoraCut, 23 septembre 2026 : un panneau en onglet impose sa largeur minimale à tout son
    // groupe. « Propriétés » (367 px) et « Voix » (545 px) empêchaient les médias et le moniteur
    // de tenir dans une rangée sur un portable. KDDockWidgets prend minimumWidth() quand il est
    // posé : on borne ces deux-là, leur contenu défile ou se rogne quand le groupe est plus étroit.
    if (objectName == QLatin1String("clip_properties") || objectName == QLatin1String("textedit")) {
        dock->setMinimumWidth(240);
    }
    if (area == KDDockWidgets::Location_None) {
        // Add widget as tab
        qDebug() << "Creating dock: " << objectName;
        Q_ASSERT(otherDockWidget != nullptr);
        otherDockWidget->addDockWidgetAsTab(dock, preferredSize);
    } else {
        mainDockWindow->addDockWidget(dock, area, otherDockWidget, preferredSize);
    }
    KActionCategory *guiActions = nullptr;
    if (kdenliveCategoryMap.contains(QStringLiteral("interface"))) {
        guiActions = kdenliveCategoryMap.take(QStringLiteral("interface"));
    } else {
        guiActions = new KActionCategory(i18n("Interface"), actionCollection());
    }
    auto dockAction = dock->toggleAction();
    connect(dockAction, &QAction::triggered, this, [dock](bool dockVisible) {
        if (!dockVisible && !KdenliveSettings::showtitlebars()) {
            // Hack: when titlebar is hidden and a standalone widget is hidden through its
            // menu action, empty space is not automatically reused. So we hack around by
            // showing widget again, then hiding
            dock->open();
            dock->close();
        }
    });
    guiActions->addAction(objectName, dockAction);
    const QString actionText = KLocalizedString::removeAcceleratorMarker(dockAction->text());
    QAction *action = new QAction(i18n("Raise %1", actionText), this);
    action->setData(QStringLiteral("_raise"));
    connect(action, &QAction::triggered, this, [dock]() {
        dock->open();
        dock->setAsCurrentTab();
        dock->setFocus(Qt::OtherFocusReason);
    });
    guiActions->addAction("raise_" + dock->objectName(), action);
    kdenliveCategoryMap.insert(QStringLiteral("interface"), guiActions);
    return dock;
}

bool MainWindow::isMixedTabbed() const
{
    return m_mixerDock->asDockWidgetController()->isTabbed();
    return false;
}

void MainWindow::slotUpdateMonitorOverlays(int id, int code)
{
    QMenu *monitorOverlay = static_cast<QMenu *>(factory()->container(QStringLiteral("monitor_config_overlay"), this));
    if (!monitorOverlay) {
        return;
    }
    QList<QAction *> actions = monitorOverlay->actions();
    for (QAction *ac : std::as_const(actions)) {
        int mid = ac->data().toInt();
        if (mid == Monitor::InfoOverlay || mid == Monitor::ClipJobsOverlay) {
            ac->setVisible(id == Kdenlive::ClipMonitor);
        }
        ac->setChecked(code & mid);
    }
}

void MainWindow::raiseMonitor(bool clipMonitor, bool raise)
{
    if (clipMonitor) {
        if (raise) {
            m_clipMonitorDock->open();
        }
        m_clipMonitorDock->setAsCurrentTab();
    } else {
        if (raise) {
            m_projectMonitorDock->open();
        }
        m_projectMonitorDock->setAsCurrentTab();
    }
}

void MainWindow::slotToggleAutoPreview(bool enable)
{
    KdenliveSettings::setAutopreview(enable);
    if (QAction *autoAction = actionCollection()->action(QStringLiteral("koracut_render_auto"))) {
        autoAction->setChecked(enable);
    }
    if (getCurrentTimeline() && getCurrentTimeline()->controller()) {
        // KoraCut : actif, on relit les zones lourdes ; coupé, le rendu en cours s'arrête.
        if (enable) {
            getCurrentTimeline()->controller()->scheduleAutoRender();
        } else {
            getCurrentTimeline()->controller()->stopPreviewRender();
        }
    }
}

void MainWindow::showTimelineToolbarMenu(const QPoint &pos)
{
    QMenu menu;
    menu.addAction(actionCollection()->action(KStandardAction::name(KStandardAction::ConfigureToolbars)));
    QMenu *contextSize = new QMenu(i18n("Icon Size"));
    menu.addMenu(contextSize);
    auto *sizeGroup = new QActionGroup(contextSize);
    int currentSize = m_timelineToolBar->iconSize().width();
    QAction *a = new QAction(i18nc("@item:inmenu Icon size", "Default"), contextSize);
    a->setData(m_timelineToolBar->iconSizeDefault());
    a->setCheckable(true);
    if (m_timelineToolBar->iconSizeDefault() == currentSize) {
        a->setChecked(true);
    }
    a->setActionGroup(sizeGroup);
    contextSize->addAction(a);
    KIconTheme *theme = KIconLoader::global()->theme();
    QList<int> avSizes;
    if (theme) {
        avSizes = theme->querySizes(KIconLoader::Toolbar);
    }

    std::sort(avSizes.begin(), avSizes.end());

    if (avSizes.count() < 10) {
        // Fixed or threshold type icons
        for (int it : avSizes) {
            QString text;
            if (it < 19) {
                text = i18n("Small (%1x%2)", it, it);
            } else if (it < 25) {
                text = i18n("Medium (%1x%2)", it, it);
            } else if (it < 35) {
                text = i18n("Large (%1x%2)", it, it);
            } else {
                text = i18n("Huge (%1x%2)", it, it);
            }

            // save the size in the contextIconSizes map
            auto *sizeAction = new QAction(text, contextSize);
            sizeAction->setData(it);
            sizeAction->setCheckable(true);
            sizeAction->setActionGroup(sizeGroup);
            if (it == currentSize) {
                sizeAction->setChecked(true);
            }
            contextSize->addAction(sizeAction);
        }
    } else {
        // Scalable icons.
        const int progression[] = {16, 22, 32, 48, 64, 96, 128, 192, 256};

        for (int i : progression) {
            for (int it : avSizes) {
                if (it >= i) {
                    QString text;
                    if (it < 19) {
                        text = i18n("Small (%1x%2)", it, it);
                    } else if (it < 25) {
                        text = i18n("Medium (%1x%2)", it, it);
                    } else if (it < 35) {
                        text = i18n("Large (%1x%2)", it, it);
                    } else {
                        text = i18n("Huge (%1x%2)", it, it);
                    }

                    // save the size in the contextIconSizes map
                    auto *sizeAction = new QAction(text, contextSize);
                    sizeAction->setData(it);
                    sizeAction->setCheckable(true);
                    sizeAction->setActionGroup(sizeGroup);
                    if (it == currentSize) {
                        sizeAction->setChecked(true);
                    }
                    contextSize->addAction(sizeAction);
                    break;
                }
            }
        }
    }
    connect(contextSize, &QMenu::triggered, this, &MainWindow::setTimelineToolbarIconSize);
    menu.exec(m_timelineToolBar->mapToGlobal(pos));
    contextSize->deleteLater();
}

void MainWindow::setTimelineToolbarIconSize(QAction *a)
{
    if (!a) {
        return;
    }
    int size = a->data().toInt();
    m_timelineToolBar->setIconDimensions(size);
    KSharedConfigPtr config = KSharedConfig::openConfig();
    KConfigGroup mainConfig(config, QStringLiteral("MainWindow"));
    KConfigGroup tbGroup(&mainConfig, QStringLiteral("Toolbar timelineToolBar"));
    m_timelineToolBar->saveSettings(tbGroup);
}

void MainWindow::slotManageCache()
{
    if (pCore->currentDoc() == nullptr) {
        return;
    }
    QPointer<TemporaryData> d(new TemporaryData(pCore->currentDoc(), false, this));
    connect(d, &TemporaryData::disableProxies, this, &MainWindow::slotDisableProxies);
    d->exec();
}

void MainWindow::slotUpdateCompositing(bool checked)
{
    getCurrentTimeline()->controller()->switchCompositing(checked);
    pCore->currentDoc()->setModified();
}

void MainWindow::slotUpdateCompositeAction(bool enable)
{
    m_compositeAction->setChecked(enable);
}

void MainWindow::showMenuBar(bool show)
{
    if (!show && toolBar()->isHidden()) {
        KMessageBox::information(this, i18n("This will hide the menu bar completely. You can show it again by typing Ctrl+M."), i18n("Hide menu bar"),
                                 QStringLiteral("show-menubar-warning"));
    }
    menuBar()->setVisible(show);
}

TimelineWidget *MainWindow::getCurrentTimeline() const
{
    // KoraCut, 23 septembre 2026 : les moniteurs sont construits avant les onglets de timeline. Un
    // moniteur resté en plein écran à la fermeture repasse en plein écran dès sa construction, et
    // son rappel « fenêtre recréée » demandait la timeline courante : plantage au lancement, à
    // chaque lancement, tant que le réglage restait (la version d'Amadou Ba, le matin du 23).
    return m_timelineTabs ? m_timelineTabs->getCurrentTimeline() : nullptr;
}

TimelineWidget *MainWindow::getTimeline(const QUuid uuid) const
{
    return m_timelineTabs->getTimeline(uuid);
}

void MainWindow::getSequenceProperties(const QUuid &uuid, QMap<QString, QString> &props)
{
    TimelineWidget *w = getTimeline(uuid);
    if (w) {
        w->controller()->getSequenceProperties(props);
    }
}

bool MainWindow::hasTimeline() const
{
    return m_timelineTabs != nullptr;
}

void MainWindow::closeTimelineTab(const QUuid uuid, bool onDeletion, bool checkActiveClosed)
{
    m_timelineTabs->closeTimelineTab(uuid, checkActiveClosed);
    if (onDeletion) {
        resetSubtitles(uuid);
    }
}

const QStringList MainWindow::openedSequences() const
{
    if (m_timelineTabs) {
        return m_timelineTabs->openedSequences();
    }
    return QStringList();
}

void MainWindow::resetTimelineTracks()
{
    TimelineWidget *current = getCurrentTimeline();
    if (current) {
        current->controller()->resetTrackHeight();
    }
}

void MainWindow::slotRemapItemTime()
{
    TimelineWidget *current = getCurrentTimeline();
    if (current) {
        current->controller()->remapItemTime(-1);
    }
}

void MainWindow::slotEditItemSpeed()
{
    TimelineWidget *current = getCurrentTimeline();
    if (current) {
        current->controller()->changeItemSpeed(-1, -1);
    }
}

void MainWindow::slotSwitchTimelineZone(bool active)
{
    pCore->currentDoc()->setDocumentProperty(QStringLiteral("enableTimelineZone"), active ? QStringLiteral("1") : QStringLiteral("0"));
    Q_EMIT getCurrentTimeline()->controller()->useRulerChanged();
    QSignalBlocker blocker(m_useTimelineZone);
    m_useTimelineZone->setActive(active);
}

void MainWindow::slotGrabItem()
{
    getCurrentTimeline()->controller()->grabCurrent();
}

void MainWindow::slotAudioZoomIn()
{
    if (KdenliveSettings::normalizechannels()) {
        KdenliveSettings::setNormalizechannels(false);
    }
    if (KdenliveSettings::waveformScaler() < 5) {
        KdenliveSettings::setWaveformScaler(KdenliveSettings::waveformScaler() * 2);
    }
    slotNormalizeAudioChannel(true);
}

void MainWindow::slotAudioZoomOut()
{
    if (KdenliveSettings::normalizechannels()) {
        KdenliveSettings::setNormalizechannels(false);
    }
    if (KdenliveSettings::waveformScaler() > 1) {
        KdenliveSettings::setWaveformScaler(KdenliveSettings::waveformScaler() / 2);
    }
    slotNormalizeAudioChannel(true);
}

void MainWindow::slotAudioZoomReset()
{
    if (KdenliveSettings::normalizechannels()) {
        KdenliveSettings::setNormalizechannels(false);
    }
    KdenliveSettings::setWaveformScaler(1);
    slotNormalizeAudioChannel(true);

    // Close the audio thumbs menu when reset is clicked
    if (m_audioThumbsMenu && m_audioThumbsMenu->isVisible()) {
        m_audioThumbsMenu->close();
    }
}

void MainWindow::slotAudioZoomCycle()
{
    if (KdenliveSettings::normalizechannels()) {
        KdenliveSettings::setNormalizechannels(false);
    }
    if (KdenliveSettings::waveformScaler() < 5) {
        KdenliveSettings::setWaveformScaler(KdenliveSettings::waveformScaler() * 2);
    } else {
        KdenliveSettings::setWaveformScaler(1);
    }
    slotNormalizeAudioChannel(true);
}

void MainWindow::updateAudioWaveformActionIcon()
{
    // TODO: remove icon check once we require KF > 6.1
    QString waveformIconName = QIcon::hasThemeIcon(QStringLiteral("waveform")) ? QStringLiteral("waveform") : QStringLiteral("kdenlive-show-audiothumb");
    if (KdenliveSettings::waveformScaler() > 1 && KdenliveSettings::audiothumbnails()) {
        int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize);
        QImage img = QIcon::fromTheme(waveformIconName).pixmap(iconSize, iconSize).toImage();
        KIconEffect::toMonochrome(img, Qt::red, Qt::red, 1.0);
        m_buttonAudioThumbs->setIcon(QIcon(QPixmap::fromImage(img)));
    } else {
        m_buttonAudioThumbs->setIcon(QIcon::fromTheme(waveformIconName));
    }
}

void MainWindow::slotCollapse()
{
    if ((QApplication::focusWidget() != nullptr) && (QApplication::focusWidget()->parentWidget() != nullptr) &&
        QApplication::focusWidget()->parentWidget() == pCore->bin()) {
        // Bin expand/collapse
        pCore->bin()->expandCurrent();

    } else {
        QWidget *widget = QApplication::focusWidget();
        while ((widget != nullptr) && widget != this) {
            if (widget == m_effectStackDock) {
                m_assetPanel->collapseCurrentEffect();
                return;
            }
            widget = widget->parentWidget();
        }
        // Collapse / expand track
        getCurrentTimeline()->controller()->collapseActiveTrack();
    }
}

void MainWindow::slotCollapseAll()
{
    if ((QApplication::focusWidget() != nullptr) && (QApplication::focusWidget()->parentWidget() != nullptr) &&
        QApplication::focusWidget()->parentWidget() == pCore->bin()) {
        // Bin expand/collapse
        pCore->bin()->expandAll();

    } else {
        QWidget *widget = QApplication::focusWidget();
        while ((widget != nullptr) && widget != this) {
            if (widget == m_effectStackDock) {
                Q_EMIT m_assetPanel->slotSwitchCollapseAll();
                return;
            }
            widget = widget->parentWidget();
        }
        // Collapse / expand track
        getCurrentTimeline()->controller()->collapseAllTracks();
    }
}

bool MainWindow::timelineIsDetached() const
{
    return m_timelineDock && m_timelineDock->isFloating();
}

void MainWindow::slotDetachTimeline()
{
    // KoraCut, 22 septembre 2026 : premier temps de « plusieurs timelines à la fois ». Le
    // panneau de timeline sort dans sa propre fenêtre, qu'on peut poser à côté de la fenêtre
    // principale, et y revient de la même façon. C'est ce que KDDockWidgets sait déjà faire.
    // Ce qui n'est PAS livré : deux timelines vivantes côte à côte dans la même fenêtre, avec
    // deux moteurs et deux têtes de lecture. Voir « 13 - Deuxième passe montage », § 5.
    if (!m_timelineDock) {
        return;
    }
    m_timelineDock->setFloating(!m_timelineDock->isFloating());
}

void MainWindow::slotExpandClip()
{
    getCurrentTimeline()->controller()->expandActiveClip();
}

bool MainWindow::timelineVisible() const
{
    return !centralWidget()->isHidden();
}

void MainWindow::slotActivateAudioTrackSequence()
{
    auto *action = qobject_cast<QAction *>(sender());
    const QList<int> trackIds = getCurrentTimeline()->model()->getTracksIds(true);
    int trackPos = qBound(0, action->data().toInt(), trackIds.count() - 1);
    int tid = trackIds.at(trackPos);
    getCurrentTimeline()->controller()->setActiveTrack(tid);
}

void MainWindow::slotActivateVideoTrackSequence()
{
    auto *action = qobject_cast<QAction *>(sender());
    const QList<int> trackIds = getCurrentTimeline()->model()->getTracksIds(false);
    int trackPos = qBound(0, action->data().toInt(), trackIds.count() - 1);
    int tid = trackIds.at(trackIds.count() - 1 - trackPos);
    getCurrentTimeline()->controller()->setActiveTrack(tid);
    if (m_activeTool == ToolType::MulticamTool) {
        pCore->monitorManager()->slotPerformMultiTrackMode();
    }
}

void MainWindow::slotActivateTarget()
{
    auto *action = qobject_cast<QAction *>(sender());
    if (action) {
        int ix = action->data().toInt();
        getCurrentTimeline()->controller()->assignCurrentTarget(ix);
    }
}

void MainWindow::resetSubtitles(const QUuid &uuid)
{
    // Hide subtitle track
    m_buttonSubtitleEditTool->setChecked(false);
    KdenliveSettings::setShowSubtitles(false);
    pCore->subtitleWidget()->setModel(nullptr);
    if (pCore->currentDoc()) {
        std::shared_ptr<TimelineItemModel> timeline = pCore->currentDoc()->getTimeline(uuid);
        if (timeline && timeline->hasSubtitleModel()) {
            auto subModel = timeline->getSubtitleModel();
            QMap<std::pair<int, QString>, QString> currentSubs = subModel->getSubtitlesList();
            QMapIterator<std::pair<int, QString>, QString> i(currentSubs);
            while (i.hasNext()) {
                i.next();
                const QString workPath = pCore->currentDoc()->subTitlePath(uuid, i.key().first, false);
                QFile workFile(workPath);
                if (workFile.exists()) {
                    workFile.remove();
                }
            }
        }
    }
}

void MainWindow::slotShowSubtitles(bool show)
{
    const QUuid uuid = getCurrentTimeline()->model()->uuid();
    KdenliveSettings::setShowSubtitles(show);
    if (getCurrentTimeline()->model()->hasSubtitleModel()) {
        getCurrentTimeline()->connectSubtitleModel(false);
    } else {
        QMap<QString, QString> props = QMap<QString, QString>();
        slotEditSubtitle(props);
    }
    pCore->currentDoc()->setSequenceProperty(uuid, QStringLiteral("hidesubtitle"), show ? 0 : 1);
}

void MainWindow::slotInitSubtitle(const QMap<QString, QString> &subProperties, const QUuid &uuid)
{
    std::shared_ptr<TimelineItemModel> timeline = pCore->currentDoc()->getTimeline(uuid);
    Q_ASSERT(!timeline->hasSubtitleModel());
    std::shared_ptr<SubtitleModel> subtitleModel = timeline->createSubtitleModel();
    // Starting a new subtitle for this project
    pCore->subtitleWidget()->setModel(subtitleModel);
    subtitleModel->loadProperties(subProperties);
    if (uuid == pCore->currentTimelineId() && pCore->currentDoc()->getSequenceProperty(uuid, QStringLiteral("hidesubtitle")).toInt() == 0) {
        KdenliveSettings::setShowSubtitles(true);
        m_buttonSubtitleEditTool->setChecked(true);
        getCurrentTimeline()->connectSubtitleModel(true);
    }
}

void MainWindow::slotEditSubtitle(const QMap<QString, QString> &subProperties)
{
    bool hasSubtitleModel = getCurrentTimeline()->hasSubtitles();
    if (!hasSubtitleModel) {
        std::shared_ptr<SubtitleModel> subtitleModel = getCurrentTimeline()->model()->createSubtitleModel();
        // Starting a new subtitle for this project
        pCore->subtitleWidget()->setModel(subtitleModel);
        m_buttonSubtitleEditTool->setChecked(true);
        KdenliveSettings::setShowSubtitles(true);
        if (!subProperties.isEmpty()) {
            subtitleModel->loadProperties(subProperties);
            // Load the disabled / locked state of the subtitle
            Q_EMIT getCurrentTimeline()->controller()->subtitlesLockedChanged();
            Q_EMIT getCurrentTimeline()->controller()->subtitlesDisabledChanged();
        }
        getCurrentTimeline()->connectSubtitleModel(true);
        // Update subtitle track combo list
        Q_EMIT getCurrentTimeline()->controller()->subtitlesListChanged();
    } else {
        KdenliveSettings::setShowSubtitles(m_buttonSubtitleEditTool->isChecked());
        getCurrentTimeline()->connectSubtitleModel(false);
    }
}

void MainWindow::slotAddSubtitle(const QString &text)
{
    showSubtitleTrack();
    getCurrentTimeline()->model()->getSubtitleModel()->addSubtitle(-1, 0, text);
}

void MainWindow::slotDisableSubtitle()
{
    getCurrentTimeline()->controller()->switchSubtitleDisable();
}

void MainWindow::slotLockSubtitle()
{
    getCurrentTimeline()->controller()->switchSubtitleLock();
}

void MainWindow::showSubtitleTrack()
{
    if (!getCurrentTimeline()->hasSubtitles() || !m_buttonSubtitleEditTool->isChecked()) {
        m_buttonSubtitleEditTool->setChecked(true);
        slotEditSubtitle();
    }
}

void MainWindow::slotImportSubtitle()
{
    showSubtitleTrack();
    getCurrentTimeline()->controller()->importSubtitle();
}

void MainWindow::slotManageSubtitle()
{
    showSubtitleTrack();
    getCurrentTimeline()->controller()->subtitlesMenuActivated(-1);
}

void MainWindow::slotExportSubtitle()
{
    if (!getCurrentTimeline()->hasSubtitles()) {
        pCore->displayMessage(i18n("No subtitles in current project"), ErrorMessage);
        return;
    }
    getCurrentTimeline()->controller()->exportSubtitle();
}

void MainWindow::slotSpeechRecognition()
{
    if (!getCurrentTimeline()->hasSubtitles()) {
        slotEditSubtitle();
    }
    getCurrentTimeline()->controller()->subtitleSpeechRecognition();
}

void MainWindow::slotCopyDebugInfo()
{
    // General note for this function: since the information targets developers, we don't want it to be translated

    QString debuginfo;
    QString packageType;
    switch (pCore->packageType()) {
    case LinuxPackageType::AppImage:
        packageType = QStringLiteral("AppImage");
        break;
    case LinuxPackageType::Flatpak:
        packageType = QStringLiteral("Flatpak");
        break;
    case LinuxPackageType::Snap:
        packageType = QStringLiteral("Snap");
        break;
    default:
        packageType = QStringLiteral("Unknown/Default");
        break;
    }
    QList<KAboutComponent> components = KAboutData::applicationData().components();
    for (auto &c : components) {
        debuginfo.append(QStringLiteral("%1: %2\n").arg(c.name(), c.version()));
    }
    debuginfo.append(QStringLiteral("Package Type: %1\n").arg(packageType));
    debuginfo.append(QStringLiteral("Qt: %1 (built against %2 %3)\n").arg(QString::fromLocal8Bit(qVersion()), QT_VERSION_STR, QSysInfo::buildAbi()));
    debuginfo.append(QStringLiteral("Frameworks: %2\n").arg(KCoreAddons::versionString()));
    debuginfo.append(QStringLiteral("System: %1\n").arg(QSysInfo::prettyProductName()));
    debuginfo.append(QStringLiteral("Kernel: %1 %2\n").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion()));
    debuginfo.append(QStringLiteral("CPU: %1\n").arg(QSysInfo::currentCpuArchitecture()));
    debuginfo.append(QStringLiteral("Windowing System: %1\n").arg(QGuiApplication::platformName()));
    if (m_clipMonitor) {
        debuginfo.append(QStringLiteral("GPU: %1\n").arg(m_clipMonitor->getGPUInfo().join(QLatin1Char('/'))));
    }
    debuginfo.append(QStringLiteral("Movit (GPU): %1\n").arg(KdenliveSettings::gpu_accel() ? QStringLiteral("enabled") : QStringLiteral("disabled")));
    debuginfo.append(QStringLiteral("Track Compositing: %1\n").arg(TransitionsRepository::get()->getCompositingTransition()));
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(debuginfo);
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    switch (event->type()) {
    case QEvent::ShortcutOverride:
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            if (pCore->isMediaCapturing()) {
                pCore->switchCapture();
                return true;
            }
            if (pCore->isMediaMonitoring()) {
                slotShowTrackRec(false);
                return true;
            }
            if (m_commandStack && m_commandStack->activeStack()) {
                if (m_commandStack->activeStack()->canUndo()) {
                    if (m_activeTool != ToolType::SelectTool) {
                        m_buttonSelectTool->trigger();
                        return true;
                    } else {
                        // Don't call selection clear if a drag operation is in progress
                        getCurrentTimeline()->model()->requestClearSelection();
                    }
                    return true;
                }
            }
        }
        break;
    case QEvent::ApplicationPaletteChange: {
        // KoraCut : la feuille de style suit le schéma de couleurs choisi dans le menu (sans redémarrer)
        UiUtils::applyKoraCutStyleSheet();
        if (m_assetPanel) {
            m_assetPanel->clear();
        }
        if (m_clipMonitor) {
            m_clipMonitor->setPalette(qApp->palette());
        }
        if (m_projectMonitor) {
            m_projectMonitor->setPalette(qApp->palette());
        }
        if (m_timelineTabs) {
            m_timelineTabs->setPalette(qApp->palette());
            if (getCurrentTimeline() && getCurrentTimeline()->controller()) {
                getCurrentTimeline()->controller()->resetView();
            }
        }
        applyToolMessageStyling();
        applyZoomLevelButtonStyling();

        for (KDDockWidgets::Core::Group *group : KDDockWidgets::DockRegistry::self()->groups()) {
            auto tab_bar = static_cast<KDDockWidgets::QtWidgets::TabBar *>(group->tabBar()->view());
            if (QProxyStyle *style = qobject_cast<QProxyStyle *>(tab_bar->style())) {
                style->setBaseStyle(QStyleFactory::create(qApp->style()->name()));
                tab_bar->setPalette(qApp->palette());
            }
        }

        Q_EMIT pCore->updatePalette();
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(object, event);
}

void MainWindow::slotRemoveBinDock(const QString &name)
{
    QWidget *toDelete = nullptr;
    int ix = 0;
    for (auto &b : m_binWidgets) {
        if (b->parentWidget()->objectName() == name) {
            toDelete = b->parentWidget();
            m_binWidgets.takeAt(ix);
            break;
        }
        ix++;
    }
    if (toDelete) {
        toDelete->deleteLater();
    }
    if (!m_windowClosing) {
        KdenliveSettings::setBinsCount(m_binWidgets.size());
        updateDockMenu();
    }
}

void MainWindow::addBin(Bin *bin, const QString &binName, bool updateCount, const QString &objectName)
{
    connect(bin, &Bin::findInTimeline, this, &MainWindow::slotClipInTimeline, Qt::DirectConnection);
    connect(bin, &Bin::setupTargets, this, [&](bool hasVideo, QMap<int, QString> audioStreams) {
        if (getCurrentTimeline() && getCurrentTimeline()->controller()) {
            getCurrentTimeline()->controller()->setTargetTracks(hasVideo, audioStreams);
        }
    });
    m_binWidgets << bin;
    if (m_binWidgets.size() > 1) {
        // This is a secondary bin widget
        int ix = 2;
        // Ensure we have a unique id
        QStringList objectNames;
        for (auto &b : m_binWidgets) {
            QWidget *p = b->parentWidget();
            if (p) {
                objectNames << p->objectName();
            }
        }
        QString newBinName = objectName.isEmpty() ? QStringLiteral("project_bin_%1").arg(ix) : objectName;
        while (objectNames.contains(newBinName)) {
            ix++;
            newBinName = QStringLiteral("project_bin_%1").arg(ix);
        }
        KDDockWidgets::QtWidgets::DockWidget *binDock =
            addDock(binName.isEmpty() ? i18n("Project Bin %1", ix) : binName, newBinName, bin, KDDockWidgets::Location_None, m_projectBinDock);
        if (pCore->guiReady()) {
            bin->setupGeneratorMenu();
        }
        connect(bin, &Bin::requestShowClipProperties, getBin(), &Bin::showClipProperties, Qt::QueuedConnection);
        connect(bin, &Bin::requestBinClose, this, [this, binDock]() { Q_EMIT removeBinDock(binDock->objectName()); });
        binDock->open();
        binDock->setAsCurrentTab();
    }
    if (updateCount) {
        KdenliveSettings::setBinsCount(m_binWidgets.size());
    }
    if (pCore->guiReady()) {
        updateDockMenu();
    }
}

void MainWindow::cleanBins()
{
    // Close secondary bins
    QWidget *wid = QApplication::focusWidget();
    bool binHasFocus = false;
    while (m_binWidgets.size() > 1) {
        int ix = 0;
        auto bin = m_binWidgets.at(ix);
        if (!binHasFocus && (bin == wid || bin->isAncestorOf(wid))) {
            binHasFocus = true;
        }
        if (bin->isMainBin()) {
            ix = 1;
            bin = m_binWidgets.at(ix);
            if (!binHasFocus && (bin == wid || bin->isAncestorOf(wid))) {
                binHasFocus = true;
            }
        }
        auto toDelete = bin->parentWidget();
        m_binWidgets.takeAt(ix);
        delete toDelete;
    }

    // Clean main bins last
    for (auto &bin : m_binWidgets) {
        if (!bin->isMainBin()) {
            continue;
        }
        bin->cleanDocument();
    }
    // Ensure monitor is cleared
    if (!binHasFocus) {
        pCore->getMonitor(Kdenlive::ClipMonitor)->refreshMonitor();
    }
}

void MainWindow::loadExtraBins(const QStringList binInfo)
{
    QString folderName;
    QStringList existingNames;
    pCore->lastActiveBin.clear();

    for (auto &bin : m_binWidgets) {
        KDDockWidgets::QtWidgets::DockWidget *dock = qobject_cast<KDDockWidgets::QtWidgets::DockWidget *>(bin->parentWidget());
        if (!dock) {
            continue;
        }
        bool binFound = false;
        const QString dockName = dock->objectName() + QLatin1Char(':');
        for (const QString &info : binInfo) {
            if (info.startsWith(dockName)) {
                existingNames << bin->loadInfo(info.split(QLatin1Char(':')), existingNames);
                binFound = true;
                break;
            }
        }
        if (!binFound) {
            // Init bin with default settings
            existingNames << bin->loadInfo({}, existingNames);
        }
    }
}

const QStringList MainWindow::extraBinIds() const
{
    QStringList ids;
    for (auto &b : m_binWidgets) {
        ids << b->binInfoToString();
    }
    return ids;
}

void MainWindow::folderRenamed(const QString &binId, const QString &folderName)
{
    for (auto &b : m_binWidgets) {
        // Find out dock widget
        if (b->rootFolderId() == binId) {
            KDDockWidgets::QtWidgets::DockWidget *dock = qobject_cast<KDDockWidgets::QtWidgets::DockWidget *>(b->parentWidget());
            if (dock) {
                dock->setWindowTitle(folderName);
            }
            break;
        }
    }
}

void MainWindow::blockBins(bool block)
{
    for (auto &b : m_binWidgets) {
        b->blockBin(block);
    }
}

Bin *MainWindow::getBin()
{
    if (m_binWidgets.isEmpty()) {
        return nullptr;
    }
    return m_binWidgets.first();
}

Bin *MainWindow::activeBin()
{
    if (!pCore->lastActiveBin.isEmpty()) {
        for (auto &bin : m_binWidgets) {
            if (bin->parentWidget()->objectName() == pCore->lastActiveBin) {
                return bin;
            }
        }
    }
    QWidget *wid = QApplication::focusWidget();
    if (wid) {
        for (auto &bin : m_binWidgets) {
            if (bin == wid || bin->isAncestorOf(wid)) {
                return bin;
            }
        }
    }
    return m_binWidgets.first();
}

int MainWindow::binCount() const
{
    if (m_binWidgets.isEmpty()) {
        return 0;
    }
    return m_binWidgets.count();
}

void MainWindow::checkMaxCacheSize()
{
    if (KdenliveSettings::lastCacheCheck().daysTo(QDateTime::currentDateTime()) < 12) {
        return;
    }
    if (KdenliveSettings::checkForUpdate()) {
        // Check if the Kdenlive version is very old
        const QStringList kdenliveVersion = KAboutData::applicationData().version().split(QLatin1Char('.'));
        if (kdenliveVersion.size() > 2) {
            bool ok;
            int kdenliveYear = kdenliveVersion.at(0).toInt(&ok);
            if (ok) {
                int kdenliveMonth = kdenliveVersion.at(1).toInt(&ok);
                if (ok) {
                    if (kdenliveYear < 100) {
                        kdenliveYear += 2000;
                    }
                    QDate releaseDate = QDate(kdenliveYear, kdenliveMonth, 1);
                    if (releaseDate.isValid()) {
                        int days = releaseDate.daysTo(QDate::currentDate());
                        if (days > 180) {
                            // Propose update
                            QAction *updateAction = new QAction(i18n("Go to download page"), this);
                            connect(updateAction, &QAction::triggered, this, []() {
                                QDesktopServices::openUrl(
                                    QUrl(QStringLiteral("https://koracut.org/telecharger")));
                            });
                            QAction *abortAction = new QAction(i18n("Never check again"), this);
                            connect(abortAction, &QAction::triggered, this, []() { KdenliveSettings::setCheckForUpdate(false); });
                            if (days > 360) {
                                Q_EMIT pCore->displayBinMessage(i18n("Your KoraCut version is older than 1 year, we strongly encourage you to upgrade"),
                                                                KMessageWidget::Warning, {updateAction, abortAction}, true,
                                                                BinMessage::BinCategory::UpdateMessage);
                            } else {
                                Q_EMIT pCore->displayBinMessage(i18n("Your KoraCut version is older than 6 months, we encourage you to upgrade"),
                                                                KMessageWidget::Information, {updateAction, abortAction}, true,
                                                                BinMessage::BinCategory::UpdateMessage);
                            }
                        }
                    }
                }
            }
        }
    }

    KdenliveSettings::setLastCacheCheck(QDateTime::currentDateTime());
    // Check cached data size
    if (KdenliveSettings::maxcachesize() <= 0 || pCore->currentDoc() == nullptr) {
        return;
    }
    bool ok;
    QDir cacheDir = pCore->currentDoc()->getCacheDir(SystemCacheRoot, &ok);
    if (!ok) {
        return;
    }
    QDir backupFolder(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/.backup"));
    QList<QDir> toAdd;
    QList<QDir> toRemove;
    if (cacheDir.exists()) {
        toAdd << cacheDir;
    }
    if (backupFolder.exists()) {
        // KoraCut : le dossier des sauvegardes, pas une seconde fois le cache (le total comptait le cache deux fois).
        toAdd << backupFolder;
    }
    if (cacheDir.cd(QStringLiteral("knewstuff"))) {
        toRemove << cacheDir;
        cacheDir.cdUp();
    }
    if (cacheDir.cd(QStringLiteral("attica"))) {
        toRemove << cacheDir;
        cacheDir.cdUp();
    }
    if (cacheDir.cd(QStringLiteral("proxy"))) {
        toRemove << cacheDir;
        cacheDir.cdUp();
    }
    pCore->displayMessage(i18n("Checking cached data size"), InformationMessage);
    // KoraCut (15 octobre 2026) : un cache trop gros se dit dans le chutier, avec un bouton. Avant, la fenêtre
    // « Gestion des données en cache » s'ouvrait toute seule à la fin de la mesure, à n'importe quel moment :
    // pendant l'ouverture d'un projet par la porte, elle gardait l'ancien document, détruit entre-temps (plantage).
    auto suggestCleanup = [this]() {
        auto *manage = new QAction(i18n("Manage Cached Data…"), this);
        connect(manage, &QAction::triggered, this, &MainWindow::slotManageCache);
        Q_EMIT pCore->displayBinMessage(
            i18n("Your cache and backup data exceeds %1, cleanup is recommended.", KIO::convertSize(KIO::filesize_t(1048576) * KdenliveSettings::maxcachesize())),
            KMessageWidget::Information, {manage}, true, BinMessage::BinCategory::InformationMessage);
        qInfo() << "KoraCut: cache trop gros, nettoyage propose" << m_totalCacheSize;
    };
    while (!toAdd.isEmpty()) {
        QDir dir = toAdd.takeFirst();
        m_totalCacheJobs++;
        QFutureWatcher<KIO::filesize_t> *watcher = new QFutureWatcher<KIO::filesize_t>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, suggestCleanup] {
            KIO::filesize_t size = watcher->result();
            m_totalCacheSize += size;
            m_totalCacheJobs--;
            watcher->deleteLater();
            if (m_totalCacheJobs == 0 && m_totalCacheSize > KIO::filesize_t(1048576) * KdenliveSettings::maxcachesize()) {
                suggestCleanup();
            }
        });
        QFuture<KIO::filesize_t> future = QtConcurrent::run(&MainWindow::fetchFolderSize, this, dir.absolutePath());
        watcher->setFuture(future);
    }
    while (!toRemove.isEmpty()) {
        QDir dir = toRemove.takeFirst();
        m_totalCacheJobs++;
        QFutureWatcher<KIO::filesize_t> *watcher = new QFutureWatcher<KIO::filesize_t>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, suggestCleanup] {
            KIO::filesize_t size = watcher->result();
            m_totalCacheSize -= size;
            m_totalCacheJobs--;
            watcher->deleteLater();
            if (m_totalCacheJobs == 0 && m_totalCacheSize > KIO::filesize_t(1048576) * KdenliveSettings::maxcachesize()) {
                suggestCleanup();
            }
        });
        QFuture<KIO::filesize_t> future = QtConcurrent::run(&MainWindow::fetchFolderSize, this, dir.absolutePath());
        watcher->setFuture(future);
    }
}

void MainWindow::manageClipJobs(AbstractTask::JOBTYPE type, QWidget *parentWidget)
{
    QScopedPointer<ClipJobManager> dialog(new ClipJobManager(type, parentWidget ? parentWidget : this));
    dialog->exec();
    // Rebuild list of clip jobs
    buildDynamicActions();
    loadClipActions();
}

TimelineWidget *MainWindow::openTimeline(const QUuid &uuid, int ix, const QString &tabName, std::shared_ptr<TimelineItemModel> timelineModel,
                                         bool openInMonitor)
{
    // Create a new timeline tab
    KdenliveDoc *project = pCore->currentDoc();
    TimelineWidget *timeline =
        m_timelineTabs->addTimeline(uuid, ix, tabName, timelineModel, pCore->monitorManager()->projectMonitor()->getControllerProxy(), openInMonitor);
    slotSetZoom(project->zoom(uuid).x(), false);
    if (openInMonitor) {
        m_projectMonitor->slotLoadClipZone(project->zone(uuid));
    }
    getTimeline(uuid)->controller()->setZone(project->zone(uuid), false);
    getTimeline(uuid)->controller()->setScrollPos(project->getSequenceProperty(uuid, QStringLiteral("scrollPos")).toInt());
    return timeline;
}

void MainWindow::seekIfCurrent(const QUuid uuid, int pos)
{
    if (getCurrentTimeline()->getUuid() == uuid) {
        pCore->monitorManager()->projectMonitor()->slotSeek(pos);
    }
}

bool MainWindow::raiseTimeline(const QUuid &uuid)
{
    return m_timelineTabs->raiseTimeline(uuid);
}

void MainWindow::connectTimeline()
{
    qDebug() << "::::::::::: connecting timeline: " << getCurrentTimeline()->getUuid() << ", DUR: " << getCurrentTimeline()->controller()->duration();
    if (!getCurrentTimeline()->model()) {
        qDebug() << "::::::::::: TIMELINE HAS NO MODEL";
    }
    // We just switched timeline, ensure a monitor effects view does not remain from previous
    m_projectMonitor->resetScene();
    const QUuid uuid = getCurrentTimeline()->getUuid();
    pCore->projectManager()->setActiveTimeline(uuid);
    if (m_porte) {
        // La porte publie la sélection de la séquence active (événement `selection`, document 18).
        m_porte->watchTimeline(getCurrentTimeline()->model());
    }
    if (m_motionMirror) {
        // Le module Motion suit la séquence active : ses compositions vivent chacune dans une séquence.
        m_motionMirror->setTimeline(getCurrentTimeline()->model());
    }
    connect(m_projectMonitor, &Monitor::multitrackView, getCurrentTimeline()->controller(), &TimelineController::slotMultitrackView, Qt::UniqueConnection);
    connect(m_projectMonitor, &Monitor::activateTrack, getCurrentTimeline()->controller(), &TimelineController::activateTrackAndSelect, Qt::UniqueConnection);
    connect(getCurrentTimeline()->controller(), &TimelineController::timelineClipSelected, this, [&](bool selected) {
        m_loopClip->setEnabled(selected);
        Q_EMIT pCore->library()->enableAddSelection(selected);
    });
    connect(pCore->library(), &LibraryWidget::saveTimelineSelection, getCurrentTimeline()->controller(), &TimelineController::saveTimelineSelection,
            Qt::UniqueConnection);
    getCurrentTimeline()->controller()->clipActions = kdenliveCategoryMap.value(QStringLiteral("timelineselection"))->actions();
    connect(getCurrentTimeline()->controller(), &TimelineController::durationChanged, pCore->projectManager(), &ProjectManager::adjustProjectDuration);
    connect(pCore.get(), &Core::processDragEnd, getCurrentTimeline(), &TimelineWidget::endDrag);
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);

    KdenliveDoc *project = pCore->currentDoc();
    QSignalBlocker blocker(m_zoomSlider);
    m_zoomSlider->setValue(project->zoom(uuid).x());
    pCore->monitorManager()->projectMonitor()->adjustRulerSize(getCurrentTimeline()->model()->duration() - 1, project->getFilteredGuideModel(uuid));
    pCore->monitorManager()->projectMonitor()->loadZone(getCurrentTimeline()->controller()->zoneIn(), getCurrentTimeline()->controller()->zoneOut());

    connect(project, &KdenliveDoc::docModified, this, &MainWindow::slotUpdateDocumentState);
    slotUpdateDocumentState(project->isModified());

    // KoraCut : les rendus des projets non ouverts depuis 30 jours partent (une fois par jour, en tâche de fond).
    KoraCutRender::cleanupOldProjects(project->getDocumentProperty(QStringLiteral("documentid")));
    // Timeline preview
    QAction *previewRender = actionCollection()->action(QStringLiteral("prerender_timeline_zone"));
    if (previewRender) {
        previewRender->setEnabled(true);
    }
    QAction *disablePreview = actionCollection()->action(QStringLiteral("disable_preview"));
    if (getCurrentTimeline()->model()->hasTimelinePreview()) {
        disablePreview->setEnabled(true);
    } else {
        disablePreview->setEnabled(false);
    }
    disablePreview->blockSignals(true);
    disablePreview->setChecked(false);
    disablePreview->blockSignals(false);

    // update track compositing
    bool compositing = project->getSequenceProperty(uuid, QStringLiteral("compositing"), QStringLiteral("1")).toInt() > 0;
    Q_EMIT project->updateCompositionMode(compositing);

    // Ensure the active timeline has an opaque black background for compositing
    getCurrentTimeline()->model()->makeTransparentBg(false);
    // Initialize audio mixer
    getCurrentTimeline()->model()->rebuildMixer();

    // Audio record actions
    connect(pCore.get(), &Core::recordAudio, getCurrentTimeline()->controller(), &TimelineController::switchRecording, Qt::DirectConnection);

    // switch to active subtitle model
    pCore->subtitleWidget()->setModel(getCurrentTimeline()->model()->getSubtitleModel());
    bool hasSubtitleModel = getCurrentTimeline()->hasSubtitles();
    Q_EMIT getCurrentTimeline()->controller()->subtitlesLockedChanged();
    Q_EMIT getCurrentTimeline()->controller()->subtitlesDisabledChanged();
    bool showSubs = project->getSequenceProperty(uuid, QStringLiteral("hidesubtitle")).toInt() == 0;
    KdenliveSettings::setShowSubtitles(showSubs && hasSubtitleModel);
    getCurrentTimeline()->connectSubtitleModel(hasSubtitleModel);
    m_buttonSubtitleEditTool->setChecked(showSubs && hasSubtitleModel);
    pCore->projectManager()->updateSequenceOffset(uuid);
    if (hasSubtitleModel) {
        // Restore style
        getCurrentTimeline()->model()->getSubtitleModel()->loadProperties({});
        slotShowSubtitles(showSubs);
    }
    // Display timeline guides in the guides list
    pCore->guidesList()->setModel(project->getGuideModel(uuid), project->getFilteredGuideModel(uuid));
    if (m_renderWidget) {
        slotCheckRenderStatus();
        m_renderWidget->setGuides(project->getGuideModel(uuid));
        m_renderWidget->showRenderDuration();
    }
}

void MainWindow::disconnectTimeline(TimelineWidget *timeline, bool onClose)
{
    // Save current tab timeline position
    if (pCore->currentDoc()) {
        // pCore->currentDoc()->position = pCore->getTimelinePosition();
        //  disconnect(pCore->currentDoc(), &KdenliveDoc::docModified, this, &MainWindow::slotUpdateDocumentState);
    }
    if (!onClose) {
        // Ensure the active timeline has an transparent black background for embedded compositing
        timeline->model()->makeTransparentBg(true);
        if (!pCore->currentDoc()->loading) {
            // Update audio thumb if necessary
            const QUuid uuid = timeline->getUuid();
            const QString binId = pCore->projectItemModel()->getSequenceId(uuid);
            getBin()->rebuildAudioThumb(binId);
        }
    }
    disconnect(timeline->controller(), &TimelineController::durationChanged, pCore->projectManager(), &ProjectManager::adjustProjectDuration);
    disconnect(m_projectMonitor, &Monitor::multitrackView, timeline->controller(), &TimelineController::slotMultitrackView);
    disconnect(m_projectMonitor, &Monitor::activateTrack, timeline->controller(), &TimelineController::activateTrackAndSelect);
    disconnect(pCore->library(), &LibraryWidget::saveTimelineSelection, timeline->controller(), &TimelineController::saveTimelineSelection);
    timeline->controller()->clipActions = QList<QAction *>();
    disconnect(pCore.get(), &Core::processDragEnd, timeline, &TimelineWidget::endDrag);
    pCore->monitorManager()->projectMonitor()->setProducer(QUuid(), nullptr, -2);
    // Audio record actions
    disconnect(pCore.get(), &Core::recordAudio, timeline->controller(), &TimelineController::switchRecording);
}

// Static
QProcessEnvironment MainWindow::getCleanEnvironement()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    qDebug() << "::: GOT ENV: " << env.value("LD_LIBRARY_PATH") << ", PATH: " << env.value("PATH") << "\n\nXDG:\n" << env.value("XDG_DATA_DIRS");
    QStringList libPath = env.value(QStringLiteral("LD_LIBRARY_PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    QStringList updatedLDPath;
    for (auto &s : libPath) {
        if (!s.startsWith(QStringLiteral("/tmp/.mount_"))) {
            updatedLDPath << s;
        }
    }
    if (updatedLDPath.isEmpty()) {
        env.remove(QStringLiteral("LD_LIBRARY_PATH"));
    } else {
        env.insert(QStringLiteral("LD_LIBRARY_PATH"), updatedLDPath.join(QLatin1Char(':')));
    }
    // Path
    libPath = env.value(QStringLiteral("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    updatedLDPath.clear();
    for (auto &s : libPath) {
        if (!s.startsWith(QStringLiteral("/tmp/.mount_"))) {
            updatedLDPath << s;
        }
    }
    if (updatedLDPath.isEmpty()) {
        env.remove(QStringLiteral("PATH"));
    } else {
        env.insert(QStringLiteral("PATH"), updatedLDPath.join(QLatin1Char(':')));
    }
    // XDG
    libPath = env.value(QStringLiteral("XDG_DATA_DIRS")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    updatedLDPath.clear();
    for (auto &s : libPath) {
        if (!s.startsWith(QStringLiteral("/tmp/.mount_"))) {
            updatedLDPath << s;
        }
    }
    if (updatedLDPath.isEmpty()) {
        env.remove(QStringLiteral("XDG_DATA_DIRS"));
    } else {
        env.insert(QStringLiteral("XDG_DATA_DIRS"), updatedLDPath.join(QLatin1Char(':')));
    }
    env.remove(QStringLiteral("QT_QPA_PLATFORM"));
    return env;
}

void MainWindow::appHelpActivated()
{
    // Pas d'aide locale : on ouvre notre documentation en ligne.
    const QString helpUrl = QStringLiteral("https://koracut.org/docs");
    if (pCore->packageType() == LinuxPackageType::AppImage) {
        qDebug() << "::::: LAUNCHING APPIMAGE BROWSER.........";
        QProcessEnvironment env = getCleanEnvironement();
        QProcess process;
        process.setProcessEnvironment(env);
        QString openPath = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
        qDebug() << "------------\nFOUND OPEN PATH: " << openPath;
        process.setProgram(openPath.isEmpty() ? QStringLiteral("xdg-open") : openPath);
        process.setArguments({helpUrl});
        process.startDetached();
    } else {
        QDesktopServices::openUrl(QUrl(helpUrl));
    }
}

void MainWindow::slotCreateSequenceFromSelection()
{
    pCore->projectManager()->slotCreateSequenceFromSelection();
}

void MainWindow::slotCopyAndCreateSequenceFromSelection()
{
    pCore->projectManager()->slotCopyAndCreateSequenceFromSelection();
}

ObjectId MainWindow::effectStackOwner()
{
    return m_assetPanel->effectStackOwner();
}

bool MainWindow::effectIsMasterOnly(const QString &assetId) const
{
    if (m_effectList2) {
        return m_effectList2->isMasterOnly(assetId);
    }
    return false;
}

void MainWindow::reloadAssetPanel()
{
    ObjectId owner = m_assetPanel->effectStackOwner();
    if (owner.type == KdenliveObjectType::NoItem) {
        return;
    }
    m_assetPanel->clearAssetPanel(-1);
    pCore->showEffectStackFromId(owner);
}

bool MainWindow::hasRunningTask() const
{
    return m_assetPanel->hasRunningTask();
}

bool MainWindow::hasRunningRenderTask() const
{
    if (m_renderWidget) {
        return m_renderWidget->isRendering();
    }
    return false;
}

KIO::filesize_t MainWindow::fetchFolderSize(const QString path)
{
    // KIO::DirectorySizeJob doesn't work on Windows, so use Qt only
    KIO::filesize_t totalSize = 0;
    const auto flags = QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::Recursive;
    for (const auto &dirEntry : QDirListing(path, flags)) {
        totalSize += dirEntry.size();
    }
    return totalSize;
}

#ifdef DEBUG_MAINW
#undef DEBUG_MAINW
#endif

void MainWindow::slotCreateRangeMarkerFromZone()
{
    if (!getCurrentTimeline() || !pCore->currentDoc()) {
        return;
    }

    if (pCore->monitorManager()->clipMonitor()->isActive()) {
        pCore->monitorManager()->clipMonitor()->slotCreateRangeMarkerFromZone();
    } else {
        pCore->monitorManager()->projectMonitor()->slotCreateRangeMarkerFromZone();
    }
}

void MainWindow::slotCreateRangeMarkerFromZoneQuick()
{
    if (!getCurrentTimeline() || !pCore->currentDoc()) {
        return;
    }

    if (pCore->monitorManager()->clipMonitor()->isActive()) {
        pCore->monitorManager()->clipMonitor()->slotCreateRangeMarkerFromZoneQuick();
    } else {
        pCore->monitorManager()->projectMonitor()->slotCreateRangeMarkerFromZoneQuick();
    }
}

QSize MainWindow::sizeHint() const
{
    const QSize desktopSize = QGuiApplication::primaryScreen()->availableSize();
    return KXmlGuiWindow::sizeHint().expandedTo(desktopSize * 0.8);
}

void MainWindow::slotEditToolbars()
{
    // backup all current shortcuts
    QMap<QString, QKeySequence> shortcutBackup;
    for (auto *action : actionCollection()->actions()) {
        if (!action->shortcut().isEmpty()) {
            shortcutBackup.insert(action->objectName(), action->shortcut());
        }
    }

    // open the standard KDE Toolbar Editor
    KEditToolBar dlg(guiFactory(), this);

    // connect dialog changes to Kdenlive's refresh/save function (Fixes visual toolbar updates)
    connect(&dlg, &KEditToolBar::newToolBarConfig, this, &MainWindow::saveNewToolbarConfig);

    dlg.exec();

    // restore shortcuts
    bool restorationHappened = false;

    for (auto i = shortcutBackup.begin(); i != shortcutBackup.end(); ++i) {
        QAction *action = actionCollection()->action(i.key());
        if (action && action->shortcut() != i.value()) {
            if (!i.value().isEmpty()) {
                for (auto *otherAction : actionCollection()->actions()) {
                    if (otherAction != action && otherAction->shortcut() == i.value()) {
                        otherAction->setShortcut(QKeySequence());
                    }
                }
            }
            action->setShortcut(i.value());
            actionCollection()->setShortcutsConfigurable(action, true);
            restorationHappened = true;
        }
    }

    // PERSISTENCE FIX: Force the action collection to write its current state (with our restored shortcuts)
    // to the standard XML file immediately.
    if (restorationHappened) {
        actionCollection()->writeSettings();
    }
}
