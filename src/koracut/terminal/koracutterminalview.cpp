/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutterminalview.h"
#include "../koracutfonts.h"
#include "koracutpty.h"

#include "libvterm/include/vterm.h"

#include <KLocalizedString>

#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace {

constexpr quint16 AttrBold = 1 << 0;
constexpr quint16 AttrItalic = 1 << 1;
constexpr quint16 AttrUnderline = 1 << 2;
constexpr quint16 AttrReverse = 1 << 3;
constexpr quint16 AttrStrike = 1 << 4;
constexpr quint16 AttrConceal = 1 << 5;
constexpr quint16 AttrDoubleUnderline = 1 << 6;
constexpr quint8 FlagDefaultFg = 1;
constexpr quint8 FlagDefaultBg = 2;

// Palette de 16 couleurs accordée à la charte produit (2 - Charte graphique KoraCut.md).
const QRgb kPalette[16] = {0xFF000000, 0xFFFF7262, 0xFF4ADE80, 0xFFFFD166, 0xFF0D99FF, 0xFF7C5CFF, 0xFF29B8DB, 0xFFCCCCCC,
                           0xFF6B6B6B, 0xFFFF8A7E, 0xFF6EE7A0, 0xFFFFE08F, 0xFF4DB5FF, 0xFFA08BFF, 0xFF5CD0E8, 0xFFFFFFFF};
const QRgb kDefaultFg = 0xFFE6E6E6;
const QRgb kDefaultBg = 0xFF1E1E1E;
const QRgb kSelection = 0xFF264F78;
const QRgb kCursor = 0xFFE6E6E6;

KoraCutTerminalView *self(void *user)
{
    return static_cast<KoraCutTerminalView *>(user);
}

int cbDamage(VTermRect, void *user)
{
    self(user)->onDamage();
    return 1;
}

int cbMoveRect(VTermRect, VTermRect, void *user)
{
    self(user)->onDamage();
    return 1;
}

int cbMoveCursor(VTermPos pos, VTermPos, int visible, void *user)
{
    self(user)->onMoveCursor(pos.row, pos.col, visible != 0);
    return 1;
}

int cbTermProp(VTermProp prop, VTermValue *val, void *user)
{
    self(user)->onTermProp(int(prop), val);
    return 1;
}

int cbBell(void *)
{
    return 1;
}

int cbPushLine(int cols, const VTermScreenCell *cells, void *user)
{
    self(user)->onPushLine(cols, cells);
    return 1;
}

int cbPopLine(int cols, VTermScreenCell *cells, void *user)
{
    return self(user)->onPopLine(cols, cells);
}

int cbClear(void *user)
{
    self(user)->onClearScrollback();
    return 1;
}

void cbOutput(const char *s, size_t len, void *user)
{
    self(user)->onOutput(s, len);
}

int cbOsc(int command, VTermStringFragment frag, void *user)
{
    self(user)->onOsc(command, frag.str, frag.len, frag.initial, frag.final);
    return 1;
}

int cbSelectionSet(VTermSelectionMask, VTermStringFragment frag, void *user)
{
    self(user)->onSelection(frag.str, frag.len, frag.initial, frag.final);
    return 1;
}

int cbSelectionQuery(VTermSelectionMask, void *)
{
    // Lire le presse-papiers depuis un programme : refusé, par prudence.
    return 0;
}

const VTermScreenCallbacks kScreenCallbacks = {cbDamage, cbMoveRect, cbMoveCursor, cbTermProp, cbBell, nullptr, cbPushLine, cbPopLine, cbClear, nullptr};
const VTermStateFallbacks kFallbacks = {nullptr, nullptr, cbOsc, nullptr, nullptr, nullptr, nullptr};
const VTermSelectionCallbacks kSelectionCallbacks = {cbSelectionSet, cbSelectionQuery};

VTermColor rgbColor(QRgb c)
{
    VTermColor col;
    vterm_color_rgb(&col, uint8_t(qRed(c)), uint8_t(qGreen(c)), uint8_t(qBlue(c)));
    return col;
}

bool isWordChar(char32_t c)
{
    if (c == U'_' || c == U'-' || c == U'.' || c == U'/' || c == U'~' || c == U':') {
        return true;
    }
    return QChar::isLetterOrNumber(c);
}

} // namespace

KoraCutTerminalView::KoraCutTerminalView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("koracutTerminalView"));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    viewport()->setAutoFillBackground(false);

    // D'abord la minuterie de dessin : libvterm appelle déjà nos rappels pendant sa mise en route.
    m_repaint = new QTimer(this);
    m_repaint->setSingleShot(true);
    m_repaint->setInterval(8);
    connect(m_repaint, &QTimer::timeout, this, [this]() {
        updateScrollBar(false);
        viewport()->update();
    });

    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, cbOutput, this);
    m_screen = vterm_obtain_screen(m_vt);
    vterm_screen_set_callbacks(m_screen, &kScreenCallbacks, this);
    vterm_screen_set_unrecognised_fallbacks(m_screen, &kFallbacks, this);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_enable_reflow(m_screen, true);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
    VTermState *state = vterm_obtain_state(m_vt);
    for (int i = 0; i < 16; ++i) {
        const VTermColor col = rgbColor(kPalette[i]);
        vterm_state_set_palette_color(state, i, &col);
    }
    vterm_state_set_bold_highbright(state, 0);
    m_clipboardBuffer.resize(4096);
    vterm_state_set_selection_callbacks(state, &kSelectionCallbacks, this, m_clipboardBuffer.data(), size_t(m_clipboardBuffer.size()));
    vterm_screen_reset(m_screen, 1);
    const VTermColor fg = rgbColor(kDefaultFg);
    const VTermColor bg = rgbColor(kDefaultBg);
    vterm_screen_set_default_colors(m_screen, &fg, &bg);

    m_pty = new KoraCutPty(this);
    connect(m_pty, &KoraCutPty::dataReceived, this, [this](const QByteArray &data) { feed(data); });
    connect(m_pty, &KoraCutPty::finished, this, [this](int code) {
        m_exited = true;
        const QString text = i18nc("@info in the integrated terminal, after the program ended; %1 exit code", "Process ended (code %1). Press Enter to start a shell.", code);
        feed(QByteArray("\r\n\x1b[0;90m") + text.toUtf8() + QByteArray("\x1b[0m\r\n"));
        Q_EMIT finished(code);
    });

    setupFont();
}

