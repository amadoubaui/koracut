/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutcolorpage.h"
#include "koracutgradecontroller.h"
#include "koracutnodeeditor.h"
#include "koracutpalettes.h"

#include "core.h"
#include "mainwindow.h"

#include "kcgrade.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KXmlGuiWindow>
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QStandardPaths>

KoraCutColorPage::KoraCutColorPage(KXmlGuiWindow *window, KoraCutNodeEditor *nodes, KoraCutColorPalettes *palettes)
    : QObject(window)
    , m_window(window)
    , m_nodes(nodes)
    , m_palettes(palettes)
{
    auto *c = KoraCutGradeController::self();
    add(QStringLiteral("koracut_color_serial"), i18nc("@action", "Add Serial Node"), QKeySequence(Qt::ALT | Qt::Key_S), [this]() { m_nodes->addSerial(); });
    add(QStringLiteral("koracut_color_serial_before"), i18nc("@action", "Add Serial Node Before"), QKeySequence(Qt::SHIFT | Qt::Key_S), [this]() { m_nodes->addSerialBefore(); }, true);
    add(QStringLiteral("koracut_color_append"), i18nc("@action", "Append Node"), QKeySequence(Qt::ALT | Qt::Key_K), [this]() { m_nodes->appendNode(); });
    add(QStringLiteral("koracut_color_parallel"), i18nc("@action", "Add Parallel Node"), QKeySequence(Qt::ALT | Qt::Key_P), [this]() { m_nodes->addParallel(); });
    add(QStringLiteral("koracut_color_layer"), i18nc("@action", "Add Layer Node"), QKeySequence(Qt::ALT | Qt::Key_L), [this]() { m_nodes->addLayer(); });
    add(QStringLiteral("koracut_color_outside"), i18nc("@action", "Add Outside Node"), QKeySequence(Qt::ALT | Qt::Key_O), [this]() { m_nodes->addOutside(); });
    add(QStringLiteral("koracut_color_splitter"), i18nc("@action", "Add Splitter/Combiner Nodes"), QKeySequence(Qt::ALT | Qt::Key_Y), [this]() { m_nodes->addSplitterCombiner(); });
    add(QStringLiteral("koracut_color_toggle_node"), i18nc("@action", "Enable or Disable Node"), QKeySequence(Qt::CTRL | Qt::Key_D), [this]() { m_nodes->toggleCurrentNode(); }, true);
    add(QStringLiteral("koracut_color_bypass"), i18nc("@action", "Bypass Grade"), QKeySequence(Qt::SHIFT | Qt::Key_D), [c]() { c->toggleBypass(); });
    add(QStringLiteral("koracut_color_toggle_all"), i18nc("@action", "Enable or Disable All Nodes"), QKeySequence(Qt::ALT | Qt::Key_D), [c]() { c->toggleBypass(); });
    add(QStringLiteral("koracut_color_reset_node"), i18nc("@action", "Reset Node Grade"), QKeySequence(Qt::SHIFT | Qt::Key_Home), [this]() { m_nodes->resetCurrentNode(); });
    add(QStringLiteral("koracut_color_reset_grade"), i18nc("@action", "Reset Grade and Nodes"), QKeySequence(Qt::CTRL | Qt::Key_Home), [c]() { c->resetGrade(false); }, true);
    add(QStringLiteral("koracut_color_reset_keep"), i18nc("@action", "Reset Grades and Keep Nodes"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Home), [c]() { c->resetGrade(true); });
    // L'étalonnage par défaut de KoraCut (8 octobre 2026). Sans raccourci : la palette de commandes et la porte le trouvent.
    add(QStringLiteral("koracut_color_signature"), i18nc("@action", "KoraCut Grade (HDR, Vivid, Skin)"), QKeySequence(), [c]() { c->applySignature(c->currentClip()); });
    add(QStringLiteral("koracut_color_signature_all"), i18nc("@action", "KoraCut Grade on All Clips"), QKeySequence(), [c]() { c->applySignature(-1); });
    add(QStringLiteral("koracut_color_grab_still"), i18nc("@action", "Grab Still"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G), [c]() { c->grabStill(); });
    add(QStringLiteral("koracut_color_split"), i18nc("@action", "Before / After Split"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_W), [c]() { c->setCompare(!c->comparing()); });
    add(QStringLiteral("koracut_color_log"), i18nc("@action", "Switch Color Wheels / Log Wheels"), QKeySequence(Qt::ALT | Qt::Key_Z), [this]() { m_palettes->togglePrimariesLog(); });
    add(QStringLiteral("koracut_color_previous_grade"), i18nc("@action", "Apply Grade From Previous Clip"), QKeySequence(Qt::Key_Equal), [c]() {
        const QList<int> clips = c->videoClips();
        const int i = int(clips.indexOf(c->currentClip()));
        if (i > 0) {
            c->copyGrade(clips.at(i - 1), {c->currentClip()});
        }
    });
    add(QStringLiteral("koracut_color_next_clip"), i18nc("@action", "Next Clip to Grade"), QKeySequence(), [c]() {
        const QList<int> clips = c->videoClips();
        const int i = int(clips.indexOf(c->currentClip()));
        if (i + 1 < clips.size()) {
            c->setCurrentClip(clips.at(i + 1), true);
        }
    });
    add(QStringLiteral("koracut_color_previous_clip"), i18nc("@action", "Previous Clip to Grade"), QKeySequence(), [c]() {
        const QList<int> clips = c->videoClips();
        const int i = int(clips.indexOf(c->currentClip()));
        if (i > 0) {
            c->setCurrentClip(clips.at(i - 1), true);
        }
    });
    add(QStringLiteral("koracut_color_export_lut"), i18nc("@action", "Export Grade as LUT…"), QKeySequence(), [this]() { exportLut(); });
    // Une action par palette et par mode des primaires : pour le menu, les raccourcis que chacun se donne,
    // et la porte de commande (actions.declencher), comme les palettes de la page Color.
    const QList<QPair<QString, QString>> pals = {{QStringLiteral("primaires"), i18nc("@action color palette", "Show Primaries Palette")},
                                                 {QStringLiteral("courbes"), i18nc("@action color palette", "Show Curves Palette")},
                                                 {QStringLiteral("qualificateur"), i18nc("@action color palette", "Show Qualifier Palette")},
                                                 {QStringLiteral("fenetres"), i18nc("@action color palette", "Show Windows Palette")},
                                                 {QStringLiteral("flou"), i18nc("@action color palette", "Show Blur Palette")},
                                                 {QStringLiteral("cle"), i18nc("@action color palette", "Show Key Palette")},
                                                 {QStringLiteral("espace"), i18nc("@action color palette", "Show Color Space Palette")}};
    for (const auto &pal : pals) {
        const QString id = pal.first;
        add(QStringLiteral("koracut_color_palette_") + id, pal.second, QKeySequence(), [this, id]() { m_palettes->showPalette(id); });
    }
    const QList<QPair<QString, QString>> modes = {{QStringLiteral("roues"), i18nc("@action primaries mode", "Primaries: Color Wheels")},
                                                  {QStringLiteral("barres"), i18nc("@action primaries mode", "Primaries: Bars")},
                                                  {QStringLiteral("log"), i18nc("@action primaries mode", "Primaries: Log Wheels")}};
    for (const auto &m : modes) {
        const QString id = m.first;
        add(QStringLiteral("koracut_color_primaries_") + id, m.second, QKeySequence(), [this, id]() {
            m_palettes->showPalette(QStringLiteral("primaires"));
            m_palettes->setPrimariesMode(id);
        });
    }
    // Les touches partagées avec le Montage : l'action du Montage qui les porte par défaut.
    const QHash<QString, QString> editing{{QStringLiteral("koracut_color_serial_before"), QStringLiteral("add_subtitle")},
                                          {QStringLiteral("koracut_color_toggle_node"), QStringLiteral("duplicate_timeline_clip")},
                                          {QStringLiteral("koracut_color_reset_grade"), QStringLiteral("seek_start")}};
    for (Shared &s : m_shared) {
        s.editingName = editing.value(s.color->objectName());
    }
    setActive(false);
    // Le module affiché : la barre du haut demande la disposition du module.
    connect(pCore.get(), &Core::loadLayoutById, this, [this](const QString &id) { setActive(id == QLatin1String("etalonnage")); });
}

