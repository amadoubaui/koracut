/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutaiconnections.h"
#include "koracutaisecrets.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QUuid>

namespace {

KSharedConfigPtr &testConfig()
{
    static KSharedConfigPtr config;
    return config;
}

KConfigGroup settingsGroup()
{
    const KSharedConfigPtr config = testConfig() ? testConfig() : KSharedConfig::openConfig();
    return config->group(QStringLiteral("KoraCut IA"));
}

QString trimmedBase(QString url)
{
    url = url.trimmed();
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

} // namespace

// ---------------------------------------------------------------------------------------------

QJsonObject KoraCutAiConnection::toJson() const
{
    QJsonObject o{{QStringLiteral("id"), id}, {QStringLiteral("fournisseur"), provider}, {QStringLiteral("compte"), account}, {QStringLiteral("modele"), model}};
    if (!baseUrl.isEmpty()) {
        o.insert(QStringLiteral("url_base"), baseUrl);
    }
    return o;
}

KoraCutAiConnection KoraCutAiConnection::fromJson(const QJsonObject &o)
{
    KoraCutAiConnection c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.provider = o.value(QStringLiteral("fournisseur")).toString();
    c.account = o.value(QStringLiteral("compte")).toString();
    c.model = o.value(QStringLiteral("modele")).toString();
    c.baseUrl = o.value(QStringLiteral("url_base")).toString();
    return c;
}

// ---------------------------------------------------------------------------------------------

KoraCutAiConnections *KoraCutAiConnections::instance()
{
    static KoraCutAiConnections *self = new KoraCutAiConnections(qApp);
    return self;
}

KoraCutAiConnections::KoraCutAiConnections(QObject *parent)
    : QObject(parent)
{
    reload();
}

void KoraCutAiConnections::useConfigForTests(const QString &file)
{
    testConfig() = file.isEmpty() ? KSharedConfigPtr() : KSharedConfig::openConfig(file, KConfig::SimpleConfig);
    instance()->reload();
}

void KoraCutAiConnections::reload()
{
    m_list.clear();
    const QByteArray raw = settingsGroup().readEntry("connexions", QString()).toUtf8();
    for (const QJsonValue &v : QJsonDocument::fromJson(raw).array()) {
        const KoraCutAiConnection c = KoraCutAiConnection::fromJson(v.toObject());
        if (c.isValid()) {
            m_list << c;
        }
    }
}

void KoraCutAiConnections::save()
{
    QJsonArray array;
    for (const KoraCutAiConnection &c : std::as_const(m_list)) {
        array.append(c.toJson());
    }
    KConfigGroup group = settingsGroup();
    group.writeEntry("connexions", QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    group.sync();
    Q_EMIT changed();
}

KoraCutAiConnection KoraCutAiConnections::connection(const QString &id) const
{
    for (const KoraCutAiConnection &c : m_list) {
        if (c.id == id) {
            return c;
        }
    }
    return {};
}

QString KoraCutAiConnections::add(KoraCutAiConnection connection, const QString &key, QString *error)
{
    connection.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    if (!key.isEmpty() && !KoraCutAiSecrets::write(connection.id, key, error)) {
        return {};
    }
    m_list << connection;
    save();
    return connection.id;
}

bool KoraCutAiConnections::update(const KoraCutAiConnection &connection, const QString &key, QString *error)
{
    for (KoraCutAiConnection &c : m_list) {
        if (c.id == connection.id) {
            if (!key.isEmpty() && !KoraCutAiSecrets::write(connection.id, key, error)) {
                return false;
            }
            c = connection;
            save();
            return true;
        }
    }
    return false;
}

void KoraCutAiConnections::remove(const QString &id)
{
    for (int i = 0; i < m_list.size(); ++i) {
        if (m_list.at(i).id == id) {
            m_list.removeAt(i);
            KoraCutAiSecrets::remove(id);
            save();
            return;
        }
    }
}

void KoraCutAiConnections::reorder(const QStringList &ids)
{
    QList<KoraCutAiConnection> ordered;
    for (const QString &id : ids) {
        const KoraCutAiConnection c = connection(id);
        if (c.isValid()) {
            ordered << c;
        }
    }
    // Ce que la liste d'ids oublie garde sa place relative, à la fin.
    for (const KoraCutAiConnection &c : std::as_const(m_list)) {
        if (!ids.contains(c.id)) {
            ordered << c;
        }
    }
    bool same = ordered.size() == m_list.size();
    for (int i = 0; same && i < ordered.size(); ++i) {
        same = ordered.at(i).id == m_list.at(i).id;
    }
    if (!same) {
        m_list = ordered;
        save();
    }
}

bool KoraCutAiConnections::hasModelConnection() const
{
    for (const KoraCutAiConnection &c : m_list) {
        if (KoraCutAiCatalog::provider(c.provider).format != QLatin1String("voix")) {
            return true;
        }
    }
    return false;
}

bool KoraCutAiConnections::hasKey(const QString &id) const
{
    bool found = false;
    KoraCutAiSecrets::read(id, &found);
    return found;
}

QString KoraCutAiConnections::effectiveBaseUrl(const KoraCutAiConnection &connection, const KoraCutAiProvider &provider)
{
    return trimmedBase(connection.baseUrl.isEmpty() ? provider.baseUrl : connection.baseUrl);
}

QJsonObject KoraCutAiConnections::protocolMessage(bool withKeys) const
{
    QJsonArray entries;
    for (const KoraCutAiConnection &c : m_list) {
        const KoraCutAiProvider p = KoraCutAiCatalog::provider(c.provider);
        // Une connexion de voix (ElevenLabs) sert à la voix off, pas aux agents : le service ne la voit pas.
        if (!p.isValid() || c.model.isEmpty() || p.format == QLatin1String("voix")) {
            continue;
        }
        const QString base = effectiveBaseUrl(c, p);
        if (base.isEmpty() || KoraCutAiCatalog::hasPlaceholder(base)) {
            continue;
        }
        QJsonObject e{{QStringLiteral("id"), c.id},
                      {QStringLiteral("fournisseur"), p.id},
                      {QStringLiteral("nom"), p.name},
                      {QStringLiteral("compte"), c.account},
                      {QStringLiteral("format"), p.format},
                      {QStringLiteral("auth"), p.auth},
                      {QStringLiteral("url_base"), base},
                      {QStringLiteral("modele"), c.model},
                      {QStringLiteral("local"), p.local}};
        if (!p.extraHeaders.isEmpty()) {
            e.insert(QStringLiteral("entetes"), p.extraHeaders);
        }
        if (withKeys) {
            bool found = false;
            const QString key = KoraCutAiSecrets::read(c.id, &found);
            if (p.keyRequired && (!found || key.isEmpty())) {
                continue;
            }
            if (found) {
                e.insert(QStringLiteral("cle"), key);
            }
        }
        entries.append(e);
    }
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("connexions")}, {QStringLiteral("version"), 1}, {QStringLiteral("connexions"), entries}};
}

