/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutworkspace.h"
#include "core.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSet>
#include <QWidget>

#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/MainWindow.h>

namespace KoraCutWorkspace {

namespace {
// Un écran dont la largeur fait au moins deux fois la hauteur est un moniteur très large (21:9,
// 32:9) : là seulement, la timeline partage le bas avec le mixeur.
constexpr double kUltraWideRatio = 2.0;

KDDockWidgets::Core::DockWidget *dock(const QString &name)
{
    return KDDockWidgets::DockRegistry::self()->dockByName(name);
}
} // namespace

QString screenSetting()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("ecranPlanDeTravail", QStringLiteral("auto"));
}

void setScreenSetting(const QString &value)
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
    group.writeEntry("ecranPlanDeTravail", value);
    group.sync();
}

// À incrémenter quand les fichiers data/layouts/montage*.json changent de forme (2 : 23 septembre 2026,
// timeline sur toute la largeur et variantes compactes ; 5 : 23 septembre 2026, onglet Motion dans le bloc Médias).
constexpr int kLayoutGeneration = 14; // 14 : 28 septembre 2026, le module Export comme la page Deliver ; 13 : 28 septembre 2026, bande des plans à la hauteur de ses vignettes (12 : l'Étalonnage comme la page Color)

bool savedLayoutOutdated()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("dispositionGeneration", 0) < kLayoutGeneration;
}

void markSavedLayoutCurrent()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
    group.writeEntry("dispositionGeneration", kLayoutGeneration);
    group.sync();
}

bool wantsCompact(const QWidget *window)
{
    // Demande d'Amadou Ba du 23 septembre 2026, 21 h 25 (sur son moniteur 24 pouces 16:9) : la
    // timeline seule en bas est la règle, sur un portable comme sur un moniteur ordinaire. Seul un
    // moniteur très large (21:9 et plus) met le mixeur à côté de la timeline.
    const QString setting = screenSetting();
    if (setting == QLatin1String("compact")) {
        return true;
    }
    if (setting == QLatin1String("large")) {
        return false;
    }
    const QScreen *screen = window ? window->screen() : nullptr;
    if (!screen) {
        return true;
    }
    const QRect g = screen->availableGeometry();
    return g.height() <= 0 || (double(g.width()) / g.height()) < kUltraWideRatio;
}

// Les trois blocs du haut de chaque module : l'hôte de chaque bloc et ses onglets. Le bloc du milieu
// (horizontal) ou de droite (vertical) est toujours la vidéo. Le bloc de droite porte tout ce qui
// règle et pilote (Amadou Ba, 24 septembre 2026 : « l'agent, le codage, le terminal à droite »).
struct ModuleBlocks
{
    QString left;
    QStringList leftTabs;
    QString right;
    QStringList rightTabs;
};

ModuleBlocks blocksFor(const QString &module)
{
    const QStringList pilot = {QStringLiteral("script"), QStringLiteral("agents"), QStringLiteral("motion"), QStringLiteral("console")};
    if (module == QLatin1String("etalonnage")) {
        // Les scopes à gauche, l'image au milieu, les réglages de couleur à droite.
        return {QStringLiteral("vectorscope"),
                {QStringLiteral("waveform"), QStringLiteral("rgb_parade"), QStringLiteral("histogram")},
                QStringLiteral("effect_stack"),
                QStringList{QStringLiteral("effect_list"), QStringLiteral("project_bin"), QStringLiteral("clip_properties")} + pilot};
    }
    if (module == QLatin1String("audio")) {
        // Le mixeur a sa place ici (et pas en Montage) : à droite, avec le spectre et les effets.
        return {QStringLiteral("project_bin"),
                {QStringLiteral("effect_list"), QStringLiteral("clip_properties"), QStringLiteral("undo_history")},
                QStringLiteral("mixer"),
                QStringList{QStringLiteral("effect_stack"), QStringLiteral("audiospectrum")} + pilot};
    }
    if (module == QLatin1String("vfx")) {
        return {QStringLiteral("effect_list"),
                {QStringLiteral("transition_list"), QStringLiteral("project_bin"), QStringLiteral("clip_properties")},
                QStringLiteral("effect_stack"),
                QStringList{QStringLiteral("timeremap"), QStringLiteral("subtitles")} + pilot};
    }
    // Montage.
    return {QStringLiteral("project_bin"),
            {QStringLiteral("transition_list"), QStringLiteral("effect_list"), QStringLiteral("clip_properties"), QStringLiteral("undo_history")},
            QStringLiteral("effect_stack"),
            QStringList{QStringLiteral("timeremap"), QStringLiteral("subtitles")} + pilot};
}