KoraCutTerminalView::~KoraCutTerminalView()
{
    delete m_pty;
    m_pty = nullptr;
    if (m_vt) {
        vterm_free(m_vt);
    }
}

void KoraCutTerminalView::setupFont()
{
    // La même police sur les trois systèmes (JetBrains Mono NL embarquée : cadres, blocs, pas de
    // ligatures) ; avant le 30 septembre 2026, Menlo, Cascadia Mono ou Consolas selon le système.
    QFont f = KoraCutFonts::code(m_fontPixels);
    f.setStyleHint(QFont::Monospace, QFont::PreferAntialias);
    f.setFixedPitch(true);
    f.setKerning(false);
    f.setPixelSize(m_fontPixels);
    m_font = f;
    m_fontBold = f;
    m_fontBold.setBold(true);
    m_fontItalic = f;
    m_fontItalic.setItalic(true);
    m_fontBoldItalic = m_fontBold;
    m_fontBoldItalic.setItalic(true);
    const QFontMetricsF fm(m_font);
    m_cellW = fm.horizontalAdvance(QLatin1Char('M'));
    m_cellH = qCeil(fm.ascent() + fm.descent());
    m_ascent = qCeil(fm.ascent());
    verticalScrollBar()->setSingleStep(1);
    resizeTerminal();
    viewport()->update();
}

void KoraCutTerminalView::zoom(int delta)
{
    const int next = delta == 0 ? 12 : qBound(8, m_fontPixels + delta, 32);
    if (next != m_fontPixels) {
        m_fontPixels = next;
        setupFont();
    }
}

QSizeF KoraCutTerminalView::cellSize() const
{
    return QSizeF(m_cellW, m_cellH);
}

bool KoraCutTerminalView::start(const QString &program, const QStringList &args, const QString &workDir, const QProcessEnvironment &env, QString *error)
{
    m_exited = false;
    resizeTerminal();
    return m_pty->start(program, args, workDir, env, m_cols, m_rows, error);
}

bool KoraCutTerminalView::isRunning() const
{
    return m_pty && m_pty->isRunning();
}

KoraCutPty *KoraCutTerminalView::pty() const
{
    return m_pty;
}

int KoraCutTerminalView::columns() const
{
    return m_cols;
}

int KoraCutTerminalView::rows() const
{
    return m_rows;
}

QString KoraCutTerminalView::title() const
{
    return m_title;
}

void KoraCutTerminalView::feed(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }
    vterm_input_write(m_vt, data.constData(), size_t(data.size()));
    vterm_screen_flush_damage(m_screen);
    scheduleUpdate();
}

void KoraCutTerminalView::sendText(const QString &text)
{
    if (m_pty && m_pty->isRunning()) {
        m_pty->write(text.toUtf8());
    }
}

void KoraCutTerminalView::scheduleUpdate()
{
    if (!m_repaint->isActive()) {
        m_repaint->start();
    }
}

void KoraCutTerminalView::onDamage()
{
    scheduleUpdate();
}

void KoraCutTerminalView::onMoveCursor(int row, int col, bool visible)
{
    m_cursorRow = row;
    m_cursorCol = col;
    Q_UNUSED(visible)
    scheduleUpdate();
}

void KoraCutTerminalView::onTermProp(int prop, const void *value)
{
    const auto *val = static_cast<const VTermValue *>(value);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        m_cursorVisible = val->boolean != 0;
        break;
    case VTERM_PROP_ALTSCREEN:
        m_altScreen = val->boolean != 0;
        updateScrollBar(true);
        break;
    case VTERM_PROP_TITLE: {
        if (val->string.initial) {
            m_oscBuffer.clear();
        }
        m_oscBuffer.append(val->string.str, qsizetype(val->string.len));
        if (val->string.final) {
            m_title = QString::fromUtf8(m_oscBuffer).trimmed();
            m_oscBuffer.clear();
            Q_EMIT titleChanged(m_title);
        }
        break;
    }
    case VTERM_PROP_CURSORSHAPE:
        m_cursorShape = val->number;
        break;
    case VTERM_PROP_MOUSE:
        m_mouseMode = val->number;
        break;
    case VTERM_PROP_REVERSE:
        m_reverse = val->boolean != 0;
        break;
    default:
        break;
    }
    scheduleUpdate();
}

