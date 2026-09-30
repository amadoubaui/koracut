/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutextensionsdialog.h"

#include <KLocalizedString>
#include <KNSWidgets/Dialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

KoraCutExtensionsDialog::KoraCutExtensionsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Extensions"));
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(i18nc("@info", "Effects, transitions, titles, LUTs, export presets and shortcuts shared by others. Choose a category, KoraCut downloads and installs."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    struct Source
    {
        QString icon;
        QString title;
        QString description;
        QString knsrc;
    };
    const QList<Source> sources = {
        {QStringLiteral("show-all-effects"), i18nc("@title catégorie d'extensions", "Effects"),
         i18nc("@info", "Ready-made effect settings, one click on a clip."), QStringLiteral(":data/koracut_effects.knsrc")},
        {QStringLiteral("composite-track-on"), i18nc("@title catégorie d'extensions", "Transitions"),
         i18nc("@info", "Wipe shapes to go from one shot to the next."), QStringLiteral(":data/koracut_wipes.knsrc")},
        {QStringLiteral("draw-text"), i18nc("@title catégorie d'extensions", "Title templates"),
         i18nc("@info", "Titles and lower thirds to customize."), QStringLiteral(":data/koracut_titles.knsrc")},
        {QStringLiteral("color-management"), i18nc("@title catégorie d'extensions", "Color grading LUTs"),
         i18nc("@info", "Color tables to give a sequence a look."), QStringLiteral(":data/koracut_luts.knsrc")},
        {QStringLiteral("document-export"), i18nc("@title catégorie d'extensions", "Export presets"),
         i18nc("@info", "Render settings for a platform or a format."), QStringLiteral(":data/koracut_renderprofiles.knsrc")},
        {QStringLiteral("configure-shortcuts"), i18nc("@title catégorie d'extensions", "Keyboard shortcuts"),
         i18nc("@info", "Shortcut sets, for example those of another editing application."),
         QStringLiteral(":data/koracut_keyboardschemes.knsrc")},
    };

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    int row = 0;
    for (const Source &source : sources) {
        auto *icon = new QLabel(this);
        icon->setPixmap(QIcon::fromTheme(source.icon).pixmap(20, 20));
        auto *text = new QLabel(QStringLiteral("<b>%1</b><br>%2").arg(source.title.toHtmlEscaped(), source.description.toHtmlEscaped()), this);
        text->setWordWrap(true);
        // KoraCut, 25 septembre 2026 : un libellé replié en texte riche est mesuré trop court,
        // le bas des lettres de la deuxième ligne était rogné (« jeux », « pour »). Deux lignes
        // pleines au moins, plus l'air des jambages.
        text->ensurePolished();
        text->setMinimumHeight(2 * text->fontMetrics().lineSpacing() + 6);
        auto *button = new QPushButton(i18nc("@action:button", "Browse"), this);
        button->setAutoDefault(false);
        connect(button, &QPushButton::clicked, this, [this, knsrc = source.knsrc]() { browse(knsrc); });
        grid->addWidget(icon, row, 0, Qt::AlignTop);
        grid->addWidget(text, row, 1);
        grid->addWidget(button, row, 2, Qt::AlignVCenter);
        ++row;
    }
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    auto *themes = new QLabel(i18nc("@info", "Color themes are installed from a file: View > Install a Theme…"), this);
    themes->setWordWrap(true);
    themes->setEnabled(false);
    layout->addWidget(themes);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addStretch();
    layout->addWidget(buttons);
    resize(560, minimumSizeHint().height());
}

void KoraCutExtensionsDialog::browse(const QString &knsrc)
{
    KNSWidgets::Dialog dialog(knsrc, this);
    dialog.exec();
    const QList<KNSCore::Entry> entries = dialog.changedEntries();
    if (!entries.isEmpty()) {
        Q_EMIT installed(knsrc, entries);
        m_status->setText(i18nc("@info", "Done. What you installed is ready to use in the panels."));
    }
}
