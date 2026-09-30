/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracut/koracutagentsession.h"

#include <QTimer>
#include <QUrl>

class QWebSocket;

/** @brief Le client du service d'agents : une connexion WebSocket, un message JSON par trame.
 *
 * L'adresse vient de la variable d'environnement KORACUT_AGENTS_URL si elle est posée, sinon du
 * réglage `koracut_agents_url` (vide : aucun service, le panneau le dit). Après une coupure, le
 * client se reconnecte avec un délai croissant (1 s, 2 s, 4 s… jusqu'à 30 s). Aucune clé de
 * fournisseur ne passe par ici : c'est le service PIXEWORK qui les garde (document 5).
 */
class KoraCutAgentClient : public KoraCutAgentLink
{
    Q_OBJECT
public:
    explicit KoraCutAgentClient(QObject *parent = nullptr);
    ~KoraCutAgentClient() override;

    /** @brief L'adresse retenue : variable d'environnement, sinon réglage. Vide si aucune. */
    static QUrl configuredUrl();

    /** @brief Ouvre la connexion vers @p url (ou vers configuredUrl() si vide). */
    void start(const QUrl &url = QUrl());
    void stop();
    QUrl url() const { return m_url; }
    /** @brief Jeton de la poignée de main (en-tête X-KoraCut-Jeton), pour le service local lancé par
     *  l'application (KoraCutAiBridge). Vide : aucun en-tête. */
    void setHandshakeToken(const QByteArray &token) { m_token = token; }

    void send(const QJsonObject &message) override;
    KoraCutAgents::LinkState state() const override { return m_state; }
    QString stateMessage() const override { return m_message; }

private:
    void setState(KoraCutAgents::LinkState state, const QString &message = QString());
    void scheduleReconnect();
    void onTextMessage(const QString &text);
    void open();

    QWebSocket *m_socket{nullptr};
    QTimer m_reconnect;
    QUrl m_url;
    KoraCutAgents::LinkState m_state{KoraCutAgents::LinkState::NoService};
    QString m_message;
    QByteArray m_token;
    int m_reconnectDelayMs{1000};
    bool m_stopped{true};
};
