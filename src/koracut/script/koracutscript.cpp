/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutscript.h"

#include "bin/model/markerlistmodel.hpp"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutvideotypes.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QUndoCommand>

#include <algorithm>
#include <tuple>

const QString KoraCutScript::PropertyName = QStringLiteral("koracut:script");

namespace {

// Les noms des blocs et des types de colonnes, tels que le JSON et les agents les écrivent.
const QString kHeading = QStringLiteral("titre");
const QString kParagraph = QStringLiteral("paragraphe");
const QString kList = QStringLiteral("liste");
const QString kTable = QStringLiteral("tableau");
const QString kDivider = QStringLiteral("separateur");

QString plain(const QString &text)
{
    return KoraCutVideoTypes::searchable(text).simplified();
}

bool truthy(const QString &value)
{
    const QString v = plain(value);
    return v == QLatin1String("1") || v == QLatin1String("x") || v == QLatin1String("[x]") || v == QLatin1String("oui") || v == QLatin1String("yes") ||
           v == QLatin1String("true") || v == QLatin1String("vrai") || v == QLatin1String("fait") || v == QLatin1String("done") ||
           value == QStringLiteral("✓") || value == QStringLiteral("✔");
}

// Les modèles, lus une fois dans la ressource.
const QJsonObject &templates()
{
    static const QJsonObject data = []() {
        QFile file(QStringLiteral(":data/koracut-script-templates.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return QJsonObject();
        }
        return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("types")).toObject();
    }();
    return data;
}

KoraCutScriptBlock heading(const QString &id, const QString &text, int level)
{
    KoraCutScriptBlock b;
    b.id = id;
    b.kind = KoraCutScriptBlock::Kind::Heading;
    b.text = text;
    b.level = level;
    return b;
}

KoraCutScriptBlock paragraph(const QString &id, const QString &text)
{
    KoraCutScriptBlock b;
    b.id = id;
    b.kind = KoraCutScriptBlock::Kind::Paragraph;
    b.text = text;
    return b;
}

KoraCutScriptBlock list(const QString &id, const QStringList &items)
{
    KoraCutScriptBlock b;
    b.id = id;
    b.kind = KoraCutScriptBlock::Kind::List;
    b.items = items;
    return b;
}

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &v : value.toArray()) {
        out << v.toString();
    }
    return out;
}

// Une cellule pour Markdown : pas de barre verticale, pas de retour à la ligne.
QString markdownCell(const QString &text)
{
    QString t = text;
    t.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    t.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return t.trimmed();
}

QString csvField(const QString &text)
{
    if (text.contains(QLatin1Char(',')) || text.contains(QLatin1Char('"')) || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char(';'))) {
        QString t = text;
        t.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + t + QLatin1Char('"');
    }
    return text;
}

QString cellText(const KoraCutScriptColumn &column, const QString &value)
{
    if (column.type == QLatin1String("case")) {
        return value.isEmpty() ? QString() : QStringLiteral("x");
    }
    return value;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Les tableaux

int KoraCutScriptBlock::columnIndex(const QString &idOrTitle) const
{
    for (int i = 0; i < columns.size(); ++i) {
        if (columns.at(i).id == idOrTitle) {
            return i;
        }
    }
    const QString wanted = plain(idOrTitle);
    for (int i = 0; i < columns.size(); ++i) {
        if (plain(columns.at(i).title) == wanted) {
            return i;
        }
    }
    return -1;
}

int KoraCutScriptBlock::rowIndex(const QString &rowId) const
{
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).id == rowId) {
            return i;
        }
    }
    return -1;
}

QString KoraCutScriptBlock::addColumn(const QString &title, const QString &type, const QStringList &choices, int at)
{
    KoraCutScriptColumn c;
    c.id = KoraCutScript::newId(QStringLiteral("c"));
    c.title = title;
    c.type = KoraCutScript::columnTypes().contains(type) ? type : QStringLiteral("texte");
    c.choices = choices;
    if (at < 0 || at > columns.size()) {
        columns.append(c);
    } else {
        columns.insert(at, c);
    }
    return c.id;
}

QString KoraCutScriptBlock::addRow(int at)
{
    KoraCutScriptRow r;
    r.id = KoraCutScript::newId(QStringLiteral("r"));
    if (at < 0 || at > rows.size()) {
        rows.append(r);
    } else {
        rows.insert(at, r);
    }
    return r.id;
}

