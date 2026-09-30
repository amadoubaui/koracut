/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "undohelper.hpp"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class KdenliveDoc;
class TimelineItemModel;

/** @brief Une colonne d'un tableau de script. */
struct KoraCutScriptColumn
{
    QString id;
    QString title;
    /** texte, duree, media, case, choix */
    QString type{QStringLiteral("texte")};
    /** Les choix possibles d'une colonne « choix ». */
    QStringList choices;
};

/** @brief Une ligne d'un tableau : une scène. */
struct KoraCutScriptRow
{
    QString id;
    /** Identifiant de colonne vers valeur. Une case cochée vaut « 1 », décochée une chaîne vide. */
    QMap<QString, QString> cells;
    /** L'image du marqueur relié dans la timeline, -1 sans marqueur. */
    int marker{-1};
    /** La zone reliée (images, fin exclue), -1 sans zone. */
    int zoneIn{-1};
    int zoneOut{-1};

    bool isLinked() const { return marker >= 0 || zoneIn >= 0; }
    /** L'image où aller : le marqueur, sinon le début de la zone, -1 si rien n'est relié. */
    int linkedFrame() const { return marker >= 0 ? marker : zoneIn; }
};

/** @brief Un bloc du script : titre, paragraphe, liste, tableau ou séparateur. */
struct KoraCutScriptBlock
{
    enum class Kind { Heading, Paragraph, List, Table, Divider };

    QString id;
    Kind kind{Kind::Paragraph};
    /** Le texte d'un titre ou d'un paragraphe, le nom d'un tableau. */
    QString text;
    /** Niveau d'un titre, 1 à 3. */
    int level{1};
    /** Les éléments d'une liste. */
    QStringList items;
    QList<KoraCutScriptColumn> columns;
    QList<KoraCutScriptRow> rows;

    /** @name Tableau */
    ///@{
    int columnIndex(const QString &idOrTitle) const;
    int rowIndex(const QString &rowId) const;
    /** Ajoute une colonne en @p at (-1 : à la fin) et rend son identifiant. */
    QString addColumn(const QString &title, const QString &type, const QStringList &choices = {}, int at = -1);
    /** Ajoute une ligne vide en @p at (-1 : à la fin) et rend son identifiant. */
    QString addRow(int at = -1);
    QString duplicateRow(int row);
    void moveRow(int from, int to);
    void moveColumn(int from, int to);
    void removeColumn(int column);
    /** La valeur à poser dans une cellule : une case accepte oui, x, 1, true ; le reste passe tel quel. */
    static QString normalizedValue(const QString &type, const QString &value);
    /** Le texte qui résume une scène (la première colonne de texte remplie). */
    QString rowSummary(int row) const;
    ///@}
};

/** @brief Le script d'un projet : un document en blocs, enregistré dans le projet.
 *
 * Demande URGENTE d'Amadou Ba du 24 septembre 2026 : une partie de l'application où les agents
 * écrivent le script, ou la personne qui monte écrit le sien. Du texte et des tableaux comme dans
 * Notion, un tableau déjà prêt qui change selon le type de projet. Document 21.
 *
 * Le script vit dans la propriété de document `koracut:script`, en JSON compact avec un numéro de
 * version du format. Tant qu'elle est vide, le script affiché est le modèle du type de vidéo du
 * projet (`koracut:videotype`) : rien ne s'écrit dans le projet avant la première modification.
 */
class KoraCutScript
{
public:
    static constexpr int FormatVersion = 1;
    static const QString PropertyName;

    QList<KoraCutScriptBlock> blocks;
    /** Le type de vidéo dont le modèle a été posé, pour information. */
    QString typeId;

    bool isEmpty() const { return blocks.isEmpty(); }
    int indexOf(const QString &blockId) const;
    KoraCutScriptBlock *block(const QString &blockId);
    const KoraCutScriptBlock *block(const QString &blockId) const;
    /** Le premier tableau, ou nul. */
    KoraCutScriptBlock *firstTable();
    int tableCount() const;

