/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutaiconnectionspage.h"
#include "koracutaisecrets.h"

#include "core.h"
#include "dialogs/settings/kdenlivesettingsdialog.h"
#include "koracut/koracutmetrics.h"
#include "mainwindow.h"

#include <KConfigDialog>
#include <KLocalizedString>

#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int ProviderRole = Qt::UserRole + 1;
constexpr int ConnectionRole = Qt::UserRole + 2;
constexpr int SearchRole = Qt::UserRole + 3;

QString folded(const QString &text)
{
    // Recherche sans accents ni majuscules.
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out += c.toLower();
        }
    }
    return out;
}

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}

} // namespace

KoraCutAiConnectionsPage::KoraCutAiConnectionsPage(QWidget *parent)
    : QWidget(parent)
    , m_verifier(new KoraCutAiVerifier(this))
{
    setObjectName(QStringLiteral("koracutAiConnectionsPage"));
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(KoraCutMetrics::SpacePanel);

    // ---- À gauche : mes connexions, puis le catalogue.
    auto *left = new QVBoxLayout();
    left->setSpacing(KoraCutMetrics::SpaceTight);
    left->addWidget(sectionLabel(i18nc("@title list of the person's AI connections", "My connections"), this));
    auto *orderHint = new QLabel(i18nc("@info", "The first one is used first. Drag to change the order."), this);
    orderHint->setWordWrap(true);
    orderHint->setObjectName(QStringLiteral("koracutAiHint"));
    left->addWidget(orderHint);
    m_connections = new QListWidget(this);
    m_connections->setObjectName(QStringLiteral("koracutAiConnections"));
    m_connections->setDragDropMode(QAbstractItemView::InternalMove);
    m_connections->setDefaultDropAction(Qt::MoveAction);
    m_connections->setIconSize(QSize(20, 20));
    m_connections->setMaximumHeight(150);
    m_connections->setSpacing(1);
    left->addWidget(m_connections);
    left->addSpacing(KoraCutMetrics::SpaceGroup);
    left->addWidget(sectionLabel(i18nc("@title list of AI providers", "Add a provider"), this));
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(i18nc("@info:placeholder", "Search a provider"));
    m_search->setClearButtonEnabled(true);
    left->addWidget(m_search);
    m_catalog = new QListWidget(this);
    m_catalog->setObjectName(QStringLiteral("koracutAiCatalog"));
    m_catalog->setIconSize(QSize(20, 20));
    m_catalog->setUniformItemSizes(false);
    left->addWidget(m_catalog, 1);
    auto *leftBox = new QWidget(this);
    leftBox->setLayout(left);
    leftBox->setFixedWidth(250);
    root->addWidget(leftBox);

    // ---- À droite : le fournisseur choisi.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *right = new QWidget(scroll);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, KoraCutMetrics::SpaceGroup, 0);
    rightLayout->setSpacing(KoraCutMetrics::SpaceZone);

    m_empty = new QLabel(i18nc("@info", "Choose a provider on the left. Several connections can be active: when the first one runs out of credit or quota, "
                                        "the agents carry on with the next one."),
                         right);
    static_cast<QLabel *>(m_empty)->setWordWrap(true);
    rightLayout->addWidget(m_empty);

    m_detail = new QWidget(right);
    auto *detail = new QVBoxLayout(m_detail);
    detail->setContentsMargins(0, 0, 0, 0);
    detail->setSpacing(KoraCutMetrics::SpaceZone);

    auto *header = new QHBoxLayout();
    header->setSpacing(KoraCutMetrics::SpaceGroup);
    m_logo = new QLabel(m_detail);
    m_logo->setFixedSize(32, 32);
    header->addWidget(m_logo);
    m_title = new QLabel(m_detail);
    QFont titleFont = m_title->font();
    titleFont.setPixelSize(16);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    header->addWidget(m_title);
    m_badge = new QLabel(i18nc("@label badge on an AI provider", "Free to start"), m_detail);
    m_badge->setObjectName(QStringLiteral("koracutAiBadge"));
    header->addWidget(m_badge);
    header->addStretch(1);
    detail->addLayout(header);

    m_description = new QLabel(m_detail);
    m_description->setWordWrap(true);
    detail->addWidget(m_description);

    auto *links = new QHBoxLayout();
    links->setSpacing(KoraCutMetrics::SpaceGroup);
    m_doc = new QPushButton(i18nc("@action:button open the provider documentation on API keys", "Documentation"), m_detail);
    m_signup = new QPushButton(i18nc("@action:button open the provider sign-up page", "Create an account"), m_detail);
    m_getKey = new QPushButton(i18nc("@action:button open the provider page where API keys are created", "Get a key"), m_detail);
    for (QPushButton *b : {m_doc, m_signup, m_getKey}) {
        b->setCursor(Qt::PointingHandCursor);
        links->addWidget(b);
    }
    links->addStretch(1);
    detail->addLayout(links);
    connect(m_doc, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl(m_provider.docUrl)); });
    connect(m_signup, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl(m_provider.signupUrl)); });
    connect(m_getKey, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl(m_provider.keyUrl)); });

    auto *form = new QFormLayout();
    form->setHorizontalSpacing(KoraCutMetrics::SpaceZone);
    form->setVerticalSpacing(KoraCutMetrics::SpaceGroup);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    m_account = new QLineEdit(m_detail);
    m_account->setPlaceholderText(i18nc("@info:placeholder name of the account owner", "Who owns this account (e.g. Amadou, studio)"));
    auto *accountLabel = new QLabel(i18nc("@label:textbox", "Account name"), m_detail);
    form->addRow(accountLabel, m_account);
    connect(m_account, &QLineEdit::editingFinished, this, [this]() {
        KoraCutAiConnection c = KoraCutAiConnections::instance()->connection(m_connectionId);
        if (c.isValid() && c.account != m_account->text().trimmed()) {
            c.account = m_account->text().trimmed();
            KoraCutAiConnections::instance()->update(c);
        }
    });

    m_key = new QLineEdit(m_detail);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setClearButtonEnabled(true);
    m_showKey = m_key->addAction(QIcon::fromTheme(QStringLiteral("view-visible")), QLineEdit::TrailingPosition);
    m_showKey->setCheckable(true);
    m_showKey->setToolTip(i18nc("@info:tooltip", "Show or hide the key"));
    connect(m_showKey, &QAction::toggled, this, [this](bool shown) { m_key->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password); });
    m_keyLabel = new QLabel(i18nc("@label:textbox", "API key"), m_detail);
    form->addRow(m_keyLabel, m_key);
    m_keyHint = new QLabel(m_detail);
    m_keyHint->setWordWrap(true);
    m_keyHint->setObjectName(QStringLiteral("koracutAiHint"));
    form->addRow(QString(), m_keyHint);

    m_baseUrl = new QLineEdit(m_detail);
    m_baseLabel = new QLabel(i18nc("@label:textbox", "Base URL"), m_detail);
    form->addRow(m_baseLabel, m_baseUrl);
    m_baseHint = new QLabel(m_detail);
    m_baseHint->setWordWrap(true);
    m_baseHint->setObjectName(QStringLiteral("koracutAiHint"));
    form->addRow(QString(), m_baseHint);
    detail->addLayout(form);

    auto *verifyRow = new QHBoxLayout();
    m_verify = new QPushButton(i18nc("@action:button check the API key with the provider and save it", "Verify and save"), m_detail);
    m_verify->setObjectName(QStringLiteral("koracutAgentPrimaryButton"));
    m_verify->setCursor(Qt::PointingHandCursor);
    m_verify->setDefault(false);
    m_verify->setAutoDefault(false);
    verifyRow->addWidget(m_verify);
    verifyRow->addStretch(1);
    detail->addLayout(verifyRow);
    m_status = new QLabel(m_detail);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail->addWidget(m_status);
    connect(m_verify, &QPushButton::clicked, this, &KoraCutAiConnectionsPage::verifyAndSave);
    connect(m_key, &QLineEdit::returnPressed, this, &KoraCutAiConnectionsPage::verifyAndSave);

    auto *modelForm = new QFormLayout();
    modelForm->setHorizontalSpacing(KoraCutMetrics::SpaceZone);
    modelForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_model = new QComboBox(m_detail);
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);
    m_model->setMinimumContentsLength(18);
    m_model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_model->completer()->setCompletionMode(QCompleter::PopupCompletion);
    m_model->completer()->setFilterMode(Qt::MatchContains);
    m_model->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    // Même colonne de libellés que le formulaire du dessus.
    auto *modelLabel = new QLabel(i18nc("@label:listbox the AI model the agents use", "Model"), m_detail);
    modelLabel->setAlignment(form->labelAlignment());
    const int labelWidth = qMax(accountLabel->sizeHint().width(), qMax(m_keyLabel->sizeHint().width(), m_baseLabel->sizeHint().width()));
    accountLabel->setMinimumWidth(labelWidth);
    modelLabel->setMinimumWidth(labelWidth);
    modelForm->setLabelAlignment(form->labelAlignment());
    modelForm->addRow(modelLabel, m_model);
    detail->addLayout(modelForm);
    connect(m_model, &QComboBox::currentTextChanged, this, [this]() {
        if (!m_filling) {
            saveModel();
        }
    });

    m_note = new QLabel(m_detail);
    m_note->setWordWrap(true);
    m_note->setObjectName(QStringLiteral("koracutAiHint"));
    m_note->setOpenExternalLinks(true);
    detail->addWidget(m_note);

    auto *removeRow = new QHBoxLayout();
    m_remove = new QPushButton(i18nc("@action:button", "Remove this connection"), m_detail);
    m_remove->setAutoDefault(false);
    removeRow->addWidget(m_remove);
    removeRow->addStretch(1);
    detail->addLayout(removeRow);
    connect(m_remove, &QPushButton::clicked, this, &KoraCutAiConnectionsPage::removeConnection);
    rightLayout->addWidget(m_detail);

    rightLayout->addStretch(1);
    m_footer = new QLabel(right);
    m_footer->setWordWrap(true);
    m_footer->setObjectName(QStringLiteral("koracutAiHint"));
    m_footer->setText(i18nc("@info %1 where keys are stored", "Keys stay on this computer, in %1. They only go to the provider itself, "
                                                             "through the local agents service. KoraCut never shows them again.",
                            KoraCutAiSecrets::backendLabel()));
    rightLayout->addWidget(m_footer);
    scroll->setWidget(right);
    root->addWidget(scroll, 1);

    m_detail->hide();
    connect(m_verifier, &KoraCutAiVerifier::finished, this, &KoraCutAiConnectionsPage::onVerified);
    connect(m_catalog, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && !m_filling) {
            m_connections->setCurrentItem(nullptr);
            selectProvider(item->data(ProviderRole).toString());
        }
    });
    connect(m_connections, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && !m_filling) {
            m_catalog->setCurrentItem(nullptr);
            selectConnection(item->data(ConnectionRole).toString());
        }
    });
    // Glisser une connexion change l'ordre de priorité, tout de suite.
    connect(m_connections->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            QStringList ids;
            for (int i = 0; i < m_connections->count(); ++i) {
                ids << m_connections->item(i)->data(ConnectionRole).toString();
            }
            KoraCutAiConnections::instance()->reorder(ids);
        });
    });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString needle = folded(text.trimmed());
        for (int i = 0; i < m_catalog->count(); ++i) {
            QListWidgetItem *item = m_catalog->item(i);
            item->setHidden(!needle.isEmpty() && !item->data(SearchRole).toString().contains(needle));
        }
    });
    connect(KoraCutAiConnections::instance(), &KoraCutAiConnections::changed, this, &KoraCutAiConnectionsPage::refreshConnections);

    buildCatalog();
    refreshConnections();
    applyColors();
}