QString KoraCutScriptBlock::duplicateRow(int row)
{
    if (row < 0 || row >= rows.size()) {
        return QString();
    }
    KoraCutScriptRow copy = rows.at(row);
    copy.id = KoraCutScript::newId(QStringLiteral("r"));
    // Une copie n'est reliée à rien : deux scènes sur le même marqueur prêteraient à confusion.
    copy.marker = -1;
    copy.zoneIn = copy.zoneOut = -1;
    rows.insert(row + 1, copy);
    return copy.id;
}

void KoraCutScriptBlock::moveRow(int from, int to)
{
    if (from < 0 || from >= rows.size() || to < 0 || to >= rows.size() || from == to) {
        return;
    }
    rows.move(from, to);
}

void KoraCutScriptBlock::moveColumn(int from, int to)
{
    if (from < 0 || from >= columns.size() || to < 0 || to >= columns.size() || from == to) {
        return;
    }
    columns.move(from, to);
}

void KoraCutScriptBlock::removeColumn(int column)
{
    if (column < 0 || column >= columns.size()) {
        return;
    }
    const QString id = columns.at(column).id;
    columns.removeAt(column);
    for (KoraCutScriptRow &r : rows) {
        r.cells.remove(id);
    }
}

QString KoraCutScriptBlock::normalizedValue(const QString &type, const QString &value)
{
    if (type == QLatin1String("case")) {
        return truthy(value) ? QStringLiteral("1") : QString();
    }
    return value;
}

QString KoraCutScriptBlock::rowSummary(int row) const
{
    if (row < 0 || row >= rows.size()) {
        return QString();
    }
    for (const KoraCutScriptColumn &c : columns) {
        if (c.type == QLatin1String("texte")) {
            const QString v = rows.at(row).cells.value(c.id).trimmed();
            if (!v.isEmpty()) {
                return v;
            }
        }
    }
    return QString();
}

// ---------------------------------------------------------------------------------------------
// Le document

int KoraCutScript::indexOf(const QString &blockId) const
{
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks.at(i).id == blockId) {
            return i;
        }
    }
    return -1;
}

KoraCutScriptBlock *KoraCutScript::block(const QString &blockId)
{
    const int i = indexOf(blockId);
    return i < 0 ? nullptr : &blocks[i];
}

const KoraCutScriptBlock *KoraCutScript::block(const QString &blockId) const
{
    const int i = indexOf(blockId);
    return i < 0 ? nullptr : &blocks.at(i);
}

KoraCutScriptBlock *KoraCutScript::firstTable()
{
    for (KoraCutScriptBlock &b : blocks) {
        if (b.kind == KoraCutScriptBlock::Kind::Table) {
            return &b;
        }
    }
    return nullptr;
}

int KoraCutScript::tableCount() const
{
    return int(std::count_if(blocks.cbegin(), blocks.cend(), [](const KoraCutScriptBlock &b) { return b.kind == KoraCutScriptBlock::Kind::Table; }));
}

QString KoraCutScript::insertBlock(KoraCutScriptBlock block, int at)
{
    if (block.id.isEmpty() || indexOf(block.id) >= 0) {
        block.id = newId(QStringLiteral("b"));
    }
    if (at < 0 || at > blocks.size()) {
        blocks.append(block);
    } else {
        blocks.insert(at, block);
    }
    return block.id;
}

bool KoraCutScript::removeBlock(const QString &blockId)
{
    const int i = indexOf(blockId);
    if (i < 0) {
        return false;
    }
    blocks.removeAt(i);
    return true;
}

bool KoraCutScript::moveBlock(const QString &blockId, int to)
{
    const int i = indexOf(blockId);
    if (i < 0 || to < 0 || to >= blocks.size()) {
        return false;
    }
    blocks.move(i, to);
    return true;
}

QString KoraCutScript::duplicateBlock(const QString &blockId)
{
    const int i = indexOf(blockId);
    if (i < 0) {
        return QString();
    }
    KoraCutScriptBlock copy = blocks.at(i);
    copy.id = newId(QStringLiteral("b"));
    QHash<QString, QString> columnIds;
    for (KoraCutScriptColumn &c : copy.columns) {
        const QString fresh = newId(QStringLiteral("c"));
        columnIds.insert(c.id, fresh);
        c.id = fresh;
    }
    for (KoraCutScriptRow &r : copy.rows) {
        r.id = newId(QStringLiteral("r"));
        r.marker = -1;
        r.zoneIn = r.zoneOut = -1;
        QMap<QString, QString> cells;
        for (auto it = r.cells.cbegin(); it != r.cells.cend(); ++it) {
            cells.insert(columnIds.value(it.key(), it.key()), it.value());
        }
        r.cells = cells;
    }
    blocks.insert(i + 1, copy);
    return copy.id;
}

