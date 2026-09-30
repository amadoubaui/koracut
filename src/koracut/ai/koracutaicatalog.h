/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QColor>
#include <QIcon>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

/** @brief Un fournisseur de modèles, tel que le décrit data/koracut-fournisseurs-ia.json. */
struct KoraCutAiProvider
{
    QString id;
    QString name;
    QString logo; // nom du fichier SVG dans :/koracut/fournisseurs/, vide : pastille avec l'initiale
    QString description;
    QString docUrl;
    QString signupUrl;
    QString keyUrl;
    QString baseUrl;
    bool baseUrlEditable{false};
    QString baseUrlHint;
    /** « anthropic », « openai » (compatible chat/completions), « gemini », ou « voix » pour un
     *  fournisseur de voix de synthèse (ElevenLabs), qui sert à la voix off et jamais aux agents. */
    QString format;
    /** « bearer », « x-api-key », « api-key », « x-goog-api-key », « xi-api-key » ou « aucune ». */
    QString auth;
    QString modelsPath;
    /** Adresse qui vérifie la clé quand la liste des modèles est publique (OpenRouter). Relative à
     *  l'adresse de base, ou complète. */
    QString verifyPath;
    bool keyRequired{true};
    bool local{false};
    bool free{false};
    QString freeNote;
    QStringList recommendedModels;
    QString note;
    QJsonObject extraHeaders;

    bool isValid() const { return !id.isEmpty(); }
};

/** @brief Le catalogue des fournisseurs de modèles (page « Connexions IA »).
 *
 * Le catalogue vit dans un fichier de données, pas dans le code : on l'étend sans toucher à la
 * logique. On lit d'abord une copie de l'utilisateur (`<données>/koracut-fournisseurs-ia.json`),
 * sinon celle embarquée dans l'application (`:/koracut/fournisseurs/koracut-fournisseurs-ia.json`).
 */
namespace KoraCutAiCatalog {

QList<KoraCutAiProvider> providers();
KoraCutAiProvider provider(const QString &id);
/** @brief Lit un catalogue depuis un JSON (pour les tests). */
QList<KoraCutAiProvider> parse(const QByteArray &json, QString *error = nullptr);
/** @brief Le logo du fournisseur. Les logos d'une seule couleur (currentColor) prennent @p ink ;
 *  sans logo, une pastille ronde avec l'initiale. Jamais de logo inventé. */
QIcon icon(const KoraCutAiProvider &provider, const QColor &ink, int size = 20);
/** @brief Vrai si l'adresse contient encore un gabarit à remplacer ({ressource}, {region}…). */
bool hasPlaceholder(const QString &url);

} // namespace KoraCutAiCatalog
