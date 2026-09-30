/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracut/koracutmodules.h"

#include <KLocalizedString>

namespace KoraCutModules {

const QList<Module> &all()
{
    // Les quatre premiers ont un vrai contenu (les docks de Kdenlive suffisent).
    // Les trois derniers attendent leur chantier : ils affichent un panneau « Module en construction ».
    static const QList<Module> list = {
        {QStringLiteral("montage"), i18nc("@title nom du module de montage vidéo", "Editing"), QStringLiteral(":/koracut/module-montage.svg"), 2, true, 0},
        {QStringLiteral("etalonnage"), i18nc("@title nom du module de correction colorimétrique", "Color"),
         QStringLiteral(":/koracut/module-etalonnage.svg"), 3, true, 6},
        {QStringLiteral("audio"), i18nc("@title nom du module audio", "Audio"), QStringLiteral(":/koracut/module-audio.svg"), 4, true, 5},
        {QStringLiteral("vfx"), i18nc("@title nom du module effets visuels", "VFX"), QStringLiteral(":/koracut/module-vfx.svg"), 5, true, 7},
        {QStringLiteral("photo"), i18nc("@title nom du module de retouche photo", "Photo"), QStringLiteral(":/koracut/module-photo.svg"), 6, false, 8},
        {QStringLiteral("illustration"), i18nc("@title nom du module de dessin vectoriel", "Illustration"),
         QStringLiteral(":/koracut/module-illustration.svg"), 7, false, 4},
        {QStringLiteral("export"), i18nc("@title nom du module d'export", "Export"), QStringLiteral(":/koracut/module-export.svg"), 8, true, 9},
    };
    return list;
}

const Module *byId(const QString &id)
{
    const QList<Module> &list = all();
    for (const Module &m : list) {
        if (m.id == id) {
            return &m;
        }
    }
    return nullptr;
}

bool isUnderConstruction(const QString &id)
{
    const Module *m = byId(id);
    return m && !m->ready;
}

} // namespace KoraCutModules
