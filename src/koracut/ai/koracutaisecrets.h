/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>

/** @brief Le coffre des clés API des connexions IA.
 *
 * Une clé ne va jamais dans koracutrc ni dans un journal. Elle va dans le trousseau du système
 * (Keychain sur macOS, Secret Service sur Linux, Credential Manager sur Windows) par QtKeychain.
 * Si QtKeychain n'est pas compilé, ou si le trousseau refuse (session sans trousseau déverrouillé),
 * repli sur un fichier lisible par son seul propriétaire (droits 0600) dans le dossier de données.
 * Chaque clé est rangée sous l'identifiant de sa connexion.
 */
namespace KoraCutAiSecrets {

enum class Backend { Keychain, File };

/** @brief Le coffre utilisé : le trousseau quand il est compilé, sinon le fichier. */
Backend backend();
/** @brief Mot affiché dans la page : « trousseau du système » ou « fichier protégé ». */
QString backendLabel();
bool write(const QString &id, const QString &secret, QString *error = nullptr);
/** @brief La clé, vide si elle n'existe pas. @p found dit si elle existe. */
QString read(const QString &id, bool *found = nullptr, QString *error = nullptr);
bool remove(const QString &id);

/** @brief Pour les tests : force le fichier, rangé dans @p directory. Vide : retour au défaut. */
void useFileForTests(const QString &directory);
/** @brief Le nom du service sous lequel les clés sont rangées dans le trousseau. */
QString serviceName();

} // namespace KoraCutAiSecrets