KoraCutTerminalView::Cell KoraCutTerminalView::convert(const void *screenCell) const
{
    const auto *c = static_cast<const VTermScreenCell *>(screenCell);
    Cell out;
    out.ch = c->chars[0] ? char32_t(c->chars[0]) : U' ';
    out.width = quint8(qMax(0, int(c->width)));
    quint16 a = 0;
    if (c->attrs.bold) {
        a |= AttrBold;
    }
    if (c->attrs.italic) {
        a |= AttrItalic;
    }
    if (c->attrs.underline == VTERM_UNDERLINE_DOUBLE) {
        a |= AttrDoubleUnderline;
    } else if (c->attrs.underline) {
        a |= AttrUnderline;
    }
    if (c->attrs.reverse) {
        a |= AttrReverse;
    }
    if (c->attrs.strike) {
        a |= AttrStrike;
    }
    if (c->attrs.conceal) {
        a |= AttrConceal;
    }
    out.attrs = a;
    VTermColor fg = c->fg;
    VTermColor bg = c->bg;
    if (VTERM_COLOR_IS_DEFAULT_FG(&fg)) {
        out.flags |= FlagDefaultFg;
        out.fg = kDefaultFg;
    } else {
        vterm_screen_convert_color_to_rgb(m_screen, &fg);
        out.fg = qRgb(fg.rgb.red, fg.rgb.green, fg.rgb.blue);
    }
    if (VTERM_COLOR_IS_DEFAULT_BG(&bg)) {
        out.flags |= FlagDefaultBg;
        out.bg = kDefaultBg;
    } else {
        vterm_screen_convert_color_to_rgb(m_screen, &bg);
        out.bg = qRgb(bg.rgb.red, bg.rgb.green, bg.rgb.blue);
    }
    return out;
}

void KoraCutTerminalView::onPushLine(int cols, const void *cells)
{
    const auto *c = static_cast<const VTermScreenCell *>(cells);
    Line line;
    line.reserve(size_t(cols));
    for (int i = 0; i < cols; ++i) {
        line.push_back(convert(&c[i]));
    }
    // Les cases vides de fin ne coûtent rien à garder : on les retire.
    while (!line.empty() && line.back().ch == U' ' && line.back().attrs == 0 && (line.back().flags & FlagDefaultBg)) {
        line.pop_back();
    }
    line.shrink_to_fit();
    m_scrollback.push_back(std::move(line));
    while (int(m_scrollback.size()) > m_maxScrollback) {
        m_scrollback.pop_front();
        ++m_trimmed;
    }
}

int KoraCutTerminalView::onPopLine(int cols, void *cells)
{
    if (m_scrollback.empty()) {
        return 0;
    }
    auto *c = static_cast<VTermScreenCell *>(cells);
    const Line line = std::move(m_scrollback.back());
    m_scrollback.pop_back();
    for (int i = 0; i < cols; ++i) {
        VTermScreenCell &cell = c[i];
        std::memset(&cell, 0, sizeof(cell));
        const Cell src = i < int(line.size()) ? line.at(size_t(i)) : Cell{U' ', kDefaultFg, kDefaultBg, 0, 1, FlagDefaultFg | FlagDefaultBg};
        cell.chars[0] = src.ch == U' ' ? 0 : uint32_t(src.ch);
        cell.width = char(src.width);
        cell.attrs.bold = (src.attrs & AttrBold) ? 1 : 0;
        cell.attrs.italic = (src.attrs & AttrItalic) ? 1 : 0;
        cell.attrs.underline = (src.attrs & AttrDoubleUnderline) ? VTERM_UNDERLINE_DOUBLE : ((src.attrs & AttrUnderline) ? VTERM_UNDERLINE_SINGLE : 0);
        cell.attrs.reverse = (src.attrs & AttrReverse) ? 1 : 0;
        cell.attrs.strike = (src.attrs & AttrStrike) ? 1 : 0;
        cell.attrs.conceal = (src.attrs & AttrConceal) ? 1 : 0;
        cell.fg = rgbColor(src.fg);
        cell.bg = rgbColor(src.bg);
        if (src.flags & FlagDefaultFg) {
            cell.fg.type = uint8_t(cell.fg.type | VTERM_COLOR_DEFAULT_FG);
        }
        if (src.flags & FlagDefaultBg) {
            cell.bg.type = uint8_t(cell.bg.type | VTERM_COLOR_DEFAULT_BG);
        }
    }
    return 1;
}

void KoraCutTerminalView::onClearScrollback()
{
    m_trimmed += qint64(m_scrollback.size());
    m_scrollback.clear();
    m_hasSelection = false;
    updateScrollBar(true);
    scheduleUpdate();
}

void KoraCutTerminalView::clearScrollback()
{
    onClearScrollback();
}

void KoraCutTerminalView::onOutput(const char *bytes, size_t len)
{
    if (m_pty) {
        m_pty->write(QByteArray(bytes, qsizetype(len)));
    }
}

void KoraCutTerminalView::onOsc(int command, const char *str, size_t len, bool initial, bool final)
{
    // OSC 10 et 11 : un programme demande nos couleurs (Claude Code choisit ainsi son thème).
    QByteArray &pending = m_oscPending;
    if (initial) {
        pending.clear();
    }
    pending.append(str, qsizetype(len));
    if (!final) {
        return;
    }
    if ((command == 10 || command == 11) && pending == "?") {
        const QRgb c = command == 10 ? kDefaultFg : kDefaultBg;
        const QByteArray reply = QStringLiteral("\x1b]%1;rgb:%2%2/%3%3/%4%4\x1b\\")
                                     .arg(command)
                                     .arg(qRed(c), 2, 16, QLatin1Char('0'))
                                     .arg(qGreen(c), 2, 16, QLatin1Char('0'))
                                     .arg(qBlue(c), 2, 16, QLatin1Char('0'))
                                     .toLatin1();
        onOutput(reply.constData(), size_t(reply.size()));
    }
    pending.clear();
}

void KoraCutTerminalView::onSelection(const char *str, size_t len, bool initial, bool final)
{
    // OSC 52 : un programme met du texte dans le presse-papiers (copier depuis Claude Code, vim).
    QByteArray &text = m_clipText;
    if (initial) {
        text.clear();
    }
    text.append(str, qsizetype(len));
    if (final && text.size() < 1024 * 1024) {
        QGuiApplication::clipboard()->setText(QString::fromUtf8(text));
        text.clear();
    }
}

qint64 KoraCutTerminalView::firstAbsolute() const
{
    return m_trimmed;
}