QColor KoraCutAiConnectionsPage::ink() const
{
    return palette().color(QPalette::WindowText);
}

void KoraCutAiConnectionsPage::applyColors()
{
    const bool dark = isDark(palette());
    // Badge « Gratuit pour commencer » : le vert « prêt » de la charte, lisible dans les deux thèmes.
    const QString green = dark ? QStringLiteral("#4ADE80") : QStringLiteral("#15803D");
    m_badge->setStyleSheet(QStringLiteral("QLabel { color: %1; border: 1px solid %1; border-radius: 6px; padding: 1px 6px; }").arg(green));
    for (int i = 0; i < m_catalog->count(); ++i) {
        if (QWidget *row = m_catalog->itemWidget(m_catalog->item(i))) {
            for (QLabel *l : row->findChildren<QLabel *>(QStringLiteral("koracutAiRowBadge"))) {
                l->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(green));
            }
        }
    }
    const QColor hint = palette().color(QPalette::PlaceholderText);
    for (QLabel *l : findChildren<QLabel *>(QStringLiteral("koracutAiHint"))) {
        l->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(hint.name()));
    }
}

void KoraCutAiConnectionsPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        applyColors();
        buildCatalog();
        refreshConnections();
    }
}

void KoraCutAiConnectionsPage::buildCatalog()
{
    m_filling = true;
    const QString current = m_catalog->currentItem() ? m_catalog->currentItem()->data(ProviderRole).toString() : QString();
    m_catalog->clear();
    const QList<KoraCutAiProvider> providers = KoraCutAiCatalog::providers();
    for (const KoraCutAiProvider &p : providers) {
        auto *item = new QListWidgetItem(m_catalog);
        item->setData(ProviderRole, p.id);
        item->setData(SearchRole, folded(p.name + QLatin1Char(' ') + p.id + QLatin1Char(' ') + p.description));
        item->setToolTip(p.description);
        auto *row = new QWidget(m_catalog);
        row->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(KoraCutMetrics::SpaceGroup);
        auto *logo = new QLabel(row);
        logo->setPixmap(KoraCutAiCatalog::icon(p, ink(), 20).pixmap(20, 20));
        logo->setFixedSize(20, 20);
        layout->addWidget(logo);
        auto *texts = new QVBoxLayout();
        texts->setSpacing(0);
        texts->addWidget(new QLabel(p.name, row));
        if (p.free) {
            auto *badge = new QLabel(p.local ? i18nc("@label badge on a local AI provider", "Free, on this computer") : i18nc("@label badge on an AI provider", "Free to start"), row);
            badge->setObjectName(QStringLiteral("koracutAiRowBadge"));
            QFont f = badge->font();
            f.setPixelSize(10);
            badge->setFont(f);
            texts->addWidget(badge);
        }
        layout->addLayout(texts, 1);
        item->setSizeHint(QSize(200, p.free ? 40 : 30));
        m_catalog->setItemWidget(item, row);
        if (p.id == current) {
            m_catalog->setCurrentItem(item);
        }
    }
    m_filling = false;
    applyColors();
}

