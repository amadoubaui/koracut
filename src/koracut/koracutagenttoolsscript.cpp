/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Document 21 (24 septembre 2026) : les trois outils du script du projet, script_lire,
// script_ecrire et script_modele. Contrat : Agents in-app/outils/script.json. Le script vit dans le
// projet (KoraCutScriptStore) ; un appel = une étape d'annulation, marqueurs posés compris.
// 1er octobre 2026 : un script par séquence. Le paramètre facultatif « sequence » (rang, nom ou
// uuid) choisit laquelle ; sans lui, la séquence active.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "script/koracutscript.h"

#include "bin/model/markerlistmodel.hpp"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "koracutvideotypes.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QJsonArray>

#include <algorithm>
#include <functional>

namespace {

using namespace KoraCutAgentToolsPrivate;

constexpr int kMaxOperations = 200;

QJsonObject summary(const KoraCutScript &script)
{
    int scenes = 0;
    int done = 0;
    for (const KoraCutScriptBlock &b : script.blocks) {
        if (b.kind != KoraCutScriptBlock::Kind::Table) {
            continue;
        }
        const int check = [&b]() {
            for (int i = 0; i < b.columns.size(); ++i) {
                if (b.columns.at(i).type == QLatin1String("case")) {
                    return i;
                }
            }
            return -1;
        }();
        for (const KoraCutScriptRow &r : b.rows) {
            ++scenes;
            if (check >= 0 && !r.cells.value(b.columns.at(check).id).isEmpty()) {
                ++done;
            }
        }
    }
    return QJsonObject{{QStringLiteral("blocs"), int(script.blocks.size())},
                       {QStringLiteral("tableaux"), script.tableCount()},
                       {QStringLiteral("scenes"), scenes},
                       {QStringLiteral("scenes_faites"), done}};
}

QString videoType()
{
    return pCore->currentDoc() ? pCore->currentDoc()->getDocumentProperty(QStringLiteral("koracut:videotype")) : QString();
}

// Une valeur de cellule telle qu'un agent l'envoie : texte, nombre, booléen.
QString cellValue(const QJsonValue &value)
{
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("1") : QString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    return value.toString();
}

// Un média du chutier donné par son id devient son nom, lisible par la personne dans le tableau.
QString mediaValue(const QString &value)
{
    if (value.isEmpty() || !pCore->projectItemModel()) {
        return value;
    }
    if (const auto clip = pCore->projectItemModel()->getClipByBinID(value)) {
        return clip->clipName();
    }
    return value;
}

// La séquence visée par l'appel : « sequence » (rang, nom ou uuid), sinon celle de la timeline de
// l'appel (la séquence active). Nulle et une erreur dans @p failure si rien ne correspond.
QUuid targetSequence(const Timeline &timeline, const Args &args, QJsonObject &failure)
{
    const QJsonValue given = args.value(QStringLiteral("sequence"));
    const QString reference = given.isDouble() ? QString::number(given.toInt()) : given.toString().trimmed();
    QUuid uuid;
    if (reference.isEmpty()) {
        uuid = timeline ? timeline->uuid() : KoraCutScriptStore::activeSequence();
    } else {
        uuid = KoraCutScriptStore::findSequence(reference);
    }
    if (uuid.isNull() || !pCore->currentDoc()->getTimeline(uuid, true)) {
        QStringList names;
        const QList<QUuid> all = KoraCutScriptStore::sequences();
        for (int i = 0; i < all.size(); ++i) {
            names << QStringLiteral("%1 : %2").arg(i + 1).arg(KoraCutScriptStore::sequenceName(all.at(i)));
        }
        failure = error(QStringLiteral("id_inconnu"), reference.isEmpty() ? QStringLiteral("Aucune séquence active.") : QStringLiteral("Aucune séquence ne correspond à « %1 ».").arg(reference),
                        QStringLiteral("sequence est un rang, un nom ou un uuid. Séquences du projet : %1.").arg(names.join(QStringLiteral(" ; "))));
        return QUuid();
    }
    return uuid;
}

// La séquence dont on vient de lire ou d'écrire le script, pour que l'agent sache laquelle.
QJsonObject sequenceJson(const QUuid &uuid)
{
    return QJsonObject{{QStringLiteral("uuid"), uuid.toString(QUuid::WithoutBraces)},
                       {QStringLiteral("nom"), KoraCutScriptStore::sequenceName(uuid)},
                       {QStringLiteral("rang"), int(KoraCutScriptStore::sequences().indexOf(uuid)) + 1},
                       {QStringLiteral("active"), uuid == KoraCutScriptStore::activeSequence()}};
}

// ---------------------------------------------------------------------------------------------
// script_lire

QJsonObject scriptLire(const Timeline &timeline, const Args &args)
{
    if (!pCore->currentDoc()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    QJsonObject failure;
    const QUuid sequence = targetSequence(timeline, args, failure);
    if (sequence.isNull()) {
        return failure;
    }
    const QString format = args.value(QStringLiteral("format")).toString(QStringLiteral("json"));
    if (format != QLatin1String("json") && format != QLatin1String("markdown") && format != QLatin1String("les_deux")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("format vaut « json », « markdown » ou « les_deux »."));
    }
    bool fromTemplate = true;
    const KoraCutScript script = KoraCutScriptStore::current(nullptr, &fromTemplate, sequence);
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("sequence"), sequenceJson(sequence)},
                       {QStringLiteral("enregistre"), !fromTemplate},
                       {QStringLiteral("type_video"), videoType()},
                       {QStringLiteral("resume"), summary(script)}};
    if (fromTemplate) {
        result.insert(QStringLiteral("remarque"), QStringLiteral("Cette séquence n'a pas encore de script : c'est le modèle du type de vidéo du projet. Il s'enregistre au premier script_ecrire."));
    }
    // Un script par séquence : les autres séquences du projet, pour savoir où sont les autres scripts.
    if (const QList<QUuid> all = KoraCutScriptStore::sequences(); all.size() > 1) {
        QJsonArray others;
        for (int i = 0; i < all.size(); ++i) {
            others.append(QJsonObject{{QStringLiteral("rang"), i + 1},
                                      {QStringLiteral("nom"), KoraCutScriptStore::sequenceName(all.at(i))},
                                      {QStringLiteral("enregistre"), KoraCutScriptStore::isStored(nullptr, all.at(i))}});
        }
        result.insert(QStringLiteral("sequences"), others);
    }
    if (const QString blockId = args.value(QStringLiteral("bloc_id")).toString(); !blockId.isEmpty()) {
        const KoraCutScriptBlock *b = script.block(blockId);
        if (!b) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun bloc n'a l'identifiant « %1 ».").arg(blockId), QStringLiteral("Relis script_lire sans bloc_id."));
        }
        KoraCutScript one;
        one.blocks << *b;
        result.insert(QStringLiteral("bloc"), one.toJson().value(QStringLiteral("blocs")).toArray().first());
        return result;
    }
    if (format != QLatin1String("markdown")) {
        result.insert(QStringLiteral("script"), script.toJson());
    }
    if (format != QLatin1String("json")) {
        result.insert(QStringLiteral("markdown"), script.toMarkdown());
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// script_ecrire

struct Writer
{
    KoraCutScript script;
    std::shared_ptr<TimelineItemModel> timeline;
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool markersPlaced{false};
    QJsonArray created;
    QString failure;
    QString advice;
    QString code{QStringLiteral("entree_invalide")};

    bool fail(const QString &message, const QString &hint = QString(), const QString &errorCode = QStringLiteral("entree_invalide"))
    {
        failure = message;
        advice = hint;
        code = errorCode;
        return false;
    }

    KoraCutScriptBlock *block(const QJsonObject &op, bool mustBeTable = false)
    {
        const QString id = op.value(QStringLiteral("bloc_id")).toString();
        KoraCutScriptBlock *b = script.block(id);
        if (!b) {
            fail(QStringLiteral("Aucun bloc n'a l'identifiant « %1 ».").arg(id), QStringLiteral("Relis script_lire : les identifiants sont dans le champ id de chaque bloc."),
                 QStringLiteral("id_inconnu"));
            return nullptr;
        }
        if (mustBeTable && b->kind != KoraCutScriptBlock::Kind::Table) {
            fail(QStringLiteral("Le bloc « %1 » n'est pas un tableau.").arg(id));
            return nullptr;
        }
        return b;
    }

    int row(KoraCutScriptBlock *table, const QJsonObject &op)
    {
        const QString id = op.value(QStringLiteral("ligne_id")).toString();
        const int r = table->rowIndex(id);
        if (r < 0) {
            fail(QStringLiteral("Le tableau « %1 » n'a pas de ligne « %2 ».").arg(table->id, id), QStringLiteral("Relis script_lire (champ lignes[].id)."),
                 QStringLiteral("id_inconnu"));
        }
        return r;
    }

    int column(KoraCutScriptBlock *table, const QString &ref)
    {
        const int c = table->columnIndex(ref);
        if (c < 0) {
            QStringList names;
            for (const KoraCutScriptColumn &col : table->columns) {
                names << QStringLiteral("%1 (%2)").arg(col.title, col.id);
            }
            fail(QStringLiteral("Le tableau « %1 » n'a pas de colonne « %2 ».").arg(table->id, ref), QStringLiteral("Colonnes : %1.").arg(names.join(QStringLiteral(", "))),
                 QStringLiteral("id_inconnu"));
        }
        return c;
    }

    bool setCells(KoraCutScriptBlock *table, int r, const QJsonObject &cells)
    {
        for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
            const int c = column(table, it.key());
            if (c < 0) {
                return false;
            }
            const KoraCutScriptColumn &col = table->columns.at(c);
            QString value = KoraCutScriptBlock::normalizedValue(col.type, cellValue(it.value()));
            if (col.type == QLatin1String("media")) {
                value = mediaValue(value);
            }
            if (value.isEmpty()) {
                table->rows[r].cells.remove(col.id);
            } else {
                table->rows[r].cells.insert(col.id, value);
            }
        }
        return true;
    }

    // La position d'insertion : « position » (rang), sinon « apres » ou « avant » un identifiant, sinon la fin.
    int insertionIndex(const QJsonObject &op, const std::function<int(const QString &)> &indexOf, int count)
    {
        if (op.contains(QStringLiteral("position"))) {
            return std::clamp(op.value(QStringLiteral("position")).toInt(count), 0, count);
        }
        if (op.contains(QStringLiteral("apres"))) {
            const int i = indexOf(op.value(QStringLiteral("apres")).toString());
            if (i < 0) {
                fail(QStringLiteral("« apres » vise un identifiant inconnu : %1.").arg(op.value(QStringLiteral("apres")).toString()), QString(), QStringLiteral("id_inconnu"));
                return -2;
            }
            return i + 1;
        }
        if (op.contains(QStringLiteral("avant"))) {
            const int i = indexOf(op.value(QStringLiteral("avant")).toString());
            if (i < 0) {
                fail(QStringLiteral("« avant » vise un identifiant inconnu : %1.").arg(op.value(QStringLiteral("avant")).toString()), QString(), QStringLiteral("id_inconnu"));
                return -2;
            }
            return i;
        }
        return count;
    }

    bool fillBlock(KoraCutScriptBlock &b, const QJsonObject &op)
    {
        if (op.contains(QStringLiteral("texte"))) {
            b.text = op.value(QStringLiteral("texte")).toString();
        }
        if (op.contains(QStringLiteral("titre")) && b.kind == KoraCutScriptBlock::Kind::Table) {
            b.text = op.value(QStringLiteral("titre")).toString();
        }
        if (op.contains(QStringLiteral("niveau"))) {
            b.level = std::clamp(op.value(QStringLiteral("niveau")).toInt(2), 1, 3);
        }
        if (op.contains(QStringLiteral("elements"))) {
            b.items.clear();
            for (const QJsonValue &v : op.value(QStringLiteral("elements")).toArray()) {
                b.items << v.toString();
            }
        }
        return true;
    }

    bool addColumns(KoraCutScriptBlock &table, const QJsonArray &columns)
    {
        for (const QJsonValue &v : columns) {
            const QJsonObject c = v.toObject();
            const QString type = c.value(QStringLiteral("type")).toString(QStringLiteral("texte"));
            if (!KoraCutScript::columnTypes().contains(type)) {
                return fail(QStringLiteral("Type de colonne inconnu : « %1 ».").arg(type), QStringLiteral("Types : texte, duree, media, case, choix."));
            }
            QStringList choices;
            for (const QJsonValue &ch : c.value(QStringLiteral("choix")).toArray()) {
                choices << ch.toString();
            }
            const QString id = table.addColumn(c.value(QStringLiteral("titre")).toString(), type, choices);
            if (const QString wanted = c.value(QStringLiteral("id")).toString(); !wanted.isEmpty() && table.columnIndex(wanted) < 0) {
                table.columns.last().id = wanted;
            }
            Q_UNUSED(id)
        }
        return true;
    }

    bool addRows(KoraCutScriptBlock *table, const QJsonArray &rows, int at)
    {
        for (const QJsonValue &v : rows) {
            const QJsonObject r = v.toObject();
            const QString id = table->addRow(at);
            const int index = table->rowIndex(id);
            if (const QString wanted = r.value(QStringLiteral("id")).toString(); !wanted.isEmpty() && table->rowIndex(wanted) < 0) {
                table->rows[index].id = wanted;
            }
            created.append(QJsonObject{{QStringLiteral("ligne_id"), table->rows.at(index).id}, {QStringLiteral("bloc_id"), table->id}});
            if (!setCells(table, index, r.value(QStringLiteral("cellules")).toObject())) {
                return false;
            }
            if (at >= 0) {
                ++at;
            }
        }
        return true;
    }

    bool run(const QJsonObject &op)
    {
        const QString verb = op.value(QStringLiteral("faire")).toString();
        if (verb == QLatin1String("ajouter_bloc")) {
            KoraCutScriptBlock::Kind kind;
            if (!KoraCutScript::kindFromName(op.value(QStringLiteral("type")).toString(), kind)) {
                return fail(QStringLiteral("Type de bloc inconnu : « %1 ».").arg(op.value(QStringLiteral("type")).toString()),
                            QStringLiteral("Types : titre, paragraphe, liste, tableau, separateur."));
            }
            KoraCutScriptBlock b;
            if (kind == KoraCutScriptBlock::Kind::Table) {
                // Sans colonnes données : les sept colonnes du modèle (Scène, Durée, Voix ou texte…).
                b = op.contains(QStringLiteral("colonnes")) ? KoraCutScriptBlock() : KoraCutScript::newScenesTable(QString());
                b.kind = KoraCutScriptBlock::Kind::Table;
                if (op.contains(QStringLiteral("colonnes")) && !addColumns(b, op.value(QStringLiteral("colonnes")).toArray())) {
                    return false;
                }
            } else {
                b.kind = kind;
                b.level = 2;
            }
            b.id = op.value(QStringLiteral("id")).toString();
            fillBlock(b, op);
            const int at = insertionIndex(op, [this](const QString &id) { return script.indexOf(id); }, int(script.blocks.size()));
            if (at == -2) {
                return false;
            }
            const QString id = script.insertBlock(b, at);
            created.append(QJsonObject{{QStringLiteral("bloc_id"), id}, {QStringLiteral("type"), KoraCutScript::kindName(kind)}});
            if (kind == KoraCutScriptBlock::Kind::Table && op.contains(QStringLiteral("lignes"))) {
                return addRows(script.block(id), op.value(QStringLiteral("lignes")).toArray(), -1);
            }
            return true;
        }
        if (verb == QLatin1String("modifier_bloc")) {
            KoraCutScriptBlock *b = block(op);
            return b && fillBlock(*b, op);
        }
        if (verb == QLatin1String("supprimer_bloc")) {
            KoraCutScriptBlock *b = block(op);
            return b && script.removeBlock(b->id);
        }
        if (verb == QLatin1String("deplacer_bloc")) {
            KoraCutScriptBlock *b = block(op);
            if (!b) {
                return false;
            }
            const int to = std::clamp(op.value(QStringLiteral("position")).toInt(0), 0, int(script.blocks.size()) - 1);
            return script.moveBlock(b->id, to);
        }
        if (verb == QLatin1String("ajouter_ligne")) {
            KoraCutScriptBlock *table = block(op, true);
            if (!table) {
                return false;
            }
            const int at = insertionIndex(op, [table](const QString &id) { return table->rowIndex(id); }, int(table->rows.size()));
            if (at == -2) {
                return false;
            }
            QJsonObject one{{QStringLiteral("cellules"), op.value(QStringLiteral("cellules"))}};
            if (op.contains(QStringLiteral("id"))) {
                one.insert(QStringLiteral("id"), op.value(QStringLiteral("id")));
            }
            return addRows(table, QJsonArray{one}, at);
        }
        if (verb == QLatin1String("modifier_ligne") || verb == QLatin1String("modifier_cellule")) {
            KoraCutScriptBlock *table = block(op, true);
            const int r = table ? row(table, op) : -1;
            if (r < 0) {
                return false;
            }
            QJsonObject cells = op.value(QStringLiteral("cellules")).toObject();
            if (verb == QLatin1String("modifier_cellule")) {
                cells = QJsonObject{{op.value(QStringLiteral("colonne")).toString(), op.value(QStringLiteral("valeur"))}};
            }
            return setCells(table, r, cells);
        }
        if (verb == QLatin1String("supprimer_ligne")) {
            KoraCutScriptBlock *table = block(op, true);
            const int r = table ? row(table, op) : -1;
            if (r < 0) {
                return false;
            }
            table->rows.removeAt(r);
            return true;
        }
        if (verb == QLatin1String("deplacer_ligne")) {
            KoraCutScriptBlock *table = block(op, true);
            const int r = table ? row(table, op) : -1;
            if (r < 0) {
                return false;
            }
            table->moveRow(r, std::clamp(op.value(QStringLiteral("position")).toInt(0), 0, int(table->rows.size()) - 1));
            return true;
        }
        if (verb == QLatin1String("ajouter_colonne")) {
            KoraCutScriptBlock *table = block(op, true);
            if (!table) {
                return false;
            }
            const int before = int(table->columns.size());
            QJsonObject c = op;
            if (!addColumns(*table, QJsonArray{c})) {
                return false;
            }
            if (op.contains(QStringLiteral("position"))) {
                table->moveColumn(before, std::clamp(op.value(QStringLiteral("position")).toInt(before), 0, before));
            }
            const QString id = op.value(QStringLiteral("id")).toString();
            created.append(QJsonObject{{QStringLiteral("colonne_id"), table->columnIndex(id) >= 0 && !id.isEmpty() ? id : table->columns.at(before).id},
                                       {QStringLiteral("bloc_id"), table->id}});
            return true;
        }
        if (verb == QLatin1String("modifier_colonne")) {
            KoraCutScriptBlock *table = block(op, true);
            const int c = table ? column(table, op.value(QStringLiteral("colonne")).toString()) : -1;
            if (c < 0) {
                return false;
            }
            KoraCutScriptColumn &col = table->columns[c];
            if (op.contains(QStringLiteral("titre"))) {
                col.title = op.value(QStringLiteral("titre")).toString();
            }
            if (op.contains(QStringLiteral("choix"))) {
                col.choices.clear();
                for (const QJsonValue &ch : op.value(QStringLiteral("choix")).toArray()) {
                    col.choices << ch.toString();
                }
            }
            if (op.contains(QStringLiteral("type"))) {
                const QString type = op.value(QStringLiteral("type")).toString();
                if (!KoraCutScript::columnTypes().contains(type)) {
                    return fail(QStringLiteral("Type de colonne inconnu : « %1 ».").arg(type), QStringLiteral("Types : texte, duree, media, case, choix."));
                }
                col.type = type;
                for (KoraCutScriptRow &r : table->rows) {
                    if (r.cells.contains(col.id)) {
                        const QString v = KoraCutScriptBlock::normalizedValue(type, r.cells.value(col.id));
                        if (v.isEmpty()) {
                            r.cells.remove(col.id);
                        } else {
                            r.cells.insert(col.id, v);
                        }
                    }
                }
            }
            return true;
        }
        if (verb == QLatin1String("supprimer_colonne")) {
            KoraCutScriptBlock *table = block(op, true);
            const int c = table ? column(table, op.value(QStringLiteral("colonne")).toString()) : -1;
            if (c < 0) {
                return false;
            }
            table->removeColumn(c);
            return true;
        }
        if (verb == QLatin1String("relier_ligne")) {
            KoraCutScriptBlock *table = block(op, true);
            const int r = table ? row(table, op) : -1;
            if (r < 0) {
                return false;
            }
            KoraCutScriptRow &line = table->rows[r];
            if (op.value(QStringLiteral("delier")).toBool(false)) {
                line.marker = -1;
                line.zoneIn = line.zoneOut = -1;
                return true;
            }
            const double fps = projectFps();
            if (op.contains(QStringLiteral("debut")) || op.contains(QStringLiteral("fin"))) {
                const int in = KoraCutAgentTools::parseTime(op.value(QStringLiteral("debut")), fps);
                const int out = KoraCutAgentTools::parseTime(op.value(QStringLiteral("fin")), fps);
                if (in < 0 || out <= in) {
                    return fail(QStringLiteral("La zone demande debut et fin, fin après debut (fin exclue)."), QStringLiteral("Images ou timecodes HH:MM:SS:II."));
                }
                line.zoneIn = in;
                line.zoneOut = out;
            }
            if (op.contains(QStringLiteral("marqueur"))) {
                const int frame = KoraCutAgentTools::parseTime(op.value(QStringLiteral("marqueur")), fps);
                if (frame < 0) {
                    return fail(QStringLiteral("marqueur est un nombre d'images ou un timecode HH:MM:SS:II."));
                }
                line.marker = frame;
                if (op.value(QStringLiteral("poser_marqueur")).toBool(false)) {
                    std::shared_ptr<MarkerListModel> guides = timeline ? timeline->getGuideModel() : nullptr;
                    if (!guides) {
                        return fail(QStringLiteral("Aucune séquence ouverte pour poser le marqueur."), QString(), QStringLiteral("non_disponible"));
                    }
                    const GenTime position(frame, fps);
                    if (!guides->hasMarker(position)) {
                        QString comment = table->rowSummary(r).simplified().left(80);
                        if (comment.isEmpty()) {
                            comment = QStringLiteral("Scène %1").arg(r + 1);
                        }
                        int category = 0;
                        if (!pCore->markerTypes.contains(category) && !pCore->markerTypes.isEmpty()) {
                            category = pCore->markerTypes.firstKey();
                        }
                        if (!guides->addRangeMarkerWithUndo(position, GenTime(), comment, category, undo, redo)) {
                            return fail(QStringLiteral("Le marqueur n'a pas pu être posé."), QString(), QStringLiteral("non_disponible"));
                        }
                        markersPlaced = true;
                    }
                }
            } else if (op.value(QStringLiteral("poser_marqueur")).toBool(false)) {
                return fail(QStringLiteral("poser_marqueur demande marqueur (la position)."));
            }
            return true;
        }
        return fail(QStringLiteral("Opération inconnue : « %1 ».").arg(verb),
                    QStringLiteral("faire vaut ajouter_bloc, modifier_bloc, supprimer_bloc, deplacer_bloc, ajouter_ligne, modifier_ligne, modifier_cellule, "
                                   "supprimer_ligne, deplacer_ligne, ajouter_colonne, modifier_colonne, supprimer_colonne ou relier_ligne."));
    }
};

