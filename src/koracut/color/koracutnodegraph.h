/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// Les gestes de l'éditeur de nœuds sur le graphe JSON (document 22), comme dans Resolve :
// série (Option-S), série avant (Shift-S), à la fin (Option-K), parallèle (Option-P), calque
// (Option-L), outside (Option-O), séparateur / combineur (Option-Y), supprimer en reconnectant.
// Partagé par l'interface, le contrôleur et les outils des agents.

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace KoraCutNodeGraph {

QJsonObject defaultGraph();
/** @brief Le graphe tel qu'enregistré, complété (format, sortie, positions) et vérifié. */
QJsonObject normalized(const QJsonObject &graph);

QJsonArray nodes(const QJsonObject &graph);
QJsonObject node(const QJsonObject &graph, int id);
bool hasNode(const QJsonObject &graph, int id);
void putNode(QJsonObject &graph, const QJsonObject &node);
int outputNode(const QJsonObject &graph);
QList<int> nodeIds(const QJsonObject &graph);
/** @brief Numéro affiché (« 01 », « 02 »…) : l'ordre de création des correcteurs, sans trou. */
int displayNumber(const QJsonObject &graph, int id);
/** @brief Les nœuds dont la sortie RVB (ou de clé) arrive dans ce nœud. */
QList<int> rgbInputs(const QJsonObject &node);
bool isCorrector(const QJsonObject &node);
bool isMixer(const QJsonObject &node);

// Gestes. Chacun rend l'identifiant du nœud créé (ou -1), et laisse le graphe rangé.
int addSerial(QJsonObject &graph, int after);
int addSerialBefore(QJsonObject &graph, int before);
int appendNode(QJsonObject &graph);
int addParallel(QJsonObject &graph, int beside);
int addLayer(QJsonObject &graph, int beside);
int addOutside(QJsonObject &graph, int after);
int addSplitterCombiner(QJsonObject &graph, int after);
bool deleteNode(QJsonObject &graph, int id);
/** @brief Relie la sortie de from à l'entrée de to (clé si key). Refuse une boucle. */
bool connectNodes(QJsonObject &graph, int from, int to, bool key, int mixerSlot = -1);
bool disconnectInput(QJsonObject &graph, int to, bool key);
void resetNode(QJsonObject &graph, int id);
void resetAll(QJsonObject &graph, bool keepNodes);
/** @brief Ajoute les nœuds d'un autre graphe à la suite de la sortie (Append Node Graph). */
void appendGraph(QJsonObject &graph, const QJsonObject &other);
/** @brief Range les nœuds en colonnes (profondeur depuis la source) et en rangées. */
void autoLayout(QJsonObject &graph);

/** @brief Lit ou écrit un réglage : section (« primaires ») et clé (« lift »). */
QJsonValue value(const QJsonObject &graph, int id, const QString &section, const QString &key);
void setValue(QJsonObject &graph, int id, const QString &section, const QString &key, const QJsonValue &v);
void setSection(QJsonObject &graph, int id, const QString &section, const QJsonValue &v);
void setField(QJsonObject &graph, int id, const QString &key, const QJsonValue &v);

/** @brief Les palettes utilisées par un nœud (badges sous la vignette). */
QStringList usedTools(const QJsonObject &node);

} // namespace KoraCutNodeGraph
