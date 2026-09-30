/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUndoCommand>
#include <functional>
#include <memory>
#include <vector>

class DocUndoStack;
class QLocalServer;
class QLocalSocket;
class QWidget;
class TimelineItemModel;

/** @brief La porte de commande de KoraCut : une prise locale, un jeton, JSON-RPC, un registre de méthodes.
 *
 * Document 18 et contrat `Agents in-app/PORTE.md` (lot 1, 24 septembre 2026). N'importe quel
 * programme qui tourne sur la machine (koracut-ctl, le serveur MCP, Claude Code, un script) parle
 * à l'application pendant qu'elle tourne : une requête `{"id", "method", "params"}` par ligne, une
 * réponse `{"id", "result"}` ou `{"id", "error"}` par ligne. Les méthodes s'enregistrent par nom
 * (`famille.methode`) avec un résumé et la liste de leurs paramètres : c'est ce que rendent
 * `porte.methodes`, `koracut-ctl --aide` et les outils du serveur MCP.
 *
 * Tout s'exécute dans le fil de l'interface, dans l'ordre d'arrivée. Une méthode peut répondre plus
 * tard (import qui attend les clips, transcription) : elle s'enregistre en asynchrone.
 *
 * Ce que la porte tient elle-même, pour toutes les méthodes :
 * - le jeton (premier message `porte.ouvrir`, sinon la prise se ferme) ;
 * - la confirmation : une méthode sensible appelle needsConfirmation(), la Console demande à la
 *   personne, la réponse rejoue l'appel ;
 * - le tour par client (`historique.tour`) : les commandes d'annulation poussées pendant le tour
 *   sont mises de côté (DocUndoStack::beginCollecting) et regroupées en une seule étape. Le tour, la
 *   confiance et les demandes suivent le nom du client, pas la connexion : koracut-ctl ouvre une
 *   connexion par commande ;
 * - les événements aux abonnés, et le journal pour la Console.
 */
class KoraCutPorte : public QObject
{
    Q_OBJECT
public:
    /** @brief Un paramètre d'une méthode, pour le catalogue. */
    struct Param
    {
        QString name;
        QString type; ///< string, number, boolean, array, object, any
        QString description;
        bool required{false};
    };

    /** @brief La réponse d'une méthode : un résultat, ou une erreur `{ code, message, conseil }`. */
    struct Reply
    {
        QJsonValue result;
        QJsonObject error;
        bool ok() const { return error.isEmpty(); }
        static Reply value(const QJsonValue &v);
        static Reply fail(const QString &code, const QString &message, const QString &advice = QString(), const QJsonObject &extra = QJsonObject());
        /** @brief Convertit la réponse d'un outil d'agent (`{ ok, erreur }`). */
        static Reply fromTool(const QJsonObject &toolResult);
        QJsonObject toJson() const;
    };

    /** @brief Ce qu'une méthode reçoit. */
    struct Call
    {
        QString client;  ///< nom donné à porte.ouvrir (« Claude Code », « koracut-ctl 1.0 »)
        quint64 clientId{0}; ///< 0 : appel de l'intérieur (Console, tests)
        QString method;
        QJsonObject params;
    };

    using Handler = std::function<Reply(const Call &)>;
    using Done = std::function<void(const Reply &)>;
    using AsyncHandler = std::function<void(const Call &, const Done &)>;

    /** @brief Une ligne du journal de la Console. */
    struct JournalEntry
    {
        QDateTime time;
        QString client;
        QString method;
        QJsonObject params;
        bool ok{true};
        QString code;    ///< code d'erreur, vide si ok
        QString summary; ///< un mot sur le résultat
        qint64 ms{0};
        int position{-1}; ///< temps concerné (images), -1 sinon
    };

    explicit KoraCutPorte(QObject *parent = nullptr);
    ~KoraCutPorte() override;

    // ---- Le registre
    void registerMethod(const QString &name, const QString &summary, const QList<Param> &params, Handler handler, bool confirmation = false);
    void registerAsyncMethod(const QString &name, const QString &summary, const QList<Param> &params, AsyncHandler handler, bool confirmation = false);
    bool hasMethod(const QString &name) const;
    /** @brief Le catalogue : `[{ nom, resume, confirmation, params: [{ nom, type, description, requis }] }]`. */
    QJsonArray catalogue() const;
    QStringList methodNames() const;

    // ---- La prise
    /** @brief Ouvre la prise `koracut-<reglage>` et écrit porte.json dans @p configDir. */
    bool listen(const QString &reglage, const QString &configDir);
    /** @brief Ferme la prise et retire porte.json. */
    void close();
    bool isListening() const;
    QString socketPath() const;
    QString token() const;
    QString porteFilePath() const { return m_porteFile; }
    QString reglage() const { return m_reglage; }
    /** @brief Le réglage « Autoriser les programmes extérieurs » : ferme ou rouvre la prise. */
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    int clientCount() const;
    static QString defaultConfigDir();

    // ---- Appels de l'intérieur (Console, tests)
    Reply call(const QString &client, const QString &method, const QJsonObject &params);
    void callAsync(const QString &client, const QString &method, const QJsonObject &params, const Done &done);
    /** @brief `famille.methode clef=valeur …` ou du JSON : la ligne tapée dans la Console. */
    static bool parseCommandLine(const QString &line, QString &method, QJsonObject &params, QString &error);

    // ---- La confirmation (pour les méthodes)
    /** @brief Vrai si l'appel doit attendre l'accord de la personne : @p gate contient alors la
     *  réponse `confirmation_requise` à rendre. @p always : même un client de confiance doit demander. */
    bool needsConfirmation(const Call &call, const QString &what, bool always, Reply &gate);
    void answerConfirmation(const QString &demandeId, bool accepted, bool trustClient = false);
    QStringList pendingConfirmations() const;