void KoraCutAiConnectionsPage::refreshConnections()
{
    m_filling = true;
    m_connections->clear();
    const QList<KoraCutAiConnection> list = KoraCutAiConnections::instance()->list();
    int n = 0;
    for (const KoraCutAiConnection &c : list) {
        const KoraCutAiProvider p = KoraCutAiCatalog::provider(c.provider);
        QString second = c.model.isEmpty() ? i18nc("@info connection without a model yet", "no model chosen") : c.model;
        if (!c.account.isEmpty()) {
            second += QStringLiteral(" · ") + c.account;
        }
        auto *item = new QListWidgetItem(KoraCutAiCatalog::icon(p, ink(), 20), QStringLiteral("%1. %2\n%3").arg(++n).arg(p.isValid() ? p.name : c.provider, second), m_connections);
        item->setData(ConnectionRole, c.id);
        item->setToolTip(second);
        if (c.id == m_connectionId) {
            m_connections->setCurrentItem(item);
        }
    }
    if (list.isEmpty()) {
        auto *item = new QListWidgetItem(i18nc("@info empty list of AI connections", "No connection yet."), m_connections);
        item->setFlags(Qt::NoItemFlags);
    }
    m_filling = false;
}

void KoraCutAiConnectionsPage::selectProvider(const QString &providerId)
{
    const KoraCutAiProvider p = KoraCutAiCatalog::provider(providerId);
    if (!p.isValid()) {
        return;
    }
    showProvider(p, QString());
}

