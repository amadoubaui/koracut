/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutmotionsync.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#include <cmath>

namespace KoraCutMotion {

namespace {

const QSet<QString> &voidTags()
{
    static const QSet<QString> tags = {QStringLiteral("img"),   QStringLiteral("br"),    QStringLiteral("hr"),    QStringLiteral("meta"),
                                       QStringLiteral("link"),  QStringLiteral("input"), QStringLiteral("source"), QStringLiteral("track"),
                                       QStringLiteral("wbr"),   QStringLiteral("area"),  QStringLiteral("base"),  QStringLiteral("col"),
                                       QStringLiteral("embed"), QStringLiteral("param")};
    return tags;
}

bool isNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char(':') || c == QLatin1Char('.');
}

QString decodeEntities(QString text)
{
    static const QRegularExpression entity(QStringLiteral("&(#x[0-9a-fA-F]+|#[0-9]+|[a-zA-Z]+);"));
    QString out;
    out.reserve(text.size());
    int last = 0;
    auto it = entity.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString name = m.captured(1);
        QString rep = m.captured(0);
        if (name.startsWith(QLatin1String("#x"))) {
            rep = QString(QChar(name.mid(2).toInt(nullptr, 16)));
        } else if (name.startsWith(QLatin1Char('#'))) {
            rep = QString(QChar(name.mid(1).toInt()));
        } else if (name == QLatin1String("amp")) {
            rep = QStringLiteral("&");
        } else if (name == QLatin1String("lt")) {
            rep = QStringLiteral("<");
        } else if (name == QLatin1String("gt")) {
            rep = QStringLiteral(">");
        } else if (name == QLatin1String("quot")) {
            rep = QStringLiteral("\"");
        } else if (name == QLatin1String("apos")) {
            rep = QStringLiteral("'");
        } else if (name == QLatin1String("nbsp")) {
            rep = QStringLiteral(" ");
        }
        out += rep;
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

/** L'index de la balise fermante qui va avec chaque balise ouvrante (-1 : élément vide ou auto-fermé). */
QVector<int> matchTags(const QList<Tag> &tags)
{
    QVector<int> close(tags.size(), -1);
    QList<int> stack;
    for (int i = 0; i < tags.size(); ++i) {
        const Tag &t = tags.at(i);
        if (t.closing) {
            // On remonte jusqu'à la balise ouvrante du même nom, en tolérant les oublis.
            for (int s = stack.size() - 1; s >= 0; --s) {
                if (tags.at(stack.at(s)).name == t.name) {
                    close[stack.at(s)] = i;
                    stack.erase(stack.begin() + s, stack.end());
                    break;
                }
            }
            continue;
        }
        if (t.selfClosing || voidTags().contains(t.name)) {
            continue;
        }
        stack.append(i);
    }
    return close;
}

int outerEndOf(const QString &html, const QList<Tag> &tags, const QVector<int> &close, int index)
{
    Q_UNUSED(html);
    return close.at(index) >= 0 ? tags.at(close.at(index)).end : tags.at(index).end;
}

/** Les nœuds de texte non vides à l'intérieur d'un élément : [début, fin[ de chaque bout, hors script et style. */
QList<QPair<int, int>> textNodes(const QString &html, const QList<Tag> &tags, const QVector<int> &close, int index)
{
    QList<QPair<int, int>> nodes;
    if (close.at(index) < 0) {
        return nodes;
    }
    int cursor = tags.at(index).end;
    const int stop = tags.at(close.at(index)).start;
    for (int i = index + 1; i < close.at(index); ++i) {
        const Tag &t = tags.at(i);
        if (t.start < cursor) {
            continue;
        }
        nodes.append({cursor, t.start});
        cursor = t.end;
        if (!t.closing && (t.name == QLatin1String("script") || t.name == QLatin1String("style")) && close.at(i) >= 0) {
            // Le contenu brut n'est pas du texte visible.
            cursor = tags.at(close.at(i)).end;
            i = close.at(i);
        }
    }
    if (cursor < stop) {
        nodes.append({cursor, stop});
    }
    QList<QPair<int, int>> kept;
    for (const auto &n : nodes) {
        if (!html.mid(n.first, n.second - n.first).trimmed().isEmpty()) {
            kept.append(n);
        }
    }
    return kept;
}

QString startTagWithoutTiming(const QString &html, const Tag &tag)
{
    QString out;
    int cursor = tag.start;
    for (const Attribute &a : tag.attributes) {
        if (a.name == QLatin1String("data-start") || a.name == QLatin1String("data-duration") || a.name == QLatin1String("data-track-index") ||
            a.name == QLatin1String("data-koracut-in")) {
            out += html.mid(cursor, a.start - cursor).trimmed().isEmpty() ? QStringLiteral(" ") : html.mid(cursor, a.start - cursor);
            cursor = a.end;
        }
    }
    out += html.mid(cursor, tag.end - cursor);
    return out;
}

QString sha1(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

} // namespace

// ---------------------------------------------------------------------------------------------

bool Tag::has(const QString &attr) const
{
    return find(attr) != nullptr;
}

QString Tag::attr(const QString &attr) const
{
    const Attribute *a = find(attr);
    return a ? a->value : QString();
}

const Attribute *Tag::find(const QString &attr) const
{
    for (const Attribute &a : attributes) {
        if (a.name == attr) {
            return &a;
        }
    }
    return nullptr;
}

QString Element::kindName() const
{
    switch (kind) {
    case Kind::Block:
        return QStringLiteral("bloc");
    case Kind::Image:
        return QStringLiteral("image");
    case Kind::Video:
        return QStringLiteral("video");
    case Kind::Audio:
        return QStringLiteral("audio");
    case Kind::Composition:
        return QStringLiteral("composition");
    case Kind::Whole:
        return QStringLiteral("entiere");
    }
    return QString();
}

QString Element::displayName() const
{
    if ((kind == Kind::Block || kind == Kind::Whole) && !text.isEmpty()) {
        return text.length() > 40 ? text.left(39) + QChar(0x2026) : text;
    }
    if (!src.isEmpty()) {
        return QFileInfo(src).fileName();
    }
    return id;
}

const Element *Composition::element(const QString &elementId) const
{
    for (const Element &e : elements) {
        if (e.id == elementId) {
            return &e;
        }
    }
    return nullptr;
}

double Composition::end() const
{
    double last = 0.;
    for (const Element &e : elements) {
        if (e.startResolved && e.durationKnown) {
            last = std::max(last, e.start + e.duration);
        }
    }
    if (last <= 0. && durationKnown) {
        return duration;
    }
    return last;
}

QList<Tag> tokenize(const QString &html)
{
    QList<Tag> tags;
    const int n = html.size();
    int i = 0;
    while (i < n) {
        const int lt = html.indexOf(QLatin1Char('<'), i);
        if (lt < 0) {
            break;
        }
        if (html.mid(lt, 4) == QLatin1String("<!--")) {
            const int end = html.indexOf(QLatin1String("-->"), lt + 4);
            i = end < 0 ? n : end + 3;
            continue;
        }
        if (lt + 1 < n && (html.at(lt + 1) == QLatin1Char('!') || html.at(lt + 1) == QLatin1Char('?'))) {
            const int end = html.indexOf(QLatin1Char('>'), lt);
            i = end < 0 ? n : end + 1;
            continue;
        }
        Tag tag;
        tag.start = lt;
        int p = lt + 1;
        if (p < n && html.at(p) == QLatin1Char('/')) {
            tag.closing = true;
            ++p;
        }
        int nameStart = p;
        while (p < n && isNameChar(html.at(p))) {
            ++p;
        }
        if (p == nameStart) {
            // Un « < » isolé (texte, formule) : on passe.
            i = lt + 1;
            continue;
        }
        tag.name = html.mid(nameStart, p - nameStart).toLower();
        if (tag.closing) {
            const int end = html.indexOf(QLatin1Char('>'), p);
            tag.end = end < 0 ? n : end + 1;
            tags.append(tag);
            i = tag.end;
            continue;
        }
        // Les attributs.
        while (p < n) {
            while (p < n && html.at(p).isSpace()) {
                ++p;
            }
            if (p >= n) {
                break;
            }
            if (html.at(p) == QLatin1Char('>')) {
                ++p;
                break;
            }
            if (html.at(p) == QLatin1Char('/')) {
                if (p + 1 < n && html.at(p + 1) == QLatin1Char('>')) {
                    tag.selfClosing = true;
                    p += 2;
                    break;
                }
                ++p;
                continue;
            }
            Attribute a;
            a.start = p;
            const int an = p;
            while (p < n && !html.at(p).isSpace() && html.at(p) != QLatin1Char('=') && html.at(p) != QLatin1Char('>') && html.at(p) != QLatin1Char('/')) {
                ++p;
            }
            a.name = html.mid(an, p - an).toLower();
            int q = p;
            while (q < n && html.at(q).isSpace()) {
                ++q;
            }
            if (q < n && html.at(q) == QLatin1Char('=')) {
                ++q;
                while (q < n && html.at(q).isSpace()) {
                    ++q;
                }
                if (q < n && (html.at(q) == QLatin1Char('"') || html.at(q) == QLatin1Char('\''))) {
                    const QChar quote = html.at(q);
                    a.valueStart = q + 1;
                    const int close = html.indexOf(quote, q + 1);
                    a.valueEnd = close < 0 ? n : close;
                    a.end = close < 0 ? n : close + 1;
                    a.quoted = true;
                } else {
                    a.valueStart = q;
                    while (q < n && !html.at(q).isSpace() && html.at(q) != QLatin1Char('>')) {
                        ++q;
                    }
                    a.valueEnd = q;
                    a.end = q;
                    a.quoted = false;
                }
                a.value = decodeEntities(html.mid(a.valueStart, a.valueEnd - a.valueStart));
                p = a.end;
            } else {
                a.valueStart = a.valueEnd = p;
                a.end = p;
                a.quoted = false;
            }
            tag.attributes.append(a);
        }
        tag.end = p;
        tags.append(tag);
        i = tag.end;
        if (!tag.selfClosing && (tag.name == QLatin1String("script") || tag.name == QLatin1String("style"))) {
            // Contenu brut jusqu'à la balise fermante.
            const int close = html.indexOf(QStringLiteral("</") + tag.name, tag.end, Qt::CaseInsensitive);
            i = close < 0 ? n : close;
        }
    }
    return tags;
}

QString visibleText(const QString &fragment)
{
    static const QRegularExpression rawBlocks(QStringLiteral("<(script|style)\\b[^>]*>.*?</\\1\\s*>"),
                                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    static const QRegularExpression blanks(QStringLiteral("\\s+"));
    QString text = fragment;
    text.remove(rawBlocks);
    text.remove(comments);
    text.replace(tags, QStringLiteral(" "));
    text = decodeEntities(text);
    text.replace(blanks, QStringLiteral(" "));
    return text.trimmed();
}

bool resolveStart(const QString &expr, const Composition &composition, double &seconds, QStringList *chain)
{
    const QString trimmed = expr.trimmed();
    bool ok = false;
    const double value = trimmed.toDouble(&ok);
    if (ok) {
        seconds = value;
        return true;
    }
    static const QRegularExpression relative(QStringLiteral("^([A-Za-z_][\\w.:-]*)\\s*(?:([+-])\\s*([0-9]*\\.?[0-9]+))?$"));
    const QRegularExpressionMatch m = relative.match(trimmed);
    if (!m.hasMatch()) {
        return false;
    }
    const QString ref = m.captured(1);
    QStringList localChain;
    QStringList &seen = chain ? *chain : localChain;
    if (seen.contains(ref)) {
        return false; // une boucle
    }
    const Element *target = composition.element(ref);
    if (!target || !target->durationKnown) {
        return false;
    }
    seen.append(ref);
    double targetStart = 0.;
    if (!resolveStart(target->startExpr, composition, targetStart, &seen)) {
        return false;
    }
    double offset = 0.;
    if (!m.captured(2).isEmpty()) {
        offset = m.captured(3).toDouble();
        if (m.captured(2) == QLatin1String("-")) {
            offset = -offset;
        }
    }
    seconds = targetStart + target->duration + offset;
    return true;
}

QString secondsText(double seconds)
{
    QString text = QString::number(std::round(seconds * 1000.) / 1000., 'f', 3);
    while (text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    if (text == QLatin1String("-0")) {
        text = QStringLiteral("0");
    }
    return text;
}

QString escapeText(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return out;
}

QString setAttribute(const QString &html, const Tag &tag, const QString &name, const QString &value)
{
    QString out = html;
    const Attribute *a = tag.find(name);
    if (a) {
        if (value.isEmpty()) {
            int from = a->start;
            while (from > tag.start + 1 && html.at(from - 1).isSpace()) {
                --from;
            }
            out.remove(from, a->end - from);
            return out;
        }
        QString escaped = value;
        escaped.replace(QLatin1Char('&'), QLatin1String("&amp;"));
        if (a->quoted) {
            escaped.replace(QLatin1Char('"'), QLatin1String("&quot;"));
            out.replace(a->valueStart, a->valueEnd - a->valueStart, escaped);
        } else {
            escaped.replace(QLatin1Char('"'), QLatin1String("&quot;"));
            out.replace(a->valueStart, a->valueEnd - a->valueStart, QLatin1Char('"') + escaped + QLatin1Char('"'));
        }
        return out;
    }
    if (value.isEmpty()) {
        return out;
    }
    QString escaped = value;
    escaped.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    escaped.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    int at = tag.end - 1; // avant le « > »
    if (tag.selfClosing && at > 0 && html.at(at - 1) == QLatin1Char('/')) {
        --at;
    }
    while (at > tag.start && html.at(at - 1).isSpace()) {
        --at;
    }
    out.insert(at, QLatin1Char(' ') + name + QLatin1String("=\"") + escaped + QLatin1Char('"'));
    return out;
}

QString setText(const QString &html, const Element &target, const QString &text)
{
    // Une composition d'un seul élément : le texte est celui de la balise marquée data-koracut-texte.
    Element element = target;
    if (element.kind == Kind::Whole) {
        if (element.textTagIndex < 0) {
            return html;
        }
        element.tagIndex = element.textTagIndex;
    }
    const QList<Tag> tags = tokenize(html);
    if (element.tagIndex < 0 || element.tagIndex >= tags.size()) {
        return html;
    }
    const QVector<int> close = matchTags(tags);
    if (close.at(element.tagIndex) < 0) {
        return html; // un élément vide (img) n'a pas de texte
    }
    const QList<QPair<int, int>> nodes = textNodes(html, tags, close, element.tagIndex);
    QString out = html;
    const QString escaped = escapeText(text);
    if (nodes.isEmpty()) {
        out.insert(tags.at(element.tagIndex).end, escaped);
        return out;
    }
    // De la fin vers le début, pour que les positions restent justes.
    for (int i = nodes.size() - 1; i >= 0; --i) {
        int from = nodes.at(i).first;
        int to = nodes.at(i).second;
        while (from < to && html.at(from).isSpace()) {
            ++from;
        }
        while (to > from && html.at(to - 1).isSpace()) {
            --to;
        }
        out.replace(from, to - from, i == 0 ? escaped : QString());
    }
    return out;
}

QString pageSignature(const QString &html)
{
    const QList<Tag> tags = tokenize(html);
    const QVector<int> close = matchTags(tags);
    QString all;
    for (int i = 0; i < tags.size(); ++i) {
        const Tag &t = tags.at(i);
        if (t.closing) {
            continue;
        }
        if (t.name == QLatin1String("script") || t.name == QLatin1String("style")) {
            all += t.attr(QStringLiteral("src"));
            if (close.at(i) >= 0) {
                all += html.mid(t.end, tags.at(close.at(i)).start - t.end);
            }
            all += QLatin1Char('\n');
        } else if (t.name == QLatin1String("link")) {
            all += t.attr(QStringLiteral("href")) + QLatin1Char('\n');
        }
    }
    return sha1(all);
}

QString removeElement(const QString &html, const Element &element, int *offset)
{
    if (element.kind == Kind::Whole || element.outerEnd <= element.outerStart || element.outerEnd > html.size()) {
        return html;
    }
    int start = element.outerStart;
    int end = element.outerEnd;
    // Le blanc de la ligne avant l'élément et le retour à la ligne après : la ligne disparaît entière.
    while (start > 0 && (html.at(start - 1) == QLatin1Char(' ') || html.at(start - 1) == QLatin1Char('\t'))) {
        --start;
    }
    if (end < html.size() && html.at(end) == QLatin1Char('\r')) {
        ++end;
    }
    if (end < html.size() && html.at(end) == QLatin1Char('\n')) {
        ++end;
    }
    if (offset) {
        *offset = start;
    }
    return html.left(start) + html.mid(end);
}

QString insertFragment(const QString &html, int offset, const QString &fragment)
{
    const int at = qBound(0, offset, int(html.size()));
    return html.left(at) + fragment + html.mid(at);
}

QString withoutKoracutRootAttributes(const QString &html)
{
    const QList<Tag> tags = tokenize(html);
    for (const Tag &t : tags) {
        if (t.closing || !t.has(QStringLiteral("data-composition-id"))) {
            continue;
        }
        QString out = html;
        // De la fin vers le début, pour que les positions restent justes.
        for (int i = t.attributes.size() - 1; i >= 0; --i) {
            const Attribute &a = t.attributes.at(i);
            if (!a.name.startsWith(QLatin1String("data-koracut-"))) {
                continue;
            }
            int from = a.start;
            while (from > t.start + 1 && html.at(from - 1).isSpace()) {
                --from;
            }
            out.remove(from, a.end - from);
        }
        return out;
    }
    return html;
}

QString fingerprint(const QString &html, const Element &element, const QString &signature)
{
    const QList<Tag> tags = tokenize(html);
    if (element.kind == Kind::Whole || element.tagIndex < 0 || element.tagIndex >= tags.size()) {
        return sha1(withoutKoracutRootAttributes(html));
    }
    const Tag &tag = tags.at(element.tagIndex);
    const QString body = startTagWithoutTiming(html, tag) + html.mid(tag.end, element.outerEnd - tag.end);
    return sha1(body + QLatin1Char('\n') + signature);
}

Composition analyse(const QString &html)
{
    Composition comp;
    const QList<Tag> tags = tokenize(html);
    const QVector<int> close = matchTags(tags);
    int rootIndex = -1;
    for (int i = 0; i < tags.size(); ++i) {
        if (!tags.at(i).closing && tags.at(i).has(QStringLiteral("data-composition-id"))) {
            rootIndex = i;
            break;
        }
    }
    // Les variables sont déclarées sur <html> ou sur la racine.
    for (int i = 0; i < tags.size(); ++i) {
        if (!tags.at(i).closing && tags.at(i).has(QStringLiteral("data-composition-variables"))) {
            comp.variables = tags.at(i).attr(QStringLiteral("data-composition-variables"));
            break;
        }
    }
    if (rootIndex < 0) {
        comp.errors << QStringLiteral("Aucune racine data-composition-id dans le fichier.");
        return comp;
    }
    const Tag &root = tags.at(rootIndex);
    comp.hasRoot = true;
    comp.id = root.attr(QStringLiteral("data-composition-id"));
    comp.elementKind = root.attr(QStringLiteral("data-koracut-element")).trimmed();
    comp.style = root.attr(QStringLiteral("data-koracut-style")).trimmed();
    if (root.has(QStringLiteral("data-koracut-cut"))) {
        bool ok = false;
        const double cut = root.attr(QStringLiteral("data-koracut-cut")).toDouble(&ok);
        comp.cut = ok ? cut : -1.;
    }
    comp.retired = root.attr(QStringLiteral("data-koracut-retire")) == QLatin1String("1");
    comp.opaque = root.attr(QStringLiteral("data-koracut-fond")).trimmed() == QLatin1String("opaque");
    comp.sequence = root.attr(QStringLiteral("data-koracut-sequence")).trimmed();
    comp.width = root.attr(QStringLiteral("data-width")).toInt();
    comp.height = root.attr(QStringLiteral("data-height")).toInt();
    comp.fps = root.attr(QStringLiteral("data-fps")).toDouble();
    if (root.has(QStringLiteral("data-duration"))) {
        comp.duration = root.attr(QStringLiteral("data-duration")).toDouble(&comp.durationKnown);
    }
    const int rootEnd = outerEndOf(html, tags, close, rootIndex);
    const QString signature = pageSignature(html);

    int skipUntil = -1;
    int generated = 0;
    for (int i = rootIndex + 1; i < tags.size(); ++i) {
        const Tag &t = tags.at(i);
        if (t.closing || t.start >= rootEnd) {
            continue;
        }
        if (t.start < skipUntil) {
            continue; // à l'intérieur d'un élément temporel déjà pris
        }
        const bool timed = t.has(QStringLiteral("data-start")) || t.has(QStringLiteral("data-composition-src"));
        if (!timed) {
            continue;
        }
        Element e;
        e.tag = t.name;
        e.tagIndex = i;
        e.outerStart = t.start;
        e.outerEnd = outerEndOf(html, tags, close, i);
        e.innerStart = t.end;
        e.innerEnd = close.at(i) >= 0 ? tags.at(close.at(i)).start : t.end;
        skipUntil = e.outerEnd;
        e.id = t.attr(QStringLiteral("id"));
        if (e.id.isEmpty()) {
            e.id = QStringLiteral("%1-%2").arg(t.name).arg(++generated);
            comp.errors << QStringLiteral("L'élément <%1> à la position %2 n'a pas d'id.").arg(t.name).arg(t.start);
        }
        if (t.name == QLatin1String("img")) {
            e.kind = Kind::Image;
        } else if (t.name == QLatin1String("video")) {
            e.kind = Kind::Video;
        } else if (t.name == QLatin1String("audio")) {
            e.kind = Kind::Audio;
        } else if (t.has(QStringLiteral("data-composition-src")) || t.has(QStringLiteral("data-composition-id"))) {
            e.kind = Kind::Composition;
        } else {
            e.kind = Kind::Block;
        }
        e.startExpr = t.attr(QStringLiteral("data-start"));
        if (t.has(QStringLiteral("data-duration"))) {
            e.duration = t.attr(QStringLiteral("data-duration")).toDouble(&e.durationKnown);
        } else if (e.kind == Kind::Image) {
            e.duration = 3.;
            e.durationKnown = true;
        } else if (e.kind == Kind::Video || e.kind == Kind::Audio) {
            e.durationKnown = false; // la durée du fichier
        } else {
            e.durationKnown = false;
            comp.errors << QStringLiteral("L'élément « %1 » n'a pas de data-duration.").arg(e.id);
        }
        e.trackIndex = t.attr(QStringLiteral("data-track-index")).toInt();
        if (t.has(QStringLiteral("data-media-start"))) {
            e.mediaStart = t.attr(QStringLiteral("data-media-start")).toDouble();
        } else if (t.has(QStringLiteral("data-playback-start"))) {
            e.mediaStart = t.attr(QStringLiteral("data-playback-start")).toDouble();
        } else if (t.has(QStringLiteral("data-koracut-in"))) {
            // Attribut propre à KoraCut, ignoré par HyperFrames : le point d'entrée dans le rendu
            // d'un bloc ou d'une image (un clip rogné par la gauche dans la timeline).
            e.mediaStart = t.attr(QStringLiteral("data-koracut-in")).toDouble();
        }
        if (t.has(QStringLiteral("data-volume"))) {
            e.volume = t.attr(QStringLiteral("data-volume")).toDouble();
        }
        e.src = t.attr(QStringLiteral("src"));
        if (e.src.isEmpty()) {
            e.src = t.attr(QStringLiteral("data-composition-src"));
        }
        if (e.kind == Kind::Block || e.kind == Kind::Composition) {
            e.text = visibleText(html.mid(e.innerStart, e.innerEnd - e.innerStart));
        }
        comp.elements.append(e);
    }

    if (comp.elements.isEmpty()) {
        // Une racine vide (tous ses éléments retirés) n'est pas une composition d'un seul tenant : rien à poser.
        const int innerEnd = close.at(rootIndex) >= 0 ? tags.at(close.at(rootIndex)).start : root.end;
        if (html.mid(root.end, innerEnd - root.end).trimmed().isEmpty()) {
            return comp;
        }
        // Une composition d'un seul tenant (StockFinity) : un clip de la durée de la racine.
        Element whole;
        whole.id = comp.id.isEmpty() ? QStringLiteral("composition") : comp.id;
        whole.kind = Kind::Whole;
        whole.tag = root.name;
        whole.tagIndex = rootIndex;
        // Sa place dans la timeline : data-koracut-start (une composition d'un seul élément posée par
        // KoraCut), 0 sinon. HyperFrames ne le lit pas : la composition commence toujours à 0 pour lui.
        whole.startExpr = QStringLiteral("0");
        whole.start = 0.;
        if (root.has(QStringLiteral("data-koracut-start"))) {
            bool ok = false;
            const double start = root.attr(QStringLiteral("data-koracut-start")).toDouble(&ok);
            if (ok && start >= 0) {
                whole.start = start;
                whole.startExpr = root.attr(QStringLiteral("data-koracut-start")).trimmed();
            }
        }
        // Le texte de l'élément : la première balise marquée data-koracut-texte.
        for (int i = rootIndex + 1; i < tags.size() && tags.at(i).start < rootEnd; ++i) {
            if (!tags.at(i).closing && tags.at(i).has(QStringLiteral("data-koracut-texte"))) {
                whole.textTagIndex = i;
                const int innerEnd = close.at(i) >= 0 ? tags.at(close.at(i)).start : tags.at(i).end;
                whole.text = visibleText(html.mid(tags.at(i).end, innerEnd - tags.at(i).end));
                break;
            }
        }
        whole.duration = comp.duration;
        whole.durationKnown = comp.durationKnown;
        whole.outerStart = root.start;
        whole.outerEnd = rootEnd;
        whole.innerStart = root.end;
        whole.innerEnd = close.at(rootIndex) >= 0 ? tags.at(close.at(rootIndex)).start : root.end;
        whole.hash = sha1(withoutKoracutRootAttributes(html));
        if (!comp.durationKnown) {
            comp.errors << QStringLiteral("La racine n'a pas de data-duration : impossible de connaître la durée de la composition.");
        }
        comp.elements.append(whole);
        return comp;
    }

    // Les débuts : resolveStart suit les expressions de proche en proche (« intro + 0.5 » lit le
    // startExpr d'intro, pas son start), un seul passage suffit ; une boucle ou un id inconnu
    // laisse l'élément non résolu. Le hash vide sert de témoin.
    for (Element &e : comp.elements) {
        double seconds = 0.;
        if (resolveStart(e.startExpr, comp, seconds)) {
            e.start = seconds;
            e.startResolved = true;
            e.hash = fingerprint(html, e, signature);
        }
    }
    for (Element &e : comp.elements) {
        if (e.hash.isEmpty()) {
            e.startResolved = false;
            e.start = 0.;
            e.hash = fingerprint(html, e, signature);
            comp.errors << QStringLiteral("Le début « %1 » de l'élément « %2 » ne se résout pas.").arg(e.startExpr, e.id);
        }
    }
    return comp;
}

} // namespace KoraCutMotion

// ---------------------------------------------------------------------------------------------

KoraCutMotionSync::KoraCutMotionSync(const QString &indexPath, QObject *parent)
    : QObject(parent)
    , m_path(indexPath)
    , m_watcher(new QFileSystemWatcher(this))
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    connect(m_debounce, &QTimer::timeout, this, &KoraCutMotionSync::checkNow);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &KoraCutMotionSync::scheduleCheck);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &KoraCutMotionSync::scheduleCheck);
    reload();
    rewatch();
}