// ---------------------------------------------------------------------------------------------
// La vérification

KoraCutAiVerifier::KoraCutAiVerifier(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

KoraCutAiVerifier::~KoraCutAiVerifier()
{
    cancel();
}

void KoraCutAiVerifier::cancel()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    m_reply.clear();
}

QNetworkRequest KoraCutAiVerifier::modelsRequest(const KoraCutAiProvider &provider, const QString &baseUrl, const QString &key, const QString &path)
{
    const QString route = path.isEmpty() ? provider.modelsPath : path;
    QUrl url = route.startsWith(QLatin1String("http")) ? QUrl(route) : QUrl(trimmedBase(baseUrl) + route);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", "application/json");
    const QByteArray k = key.trimmed().toUtf8();
    if (!k.isEmpty()) {
        if (provider.auth == QLatin1String("x-api-key")) {
            request.setRawHeader("x-api-key", k);
        } else if (provider.auth == QLatin1String("api-key")) {
            request.setRawHeader("api-key", k);
        } else if (provider.auth == QLatin1String("x-goog-api-key")) {
            request.setRawHeader("x-goog-api-key", k);
        } else if (provider.auth == QLatin1String("xi-api-key")) {
            request.setRawHeader("xi-api-key", k);
        } else if (provider.auth != QLatin1String("aucune")) {
            request.setRawHeader("Authorization", "Bearer " + k);
        }
    }
    if (provider.format == QLatin1String("anthropic")) {
        request.setRawHeader("anthropic-version", "2023-06-01");
    }
    for (auto it = provider.extraHeaders.constBegin(); it != provider.extraHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
    }
    return request;
}

