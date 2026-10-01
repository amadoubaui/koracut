/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutscriptpanel.h"
#include "koracutscripttable.h"

#include "bin/bin.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutmetrics.h"
#include "koracut/koracutvideotypes.h"
#include "koracut/voix/koracutvoiceoverdialog.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"

#include <KLocalizedString>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

bool noDialogs()
{
    return qApp && qApp->property("koracut_sans_fenetre").toBool();
}

// Un cadre qui montre certains de ses enfants seulement au survol : la poignée d'un bloc, le « + »
// entre deux blocs. Les enfants gardent leur place cachés, rien ne saute sous la souris.
class HoverFrame : public QFrame
{
public:
    using QFrame::QFrame;
    void reveal(QWidget *w)
    {
        QSizePolicy p = w->sizePolicy();
        p.setRetainSizeWhenHidden(true);
        w->setSizePolicy(p);
        w->hide();
        m_revealed << w;
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        for (const QPointer<QWidget> &w : std::as_const(m_revealed)) {
            if (w) {
                w->show();
            }
        }
        QFrame::enterEvent(event);
    }
    void leaveEvent(QEvent *event) override
    {
        for (const QPointer<QWidget> &w : std::as_const(m_revealed)) {
            if (w) {
                w->hide();
            }
        }
        QFrame::leaveEvent(event);
    }

private:
    QList<QPointer<QWidget>> m_revealed;
};

// Un champ de texte qui grandit avec son contenu (paragraphes et listes) et prévient quand il perd le focus.
class GrowingTextEdit : public QTextEdit
{
public:
    explicit GrowingTextEdit(QWidget *parent)
        : QTextEdit(parent)
    {
        setAcceptRichText(false);
        setFrameShape(QFrame::NoFrame);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setLineWrapMode(QTextEdit::WidgetWidth);
        setTabChangesFocus(true);
        document()->setDocumentMargin(3);
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this]() { fit(); });
    }
    std::function<void()> onFocusOut;

    void fit()
    {
        const int h = int(std::ceil(document()->size().height())) + 2 * frameWidth() + 2;
        if (h != height()) {
            setFixedHeight(std::max(h, fontMetrics().lineSpacing() + 10));
        }
    }

protected:
    void focusOutEvent(QFocusEvent *event) override
    {
        QTextEdit::focusOutEvent(event);
        if (onFocusOut) {
            onFocusOut();
        }
    }
    void resizeEvent(QResizeEvent *event) override
    {
        QTextEdit::resizeEvent(event);
        fit();
    }
    // La molette fait défiler la page, pas le paragraphe (il n'a pas de barre).
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};

QStringList listItems(const QTextDocument *doc)
{
    QStringList items;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        items << b.text();
    }
    // Les lignes vides en fin de liste ne comptent pas.
    while (items.size() > 1 && items.last().trimmed().isEmpty()) {
        items.removeLast();
    }
    return items;
}

void fillList(QTextEdit *edit, const QStringList &items)
{
    edit->clear();
    QTextCursor cursor(edit->document());
    QTextListFormat format;
    format.setStyle(QTextListFormat::ListDisc);
    format.setIndent(1);
    cursor.insertList(format);
    for (int i = 0; i < items.size(); ++i) {
        if (i > 0) {
            cursor.insertBlock();
        }
        cursor.insertText(items.at(i));
    }
}

QString kindLabel(KoraCutScriptBlock::Kind kind)
{
    switch (kind) {
    case KoraCutScriptBlock::Kind::Heading:
        return i18nc("@item script block", "Heading");
    case KoraCutScriptBlock::Kind::Paragraph:
        return i18nc("@item script block", "Text");
    case KoraCutScriptBlock::Kind::List:
        return i18nc("@item script block", "List");
    case KoraCutScriptBlock::Kind::Table:
        return i18nc("@item script block", "Table");
    case KoraCutScriptBlock::Kind::Divider:
        return i18nc("@item script block", "Divider");
    }
    return QString();
}

QString kindIcon(KoraCutScriptBlock::Kind kind)
{
    switch (kind) {
    case KoraCutScriptBlock::Kind::Heading:
        return QStringLiteral("draw-text");
    case KoraCutScriptBlock::Kind::Paragraph:
        return QStringLiteral("text-plain");
    case KoraCutScriptBlock::Kind::List:
        return QStringLiteral("view-list-details");
    case KoraCutScriptBlock::Kind::Table:
        return QStringLiteral("view-grid");
    case KoraCutScriptBlock::Kind::Divider:
        return QStringLiteral("list-remove");
    }
    return QString();
}

const QList<KoraCutScriptBlock::Kind> kAllKinds = {KoraCutScriptBlock::Kind::Paragraph, KoraCutScriptBlock::Kind::Heading, KoraCutScriptBlock::Kind::List,
                                                   KoraCutScriptBlock::Kind::Table, KoraCutScriptBlock::Kind::Divider};

} // namespace

