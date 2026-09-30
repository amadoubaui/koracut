/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QFont>
#include <QPoint>
#include <QProcessEnvironment>

#include <deque>
#include <vector>

class KoraCutPty;
class QTimer;

struct VTerm;
struct VTermScreen;

/** @brief Un terminal dessiné dans KoraCut : libvterm pour comprendre les séquences, un PTY pour le
 *  processus, un QPainter pour l'écran (document 18, journal du 25 septembre 2026).
 *
 * Couleurs ANSI (16, 256 et 24 bits), curseur, écran alterné des applications plein écran (Claude
 * Code, vim, htop), redimensionnement, défilement de 5 000 lignes, sélection, copier et coller
 * (collage entre crochets quand l'application le demande), souris quand l'application la demande.
 * Rien ne tourne tant que rien n'arrive : pas de curseur qui clignote, pas de minuterie.
 */
class KoraCutTerminalView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit KoraCutTerminalView(QWidget *parent = nullptr);
    ~KoraCutTerminalView() override;

    /** @brief Lance @p program dans le PTY. Rend false avec @p error rempli si rien n'a démarré. */
    bool start(const QString &program, const QStringList &args, const QString &workDir, const QProcessEnvironment &env, QString *error = nullptr);
    bool isRunning() const;
    /** @brief Tape @p text comme au clavier (sans crochets de collage). */
    void sendText(const QString &text);
    /** @brief Écrit dans l'écran comme si le processus l'avait fait (messages de KoraCut). */
    void feed(const QByteArray &data);

    /** @brief Les lignes de l'écran visible, espaces de fin retirés (essais, capture de texte). */
    QStringList screenLines() const;
    /** @brief Tout le texte : le défilement puis l'écran. */
    QString allText() const;
    QString title() const;
    int columns() const;
    int rows() const;
    KoraCutPty *pty() const;

    void copySelection();
    void paste();
    void selectAll();
    void clearScrollback();
    void zoom(int delta);

    /** @brief Les dimensions d'une case, en pixels logiques. */
    QSizeF cellSize() const;

Q_SIGNALS:
    void titleChanged(const QString &title);
    void finished(int exitCode);
    /** @brief Entrée appuyée après la fin du processus : le panneau relance un shell. */
    void restartRequested();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool focusNextPrevChild(bool next) override;

public:
    // Une case telle qu'on la garde dans le défilement : 16 octets.
    struct Cell
    {
        char32_t ch{U' '};
        QRgb fg{0};
        QRgb bg{0};
        quint16 attrs{0};
        quint8 width{1};
        quint8 flags{0}; // 1 : couleur de texte par défaut, 2 : fond par défaut
    };
    using Line = std::vector<Cell>;

    // Rappels de libvterm (fonctions libres dans le .cpp).
    void onDamage();
    void onMoveCursor(int row, int col, bool visible);
    void onTermProp(int prop, const void *value);
    void onPushLine(int cols, const void *cells);
    int onPopLine(int cols, void *cells);
    void onClearScrollback();
    void onOutput(const char *bytes, size_t len);
    void onOsc(int command, const char *str, size_t len, bool initial, bool final);
    void onSelection(const char *str, size_t len, bool initial, bool final);

private:
    void setupFont();
    void resizeTerminal();
    void scheduleUpdate();
    void updateScrollBar(bool keepBottom);
    Line lineAt(qint64 absolute) const;
    qint64 firstAbsolute() const;
    qint64 screenTopAbsolute() const;
    void cellAt(const QPointF &pos, qint64 &line, int &col) const;
    bool mouseReporting(Qt::KeyboardModifiers mods) const;
    int vtermModifiers(Qt::KeyboardModifiers mods) const;
    QString selectedText() const;
    bool inSelection(qint64 line, int col) const;
    Cell convert(const void *screenCell) const;
    QColor defaultForeground() const;
    QColor defaultBackground() const;
    bool handleShortcut(QKeyEvent *event);

    VTerm *m_vt{nullptr};
    VTermScreen *m_screen{nullptr};
    KoraCutPty *m_pty{nullptr};
    QTimer *m_repaint{nullptr};
    std::deque<Line> m_scrollback;
    qint64 m_trimmed{0};
    int m_maxScrollback{5000};
    int m_rows{24};
    int m_cols{80};
    int m_cursorRow{0};
    int m_cursorCol{0};
    bool m_cursorVisible{true};
    int m_cursorShape{1};
    bool m_altScreen{false};
    int m_mouseMode{0};
    bool m_reverse{false};
    QString m_title;
    QByteArray m_oscBuffer;
    bool m_exited{false};

    QFont m_font;
    QFont m_fontBold;
    QFont m_fontItalic;
    QFont m_fontBoldItalic;
    int m_fontPixels{12};
    qreal m_cellW{7.0};
    int m_cellH{15};
    int m_ascent{12};

    // La sélection, en lignes absolues (elles ne bougent pas quand le texte défile).
    bool m_selecting{false};
    bool m_hasSelection{false};
    qint64 m_selStartLine{0};
    int m_selStartCol{0};
    qint64 m_selEndLine{0};
    int m_selEndCol{0};
    QByteArray m_clipboardBuffer;
    QByteArray m_oscPending;
    QByteArray m_clipText;
    int m_mouseButtonsDown{0};
    QPointF m_wheelRemainder;
};