// ---------------------------------------------------------------------------------------------
// JSON

QJsonObject KoraCutScript::toJson() const
{
    QJsonArray out;
    for (const KoraCutScriptBlock &b : blocks) {
        QJsonObject o{{QStringLiteral("id"), b.id}, {QStringLiteral("type"), kindName(b.kind)}};
        switch (b.kind) {
        case KoraCutScriptBlock::Kind::Heading:
            o.insert(QStringLiteral("texte"), b.text);
            o.insert(QStringLiteral("niveau"), b.level);
            break;
        case KoraCutScriptBlock::Kind::Paragraph:
            o.insert(QStringLiteral("texte"), b.text);
            break;
        case KoraCutScriptBlock::Kind::List:
            o.insert(QStringLiteral("elements"), QJsonArray::fromStringList(b.items));
            break;
        case KoraCutScriptBlock::Kind::Table: {
            o.insert(QStringLiteral("titre"), b.text);
            QJsonArray columns;
            for (const KoraCutScriptColumn &c : b.columns) {
                QJsonObject col{{QStringLiteral("id"), c.id}, {QStringLiteral("titre"), c.title}, {QStringLiteral("type"), c.type}};
                if (c.type == QLatin1String("choix")) {
                    col.insert(QStringLiteral("choix"), QJsonArray::fromStringList(c.choices));
                }
                columns.append(col);
            }
            o.insert(QStringLiteral("colonnes"), columns);
            QJsonArray rows;
            for (const KoraCutScriptRow &r : b.rows) {
                QJsonObject cells;
                for (const KoraCutScriptColumn &c : b.columns) {
                    const QString v = r.cells.value(c.id);
                    if (c.type == QLatin1String("case")) {
                        if (!v.isEmpty()) {
                            cells.insert(c.id, true);
                        }
                    } else if (!v.isEmpty()) {
                        cells.insert(c.id, v);
                    }
                }
                QJsonObject row{{QStringLiteral("id"), r.id}, {QStringLiteral("cellules"), cells}};
                if (r.marker >= 0) {
                    row.insert(QStringLiteral("marqueur"), r.marker);
                }
                if (r.zoneIn >= 0) {
                    row.insert(QStringLiteral("zone"), QJsonArray{r.zoneIn, r.zoneOut});
                }
                rows.append(row);
            }
            o.insert(QStringLiteral("lignes"), rows);
            break;
        }
        case KoraCutScriptBlock::Kind::Divider:
            break;
        }
        out.append(o);
    }
    QJsonObject root{{QStringLiteral("format"), FormatVersion}, {QStringLiteral("blocs"), out}};
    if (!typeId.isEmpty()) {
        root.insert(QStringLiteral("type_video"), typeId);
    }
    return root;
}

QString KoraCutScript::toCompactString() const
{
    return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}