qint64 KoraCutTerminalView::screenTopAbsolute() const
{
    return m_trimmed + qint64(m_scrollback.size());
}

KoraCutTerminalView::Line KoraCutTerminalView::lineAt(qint64 absolute) const
{
    const qint64 top = screenTopAbsolute();
    if (absolute < top) {
        const qint64 index = absolute - m_trimmed;
        if (index < 0 || index >= qint64(m_scrollback.size())) {
            return {};
        }
        return m_scrollback.at(size_t(index));
    }
    const int row = int(absolute - top);
    Line line;
    if (row < 0 || row >= m_rows) {
        return line;
    }
    line.reserve(size_t(m_cols));
    VTermScreenCell cell;
    for (int col = 0; col < m_cols; ++col) {
        VTermPos pos{row, col};
        if (vterm_screen_get_cell(m_screen, pos, &cell)) {
            line.push_back(convert(&cell));
        } else {
            line.push_back(Cell{U' ', kDefaultFg, kDefaultBg, 0, 1, FlagDefaultFg | FlagDefaultBg});
        }
    }
    return line;
}

void KoraCutTerminalView::updateScrollBar(bool keepBottom)
{
    QScrollBar *bar = verticalScrollBar();
    const int maximum = m_altScreen ? 0 : int(m_scrollback.size());
    const bool atBottom = keepBottom || bar->value() >= bar->maximum();
    const QSignalBlocker blocker(bar);
    bar->setRange(0, maximum);
    bar->setPageStep(m_rows);
    if (atBottom) {
        bar->setValue(maximum);
    }
}

void KoraCutTerminalView::scrollContentsBy(int, int)
{
    viewport()->update();
}

void KoraCutTerminalView::resizeTerminal()
{
    const QSize area = viewport()->size();
    const int cols = qMax(10, int(area.width() / m_cellW));
    const int rows = qMax(3, area.height() / qMax(1, m_cellH));
    if (cols == m_cols && rows == m_rows) {
        return;
    }
    m_cols = cols;
    m_rows = rows;
    vterm_set_size(m_vt, rows, cols);
    vterm_screen_flush_damage(m_screen);
    if (m_pty) {
        m_pty->resize(cols, rows);
    }
    updateScrollBar(true);
}

void KoraCutTerminalView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    resizeTerminal();
    viewport()->update();
}

QColor KoraCutTerminalView::defaultForeground() const
{
    return QColor::fromRgb(kDefaultFg);
}

QColor KoraCutTerminalView::defaultBackground() const
{
    return QColor::fromRgb(kDefaultBg);
}

bool KoraCutTerminalView::inSelection(qint64 line, int col) const
{
    if (!m_hasSelection) {
        return false;
    }
    qint64 l1 = m_selStartLine, l2 = m_selEndLine;
    int c1 = m_selStartCol, c2 = m_selEndCol;
    if (l1 > l2 || (l1 == l2 && c1 > c2)) {
        std::swap(l1, l2);
        std::swap(c1, c2);
    }
    if (line < l1 || line > l2) {
        return false;
    }
    if (line == l1 && col < c1) {
        return false;
    }
    if (line == l2 && col > c2) {
        return false;
    }
    return true;
}

