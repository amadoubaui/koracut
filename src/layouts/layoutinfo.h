/*
SPDX-FileCopyrightText: 2025 Kdenlive contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QString>
#include <QStringList>

/**
 * @brief Represents a single layout configuration. Used for presenting the layout in the switcher and in the layout manager dialog.
 */
struct LayoutInfo
{
    QString internalId;   // Internal ID like "kdenlive_editing"
    QString displayName;  // User specified name or for default layouts this we be the translated name like "Editing"
    QString path;         // The path to the json layout
    QString verticalPath; // The path to the json vertical layout
    // KoraCut, 23 septembre 2026 : les variantes pour un écran de portable (timeline sur toute
    // la largeur, mixeur dans la rangée du haut). Fichiers <id>_compact.json et
    // <id>_vertical_compact.json, bloc kdenliveInfo « compact »: true.
    QString compactPath;
    QString verticalCompactPath;
    bool isDefault{false}; // Whether this is a default layout
    // int sortOrder;       // Custom order for display

    /**
     * @brief Constructor for a layout
     * @param id Internal ID of the layout
     * @param name Display name of the layout (translated)
     * @param layoutData Serialized layout data
     * @param defaultLayout Whether this is a default layout
     */
    LayoutInfo(const QString &id = QString(), const QString &name = QString())
        : internalId(id)
        , displayName(name)
    {
    }

    /**
     * @brief Check if the layout has valid data
     */
    bool isValid() const { return !internalId.isEmpty() && (!path.isEmpty() || !verticalPath.isEmpty()); }
    bool hasHorizontalData() const { return !path.isEmpty(); }
    bool hasVerticalData() const { return !verticalPath.isEmpty(); }
    bool hasCompactData() const { return !compactPath.isEmpty() || !verticalCompactPath.isEmpty(); }
    /** @brief Le fichier à charger pour cette orientation et cet écran, avec repli sur ce qui existe. */
    QString pathFor(bool vertical, bool compact) const
    {
        // Le petit écran passe avant l'orientation : une timeline sur toute la largeur compte
        // plus qu'un moniteur taillé pour le format.
        const QStringList candidates = vertical ? QStringList{compact ? verticalCompactPath : QString(), compact ? compactPath : QString(), verticalPath, path}
                                                : QStringList{compact ? compactPath : QString(), path, compact ? verticalCompactPath : QString(), verticalPath};
        for (const QString &c : candidates) {
            if (!c.isEmpty()) {
                return c;
            }
        }
        return QString();
    }
};