KoraCutScript KoraCutScript::fromJson(const QJsonObject &json, QString *error)
{
    KoraCutScript s;
    const int format = json.value(QStringLiteral("format")).toInt(FormatVersion);
    if (format > FormatVersion && error) {
        // Un script écrit par une KoraCut plus récente : on lit ce qu'on connaît, on le dit.
        *error = QStringLiteral("Le script vient d'une version plus récente (format %1) : ce qui est inconnu est ignoré.").arg(format);
    }
    s.typeId = json.value(QStringLiteral("type_video")).toString();
    for (const QJsonValue &v : json.value(QStringLiteral("blocs")).toArray()) {
        const QJsonObject o = v.toObject();
        KoraCutScriptBlock b;
        if (!kindFromName(o.value(QStringLiteral("type")).toString(), b.kind)) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("Type de bloc inconnu : « %1 ».").arg(o.value(QStringLiteral("type")).toString());
            }
            continue;
        }
        b.id = o.value(QStringLiteral("id")).toString();
        if (b.id.isEmpty() || s.indexOf(b.id) >= 0) {
            b.id = newId(QStringLiteral("b"));
        }
        switch (b.kind) {
        case KoraCutScriptBlock::Kind::Heading:
            b.text = o.value(QStringLiteral("texte")).toString();
            b.level = std::clamp(o.value(QStringLiteral("niveau")).toInt(1), 1, 3);
            break;
        case KoraCutScriptBlock::Kind::Paragraph:
            b.text = o.value(QStringLiteral("texte")).toString();
            break;
        case KoraCutScriptBlock::Kind::List:
            b.items = stringList(o.value(QStringLiteral("elements")));
            break;
        case KoraCutScriptBlock::Kind::Table:
            b.text = o.value(QStringLiteral("titre")).toString();
            for (const QJsonValue &cv : o.value(QStringLiteral("colonnes")).toArray()) {
                const QJsonObject co = cv.toObject();
                KoraCutScriptColumn c;
                c.id = co.value(QStringLiteral("id")).toString();
                if (c.id.isEmpty() || b.columnIndex(c.id) >= 0) {
                    c.id = newId(QStringLiteral("c"));
                }
                c.title = co.value(QStringLiteral("titre")).toString();
                c.type = co.value(QStringLiteral("type")).toString(QStringLiteral("texte"));
                if (!columnTypes().contains(c.type)) {
                    c.type = QStringLiteral("texte");
                }
                c.choices = stringList(co.value(QStringLiteral("choix")));
                b.columns.append(c);
            }
            for (const QJsonValue &rv : o.value(QStringLiteral("lignes")).toArray()) {
                const QJsonObject ro = rv.toObject();
                KoraCutScriptRow r;
                r.id = ro.value(QStringLiteral("id")).toString();
                if (r.id.isEmpty() || b.rowIndex(r.id) >= 0) {
                    r.id = newId(QStringLiteral("r"));
                }
                const QJsonObject cells = ro.value(QStringLiteral("cellules")).toObject();
                for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
                    const int ci = b.columnIndex(it.key());
                    if (ci < 0) {
                        continue;
                    }
                    const KoraCutScriptColumn &c = b.columns.at(ci);
                    const QString value = it.value().isBool() ? (it.value().toBool() ? QStringLiteral("1") : QString())
                                          : it.value().isDouble() ? QString::number(it.value().toDouble())
                                                                  : it.value().toString();
                    r.cells.insert(c.id, KoraCutScriptBlock::normalizedValue(c.type, value));
                }
                r.marker = ro.value(QStringLiteral("marqueur")).toInt(-1);
                const QJsonArray zone = ro.value(QStringLiteral("zone")).toArray();
                if (zone.size() == 2) {
                    r.zoneIn = zone.at(0).toInt(-1);
                    r.zoneOut = zone.at(1).toInt(-1);
                }
                b.rows.append(r);
            }
            break;
        case KoraCutScriptBlock::Kind::Divider:
            break;
        }
        s.blocks.append(b);
    }
    return s;
}

KoraCutScript KoraCutScript::fromString(const QString &text, bool *ok)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (ok) {
        *ok = doc.isObject();
    }
    return doc.isObject() ? fromJson(doc.object()) : KoraCutScript();
}

// ---------------------------------------------------------------------------------------------
// Export et import

QString KoraCutScript::toMarkdown() const
{
    QStringList parts;
    for (const KoraCutScriptBlock &b : blocks) {
        switch (b.kind) {
        case KoraCutScriptBlock::Kind::Heading:
            parts << QString(b.level, QLatin1Char('#')) + QLatin1Char(' ') + b.text.trimmed();
            break;
        case KoraCutScriptBlock::Kind::Paragraph:
            if (!b.text.trimmed().isEmpty()) {
                parts << b.text.trimmed();
            }
            break;
        case KoraCutScriptBlock::Kind::List: {
            QStringList lines;
            for (const QString &item : b.items) {
                if (!item.trimmed().isEmpty()) {
                    lines << QStringLiteral("- ") + item.trimmed();
                }
            }
            if (!lines.isEmpty()) {
                parts << lines.join(QLatin1Char('\n'));
            }
            break;
        }
        case KoraCutScriptBlock::Kind::Table: {
            QStringList lines;
            if (!b.text.trimmed().isEmpty()) {
                lines << QStringLiteral("**%1**").arg(b.text.trimmed()) << QString();
            }
            QStringList header;
            QStringList rule;
            for (const KoraCutScriptColumn &c : b.columns) {
                header << markdownCell(c.title);
                rule << (c.type == QLatin1String("duree") ? QStringLiteral("---:") : c.type == QLatin1String("case") ? QStringLiteral(":---:") : QStringLiteral("---"));
            }
            if (header.isEmpty()) {
                break;
            }
            lines << QStringLiteral("| ") + header.join(QStringLiteral(" | ")) + QStringLiteral(" |");
            lines << QStringLiteral("| ") + rule.join(QStringLiteral(" | ")) + QStringLiteral(" |");
            for (const KoraCutScriptRow &r : b.rows) {
                QStringList cells;
                for (const KoraCutScriptColumn &c : b.columns) {
                    const QString v = r.cells.value(c.id);
                    cells << (c.type == QLatin1String("case") ? (v.isEmpty() ? QStringLiteral("[ ]") : QStringLiteral("[x]")) : markdownCell(v));
                }
                lines << QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |");
            }
            parts << lines.join(QLatin1Char('\n'));
            break;
        }
        case KoraCutScriptBlock::Kind::Divider:
            parts << QStringLiteral("---");
            break;
        }
    }
    return parts.join(QStringLiteral("\n\n")) + QLatin1Char('\n');
}

