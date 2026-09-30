/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// Les actions et raccourcis du module Étalonnage (document 22). Comme dans DaVinci Resolve, où
// chaque page a ses raccourcis : ⌘D, ⇧S et ⌘Home servent au Montage (dupliquer, sous-titre, début)
// et à l'étalonnage (couper un nœud, nœud en série avant, remettre le grade à zéro). Quand le module
// Étalonnage est affiché, ces trois touches passent à l'étalonnage ; en le quittant, elles reviennent.

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>
#include <functional>

class KXmlGuiWindow;
class KoraCutNodeEditor;
class KoraCutColorPalettes;
class QAction;

class KoraCutColorPage : public QObject
{
    Q_OBJECT
public:
    KoraCutColorPage(KXmlGuiWindow *window, KoraCutNodeEditor *nodes, KoraCutColorPalettes *palettes);
    void setActive(bool active);
    bool isActive() const { return m_active; }
    /** @brief Les pseudo-actions de capture « etal:… » (KORACUT_SHOT_ACTIONS), pour vérifier sans souris :
     * graphe-fichier=<json>, noeud=serie|avant|parallele|calque|outside|separateur|fin, palette=<id>,
     * primaires=roues|barres|log, split, still, plan=<rang>, etat (ligne KORACUT-ETAL). */
    static void shotAction(const QString &command);
    /** @brief Exporte le grade du plan courant en LUT .cube (« Generate LUT » de Resolve). */
    void exportLut();

private:
    QAction *add(const QString &name, const QString &text, const QKeySequence &key, const std::function<void()> &fn, bool shared = false);
    QPointer<KXmlGuiWindow> m_window;
    QPointer<KoraCutNodeEditor> m_nodes;
    QPointer<KoraCutColorPalettes> m_palettes;
    bool m_active = false;
    QList<QAction *> m_actions;
    // Raccourcis partagés avec le Montage : l'action de l'étalonnage, sa touche, et l'action du Montage.
    struct Shared
    {
        QPointer<QAction> color;
        QKeySequence key;
        QString editingName;
        QList<QKeySequence> editingKeys;
    };
    QList<Shared> m_shared;
};
