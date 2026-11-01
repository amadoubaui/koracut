/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include "koracut/motion/koracutmotionmirror.h"

#include <QStringList>

/** @brief Relance les rendus Motion en échec, dans toutes les compositions du projet ouvert.
 *
 * Le miroir garde un élément en échec tant qu'on ne lui redemande pas son rendu : après l'installation
 * de HyperFrames sur une machine neuve (1er novembre 2026, Mac mini, montage d’une campagne), les
 * 15 compositions restaient « INVALID » jusqu'à la réouverture du projet. Appelé à la fin d'une
 * installation réussie, depuis le panneau Motion comme depuis la porte.
 * @return le nombre de rendus redemandés. */
inline int koraCutRetryFailedMotionRenders(KoraCutMotionMirror *mirror)
{
    if (!mirror) {
        return 0;
    }
    int count = 0;
    const QStringList projects = mirror->projects();
    for (const QString &project : projects) {
        QStringList ids;
        const auto elements = mirror->elements(project);
        for (const auto &e : elements) {
            if (e.failed) {
                ids << e.id;
            }
        }
        if (!ids.isEmpty()) {
            count += mirror->renderElements(project, ids, false);
        }
    }
    return count;
}