QString KoraCutScript::tableToCsv(const KoraCutScriptBlock &table)
{
    QStringList lines;
    QStringList header;
    for (const KoraCutScriptColumn &c : table.columns) {
        header << csvField(c.title);
    }
    lines << header.join(QLatin1Char(','));
    for (const KoraCutScriptRow &r : table.rows) {
        QStringList cells;
        for (const KoraCutScriptColumn &c : table.columns) {
            cells << csvField(cellText(c, r.cells.value(c.id)));
        }
        lines << cells.join(QLatin1Char(','));
    }
    return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

QList<QStringList> KoraCutScript::parseCsv(const QString &input)
{
    QString text = input;
    if (text.startsWith(QChar(0xFEFF))) {
        text.remove(0, 1);
    }
    // Le séparateur : celui qui revient le plus sur la première ligne (Excel en français écrit des « ; »).
    const QString firstLine = text.section(QLatin1Char('\n'), 0, 0);
    QChar separator = QLatin1Char(',');
    int best = int(firstLine.count(QLatin1Char(',')));
    for (const QChar candidate : {QLatin1Char(';'), QLatin1Char('\t')}) {
        const int n = int(firstLine.count(candidate));
        if (n > best) {
            best = n;
            separator = candidate;
        }
    }
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool fieldStarted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += ch;
            }
            continue;
        }
        if (ch == QLatin1Char('"') && !fieldStarted) {
            quoted = true;
            fieldStarted = true;
        } else if (ch == separator) {
            row << field;
            field.clear();
            fieldStarted = false;
        } else if (ch == QLatin1Char('\r')) {
            continue;
        } else if (ch == QLatin1Char('\n')) {
            row << field;
            field.clear();
            fieldStarted = false;
            if (!(row.size() == 1 && row.first().isEmpty())) {
                rows << row;
            }
            row.clear();
        } else {
            field += ch;
            fieldStarted = true;
        }
    }
    if (fieldStarted || !field.isEmpty() || !row.isEmpty()) {
        row << field;
        if (!(row.size() == 1 && row.first().isEmpty())) {
            rows << row;
        }
    }
    return rows;
}

int KoraCutScript::importCsv(KoraCutScriptBlock &table, const QString &csv, QString *error)
{
    const QList<QStringList> rows = parseCsv(csv);
    if (rows.isEmpty()) {
        if (error) {
            *error = i18nc("@info", "The file is empty.");
        }
        return 0;
    }
    // La première ligne donne les colonnes.
    QList<int> mapping;
    for (const QString &title : rows.first()) {
        const QString name = title.trimmed().isEmpty() ? i18nc("@title:column default name", "Column") : title.trimmed();
        int index = table.columnIndex(name);
        if (index < 0) {
            table.addColumn(name, QStringLiteral("texte"));
            index = int(table.columns.size()) - 1;
        }
        mapping << index;
    }
    int added = 0;
    for (int i = 1; i < rows.size(); ++i) {
        const QStringList &values = rows.at(i);
        bool empty = true;
        for (const QString &v : values) {
            empty = empty && v.trimmed().isEmpty();
        }
        if (empty) {
            continue;
        }
        KoraCutScriptRow r;
        r.id = newId(QStringLiteral("r"));
        for (int k = 0; k < values.size() && k < mapping.size(); ++k) {
            const KoraCutScriptColumn &c = table.columns.at(mapping.at(k));
            const QString v = KoraCutScriptBlock::normalizedValue(c.type, values.at(k).trimmed());
            if (!v.isEmpty()) {
                r.cells.insert(c.id, v);
            }
        }
        table.rows.append(r);
        ++added;
    }
    return added;
}

// ---------------------------------------------------------------------------------------------
// Blocs neufs et modèles