QStringList KoraCutAiVerifier::parseModels(const KoraCutAiProvider &provider, const QByteArray &body)
{
    QStringList ids;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonArray array;
    if (doc.isArray()) {
        array = doc.array();
    } else if (doc.object().contains(QStringLiteral("data"))) {
        array = doc.object().value(QStringLiteral("data")).toArray();
    } else if (doc.object().contains(QStringLiteral("models"))) {
        array = doc.object().value(QStringLiteral("models")).toArray();
    } else if (doc.object().contains(QStringLiteral("voices"))) {
        // Un fournisseur de voix (ElevenLabs) : ses voix tiennent lieu de modèles.
        array = doc.object().value(QStringLiteral("voices")).toArray();
    }
    for (const QJsonValue &v : std::as_const(array)) {
        const QJsonObject o = v.toObject();
        QString id = o.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            id = o.value(QStringLiteral("voice_id")).toString();
        }
        if (id.isEmpty()) {
            id = o.value(QStringLiteral("name")).toString();
        }
        if (id.isEmpty() && v.isString()) {
            id = v.toString();
        }
        if (provider.format == QLatin1String("gemini")) {
            // Gemini liste aussi les modèles d'embedding : on garde ceux qui génèrent du texte.
            const QJsonArray methods = o.value(QStringLiteral("supportedGenerationMethods")).toArray();
            if (!methods.isEmpty() && !methods.contains(QJsonValue(QStringLiteral("generateContent")))) {
                continue;
            }
            if (id.startsWith(QLatin1String("models/"))) {
                id = id.mid(7);
            }
        }
        if (!id.isEmpty() && !ids.contains(id)) {
            ids << id;
        }
    }
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    // Les modèles conseillés passent devant, dans l'ordre du catalogue.
    QStringList first;
    for (const QString &r : provider.recommendedModels) {
        if (ids.contains(r)) {
            first << r;
            ids.removeAll(r);
        }
    }
    return first + ids;
}

QString KoraCutAiVerifier::explainError(const KoraCutAiProvider &provider, int httpStatus, const QByteArray &body, const QString &networkError)
{
    const QString lower = QString::fromUtf8(body).toLower();
    if (httpStatus == 401 || httpStatus == 403 || lower.contains(QLatin1String("invalid api key")) || lower.contains(QLatin1String("api_key_invalid")) ||
        lower.contains(QLatin1String("invalid_api_key"))) {
        return i18nc("@info verifying an API key, %1 provider", "%1 refused this key. Check that you copied it whole, then try again.", provider.name);
    }
    if (httpStatus == 402 || lower.contains(QLatin1String("credit balance")) || lower.contains(QLatin1String("insufficient")) ||
        lower.contains(QLatin1String("billing"))) {
        return i18nc("@info verifying an API key, %1 provider", "The key works, but the %1 account has no credit left.", provider.name);
    }
    if (httpStatus == 429) {
        return i18nc("@info verifying an API key, %1 provider", "%1 says too many requests or quota reached. Try again in a moment.", provider.name);
    }
    if (httpStatus == 404) {
        return i18nc("@info verifying an API key", "Nothing answers at this address. Check the base URL.");
    }
    if (httpStatus >= 500) {
        return i18nc("@info verifying an API key, %1 provider", "%1 is not answering right now (error %2). Try again later.", provider.name, httpStatus);
    }
    if (httpStatus == 0) {
        if (provider.local) {
            return i18nc("@info verifying a local model server, %1 provider", "%1 is not answering on this computer. Start it, then try again.", provider.name);
        }
        return i18nc("@info verifying an API key, %1 network error", "No answer: %1", networkError);
    }
    return i18nc("@info verifying an API key, %1 HTTP status", "The provider answered with error %1.", httpStatus);
}

