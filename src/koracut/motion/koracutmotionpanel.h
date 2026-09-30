/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutmotioncodeeditor.h"

#include <QWidget>

class KoraCutMotionInstaller;
class KoraCutMotionMirror;
class KoraCutMotionStudio;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QToolButton;
class QTreeWidget;

/** @brief Le panneau « Motion » (dock `motion`, onglet du bloc Médias).
 *
 * Onglet Compositions : la liste des projets Motion du projet (nom, éléments, rendus), les éléments
 * du projet choisi, « Nouvelle composition » (gabarit vide, import d'un dossier ou d'un .html),
 * « Rendre tout », « Ouvrir dans Studio », l'état de HyperFrames (installé, à installer, Node absent)
 * et le journal du dernier rendu (repliable).
 * Onglet Code : l'index.html du projet choisi, coloré, numéroté, enregistré 800 ms après la dernière
 * frappe (ou ⌘S) ; `hyperframes lint` à chaque enregistrement, ses messages sous l'éditeur, un clic
 * sur un message met le curseur à la ligne. Un enregistrement passe par le synchroniseur comme un
 * changement extérieur : la timeline suit.
 * Charte produit : `2 - Charte graphique KoraCut.md`, couleurs dans data/koracut.qss (koracutMotion…).
 */
class KoraCutMotionPanel : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutMotionPanel(KoraCutMotionMirror *mirror, KoraCutMotionStudio *studio, QWidget *parent = nullptr);

    QString currentProject() const;
    void selectProject(const QString &name);
    void showCode(const QString &name = QString());
    /** @brief Pour les essais : importe sans fenêtre. */
    QString importWithoutDialog(const QString &path, const QString &name = QString());
    void renderAll();

Q_SIGNALS:
    void requestOpenStudio(const QString &projectDir);

private:
    void buildCompositionsTab();
    void buildCodeTab();
    void refreshProjects();
    void refreshElements();
    void refreshHostState();
    void refreshLog();
    void newComposition();
    void openStudio();
    void loadCode();
    void saveCode(const QString &text);
    void showLint(const QList<KoraCutMotionLintMessage> &messages, const QString &error);

    KoraCutMotionMirror *m_mirror;
    KoraCutMotionStudio *m_studio;
    KoraCutMotionInstaller *m_installer;
    KoraCutMotionLinter *m_linter;
    QTabWidget *m_tabs{nullptr};
    // Compositions
    QTreeWidget *m_projects{nullptr};
    QTreeWidget *m_elements{nullptr};
    QPushButton *m_newButton{nullptr};
    QPushButton *m_renderButton{nullptr};
    QPushButton *m_studioButton{nullptr};
    QLabel *m_hostState{nullptr};
    QPushButton *m_installButton{nullptr};
    QPushButton *m_updateButton{nullptr};
    QToolButton *m_logToggle{nullptr};
    QPlainTextEdit *m_log{nullptr};
    QLabel *m_emptyHint{nullptr};
    // Code
    QLabel *m_codeFile{nullptr};
    QPushButton *m_saveButton{nullptr};
    KoraCutMotionCodeEditor *m_editor{nullptr};
    QListWidget *m_lint{nullptr};
    QLabel *m_lintState{nullptr};
    QString m_codeProject;
};
