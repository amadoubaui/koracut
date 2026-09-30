/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cliploadrecovery.h"

#include <QFileInfo>

namespace ClipLoadRecovery {

bool sourceIsThere(const QString &path)
{
    if (path.isEmpty() || path == QLatin1String("<tractor>")) {
        return false;
    }
    const QFileInfo info(path);
    return info.exists() && info.isFile();
}

Decision decide(const Facts &facts)
{
    // On ferme, ou la tâche ne portait pas de jugement sur le média.
    if (facts.managerBlocked || facts.softDelete || facts.thumbOnly) {
        return Decision::Ignore;
    }
    // Une tâche plus récente est en route ou a déjà réussi : c'est elle qui décide.
    if (facts.otherLoadJobPending || facts.clipAlreadyReady) {
        return Decision::Ignore;
    }
    // Le fichier est là et le média ne s'est pas refusé : l'échec vient de l'interruption.
    if (!facts.loadFailed && facts.reloading && facts.sourceOnDisk && facts.retriesDone < facts.maxRetries) {
        return Decision::Retry;
    }
    // L'utilisateur vient d'ajouter un média que rien n'a réussi à ouvrir.
    if (!facts.canceled && !facts.reloading) {
        return Decision::Delete;
    }
    return Decision::MarkMissing;
}

} // namespace ClipLoadRecovery
