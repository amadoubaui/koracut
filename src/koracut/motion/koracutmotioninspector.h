/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QWidget>

class KoraCutMotionMirror;
class KoraCutMotionStudio;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

/** @brief La section Motion de l'inspecteur, au-dessus de la pile d'effets d'un clip Motion.
 *
 * Le texte d'un bloc (champ multi-lignes, Entrée applique, Maj+Entrée saute une ligne), le fichier
 * d'un média, les variables de la composition (`data-composition-variables`, une ligne par variable)
 * et « Modifier l'animation dans Studio ». Un double-clic sur un clip Motion de titre dans la
 * timeline donne le focus au champ texte.
 */
class KoraCutMotionInspector : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutMotionInspector(KoraCutMotionMirror *mirror, KoraCutMotionStudio *studio, QWidget *parent = nullptr);

    /** @brief Montre ce clip de timeline s'il vient d'un projet Motion, sinon se cache. */
    void showClip(int clipId);
    void clear();
    void focusText();
    int clipId() const { return m_clipId; }

private:
    void applyText();
    void applyFile();
    void refresh();

    KoraCutMotionMirror *m_mirror;
    KoraCutMotionStudio *m_studio;
    int m_clipId{-1};
    QString m_project;
    QString m_elementId;
    QLabel *m_title;
    QLabel *m_textLabel;
    QPlainTextEdit *m_text;
    QLabel *m_fileLabel;
    QLineEdit *m_file;
    QPushButton *m_browse;
    QLabel *m_variablesLabel;
    QLabel *m_variables;
    QPushButton *m_studioButton;
};