KoraCutScriptPanel::KoraCutScriptPanel(QWidget *parent)
    : QWidget(parent)
    , m_textTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("koracutScriptPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    // Un panneau en onglet impose sa largeur minimale à tout son groupe : on borne.
    setMinimumWidth(240);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // La barre du panneau : le nom, le type de vidéo, « + » et le menu.
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("koracutScriptBar"));
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup);
    barLayout->setSpacing(KoraCutMetrics::SpaceGroup);
    m_title = new QLabel(i18nc("@title panel name", "Script"), bar);
    m_title->setObjectName(QStringLiteral("koracutScriptPanelTitle"));
    m_typeChip = new QLabel(bar);
    m_typeChip->setObjectName(QStringLiteral("koracutScriptTypeChip"));
    m_typeChip->setTextFormat(Qt::PlainText);
    barLayout->addWidget(m_title);
    barLayout->addWidget(m_typeChip, 0, Qt::AlignVCenter);
    barLayout->addStretch(1);
    m_addButton = new QToolButton(bar);
    m_addButton->setObjectName(QStringLiteral("koracutScriptBarButton"));
    m_addButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_addButton->setIconSize(KoraCutMetrics::iconToolbar());
    m_addButton->setToolTip(i18nc("@info:tooltip", "Add a block at the end: text, heading, list, table, divider"));
    m_addButton->setPopupMode(QToolButton::InstantPopup);
    m_addButton->setCursor(Qt::PointingHandCursor);
    connect(m_addButton, &QToolButton::pressed, this, [this]() {
        QMenu *menu = insertMenu(m_addButton, -1);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(m_addButton->mapToGlobal(QPoint(0, m_addButton->height())));
    });
    m_menuButton = new QToolButton(bar);
    m_menuButton->setObjectName(QStringLiteral("koracutScriptBarButton"));
    m_menuButton->setIcon(QIcon::fromTheme(QStringLiteral("view-more-symbolic")));
    m_menuButton->setIconSize(KoraCutMetrics::iconToolbar());
    m_menuButton->setToolTip(i18nc("@info:tooltip", "Export, import, template"));
    m_menuButton->setPopupMode(QToolButton::InstantPopup);
    m_menuButton->setCursor(Qt::PointingHandCursor);
    auto *menu = new QMenu(m_menuButton);
    menu->addAction(QIcon::fromTheme(QStringLiteral("document-export")), i18nc("@action:inmenu", "Export as Markdown…"), this, &KoraCutScriptPanel::exportMarkdown);
    menu->addAction(QIcon::fromTheme(QStringLiteral("document-export")), i18nc("@action:inmenu", "Export Table as CSV…"), this, &KoraCutScriptPanel::exportCsv);
    menu->addAction(QIcon::fromTheme(QStringLiteral("document-import")), i18nc("@action:inmenu", "Import a CSV into the Table…"), this, &KoraCutScriptPanel::importCsv);
    menu->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18nc("@action:inmenu", "Copy as Markdown"), this, [this]() {
        flushTextCommit();
        QApplication::clipboard()->setText(m_script.toMarkdown());
        pCore->displayMessage(i18nc("@info", "Script copied as Markdown"), InformationMessage, 3000);
    });
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action:inmenu", "Start Again from the Template"), this,
                    &KoraCutScriptPanel::resetToTemplate);
    m_menuButton->setMenu(menu);
    // Chantier 5 : la voix off tirée du script (la ligne ou la sélection, ou tout le script).
    m_voiceButton = new QToolButton(bar);
    m_voiceButton->setObjectName(QStringLiteral("koracutScriptBarButton"));
    m_voiceButton->setIcon(QIcon::fromTheme(QStringLiteral("audio-input-microphone")));
    m_voiceButton->setIconSize(KoraCutMetrics::iconToolbar());
    m_voiceButton->setToolTip(i18nc("@info:tooltip", "Generate the voice-over: the selected rows or text, or the whole script scene by scene"));
    m_voiceButton->setPopupMode(QToolButton::InstantPopup);
    m_voiceButton->setCursor(Qt::PointingHandCursor);
    auto *voiceMenu = new QMenu(m_voiceButton);
    voiceMenu->addAction(i18nc("@action:inmenu", "Voice-over of the Selection…"), this, &KoraCutScriptPanel::voiceOverFromSelection);
    voiceMenu->addAction(i18nc("@action:inmenu", "Voice-over of the Whole Script, Scene by Scene…"), this, &KoraCutScriptPanel::voiceOverWholeScript);
    m_voiceButton->setMenu(voiceMenu);
    barLayout->addWidget(m_voiceButton);
    barLayout->addWidget(m_addButton);
    barLayout->addWidget(m_menuButton);
    root->addWidget(bar);

    // Le nom de la séquence dont on écrit le script : chaque séquence a le sien.
    m_sequenceLabel = new QLabel(this);
    m_sequenceLabel->setObjectName(QStringLiteral("koracutScriptSequence"));
    m_sequenceLabel->setTextFormat(Qt::PlainText);
    m_sequenceLabel->setToolTip(i18nc("@info:tooltip", "Each sequence of the project has its own script. This panel follows the active sequence."));
    m_sequenceLabel->setContentsMargins(KoraCutMetrics::SpaceZone, 0, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup);
    m_sequenceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    root->addWidget(m_sequenceLabel);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("koracutScriptHint"));
    m_hint->setWordWrap(true);
    m_hint->setContentsMargins(KoraCutMetrics::SpaceZone, 0, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup);
    root->addWidget(m_hint);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("koracutScriptScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_canvas = new QWidget(m_scroll);
    m_canvas->setObjectName(QStringLiteral("koracutScriptCanvas"));
    m_blocks = new QVBoxLayout(m_canvas);
    m_blocks->setContentsMargins(KoraCutMetrics::SpaceTight, 0, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpacePanel);
    m_blocks->setSpacing(0);
    m_scroll->setWidget(m_canvas);
    root->addWidget(m_scroll, 1);

    m_textTimer->setSingleShot(true);
    m_textTimer->setInterval(500);
    connect(m_textTimer, &QTimer::timeout, this, &KoraCutScriptPanel::flushTextCommit);

    connect(KoraCutScriptNotifier::instance(), &KoraCutScriptNotifier::changed, this, [this](const QUuid &sequence) {
        if (m_selfUpdate || (!sequence.isNull() && sequence != m_sequence)) {
            // Le geste vient d'ici, ou il touche le script d'une autre séquence que celle à l'écran.
            return;
        }
        // Un annuler, un agent, la porte : on relit. Une frappe en attente est dépassée.
        m_textTimer->stop();
        m_pendingText.clear();
        reload();
    });
    connect(KoraCutScriptNotifier::instance(), &KoraCutScriptNotifier::activeSequenceChanged, this, &KoraCutScriptPanel::followActiveSequence);
    connect(pCore.get(), &Core::GUISetupDone, this, [this]() {
        if (!m_binConnected && pCore->bin()) {
            // Une séquence renommée : le nom affiché suit.
            m_binConnected = true;
            connect(pCore->bin(), &Bin::updateTabName, this, [this](const QUuid &uuid) {
                if (uuid == m_sequence) {
                    refreshHeader();
                }
            });
        }
        if (isVisible()) {
            reload();
        } else {
            m_doc.clear();
        }
    });
}

void KoraCutScriptPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_doc != pCore->currentDoc() || m_sequence != KoraCutScriptStore::activeSequence() || m_blocks->count() == 0) {
        reload();
    }
}

void KoraCutScriptPanel::reload()
{
    m_doc = pCore->currentDoc();
    m_sequence = KoraCutScriptStore::activeSequence(m_doc);
    m_script = KoraCutScriptStore::current(m_doc, &m_fromTemplate, m_sequence);
    rebuild();
    refreshHeader();
}

void KoraCutScriptPanel::followActiveSequence()
{
    if (m_doc == pCore->currentDoc() && m_sequence == KoraCutScriptStore::activeSequence()) {
        return;
    }
    // Ce qui était en cours de frappe appartient à la séquence qu'on quitte.
    flushTextCommit();
    if (isVisible()) {
        reload();
    } else {
        // Relu à la prochaine ouverture du panneau (showEvent).
        m_doc.clear();
        m_sequence = QUuid();
    }
}

void KoraCutScriptPanel::refreshHeader()
{
    const QString typeId = m_doc ? m_doc->getDocumentProperty(QStringLiteral("koracut:videotype")) : QString();
    const KoraCutVideoTypes::VideoType *type = KoraCutVideoTypes::byId(typeId);
    m_typeChip->setText(type ? type->name : i18nc("@info script panel, project without a video type", "Free project"));
    m_typeChip->setToolTip(type ? i18nc("@info:tooltip", "The template follows the video type chosen in New Project") : QString());
    QString sequenceName = KoraCutScriptStore::sequenceName(m_sequence);
    if (sequenceName.size() > 48) {
        sequenceName = sequenceName.left(47) + QChar(0x2026);
    }
    m_sequenceLabel->setText(i18nc("@info script panel, %1 is the name of the sequence whose script is shown", "Sequence: %1", sequenceName));
    m_sequenceLabel->setVisible(!sequenceName.isEmpty());
    if (m_fromTemplate) {
        m_hint->setText(type ? i18nc("@info script panel, %1 is a video type", "Ready-made script for “%1”. Change anything: it is saved in the project at your first edit.",
                                     type->name)
                             : i18nc("@info script panel", "A ready-made script to start from. Change anything: it is saved in the project at your first edit."));
        m_hint->show();
    } else {
        m_hint->hide();
    }
}

// ---------------------------------------------------------------------------------------------
// Enregistrer

void KoraCutScriptPanel::commit(const QString &undoText, const QString &mergeKey)
{
    if (!m_doc || m_doc != pCore->currentDoc()) {
        return;
    }
    m_selfUpdate = true;
    // Dans la séquence à l'écran, même si une autre vient de devenir active.
    KoraCutScriptStore::apply(m_script, undoText, mergeKey, Fun(), Fun(), m_sequence);
    m_selfUpdate = false;
    if (m_fromTemplate) {
        m_fromTemplate = false;
        refreshHeader();
    }
}

void KoraCutScriptPanel::commitStructure(const QString &undoText, const QString &focusBlock)
{
    flushTextCommit();
    commit(undoText);
    m_focusBlock = focusBlock;
    rebuild();
}

void KoraCutScriptPanel::scheduleTextCommit(const QString &blockId)
{
    if (!m_pendingText.isEmpty() && m_pendingText != blockId) {
        flushTextCommit();
    }
    m_pendingText = blockId;
    m_textTimer->start();
}

void KoraCutScriptPanel::flushTextCommit()
{
    m_textTimer->stop();
    if (m_pendingText.isEmpty()) {
        return;
    }
    const QString id = m_pendingText;
    m_pendingText.clear();
    commit(i18nc("@action undo text", "Script: write"), QStringLiteral("texte:%1").arg(id));
}

// ---------------------------------------------------------------------------------------------
// Dessiner