void KoraCutTerminalView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    const QRect area = viewport()->rect();
    p.fillRect(area, m_reverse ? QColor::fromRgb(kDefaultFg) : defaultBackground());
    const qint64 top = m_altScreen ? screenTopAbsolute() : firstAbsolute() + verticalScrollBar()->value();
    const qint64 screenTop = screenTopAbsolute();
    const bool focused = hasFocus();

    for (int r = 0; r < m_rows; ++r) {
        const qint64 absolute = top + r;
        const Line line = lineAt(absolute);
        const int y = r * m_cellH;
        // Premier passage : les fonds (sélection et inverse compris).
        int col = 0;
        for (size_t i = 0; i < line.size() && col < m_cols; ++i) {
            const Cell &c = line.at(i);
            const int w = qMax(1, int(c.width));
            if (c.width == 0) {
                continue;
            }
            QRgb bg = (c.attrs & AttrReverse) ? c.fg : c.bg;
            bool paint = (c.attrs & AttrReverse) || !(c.flags & FlagDefaultBg);
            if (inSelection(absolute, col)) {
                bg = kSelection;
                paint = true;
            }
            if (paint) {
                p.fillRect(QRectF(col * m_cellW, y, w * m_cellW + 0.5, m_cellH), QColor::fromRgb(bg));
            }
            col += w;
        }
        // Une sélection qui passe au-delà de la fin de ligne : on la peint jusqu'au bord.
        if (m_hasSelection) {
            for (int c = col; c < m_cols; ++c) {
                if (inSelection(absolute, c)) {
                    p.fillRect(QRectF(c * m_cellW, y, m_cellW + 0.5, m_cellH), QColor::fromRgb(kSelection));
                }
            }
        }
        // Second passage : le texte, par suites de même style.
        col = 0;
        size_t i = 0;
        while (i < line.size() && col < m_cols) {
            const Cell &c = line.at(i);
            if (c.width == 0) {
                ++i;
                continue;
            }
            const int w = qMax(1, int(c.width));
            if (c.ch == U' ' || (c.attrs & AttrConceal)) {
                if (c.attrs & (AttrUnderline | AttrDoubleUnderline | AttrStrike)) {
                    // un soulignement sur un espace reste visible
                } else {
                    col += w;
                    ++i;
                    continue;
                }
            }
            const QRgb fg = (c.attrs & AttrReverse) ? c.bg : c.fg;
            const quint16 style = c.attrs & (AttrBold | AttrItalic);
            const QFont &font = style == (AttrBold | AttrItalic) ? m_fontBoldItalic : style == AttrBold ? m_fontBold : style == AttrItalic ? m_fontItalic : m_font;
            // Une suite de caractères ASCII de même style se dessine d'un coup ; le reste case par case.
            QString run;
            const int startCol = col;
            if (c.ch < 0x80 && w == 1) {
                while (i < line.size() && col < m_cols) {
                    const Cell &n = line.at(i);
                    if (n.width != 1 || n.ch >= 0x80 || n.attrs != c.attrs || ((n.attrs & AttrReverse) ? n.bg : n.fg) != fg) {
                        break;
                    }
                    run.append(QChar(ushort(n.ch)));
                    ++col;
                    ++i;
                }
            } else {
                run = QString::fromUcs4(&c.ch, 1);
                col += w;
                ++i;
            }
            p.setFont(font);
            p.setPen(QColor::fromRgb(fg));
            p.drawText(QPointF(startCol * m_cellW, y + m_ascent), run);
            const qreal x1 = startCol * m_cellW;
            const qreal x2 = col * m_cellW;
            if (c.attrs & (AttrUnderline | AttrDoubleUnderline)) {
                p.drawLine(QPointF(x1, y + m_cellH - 1.5), QPointF(x2, y + m_cellH - 1.5));
                if (c.attrs & AttrDoubleUnderline) {
                    p.drawLine(QPointF(x1, y + m_cellH - 3.5), QPointF(x2, y + m_cellH - 3.5));
                }
            }
            if (c.attrs & AttrStrike) {
                p.drawLine(QPointF(x1, y + m_cellH / 2.0), QPointF(x2, y + m_cellH / 2.0));
            }
        }
    }

    // Le curseur, s'il est dans la partie visible.
    const qint64 cursorAbsolute = screenTop + m_cursorRow;
    if (m_cursorVisible && !m_exited && cursorAbsolute >= top && cursorAbsolute < top + m_rows) {
        const int r = int(cursorAbsolute - top);
        const QRectF cell(m_cursorCol * m_cellW, r * m_cellH, m_cellW, m_cellH);
        const QColor color = QColor::fromRgb(kCursor);
        if (!focused) {
            p.setPen(color);
            p.setBrush(Qt::NoBrush);
            p.drawRect(cell.adjusted(0.5, 0.5, -0.5, -0.5));
        } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
            p.fillRect(QRectF(cell.left(), cell.bottom() - 2, cell.width(), 2), color);
        } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) {
            p.fillRect(QRectF(cell.left(), cell.top(), 2, cell.height()), color);
        } else {
            p.fillRect(cell, color);
            VTermScreenCell vc;
            if (vterm_screen_get_cell(m_screen, VTermPos{m_cursorRow, m_cursorCol}, &vc) && vc.chars[0]) {
                const Cell c = convert(&vc);
                p.setFont((c.attrs & AttrBold) ? m_fontBold : m_font);
                p.setPen(defaultBackground());
                p.drawText(QPointF(cell.left(), cell.top() + m_ascent), QString::fromUcs4(&c.ch, 1));
            }
        }
    }
}

// ---------------------------------------------------------------- clavier

int KoraCutTerminalView::vtermModifiers(Qt::KeyboardModifiers mods) const
{
    int m = VTERM_MOD_NONE;
    if (mods & Qt::ShiftModifier) {
        m |= VTERM_MOD_SHIFT;
    }
#if defined(Q_OS_MACOS)
    // Sur Mac, Qt appelle Meta la touche Contrôle et Control la touche Commande.
    if (mods & Qt::MetaModifier) {
        m |= VTERM_MOD_CTRL;
    }
#else
    if (mods & Qt::ControlModifier) {
        m |= VTERM_MOD_CTRL;
    }
#endif
    if (mods & Qt::AltModifier) {
        m |= VTERM_MOD_ALT;
    }
    return m;
}

bool KoraCutTerminalView::handleShortcut(QKeyEvent *event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();
#if defined(Q_OS_MACOS)
    // ⌘C, ⌘V, ⌘A, ⌘K, ⌘+, ⌘-, ⌘0 : comme Terminal.app.
    if (!(mods & Qt::ControlModifier)) {
        return false;
    }
    const bool shift = false;
#else
    // Ctrl+Maj+C, Ctrl+Maj+V, Maj+Inser, Ctrl+Maj+A, Ctrl+Maj+K, Ctrl+plus, Ctrl+moins, Ctrl+0.
    if (key == Qt::Key_Insert && mods == Qt::ShiftModifier) {
        paste();
        return true;
    }
    if (!(mods & Qt::ControlModifier)) {
        return false;
    }
    const bool shift = mods & Qt::ShiftModifier;
#endif
    Q_UNUSED(shift)
    switch (key) {
    case Qt::Key_C:
#if !defined(Q_OS_MACOS)
        if (!shift) {
            return false;
        }
#endif
        copySelection();
        return true;
    case Qt::Key_V:
#if !defined(Q_OS_MACOS)
        if (!shift) {
            return false;
        }
#endif
        paste();
        return true;
    case Qt::Key_A:
#if !defined(Q_OS_MACOS)
        if (!shift) {
            return false;
        }
#endif
        selectAll();
        return true;
    case Qt::Key_K:
#if !defined(Q_OS_MACOS)
        if (!shift) {
            return false;
        }
#endif
        clearScrollback();
        return true;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoom(1);
        return true;
    case Qt::Key_Minus:
        zoom(-1);
        return true;
    case Qt::Key_0:
        zoom(0);
        return true;
    default:
        return false;
    }
}

bool KoraCutTerminalView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const int key = ke->key();
        const Qt::KeyboardModifiers mods = ke->modifiers();
        // ⌃` ouvre et ferme le terminal, comme dans VS Code : on le laisse à l'application.
        // ⌃ù aussi, son équivalent sur un clavier AZERTY (25 septembre 2026).