KoraCutMotionSync::~KoraCutMotionSync() = default;

QString KoraCutMotionSync::directory() const
{
    return QFileInfo(m_path).absolutePath();
}

QByteArray KoraCutMotionSync::digest(const QString &content)
{
    return QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha1);
}

bool KoraCutMotionSync::reload()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll());
    const QByteArray d = digest(content);
    if (d == m_digest) {
        return false;
    }
    m_html = content;
    m_digest = d;
    m_composition = KoraCutMotion::analyse(m_html);
    return true;
}

bool KoraCutMotionSync::changedOnDisk() const
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    return digest(QString::fromUtf8(file.readAll())) != m_digest;
}

void KoraCutMotionSync::setDebounce(int ms)
{
    m_debounce->setInterval(ms);
}

void KoraCutMotionSync::scheduleCheck()
{
    m_debounce->start();
}

void KoraCutMotionSync::checkNow()
{
    rewatch();
    if (reload()) {
        Q_EMIT changedOutside();
    }
}

void KoraCutMotionSync::rewatch()
{
    // Beaucoup d'éditeurs remplacent le fichier au lieu de l'écrire : la surveillance du chemin
    // se perd, celle du dossier reste. On remet les deux à chaque passage.
    const QStringList files = m_watcher->files();
    if (!files.contains(m_path) && QFile::exists(m_path)) {
        m_watcher->addPath(m_path);
    }
    const QString dir = directory();
    if (!m_watcher->directories().contains(dir) && QDir(dir).exists()) {
        m_watcher->addPath(dir);
    }
}

