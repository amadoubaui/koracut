/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutmotioncodeeditor.h"
#include "../koracutfonts.h"
#include "koracutmotionrenderer.h"

#include "koracut/koracutmetrics.h"

#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPainter>
#include <QProcess>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>

// ---------------------------------------------------------------------------------------------
// Coloration

KoraCutMotionHighlighter::KoraCutMotionHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    // Les couleurs de la charte produit : accent bleu, vert voix, orange musique, violet titres, jaune.
    m_tag.setForeground(QColor(0x0D, 0x99, 0xFF));
    m_attribute.setForeground(QColor(0x14, 0xA3, 0x7F));
    m_string.setForeground(QColor(0xE0, 0x7B, 0x39));
    m_comment.setForeground(QColor(255, 255, 255, 110));
    m_comment.setFontItalic(true);
    m_keyword.setForeground(QColor(0x7C, 0x5C, 0xFF));
    m_number.setForeground(QColor(0xFF, 0xD1, 0x66));
    m_property.setForeground(QColor(0x14, 0xA3, 0x7F));
    m_keywords = QRegularExpression(QStringLiteral("\\b(const|let|var|function|return|if|else|for|while|new|window|document|true|false|null|undefined|"
                                                   "class|this|import|export|await|async|of|in|gsap|tl|timeline)\\b"));
}

void KoraCutMotionHighlighter::highlightBlock(const QString &text)
{
    int state = previousBlockState() < 0 ? Html : previousBlockState();
    int pos = 0;
    const int n = text.size();
    while (pos < n) {
        if (state == HtmlComment) {
            const int end = text.indexOf(QLatin1String("-->"), pos);
            const int stop = end < 0 ? n : end + 3;
            setFormat(pos, stop - pos, m_comment);
            pos = stop;
            if (end >= 0) {
                state = Html;
            }
            continue;
        }
        if (state == ScriptComment || state == StyleComment) {
            const int end = text.indexOf(QLatin1String("*/"), pos);
            const int stop = end < 0 ? n : end + 2;
            setFormat(pos, stop - pos, m_comment);
            pos = stop;
            if (end >= 0) {
                state = state == ScriptComment ? Script : Style;
            }
            continue;
        }
        if (state == Script || state == Style) {
            const QString closing = state == Script ? QStringLiteral("</script") : QStringLiteral("</style");
            const int end = text.indexOf(closing, pos, Qt::CaseInsensitive);
            const int blockComment = text.indexOf(QLatin1String("/*"), pos);
            const int lineComment = state == Script ? text.indexOf(QLatin1String("//"), pos) : -1;
            int stop = end < 0 ? n : end;
            if (blockComment >= 0 && blockComment < stop) {
                highlightCode(text, pos, blockComment, state == Style);
                pos = blockComment;
                state = state == Script ? ScriptComment : StyleComment;
                continue;
            }
            if (lineComment >= 0 && lineComment < stop) {
                highlightCode(text, pos, lineComment, false);
                setFormat(lineComment, stop - lineComment, m_comment);
                pos = stop;
                if (end >= 0) {
                    state = Html;
                }
                continue;
            }
            highlightCode(text, pos, stop, state == Style);
            pos = stop;
            if (end >= 0) {
                state = Html;
            }
            continue;
        }
        // HTML
        const int comment = text.indexOf(QLatin1String("<!--"), pos);
        const int scriptOpen = text.indexOf(QLatin1String("<script"), pos, Qt::CaseInsensitive);
        const int styleOpen = text.indexOf(QLatin1String("<style"), pos, Qt::CaseInsensitive);
        int next = n;
        int kind = Html;
        if (comment >= 0 && comment < next) {
            next = comment;
            kind = HtmlComment;
        }
        if (scriptOpen >= 0 && scriptOpen < next) {
            next = scriptOpen;
            kind = Script;
        }
        if (styleOpen >= 0 && styleOpen < next) {
            next = styleOpen;
            kind = Style;
        }
        highlightHtml(text, pos, next);
        if (kind == Html) {
            pos = n;
            break;
        }
        if (kind == HtmlComment) {
            pos = next;
            state = HtmlComment;
            continue;
        }
        // La balise <script …> ou <style …> elle-même, puis son contenu.
        const int gt = text.indexOf(QLatin1Char('>'), next);
        const int tagEnd = gt < 0 ? n : gt + 1;
        highlightHtml(text, next, tagEnd);
        pos = tagEnd;
        state = kind;
    }
    setCurrentBlockState(state);
}

