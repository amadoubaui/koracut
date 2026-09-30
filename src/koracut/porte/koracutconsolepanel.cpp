/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutconsolepanel.h"
#include "../koracutfonts.h"
#include "koracut/koracutmetrics.h"
#include "koracutterminal.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QColor>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaMethod>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr int kMaxLines = 500;
constexpr int PositionRole = Qt::UserRole + 1;

QString compactParams(const QJsonObject &params, int max = 110)
{
    if (params.isEmpty()) {
        return QString();
    }
    QJsonObject shown = params;
    shown.remove(QStringLiteral("confirmation"));
    QString text = QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact));
    if (text.size() > max) {
        text = text.left(max - 1) + QChar(0x2026);
    }
    return text;
}

// Le champ garde l'historique des commandes (flèches haut et bas).
class HistoryLineEdit : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;
    std::function<void(int)> onHistory;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Up && onHistory) {
            onHistory(-1);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Down && onHistory) {
            onHistory(1);
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

} // namespace

KoraCutConsolePanel::KoraCutConsolePanel(KoraCutPorte *porte, QWidget *parent)
    : QWidget(parent)
    , m_porte(porte)
{
    setObjectName(QStringLiteral("koracutConsolePanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(240);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone);
    root->setSpacing(KoraCutMetrics::SpaceGroup);

    // L'état de la porte, et le terminal.
    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(KoraCutMetrics::SpaceGroup);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("koracutConsoleStatus"));
    m_status->setWordWrap(true);
    header->addWidget(m_status, 1);
    m_terminal = new QPushButton(i18nc("@action:button open the system terminal in the project folder", "Open a terminal here"), this);
    m_terminal->setObjectName(QStringLiteral("koracutConsolePrimaryButton"));
    m_terminal->setCursor(Qt::PointingHandCursor);
    m_terminal->setToolTip(i18nc("@info:tooltip", "The terminal inside KoraCut, in the project folder, with the agent you chose (Ctrl+`)"));
    connect(m_terminal, &QPushButton::clicked, this, [this]() { openTerminal(); });
    header->addWidget(m_terminal);
    m_systemTerminal = new QPushButton(this);
    m_systemTerminal->setObjectName(QStringLiteral("koracutConsoleButton"));
    m_systemTerminal->setIcon(QIcon::fromTheme(QStringLiteral("utilities-terminal")));
    m_systemTerminal->setCursor(Qt::PointingHandCursor);
    m_systemTerminal->setToolTip(i18nc("@info:tooltip", "In the system terminal instead"));
    m_systemTerminal->setAccessibleName(i18nc("@action:button", "In the System Terminal"));
    connect(m_systemTerminal, &QPushButton::clicked, this, [this]() { openSystemTerminal(); });
    header->addWidget(m_systemTerminal);
    root->addLayout(header);

    // Les demandes d'accord, en ligne, au-dessus du journal.
    m_confirmations = new QWidget(this);
    m_confirmations->setObjectName(QStringLiteral("koracutConsoleConfirmations"));
    m_confirmationsLayout = new QVBoxLayout(m_confirmations);
    m_confirmationsLayout->setContentsMargins(0, 0, KoraCutMetrics::SpaceTight, 0);
    m_confirmationsLayout->setSpacing(KoraCutMetrics::SpaceGroup);
    // Plusieurs demandes en attente ne doivent pas chasser le journal : la zone défile, bornée.
    m_confirmationsScroll = new QScrollArea(this);
    m_confirmationsScroll->setObjectName(QStringLiteral("koracutConsoleConfirmationsScroll"));
    m_confirmationsScroll->setWidgetResizable(true);
    m_confirmationsScroll->setFrameShape(QFrame::NoFrame);
    m_confirmationsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_confirmationsScroll->setMaximumHeight(300);
    m_confirmationsScroll->setWidget(m_confirmations);
    m_confirmationsScroll->hide();
    root->addWidget(m_confirmationsScroll);

    // Le journal.
    m_journal = new QTreeWidget(this);
    m_journal->setObjectName(QStringLiteral("koracutConsoleJournal"));
    m_journal->setRootIsDecorated(false);
    m_journal->setAlternatingRowColors(false);
    m_journal->setUniformRowHeights(true);
    m_journal->setSelectionMode(QAbstractItemView::SingleSelection);
    m_journal->setHeaderLabels({i18nc("@title:column", "Time"), i18nc("@title:column who sent the command", "Client"), i18nc("@title:column", "Command"),
                                i18nc("@title:column", "Result"), i18nc("@title:column duration in milliseconds", "ms")});
    m_journal->header()->setStretchLastSection(false);
    m_journal->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_journal->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_journal->setColumnWidth(0, 62);
    m_journal->setColumnWidth(1, 90);
    m_journal->setColumnWidth(3, 120);
    m_journal->setColumnWidth(4, 44);
    m_journal->setFont(KoraCutFonts::code(KoraCutMetrics::FontMono));
    m_journal->setToolTip(i18nc("@info:tooltip", "Everything the command port receives. Click a line to move the playhead to its time."));
    connect(m_journal, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        const int position = item->data(0, PositionRole).toInt();
        if (item->data(0, PositionRole).isValid() && position >= 0) {
            Q_EMIT seekRequested(position);
        }
    });
    root->addWidget(m_journal, 1);

    // Le champ du bas.
    auto *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(KoraCutMetrics::SpaceGroup);
    auto *input = new HistoryLineEdit(this);
    input->setObjectName(QStringLiteral("koracutConsoleInput"));
    input->setPlaceholderText(i18nc("@info:placeholder", "timeline etat niveau=resume"));
    input->setClearButtonEnabled(true);
    input->setToolTip(i18nc("@info:tooltip", "A port command: family method key=value, or JSON. Up and Down browse the history."));
    input->onHistory = [this](int delta) {
        if (m_history.isEmpty()) {
            return;
        }
        m_historyIndex = qBound(0, m_historyIndex + delta, m_history.size());
        m_input->setText(m_historyIndex < m_history.size() ? m_history.at(m_historyIndex) : QString());
    };
    m_input = input;
    connect(m_input, &QLineEdit::returnPressed, this, &KoraCutConsolePanel::submit);
    inputRow->addWidget(m_input, 1);
    m_send = new QPushButton(i18nc("@action:button send the typed command to the port", "Send"), this);
    m_send->setObjectName(QStringLiteral("koracutConsoleButton"));
    m_send->setCursor(Qt::PointingHandCursor);
    connect(m_send, &QPushButton::clicked, this, &KoraCutConsolePanel::submit);
    inputRow->addWidget(m_send);
    root->addLayout(inputRow);

    connect(m_porte, &KoraCutPorte::journal, this, &KoraCutConsolePanel::appendJournal);
    connect(m_porte, &KoraCutPorte::confirmationRequested, this, &KoraCutConsolePanel::addConfirmation);
    connect(m_porte, &KoraCutPorte::confirmationAnswered, this, [this](const QString &id, bool) { removeConfirmation(id); });
    connect(m_porte, &KoraCutPorte::listeningChanged, this, &KoraCutConsolePanel::refreshStatus);
    connect(m_porte, &KoraCutPorte::clientOpened, this, [this](const QString &client) {
        refreshStatus();
        KoraCutPorte::JournalEntry e;
        e.time = QDateTime::currentDateTime();
        e.client = client;
        e.method = QStringLiteral("porte.ouvrir");
        e.summary = i18nc("@info journal line, a program connected to the port", "connected");
        appendJournal(e);
    });
    connect(m_porte, &KoraCutPorte::clientClosed, this, [this](const QString &) { refreshStatus(); });
    refreshStatus();
}