QString KoraCutScript::newId(const QString &prefix)
{
    // Court, lisible par un agent, et sans collision en pratique dans un même script.
    static const char alphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
    QString id = prefix;
    for (int i = 0; i < 6; ++i) {
        id += QLatin1Char(alphabet[QRandomGenerator::global()->bounded(int(sizeof(alphabet) - 1))]);
    }
    return id;
}

KoraCutScriptBlock KoraCutScript::newBlock(KoraCutScriptBlock::Kind kind)
{
    if (kind == KoraCutScriptBlock::Kind::Table) {
        return newScenesTable(i18nc("@title default name of a new script table", "Scenes"));
    }
    KoraCutScriptBlock b;
    b.id = newId(QStringLiteral("b"));
    b.kind = kind;
    b.level = 2;
    if (kind == KoraCutScriptBlock::Kind::List) {
        b.items << QString();
    }
    return b;
}

KoraCutScriptBlock KoraCutScript::newScenesTable(const QString &title)
{
    KoraCutScriptBlock b;
    b.id = newId(QStringLiteral("b"));
    b.kind = KoraCutScriptBlock::Kind::Table;
    b.text = title;
    // Les sept colonnes demandées (document 21), avec des identifiants stables pour les agents.
    const QList<std::tuple<QString, QString, QString>> columns = {
        {QStringLiteral("scene"), i18nc("@title:column script table", "Scene"), QStringLiteral("texte")},
        {QStringLiteral("duree"), i18nc("@title:column script table", "Duration"), QStringLiteral("duree")},
        {QStringLiteral("voix"), i18nc("@title:column script table", "Voice or text"), QStringLiteral("texte")},
        {QStringLiteral("image"), i18nc("@title:column script table", "On screen"), QStringLiteral("texte")},
        {QStringLiteral("son"), i18nc("@title:column script table", "Sound"), QStringLiteral("texte")},
        {QStringLiteral("plans"), i18nc("@title:column script table", "Shots to use"), QStringLiteral("media")},
        {QStringLiteral("fait"), i18nc("@title:column script table", "Done"), QStringLiteral("case")},
    };
    for (const auto &[id, title, type] : columns) {
        KoraCutScriptColumn c;
        c.id = id;
        c.title = title;
        c.type = type;
        b.columns.append(c);
    }
    return b;
}

bool KoraCutScript::hasTemplate(const QString &typeId)
{
    return !typeId.isEmpty() && templates().contains(typeId);
}

KoraCutScript KoraCutScript::templateFor(const QString &typeId)
{
    // Identifiants fixes : le modèle affiché avant la première modification est recalculé à chaque
    // lecture, un agent qui vise « deroule » ou « s2 » doit tomber sur la même chose.
    KoraCutScript s;
    KoraCutScriptBlock table = newScenesTable(i18nc("@title script table of a template", "Run-down"));
    table.id = QStringLiteral("deroule");
    auto addScene = [&table](const QString &scene, const QString &duration, const QString &image) {
        KoraCutScriptRow r;
        r.id = QStringLiteral("s%1").arg(table.rows.size() + 1);
        r.cells.insert(QStringLiteral("scene"), scene);
        if (!duration.isEmpty()) {
            r.cells.insert(QStringLiteral("duree"), duration);
        }
        if (!image.isEmpty()) {
            r.cells.insert(QStringLiteral("image"), image);
        }
        table.rows.append(r);
    };
    if (hasTemplate(typeId)) {
        const QJsonObject t = templates().value(typeId).toObject();
        s.typeId = typeId;
        s.blocks << heading(QStringLiteral("titre"), t.value(QStringLiteral("nom")).toString(), 1);
        const QString duration = t.value(QStringLiteral("duree")).toString();
        if (!duration.isEmpty()) {
            s.blocks << paragraph(QStringLiteral("duree"), i18nc("@info script template, %1 is the length advice from the guide", "Length: %1", duration));
        }
        const QStringList hook = stringList(t.value(QStringLiteral("accroche")));
        if (!hook.isEmpty()) {
            s.blocks << heading(QStringLiteral("accroche-titre"), i18nc("@title script template section", "Hook"), 2);
            s.blocks << list(QStringLiteral("accroche"), hook);
        }
        for (const QJsonValue &v : t.value(QStringLiteral("scenes")).toArray()) {
            const QJsonObject o = v.toObject();
            addScene(o.value(QStringLiteral("scene")).toString(), o.value(QStringLiteral("duree")).toString(), o.value(QStringLiteral("image")).toString());
        }
        s.blocks << table;
        const QStringList ending = stringList(t.value(QStringLiteral("fin")));
        if (!ending.isEmpty()) {
            s.blocks << heading(QStringLiteral("fin-titre"), i18nc("@title script template section", "Ending and call to action"), 2);
            s.blocks << list(QStringLiteral("fin"), ending);
        }
        return s;
    }
    // Sans type : le modèle générique, quatre temps.
    s.blocks << heading(QStringLiteral("titre"), i18nc("@title default script title", "Script"), 1);
    s.blocks << paragraph(QStringLiteral("intro"), i18nc("@info script template", "One line to say what this video is for and who it speaks to."));
    addScene(i18nc("@item script template scene", "Hook"), QStringLiteral("3 s"), i18nc("@item script template", "What stops the scroll in the first seconds."));
    addScene(i18nc("@item script template scene", "Promise"), QStringLiteral("5 s"), i18nc("@item script template", "What the viewer gets by staying."));
    addScene(i18nc("@item script template scene", "Development"), QString(), i18nc("@item script template", "The heart of the video, one idea per scene."));
    addScene(i18nc("@item script template scene", "Call to action"), QStringLiteral("5 s"), i18nc("@item script template", "One clear thing to do after watching."));
    s.blocks << table;
    return s;
}

