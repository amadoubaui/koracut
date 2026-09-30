/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// Les palettes du module Étalonnage (document 22), comme la barre des palettes de la page Color de
// DaVinci Resolve : Primaires (roues, barres, log), Courbes (Custom et courbes de teinte),
// Qualificateur, Fenêtres, Flou, Clé, Espace couleur (Color Space Transform et LUT). Chaque palette
// règle le nœud courant du graphe par KoraCutGradeController.

#include <QJsonObject>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QStackedWidget;

class KoraCutColorPalettes : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutColorPalettes(QWidget *parent = nullptr);
    /** @brief Affiche une palette : primaires, courbes, qualificateur, fenetres, flou, cle, espace. */
    void showPalette(const QString &id);
    QString currentPalette() const;
    /** @brief Mode des primaires : roues, barres, log (Option-Z bascule roues / log). */
    void setPrimariesMode(const QString &mode);
    void togglePrimariesLog();

private:
    void updateTitle();
    QStackedWidget *m_stack = nullptr;
    QButtonGroup *m_buttons = nullptr;
    QLabel *m_title = nullptr;
    QStringList m_ids;
    class KoraCutPrimariesPanel *m_primaries = nullptr;
};