void KoraCutConsolePanel::refreshStatus()
{
    QString text;
    if (m_porte->isListening()) {
        text = i18ncp("@info state of the command port; %2 is the socket name", "Port open (%2), %1 client", "Port open (%2), %1 clients", m_porte->clientCount(),
                      QStringLiteral("koracut-%1").arg(m_porte->reglage()));
    } else if (!m_porte->isEnabled()) {
        text = i18nc("@info", "Port closed: allow external programs in Settings to open it.");
    } else {
        text = i18nc("@info", "Port not open.");
    }
    m_status->setText(text);
    m_terminal->setEnabled(m_porte->isListening());
    m_systemTerminal->setEnabled(m_porte->isListening());
}

void KoraCutConsolePanel::appendJournal(const KoraCutPorte::JournalEntry &entry)
{
    QString command = entry.method;
    const QString params = compactParams(entry.params);
    if (!params.isEmpty()) {
        command += QLatin1Char(' ') + params;
    }
    QString result = entry.ok ? (entry.summary.isEmpty() ? i18nc("@info journal line, the command succeeded", "ok") : entry.summary) : entry.code;
    if (!entry.ok && !entry.summary.isEmpty()) {
        result += QStringLiteral(" · ") + entry.summary;
    }
    auto *item = new QTreeWidgetItem({entry.time.toString(QStringLiteral("HH:mm:ss")), entry.client, command, result, entry.ms > 0 ? QString::number(entry.ms) : QString()});
    item->setData(0, PositionRole, entry.position);
    item->setToolTip(2, QString::fromUtf8(QJsonDocument(entry.params).toJson(QJsonDocument::Indented)));
    item->setToolTip(3, entry.ok ? entry.summary : entry.summary.isEmpty() ? entry.code : entry.code + QStringLiteral(" : ") + entry.summary);
    if (!entry.ok) {
        item->setForeground(3, QColor(0xFF, 0x72, 0x62));
    }
    if (entry.position >= 0) {
        item->setForeground(0, QColor(0x0D, 0x99, 0xFF));
    }
    m_journal->addTopLevelItem(item);
    while (m_journal->topLevelItemCount() > kMaxLines) {
        delete m_journal->takeTopLevelItem(0);
    }
    m_journal->scrollToBottom();
}