void KoraCutScriptPanel::rebuild()
{
    const int scroll = m_scroll->verticalScrollBar()->value();
    m_canvas->setUpdatesEnabled(false);
    while (QLayoutItem *item = m_blocks->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    QWidget *focusTarget = nullptr;
    for (int i = 0; i < m_script.blocks.size(); ++i) {
        m_blocks->addWidget(gapWidget(i));
        QWidget *w = blockWidget(m_script.blocks.at(i));
        m_blocks->addWidget(w);
        if (!m_focusBlock.isEmpty() && m_script.blocks.at(i).id == m_focusBlock) {
            focusTarget = w->property("koracutEditor").value<QWidget *>();
        }
    }
    m_blocks->addWidget(gapWidget(int(m_script.blocks.size())));
    // Toujours un moyen d'ajouter à la fin, même quand le script est vide.
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:button", "Add a block"), m_canvas);
    add->setObjectName(QStringLiteral("koracutScriptAddBlock"));
    add->setCursor(Qt::PointingHandCursor);
    add->setIconSize(KoraCutMetrics::iconToolbar());
    connect(add, &QPushButton::clicked, this, [this, add]() {
        QMenu *menu = insertMenu(add, -1);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(add->mapToGlobal(QPoint(0, add->height())));
    });
    m_blocks->addWidget(add, 0, Qt::AlignLeft);
    m_blocks->addStretch(1);
    m_canvas->setUpdatesEnabled(true);
    m_focusBlock.clear();
    QTimer::singleShot(0, this, [this, scroll, focus = QPointer<QWidget>(focusTarget)]() {
        m_scroll->verticalScrollBar()->setValue(scroll);
        if (focus) {
            focus->setFocus(Qt::OtherFocusReason);
            m_scroll->ensureWidgetVisible(focus);
        }
    });
}

QWidget *KoraCutScriptPanel::gapWidget(int at)
{
    // Entre deux blocs : un « + » qui n'apparaît qu'au survol, comme dans Notion.
    auto *gap = new HoverFrame(m_canvas);
    gap->setObjectName(QStringLiteral("koracutScriptGap"));
    gap->setFixedHeight(12);
    auto *lay = new QHBoxLayout(gap);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(KoraCutMetrics::SpaceTight);
    auto *plus = new QToolButton(gap);
    plus->setObjectName(QStringLiteral("koracutScriptGapButton"));
    plus->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    plus->setIconSize(QSize(10, 10));
    plus->setFixedSize(18, 12);
    plus->setCursor(Qt::PointingHandCursor);
    plus->setToolTip(i18nc("@info:tooltip", "Insert a block here"));
    connect(plus, &QToolButton::clicked, this, [this, plus, at]() {
        QMenu *menu = insertMenu(plus, at);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(plus->mapToGlobal(QPoint(0, plus->height())));
    });
    auto *line = new QFrame(gap);
    line->setObjectName(QStringLiteral("koracutScriptGapLine"));
    line->setFixedHeight(1);
    lay->addWidget(plus);
    lay->addWidget(line, 1, Qt::AlignVCenter);
    gap->reveal(plus);
    gap->reveal(line);
    return gap;
}

QWidget *KoraCutScriptPanel::blockWidget(const KoraCutScriptBlock &block)
{
    auto *frame = new HoverFrame(m_canvas);
    frame->setObjectName(QStringLiteral("koracutScriptBlock"));
    frame->setProperty("koracutBlockId", block.id);
    auto *lay = new QHBoxLayout(frame);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(KoraCutMetrics::SpaceTight);

    // La poignée du bloc : son menu (insérer, transformer, dupliquer, déplacer, supprimer).
    auto *handle = new QToolButton(frame);
    handle->setObjectName(QStringLiteral("koracutScriptHandle"));
    handle->setIcon(QIcon::fromTheme(QStringLiteral("view-more-symbolic")));
    handle->setIconSize(QSize(12, 12));
    handle->setFixedWidth(18);
    handle->setCursor(Qt::PointingHandCursor);
    handle->setToolTip(i18nc("@info:tooltip", "Block menu: insert, turn into, duplicate, move, delete"));
    const QString id = block.id;
    connect(handle, &QToolButton::clicked, this, [this, handle, id]() { showBlockMenu(id, handle->mapToGlobal(QPoint(handle->width(), 0))); });
    lay->addWidget(handle, 0, Qt::AlignTop);
    frame->reveal(handle);
    frame->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(frame, &QWidget::customContextMenuRequested, this, [this, frame, id](const QPoint &pos) { showBlockMenu(id, frame->mapToGlobal(pos)); });

    QWidget *editor = nullptr;
    switch (block.kind) {
    case KoraCutScriptBlock::Kind::Heading: {
        auto *edit = new QLineEdit(block.text, frame);
        edit->setObjectName(QStringLiteral("koracutScriptH%1").arg(std::clamp(block.level, 1, 3)));
        edit->setFrame(false);
        edit->setPlaceholderText(i18nc("@info:placeholder", "Heading"));
        connect(edit, &QLineEdit::textEdited, this, [this, id](const QString &text) {
            if (KoraCutScriptBlock *b = m_script.block(id)) {
                b->text = text;
                scheduleTextCommit(id);
            }
        });
        connect(edit, &QLineEdit::editingFinished, this, &KoraCutScriptPanel::flushTextCommit);
        // Entrée sous un titre : un paragraphe neuf juste en dessous, prêt à écrire.
        connect(edit, &QLineEdit::returnPressed, this, [this, id]() { insertBlock(m_script.indexOf(id) + 1, KoraCutScriptBlock::Kind::Paragraph); });
        editor = edit;
        break;
    }
    case KoraCutScriptBlock::Kind::Paragraph:
    case KoraCutScriptBlock::Kind::List: {
        const bool isList = block.kind == KoraCutScriptBlock::Kind::List;
        auto *edit = new GrowingTextEdit(frame);
        edit->setObjectName(isList ? QStringLiteral("koracutScriptList") : QStringLiteral("koracutScriptParagraph"));
        edit->setPlaceholderText(isList ? i18nc("@info:placeholder", "List item") : i18nc("@info:placeholder", "Write here…"));
        if (isList) {
            fillList(edit, block.items.isEmpty() ? QStringList{QString()} : block.items);
        } else {
            edit->setPlainText(block.text);
        }
        connect(edit, &QTextEdit::textChanged, this, [this, id, edit, isList]() {
            KoraCutScriptBlock *b = m_script.block(id);
            if (!b) {
                return;
            }
            if (isList) {
                // Une liste vidée perd ses puces : on les remet, la liste reste une liste.
                if (!edit->textCursor().currentList() && edit->document()->blockCount() == 1 && edit->toPlainText().isEmpty()) {
                    QSignalBlocker blocker(edit);
                    fillList(edit, {QString()});
                }
                const QStringList items = listItems(edit->document());
                if (items == b->items) {
                    return;
                }
                b->items = items;
            } else {
                if (b->text == edit->toPlainText()) {
                    return;
                }
                b->text = edit->toPlainText();
            }
            scheduleTextCommit(id);
        });
        edit->onFocusOut = [this]() { flushTextCommit(); };
        editor = edit;
        break;
    }
    case KoraCutScriptBlock::Kind::Table: {
        auto *table = new KoraCutScriptTableBlock(block, frame);
        connect(table->model(), &KoraCutScriptTableModel::edited, this, [this, id, table](const QString &undoText, const QString &mergeKey) {
            flushTextCommit();
            if (KoraCutScriptBlock *b = m_script.block(id)) {
                *b = table->model()->table();
                commit(undoText, mergeKey);
            }
        });
        connect(table, &KoraCutScriptTableBlock::seekRequested, this, &KoraCutScriptPanel::seekRequested);
        connect(table, &KoraCutScriptTableBlock::placeMarkerRequested, this, &KoraCutScriptPanel::placeMarker);
        connect(table, &KoraCutScriptTableBlock::linkZoneRequested, this, &KoraCutScriptPanel::linkZone);
        connect(table, &KoraCutScriptTableBlock::voiceOverRequested, this, &KoraCutScriptPanel::openVoiceOver);
        connect(table, &KoraCutScriptTableBlock::activated, this, [this](const QString &blockId) { m_lastTable = blockId; });
        editor = table->view();
        lay->addWidget(table, 1);
        frame->setProperty("koracutEditor", QVariant::fromValue<QWidget *>(editor));
        return frame;
    }
    case KoraCutScriptBlock::Kind::Divider: {
        auto *line = new QFrame(frame);
        line->setObjectName(QStringLiteral("koracutScriptDivider"));
        line->setFixedHeight(1);
        auto *holder = new QWidget(frame);
        holder->setFixedHeight(17);
        auto *hl = new QVBoxLayout(holder);
        hl->setContentsMargins(0, 8, 0, 8);
        hl->addWidget(line);
        lay->addWidget(holder, 1);
        return frame;
    }
    }
    if (editor) {
        lay->addWidget(editor, 1);
        frame->setProperty("koracutEditor", QVariant::fromValue<QWidget *>(editor));
    }
    return frame;
}

// ---------------------------------------------------------------------------------------------
// Gestes sur les blocs

QMenu *KoraCutScriptPanel::insertMenu(QWidget *parent, int at)
{
    auto *menu = new QMenu(parent);
    for (KoraCutScriptBlock::Kind kind : kAllKinds) {
        menu->addAction(QIcon::fromTheme(kindIcon(kind)), kindLabel(kind), this, [this, at, kind]() { insertBlock(at, kind); });
    }
    return menu;
}

void KoraCutScriptPanel::insertBlock(int at, KoraCutScriptBlock::Kind kind)
{
    flushTextCommit();
    const QString id = m_script.insertBlock(KoraCutScript::newBlock(kind), at);
    commitStructure(i18nc("@action undo text, %1 is a block kind", "Script: add %1", kindLabel(kind).toLower()), id);
}

void KoraCutScriptPanel::showBlockMenu(const QString &blockId, const QPoint &globalPos)
{
    flushTextCommit();
    const int index = m_script.indexOf(blockId);
    if (index < 0) {
        return;
    }
    const KoraCutScriptBlock block = m_script.blocks.at(index);
    QMenu menu(this);
    QMenu *above = menu.addMenu(QIcon::fromTheme(QStringLiteral("go-up")), i18nc("@title:menu", "Insert Above"));
    QMenu *below = menu.addMenu(QIcon::fromTheme(QStringLiteral("go-down")), i18nc("@title:menu", "Insert Below"));
    for (KoraCutScriptBlock::Kind kind : kAllKinds) {
        above->addAction(QIcon::fromTheme(kindIcon(kind)), kindLabel(kind), this, [this, blockId, kind]() { insertBlock(m_script.indexOf(blockId), kind); });
        below->addAction(QIcon::fromTheme(kindIcon(kind)), kindLabel(kind), this, [this, blockId, kind]() { insertBlock(m_script.indexOf(blockId) + 1, kind); });
    }
    // Transformer : entre titre, paragraphe et liste, le texte suit.
    if (block.kind == KoraCutScriptBlock::Kind::Heading || block.kind == KoraCutScriptBlock::Kind::Paragraph || block.kind == KoraCutScriptBlock::Kind::List) {
        QMenu *turn = menu.addMenu(i18nc("@title:menu", "Turn Into"));
        auto turnInto = [this, blockId](KoraCutScriptBlock::Kind kind, int level) {
            KoraCutScriptBlock *b = m_script.block(blockId);
            if (!b) {
                return;
            }
            const QString text = b->kind == KoraCutScriptBlock::Kind::List ? b->items.join(QLatin1Char('\n')) : b->text;
            b->kind = kind;
            b->level = level;
            if (kind == KoraCutScriptBlock::Kind::List) {
                b->items = text.split(QLatin1Char('\n'));
                b->text.clear();
            } else {
                b->text = kind == KoraCutScriptBlock::Kind::Heading ? text.section(QLatin1Char('\n'), 0, 0) : text;
                b->items.clear();
            }
            commitStructure(i18nc("@action undo text", "Script: turn block into another kind"), blockId);
        };
        for (int level = 1; level <= 3; ++level) {
            turn->addAction(i18nc("@item script block, %1 is 1, 2 or 3", "Heading %1", level), this, [turnInto, level]() {
                turnInto(KoraCutScriptBlock::Kind::Heading, level);
            });
        }
        turn->addAction(kindLabel(KoraCutScriptBlock::Kind::Paragraph), this, [turnInto]() { turnInto(KoraCutScriptBlock::Kind::Paragraph, 1); });
        turn->addAction(kindLabel(KoraCutScriptBlock::Kind::List), this, [turnInto]() { turnInto(KoraCutScriptBlock::Kind::List, 1); });
    }
    menu.addSeparator();
    if (block.kind != KoraCutScriptBlock::Kind::Divider) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("audio-input-microphone")), i18nc("@action:inmenu", "Generate the Voice-over…"), this,
                       [this, blockId]() { openVoiceOver(blockId, {}); });
        menu.addSeparator();
    }
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18nc("@action:inmenu", "Duplicate"), this, [this, blockId]() {
        const QString copy = m_script.duplicateBlock(blockId);
        commitStructure(i18nc("@action undo text", "Script: duplicate block"), copy);
    });
    if (index > 0) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("go-up")), i18nc("@action:inmenu", "Move Up"), this, [this, blockId]() {
            m_script.moveBlock(blockId, m_script.indexOf(blockId) - 1);
            commitStructure(i18nc("@action undo text", "Script: move block"), blockId);
        });
    }
    if (index + 1 < m_script.blocks.size()) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("go-down")), i18nc("@action:inmenu", "Move Down"), this, [this, blockId]() {
            m_script.moveBlock(blockId, m_script.indexOf(blockId) + 1);
            commitStructure(i18nc("@action undo text", "Script: move block"), blockId);
        });
    }
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18nc("@action:inmenu", "Delete"), this, [this, blockId]() {
        m_script.removeBlock(blockId);
        commitStructure(i18nc("@action undo text", "Script: delete block"), QString());
    });
    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------------------------
