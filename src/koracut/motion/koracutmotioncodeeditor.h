/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QWidget>

class QLabel;
class QListWidget;
class QProcess;
class QTimer;

/** @brief Coloration simple du HTML d'une composition : balises, attributs, chaînes, commentaires,
 * et dans script et style les mots-clés, nombres et commentaires. */
class KoraCutMotionHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit KoraCutMotionHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;

private:
    enum State { Html = 0, HtmlComment = 1, Script = 2, Style = 3, ScriptComment = 4, StyleComment = 5 };
    void highlightHtml(const QString &text, int from, int to);
    void highlightCode(const QString &text, int from, int to, bool style);
    QTextCharFormat m_tag;
    QTextCharFormat m_attribute;
    QTextCharFormat m_string;
    QTextCharFormat m_comment;
    QTextCharFormat m_keyword;
    QTextCharFormat m_number;
    QTextCharFormat m_property;
    QRegularExpression m_keywords;
};

/** @brief L'éditeur du HTML (onglet Code du panneau Motion) : monospace, numéros de ligne,
 * enregistrement automatique 800 ms après la dernière frappe (ou ⌘S). */
class KoraCutMotionCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit KoraCutMotionCodeEditor(QWidget *parent = nullptr);

    /** @brief Le texte à montrer, sans déclencher d'enregistrement. */
    void setContent(const QString &text);
    /** @brief Vrai si le texte diffère du contenu posé par setContent (ou enregistré). */
    bool isDirty() const { return m_dirty; }
    void goToLine(int line);
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

    /** @brief Enregistre tout de suite (⌘S ou le bouton). */
    void saveNow();

Q_SIGNALS:
    /** @brief Le texte est à enregistrer : le récepteur écrit le fichier et rappelle setContent ou markSaved. */
    void saveRequested(const QString &text);

public Q_SLOTS:
    void markSaved();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();

    QWidget *m_lineNumbers;
    QTimer *m_autosave;
    KoraCutMotionHighlighter *m_highlighter;
    bool m_dirty{false};
    bool m_settingContent{false};
};

/** @brief Un message de `hyperframes lint`. */
struct KoraCutMotionLintMessage
{
    QString severity; ///< error, warning, info
    QString code;
    QString message;
    QString file;
    int line = 0;
};

/** @brief Lance `hyperframes lint --json` sur un dossier, en fond, et rend les messages. */
class KoraCutMotionLinter : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutMotionLinter(QObject *parent = nullptr);
    void run(const QString &projectDir);
    bool isRunning() const;
    static QList<KoraCutMotionLintMessage> parse(const QByteArray &json);

Q_SIGNALS:
    void finished(const QList<KoraCutMotionLintMessage> &messages, const QString &error);

private:
    QProcess *m_process{nullptr};
    QString m_pendingDir;
};