int KoraCutConsolePanel::journalCount() const
{
    return m_journal->topLevelItemCount();
}

void KoraCutConsolePanel::addConfirmation(const QString &demandeId, const QString &client, const QString &method, const QJsonObject &params, const QString &what)
{
    auto *card = new QWidget(m_confirmations);
    card->setObjectName(QStringLiteral("koracutConsoleCard"));
    card->setProperty("demandeId", demandeId);
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup);
    layout->setSpacing(KoraCutMetrics::SpaceTight);
    auto *title = new QLabel(i18nc("@info a program asks permission: %1 program name, %2 what it wants to do", "%1 asks to: %2", client, what), card);
    title->setObjectName(QStringLiteral("koracutConsoleCardTitle"));
    title->setWordWrap(true);
    layout->addWidget(title);
    auto *detail = new QLabel(method + QLatin1Char(' ') + compactParams(params, 160), card);
    detail->setObjectName(QStringLiteral("koracutConsoleCardBody"));
    detail->setWordWrap(true);
    detail->setFont(KoraCutFonts::code(KoraCutMetrics::FontMono));
    layout->addWidget(detail);
    auto *trust = new QCheckBox(i18nc("@option:check", "Trust this program for the session"), card);
    trust->setObjectName(QStringLiteral("koracutConsoleTrust"));
    trust->setToolTip(i18nc("@info:tooltip", "Its next requests go through without asking, except quitting and overwriting a file"));
    layout->addWidget(trust);
    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(KoraCutMetrics::SpaceGroup);
    buttons->addStretch(1);
    auto *refuse = new QPushButton(i18nc("@action:button", "Refuse"), card);
    refuse->setObjectName(QStringLiteral("koracutConsoleButton"));
    refuse->setCursor(Qt::PointingHandCursor);
    auto *accept = new QPushButton(i18nc("@action:button", "Accept"), card);
    accept->setObjectName(QStringLiteral("koracutConsolePrimaryButton"));
    accept->setCursor(Qt::PointingHandCursor);
    accept->setDefault(true);
    buttons->addWidget(refuse);
    buttons->addWidget(accept);
    layout->addLayout(buttons);
    connect(accept, &QPushButton::clicked, this, [this, demandeId, trust]() { m_porte->answerConfirmation(demandeId, true, trust->isChecked()); });
    connect(refuse, &QPushButton::clicked, this, [this, demandeId]() { m_porte->answerConfirmation(demandeId, false, false); });
    m_confirmationsLayout->addWidget(card);
    m_confirmationsScroll->show();
}

void KoraCutConsolePanel::removeConfirmation(const QString &demandeId)
{
    const QList<QWidget *> cards = m_confirmations->findChildren<QWidget *>(QStringLiteral("koracutConsoleCard"), Qt::FindDirectChildrenOnly);
    for (QWidget *card : cards) {
        if (card->property("demandeId").toString() == demandeId) {
            m_confirmationsLayout->removeWidget(card);
            card->deleteLater();
        }
    }
    if (cards.size() <= 1) {
        m_confirmationsScroll->hide();
    }
}