void arrangeModule(MainWindow *window, const QString &module, bool vertical)
{
    // La disposition par défaut de chaque module, d'après la maquette d'Amadou Ba du 24 septembre
    // 2026 (Figma, « Capture d'écran 2026-09-24 à 11.10.57 » dans la boîte aux lettres), la même sur
    // un portable et sur un moniteur :
    // - projet horizontal : trois blocs égaux en haut (gauche, la vidéo, droite), la timeline seule
    //   en bas sur toute la largeur ;
    // - projet vertical : la vidéo à droite sur toute la hauteur (un tiers de la largeur) ; à gauche,
    //   deux blocs égaux en haut et la timeline en dessous, collée à la vidéo.
    using namespace KDDockWidgets;
    namespace KCore = KDDockWidgets::Core;
    KCore::MainWindow *main = DockRegistry::self()->mainWindowByName(QStringLiteral("KdenliveKDDock"));
    const ModuleBlocks blocks = blocksFor(module);
    KCore::DockWidget *timeline = dock(QStringLiteral("timeline"));
    KCore::DockWidget *left = dock(blocks.left);
    KCore::DockWidget *right = dock(blocks.right);
    KCore::DockWidget *source = dock(QStringLiteral("clipmonitor"));
    KCore::DockWidget *program = dock(QStringLiteral("projectmonitor"));
    if (!main || !timeline || !left || !right || !source || !program) {
        return;
    }
    // On repart d'une page blanche : tout se ferme, puis chaque bloc reprend sa place.
    const auto docks = DockRegistry::self()->dockwidgets();
    for (KCore::DockWidget *d : docks) {
        d->forceClose();
    }
    const QSize area = window->size();
    const int width = area.width();
    const int height = area.height();
    // Un moniteur à deux onglets (le réglage par défaut) ou deux moniteurs côte à côte : on
    // enregistre ce que la personne voit, slotLoadLayout() réapplique son réglage ensuite.
    const bool single = KdenliveSettings::singleMonitor();
    const auto tab = [](KCore::DockWidget *host, const QStringList &names) {
        for (const QString &name : names) {
            if (KCore::DockWidget *d = dock(name)) {
                host->addDockWidgetAsTab(d);
            }
        }
        host->setAsCurrentTab();
    };
    const auto monitors = [&](KCore::DockWidget *relativeTo, Location where, int w, int h) {
        // Poser un panneau « à côté de » un autre prend la place sur cet autre : le dernier posé
        // se retrouve le plus près. Programme d'abord (deux moniteurs), Source ensuite.
        if (!single) {
            main->addDockWidget(program, where, relativeTo, InitialOption(QSize(w / 2, h)));
        }
        main->addDockWidget(source, where, relativeTo, InitialOption(QSize(single ? w : w / 2, h)));
        if (single) {
            tab(source, {QStringLiteral("projectmonitor"), QStringLiteral("library"), QStringLiteral("textedit"), QStringLiteral("notes_widget")});
        } else {
            tab(source, {QStringLiteral("library")});
            tab(program, {QStringLiteral("textedit"), QStringLiteral("notes_widget")});
        }
        // C'est l'image du programme qu'on regarde en montant comme en étalonnant : Programme devant,
        // Source en onglet (sinon une séquence verticale s'ouvrait sur « Aucun média ouvert »).
        program->setAsCurrentTab();
    };
    const int third = width / 3;
    const int top = int(height * 0.52);
    const int bottom = height - top;

    if (module == QLatin1String("etalonnage")) {
        // La page Color de DaVinci Resolve (document 22) : en haut la galerie, la visionneuse au centre et
        // les nœuds à droite ; au milieu la bande des plans ; en bas les palettes et, à droite, les scopes
        // (la parade devant, comme dans Resolve). La timeline de montage n'y est pas.
        KCore::DockWidget *gallery = dock(QStringLiteral("etal_galerie"));
        KCore::DockWidget *nodes = dock(QStringLiteral("etal_noeuds"));
        KCore::DockWidget *strip = dock(QStringLiteral("etal_plans"));
        KCore::DockWidget *palettes = dock(QStringLiteral("etal_palettes"));
        KCore::DockWidget *parade = dock(QStringLiteral("rgb_parade"));
        if (gallery && nodes && strip && palettes && parade) {
            const int topH = int(height * 0.50);
            const int stripH = 130;
            const int bottomH = height - topH - stripH;
            main->addDockWidget(gallery, Location_OnTop, nullptr, InitialOption(QSize(int(width * 0.2), topH)));
            main->addDockWidget(nodes, Location_OnRight, gallery, InitialOption(QSize(int(width * 0.3), topH)));
            monitors(gallery, Location_OnRight, int(width * 0.5), topH);
            main->addDockWidget(strip, Location_OnBottom, nullptr, InitialOption(QSize(width, stripH)));
            main->addDockWidget(palettes, Location_OnBottom, nullptr, InitialOption(QSize(int(width * 0.64), bottomH)));
            main->addDockWidget(parade, Location_OnRight, palettes, InitialOption(QSize(int(width * 0.36), bottomH)));
            tab(parade, {QStringLiteral("waveform"), QStringLiteral("vectorscope"), QStringLiteral("histogram")});
            tab(palettes, {QStringLiteral("effect_stack"), QStringLiteral("agents"), QStringLiteral("console")});
            tab(nodes, {QStringLiteral("project_bin"), QStringLiteral("effect_list")});
            parade->setAsCurrentTab();
            palettes->setAsCurrentTab();
            nodes->setAsCurrentTab();
            return;
        }
    }

    if (module == QLatin1String("export")) {
        // La page Deliver de DaVinci Resolve (document 23) : les modèles en haut sur toute la largeur, puis
        // les paramètres à gauche, la vidéo au milieu, la file de rendu à droite, la timeline en bas.
        KCore::DockWidget *tiles = dock(QStringLiteral("export_modeles"));
        KCore::DockWidget *settings = dock(QStringLiteral("export_reglages"));
        KCore::DockWidget *queue = dock(QStringLiteral("export_file"));
        if (tiles && settings && queue) {
            const int tilesH = 112;
            const int bottomH = int(height * 0.28);
            const int midH = height - tilesH - bottomH;
            main->addDockWidget(tiles, Location_OnTop, nullptr, InitialOption(QSize(width, tilesH)));
            main->addDockWidget(timeline, Location_OnBottom, nullptr, InitialOption(QSize(width, bottomH)));
            main->addDockWidget(settings, Location_OnBottom, tiles, InitialOption(QSize(int(width * 0.3), midH)));
            main->addDockWidget(queue, Location_OnRight, settings, InitialOption(QSize(int(width * 0.26), midH)));
            monitors(settings, Location_OnRight, int(width * 0.44), midH);
            tab(queue, {QStringLiteral("agents"), QStringLiteral("console")});
            queue->setAsCurrentTab();
            return;
        }
    }

    if (vertical) {
        main->addDockWidget(timeline, Location_OnBottom, nullptr, InitialOption(QSize(width - third, bottom)));
        monitors(nullptr, Location_OnRight, third, height);
        main->addDockWidget(left, Location_OnTop, timeline, InitialOption(QSize((width - third) / 2, top)));
        main->addDockWidget(right, Location_OnRight, left, InitialOption(QSize((width - third) / 2, top)));
    } else {
        main->addDockWidget(timeline, Location_OnBottom, nullptr, InitialOption(QSize(width, bottom)));
        main->addDockWidget(left, Location_OnTop, nullptr, InitialOption(QSize(width, top)));
        // Posé à droite de « gauche », puis la vidéo entre les deux (le dernier posé est le plus près).
        main->addDockWidget(right, Location_OnRight, left, InitialOption(QSize(third, top)));
        monitors(left, Location_OnRight, third, top);
    }
    tab(right, blocks.rightTabs);
    tab(left, blocks.leftTabs);
}

