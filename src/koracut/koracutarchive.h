/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

class QDomDocument;

/** @brief Ce qui ne doit pas partir avec un projet archivé ou partagé. */
namespace KoraCutArchive {

/** @brief Retire le rendu d'aperçu (pistes « timeline_preview » et leurs morceaux, qui pointent vers le
 * cache de cette machine) et vide les listes de morceaux rendus. KoraCut refait l'aperçu à l'ouverture.
 * @return le nombre de pistes d'aperçu retirées. */
int stripPreview(QDomDocument &doc);

} // namespace KoraCutArchive