    // ---- Le tour par client
    bool beginTurn(quint64 clientId, const QString &client, const QString &title, QString &error);
    /** @brief Ferme le tour : ses commandes deviennent une étape « <client> : <titre> ». Rend le nombre d'actions. */
    int endTurn(quint64 clientId, QString &error, bool abandon = false);
    bool turnOpen(quint64 clientId) const;
    /** @brief Exécute @p fn comme une commande du client @p client (son nom) : dans son tour s'il en a un
     *  ouvert, sans fenêtre à choix. Pour une méthode qui agit après une pause (montage visible,
     *  25 septembre 2026) : execute() a déjà rendu la main quand le geste se fait. */
    void runAsClient(const QString &client, const std::function<void()> &fn);

    // ---- Les événements
    static const QStringList &eventTypes();
    void publish(const QString &type, const QJsonObject &params, quint64 exceptClient = 0);
    bool subscribe(quint64 clientId, const QStringList &types, QStringList &accepted);
    bool unsubscribe(quint64 clientId, const QStringList &types);

    void setUndoStackProvider(std::function<std::shared_ptr<DocUndoStack>()> provider);
    /** @brief Ce que porte.ouvrir dit du projet courant (chemin ou null). */
    void setProjectProvider(std::function<QString()> provider);
    /** @brief Vrai tant qu'un projet se charge : les commandes (hors app.* et porte.*) répondent « occupe ». */
    void setBusyProvider(std::function<bool()> provider);

    // ---- Les fenêtres (lot 3 : aucune commande n'ouvre une fenêtre qui bloque)
    /** @brief Décrit une fenêtre secondaire : `{ titre, classe, boutons, champs, onglets }`. */
    static QJsonObject describeWindow(QWidget *w);
    /** @brief Le nom de la propriété posée sur l'application pendant une commande : le code qui
     *  ouvrirait une fenêtre modale prend alors sa réponse par défaut (création des pistes audio). */
    static const char *noDialogProperty();
    /** @brief Surveille les fenêtres modales de l'application : événement `fenetre` aux abonnés. */
    void watchWindows();
    /** @brief La séquence dont la sélection est publiée (`selection`) ; à rappeler quand elle change. */
    void watchTimeline(const std::shared_ptr<TimelineItemModel> &timeline);
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void journal(const KoraCutPorte::JournalEntry &entry);
    void confirmationRequested(const QString &demandeId, const QString &client, const QString &method, const QJsonObject &params, const QString &what);
    void confirmationAnswered(const QString &demandeId, bool accepted);
    void clientOpened(const QString &client);
    void clientClosed(const QString &client);
    void listeningChanged(bool listening);

private:
    struct Method;
    struct Client;
    struct ClientState;
    struct Turn;
    struct Demand;

    void onNewConnection();
    void onReadyRead(Client *client);
    void onDisconnected(Client *client);
    void handleLine(Client *client, const QByteArray &line);
    void dispatch(Client *client, const QJsonValue &id, const QString &method, const QJsonObject &params);
    void execute(Client *client, const Call &call, const Done &done);
    void send(Client *client, const QJsonObject &message);
    void writePorteFile();
    void removePorteFile();
    /** @brief Retire les porte*.json des instances mortes (tuées sans passer par la fermeture). */
    void sweepStaleFiles();
    Client *clientById(quint64 id) const;
    std::shared_ptr<DocUndoStack> undoStack() const;
    void logCall(const Call &call, const Reply &reply, qint64 ms);

    QHash<QString, std::shared_ptr<Method>> m_methods;
    QStringList m_order;
    QLocalServer *m_server{nullptr};
    std::vector<std::unique_ptr<Client>> m_clients;
    /** Le client de l'intérieur (Console, tests) : sans prise, jamais fermé. */
    std::unique_ptr<Client> m_inner;
    QHash<QString, std::shared_ptr<Demand>> m_demands;
    /** Ce qui survit à la connexion, par nom de client (koracut-ctl se reconnecte à chaque commande) :
     *  le tour ouvert et la confiance accordée pour la session. */
    QHash<QString, std::shared_ptr<ClientState>> m_states;
    std::shared_ptr<ClientState> stateFor(Client *client);
    QString m_reglage;
    QString m_configDir;
    QString m_porteFile;
    QString m_token;
    bool m_enabled{true};
    quint64 m_nextClient{1};
    int m_nextDemand{1};
    std::function<std::shared_ptr<DocUndoStack>()> m_undoStackProvider;
    std::function<QString()> m_projectProvider;
    std::function<bool()> m_busyProvider;
    int m_running{0};
    bool m_watchingWindows{false};
    std::weak_ptr<TimelineItemModel> m_watchedTimeline;
    QMetaObject::Connection m_selectionConnection;
};

/** @brief Tout ce qu'un client a fait pendant un tour, en une seule étape d'annulation.
 *  Même mécanique que le tour d'agent (KoraCutTurnCommand), tenue ici pour que la porte se compile
 *  aussi sans KORACUT_AGENTS. */
class KoraCutPorteTurnCommand : public QUndoCommand
{
public:
    KoraCutPorteTurnCommand(std::vector<QUndoCommand *> steps, const QString &text);
    ~KoraCutPorteTurnCommand() override;
    void undo() override;
    void redo() override;
    int steps() const { return int(m_steps.size()); }

private:
    std::vector<QUndoCommand *> m_steps;
    bool m_undone{false};
};

Q_DECLARE_METATYPE(KoraCutPorte::JournalEntry)
