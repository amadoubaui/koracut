/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutaibridge.h"
#include "koracutaiconnections.h"

#include "kdenlive_debug.h"
#include "koracut/koracutagentclient.h"
#include "koracut/koracutagentpanel.h"
#include "koracut/motion/koracutmotionrenderer.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>

#ifndef KORACUT_AGENT_SERVICE_DIR
#define KORACUT_AGENT_SERVICE_DIR ""
#endif

KoraCutAiBridge::KoraCutAiBridge(KoraCutAgentClient *client, KoraCutAgentPanel *panel, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_panel(panel)
{
    // Connecté après la session : `bonjour` part d'abord, puis `connexions`.
    connect(m_client, &KoraCutAgentLink::stateChanged, this, [this](KoraCutAgents::LinkState state) {
        if (state == KoraCutAgents::LinkState::Connected) {
            sendConnections();
        }
    });
    connect(m_client, &KoraCutAgentLink::messageReceived, this, [this](const QJsonObject &message) {
        if (message.value(QStringLiteral("type")).toString() == QLatin1String("bascule") && m_panel) {
            m_panel->showServiceNote(message.value(QStringLiteral("message")).toString());
        }
    });
    connect(KoraCutAiConnections::instance(), &KoraCutAiConnections::changed, this, &KoraCutAiBridge::onConnectionsChanged);
    m_startTimeout.setSingleShot(true);
    connect(&m_startTimeout, &QTimer::timeout, this, [this]() {
        // Le service n'a rien dit : on tente quand même, le client réessaie tout seul.
        if (m_serviceUrl.isValid()) {
            m_client->start(m_serviceUrl);
        }
    });
    if (m_panel) {
        m_panel->installEventFilter(this);
    }
}

KoraCutAiBridge::~KoraCutAiBridge()
{
    stopService();
}

bool KoraCutAiBridge::isLocal(const QUrl &url)
{
    const QString host = url.host();
    if (host == QLatin1String("localhost")) {
        return true;
    }
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

QString KoraCutAiBridge::serviceDirectory()
{
    QStringList candidates;
    candidates << qEnvironmentVariable("KORACUT_SERVICE_AGENTS");
    candidates << KSharedConfig::openConfig()->group(QStringLiteral("KoraCut IA")).readEntry("serviceDossier", QString());
    candidates << QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("agents-service"), QStandardPaths::LocateDirectory);
    candidates << QString::fromUtf8(KORACUT_AGENT_SERVICE_DIR);
    for (const QString &dir : std::as_const(candidates)) {
        if (!dir.isEmpty() && QFileInfo::exists(QDir(dir).filePath(QStringLiteral("src/main.ts")))) {
            return QDir(dir).canonicalPath();
        }
    }
    return {};
}

void KoraCutAiBridge::start()
{
    const QUrl configured = KoraCutAgentClient::configuredUrl();
    if (!configured.isEmpty()) {
        // Une adresse est réglée (service PIXEWORK, service d'essai lancé à la main) : on s'y tient.
        m_external = true;
        m_client->start(configured);
        return;
    }
    if (m_panel && m_panel->isVisible() && KoraCutAiConnections::instance()->hasModelConnection()) {
        launchService();
    }
}

bool KoraCutAiBridge::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_panel && event->type() == QEvent::Show && !m_external && !m_process && !m_launchFailed &&
        KoraCutAiConnections::instance()->hasModelConnection()) {
        // Le panneau vient de s'ouvrir : c'est le moment de démarrer le service, pas avant.
        launchService();
    }
    return QObject::eventFilter(watched, event);
}

void KoraCutAiBridge::onConnectionsChanged()
{
    m_launchFailed = false;
    if (m_client->state() == KoraCutAgents::LinkState::Connected) {
        sendConnections();
        return;
    }
    if (!m_external && !m_process && m_panel && m_panel->isVisible() && KoraCutAiConnections::instance()->hasModelConnection()) {
        launchService();
    }
}

void KoraCutAiBridge::sendConnections()
{
    if (!isLocal(m_client->url())) {
        // Un service distant (le futur service PIXEWORK) garde ses propres clés : on n'envoie rien.
        return;
    }
    const QJsonObject message = KoraCutAiConnections::instance()->protocolMessage(true);
    m_client->send(message);
    qCDebug(KDENLIVE_LOG) << "KoraCut: connexions IA transmises au service local :" << message.value(QStringLiteral("connexions")).toArray().size();
}

void KoraCutAiBridge::launchService()
{
    if (m_process || m_external) {
        return;
    }
    const QString node = KoraCutMotionHost::nodePath();
    const QString dir = serviceDirectory();
    if (node.isEmpty() || dir.isEmpty()) {
        m_launchFailed = true;
        if (m_panel) {
            m_panel->showServiceNote(node.isEmpty() ? i18nc("@info", "The agents need Node.js 24 or later on this computer (nodejs.org). Install it, then reopen this panel.")
                                                    : i18nc("@info", "The local agents service was not found. Set its folder in KORACUT_SERVICE_AGENTS."));
        }
        return;
    }
    const int port = KoraCutMotionHost::freePort();
    // Un jeton neuf à chaque lancement : seule cette KoraCut peut parler à ce service.
    QByteArray token;
    for (int i = 0; i < 4; ++i) {
        token += QByteArray::number(QRandomGenerator::system()->generate64(), 16);
    }
    m_serviceUrl = QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port));
    m_client->setHandshakeToken(token);

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(dir);
    QProcessEnvironment env = KoraCutMotionHost::environment();
    env.insert(QStringLiteral("KORACUT_SERVICE_JETON"), QString::fromLatin1(token));
    m_process->setProcessEnvironment(env);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        while (m_process && m_process->canReadLine()) {
            const QString line = QString::fromUtf8(m_process->readLine()).trimmed();
            m_lastLines << line;
            if (m_lastLines.size() > 12) {
                m_lastLines.removeFirst();
            }
            qCDebug(KDENLIVE_LOG) << "KoraCut service :" << line;
            if (line.contains(QLatin1String("l'écoute")) && m_startTimeout.isActive()) {
                m_startTimeout.stop();
                m_client->start(m_serviceUrl);
            }
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QString last = m_lastLines.isEmpty() ? QString() : m_lastLines.constLast();
        m_process->deleteLater();
        m_process = nullptr;
        m_startTimeout.stop();
        m_client->stop();
        m_launchFailed = true;
        if (m_panel) {
            m_panel->showServiceNote(i18nc("@info %1 exit code, %2 last line of the service", "The local agents service stopped (code %1). %2", code, last));
        }
    });
    // Le service lit son entrée standard : quand KoraCut disparaît, même brutalement, il s'arrête.
    m_process->start(node, {QStringLiteral("src/main.ts"), QStringLiteral("modele"), QStringLiteral("--depuis-app"), QStringLiteral("--port"), QString::number(port)});
    m_startTimeout.start(8000);
    if (m_panel) {
        m_panel->showServiceNote(i18nc("@info", "Starting the agents…"));
    }
}

void KoraCutAiBridge::stopService()
{
    m_startTimeout.stop();
    if (!m_process) {
        return;
    }
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    process->closeWriteChannel();
    process->terminate();
    if (!process->waitForFinished(1500)) {
        process->kill();
        process->waitForFinished(500);
    }
    delete process;
}
