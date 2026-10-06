/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutsupportdialog.h"

#include "koracuticons.h"
#include "koracutmetrics.h"

#include <KLocalizedString>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// Largeur fixe : le texte se replie toujours au même endroit, sur les trois systèmes.
constexpr int kWidth = 440;
constexpr int kMargin = 24;
} // namespace

QUrl KoraCutSupportDialog::donationUrl()
{
    return QUrl(QStringLiteral("https://buymeacoffee.com/amadouba"));
}

QUrl KoraCutSupportDialog::otherWaysUrl()
{
    return QUrl(QStringLiteral("https://koracut.org/soutenir"));
}

KoraCutSupportDialog *KoraCutSupportDialog::open(QWidget *parent)
{
    static QPointer<KoraCutSupportDialog> current;
    if (!current) {
        current = new KoraCutSupportDialog(parent);
        current->setAttribute(Qt::WA_DeleteOnClose);
    }
    current->show();
    current->raise();
    current->activateWindow();
    return current;
}

KoraCutSupportDialog::KoraCutSupportDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("koracutSupportDialog"));
    setWindowTitle(i18nc("@title:window", "Support KoraCut"));
    setModal(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kMargin, kMargin, kMargin, 20);
    layout->setSpacing(0);

    // En-tête : le symbole (seule trace de la marque) et le titre.
    auto *header = new QHBoxLayout;
    header->setSpacing(10);
    m_symbol = new QLabel(this);
    m_symbol->setFixedSize(20, 20);
    header->addWidget(m_symbol);
    auto *title = new QLabel(i18nc("@title", "Support KoraCut"), this);
    title->setObjectName(QStringLiteral("koracutSupportTitle"));
    header->addWidget(title);
    header->addStretch();
    layout->addLayout(header);
    layout->addSpacing(14);

    auto *text = new QLabel(i18nc("@info mot du fondateur, à la première personne",
                                  "KoraCut is free software, free of charge, and it will stay that way. I build it in Dakar, every day, with no "
                                  "investor and no advertising. If the application is useful to you, you can buy me a coffee: a few dollars, and it "
                                  "moves forward faster."),
                            this);
    text->setObjectName(QStringLiteral("koracutSupportText"));
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::NoTextInteraction);
    layout->addWidget(text);
    layout->addSpacing(8);

    // Un nom propre ne se traduit pas.
    auto *signature = new QLabel(QStringLiteral("Amadou Ba"), this);
    signature->setObjectName(QStringLiteral("koracutSupportSignature"));
    layout->addWidget(signature);
    layout->addSpacing(18);

    // Carte : ce que le don paie, trois lignes.
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("koracutSupportCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 14);
    cardLayout->setSpacing(8);
    auto *cardTitle = new QLabel(i18nc("@title:group", "What your coffee pays for"), card);
    cardTitle->setObjectName(QStringLiteral("koracutSupportCardTitle"));
    cardLayout->addWidget(cardTitle);
    const QStringList lines = {
        i18nc("@info ce que le don paie", "Development time"),
        i18nc("@info ce que le don paie", "The Windows, Linux and mobile versions"),
        i18nc("@info ce que le don paie", "Tests on modest machines"),
    };
    for (const QString &line : lines) {
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        auto *dot = new QLabel(card);
        dot->setObjectName(QStringLiteral("koracutSupportDot"));
        dot->setFixedSize(6, 6);
        auto *label = new QLabel(line, card);
        label->setObjectName(QStringLiteral("koracutSupportLine"));
        row->addWidget(dot, 0, Qt::AlignVCenter);
        row->addWidget(label, 1);
        cardLayout->addLayout(row);
    }
    layout->addWidget(card);
    layout->addSpacing(20);

    // Pied : le lien à gauche, Fermer et le bouton principal à droite.
    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    auto *other = new QPushButton(i18nc("@action:button lien vers le site", "Other ways to help"), this);
    other->setObjectName(QStringLiteral("koracutSupportLink"));
    other->setFlat(true);
    other->setCursor(Qt::PointingHandCursor);
    other->setAutoDefault(false);
    other->setToolTip(otherWaysUrl().toString());
    connect(other, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(otherWaysUrl()); });
    footer->addWidget(other);
    footer->addStretch();
    auto *close = new QPushButton(i18nc("@action:button", "Close"), this);
    close->setObjectName(QStringLiteral("koracutSupportCloseButton"));
    close->setAutoDefault(false);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    footer->addWidget(close);
    auto *donate = new QPushButton(i18nc("@action:button ouvre la page de don dans le navigateur", "Buy Me a Coffee"), this);
    donate->setObjectName(QStringLiteral("koracutSupportPrimaryButton"));
    donate->setCursor(Qt::PointingHandCursor);
    donate->setToolTip(donationUrl().toString());
    donate->setDefault(true);
    connect(donate, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(donationUrl()); });
    footer->addWidget(donate);
    layout->addLayout(footer);
    // Le clavier part du bouton principal, pas du lien (premier dans l'ordre de la fenêtre).
    donate->setFocus();

    // La hauteur vient du texte replié à cette largeur, quelle que soit la langue.
    ensurePolished();
    updateSymbol();
    setFixedWidth(kWidth);
    const int height = layout->totalHeightForWidth(kWidth);
    setFixedHeight(height > 0 ? height : sizeHint().height());
}

void KoraCutSupportDialog::updateSymbol()
{
    if (!m_symbol) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    m_symbol->setPixmap(KoraCutIcons::pixmap(QStringLiteral(":/koracut/koracut-symbole.svg"), 20, palette().color(QPalette::WindowText), dpr));
}

void KoraCutSupportDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    // Un thème appliqué fenêtre ouverte : le symbole reprend la couleur du texte.
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        updateSymbol();
    }
}
