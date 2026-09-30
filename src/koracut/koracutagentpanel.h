/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracut/koracutagentsession.h"

#include <QPlainTextEdit>
#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/** @brief Le champ de saisie du panneau : Entrée envoie, Maj+Entrée saute une ligne. */
class KoraCutAgentInput : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit KoraCutAgentInput(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    /** Recalcule la hauteur : le texte, ou le texte d'attente quand le champ est vide. */
    void adjustHeight();

Q_SIGNALS:
    void submitted();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
};

/** @brief Le panneau « Agents » : l'état du service, la conversation, les accords, le plan, la saisie.
 *
 * Charte produit (« 2 - Charte graphique KoraCut.md ») : fond #1E1E1E, surfaces #2C2C2C, accent
 * #0D99FF, police système, coins 6 et 9 px. Les couleurs vivent dans data/koracut.qss, par nom
 * d'objet (koracutAgent…). Bulles de la personne à droite, de l'agent à gauche avec son nom, appels
 * d'outils en lignes monospace discrètes, demandes d'accord en ligne avec deux boutons, plan avec
 * Appliquer / Modifier / Annuler, et « Annuler ce tour » tant que le tour est ouvert.
 */
class KoraCutAgentPanel : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutAgentPanel(KoraCutAgentSession *session, QWidget *parent = nullptr);

    void focusInput();
    /** @brief Pour les essais sans souris : répond à la dernière demande d'accord en attente. */
    void answerPendingConfirmation(bool accepted);
    /** @brief Pour les essais sans souris : décide du dernier plan en attente. */
    void decidePendingPlan(KoraCutAgentSession::PlanDecision decision, const QString &comment = QString());
    /** @brief Une ligne du service local (démarrage, bascule d'une connexion IA à la suivante). */
    void showServiceNote(const QString &text);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class InputMode { Request, ModifyPlan, AnswerQuestion };

    void submit();
    void addBubble(const QString &author, const QString &text, bool person);
    void addToolLine(const QString &tool, const QJsonObject &input, const QJsonObject &result);
    void addNote(const QString &text);
    QWidget *addCard(const QString &title, const QString &body);
    void scrollToBottom();
    void refreshHeader();
    void setInputMode(InputMode mode, const QString &id = QString());
    void limitBubbleWidths();

    KoraCutAgentSession *m_session{nullptr};
    QLabel *m_status{nullptr};
    QPushButton *m_cancelTurn{nullptr};
    QPushButton *m_connectModel{nullptr};
    QScrollArea *m_scroll{nullptr};
    QWidget *m_thread{nullptr};
    QVBoxLayout *m_threadLayout{nullptr};
    KoraCutAgentInput *m_input{nullptr};
    QPushButton *m_send{nullptr};
    QList<QPointer<QLabel>> m_bubbles;
    KoraCutAgents::LinkState m_linkState{KoraCutAgents::LinkState::NoService};
    QString m_linkMessage;
    QString m_serviceStatus;
    QString m_workingAgent;
    InputMode m_mode{InputMode::Request};
    QString m_modeId;
    QStringList m_pendingConfirmations;
    QStringList m_pendingPlans;
    QHash<QString, QPointer<QWidget>> m_cards;
};