#if defined(Q_OS_MACOS)
        const bool ctrl = mods & Qt::MetaModifier;
        const bool cmd = mods & Qt::ControlModifier;
#else
        const bool ctrl = mods & Qt::ControlModifier;
        const bool cmd = false;
#endif
        if (ctrl && (key == Qt::Key_QuoteLeft || key == Qt::Key_twosuperior || key == Qt::Key_At || key == Qt::Key_Ugrave)) {
            return QAbstractScrollArea::event(event);
        }
        if (cmd) {
            // Sur Mac, ⌘ reste à l'application (enregistrer, quitter…), sauf nos raccourcis de terminal.
            if (key == Qt::Key_C || key == Qt::Key_V || key == Qt::Key_A || key == Qt::Key_K || key == Qt::Key_Plus || key == Qt::Key_Equal ||
                key == Qt::Key_Minus || key == Qt::Key_0) {
                event->accept();
                return true;
            }
            return QAbstractScrollArea::event(event);
        }
        // Tout le reste va au terminal : Espace, les lettres, Échap… ne déclenchent plus les outils de montage.
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            keyPressEvent(ke);
            return true;
        }
    }
    return QAbstractScrollArea::event(event);
}

bool KoraCutTerminalView::focusNextPrevChild(bool)
{
    return false;
}

void KoraCutTerminalView::keyPressEvent(QKeyEvent *event)
{
    if (handleShortcut(event)) {
        event->accept();
        return;
    }
    if (m_exited) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            Q_EMIT restartRequested();
        }
        event->accept();
        return;
    }
    // Taper ramène en bas et efface la sélection, comme partout.
    if (verticalScrollBar()->value() != verticalScrollBar()->maximum()) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
    const Qt::KeyboardModifiers mods = event->modifiers();
    const int vmods = vtermModifiers(mods);
    VTermKey vkey = VTERM_KEY_NONE;
    switch (event->key()) {
    case Qt::Key_Return:
        vkey = VTERM_KEY_ENTER;
        break;
    case Qt::Key_Enter:
        vkey = (mods & Qt::KeypadModifier) ? VTERM_KEY_KP_ENTER : VTERM_KEY_ENTER;
        break;
    case Qt::Key_Tab:
        vkey = VTERM_KEY_TAB;
        break;
    case Qt::Key_Backtab:
        vkey = VTERM_KEY_TAB;
        break;
    case Qt::Key_Backspace:
        vkey = VTERM_KEY_BACKSPACE;
        break;
    case Qt::Key_Escape:
        vkey = VTERM_KEY_ESCAPE;
        break;
    case Qt::Key_Up:
        vkey = VTERM_KEY_UP;
        break;
    case Qt::Key_Down:
        vkey = VTERM_KEY_DOWN;
        break;
    case Qt::Key_Left:
        vkey = VTERM_KEY_LEFT;
        break;
    case Qt::Key_Right:
        vkey = VTERM_KEY_RIGHT;
        break;
    case Qt::Key_Insert:
        vkey = VTERM_KEY_INS;
        break;
    case Qt::Key_Delete:
        vkey = VTERM_KEY_DEL;
        break;
    case Qt::Key_Home:
        vkey = VTERM_KEY_HOME;
        break;
    case Qt::Key_End:
        vkey = VTERM_KEY_END;
        break;
    case Qt::Key_PageUp:
        vkey = VTERM_KEY_PAGEUP;
        break;
    case Qt::Key_PageDown:
        vkey = VTERM_KEY_PAGEDOWN;
        break;
    default:
        if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F35) {
            vkey = VTermKey(VTERM_KEY_FUNCTION(event->key() - Qt::Key_F1 + 1));
        }
        break;
    }
    if (vkey != VTERM_KEY_NONE) {
        int m = vmods;
        if (event->key() == Qt::Key_Backtab) {
            m |= VTERM_MOD_SHIFT;
        }
        vterm_keyboard_key(m_vt, vkey, VTermModifier(m));
        event->accept();
        return;
    }

    const bool ctrl = vmods & VTERM_MOD_CTRL;
    const int key = event->key();
    if (ctrl) {
        // Contrôle + lettre : le caractère de contrôle (⌃C interrompt, ⌃D ferme…).
        if (key >= Qt::Key_A && key <= Qt::Key_Z) {
            vterm_keyboard_unichar(m_vt, uint32_t('a' + (key - Qt::Key_A)), VTermModifier(vmods & ~VTERM_MOD_SHIFT));
            event->accept();
            return;
        }
        uint32_t c = 0;
        switch (key) {
        case Qt::Key_Space:
        case Qt::Key_At:
            c = '@';
            break;
        case Qt::Key_BracketLeft:
            c = '[';
            break;
        case Qt::Key_Backslash:
            c = '\\';
            break;
        case Qt::Key_BracketRight:
            c = ']';
            break;
        case Qt::Key_AsciiCircum:
            c = '^';
            break;
        case Qt::Key_Underscore:
            c = '_';
            break;
        default:
            break;
        }
        if (c) {
            vterm_keyboard_unichar(m_vt, c, VTERM_MOD_CTRL);
            event->accept();
            return;
        }
    }

    const QString text = event->text();
    if (text.isEmpty()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
#if defined(Q_OS_MACOS)
    // Option compose les caractères (@, {, é sur un clavier AZERTY) : on envoie le texte tel quel.
    const int textMods = VTERM_MOD_NONE;
#else
    // Alt seul : préfixe Échap (Alt+b recule d'un mot). AltGr (Ctrl+Alt sous Windows) compose du texte.
    const int textMods = ((mods & Qt::AltModifier) && !(mods & Qt::ControlModifier)) ? VTERM_MOD_ALT : VTERM_MOD_NONE;
#endif
    const QList<uint> ucs = text.toUcs4();
    for (uint u : ucs) {
        if (u < 0x20 || u == 0x7F) {
            // Un caractère de contrôle déjà composé par le système : tel quel.
            const char b = char(u);
            onOutput(&b, 1);
            continue;
        }
        vterm_keyboard_unichar(m_vt, u, VTermModifier(textMods));
    }
    event->accept();
}

