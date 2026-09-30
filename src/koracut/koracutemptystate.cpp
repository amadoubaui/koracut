/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracut/koracutemptystate.h"
#include "koracut/koracutmetrics.h"

#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPaintDevice>
#include <QPalette>
#include <QPixmap>

namespace KoraCutEmptyState {

QRect paint(QPainter &painter, const QRect &rect, const QPalette &palette, const Modele &modele)
{
    if (modele.titre.isEmpty()) {
        return QRect();
    }

    // La police de l'application est réglée en pixels : `pointSizeF()` y vaut -1, et l'agrandir en
    // points donnerait un titre de deux points. C'était le défaut de l'état vide d'avant, illisible
    // même agrandi cinq fois.
    QFont policeTitre = painter.font();
    QFont policePhrase = painter.font();
    QFont policeBouton = painter.font();
    if (policeTitre.pixelSize() > 0) {
        policeTitre.setPixelSize(KoraCutMetrics::FontPage);
        policePhrase.setPixelSize(KoraCutMetrics::FontBase);
        policeBouton.setPixelSize(KoraCutMetrics::FontBase);
    } else {
        policeTitre.setPointSizeF(policeTitre.pointSizeF() + 4);
    }
    policeTitre.setWeight(QFont::DemiBold);

    const QFontMetrics fmTitre(policeTitre);
    const QFontMetrics fmPhrase(policePhrase);
    const QFontMetrics fmBouton(policeBouton);

    const bool avecBouton = !modele.bouton.isEmpty();
    const bool avecPhrase = !modele.phrase.isEmpty();
    const bool avecIcone = !modele.icone.isEmpty();
    const int picto = 32;
    const int hauteurBouton = 28;
    const int largeurBouton = avecBouton ? fmBouton.horizontalAdvance(modele.bouton) + 2 * KoraCutMetrics::SpaceZone : 0;

    // Le titre et la phrase passent à la ligne plutôt que de faire disparaître l'état vide.
    // Mesuré : « Change de catégorie dans la liste au-dessus. » fait environ 246 px à 12 px, donc
    // exiger cette largeur d'un coup ferait disparaître tout le bloc dans un dock de 280 px, et un
    // panneau vide sans rien dedans a l'air cassé. Seul le bouton, qui ne se coupe pas, impose une
    // largeur.
    const int drapeaux = Qt::TextWordWrap | Qt::AlignHCenter;
    const int largeurTexte = rect.width() - 2 * KoraCutMetrics::SpacePanel;
    if (largeurTexte <= 0) {
        return QRect();
    }
    const QRect mesureTitre = fmTitre.boundingRect(QRect(0, 0, largeurTexte, 4000), drapeaux, modele.titre);
    const QRect mesurePhrase = avecPhrase ? fmPhrase.boundingRect(QRect(0, 0, largeurTexte, 4000), drapeaux, modele.phrase) : QRect();

    int hauteurTotale = mesureTitre.height();
    if (avecIcone) {
        hauteurTotale += picto + KoraCutMetrics::SpaceZone;
    }
    if (avecPhrase) {
        hauteurTotale += KoraCutMetrics::SpaceTight + mesurePhrase.height();
    }
    if (avecBouton) {
        hauteurTotale += KoraCutMetrics::SpacePanel + hauteurBouton;
    }

    if (rect.width() < largeurBouton + 2 * KoraCutMetrics::SpacePanel || rect.height() < hauteurTotale + KoraCutMetrics::SpacePanel) {
        return QRect();
    }

    const int cx = rect.center().x();
    int y = rect.center().y() - hauteurTotale / 2;

    const QColor couleurTexte = palette.color(QPalette::Text);
    QColor couleurPhrase = couleurTexte;
    couleurPhrase.setAlphaF(0.62);

    if (avecIcone) {
        const QIcon icone = QIcon::fromTheme(modele.icone);
        const qreal ratio = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
        QPixmap pix = icone.pixmap(qRound(picto * ratio), qRound(picto * ratio));
        pix.setDevicePixelRatio(ratio);
        if (!pix.isNull()) {
            painter.save();
            painter.setOpacity(0.38);
            painter.drawPixmap(cx - picto / 2, y, picto, picto, pix);
            painter.restore();
        }
        y += picto + KoraCutMetrics::SpaceZone;
    }

    painter.save();
    painter.setFont(policeTitre);
    painter.setPen(couleurTexte);
    painter.drawText(QRect(rect.left() + KoraCutMetrics::SpacePanel, y, largeurTexte, mesureTitre.height()), drapeaux, modele.titre);
    y += mesureTitre.height();

    if (avecPhrase) {
        y += KoraCutMetrics::SpaceTight;
        painter.setFont(policePhrase);
        painter.setPen(couleurPhrase);
        painter.drawText(QRect(rect.left() + KoraCutMetrics::SpacePanel, y, largeurTexte, mesurePhrase.height()), drapeaux, modele.phrase);
        y += mesurePhrase.height();
    }

    QRect zoneBouton;
    if (avecBouton) {
        y += KoraCutMetrics::SpacePanel;
        zoneBouton = QRect(cx - largeurBouton / 2, y, largeurBouton, hauteurBouton);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        // Bouton secondaire de la charte : #383838, rayon 4, texte à 100 %.
        painter.setBrush(palette.color(QPalette::Button));
        painter.drawRoundedRect(zoneBouton, KoraCutMetrics::RadiusControl, KoraCutMetrics::RadiusControl);
        painter.setFont(policeBouton);
        painter.setPen(couleurTexte);
        painter.drawText(zoneBouton, Qt::AlignCenter, modele.bouton);
    }
    painter.restore();

    return zoneBouton;
}

} // namespace KoraCutEmptyState
