/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"

#include "koracut/ai/koracutaibridge.h"
#include "koracut/ai/koracutaicatalog.h"
#include "koracut/ai/koracutaiconnections.h"
#include "koracut/ai/koracutaisecrets.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QEventLoop>
#include <QTimer>
#include <QSvgRenderer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

namespace {

/** Un faux fournisseur HTTP : la bonne clé liste deux modèles, une autre reçoit 401. */
class FakeProvider : public QObject
{
public:
    explicit FakeProvider(QByteArray goodKey)
        : m_goodKey(std::move(goodKey))
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket *socket = m_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                m_buffer += socket->readAll();
                const int end = m_buffer.indexOf("\r\n\r\n");
                if (end < 0) {
                    return;
                }
                const QByteArray head = m_buffer.left(end);
                const QByteArray firstLine = head.left(head.indexOf("\r\n"));
                int length = 0;
                for (const QByteArray &line : head.split('\n')) {
                    if (line.toLower().startsWith("content-length:")) {
                        length = line.mid(15).trimmed().toInt();
                    }
                }
                if (m_buffer.size() < end + 4 + length) {
                    return;
                }
                requests << firstLine;
                const bool authorized = head.contains(QByteArray("Bearer " + m_goodKey)) || head.contains(QByteArray("x-api-key: " + m_goodKey));
                QByteArray body;
                QByteArray status = "200 OK";
                if (!authorized) {
                    status = "401 Unauthorized";
                    body = R"({"error":{"message":"Incorrect API key provided"}})";
                } else if (firstLine.contains("/chat/completions")) {
                    body = R"({"choices":[{"message":{"role":"assistant","content":"ok"}}]})";
                } else if (firstLine.contains("/key")) {
                    body = R"({"data":{"label":"essai"}})";
                } else {
                    body = R"({"data":[{"id":"zeta-modele"},{"id":"alpha-conseille"}]})";
                }
                m_buffer.clear();
                socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort()); }
    QList<QByteArray> requests;

private:
    QTcpServer m_server;
    QByteArray m_goodKey;
    QByteArray m_buffer;
};

/** Attend la réponse du vérificateur (QtTest n'est pas lié aux tests de KoraCut). */
struct Verdict
{
    bool ok{false};
    QStringList models;
    QString message;
};