void KoraCutTerminalView::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty() && !m_exited) {
        const QList<uint> ucs = event->commitString().toUcs4();
        for (uint u : ucs) {
            vterm_keyboard_unichar(m_vt, u, VTERM_MOD_NONE);
        }
    }
    event->accept();
}

QVariant KoraCutTerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImCursorRectangle) {
        return QRectF(m_cursorCol * m_cellW, m_cursorRow * m_cellH, m_cellW, m_cellH).toRect();
    }
    if (query == Qt::ImFont) {
        return m_font;
    }
    return QAbstractScrollArea::inputMethodQuery(query);
}

void KoraCutTerminalView::focusInEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusInEvent(event);
    vterm_state_focus_in(vterm_obtain_state(m_vt));
    viewport()->update();
}

void KoraCutTerminalView::focusOutEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusOutEvent(event);
    vterm_state_focus_out(vterm_obtain_state(m_vt));
    viewport()->update();
}

// ---------------------------------------------------------------- souris et sélection

void KoraCutTerminalView::cellAt(const QPointF &pos, qint64 &line, int &col) const
{
    const qint64 top = m_altScreen ? screenTopAbsolute() : firstAbsolute() + verticalScrollBar()->value();
    const int row = qBound(0, int(pos.y() / m_cellH), m_rows - 1);
    col = qBound(0, int(pos.x() / m_cellW), m_cols - 1);
    line = top + row;
}

bool KoraCutTerminalView::mouseReporting(Qt::KeyboardModifiers mods) const
{
    // Maj garde la sélection à la main même quand le programme veut la souris.
    return m_mouseMode != VTERM_PROP_MOUSE_NONE && !(mods & Qt::ShiftModifier) && !m_exited;
}