void arrangeEditing(MainWindow *window, bool compact, bool vertical)
{
    // Depuis la maquette du 24 septembre 2026, le portable et le moniteur ont la même disposition.
    Q_UNUSED(compact)
    arrangeModule(window, QStringLiteral("montage"), vertical);
}

void closeDocksAbsentFrom(const QByteArray &layoutJson)
{
    const QJsonObject root = QJsonDocument::fromJson(layoutJson).object();
    QSet<QString> known;
    for (const QJsonValue &v : root.value(QStringLiteral("allDockWidgets")).toArray()) {
        known.insert(v.toObject().value(QStringLiteral("uniqueName")).toString());
    }
    if (known.isEmpty()) {
        return;
    }
    // Les panneaux ajoutés après coup (absents des dispositions plus anciennes) sont rangés par
    // dockStrayPanels() après le chargement : on les laisse.
    static const QSet<QString> rangesApres {QStringLiteral("motion"), QStringLiteral("agents"), QStringLiteral("console"), QStringLiteral("script"),
                                            QStringLiteral("diagnostic")};
    const auto docks = KDDockWidgets::DockRegistry::self()->dockwidgets();
    for (KDDockWidgets::Core::DockWidget *d : docks) {
        if (d && d->isOpen() && !known.contains(d->uniqueName()) && !rangesApres.contains(d->uniqueName())) {
            d->forceClose();
        }
    }
}