// La timeline

void KoraCutScriptPanel::placeMarker(const QString &blockId, const QString &rowId)
{
    flushTextCommit();
    KdenliveDoc *doc = pCore->currentDoc();
    if (!doc) {
        return;
    }
    // Le script affiché peut être le modèle pas encore enregistré : placeMarker() l'enregistre avec
    // le marqueur et le lien, en une seule étape.
    QString error;
    if (!KoraCutScriptStore::placeMarker(doc->getTimeline(m_sequence, true), blockId, rowId, pCore->getMonitorPosition(), &error)) {
        pCore->displayMessage(error, ErrorMessage, 5000);
    }
}

void KoraCutScriptPanel::linkZone(const QString &blockId, const QString &rowId)
{
    flushTextCommit();
    TimelineWidget *timeline = pCore->window() ? pCore->window()->getCurrentTimeline() : nullptr;
    if (!timeline || !timeline->controller()) {
        return;
    }
    const int in = timeline->controller()->zoneIn();
    const int out = timeline->controller()->zoneOut();
    if (out <= in) {
        pCore->displayMessage(i18nc("@info", "Set a zone in the timeline first (I and O)"), InformationMessage, 5000);
        return;
    }
    KoraCutScriptBlock *table = m_script.block(blockId);
    const int row = table ? table->rowIndex(rowId) : -1;
    if (row < 0) {
        return;
    }
    table->rows[row].zoneIn = in;
    table->rows[row].zoneOut = out;
    commitStructure(i18nc("@action undo text", "Script: link scene to zone"), blockId);
}

