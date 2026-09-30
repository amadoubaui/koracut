/*
    SPDX-FileCopyrightText: 2017 Nicolas Carion
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "docundostack.hpp"
#include <QUndoCommand>
#include <QUndoGroup>

DocUndoStack::DocUndoStack(QUndoGroup *parent)
    : QUndoStack(parent)
{
}

// TODO: custom undostack everywhere do that
void DocUndoStack::push(QUndoCommand *cmd)
{
    if (m_collecting) {
        // Comme QUndoStack::push : la commande s'exécute (une FunctionalUndoCommand ignore ce
        // premier redo, l'action est déjà faite), mais elle attend la fin du tour d'agent.
        cmd->redo();
        m_collected.push_back(cmd);
        return;
    }
    if (index() < count()) {
        Q_EMIT invalidate(index());
    }
    QUndoStack::push(cmd);
}

void DocUndoStack::beginCollecting()
{
    m_collecting = true;
}

std::vector<QUndoCommand *> DocUndoStack::endCollecting()
{
    m_collecting = false;
    std::vector<QUndoCommand *> collected;
    collected.swap(m_collected);
    return collected;
}