void KoraCutAiConnectionsPage::selectConnection(const QString &connectionId)
{
    const KoraCutAiConnection c = KoraCutAiConnections::instance()->connection(connectionId);
    if (!c.isValid()) {
        selectProvider(connectionId);
        return;
    }
    showProvider(KoraCutAiCatalog::provider(c.provider), c.id);
}

void KoraCutAiConnectionsPage::showProvider(const KoraCutAiProvider &provider, const QString &connectionId)
{
    m_verifier->cancel();
    m_filling = true;
    m_provider = provider;
    m_connectionId = connectionId;
    const KoraCutAiConnection c = KoraCutAiConnections::instance()->connection(connectionId);
    const bool existing = c.isValid();

    m_empty->hide();
    m_detail->show();
    m_logo->setPixmap(KoraCutAiCatalog::icon(provider, ink(), 32).pixmap(32, 32));
    m_title->setText(provider.name);
    m_badge->setText(provider.local ? i18nc("@label badge on a local AI provider", "Free, on this computer") : i18nc("@label badge on an AI provider", "Free to start"));
    m_badge->setVisible(provider.free);
    QString description = provider.description;
    if (provider.free && !provider.freeNote.isEmpty()) {
        description += (description.isEmpty() ? QString() : QStringLiteral("\n")) + provider.freeNote;
    }
    m_description->setText(description);
    m_description->setVisible(!description.isEmpty());
    m_doc->setVisible(!provider.docUrl.isEmpty());
    m_signup->setVisible(!provider.signupUrl.isEmpty());
    m_getKey->setVisible(!provider.keyUrl.isEmpty());
    m_getKey->setText(provider.local ? i18nc("@action:button open the page to download a local model server", "Download") : i18nc("@action:button open the provider page where API keys are created", "Get a key"));

    if (existing) {
        m_account->setText(c.account);
    } else {
        m_account->clear();
    }
    m_key->clear();
    m_showKey->setChecked(false);
    const bool keyStored = existing && KoraCutAiConnections::instance()->hasKey(c.id);
    const bool showKey = provider.keyRequired || provider.auth != QLatin1String("aucune");
    m_keyLabel->setVisible(showKey);
    m_key->setVisible(showKey);
    if (keyStored) {
        m_key->setPlaceholderText(i18nc("@info:placeholder", "Saved. Leave empty to keep it."));
        m_keyHint->setText(i18nc("@info %1 where keys are stored", "The key is saved in %1. You never have to type it again.", KoraCutAiSecrets::backendLabel()));
    } else {
        m_key->setPlaceholderText(provider.keyRequired ? i18nc("@info:placeholder", "Paste the key here") : i18nc("@info:placeholder", "Optional"));
        m_keyHint->setText(provider.keyRequired ? i18nc("@info", "Click “Get a key”, create it on the provider page, copy it, then paste it here.") : QString());
    }
    m_keyHint->setVisible(showKey && !m_keyHint->text().isEmpty());

    const QString base = existing ? KoraCutAiConnections::effectiveBaseUrl(c, provider) : provider.baseUrl;
    m_baseUrl->setText(base);
    const bool showBase = provider.baseUrlEditable || KoraCutAiCatalog::hasPlaceholder(provider.baseUrl);
    m_baseLabel->setVisible(showBase);
    m_baseUrl->setVisible(showBase);
    m_baseHint->setText(provider.baseUrlHint);
    m_baseHint->setVisible(showBase && !provider.baseUrlHint.isEmpty());

    m_model->clear();
    m_model->addItems(provider.recommendedModels);
    if (existing && !c.model.isEmpty()) {
        if (m_model->findText(c.model) < 0) {
            m_model->insertItem(0, c.model);
        }
        m_model->setCurrentText(c.model);
    } else if (m_model->count() > 0) {
        m_model->setCurrentIndex(0);
    }
    m_model->setEnabled(existing);
    m_note->setText(provider.note);
    m_note->setVisible(!provider.note.isEmpty());
    m_remove->setVisible(existing);
    m_verify->setText(existing ? i18nc("@action:button check the connection again and refresh the list of models", "Verify again")
                               : i18nc("@action:button check the API key with the provider and save it", "Verify and save"));
    if (existing && provider.keyRequired && !keyStored) {
        setStatus(i18nc("@info", "The key of this connection is missing. Paste it, then verify."), true);
    } else {
        setStatus(existing ? i18nc("@info", "This connection is saved. The agents can use it.") : QString(), false);
    }
    // La connexion montrée reste en surbrillance dans « Mes connexions ».
    for (int i = 0; i < m_connections->count(); ++i) {
        if (existing && m_connections->item(i)->data(ConnectionRole).toString() == connectionId) {
            m_connections->setCurrentRow(i);
        }
    }
    m_filling = false;
}