// ---------------------------------------------------------------------------------------------
// Export, import, modèle

KoraCutScriptBlock *KoraCutScriptPanel::targetTable()
{
    if (KoraCutScriptBlock *b = m_script.block(m_lastTable); b && b->kind == KoraCutScriptBlock::Kind::Table) {
        return b;
    }
    return m_script.firstTable();
}

QString KoraCutScriptPanel::defaultExportPath(const QString &suffix) const
{
    KdenliveDoc *doc = pCore->currentDoc();
    QString folder = QDir::homePath();
    QString name = i18nc("@info default file name", "Script");
    if (doc && doc->url().isValid() && doc->url().isLocalFile()) {
        const QFileInfo info(doc->url().toLocalFile());
        folder = info.absolutePath();
        name = info.completeBaseName() + QStringLiteral(" - ") + name;
    }
    return QDir(folder).filePath(name + suffix);
}

void KoraCutScriptPanel::exportMarkdown()
{
    flushTextCommit();
    const QString path =
        QFileDialog::getSaveFileName(this, i18nc("@title:window", "Export Script as Markdown"), defaultExportPath(QStringLiteral(".md")), QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        pCore->displayMessage(i18nc("@info", "Cannot write %1", path), ErrorMessage, 5000);
        return;
    }
    file.write(m_script.toMarkdown().toUtf8());
    pCore->displayMessage(i18nc("@info", "Script exported to %1", QFileInfo(path).fileName()), InformationMessage, 4000);
}

void KoraCutScriptPanel::exportCsv()
{
    flushTextCommit();
    KoraCutScriptBlock *table = targetTable();
    if (!table) {
        pCore->displayMessage(i18nc("@info", "The script has no table to export"), InformationMessage, 4000);
        return;
    }
    const QString path =
        QFileDialog::getSaveFileName(this, i18nc("@title:window", "Export Table as CSV"), defaultExportPath(QStringLiteral(".csv")), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        pCore->displayMessage(i18nc("@info", "Cannot write %1", path), ErrorMessage, 5000);
        return;
    }
    // Avec l'indicateur UTF-8 en tête : Excel et Numbers lisent alors les accents correctement.
    file.write("\xEF\xBB\xBF");
    file.write(KoraCutScript::tableToCsv(*table).toUtf8());
    pCore->displayMessage(i18nc("@info", "Table exported to %1", QFileInfo(path).fileName()), InformationMessage, 4000);
}

