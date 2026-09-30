/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutscript.h"

#include <QPair>
#include <QPointer>
#include <QWidget>

class KdenliveDoc;
class QLabel;
class QMenu;
class QScrollArea;
class QTimer;
class QToolButton;
class QVBoxLayout;

/** @brief Le panneau Script : l'éditeur en blocs du script du projet (document 21).
 *
 * Un canevas vertical de blocs (titres, paragraphes, listes, tableaux, séparateurs), un « + » au
 * survol entre deux blocs, un « + » à droite des tableaux pour une colonne et en bas pour une ligne.
 * Chaque geste est une étape d'annulation du projet ; une frappe continue n'en fait qu'une.
 * Le script s'enregistre dans le projet (KoraCutScriptStore), les agents le lisent et l'écrivent
 * par les outils script_lire, script_ecrire et script_modele.
 */
class KoraCutScriptPanel : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutScriptPanel(QWidget *parent = nullptr);

    /** @brief Relit le script du document courant et redessine. */
    void reload();

    /** @brief Les pseudo-actions de capture (`script:…`, CLAUDE.md « Se relire sans écran ») :
     * `modele`, `ligne`, `colonne=<type ou libellé>`, `bloc=<type>`, `etat`, `markdown=<fichier>`,
     * `csv=<fichier>`. Rend une ligne à écrire dans le journal. */
    QString debugCommand(const QString &command);

Q_SIGNALS:
    void seekRequested(int frame);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void rebuild();
    void refreshHeader();
    /** Enregistre m_script sans redessiner (le geste vient d'ici). */
    void commit(const QString &undoText, const QString &mergeKey = QString());
    /** Enregistre m_script, puis redessine et donne le focus à @p focusBlock. */
    void commitStructure(const QString &undoText, const QString &focusBlock);
    void scheduleTextCommit(const QString &blockId);
    void flushTextCommit();
    void insertBlock(int at, KoraCutScriptBlock::Kind kind);
    QMenu *insertMenu(QWidget *parent, int at);
    void showBlockMenu(const QString &blockId, const QPoint &globalPos);
    QWidget *blockWidget(const KoraCutScriptBlock &block);
    QWidget *gapWidget(int at);
    void placeMarker(const QString &blockId, const QString &rowId);
    void linkZone(const QString &blockId, const QString &rowId);
    void exportMarkdown();
    void exportCsv();
    void importCsv();
    void resetToTemplate();
    QString defaultExportPath(const QString &suffix) const;
    KoraCutScriptBlock *targetTable();
    /** @name Chantier 5 : la voix off tirée du script */
    ///@{
    /** Les phrases à dire : les lignes @p rowIds du tableau @p blockId (toutes si vide), ou le texte du bloc. */
    QList<QPair<QString, QString>> voiceLines(const QString &blockId, const QStringList &rowIds, QList<int> *positions) const;
    void openVoiceOver(const QString &blockId, const QStringList &rowIds);
    /** « Générer la voix off » du bouton : la sélection du moment, sinon tout le script. */
    void voiceOverFromSelection();
    void voiceOverWholeScript();
    ///@}

    KoraCutScript m_script;
    bool m_fromTemplate{true};
    bool m_selfUpdate{false};
    QPointer<KdenliveDoc> m_doc;
    QString m_focusBlock;
    QString m_lastTable;
    QString m_pendingText;
    QString m_pendingUndoText;

    QLabel *m_title{nullptr};
    QLabel *m_typeChip{nullptr};
    QLabel *m_hint{nullptr};
    QToolButton *m_addButton{nullptr};
    QToolButton *m_menuButton{nullptr};
    QToolButton *m_voiceButton{nullptr};
    QScrollArea *m_scroll{nullptr};
    QWidget *m_canvas{nullptr};
    QVBoxLayout *m_blocks{nullptr};
    QTimer *m_textTimer{nullptr};
};
