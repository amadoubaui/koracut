/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutagentclient.h"

#include "kdenlive_debug.h"
#include "kdenlivesettings.h"

#include <KLocalizedString>

#include <QJsonDocument>
#include <QNetworkRequest>
#include <QWebSocket>

KoraCutAgentClient::KoraCutAgentClient(QObject *parent)
    : KoraCutAgentLink(parent)
{
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_reconnectDelayMs = 1000;
        setState(KoraCutAgents::LinkState::Connected, m_url.toString());
    });
    connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        if (m_stopped) {
            setState(KoraCutAgents::LinkState::NoService);
            return;
        }
        setState(KoraCutAgents::LinkState::Connecting, i18nc("@info link state", "Connection lost, retrying"));
        scheduleReconnect();
    });
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_stopped) {
            return;
        }
        setState(KoraCutAgents::LinkState::Error, m_socket->errorString());
        scheduleReconnect();
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, &KoraCutAgentClient::onTextMessage);
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, [this]() {
        if (!m_stopped && m_socket->state() == QAbstractSocket::UnconnectedState) {
            setState(KoraCutAgents::LinkState::Connecting, m_url.toString());
            open();
        }
    });
}

KoraCutAgentClient::~KoraCutAgentClient()
{
    m_stopped = true;
    m_reconnect.stop();
    // À la fermeture, la fenêtre détruit ses enfants dans l'ordre : la barre du haut part avant ce
    // client. abort() émet disconnected, qui remontait jusqu'à la pastille Agents déjà détruite
    // (plantage 139 à chaque fermeture avec un service connecté, 24 septembre 2026). On coupe
    // les signaux du socket avant de le fermer : plus personne à prévenir.
    QObject::disconnect(m_socket, nullptr, this, nullptr);
    m_socket->abort();
}

QUrl KoraCutAgentClient::configuredUrl()
{
    const QString fromEnv = qEnvironmentVariable("KORACUT_AGENTS_URL").trimmed();
    if (!fromEnv.isEmpty()) {
        return QUrl(fromEnv);
    }
    return QUrl(KdenliveSettings::koracut_agents_url().trimmed());
}

void KoraCutAgentClient::start(const QUrl &url)
{
    m_url = url.isEmpty() ? configuredUrl() : url;
    m_stopped = false;
    if (m_url.isEmpty() || !m_url.isValid()) {
        m_stopped = true;
        setState(KoraCutAgents::LinkState::NoService, i18nc("@info link state", "No service address"));
        return;
    }
    m_reconnectDelayMs = 1000;
    setState(KoraCutAgents::LinkState::Connecting, m_url.toString());
    open();
}

void KoraCutAgentClient::open()
{
    QNetworkRequest request(m_url);
    if (!m_token.isEmpty()) {
        request.setRawHeader("X-KoraCut-Jeton", m_token);
    }
    m_socket->open(request);
}

void KoraCutAgentClient::stop()
{
    m_stopped = true;
    m_reconnect.stop();
    m_socket->close();
    setState(KoraCutAgents::LinkState::NoService);
}

void KoraCutAgentClient::send(const QJsonObject &message)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(KDENLIVE_LOG) << "KoraCut: message d'agent perdu, liaison fermée :" << message.value(QStringLiteral("type")).toString();
        return;
    }
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void KoraCutAgentClient::setState(KoraCutAgents::LinkState state, const QString &message)
{
    m_message = message;
    if (state == m_state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged(state);
}

void KoraCutAgentClient::scheduleReconnect()
{
    if (m_stopped || m_reconnect.isActive()) {
        return;
    }
    m_reconnect.start(m_reconnectDelayMs);
    m_reconnectDelayMs = qMin(30000, m_reconnectDelayMs * 2);
}

void KoraCutAgentClient::onTextMessage(const QString &text)
{
    // Une ligne par message ; on tolère plusieurs lignes dans une même trame.
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);
        if (!doc.isObject()) {
            qCWarning(KDENLIVE_LOG) << "KoraCut: message du service illisible :" << error.errorString();
            continue;
        }
        Q_EMIT messageReceived(doc.object());
    }
}