QJsonObject scriptEcrire(const Timeline &timeline, const Args &args)
{
    if (!pCore->currentDoc()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    QJsonObject failure;
    const QUuid sequence = targetSequence(timeline, args, failure);
    if (sequence.isNull()) {
        return failure;
    }
    const bool replace = args.contains(QStringLiteral("script"));
    const QJsonArray operations = args.value(QStringLiteral("operations")).toArray();
    if (!replace && operations.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Donne script (tout remplacer) ou operations (modifier)."),
                     QStringLiteral("Pour une cellule : operations = [{ faire: \"modifier_cellule\", bloc_id, ligne_id, colonne, valeur }]."));
    }
    if (operations.size() > kMaxOperations) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Au plus %1 opérations par appel.").arg(kMaxOperations));
    }
    Writer w;
    // Les marqueurs de relier_ligne vont dans la séquence du script, pas dans la séquence active.
    w.timeline = pCore->currentDoc()->getTimeline(sequence, true);
    if (replace) {
        if (!args.value(QStringLiteral("script")).isObject()) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("script est un objet { blocs: [...] }, comme script_lire le rend."));
        }
        QString problem;
        w.script = KoraCutScript::fromJson(args.value(QStringLiteral("script")).toObject(), &problem);
        if (!problem.isEmpty() && problem.startsWith(QStringLiteral("Type de bloc"))) {
            return error(QStringLiteral("entree_invalide"), problem, QStringLiteral("Types : titre, paragraphe, liste, tableau, separateur."));
        }
        if (w.script.typeId.isEmpty()) {
            w.script.typeId = videoType();
        }
    } else {
        w.script = KoraCutScriptStore::current(nullptr, nullptr, sequence);
    }
    for (int i = 0; i < operations.size(); ++i) {
        if (!w.run(operations.at(i).toObject())) {
            // Rien n'est gardé : les marqueurs déjà posés par cet appel sont retirés.
            if (w.markersPlaced) {
                w.undo();
            }
            return error(w.code, QStringLiteral("Opération %1 : %2").arg(i + 1).arg(w.failure), w.advice);
        }
    }
    const QString undoText = replace ? i18nc("@action undo text", "Script: rewrite") : i18nc("@action undo text", "Script: edit");
    if (!KoraCutScriptStore::apply(w.script, undoText, QString(), w.markersPlaced ? w.undo : Fun(), w.markersPlaced ? w.redo : Fun(), sequence)) {
        if (w.markersPlaced) {
            w.undo();
        }
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le script n'a pas pu être enregistré."));
    }
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("sequence"), sequenceJson(sequence)}, {QStringLiteral("resume"), summary(w.script)}};
    if (!w.created.isEmpty()) {
        result.insert(QStringLiteral("crees"), w.created);
    }
    if (args.value(QStringLiteral("rendre_script")).toBool(false)) {
        result.insert(QStringLiteral("script"), w.script.toJson());
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// script_modele

QJsonObject scriptModele(const Timeline &timeline, const Args &args)
{
    if (!pCore->currentDoc()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    QJsonObject failure;
    const QUuid sequence = targetSequence(timeline, args, failure);
    if (sequence.isNull()) {
        return failure;
    }
    QString type = args.value(QStringLiteral("type_video")).toString();
    if (type.isEmpty()) {
        type = videoType();
    } else if (!KoraCutScript::hasTemplate(type)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun type de vidéo « %1 » dans le guide.").arg(type),
                     QStringLiteral("Les identifiants sont de la forme F04-13 ; laisse type_video vide pour le type du projet."));
    }
    if (KoraCutScriptStore::isStored(nullptr, sequence) && !args.value(QStringLiteral("remplacer")).toBool(false)) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("La séquence « %1 » a déjà un script.").arg(KoraCutScriptStore::sequenceName(sequence)),
                     QStringLiteral("Lis-le avec script_lire et modifie-le avec script_ecrire ; remplacer = true efface tout (un Annuler le rend)."));
    }
    const KoraCutScript script = KoraCutScript::templateFor(type);
    if (!KoraCutScriptStore::apply(script, i18nc("@action undo text", "Script: put back the template"), QString(), Fun(), Fun(), sequence)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le script n'a pas pu être enregistré."));
    }
    const KoraCutVideoTypes::VideoType *videoTypeInfo = KoraCutVideoTypes::byId(type);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("sequence"), sequenceJson(sequence)},
                       {QStringLiteral("type_video"), type},
                       {QStringLiteral("nom_du_type"), videoTypeInfo ? videoTypeInfo->name : QStringLiteral("générique")},
                       {QStringLiteral("resume"), summary(script)},
                       {QStringLiteral("script"), script.toJson()}};
}

} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> scriptTools()
{
    return {
        {QStringLiteral("script_lire"), scriptLire},
        {QStringLiteral("script_ecrire"), scriptEcrire},
        {QStringLiteral("script_modele"), scriptModele},
    };
}

} // namespace KoraCutAgentToolsPrivate