void KoraCutAiConnectionsPage::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
    const bool dark = isDark(palette());
    const QString color = error ? QStringLiteral("#FF7262") : (dark ? QStringLiteral("#4ADE80") : QStringLiteral("#15803D"));
    m_status->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(color));
}

void KoraCutAiConnectionsPage::verifyAndSave()
{
    if (!m_provider.isValid() || m_verifier->running()) {
        return;
    }
    const QString typed = m_key->text().trimmed();
    QString key = typed;
    if (key.isEmpty() && !m_connectionId.isEmpty()) {
        // La clé enregistrée, lue seulement au moment de vérifier.
        key = KoraCutAiSecrets::read(m_connectionId);
    }
    if (m_provider.keyRequired && key.isEmpty()) {
        setStatus(i18nc("@info", "Paste the API key first."), true);
        m_key->setFocus();
        return;
    }
    const QString base = m_baseUrl->text().trimmed();
    if (base.isEmpty() || KoraCutAiCatalog::hasPlaceholder(base)) {
        setStatus(i18nc("@info", "Complete the base URL: replace the part between braces with your own value."), true);
        m_baseUrl->setFocus();
        return;
    }
    m_verify->setEnabled(false);
    setStatus(i18nc("@info", "Asking %1…", m_provider.name), false);
    m_verifier->verify(m_provider, base, key);
}