bool waitVerdict(KoraCutAiVerifier &verifier, Verdict &verdict)
{
    QEventLoop loop;
    bool done = false;
    const auto c = QObject::connect(&verifier, &KoraCutAiVerifier::finished, &loop, [&](bool ok, const QStringList &models, const QString &message) {
        verdict = {ok, models, message};
        done = true;
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(c);
    return done;
}

KoraCutAiProvider fakeProvider(const QString &base)
{
    KoraCutAiProvider p;
    p.id = QStringLiteral("faux");
    p.name = QStringLiteral("Faux");
    p.format = QStringLiteral("openai");
    p.auth = QStringLiteral("bearer");
    p.baseUrl = base;
    p.modelsPath = QStringLiteral("/models");
    p.recommendedModels = {QStringLiteral("alpha-conseille")};
    return p;
}

} // namespace

TEST_CASE("Connexions IA : le catalogue des fournisseurs", "[KoraCutAi]")
{
    const QList<KoraCutAiProvider> providers = KoraCutAiCatalog::providers();
    REQUIRE(providers.size() >= 30);
    QSet<QString> ids;
    QStringList free;
    for (const KoraCutAiProvider &p : providers) {
        INFO(p.id.toStdString());
        CHECK_FALSE(ids.contains(p.id));
        ids.insert(p.id);
        CHECK((p.format == QLatin1String("anthropic") || p.format == QLatin1String("openai") || p.format == QLatin1String("gemini") ||
               p.format == QLatin1String("voix")));
        CHECK((p.auth == QLatin1String("bearer") || p.auth == QLatin1String("x-api-key") || p.auth == QLatin1String("api-key") ||
               p.auth == QLatin1String("x-goog-api-key") || p.auth == QLatin1String("xi-api-key") || p.auth == QLatin1String("aucune")));
        CHECK(p.baseUrl.startsWith(QLatin1String("http")));
        // Jamais de tiret long dans les textes affichés.
        CHECK_FALSE(QString(p.description + p.freeNote + p.note + p.baseUrlHint).contains(QChar(0x2014)));
        if (p.id != QLatin1String("compatible_openai")) {
            CHECK(p.docUrl.startsWith(QLatin1String("https://")));
            CHECK(p.keyUrl.startsWith(QLatin1String("https://")));
        }
        if (!p.logo.isEmpty()) {
            QFile logo(QStringLiteral(":/koracut/fournisseurs/") + p.logo);
            REQUIRE(logo.open(QIODevice::ReadOnly));
            QByteArray svg = logo.readAll();
            svg.replace("currentColor", "#ffffff");
            CHECK(QSvgRenderer(svg).isValid());
        }
        CHECK_FALSE(KoraCutAiCatalog::icon(p, Qt::white).isNull());
        if (p.free) {
            free << p.id;
        }
    }
    for (const char *id : {"anthropic", "openai", "gemini", "openrouter", "mistral", "groq", "deepseek", "xai", "ollama", "lmstudio", "azure", "bedrock"}) {
        CHECK(ids.contains(QString::fromLatin1(id)));
    }
    for (const char *id : {"gemini", "openrouter", "groq", "cerebras", "mistral", "nvidia", "ollama"}) {
        CHECK(free.contains(QString::fromLatin1(id)));
    }
    CHECK(KoraCutAiCatalog::provider(QStringLiteral("anthropic")).format == QLatin1String("anthropic"));
    CHECK(KoraCutAiCatalog::provider(QStringLiteral("gemini")).format == QLatin1String("gemini"));
    // Chantier 5 : ElevenLabs est un fournisseur de voix, ses voix tiennent lieu de modèles.
    const KoraCutAiProvider eleven = KoraCutAiCatalog::provider(QStringLiteral("elevenlabs"));
    CHECK(eleven.format == QLatin1String("voix"));
    CHECK(KoraCutAiVerifier::modelsRequest(eleven, eleven.baseUrl, QStringLiteral("cle")).rawHeader("xi-api-key") == "cle");
    CHECK(KoraCutAiVerifier::parseModels(eleven, R"({"voices":[{"voice_id":"v2","name":"B"},{"voice_id":"v1","name":"A"}]})") == QStringList{QStringLiteral("v1"), QStringLiteral("v2")});
    CHECK(KoraCutAiCatalog::hasPlaceholder(KoraCutAiCatalog::provider(QStringLiteral("azure")).baseUrl));
    CHECK_FALSE(KoraCutAiCatalog::hasPlaceholder(KoraCutAiCatalog::provider(QStringLiteral("openai")).baseUrl));
    // Un fournisseur sans logo reçoit une pastille, pas un logo inventé.
    KoraCutAiProvider sansLogo;
    sansLogo.id = QStringLiteral("x");
    sansLogo.name = QStringLiteral("Xyz");
    CHECK_FALSE(KoraCutAiCatalog::icon(sansLogo, Qt::white).isNull());
    QString error;
    CHECK(KoraCutAiCatalog::parse("pas du json", &error).isEmpty());
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("Connexions IA : le coffre des clés et la liste ordonnée", "[KoraCutAi]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    KoraCutAiSecrets::useFileForTests(dir.path());
    KoraCutAiConnections::useConfigForTests(dir.filePath(QStringLiteral("koracutrc-essai")));
    auto *connections = KoraCutAiConnections::instance();
    REQUIRE(connections->isEmpty());

    SECTION("Le fichier de repli n'est lisible que par son propriétaire")
    {
        REQUIRE(KoraCutAiSecrets::write(QStringLiteral("c1"), QStringLiteral("sk-secret-1")));
        bool found = false;
        CHECK(KoraCutAiSecrets::read(QStringLiteral("c1"), &found) == QLatin1String("sk-secret-1"));
        CHECK(found);
        const QFileInfo info(dir.filePath(QStringLiteral("koracut-cles-ia.json")));
        REQUIRE(info.exists());
#ifndef Q_OS_WIN // 0600 n'existe pas sous Windows : le fichier vit dans le profil de la personne (ACL du dossier)
        CHECK(info.permissions() == (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser));
#endif
        CHECK(KoraCutAiSecrets::remove(QStringLiteral("c1")));
        KoraCutAiSecrets::read(QStringLiteral("c1"), &found);
        CHECK_FALSE(found);
    }

    SECTION("Ajouter, réordonner, transmettre, retirer")
    {
        int changed = 0;
        const auto counter = QObject::connect(connections, &KoraCutAiConnections::changed, [&changed]() { ++changed; });
        KoraCutAiConnection a;
        a.provider = QStringLiteral("anthropic");
        a.account = QStringLiteral("Amadou");
        a.model = QStringLiteral("claude-sonnet-5");
        const QString idA = connections->add(a, QStringLiteral("sk-ant-essai"));
        KoraCutAiConnection g;
        g.provider = QStringLiteral("gemini");
        g.model = QStringLiteral("gemini-3.8-flash");
        const QString idG = connections->add(g, QStringLiteral("cle-gemini-essai"));
        KoraCutAiConnection o;
        o.provider = QStringLiteral("ollama");
        o.model = QStringLiteral("gpt-oss:20b");
        const QString idO = connections->add(o, QString());
        REQUIRE_FALSE(idA.isEmpty());
        REQUIRE(connections->list().size() == 3);
        CHECK(changed == 3);
        QObject::disconnect(counter);
        CHECK(connections->hasKey(idA));
        CHECK_FALSE(connections->hasKey(idO));

        // Aucune clé dans le fichier de réglages.
        QFile rc(dir.filePath(QStringLiteral("koracutrc-essai")));
        REQUIRE(rc.open(QIODevice::ReadOnly));
        const QByteArray rcText = rc.readAll();
        CHECK(rcText.contains("claude-sonnet-5"));
        CHECK_FALSE(rcText.contains("sk-ant-essai"));
        CHECK_FALSE(rcText.contains("cle-gemini-essai"));

        connections->reorder({idG, idA, idO});
        CHECK(connections->list().at(0).id == idG);
        connections->reload();
        CHECK(connections->list().at(0).id == idG);

        const QJsonObject withKeys = connections->protocolMessage(true);
        CHECK(withKeys.value(QStringLiteral("type")).toString() == QLatin1String("connexions"));
        CHECK(withKeys.value(QStringLiteral("version")).toInt() == 1);
        const QJsonArray list = withKeys.value(QStringLiteral("connexions")).toArray();
        REQUIRE(list.size() == 3);
        CHECK(list.at(0).toObject().value(QStringLiteral("fournisseur")).toString() == QLatin1String("gemini"));
        CHECK(list.at(0).toObject().value(QStringLiteral("cle")).toString() == QLatin1String("cle-gemini-essai"));
        CHECK(list.at(0).toObject().value(QStringLiteral("format")).toString() == QLatin1String("gemini"));
        CHECK(list.at(1).toObject().value(QStringLiteral("url_base")).toString() == QLatin1String("https://api.anthropic.com"));
        CHECK(list.at(1).toObject().value(QStringLiteral("compte")).toString() == QLatin1String("Amadou"));
        CHECK_FALSE(list.at(2).toObject().contains(QStringLiteral("cle")));
        CHECK(list.at(2).toObject().value(QStringLiteral("local")).toBool());
        const QByteArray withoutKeys = QJsonDocument(connections->protocolMessage(false)).toJson();
        CHECK_FALSE(withoutKeys.contains("sk-ant-essai"));

        // Une connexion dont la clé a disparu n'est pas transmise (sauf les modèles locaux).
        KoraCutAiSecrets::remove(idA);
        CHECK(connections->protocolMessage(true).value(QStringLiteral("connexions")).toArray().size() == 2);

        connections->remove(idG);
        bool found = true;
        KoraCutAiSecrets::read(idG, &found);
        CHECK_FALSE(found);
        connections->remove(idA);
        connections->remove(idO);
        CHECK(connections->isEmpty());
    }
    KoraCutAiConnections::useConfigForTests(QString());
    KoraCutAiSecrets::useFileForTests(QString());
}

TEST_CASE("Connexions IA : la vérification auprès du fournisseur", "[KoraCutAi]")
{
    SECTION("En-têtes selon le fournisseur")
    {
        KoraCutAiProvider p = fakeProvider(QStringLiteral("https://exemple.org/v1"));
        CHECK(KoraCutAiVerifier::modelsRequest(p, p.baseUrl, QStringLiteral("k1")).rawHeader("Authorization") == "Bearer k1");
        CHECK(KoraCutAiVerifier::modelsRequest(p, p.baseUrl, QStringLiteral("k1")).url().toString() == QLatin1String("https://exemple.org/v1/models"));
        p.auth = QStringLiteral("api-key");
        CHECK(KoraCutAiVerifier::modelsRequest(p, p.baseUrl, QStringLiteral("k2")).rawHeader("api-key") == "k2");
        const KoraCutAiProvider anthropic = KoraCutAiCatalog::provider(QStringLiteral("anthropic"));
        const QNetworkRequest ra = KoraCutAiVerifier::modelsRequest(anthropic, anthropic.baseUrl, QStringLiteral("k3"));
        CHECK(ra.rawHeader("x-api-key") == "k3");
        CHECK(ra.rawHeader("anthropic-version") == "2023-06-01");
        CHECK(ra.url().toString() == QLatin1String("https://api.anthropic.com/v1/models"));
        const KoraCutAiProvider gemini = KoraCutAiCatalog::provider(QStringLiteral("gemini"));
        CHECK(KoraCutAiVerifier::modelsRequest(gemini, gemini.baseUrl, QStringLiteral("k4")).rawHeader("x-goog-api-key") == "k4");
        const KoraCutAiProvider ollama = KoraCutAiCatalog::provider(QStringLiteral("ollama"));
        CHECK(KoraCutAiVerifier::modelsRequest(ollama, ollama.baseUrl, QString()).rawHeader("Authorization").isEmpty());
    }

    SECTION("Lire la liste des modèles")
    {
        const KoraCutAiProvider p = fakeProvider(QString());
        CHECK(KoraCutAiVerifier::parseModels(p, R"({"data":[{"id":"zeta"},{"id":"alpha-conseille"},{"id":"beta"}]})") ==
              QStringList{QStringLiteral("alpha-conseille"), QStringLiteral("beta"), QStringLiteral("zeta")});
        const KoraCutAiProvider gemini = KoraCutAiCatalog::provider(QStringLiteral("gemini"));
        const QStringList g = KoraCutAiVerifier::parseModels(
            gemini, R"({"models":[{"name":"models/gemini-3.8-flash","supportedGenerationMethods":["generateContent"]},{"name":"models/text-embedding-9","supportedGenerationMethods":["embedContent"]}]})");
        CHECK(g == QStringList{QStringLiteral("gemini-3.8-flash")});
        CHECK(KoraCutAiVerifier::parseModels(p, R"({"models":[{"name":"command-a-03-2025"}]})") == QStringList{QStringLiteral("command-a-03-2025")});
        CHECK(KoraCutAiVerifier::explainError(p, 401, "", QString()).contains(QStringLiteral("Faux")));
    }

    SECTION("Un vrai échange avec un faux fournisseur local")
    {
        FakeProvider server("bonne-cle");
        KoraCutAiProvider p = fakeProvider(server.base());
        KoraCutAiVerifier verifier;
        Verdict v;
        verifier.verify(p, p.baseUrl, QStringLiteral("mauvaise-cle"));
        REQUIRE(waitVerdict(verifier, v));
        CHECK_FALSE(v.ok);
        CHECK(v.message.contains(QStringLiteral("Faux")));

        verifier.verify(p, p.baseUrl, QStringLiteral("bonne-cle"));
        REQUIRE(waitVerdict(verifier, v));
        CHECK(v.ok);
        CHECK(v.models == QStringList{QStringLiteral("alpha-conseille"), QStringLiteral("zeta-modele")});

        // Liste publique : la clé se vérifie par un appel minimal (une réponse d'un jeton).
        p.verifyPath = QStringLiteral("chat");
        verifier.verify(p, p.baseUrl, QStringLiteral("bonne-cle"));
        REQUIRE(waitVerdict(verifier, v));
        CHECK(v.ok);
        CHECK(server.requests.last().startsWith("POST /v1/chat/completions"));

        // Une adresse qui vérifie la clé, puis la liste (OpenRouter).
        p.verifyPath = QStringLiteral("/key");
        verifier.verify(p, p.baseUrl, QStringLiteral("mauvaise-cle"));
        REQUIRE(waitVerdict(verifier, v));
        CHECK_FALSE(v.ok);
        CHECK(server.requests.last().startsWith("GET /v1/key"));
    }
}

TEST_CASE("Connexions IA : les clés ne partent que vers une adresse locale", "[KoraCutAi]")
{
    CHECK(KoraCutAiBridge::isLocal(QUrl(QStringLiteral("ws://127.0.0.1:8765"))));
    CHECK(KoraCutAiBridge::isLocal(QUrl(QStringLiteral("ws://localhost:8765"))));
    CHECK(KoraCutAiBridge::isLocal(QUrl(QStringLiteral("ws://[::1]:8765"))));
    CHECK_FALSE(KoraCutAiBridge::isLocal(QUrl(QStringLiteral("wss://agents.koracut.org"))));
    CHECK_FALSE(KoraCutAiBridge::isLocal(QUrl(QStringLiteral("ws://192.168.1.20:8765"))));
    CHECK_FALSE(KoraCutAiBridge::isLocal(QUrl()));
}