void KoraCutAiVerifier::verify(const KoraCutAiProvider &provider, const QString &baseUrl, const QString &key)
{
    cancel();
    m_provider = provider;
    m_baseUrl = baseUrl;
    m_key = key;
    m_models.clear();
    if (!provider.verifyPath.isEmpty() && provider.verifyPath != QLatin1String("chat") && !key.isEmpty()) {
        // La liste des modèles est publique chez ce fournisseur : on vérifie d'abord la clé elle-même.
        m_reply = m_network->get(modelsRequest(provider, baseUrl, key, provider.verifyPath));
        connect(m_reply, &QNetworkReply::finished, this, [this]() { onReply(Step::Verify); });
        return;
    }
    fetchModels();
}

void KoraCutAiVerifier::fetchModels()
{
    if (m_provider.modelsPath.isEmpty()) {
        // Pas de liste chez ce fournisseur : les modèles conseillés du catalogue.
        m_models = m_provider.recommendedModels;
        if (m_provider.verifyPath == QLatin1String("chat") && !m_key.isEmpty()) {
            checkByChat();
            return;
        }
        Q_EMIT finished(true, m_models, QString());
        return;
    }
    m_reply = m_network->get(modelsRequest(m_provider, m_baseUrl, m_key));
    connect(m_reply, &QNetworkReply::finished, this, [this]() { onReply(Step::Models); });
}

void KoraCutAiVerifier::checkByChat()
{
    // Liste publique et pas d'adresse pour tester la clé : un appel minimal (une réponse d'un jeton).
    QString model;
    for (const QString &r : std::as_const(m_provider.recommendedModels)) {
        if (m_models.isEmpty() || m_models.contains(r)) {
            model = r;
            break;
        }
    }
    if (model.isEmpty() && !m_models.isEmpty()) {
        model = m_models.first();
    }
    QNetworkRequest request = modelsRequest(m_provider, m_baseUrl, m_key, QStringLiteral("/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject body{{QStringLiteral("model"), model},
                           {QStringLiteral("max_tokens"), 1},
                           {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("ok")}}}}};
    m_reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, [this]() { onReply(Step::Chat); });
}

void KoraCutAiVerifier::onReply(Step step)
{
    QNetworkReply *reply = m_reply;
    m_reply.clear();
    if (!reply) {
        return;
    }
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const bool failed = reply->error() != QNetworkReply::NoError || status >= 400;
    if (step == Step::Chat) {
        // Seul un refus d'identité compte ici : un autre refus (modèle, débit) prouve que la clé passe.
        if (status == 401 || status == 403 || (status == 0 && failed)) {
            Q_EMIT finished(false, {}, explainError(m_provider, status, body, reply->errorString()));
        } else {
            Q_EMIT finished(true, m_models, QString());
        }
        return;
    }
    if (failed) {
        Q_EMIT finished(false, {}, explainError(m_provider, status, body, reply->errorString()));
        return;
    }
    if (step == Step::Verify) {
        fetchModels();
        return;
    }
    m_models = parseModels(m_provider, body);
    if (m_provider.verifyPath == QLatin1String("chat") && !m_key.isEmpty()) {
        checkByChat();
        return;
    }
    Q_EMIT finished(true, m_models, QString());
}
