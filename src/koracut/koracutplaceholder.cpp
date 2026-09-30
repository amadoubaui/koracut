/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracut/koracutplaceholder.h"
#include "koracut/koracuticons.h"
#include "koracut/koracutmodules.h"

#include <KColorScheme>
#include <KLocalizedString>

#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QVBoxLayout>

KoraCutPlaceholder::KoraCutPlaceholder(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("koracutPlaceholder"));
    setAutoFillBackground(true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(40, 40, 40, 40);
    root->setSpacing(0);
    root->addStretch();

    m_symbol = new QLabel(this);
    m_symbol->setAlignment(Qt::AlignCenter);
    root->addWidget(m_symbol);
    root->addSpacing(20);

    m_title = new QLabel(this);
    m_title->setAlignment(Qt::AlignCenter);
    QFont titleFont = font();
    // La police de l'application est réglée en pixels : pointSizeF() vaut alors -1 et l'agrandir
    // en points donnerait un titre minuscule.
    if (titleFont.pixelSize() > 0) {
        titleFont.setPixelSize(titleFont.pixelSize() + 8);
    } else {
        titleFont.setPointSizeF(titleFont.pointSizeF() + 6);
    }
    titleFont.setWeight(QFont::DemiBold);
    m_title->setFont(titleFont);
    root->addWidget(m_title);
    root->addSpacing(8);

    m_detail = new QLabel(this);
    m_detail->setAlignment(Qt::AlignCenter);
    root->addWidget(m_detail);
    root->addStretch();

    refreshPalette();
}

void KoraCutPlaceholder::setModule(const QString &moduleId)
{
    const KoraCutModules::Module *m = KoraCutModules::byId(moduleId);
    if (!m) {
        m_title->setText(i18nc("@info page d'un module vide", "Module under construction"));
        m_detail->clear();
        m_symbol->clear();
        return;
    }
    m_title->setText(i18nc("@info page d'un module vide, %1 est le nom du module", "%1: module under construction", m->name));
    m_detail->setText(i18nc("@info renvoi vers la liste des tâches, %1 est un numéro de chantier",
                            "This module is planned (work package %1). The other modules remain usable in the meantime.",
                            QString::number(m->chantier)));
    KColorScheme scheme(QPalette::Active, KColorScheme::Window);
    m_symbol->setPixmap(KoraCutIcons::pixmap(m->iconPath, 48, scheme.foreground(KColorScheme::InactiveText).color(), devicePixelRatioF()));
}

void KoraCutPlaceholder::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshPalette();
    }
}

void KoraCutPlaceholder::refreshPalette()
{
    KColorScheme scheme(QPalette::Active, KColorScheme::View);
    QPalette pal = palette();
    // Fond général #1E1E1E : la même couleur que le canvas et le moniteur vide.
    pal.setColor(QPalette::Window, scheme.background(KColorScheme::NormalBackground).color());
    pal.setColor(QPalette::WindowText, scheme.foreground(KColorScheme::NormalText).color());
    setPalette(pal);

    if (m_detail) {
        QPalette dp = m_detail->palette();
        dp.setColor(QPalette::WindowText, scheme.foreground(KColorScheme::InactiveText).color());
        m_detail->setPalette(dp);
    }
}
