/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutporte.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

/** @brief Le panneau Console (dock `console`) : ce que la porte reçoit, en direct.
 *
 * Document 18, § « Le terminal dans l'application ». Trois parties : le journal (heure, client,
 * commande, résultat, durée ; un clic remet la tête de lecture sur le temps concerné), les demandes
 * d'accord (Accepter, Refuser, faire confiance à ce client pour la session), et le champ du bas pour
 * taper une commande à la main. Le bouton « Ouvrir un terminal ici » lance le terminal du système
 * dans le dossier du projet, la porte connue, Claude Code lancé s'il est là.
 */
class KoraCutConsolePanel : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutConsolePanel(KoraCutPorte *porte, QWidget *parent = nullptr);

    void focusInput();
    /** @brief Répond à la demande d'accord la plus ancienne encore ouverte (essais sans souris). */
    void answerPendingConfirmation(bool accepted, bool trust = false);
    /** @brief Envoie la ligne comme si elle avait été tapée dans le champ. */
    void runLine(const QString &line);
    /** @brief Le bouton du terminal, comme un clic : le terminal intégré (panneau Terminal) quand il est
     *  branché, sinon celui du système. Rend false si rien ne s'est ouvert. */
    bool openTerminal();
    /** @brief Le terminal du système, dans le dossier du projet (l'option d'avant le 25 septembre 2026). */
    bool openSystemTerminal();
    int journalCount() const;

Q_SIGNALS:
    /** @brief Un clic sur une ligne du journal qui porte un temps. */
    void seekRequested(int position);
    /** @brief « Ouvrir un terminal ici » : le panneau Terminal s'ouvre (document 18, 25 septembre 2026). */
    void integratedTerminalRequested();
    /** @brief « Dans le terminal du système » : le panneau Terminal le fait, avec l'agent choisi. */
    void systemTerminalRequested();

private:
    void appendJournal(const KoraCutPorte::JournalEntry &entry);
    void addConfirmation(const QString &demandeId, const QString &client, const QString &method, const QJsonObject &params, const QString &what);
    void removeConfirmation(const QString &demandeId);
    void refreshStatus();
    void submit();

    KoraCutPorte *m_porte{nullptr};
    QLabel *m_status{nullptr};
    QPushButton *m_terminal{nullptr};
    QPushButton *m_systemTerminal{nullptr};
    QWidget *m_confirmations{nullptr};
    QScrollArea *m_confirmationsScroll{nullptr};
    QVBoxLayout *m_confirmationsLayout{nullptr};
    QTreeWidget *m_journal{nullptr};
    QLineEdit *m_input{nullptr};
    QPushButton *m_send{nullptr};
    QStringList m_history;
    int m_historyIndex{-1};
};
