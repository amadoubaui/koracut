/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class KoraCutAgentClient;
class KoraCutAgentPanel;
class QProcess;

/** @brief Relie les connexions IA de la personne au service d'agents.
 *
 * Trois rôles, sans rien toucher à la session :
 * 1. **Transmettre les connexions** : dès que la liaison est ouverte (après `bonjour`) et à chaque
 *    changement, le message `connexions` (PROTOCOLE.md) part avec les clés, mais **seulement vers un
 *    service local** (127.0.0.1, ::1, localhost). Jamais une clé vers une adresse distante.
 * 2. **Lancer le service local** quand aucune adresse n'est réglée (ni KORACUT_AGENTS_URL, ni
 *    koracut_agents_url) et qu'au moins une connexion existe : `node src/main.ts modele --depuis-app`
 *    dans le dossier du service, sur un port libre, avec un jeton de poignée de main. Il ne démarre
 *    qu'à l'ouverture du panneau Agents (rien en fond tant qu'on ne s'en sert pas) et s'arrête avec
 *    l'application.
 * 3. **Dire la bascule** : le message `bascule` du service (« Anthropic à court de crédit, je continue
 *    avec Gemini ») devient une ligne dans le panneau.
 */
class KoraCutAiBridge : public QObject
{
    Q_OBJECT
public:
    KoraCutAiBridge(KoraCutAgentClient *client, KoraCutAgentPanel *panel, QObject *parent = nullptr);
    ~KoraCutAiBridge() override;

    /** @brief À appeler à la place de client->start() au lancement. */
    void start();
    /** @brief Arrête le service lancé par l'application, s'il y en a un. */
    void stopService();

    /** @brief Vrai pour 127.0.0.1, ::1 et localhost : les seules adresses qui reçoivent les clés. */
    static bool isLocal(const QUrl &url);
    /** @brief Le dossier du service local : KORACUT_SERVICE_AGENTS, réglage, données, puis la source. */
    static QString serviceDirectory();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void sendConnections();
    void launchService();
    void onConnectionsChanged();

    KoraCutAgentClient *m_client{nullptr};
    QPointer<KoraCutAgentPanel> m_panel;
    QProcess *m_process{nullptr};
    QTimer m_startTimeout;
    QUrl m_serviceUrl;
    QStringList m_lastLines;
    bool m_external{false};
    bool m_launchFailed{false};
};