QAction *KoraCutColorPage::add(const QString &name, const QString &text, const QKeySequence &key, const std::function<void()> &fn, bool shared)
{
    auto *a = new QAction(text, this);
    a->setObjectName(name);
    connect(a, &QAction::triggered, this, [fn]() {
        if (KoraCutGradeController::self()) {
            fn();
        }
    });
    m_window->actionCollection()->addAction(name, a);
    if (shared) {
        m_shared << Shared{a, key, QString(), {}};
    } else if (!key.isEmpty()) {
        m_window->actionCollection()->setDefaultShortcut(a, key);
    }
    m_actions << a;
    return a;
}

void KoraCutColorPage::setActive(bool active)
{
    m_active = active;
    // Lu par la fenêtre : dans ce module, choisir un plan ne fait pas passer l'Inspecteur devant les palettes.
    if (m_window) {
        m_window->setProperty("koracut_etalonnage_actif", active);
    }
    for (QAction *a : std::as_const(m_actions)) {
        a->setEnabled(active);
    }
    for (Shared &s : m_shared) {
        QAction *editingAction = m_window->actionCollection()->action(s.editingName);
        if (active) {
            if (editingAction && !editingAction->shortcuts().isEmpty()) {
                s.editingKeys = editingAction->shortcuts();
                editingAction->setShortcuts({});
            }
            if (s.color) {
                s.color->setShortcut(s.key);
            }
        } else {
            if (s.color) {
                s.color->setShortcut(QKeySequence());
            }
            if (editingAction && !s.editingKeys.isEmpty()) {
                editingAction->setShortcuts(s.editingKeys);
                s.editingKeys.clear();
            }
        }
    }
    if (!active && KoraCutGradeController::self()->comparing()) {
        KoraCutGradeController::self()->setCompare(false);
    }
}

