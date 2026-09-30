/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutaicatalog.h"

#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

/** @brief Une connexion IA : un fournisseur, un compte, un modèle. La clé n'est jamais ici. */
struct KoraCutAiConnection
{
    QString id;
    QString provider;
    QString account;
    QString model;
    /** Vide : l'adresse du catalogue. */
    QString baseUrl;

    bool isValid() const { return !id.isEmpty() && !provider.isEmpty(); }
    QJsonObject toJson() const;
    static KoraCutAiConnection fromJson(const QJsonObject &o);
};

/** @brief Les connexions de la personne, dans l'ordre de priorité.
 *
 * La première sert d'abord ; si elle n'a plus de crédit, de quota ou si sa clé est refusée, les
 * agents passent à la suivante (le service le dit dans le panneau). La liste (sans les clés) vit
 * dans koracutrc, groupe « KoraCut IA » ; les clés vivent dans le coffre (koracutaisecrets.h).
 */
class KoraCutAiConnections : public QObject
{
    Q_OBJECT
public:
    static KoraCutAiConnections *instance();

    QList<KoraCutAiConnection> list() const { return m_list; }
    KoraCutAiConnection connection(const QString &id) const;
    bool isEmpty() const { return m_list.isEmpty(); }
    /** @brief Vrai s'il y a au moins une connexion à un modèle pour les agents (une connexion de voix,
     *  format « voix », ne compte pas : elle sert à la voix off). */
    bool hasModelConnection() const;
    /** @brief Ajoute la connexion à la fin (la dernière est la moins prioritaire) et range la clé. */
    QString add(KoraCutAiConnection connection, const QString &key, QString *error = nullptr);
    /** @brief Change le compte, le modèle ou l'adresse ; la clé seulement si @p key n'est pas vide. */
    bool update(const KoraCutAiConnection &connection, const QString &key = QString(), QString *error = nullptr);
    void remove(const QString &id);
    /** @brief Nouvel ordre de priorité, par identifiants. Les inconnus sont ignorés. */
    void reorder(const QStringList &ids);
    bool hasKey(const QString &id) const;

    /** @brief Le message `connexions` du protocole (version 1). Avec @p withKeys, chaque entrée porte
     *  sa clé : à n'envoyer qu'à un service local (voir KoraCutAiBridge). */
    QJsonObject protocolMessage(bool withKeys) const;
    /** @brief L'adresse de base d'une connexion : la sienne, sinon celle du catalogue. */
    static QString effectiveBaseUrl(const KoraCutAiConnection &connection, const KoraCutAiProvider &provider);
    /** @brief Relit la liste depuis les réglages (tests). */
    void reload();
    /** @brief Pour les tests : les réglages dans @p file (chemin complet) au lieu de koracutrc. */
    static void useConfigForTests(const QString &file);

Q_SIGNALS:
    void changed();

private:
    explicit KoraCutAiConnections(QObject *parent = nullptr);
    void save();
    QList<KoraCutAiConnection> m_list;
};

/** @brief « Vérifier et enregistrer » : interroge vraiment le fournisseur (liste de ses modèles). */
class KoraCutAiVerifier : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutAiVerifier(QObject *parent = nullptr);
    ~KoraCutAiVerifier() override;

    void verify(const KoraCutAiProvider &provider, const QString &baseUrl, const QString &key);
    void cancel();
    bool running() const { return !m_reply.isNull(); }

    /** @brief La requête qui liste les modèles, en-têtes d'authentification compris. */
    static QNetworkRequest modelsRequest(const KoraCutAiProvider &provider, const QString &baseUrl, const QString &key, const QString &path = QString());
    /** @brief Les identifiants de modèles d'une réponse, conseillés d'abord. */
    static QStringList parseModels(const KoraCutAiProvider &provider, const QByteArray &body);
    /** @brief Une phrase simple pour la personne, d'après le code HTTP et la réponse. */
    static QString explainError(const KoraCutAiProvider &provider, int httpStatus, const QByteArray &body, const QString &networkError);

Q_SIGNALS:
    void finished(bool ok, const QStringList &models, const QString &message);

private:
    enum class Step { Verify, Models, Chat };
    void fetchModels();
    void checkByChat();
    void onReply(Step step);

    QNetworkAccessManager *m_network{nullptr};
    QPointer<QNetworkReply> m_reply;
    KoraCutAiProvider m_provider;
    QString m_baseUrl;
    QString m_key;
    QStringList m_models;
};