void KoraCutConsolePanel::answerPendingConfirmation(bool accepted, bool trust)
{
    const QStringList pending = m_porte->pendingConfirmations();
    if (!pending.isEmpty()) {
        QStringList sorted = pending;
        std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) { return a.mid(1).toInt() < b.mid(1).toInt(); });
        m_porte->answerConfirmation(sorted.first(), accepted, trust);
    }
}

void KoraCutConsolePanel::focusInput()
{
    m_input->setFocus(Qt::OtherFocusReason);
}

void KoraCutConsolePanel::submit()
{
    const QString line = m_input->text().trimmed();
    if (line.isEmpty()) {
        return;
    }
    m_history << line;
    m_historyIndex = m_history.size();
    m_input->clear();
    runLine(line);
}

void KoraCutConsolePanel::runLine(const QString &line)
{
    QString method;
    QJsonObject params;
    QString error;
    if (!KoraCutPorte::parseCommandLine(line, method, params, error)) {
        KoraCutPorte::JournalEntry e;
        e.time = QDateTime::currentDateTime();
        e.client = i18nc("@item the person typing in the console", "you");
        e.method = line;
        e.ok = false;
        e.code = QStringLiteral("parametres_invalides");
        e.summary = error;
        appendJournal(e);
        return;
    }
    // La réponse complète passe dans l'infobulle de la ligne du journal (logCall l'y met en résumé).
    m_porte->callAsync(i18nc("@item the person typing in the console", "you"), method, params, [this](const KoraCutPorte::Reply &reply) {
        if (m_journal->topLevelItemCount() > 0) {
            QTreeWidgetItem *last = m_journal->topLevelItem(m_journal->topLevelItemCount() - 1);
            const QJsonDocument doc = reply.ok() ? (reply.result.isObject() ? QJsonDocument(reply.result.toObject()) : QJsonDocument(reply.result.toArray()))
                                                 : QJsonDocument(reply.error);
            last->setToolTip(3, QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).left(4000));
        }
    });
}

bool KoraCutConsolePanel::openTerminal()
{
    // Le terminal intégré d'abord (panneau Terminal) : il écrit les instructions de l'agent choisi.
    if (isSignalConnected(QMetaMethod::fromSignal(&KoraCutConsolePanel::integratedTerminalRequested))) {
        Q_EMIT integratedTerminalRequested();
        return true;
    }
    return openSystemTerminal();
}

bool KoraCutConsolePanel::openSystemTerminal()
{
    if (isSignalConnected(QMetaMethod::fromSignal(&KoraCutConsolePanel::systemTerminalRequested))) {
        Q_EMIT systemTerminalRequested();
        return true;
    }
    const KoraCutPorte::Reply info = m_porte->call(i18nc("@item the person typing in the console", "you"), QStringLiteral("projet.info"), {});
    const QJsonObject project = info.ok() ? info.result.toObject() : QJsonObject();
    const QString folder = project.value(QStringLiteral("dossier")).toString();
    if (folder.isEmpty()) {
        m_status->setText(i18nc("@info", "Save the project first: the terminal opens in its folder."));
        return false;
    }
    if (!m_porte->isListening()) {
        refreshStatus();
        return false;
    }
    const QString notes = KoraCutTerminal::writeProjectNotes(folder, project);
    QString error;
    const QString command = KoraCutTerminal::defaultCommand();
    if (!KoraCutTerminal::open(folder, m_porte->porteFilePath(), KoraCutTerminal::ctlDirectory(), command, error)) {
        m_status->setText(i18nc("@info", "No terminal could be opened (%1).", error));
        return false;
    }
    KoraCutPorte::JournalEntry e;
    e.time = QDateTime::currentDateTime();
    e.client = i18nc("@item the person typing in the console", "you");
    e.method = QStringLiteral("terminal");
    e.summary = command.isEmpty() ? i18nc("@info journal line", "opened in %1", folder) : i18nc("@info journal line: %1 command, %2 folder", "%1 in %2", command, folder);
    if (!notes.isEmpty()) {
        e.summary += QStringLiteral(" · ") + QFileInfo(notes).fileName();
    }
    appendJournal(e);
    return true;
}