void KoraCutAiConnectionsPage::onVerified(bool ok, const QStringList &models, const QString &message)
{
    m_verify->setEnabled(true);
    if (!ok) {
        setStatus(message, true);
        return;
    }
    const QString typed = m_key->text().trimmed();
    const QString base = m_baseUrl->text().trimmed();
    // Les modèles de la réponse, le modèle choisi gardé s'il existe encore.
    m_filling = true;
    const QString previous = m_model->currentText();
    m_model->clear();
    m_model->addItems(models.isEmpty() ? m_provider.recommendedModels : models);
    QString chosen = previous;
    if (m_connectionId.isEmpty() || chosen.isEmpty() || (!models.isEmpty() && !models.contains(chosen))) {
        chosen.clear();
        for (const QString &r : std::as_const(m_provider.recommendedModels)) {
            if (models.isEmpty() || models.contains(r)) {
                chosen = r;
                break;
            }
        }
        if (chosen.isEmpty() && !models.isEmpty()) {
            chosen = models.first();
        }
    }
    if (m_model->findText(chosen) < 0 && !chosen.isEmpty()) {
        m_model->insertItem(0, chosen);
    }
    m_model->setCurrentText(chosen);
    m_model->setEnabled(true);
    m_filling = false;

    KoraCutAiConnection c = KoraCutAiConnections::instance()->connection(m_connectionId);
    c.provider = m_provider.id;
    c.account = m_account->text().trimmed();
    c.model = chosen;
    c.baseUrl = base == m_provider.baseUrl ? QString() : base;
    QString error;
    bool saved = false;
    if (c.isValid()) {
        saved = KoraCutAiConnections::instance()->update(c, typed, &error);
    } else {
        const QString id = KoraCutAiConnections::instance()->add(c, typed, &error);
        saved = !id.isEmpty();
        if (saved) {
            m_connectionId = id;
        }
    }
    if (!saved) {
        setStatus(i18nc("@info %1 error", "The key could not be saved: %1", error), true);
        return;
    }
    // La clé ne se réaffiche jamais : le champ se vide, la page dit où elle est.
    const QString id = m_connectionId;
    showProvider(m_provider, id);
    refreshConnections();
    setStatus(models.isEmpty() ? i18nc("@info", "Connection saved. Type the name of the model you want to use.")
                               : i18ncp("@info", "Connection saved. %1 model available.", "Connection saved. %1 models available.", models.size()),
              false);
    if (!models.isEmpty()) {
        m_filling = true;
        const QString keep = m_model->currentText();
        m_model->clear();
        m_model->addItems(models);
        m_model->setCurrentText(keep);
        m_filling = false;
    }
}

void KoraCutAiConnectionsPage::saveModel()
{
    if (m_connectionId.isEmpty()) {
        return;
    }
    KoraCutAiConnection c = KoraCutAiConnections::instance()->connection(m_connectionId);
    const QString model = m_model->currentText().trimmed();
    if (!c.isValid() || model.isEmpty() || c.model == model) {
        return;
    }
    c.model = model;
    c.account = m_account->text().trimmed();
    KoraCutAiConnections::instance()->update(c);
}

void KoraCutAiConnectionsPage::removeConnection()
{
    if (m_connectionId.isEmpty()) {
        return;
    }
    const QString providerId = m_provider.id;
    KoraCutAiConnections::instance()->remove(m_connectionId);
    m_connectionId.clear();
    selectProvider(providerId);
    setStatus(i18nc("@info", "Connection removed, and its key deleted."), false);
}

void KoraCutAiConnectionsPage::openInPreferences(const QString &id)
{
    if (!pCore || !pCore->window()) {
        return;
    }
    pCore->window()->slotShowPreferencePage(Kdenlive::NoPage);
    auto *dialog = qobject_cast<KdenliveSettingsDialog *>(KConfigDialog::exists(QStringLiteral("settings")));
    if (dialog) {
        dialog->showKoraCutAiPage(id);
    }
}