bool KoraCutMotionSync::writeFile(const QString &content)
{
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(content.toUtf8());
    if (!file.commit()) {
        return false;
    }
    m_html = content;
    m_digest = digest(content);
    m_composition = KoraCutMotion::analyse(m_html);
    rewatch();
    return true;
}

bool KoraCutMotionSync::writeElement(const QString &id, const QMap<QString, QString> &attributes, const QString *text)
{
    if (changedOnDisk()) {
        // Une écriture extérieure attend d'être relue : on ne l'écrase pas.
        scheduleCheck();
        return false;
    }
    QString html = m_html;
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        const KoraCutMotion::Composition comp = KoraCutMotion::analyse(html);
        const KoraCutMotion::Element *e = comp.element(id);
        if (!e) {
            return false;
        }
        const QList<KoraCutMotion::Tag> tags = KoraCutMotion::tokenize(html);
        if (e->tagIndex < 0 || e->tagIndex >= tags.size()) {
            return false;
        }
        html = KoraCutMotion::setAttribute(html, tags.at(e->tagIndex), it.key(), it.value());
    }
    if (text) {
        const KoraCutMotion::Composition comp = KoraCutMotion::analyse(html);
        const KoraCutMotion::Element *e = comp.element(id);
        if (!e) {
            return false;
        }
        html = KoraCutMotion::setText(html, *e, *text);
    }
    if (html == m_html) {
        return true;
    }
    return writeFile(html);
}

bool KoraCutMotionSync::writeAllSilently(const QString &html)
{
    if (html == m_html) {
        return true;
    }
    return writeFile(html);
}

bool KoraCutMotionSync::writeAll(const QString &html)
{
    if (!writeFile(html)) {
        return false;
    }
    Q_EMIT changedOutside();
    return true;
}