void KoraCutTerminalView::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (mouseReporting(event->modifiers())) {
        qint64 line;
        int col;
        cellAt(event->position(), line, col);
        const int row = int(line - screenTopAbsolute());
        const int button = event->button() == Qt::LeftButton ? 1 : event->button() == Qt::MiddleButton ? 2 : event->button() == Qt::RightButton ? 3 : 0;
        if (button && row >= 0) {
            vterm_mouse_move(m_vt, row, col, VTermModifier(vtermModifiers(event->modifiers())));
            vterm_mouse_button(m_vt, button, true, VTermModifier(vtermModifiers(event->modifiers())));
            ++m_mouseButtonsDown;
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        cellAt(event->position(), m_selStartLine, m_selStartCol);
        m_selEndLine = m_selStartLine;
        m_selEndCol = m_selStartCol;
        m_selecting = true;
        m_hasSelection = false;
        viewport()->update();
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        paste();
        event->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void KoraCutTerminalView::mouseMoveEvent(QMouseEvent *event)
{
    if (mouseReporting(event->modifiers())) {
        if (m_mouseMode >= VTERM_PROP_MOUSE_DRAG) {
            qint64 line;
            int col;
            cellAt(event->position(), line, col);
            const int row = int(line - screenTopAbsolute());
            if (row >= 0 && (m_mouseButtonsDown > 0 || m_mouseMode == VTERM_PROP_MOUSE_MOVE)) {
                vterm_mouse_move(m_vt, row, col, VTermModifier(vtermModifiers(event->modifiers())));
            }
        }
        event->accept();
        return;
    }
    if (m_selecting) {
        cellAt(event->position(), m_selEndLine, m_selEndCol);
        m_hasSelection = m_selEndLine != m_selStartLine || m_selEndCol != m_selStartCol;
        // Faire glisser au-dessus ou en dessous fait défiler.
        if (event->position().y() < 0) {
            verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
        } else if (event->position().y() > viewport()->height()) {
            verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
        }
        viewport()->update();
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void KoraCutTerminalView::mouseReleaseEvent(QMouseEvent *event)
{
    if (mouseReporting(event->modifiers()) && m_mouseButtonsDown > 0) {
        const int button = event->button() == Qt::LeftButton ? 1 : event->button() == Qt::MiddleButton ? 2 : event->button() == Qt::RightButton ? 3 : 0;
        if (button) {
            vterm_mouse_button(m_vt, button, false, VTermModifier(vtermModifiers(event->modifiers())));
        }
        m_mouseButtonsDown = qMax(0, m_mouseButtonsDown - 1);
        event->accept();
        return;
    }
    if (m_selecting) {
        m_selecting = false;
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void KoraCutTerminalView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (mouseReporting(event->modifiers()) || event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }
    // Double clic : le mot (un chemin, un nom de fichier) sous la souris.
    qint64 line;
    int col;
    cellAt(event->position(), line, col);
    const Line cells = lineAt(line);
    if (col >= int(cells.size()) || !isWordChar(cells.at(size_t(col)).ch)) {
        return;
    }
    int a = col, b = col;
    while (a > 0 && isWordChar(cells.at(size_t(a - 1)).ch)) {
        --a;
    }
    while (b + 1 < int(cells.size()) && isWordChar(cells.at(size_t(b + 1)).ch)) {
        ++b;
    }
    m_selStartLine = m_selEndLine = line;
    m_selStartCol = a;
    m_selEndCol = b;
    m_hasSelection = true;
    m_selecting = false;
    viewport()->update();
    event->accept();
}

void KoraCutTerminalView::wheelEvent(QWheelEvent *event)
{
    // Un pavé tactile envoie des pixels, une molette des crans : on compte en lignes.
    QPointF delta = event->pixelDelta().isNull() ? QPointF(event->angleDelta()) / 40.0 : QPointF(event->pixelDelta()) / qreal(m_cellH);
    m_wheelRemainder += delta;
    const int lines = int(m_wheelRemainder.y());
    if (lines == 0) {
        event->accept();
        return;
    }
    m_wheelRemainder.setY(m_wheelRemainder.y() - lines);
    if (mouseReporting(event->modifiers())) {
        qint64 line;
        int col;
        cellAt(event->position(), line, col);
        const int row = qMax(0, int(line - screenTopAbsolute()));
        vterm_mouse_move(m_vt, row, col, VTERM_MOD_NONE);
        for (int i = 0; i < qMin(qAbs(lines), 10); ++i) {
            vterm_mouse_button(m_vt, lines > 0 ? 4 : 5, true, VTERM_MOD_NONE);
        }
    } else if (m_altScreen) {
        // Écran alterné sans souris (less, man) : la molette devient des flèches.
        for (int i = 0; i < qMin(qAbs(lines), 10); ++i) {
            vterm_keyboard_key(m_vt, lines > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        }
    } else {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - lines);
    }
    event->accept();
}

QString KoraCutTerminalView::selectedText() const
{
    if (!m_hasSelection) {
        return QString();
    }
    qint64 l1 = m_selStartLine, l2 = m_selEndLine;
    int c1 = m_selStartCol, c2 = m_selEndCol;
    if (l1 > l2 || (l1 == l2 && c1 > c2)) {
        std::swap(l1, l2);
        std::swap(c1, c2);
    }
    QStringList out;
    for (qint64 l = l1; l <= l2; ++l) {
        const Line cells = lineAt(l);
        QString text;
        const int from = l == l1 ? c1 : 0;
        const int to = l == l2 ? c2 : int(cells.size()) - 1;
        for (int c = from; c <= to && c < int(cells.size()); ++c) {
            const Cell &cell = cells.at(size_t(c));
            if (cell.width == 0) {
                continue;
            }
            text.append(QString::fromUcs4(&cell.ch, 1));
        }
        // Les espaces de fin de ligne ne sont que du vide d'écran.
        while (text.endsWith(QLatin1Char(' '))) {
            text.chop(1);
        }
        out << text;
    }
    return out.join(QLatin1Char('\n'));
}

void KoraCutTerminalView::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
    }
}

void KoraCutTerminalView::paste()
{
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty() || !isRunning()) {
        return;
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    vterm_keyboard_start_paste(m_vt);
    m_pty->write(text.toUtf8());
    vterm_keyboard_end_paste(m_vt);
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void KoraCutTerminalView::selectAll()
{
    m_selStartLine = firstAbsolute();
    m_selStartCol = 0;
    m_selEndLine = screenTopAbsolute() + m_rows - 1;
    m_selEndCol = m_cols - 1;
    m_hasSelection = true;
    viewport()->update();
}

void KoraCutTerminalView::contextMenuEvent(QContextMenuEvent *event)
{
    if (mouseReporting(event->modifiers())) {
        return;
    }
    QMenu menu(this);
#if defined(Q_OS_MACOS)
    const QString copyKeys = QStringLiteral("⌘C");
    const QString pasteKeys = QStringLiteral("⌘V");
#else
    const QString copyKeys = QStringLiteral("Ctrl+Shift+C");
    const QString pasteKeys = QStringLiteral("Ctrl+Shift+V");
#endif
    QAction *copy = menu.addAction(i18nc("@action:inmenu integrated terminal", "Copy") + QLatin1Char('\t') + copyKeys, this, &KoraCutTerminalView::copySelection);
    copy->setEnabled(m_hasSelection);
    QAction *pasteAction =
        menu.addAction(i18nc("@action:inmenu integrated terminal", "Paste") + QLatin1Char('\t') + pasteKeys, this, &KoraCutTerminalView::paste);
    pasteAction->setEnabled(isRunning() && !QGuiApplication::clipboard()->text().isEmpty());
    menu.addAction(i18nc("@action:inmenu integrated terminal", "Select All"), this, &KoraCutTerminalView::selectAll);
    menu.addSeparator();
    menu.addAction(i18nc("@action:inmenu integrated terminal", "Clear Scrollback"), this, &KoraCutTerminalView::clearScrollback);
    menu.addAction(i18nc("@action:inmenu integrated terminal", "Larger Text"), this, [this]() { zoom(1); });
    menu.addAction(i18nc("@action:inmenu integrated terminal", "Smaller Text"), this, [this]() { zoom(-1); });
    menu.exec(event->globalPos());
}

QStringList KoraCutTerminalView::screenLines() const
{
    QStringList out;
    const qint64 top = screenTopAbsolute();
    for (int r = 0; r < m_rows; ++r) {
        const Line cells = lineAt(top + r);
        QString text;
        for (const Cell &c : cells) {
            if (c.width == 0) {
                continue;
            }
            text.append(QString::fromUcs4(&c.ch, 1));
        }
        while (text.endsWith(QLatin1Char(' '))) {
            text.chop(1);
        }
        out << text;
    }
    return out;
}

QString KoraCutTerminalView::allText() const
{
    QStringList out;
    for (const Line &cells : m_scrollback) {
        QString text;
        for (const Cell &c : cells) {
            if (c.width == 0) {
                continue;
            }
            text.append(QString::fromUcs4(&c.ch, 1));
        }
        out << text;
    }
    out << screenLines();
    while (!out.isEmpty() && out.last().isEmpty()) {
        out.removeLast();
    }
    return out.join(QLatin1Char('\n'));
}