void KoraCutScriptPanel::importCsv()
{
    flushTextCommit();
    const QString path = QFileDialog::getOpenFileName(this, i18nc("@title:window", "Import a CSV"), defaultExportPath(QString()).section(QLatin1Char('/'), 0, -2),
                                                      QStringLiteral("CSV (*.csv *.tsv *.txt)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        pCore->displayMessage(i18nc("@info", "Cannot read %1", path), ErrorMessage, 5000);
        return;
    }
    const QString csv = QString::fromUtf8(file.readAll());
    KoraCutScriptBlock *table = targetTable();
    QString id;
    if (!table) {
        id = m_script.insertBlock(KoraCutScript::newBlock(KoraCutScriptBlock::Kind::Table));
        table = m_script.block(id);
        table->columns.clear();
        table->text = QFileInfo(path).completeBaseName();
    }
    QString error;
    const int added = KoraCutScript::importCsv(*table, csv, &error);
    if (added == 0 && !error.isEmpty()) {
        pCore->displayMessage(error, ErrorMessage, 5000);
        return;
    }
    commitStructure(i18nc("@action undo text", "Script: import CSV"), table->id);
    pCore->displayMessage(i18ncp("@info", "%1 row imported", "%1 rows imported", added), InformationMessage, 4000);
}

void KoraCutScriptPanel::resetToTemplate()
{
    flushTextCommit();
    if (!noDialogs() && KoraCutScriptStore::isStored(m_doc, m_sequence) &&
        QMessageBox::question(this, i18nc("@title:window", "Start Again from the Template"),
                              i18nc("@info", "Replace the whole script with the template for this video type? You can undo it.")) != QMessageBox::Yes) {
        return;
    }
    KdenliveDoc *doc = pCore->currentDoc();
    m_script = KoraCutScript::templateFor(doc ? doc->getDocumentProperty(QStringLiteral("koracut:videotype")) : QString());
    commitStructure(i18nc("@action undo text", "Script: put back the template"), QString());
}

// ---------------------------------------------------------------------------------------------
// La voix off tirée du script (chantier 5)

namespace {
// La colonne de ce qui se dit : « voix » dans le modèle, sinon une colonne dont le titre parle de voix
// ou de texte, sinon la première colonne de texte qui n'est pas le nom de la scène.
int spokenColumn(const KoraCutScriptBlock &table)
{
    for (int c = 0; c < table.columns.size(); ++c) {
        if (table.columns.at(c).id == QLatin1String("voix")) {
            return c;
        }
    }
    for (int c = 0; c < table.columns.size(); ++c) {
        const QString title = KoraCutVideoTypes::searchable(table.columns.at(c).title);
        if (title.contains(QLatin1String("voix")) || title.contains(QLatin1String("voice")) || title.contains(QLatin1String("dialogue")) ||
            title.contains(QLatin1String("texte")) || title.contains(QLatin1String("text"))) {
            return c;
        }
    }
    for (int c = 0; c < table.columns.size(); ++c) {
        if (table.columns.at(c).type == QLatin1String("texte") && table.columns.at(c).id != QLatin1String("scene")) {
            return c;
        }
    }
    return -1;
}
} // namespace

QList<QPair<QString, QString>> KoraCutScriptPanel::voiceLines(const QString &blockId, const QStringList &rowIds, QList<int> *positions) const
{
    QList<QPair<QString, QString>> lines;
    const KoraCutScriptBlock *block = m_script.block(blockId);
    if (!block) {
        return lines;
    }
    if (block->kind == KoraCutScriptBlock::Kind::Table) {
        const int column = spokenColumn(*block);
        if (column < 0) {
            return lines;
        }
        const QString columnId = block->columns.at(column).id;
        const int sceneColumn = block->columnIndex(QStringLiteral("scene"));
        for (int r = 0; r < block->rows.size(); ++r) {
            const KoraCutScriptRow &row = block->rows.at(r);
            if (!rowIds.isEmpty() && !rowIds.contains(row.id)) {
                continue;
            }
            const QString text = row.cells.value(columnId).simplified();
            if (text.isEmpty()) {
                continue;
            }
            const QString scene = sceneColumn >= 0 ? row.cells.value(block->columns.at(sceneColumn).id).simplified() : QString();
            const QString label = scene.isEmpty() ? i18nc("@item voice-over line, %1 row number", "Scene %1", r + 1)
                                                  : i18nc("@item voice-over line, %1 row number, %2 scene name", "%1. %2", r + 1, scene.left(24));
            lines.append({label, text});
            if (positions) {
                positions->append(row.linkedFrame());
            }
        }
        return lines;
    }
    QString text = block->kind == KoraCutScriptBlock::Kind::List ? block->items.join(QLatin1Char(' ')) : block->text;
    text = text.simplified();
    if (!text.isEmpty()) {
        lines.append({i18nc("@item voice-over line from a text block", "Text"), text});
        if (positions) {
            positions->append(-1);
        }
    }
    return lines;
}

void KoraCutScriptPanel::openVoiceOver(const QString &blockId, const QStringList &rowIds)
{
    flushTextCommit();
    QList<KoraCutVoiceOverDialog::Item> items;
    auto collect = [this, &items](const QString &id, const QStringList &rows) {
        QList<int> positions;
        const auto lines = voiceLines(id, rows, &positions);
        for (int i = 0; i < lines.size(); ++i) {
            items.append(KoraCutVoiceOverDialog::Item{lines.at(i).first, lines.at(i).second, positions.value(i, -1)});
        }
    };
    if (blockId.isEmpty()) {
        // Tout le script, scène par scène : les lignes de chaque tableau.
        for (const KoraCutScriptBlock &b : std::as_const(m_script.blocks)) {
            if (b.kind == KoraCutScriptBlock::Kind::Table) {
                collect(b.id, {});
            }
        }
    } else {
        collect(blockId, rowIds);
    }
    if (items.isEmpty()) {
        pCore->displayMessage(i18nc("@info", "Nothing to say here: write the text in the “Voice or text” column first."), InformationMessage, 4000);
        return;
    }
    auto *dialog = new KoraCutVoiceOverDialog(items, pCore->window() ? static_cast<QWidget *>(pCore->window()) : this);
    dialog->show();
}

void KoraCutScriptPanel::voiceOverFromSelection()
{
    // Une ligne de tableau choisie, un texte sélectionné, ou le bloc où l'on écrit.
    QWidget *focus = QApplication::focusWidget();
    for (auto *table : m_canvas->findChildren<KoraCutScriptTableBlock *>()) {
        const QStringList rows = table->selectedRowIds();
        if (!rows.isEmpty() && (table->isAncestorOf(focus) || table->blockId() == m_lastTable)) {
            openVoiceOver(table->blockId(), rows);
            return;
        }
    }
    QString selected;
    if (auto *edit = qobject_cast<QTextEdit *>(focus)) {
        selected = edit->textCursor().selectedText();
    } else if (auto *line = qobject_cast<QLineEdit *>(focus)) {
        selected = line->selectedText();
    }
    selected.replace(QChar::ParagraphSeparator, QLatin1Char(' '));
    if (!selected.simplified().isEmpty()) {
        auto *dialog = new KoraCutVoiceOverDialog({KoraCutVoiceOverDialog::Item{i18nc("@item voice-over line from selected text", "Selection"), selected.simplified(), -1}},
                                                  pCore->window() ? static_cast<QWidget *>(pCore->window()) : this);
        dialog->show();
        return;
    }
    for (QWidget *w = focus; w && w != m_canvas; w = w->parentWidget()) {
        const QString id = w->property("koracutBlockId").toString();
        if (!id.isEmpty()) {
            openVoiceOver(id, {});
            return;
        }
    }
    voiceOverWholeScript();
}

void KoraCutScriptPanel::voiceOverWholeScript()
{
    openVoiceOver(QString(), {});
}

// ---------------------------------------------------------------------------------------------
// Pseudo-actions de capture

QString KoraCutScriptPanel::debugCommand(const QString &command)
{
    flushTextCommit();
    const QString verb = command.section(QLatin1Char('='), 0, 0);
    const QString rest = command.section(QLatin1Char('='), 1);
    if (m_doc != pCore->currentDoc() || m_sequence != KoraCutScriptStore::activeSequence()) {
        reload();
    }
    if (verb == QLatin1String("modele")) {
        // Pose le modèle pour de bon (une étape d'annulation), comme un premier geste.
        KdenliveDoc *doc = pCore->currentDoc();
        m_script = KoraCutScript::templateFor(doc ? doc->getDocumentProperty(QStringLiteral("koracut:videotype")) : QString());
        commitStructure(i18nc("@action undo text", "Script: put back the template"), QString());
        const KoraCutScriptBlock *table = m_script.firstTable();
        return QStringLiteral("modele %1 : %2 blocs, %3 scènes").arg(m_script.typeId).arg(m_script.blocks.size()).arg(table ? table->rows.size() : 0);
    }
    if (verb == QLatin1String("ligne") || verb == QLatin1String("colonne")) {
        KoraCutScriptBlock *table = targetTable();
        if (!table) {
            return QStringLiteral("pas de tableau");
        }
        const QString id = table->id;
        auto *widget = m_canvas->findChild<KoraCutScriptTableBlock *>();
        for (auto *candidate : m_canvas->findChildren<KoraCutScriptTableBlock *>()) {
            if (candidate->blockId() == id) {
                widget = candidate;
            }
        }
        if (!widget) {
            return QStringLiteral("tableau introuvable à l'écran");
        }
        if (verb == QLatin1String("ligne")) {
            const int r = widget->model()->addRow();
            if (!rest.isEmpty()) {
                // « script:ligne=Texte de la scène » remplit la première colonne.
                widget->model()->setData(widget->model()->index(r, 0), rest, Qt::EditRole);
            }
            return QStringLiteral("ligne %1 ajoutée").arg(r + 1);
        }
        // « colonne=Texte », « colonne=Case à cocher », ou l'identifiant du type (texte, duree, media, case, choix).
        QString type = QStringLiteral("texte");
        const QString wanted = KoraCutVideoTypes::searchable(rest);
        for (const QString &t : KoraCutScript::columnTypes()) {
            if (t == wanted || KoraCutVideoTypes::searchable(KoraCutScript::columnTypeLabel(t)).startsWith(wanted)) {
                type = t;
                break;
            }
        }
        const int c = widget->addColumnOfType(type, false);
        return QStringLiteral("colonne %1 (%2) ajoutée").arg(c + 1).arg(type);
    }
    if (verb == QLatin1String("bloc")) {
        KoraCutScriptBlock::Kind kind;
        if (!KoraCutScript::kindFromName(rest, kind)) {
            return QStringLiteral("type de bloc inconnu : %1").arg(rest);
        }
        insertBlock(-1, kind);
        return QStringLiteral("bloc %1 ajouté").arg(rest);
    }
    if (verb == QLatin1String("markdown") || verb == QLatin1String("csv")) {
        QFile file(rest);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QStringLiteral("écriture impossible : %1").arg(rest);
        }
        KoraCutScriptBlock *table = targetTable();
        file.write(verb == QLatin1String("markdown") ? m_script.toMarkdown().toUtf8() : (table ? KoraCutScript::tableToCsv(*table).toUtf8() : QByteArray()));
        return QStringLiteral("%1 écrit : %2").arg(verb, rest);
    }
    if (verb == QLatin1String("voir")) {
        // « script:voir=deroule » : fait défiler la page jusqu'à ce bloc (captures).
        for (QWidget *w : m_canvas->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (w->property("koracutBlockId").toString() == rest) {
                QTimer::singleShot(100, this, [this, target = QPointer<QWidget>(w)]() {
                    if (target) {
                        m_scroll->verticalScrollBar()->setValue(target->y());
                    }
                });
                return QStringLiteral("bloc %1 en vue").arg(rest);
            }
        }
        return QStringLiteral("bloc introuvable : %1").arg(rest);
    }
    if (verb == QLatin1String("voix")) {
        // « script:voix » ouvre la fenêtre Voix off pour tout le script ; « script:voix=<bloc_id> » pour un bloc ;
        // « script:voix-generer », « script:voix-poser », « script:voix-etat » la pilotent.
        openVoiceOver(rest, {});
        return QStringLiteral("voix off ouverte");
    }
    if (verb.startsWith(QLatin1String("voix-"))) {
        const auto dialogs = pCore->window() ? pCore->window()->findChildren<KoraCutVoiceOverDialog *>() : QList<KoraCutVoiceOverDialog *>();
        if (dialogs.isEmpty()) {
            return QStringLiteral("aucune fenêtre Voix off");
        }
        KoraCutVoiceOverDialog *dialog = dialogs.last();
        if (verb == QLatin1String("voix-generer")) {
            dialog->startGeneration();
        } else if (verb == QLatin1String("voix-poser")) {
            dialog->placeReady(true);
        }
        return dialog->debugState();
    }
    if (verb == QLatin1String("etat")) {
        // La séquence d'abord : chaque séquence a son script.
        const int rank = int(KoraCutScriptStore::sequences(m_doc).indexOf(m_sequence)) + 1;
        return QStringLiteral("sequence=%1 rang=%2 uuid=%3 %4 %5")
            .arg(QStringLiteral("\"%1\"").arg(KoraCutScriptStore::sequenceName(m_sequence)))
            .arg(rank)
            .arg(m_sequence.toString(QUuid::WithoutBraces), m_fromTemplate ? QStringLiteral("modele") : QStringLiteral("enregistre"), m_script.toCompactString());
    }
    return QStringLiteral("commande inconnue : %1").arg(command);
}