    /** @brief Insère @p block en @p at (-1 : à la fin), lui donne un identifiant s'il n'en a pas. */
    QString insertBlock(KoraCutScriptBlock block, int at = -1);
    bool removeBlock(const QString &blockId);
    bool moveBlock(const QString &blockId, int to);
    /** La copie reçoit de nouveaux identifiants, lignes et colonnes comprises. */
    QString duplicateBlock(const QString &blockId);

    QJsonObject toJson() const;
    QString toCompactString() const;
    /** @param error le message, en français, quand le JSON ne se lit pas. */
    static KoraCutScript fromJson(const QJsonObject &json, QString *error = nullptr);
    static KoraCutScript fromString(const QString &text, bool *ok = nullptr);

    /** @name Export et import */
    ///@{
    QString toMarkdown() const;
    static QString tableToCsv(const KoraCutScriptBlock &table);
    static QList<QStringList> parseCsv(const QString &text);
    /** @brief Ajoute les lignes d'un CSV à @p table. La première ligne donne les colonnes : une
     * colonne de même nom est reprise, les autres sont créées en texte. Rend le nombre de lignes. */
    static int importCsv(KoraCutScriptBlock &table, const QString &csv, QString *error = nullptr);
    ///@}

    /** @name Blocs neufs et modèles */
    ///@{
    static QString newId(const QString &prefix);
    static KoraCutScriptBlock newBlock(KoraCutScriptBlock::Kind kind);
    /** Un tableau de scènes avec les sept colonnes du modèle, sans ligne. */
    static KoraCutScriptBlock newScenesTable(const QString &title);
    /** @brief Le modèle du type de vidéo @p typeId (data/koracut-script-templates.json), ou le
     * modèle générique (Accroche, Promesse, Développement, Appel à l'action) si le type est vide ou inconnu. */
    static KoraCutScript templateFor(const QString &typeId);
    static bool hasTemplate(const QString &typeId);
    ///@}

    static QString kindName(KoraCutScriptBlock::Kind kind);
    static bool kindFromName(const QString &name, KoraCutScriptBlock::Kind &kind);
    static QStringList columnTypes();
    static QString columnTypeLabel(const QString &type);
};

/** @brief Prévient le panneau et les agents quand le script du projet change (édition, annuler). */
class KoraCutScriptNotifier : public QObject
{
    Q_OBJECT
public:
    static KoraCutScriptNotifier *instance();
Q_SIGNALS:
    void changed();
};

/** @brief Le script du document courant : le lire, et le changer en une étape d'annulation. */
namespace KoraCutScriptStore {

/** @brief Le script du document (le courant si @p doc est nul). Sans script enregistré, le modèle
 * du type de vidéo du projet ; @p fromTemplate le dit. */
KoraCutScript current(KdenliveDoc *doc = nullptr, bool *fromTemplate = nullptr);
/** @brief Vrai si le projet a déjà un script enregistré. */
bool isStored(KdenliveDoc *doc = nullptr);

/** @brief Enregistre @p after dans le document courant, en une étape d'annulation.
 * @param mergeKey non vide : les étapes qui se suivent avec la même clé à moins de 2,5 s d'écart
 *        n'en font qu'une (une frappe dans une même cellule, comme dans un éditeur de texte).
 * @param extraUndo, extraRedo une action déjà faite à défaire et refaire avec le script (le marqueur
 *        posé par « Poser un marqueur ici »).
 * @return faux sans document ouvert. */
bool apply(const KoraCutScript &after, const QString &undoText, const QString &mergeKey = QString(), const Fun &extraUndo = Fun(),
           const Fun &extraRedo = Fun());

/** @brief Pose un marqueur de la timeline à @p frame avec le texte de la scène et relie la ligne,
 * le tout en une étape d'annulation. */
bool placeMarker(const std::shared_ptr<TimelineItemModel> &timeline, const QString &blockId, const QString &rowId, int frame, QString *error = nullptr);

} // namespace KoraCutScriptStore