void KoraCutMotionHighlighter::highlightHtml(const QString &text, int from, int to)
{
    static const QRegularExpression tag(QStringLiteral("</?[A-Za-z][A-Za-z0-9:-]*|/?>"));
    static const QRegularExpression attribute(QStringLiteral("\\b[A-Za-z_:][A-Za-z0-9_:.-]*(?=\\s*=)"));
    static const QRegularExpression string(QStringLiteral("\"[^\"]*\"|'[^']*'"));
    const QString part = text.mid(from, to - from);
    for (const QRegularExpression *re : {&tag, &attribute, &string}) {
        auto it = re->globalMatch(part);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            setFormat(from + m.capturedStart(), m.capturedLength(), re == &tag ? m_tag : re == &attribute ? m_attribute : m_string);
        }
    }
}

void KoraCutMotionHighlighter::highlightCode(const QString &text, int from, int to, bool style)
{
    static const QRegularExpression number(QStringLiteral("\\b\\d+(\\.\\d+)?(px|s|ms|%|em|rem|deg)?\\b"));
    static const QRegularExpression string(QStringLiteral("\"[^\"]*\"|'[^']*'|`[^`]*`"));
    static const QRegularExpression property(QStringLiteral("[A-Za-z-]+(?=\\s*:)"));
    static const QRegularExpression selector(QStringLiteral("[#.][A-Za-z_][A-Za-z0-9_-]*"));
    const QString part = text.mid(from, to - from);
    if (style) {
        for (const QRegularExpression *re : {&property, &selector, &number, &string}) {
            auto it = re->globalMatch(part);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                setFormat(from + m.capturedStart(), m.capturedLength(), re == &property ? m_property : re == &selector ? m_tag : re == &number ? m_number : m_string);
            }
        }
        return;
    }
    const QRegularExpression *keywords = &m_keywords;
    for (const QRegularExpression *re : {keywords, &number, &string}) {
        auto it = re->globalMatch(part);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            setFormat(from + m.capturedStart(), m.capturedLength(), re == keywords ? m_keyword : re == &number ? m_number : m_string);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// L'éditeur

namespace {
class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(KoraCutMotionCodeEditor *editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }
    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->lineNumberAreaPaintEvent(event); }

private:
    KoraCutMotionCodeEditor *m_editor;
};
} // namespace

KoraCutMotionCodeEditor::KoraCutMotionCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumbers(new LineNumberArea(this))
    , m_autosave(new QTimer(this))
{
    setObjectName(QStringLiteral("koracutMotionCode"));
    // La chasse fixe du code, embarquée : la même sur les trois systèmes (koracutfonts.h).
    setFont(KoraCutFonts::code(KoraCutMetrics::FontDense));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(2 * fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    m_highlighter = new KoraCutMotionHighlighter(document());
    m_autosave->setSingleShot(true);
    m_autosave->setInterval(800);
    connect(m_autosave, &QTimer::timeout, this, &KoraCutMotionCodeEditor::saveNow);
    connect(this, &QPlainTextEdit::blockCountChanged, this, &KoraCutMotionCodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &KoraCutMotionCodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &KoraCutMotionCodeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_settingContent) {
            return;
        }
        m_dirty = true;
        m_autosave->start();
    });
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

