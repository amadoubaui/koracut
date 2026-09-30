/*
    SPDX-FileCopyrightText: 2017 Nicolas Carion
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QUndoCommand>
#include <vector>

class QUndoGroup;
class QUndoCommand;

class DocUndoStack : public QUndoStack
{
    Q_OBJECT
public:
    explicit DocUndoStack(QUndoGroup *parent = Q_NULLPTR);
    void push(QUndoCommand *cmd);

    /** @name Collecte (KoraCut, 23 septembre 2026)
     *
     * Un tour d'agent doit tenir dans une seule étape d'annulation, alors que chaque outil pousse
     * la sienne. Entre beginCollecting() et endCollecting(), push() n'inscrit rien dans la pile :
     * il exécute la commande comme d'habitude et la met de côté. Le tour les regroupe ensuite dans
     * une seule commande (KoraCutTurnCommand). Un tour sans action ne laisse rien.
     */
    ///@{
    void beginCollecting();
    /** @brief Rend les commandes mises de côté, dans l'ordre. L'appelant en devient responsable. */
    std::vector<QUndoCommand *> endCollecting();
    bool isCollecting() const { return m_collecting; }
    ///@}

Q_SIGNALS:
    void invalidate(int ix);

private:
    bool m_collecting{false};
    std::vector<QUndoCommand *> m_collected;
};