void dockStrayPanels()
{
    // Les panneaux ajoutés par KoraCut (Motion, Agents, Console) n'existent pas dans les dispositions
    // plus anciennes (Étalonnage, Audio, VFX, ou un état enregistré avant eux) : KDDockWidgets les
    // ouvrait alors en fenêtres flottantes, sans barre de titre quand les barres sont masquées, donc
    // impossibles à fermer ou à déplacer (trois fenêtres collées au lancement, 24 septembre 2026).
    // Après chaque chargement, on les range en onglets du bloc de droite (l'inspecteur), ou on les ferme.
    namespace KCore = KDDockWidgets::Core;
    KCore::DockWidget *host = dock(QStringLiteral("effect_stack"));
    if (!host || !host->isOpen() || host->isFloating()) {
        host = dock(QStringLiteral("mixer"));
    }
    const bool hostUsable = host && host->isOpen() && !host->isFloating();
    bool moved = false;
    // Les panneaux de l'Étalonnage et de l'Export n'appartiennent qu'à leur disposition : ailleurs, flottants, on les ferme.
    for (const QString &name : {QStringLiteral("etal_noeuds"), QStringLiteral("etal_palettes"), QStringLiteral("etal_plans"), QStringLiteral("etal_galerie"),
                                QStringLiteral("export_modeles"), QStringLiteral("export_reglages"), QStringLiteral("export_file")}) {
        KCore::DockWidget *d = dock(name);
        if (d && d->isOpen() && d->isFloating()) {
            d->forceClose();
        }
    }
    for (const QString &name :
         {QStringLiteral("motion"), QStringLiteral("agents"), QStringLiteral("console"), QStringLiteral("script"), QStringLiteral("diagnostic")}) {
        KCore::DockWidget *d = dock(name);
        if (!d || !d->isOpen() || !d->isFloating()) {
            continue;
        }
        if (hostUsable) {
            host->addDockWidgetAsTab(d);
            moved = true;
        } else {
            d->forceClose();
        }
    }
    // Seulement si on a rangé quelque chose : sinon l'onglet choisi par la disposition (le Mixeur en
    // Audio) doit rester devant.
    if (moved) {
        host->setAsCurrentTab();
    }
}

bool writeLayout(const QString &path, const QString &id, const QString &displayName, bool vertical, bool compact)
{
    KDDockWidgets::LayoutSaver saver(KDDockWidgets::RestoreOption_RelativeToMainWindow);
    QJsonDocument doc = QJsonDocument::fromJson(saver.serializeLayout());
    if (!doc.isObject()) {
        return false;
    }
    QJsonObject info;
    info.insert(QStringLiteral("layoutVersion"), 1);
    info.insert(QStringLiteral("displayName"), displayName);
    info.insert(QStringLiteral("default"), true);
    info.insert(QStringLiteral("vertical"), vertical);
    info.insert(QStringLiteral("compact"), compact);
    info.insert(QStringLiteral("id"), id);
    QJsonObject root = doc.object();
    root.insert(QStringLiteral("kdenliveInfo"), QJsonArray{info});
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    return true;
}

} // namespace KoraCutWorkspace