void KoraCutColorPage::exportLut()
{
    auto *c = KoraCutGradeController::self();
    const QString json = QString::fromUtf8(QJsonDocument(c->graph()).toJson(QJsonDocument::Compact));
    std::string error;
    auto grade = kcgrade::Grade::compile(json.toStdString(), &error);
    if (!grade) {
        QMessageBox::warning(m_window, i18nc("@title:window", "Export LUT"), QString::fromStdString(error));
        return;
    }
    const QString file = QFileDialog::getSaveFileName(m_window, i18nc("@title:window", "Export Grade as LUT"),
                                                      QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/grade.cube"),
                                                      i18nc("@item file type", "LUT (*.cube)"));
    if (file.isEmpty()) {
        return;
    }
    if (!grade->exportCube(file.toStdString(), 33, &error)) {
        QMessageBox::warning(m_window, i18nc("@title:window", "Export LUT"), QString::fromStdString(error));
    }
}

void KoraCutColorPage::shotAction(const QString &command)
{
    auto *c = KoraCutGradeController::self();
    const QString key = command.section(QLatin1Char('='), 0, 0);
    const QString value = command.section(QLatin1Char('='), 1);
    QWidget *window = pCore ? pCore->window() : nullptr;
    auto *nodes = window ? window->findChild<KoraCutNodeEditor *>() : nullptr;
    auto *palettes = window ? window->findChild<KoraCutColorPalettes *>() : nullptr;
    if (key == QLatin1String("graphe-fichier")) {
        QFile f(value);
        if (f.open(QIODevice::ReadOnly)) {
            c->edit(QJsonDocument::fromJson(f.readAll()).object(), QStringLiteral("essai"));
        }
    } else if (key == QLatin1String("noeud") && nodes) {
        if (value == QLatin1String("serie")) {
            nodes->addSerial();
        } else if (value == QLatin1String("avant")) {
            nodes->addSerialBefore();
        } else if (value == QLatin1String("parallele")) {
            nodes->addParallel();
        } else if (value == QLatin1String("calque")) {
            nodes->addLayer();
        } else if (value == QLatin1String("outside")) {
            nodes->addOutside();
        } else if (value == QLatin1String("separateur")) {
            nodes->addSplitterCombiner();
        } else if (value == QLatin1String("fin")) {
            nodes->appendNode();
        } else {
            c->setCurrentNode(value.toInt());
        }
    } else if (key == QLatin1String("palette") && palettes) {
        palettes->showPalette(value);
    } else if (key == QLatin1String("primaires") && palettes) {
        palettes->setPrimariesMode(value);
    } else if (key == QLatin1String("split")) {
        c->setCompare(!c->comparing());
    } else if (key == QLatin1String("still")) {
        c->grabStill();
    } else if (key == QLatin1String("plan")) {
        const QList<int> clips = c->videoClips();
        const int i = value.toInt() - 1;
        if (i >= 0 && i < clips.size()) {
            c->setCurrentClip(clips.at(i), true);
        }
    }
    qWarning().noquote() << "KORACUT-ETAL" << c->currentClip() << c->currentNode() << QString::fromUtf8(QJsonDocument(c->graph()).toJson(QJsonDocument::Compact));
}
