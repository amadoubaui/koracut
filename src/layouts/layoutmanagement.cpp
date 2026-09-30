/*
SPDX-FileCopyrightText: 2012 Jean-Baptiste Mardelle <jb@kdenlive.org>
SPDX-FileCopyrightText: 2014 Till Theato <root@ttill.de>
SPDX-FileCopyrightText: 2020 Julius Künzel <julius.kuenzel@kde.org>
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "layouts/layoutmanagement.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "koracut/koracuticons.h"
#include "koracut/koracutmodules.h"
#include "koracut/koracutpanelfocus.h"
#include "koracut/koracuttopbar.h"
#include "koracut/koracutworkspace.h"
#include "layouts/layoutmanagerdialog.h"
#include "layouts/layoutswitcher.h"
#include "mainwindow.h"

#include <KMessageBox>
#include <QButtonGroup>
#include <QDialog>
#include <QFile>
#include <QActionGroup>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFrame>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLocale>
#include <QTime>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <KActionCollection>
#include <KColorScheme>
#include <KConfigGroup>
#include <KIconEffect>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KToolBar>
#include <KXMLGUIFactory>

LayoutManagement::LayoutManagement(QObject *parent)
    : QObject(parent)
{
    // Initialize layout categories in KDE action framework
    m_layoutCategory = new KActionCategory(i18n("Layouts"), pCore->window()->actionCollection());
    m_loadLayout = new KSelectAction(i18n("Load Layout"), pCore->window()->actionCollection());
    pCore->window()->actionCollection()->setShortcutsConfigurable(m_loadLayout, false);

    // Required to enable user to add the load layout action to toolbar
    m_layoutCategory->addAction(QStringLiteral("load_layouts"), m_loadLayout);
    connect(m_loadLayout, &KSelectAction::actionTriggered, this,
            static_cast<void (LayoutManagement::*)(QAction *)>(&LayoutManagement::slotLoadLayoutFromAction));

    QAction *saveLayout = new QAction(i18n("Save Layout…"), pCore->window()->actionCollection());
    m_layoutCategory->addAction(QStringLiteral("save_layout"), saveLayout);
    connect(saveLayout, &QAction::triggered, this, &LayoutManagement::slotSaveLayout);

    QAction *manageLayout = new QAction(i18n("Manage Layouts…"), pCore->window()->actionCollection());
    m_layoutCategory->addAction(QStringLiteral("manage_layout"), manageLayout);
    connect(manageLayout, &QAction::triggered, this, &LayoutManagement::slotManageLayouts);

    // Create 9 layout actions
    for (int i = 1; i < 10; i++) {
        QAction *load = new QAction(QIcon(), QString(), this);
        m_layoutActions << m_layoutCategory->addAction("load_layout" + QString::number(i), load);
    }

    // KoraCut : le sélecteur de dispositions est devenu les onglets de modules de la barre du haut.
    // Il n'est plus posé dans le coin de la barre de menus (invisible sur Mac, où elle est native).
    MainWindow *main = pCore->window();
    m_container = main->topBar();
    m_layoutSwitcher = main->topBar()->moduleTabs();
    connect(m_layoutSwitcher, &LayoutSwitcher::layoutSelected, this, &LayoutManagement::slotModuleSelected);

    // Témoin d'enregistrement automatique, à droite de l'état du projet.
    m_autosaveContainer = nullptr;
    m_switcherContainer = nullptr;
    m_autosaveLabel = new QLabel(QString(), main->topBar());
    // KoraCut, 22 septembre 2026 : le témoin existait mais restait muet. Quelqu'un qui monte
    // une heure ne sait pas qu'il est protégé. L'infobulle dit maintenant le rythme, puis
    // l'heure de la dernière sauvegarde.
    m_autosaveLabel->setToolTip(i18nc("@info:tooltip témoin de sauvegarde automatique",
                                      "Automatic save every %1 minutes.\nNo save since the project was opened.",
                                      qMax(1, KdenliveSettings::autosave_time() / 60)));
    int iconSize = main->style()->pixelMetric(QStyle::PM_SmallIconSize);
    m_autosaveLabel->setFixedSize(iconSize, iconSize);
    m_autosaveLabel->setScaledContents(true);
    updateAutosaveIcon();
    m_autosaveLabel->hide(); // caché tant qu'aucun enregistrement automatique n'a lieu
    main->topBar()->setAutoSaveIndicator(m_autosaveLabel);

    slotUpdatePalette();
    connect(pCore.get(), &Core::updatePalette, this, &LayoutManagement::slotUpdatePalette);

    m_autosaveDisplayTimer.setInterval(2000);
    m_autosaveDisplayTimer.setSingleShot(true);
    connect(&m_autosaveDisplayTimer, &QTimer::timeout, this, &LayoutManagement::hideAutoSave);
    connect(pCore.get(), &Core::startAutoSave, this, &LayoutManagement::startAutoSave);

    buildWorkspaceMenu();

    // Load layouts and initialize UI
    initializeLayouts();
}

namespace {
// Réglage d'orientation du plan de travail : « auto » (selon le format du projet), « horizontal », « vertical ».
// Rangé dans koracutrc, groupe KoraCut, plutôt que dans kdenlivesettings.kcfg dont chaque retouche
// fait recompiler presque toute l'application.
QString orientationSetting()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("orientationPlanDeTravail", QStringLiteral("auto"));
}
void setOrientationSetting(const QString &value)
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
    group.writeEntry("orientationPlanDeTravail", value);
    group.sync();
}
}

bool LayoutManagement::wantsVertical() const
{
    const QString orientation = orientationSetting();
    if (orientation == QLatin1String("vertical")) {
        return true;
    }
    if (orientation == QLatin1String("horizontal")) {
        return false;
    }
    return pCore->isVertical();
}

void LayoutManagement::buildWorkspaceMenu()
{
    // KoraCut, 22 septembre 2026 : le bouton de la barre du haut n'avait jamais reçu de menu.
    // Amadou Ba voulait un endroit visible pour remettre les panneaux en place, choisir un plan
    // vertical ou horizontal et agrandir un panneau. Tout est ici, à un clic.
    MainWindow *main = pCore->window();
    KActionCollection *collection = main->actionCollection();

    auto *reset = new QAction(i18nc("@action", "Reset the Workspace"), this);
    reset->setToolTip(i18nc("@info:tooltip", "Puts the module panels back where they started"));
    m_layoutCategory->addAction(QStringLiteral("koracut_reset_workspace"), reset);
    connect(reset, &QAction::triggered, this, &LayoutManagement::resetWorkspace);

    auto *resetAll = new QAction(i18nc("@action", "Reset Everything"), this);
    resetAll->setToolTip(i18nc("@info:tooltip", "Puts the four modules back in their original layout. Your layouts saved under another name stay."));
    m_layoutCategory->addAction(QStringLiteral("koracut_reset_all_workspaces"), resetAll);
    connect(resetAll, &QAction::triggered, this, [this]() {
        if (KMessageBox::questionTwoActions(pCore->window(),
                                            i18nc("@info", "Put the Editing, Color, Audio and VFX modules back in their original layout?\nThe layouts you saved under another name are not touched."),
                                            i18nc("@title:window", "Reset Everything"), KGuiItem(i18nc("@action:button", "Reset Everything")),
                                            KStandardGuiItem::cancel()) != KMessageBox::PrimaryAction) {
            return;
        }
        m_layoutCollection.resetDefaultLayouts();
        initializeLayouts();
        resetWorkspace();
    });

    auto *maximize = new QAction(i18nc("@action", "Maximize the Active Panel"), this);
    maximize->setCheckable(true);
    m_layoutCategory->addAction(QStringLiteral("koracut_maximize_panel"), maximize);
    // ⌘⇧F sur Mac, Ctrl+Maj+F ailleurs : libre dans les 834 actions (KORACUT_DUMP_ACTIONS=all, 22/09/2026).
    collection->setDefaultShortcut(maximize, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(maximize, &QAction::triggered, this, []() { KoraCutPanelFocus::toggleFocused(); });

    auto *menu = new QMenu(main);
    menu->setToolTipsVisible(true);
    menu->addAction(reset);
    menu->addAction(resetAll);
    menu->addAction(maximize);
    auto *hint = menu->addAction(i18nc("@info menu, astuce", "Tip: triple-click a panel bar to maximize it"));
    hint->setEnabled(false);

    menu->addSection(i18nc("@title:menu", "Orientation"));
    auto *orientationGroup = new QActionGroup(menu);
    const QList<QPair<QString, QString>> orientations = {
        {QStringLiteral("auto"), i18nc("@option:radio orientation du plan de travail", "Automatic (from the project format)")},
        {QStringLiteral("horizontal"), i18nc("@option:radio orientation du plan de travail", "Horizontal (16:9)")},
        {QStringLiteral("vertical"), i18nc("@option:radio orientation du plan de travail", "Vertical (9:16)")},
    };
    for (const auto &entry : orientations) {
        QAction *a = menu->addAction(entry.second);
        a->setCheckable(true);
        a->setData(entry.first);
        orientationGroup->addAction(a);
    }
    connect(orientationGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        setOrientationSetting(a->data().toString());
        if (!KoraCutModules::isUnderConstruction(m_currentLayoutId)) {
            slotLoadLayoutById(m_currentLayoutId);
        }
    });

    // L'écran : la timeline seule en bas (portable, moniteur 16:9 : la règle) ou à côté du mixeur
    // (moniteur très large, 21:9 et plus). Automatique : selon la forme de l'écran.
    menu->addSection(i18nc("@title:menu", "Screen"));
    auto *screenGroup = new QActionGroup(menu);
    const QList<QPair<QString, QString>> screens = {
        {QStringLiteral("auto"), i18nc("@option:radio taille d'écran du plan de travail", "Automatic (from the screen shape)")},
        {QStringLiteral("compact"), i18nc("@option:radio taille d'écran du plan de travail", "Timeline alone at the bottom (laptop, 16:9 monitor)")},
        {QStringLiteral("large"), i18nc("@option:radio taille d'écran du plan de travail", "Timeline next to the mixer (very wide monitor)")},
    };
    for (const auto &entry : screens) {
        QAction *a = menu->addAction(entry.second);
        a->setCheckable(true);
        a->setData(entry.first);
        screenGroup->addAction(a);
    }
    connect(screenGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        KoraCutWorkspace::setScreenSetting(a->data().toString());
        if (!KoraCutModules::isUnderConstruction(m_currentLayoutId)) {
            slotLoadLayoutById(m_currentLayoutId);
        }
    });

    menu->addSection(i18nc("@title:menu", "Custom Layouts"));
    auto *customMenu = menu->addMenu(i18nc("@title:menu", "My Saved Layouts"));
    menu->addAction(collection->action(QStringLiteral("save_layout")));
    menu->addAction(collection->action(QStringLiteral("manage_layout")));
    collection->action(QStringLiteral("save_layout"))->setText(i18nc("@action", "Save This Workspace…"));
    collection->action(QStringLiteral("manage_layout"))->setText(i18nc("@action", "Manage Workspaces…"));

    connect(menu, &QMenu::aboutToShow, this, [this, reset, maximize, orientationGroup, screenGroup, customMenu]() {
        const QString screenChoice = KoraCutWorkspace::screenSetting();
        const auto screenActions = screenGroup->actions();
        for (QAction *a : screenActions) {
            a->setChecked(a->data().toString() == screenChoice);
        }
        const bool construction = KoraCutModules::isUnderConstruction(m_currentLayoutId);
        reset->setEnabled(!construction);
        maximize->setEnabled(!construction);
        maximize->setChecked(KoraCutPanelFocus::isActive());
        const QString orientation = orientationSetting();
        const auto actions = orientationGroup->actions();
        for (QAction *a : actions) {
            a->setChecked(a->data().toString() == orientation);
        }
        customMenu->clear();
        const QList<LayoutInfo> layouts = m_layoutCollection.getAllLayouts();
        for (const LayoutInfo &layout : layouts) {
            if (layout.isDefault || layout.internalId.isEmpty()) {
                continue;
            }
            QAction *load = customMenu->addAction(layout.displayName);
            connect(load, &QAction::triggered, this, [this, id = layout.internalId]() { slotLoadLayoutById(id); });
        }
        if (customMenu->isEmpty()) {
            customMenu->addAction(i18nc("@info menu vide", "No saved layout yet"))->setEnabled(false);
        }
    });
    main->topBar()->setLayoutMenu(menu);
}

void LayoutManagement::resetWorkspace()
{
    const QString id = m_currentLayoutId;
    if (id.isEmpty() || KoraCutModules::isUnderConstruction(id)) {
        return;
    }
    // La disposition livrée avec l'application, pas une version que l'utilisateur aurait
    // enregistrée par-dessus sous le même nom (dossier des données modifiables).
    const QString writable = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    LayoutInfo layout = m_layoutCollection.getLayout(id);
    const QStringList horizontals = QStandardPaths::locateAll(QStandardPaths::AppLocalDataLocation, QStringLiteral("layouts/%1.json").arg(id));
    const QStringList verticals = QStandardPaths::locateAll(QStandardPaths::AppLocalDataLocation, QStringLiteral("layouts/%1_vertical.json").arg(id));
    const QStringList compacts = QStandardPaths::locateAll(QStandardPaths::AppLocalDataLocation, QStringLiteral("layouts/%1_compact.json").arg(id));
    const QStringList verticalCompacts =
        QStandardPaths::locateAll(QStandardPaths::AppLocalDataLocation, QStringLiteral("layouts/%1_vertical_compact.json").arg(id));
    const auto shipped = [&writable](const QStringList &paths, QString &target) {
        for (const QString &path : paths) {
            if (!path.startsWith(writable)) {
                target = path;
            }
        }
    };
    shipped(horizontals, layout.path);
    shipped(verticals, layout.verticalPath);
    shipped(compacts, layout.compactPath);
    shipped(verticalCompacts, layout.verticalCompactPath);
    if (slotLoadLayout(layout)) {
        pCore->displayMessage(i18nc("@info:status", "Workspace reset"), InformationMessage, 2000);
    }
}

void LayoutManagement::recheckScreen()
{
    if (!m_loadedFromFile || m_currentLayoutId.isEmpty() || KoraCutModules::isUnderConstruction(m_currentLayoutId)) {
        return;
    }
    if (KoraCutWorkspace::wantsCompact(pCore->window()) != m_loadedCompact) {
        slotLoadLayoutById(m_currentLayoutId);
    }
}

void LayoutManagement::startAutoSave()
{
    m_autosaveLabel->setToolTip(i18nc("@info:tooltip témoin de sauvegarde automatique",
                                      "Project saved automatically at %1.\nEvery %2 minutes.",
                                      QLocale().toString(QTime::currentTime(), QLocale::ShortFormat),
                                      qMax(1, KdenliveSettings::autosave_time() / 60)));
    m_autosaveLabel->show();
    m_autosaveDisplayTimer.start();
}

void LayoutManagement::hideAutoSave()
{
    m_autosaveLabel->hide();
}

void LayoutManagement::initializeLayouts()
{
    if (m_loadLayout == nullptr) {
        return;
    }

    // Get the currently selected layout
    m_currentLayoutId = m_layoutSwitcher->currentLayout();
    if (m_currentLayoutId.isEmpty()) {
        // Premier passage : on repart du dernier module utilisé.
        m_currentLayoutId = KdenliveSettings::koracut_module();
    }
    MainWindow *main = pCore->window();

    // Load layouts from config
    m_layoutCollection.loadLayouts();

    QList<LayoutInfo> allLayouts = m_layoutCollection.getAllLayouts();

    // KoraCut : les onglets ne sont plus les cinq premières dispositions trouvées, mais les sept modules.
    // Un module garde son onglet même sans disposition sur le disque (Photo, Illustration, Export).
    KColorScheme scheme(QPalette::Active, KColorScheme::Window);
    const QColor tabColor = scheme.foreground(KColorScheme::InactiveText).color();
    const QColor tabActiveColor = scheme.foreground(KColorScheme::NormalText).color();
    const qreal dpr = main->devicePixelRatioF();
    QList<LayoutTab> tabs;
    const QList<KoraCutModules::Module> &modules = KoraCutModules::all();
    for (const KoraCutModules::Module &module : modules) {
        LayoutTab tab;
        tab.id = module.id;
        tab.label = module.name;
        tab.icon = KoraCutIcons::icon(module.iconPath, 16, tabColor, tabActiveColor, dpr);
        const QString shortcut = QKeySequence(Qt::SHIFT | Qt::Key(Qt::Key_0 + module.number)).toString(QKeySequence::NativeText);
        tab.tooltip = module.ready ? i18nc("@info:tooltip nom du module puis son raccourci", "%1 (%2)", module.name, shortcut)
                                   : i18nc("@info:tooltip module pas encore écrit", "%1 (%2) · under construction", module.name, shortcut);
        tabs << tab;
    }
    m_layoutSwitcher->setLayouts(tabs, m_currentLayoutId);

    // Clear and recreate the menu actions
    m_loadLayout->removeAllActions();

    // Set up actions
    for (int i = 0; i < allLayouts.count(); i++) {
        const LayoutInfo &layout = allLayouts.at(i);
        QAction *load = nullptr;

        if (i >= m_layoutActions.count()) {
            load = new QAction(QIcon(), QString(), this);
            m_layoutActions << m_layoutCategory->addAction("load_layout" + QString::number(i + 1), load);
        } else {
            load = m_layoutActions.at(i);
        }

        if (layout.internalId.isEmpty()) {
            load->setText(QString());
            load->setIcon(QIcon());
        } else {
            load->setText(i18n("Layout %1: %2", i + 1, layout.displayName));
        }

        load->setData(layout.internalId);

        if (!layout.internalId.isEmpty()) {
            load->setEnabled(true);
            m_loadLayout->addAction(load);
        } else {
            load->setEnabled(false);
        }
    }

    // Required to trigger a refresh of the container buttons
    main->menuBar()->resize(main->menuBar()->sizeHint());
}

void LayoutManagement::slotLoadLayoutFromAction(QAction *action)
{
    if (!action) return;
    const QString layoutId = action->data().toString();
    if (layoutId.isEmpty()) return;
    slotLoadLayoutById(layoutId);
}

void LayoutManagement::slotModuleSelected(const QString &moduleId)
{
    MainWindow *main = pCore->window();
    KdenliveSettings::setKoracut_module(moduleId);
    if (KoraCutModules::isUnderConstruction(moduleId)) {
        // Photo, Illustration et Export n'ont pas encore de contenu : on montre la page « en construction »
        // sans toucher à la disposition des docks, pour la retrouver intacte au retour.
        m_currentLayoutId = moduleId;
        main->showModuleUnderConstruction(moduleId);
        return;
    }
    main->showDockArea();
    slotLoadLayoutById(moduleId);
}

bool LayoutManagement::slotLoadLayoutById(const QString &layoutId)
{
    // Get the layout from our collection
    LayoutInfo layout = m_layoutCollection.getLayout(layoutId);
    return slotLoadLayout(layout);
}

bool LayoutManagement::slotLoadLayout(LayoutInfo layout, bool onlyIfNoPrevious)
{
    if (!layout.isValid() || (onlyIfNoPrevious && m_firstLayoutLoaded)) {
        // Layout not found or has no data
        return false;
    }

    // Set as current layout
    m_currentLayoutId = layout.internalId;
    // Une nouvelle disposition remplace celle d'un panneau agrandi : la photo ne vaut plus rien.
    KoraCutPanelFocus::forget();

    // Parse layout data
    KDDockWidgets::LayoutSaver dockLayout(KDDockWidgets::RestoreOption_RelativeToMainWindow);
    // KoraCut : la variante dépend de l'orientation du projet et de la taille de l'écran (compact
    // sur un portable : timeline sur toute la largeur, mixeur dans la rangée du haut).
    const bool compact = KoraCutWorkspace::wantsCompact(pCore->window());
    const QString file = layout.pathFor(wantsVertical(), compact);
    if (!file.isEmpty()) {
        QFile lu(file);
        if (lu.open(QIODevice::ReadOnly)) {
            KoraCutWorkspace::closeDocksAbsentFrom(lu.readAll());
        }
    }
    const bool restore = !file.isEmpty() && dockLayout.restoreFromFile(file);
    if (!restore) {
        pCore->displayBinMessage(i18n("The layout %1 could not be restored, should be removed and recreated.", layout.displayName), KMessageWidget::Warning);
        return false;
    }
    m_firstLayoutLoaded = true;
    m_loadedCompact = compact;
    m_loadedFromFile = true;
    m_layoutSwitcher->setCurrentLayout(layout.internalId);
    // KoraCut : les dispositions livrées ont été enregistrées avec deux moniteurs côte à côte.
    // Le mode à un ou deux moniteurs est un réglage de l'utilisateur, pas une propriété de la
    // disposition : on le réapplique après chaque chargement plutôt que de réenregistrer les
    // cinq fichiers, qui seraient à refaire au moindre changement de disposition.
    // Avant le masquage des barres de titre : déplacer un dock recrée des groupes, et leurs
    // barres de titre reviendraient à l'écran si on masquait d'abord (vu le 21/09/2026, la barre
    // « Montage » réapparaissait au-dessus de la timeline).
    if (pCore->window()) {
        pCore->window()->setSingleMonitor(KdenliveSettings::singleMonitor());
    }
    KoraCutWorkspace::dockStrayPanels();
    if (!KdenliveSettings::showtitlebars()) {
        Q_EMIT pCore->hideBars(!KdenliveSettings::showtitlebars());
    }
    return true;
}

void LayoutManagement::adjustLayoutToDar()
{
    if (!m_currentLayoutId.isEmpty()) {
        LayoutInfo lay = m_layoutCollection.getLayout(m_currentLayoutId);
        if (lay.isValid()) {
            if (lay.hasHorizontalData() && lay.hasVerticalData()) {
                slotLoadLayoutById(lay.internalId);
            }
        }
    }
}

bool LayoutManagement::slotLoadLayoutFromData(const QString &layoutData, bool onlyIfNoPrevious)
{
    if (layoutData.isEmpty() || (onlyIfNoPrevious && m_firstLayoutLoaded)) {
        // Layout not found or has no data
        return false;
    }

    KDDockWidgets::LayoutSaver dockLayout(KDDockWidgets::RestoreOption_RelativeToMainWindow);
    KoraCutWorkspace::closeDocksAbsentFrom(layoutData.toUtf8());
    bool restore = dockLayout.restoreLayout(layoutData.toUtf8());
    if (!restore) {
        pCore->displayBinMessage(i18n("The layout from the project file could not be restored."), KMessageWidget::Warning);
        return false;
    }
    // Disposition venue des réglages ou du projet : on garde l'onglet du dernier module utilisé,
    // sinon la barre du haut n'affiche plus aucun module actif au démarrage.
    m_currentLayoutId = KdenliveSettings::koracut_module();
    m_layoutSwitcher->setCurrentLayout(m_currentLayoutId);
    m_loadedFromFile = false;
    KoraCutWorkspace::dockStrayPanels();
    if (!KdenliveSettings::showtitlebars() && m_firstLayoutLoaded) {
        Q_EMIT pCore->hideBars(!KdenliveSettings::showtitlebars());
    }
    m_firstLayoutLoaded = true;
    return true;
}

void LayoutManagement::slotSaveLayout()
{
    // Get a suggested name for the layout
    QString saveName = m_layoutCollection.getTranslatedName(m_layoutSwitcher->currentLayout());

    // Ask user for a name
    saveName = QInputDialog::getText(pCore->window(), i18nc("@title:window", "Save Layout"), i18n("Layout name:"), QLineEdit::Normal, saveName);
    if (saveName.isEmpty()) {
        return;
    }
    // Ensure the layout directory exists
    QDir layoutsFolder(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    layoutsFolder.mkdir(QStringLiteral("layouts"));
    if (!layoutsFolder.cd(QStringLiteral("layouts"))) {
        return;
    }
    LayoutInfo layout(saveName, saveName);
    // Get current window state
    QString fileName = saveName;
    if (pCore->isVertical()) {
        fileName.append(QStringLiteral("_vertical"));
    }
    fileName = layoutsFolder.absoluteFilePath(fileName + QStringLiteral(".json"));
    if (QFile::exists(fileName)) {
        if (KMessageBox::questionTwoActions(pCore->window(), i18n("The layout %1 already exists. Do you want to replace it?", saveName), {},
                                            KStandardGuiItem::overwrite(), KStandardGuiItem::cancel()) != KMessageBox::PrimaryAction) {
            return;
        }
    }
    KDDockWidgets::LayoutSaver dockLayout(KDDockWidgets::RestoreOption_RelativeToMainWindow);
    if (pCore->isVertical()) {
        layout.verticalPath = fileName;
    } else {
        layout.path = fileName;
    }
    const QByteArray layoutData = dockLayout.serializeLayout();
    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(layoutData, &jsonError);
    QJsonArray info;
    QJsonObject kdenliveData;
    kdenliveData.insert(QLatin1String("displayName"), QJsonValue(saveName));
    kdenliveData.insert(QLatin1String("layoutVersion"), QJsonValue(1));
    kdenliveData.insert(QLatin1String("vertical"), QJsonValue(pCore->isVertical()));
    info.push_back(kdenliveData);
    bool ok;
    if (doc.isObject()) {
        QJsonObject updatedObject = doc.object();
        qDebug() << ":::: JSON INSERTED INFO: " << info.toVariantList();
        updatedObject.insert(QStringLiteral("kdenliveInfo"), info);
        QJsonDocument updatedDoc(updatedObject);
        QFile jsonFile(fileName);
        ok = jsonFile.open(QFile::WriteOnly);
        if (ok) {
            ok = jsonFile.write(updatedDoc.toJson());
            jsonFile.close();
        }
    } else {
        ok = false;
        qDebug() << ":::: JSON IS NOT AN OBJECT. IS ARRAY: " << doc.isArray();
    }
    if (!ok) {
        KMessageBox::error(QApplication::activeWindow(), i18n("Could not save layout to file: %1", fileName));
    }

    // Update UI if saved successfully
    m_currentLayoutId = layout.internalId;
    if (m_layoutCollection.hasLayout(layout.internalId)) {
        LayoutInfo currentLayout = m_layoutCollection.getLayout(layout.internalId);
        if (pCore->isVertical()) {
            currentLayout.verticalPath = fileName;
        } else {
            currentLayout.path = fileName;
        }
        m_layoutCollection.addLayout(currentLayout);
    } else {
        m_layoutCollection.addLayout(layout);
    }
    m_layoutSwitcher->setCurrentLayout(m_currentLayoutId);
    initializeLayouts();
}

void LayoutManagement::slotManageLayouts()
{
    QString currentLayoutId = m_layoutSwitcher->currentLayout();

    // Use the new dialog
    LayoutManagerDialog dlg(pCore->window(), m_layoutCollection, currentLayoutId);
    dlg.exec();
    dlg.updateOrder();
    initializeLayouts();
}

void LayoutManagement::slotUpdatePalette()
{
    // Les onglets de modules suivent la palette par la feuille de style ; il ne reste que le témoin.
    updateAutosaveIcon();
    initializeLayouts();
}

void LayoutManagement::updateAutosaveIcon()
{
    MainWindow *main = pCore->window();
    qreal dpr = main->devicePixelRatioF();
    int iconSize = main->style()->pixelMetric(QStyle::PM_SmallIconSize);

    QSize iconPxSize = QSize(iconSize, iconSize) * dpr;
    QIcon icon = QIcon::fromTheme(QStringLiteral("document-save"));
    QPixmap iconPixmap = icon.pixmap(iconPxSize);
    iconPixmap.setDevicePixelRatio(dpr);

    m_autosaveLabel->setPixmap(iconPixmap);
}