QString KoraCutScript::kindName(KoraCutScriptBlock::Kind kind)
{
    switch (kind) {
    case KoraCutScriptBlock::Kind::Heading:
        return kHeading;
    case KoraCutScriptBlock::Kind::Paragraph:
        return kParagraph;
    case KoraCutScriptBlock::Kind::List:
        return kList;
    case KoraCutScriptBlock::Kind::Table:
        return kTable;
    case KoraCutScriptBlock::Kind::Divider:
        return kDivider;
    }
    return kParagraph;
}

bool KoraCutScript::kindFromName(const QString &name, KoraCutScriptBlock::Kind &kind)
{
    const QString n = plain(name);
    if (n == kHeading) {
        kind = KoraCutScriptBlock::Kind::Heading;
    } else if (n == kParagraph || n == QLatin1String("texte")) {
        kind = KoraCutScriptBlock::Kind::Paragraph;
    } else if (n == kList) {
        kind = KoraCutScriptBlock::Kind::List;
    } else if (n == kTable) {
        kind = KoraCutScriptBlock::Kind::Table;
    } else if (n == kDivider) {
        kind = KoraCutScriptBlock::Kind::Divider;
    } else {
        return false;
    }
    return true;
}

QStringList KoraCutScript::columnTypes()
{
    return {QStringLiteral("texte"), QStringLiteral("duree"), QStringLiteral("media"), QStringLiteral("case"), QStringLiteral("choix")};
}

QString KoraCutScript::columnTypeLabel(const QString &type)
{
    if (type == QLatin1String("duree")) {
        return i18nc("@item script column type", "Duration or timecode");
    }
    if (type == QLatin1String("media")) {
        return i18nc("@item script column type", "Shot or media");
    }
    if (type == QLatin1String("case")) {
        return i18nc("@item script column type", "Checkbox");
    }
    if (type == QLatin1String("choix")) {
        return i18nc("@item script column type", "Choice from a list");
    }
    return i18nc("@item script column type", "Text");
}

// ---------------------------------------------------------------------------------------------
// Le notificateur, la commande d'annulation, le magasin

KoraCutScriptNotifier *KoraCutScriptNotifier::instance()
{
    static KoraCutScriptNotifier notifier;
    return &notifier;
}

namespace {

// Une étape d'annulation du script : l'avant et l'après en entier (un script pèse quelques Ko).
// Les frappes qui se suivent dans la même cellule, à moins de 2,5 s, fusionnent.
class KoraCutScriptCommand : public QUndoCommand
{
public:
    KoraCutScriptCommand(KdenliveDoc *doc, QString before, QString after, const QString &text, QString mergeKey, Fun extraUndo, Fun extraRedo)
        : QUndoCommand(text)
        , m_doc(doc)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_key(std::move(mergeKey))
        , m_time(QDateTime::currentMSecsSinceEpoch())
        , m_extraUndo(std::move(extraUndo))
        , m_extraRedo(std::move(extraRedo))
    {
    }

    int id() const override { return m_key.isEmpty() ? -1 : 0x4b435343; }

    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *o = dynamic_cast<const KoraCutScriptCommand *>(other);
        if (!o || o->m_key != m_key || o->m_doc != m_doc || m_extraUndo || o->m_extraUndo || o->m_time - m_time > 2500) {
            return false;
        }
        m_after = o->m_after;
        m_time = o->m_time;
        setObsolete(m_after == m_before);
        return true;
    }