void KoraCutMotionCodeEditor::setContent(const QString &text)
{
    if (text == toPlainText()) {
        m_dirty = false;
        return;
    }
    m_settingContent = true;
    const int line = textCursor().blockNumber();
    const int scroll = verticalScrollBar()->value();
    setPlainText(text);
    goToLine(line + 1);
    verticalScrollBar()->setValue(scroll);
    m_settingContent = false;
    m_dirty = false;
    m_autosave->stop();
}

void KoraCutMotionCodeEditor::markSaved()
{
    m_dirty = false;
}

void KoraCutMotionCodeEditor::saveNow()
{
    m_autosave->stop();
    if (!m_dirty) {
        return;
    }
    Q_EMIT saveRequested(toPlainText());
}

void KoraCutMotionCodeEditor::goToLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(qMax(0, line - 1));
    if (!block.isValid()) {
        return;
    }
    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

int KoraCutMotionCodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    return KoraCutMetrics::SpaceGroup + fontMetrics().horizontalAdvance(QLatin1Char('9')) * (digits + 1);
}

void KoraCutMotionCodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void KoraCutMotionCodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy) {
        m_lineNumbers->scroll(0, dy);
    } else {
        m_lineNumbers->update(0, rect.y(), m_lineNumbers->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth();
    }
}

void KoraCutMotionCodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumbers->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void KoraCutMotionCodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Save)) {
        saveNow();
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void KoraCutMotionCodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumbers);
    painter.fillRect(event->rect(), QColor(0x2C, 0x2C, 0x2C));
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = int(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + int(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(number == current ? QColor(0xFF, 0xFF, 0xFF) : QColor(255, 255, 255, 110));
            painter.drawText(0, top, m_lineNumbers->width() - KoraCutMetrics::SpaceTight, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + int(blockBoundingRect(block).height());
        ++number;
    }
}

void KoraCutMotionCodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor(255, 255, 255, 10));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
    }
    setExtraSelections(selections);
}

// ---------------------------------------------------------------------------------------------
// Lint

KoraCutMotionLinter::KoraCutMotionLinter(QObject *parent)
    : QObject(parent)
{
}

bool KoraCutMotionLinter::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QList<KoraCutMotionLintMessage> KoraCutMotionLinter::parse(const QByteArray &json)
{
    QList<KoraCutMotionLintMessage> messages;
    const int brace = json.indexOf('{');
    if (brace < 0) {
        return messages;
    }
    const QJsonObject root = QJsonDocument::fromJson(json.mid(brace)).object();
    const QJsonArray findings = root.value(QStringLiteral("findings")).toArray();
    for (const QJsonValue &v : findings) {
        const QJsonObject f = v.toObject();
        KoraCutMotionLintMessage m;
        m.severity = f.value(QStringLiteral("severity")).toString();
        m.code = f.value(QStringLiteral("code")).toString();
        m.message = f.value(QStringLiteral("message")).toString();
        m.file = f.value(QStringLiteral("file")).toString();
        m.line = f.value(QStringLiteral("line")).toInt();
        messages.append(m);
    }
    return messages;
}

void KoraCutMotionLinter::run(const QString &projectDir)
{
    if (isRunning()) {
        m_pendingDir = projectDir;
        return;
    }
    if (KoraCutMotionHost::state() != KoraCutMotionHost::State::Ready) {
        Q_EMIT finished({}, QStringLiteral("HyperFrames n'est pas installé."));
        return;
    }
    m_process = new QProcess(this);
    m_process->setProcessEnvironment(KoraCutMotionHost::environment());
    m_process->setWorkingDirectory(projectDir);
    connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        const QByteArray out = m_process->readAllStandardOutput();
        const QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        m_process->deleteLater();
        m_process = nullptr;
        Q_EMIT finished(parse(out), out.contains('{') ? QString() : err);
        if (!m_pendingDir.isEmpty()) {
            const QString dir = m_pendingDir;
            m_pendingDir.clear();
            run(dir);
        }
    });
    m_process->start(KoraCutMotionHost::nodePath(), KoraCutMotionHost::arguments({QStringLiteral("lint"), projectDir, QStringLiteral("--json")}));
}