    void undo() override
    {
        if (m_extraUndo) {
            m_extraUndo();
        }
        write(m_before);
    }

    void redo() override
    {
        // Le premier redo vient de push() : l'état est déjà écrit (même règle que FunctionalUndoCommand).
        if (m_first) {
            m_first = false;
            return;
        }
        if (m_extraRedo) {
            m_extraRedo();
        }
        write(m_after);
    }

private:
    void write(const QString &value)
    {
        if (m_doc) {
            m_doc->setDocumentProperty(KoraCutScript::PropertyName, value);
        }
        Q_EMIT KoraCutScriptNotifier::instance()->changed();
    }

    QPointer<KdenliveDoc> m_doc;
    QString m_before;
    QString m_after;
    QString m_key;
    qint64 m_time;
    Fun m_extraUndo;
    Fun m_extraRedo;
    bool m_first{true};
};

} // namespace

namespace KoraCutScriptStore {

KoraCutScript current(KdenliveDoc *doc, bool *fromTemplate)
{
    if (!doc) {
        doc = pCore ? pCore->currentDoc() : nullptr;
    }
    const QString stored = doc ? doc->getDocumentProperty(KoraCutScript::PropertyName) : QString();
    bool ok = false;
    if (!stored.isEmpty()) {
        KoraCutScript s = KoraCutScript::fromString(stored, &ok);
        if (ok) {
            if (fromTemplate) {
                *fromTemplate = false;
            }
            return s;
        }
    }
    if (fromTemplate) {
        *fromTemplate = true;
    }
    return KoraCutScript::templateFor(doc ? doc->getDocumentProperty(QStringLiteral("koracut:videotype")) : QString());
}

bool isStored(KdenliveDoc *doc)
{
    if (!doc) {
        doc = pCore ? pCore->currentDoc() : nullptr;
    }
    return doc && !doc->getDocumentProperty(KoraCutScript::PropertyName).isEmpty();
}

bool apply(const KoraCutScript &after, const QString &undoText, const QString &mergeKey, const Fun &extraUndo, const Fun &extraRedo)
{
    KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    if (!doc) {
        return false;
    }
    const QString before = doc->getDocumentProperty(KoraCutScript::PropertyName);
    const QString value = after.toCompactString();
    if (before == value && !extraUndo) {
        return true;
    }
    doc->setDocumentProperty(KoraCutScript::PropertyName, value);
    pCore->pushUndo(new KoraCutScriptCommand(doc, before, value, undoText, mergeKey, extraUndo, extraRedo));
    Q_EMIT KoraCutScriptNotifier::instance()->changed();
    return true;
}

bool placeMarker(const std::shared_ptr<TimelineItemModel> &timeline, const QString &blockId, const QString &rowId, int frame, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (!timeline || frame < 0) {
        return fail(QStringLiteral("Aucune séquence ouverte."));
    }
    KoraCutScript script = current();
    KoraCutScriptBlock *table = script.block(blockId);
    const int row = table ? table->rowIndex(rowId) : -1;
    if (row < 0) {
        return fail(QStringLiteral("Cette ligne n'existe plus."));
    }
    std::shared_ptr<MarkerListModel> guides = timeline->getGuideModel();
    if (!guides) {
        return fail(QStringLiteral("Aucune liste de marqueurs ici."));
    }
    const GenTime position(frame, pCore->getCurrentFps());
    if (guides->hasMarker(position)) {
        // Un marqueur existe déjà à cette image : on relie la scène sans en poser un deuxième.
        table->rows[row].marker = frame;
        return apply(script, i18nc("@action undo text", "Script: link scene to marker"));
    }
    int category = 0;
    if (!pCore->markerTypes.contains(category) && !pCore->markerTypes.isEmpty()) {
        category = pCore->markerTypes.firstKey();
    }
    QString comment = table->rowSummary(row).simplified().left(80);
    if (comment.isEmpty()) {
        comment = i18nc("@info default marker text for a script scene, %1 is the scene number", "Scene %1", row + 1);
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!guides->addRangeMarkerWithUndo(position, GenTime(), comment, category, undo, redo)) {
        return fail(QStringLiteral("Le marqueur n'a pas pu être posé."));
    }
    table->rows[row].marker = frame;
    return apply(script, i18nc("@action undo text", "Script: place marker"), QString(), undo, redo);
}

} // namespace KoraCutScriptStore
