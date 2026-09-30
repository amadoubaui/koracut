/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracut/koracutstyle.h"
#include "koracut/koracutmetrics.h"

#include <KColorScheme>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QAction>
#include <QFrame>
#include <QTimer>
#include <QToolBar>
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include "koracut/koracutthemes.h"

#include <QStyleOption>
#include <QToolButton>
#include <QWidget>

namespace {

/** Le fond d'un survol : rgba(255,255,255,.07) de la charte. */
QColor survolColor()
{
    // Le voile du thème (blanc sur Sombre, noir sur Clair, vert sur Matrix), gardé en mémoire.
    return KoraCutThemes::token(QStringLiteral("voile"), 18);
}

/** L'accent de la charte, #0D99FF, pris au schéma de couleurs.
 *
 * Et non `palette.color(QPalette::Highlight)` : mesuré à l'écran le 21 septembre 2026, la teinte
 * qui en sortait dans un delegate d'item view ne valait pas l'accent (le fond de sélection tombait
 * à #212A31 au lieu de #213A4C). Le schéma, lui, donne la valeur exacte.
 */
QColor accentColor()
{
    // Gardé en cache : cette fonction est appelée une fois par cellule peinte, donc des milliers
    // de fois par seconde quand on fait défiler un chutier de deux cents clips, pour une valeur
    // qui ne change qu'au changement de thème. La clé de cache de la palette de l'application
    // change quand le schéma change, c'est elle qui invalide.
    static QColor cache;
    static qint64 clePalette = -1;
    const qint64 cle = QApplication::palette().cacheKey();
    if (cle != clePalette || !cache.isValid()) {
        KColorScheme selection(QPalette::Active, KColorScheme::Selection);
        cache = selection.background(KColorScheme::NormalBackground).color();
        clePalette = cle;
    }
    return cache;
}

/** Le fond d'une ligne sélectionnée : rgba(13,153,255,.18) de la charte. */
QColor selectionColor()
{
    QColor accent = accentColor();
    accent.setAlpha(46);
    return accent;
}

/** Dessine un chevron vers le bas, centré dans @p zone.
 *
 * Même vocabulaire que le jeu d'icônes : trait de 1,5 px, bouts ronds, pas de remplissage.
 * 62 % de la couleur du texte au repos, 100 % au survol : le NN/g rappelle qu'une petite flèche
 * est très souvent prise pour une décoration, donc elle a besoin de contraste, pas de 38 %
 * dans un coin.
 */
void peintChevron(QPainter *painter, const QRectF &zone, const QColor &couleurTexte, bool survol, bool versLeHaut = false)
{
    const qreal demi = KoraCutMetrics::MenuChevron / 2.0;
    const QPointF centre = zone.center();
    const qreal sens = versLeHaut ? -1.0 : 1.0;
    QPainterPath chevron;
    chevron.moveTo(centre.x() - demi, centre.y() - sens * demi / 2);
    chevron.lineTo(centre.x(), centre.y() + sens * demi / 2);
    chevron.lineTo(centre.x() + demi, centre.y() - sens * demi / 2);
    QColor trait = couleurTexte;
    trait.setAlpha(survol ? 255 : 158);
    QPen stylo(trait, 1.5);
    stylo.setCapStyle(Qt::RoundCap);
    stylo.setJoinStyle(Qt::RoundJoin);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(stylo);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(chevron);
    painter->restore();
}

} // namespace

KoraCutStyle::MenuKind KoraCutStyle::menuKind(const QWidget *widget)
{
    const auto *bouton = qobject_cast<const QToolButton *>(widget);
    if (!bouton) {
        return MenuKind::None;
    }
    // Les boutons dont la feuille de style retire la flèche (barre du haut, « ? », tâches, script)
    // gardent leur dessin. Avant le 25 septembre 2026, tout bouton nommé « koracut… » était exclu :
    // le zoom du moniteur et le bouton Rendu recevaient alors la flèche de Qt, posée sous la
    // ligne de base, dans le coin bas droit.
    static const QStringList sansChevron = {QStringLiteral("koracutMainMenu"),       QStringLiteral("koracutLayoutButton"),
                                            QStringLiteral("koracutHelpButton"),     QStringLiteral("koracutBackgroundJobs"),
                                            QStringLiteral("koracutScriptBarButton"), QStringLiteral("koracutSymbol"),
                                            QStringLiteral("koracutModuleTab")};
    // Le bouton d'aide « ? » des Préférences (KContextualHelpButton) ouvre une bulle, pas un menu :
    // pas de chevron, la feuille de style retire aussi celui de Qt.
    if (bouton->property("koracutNoChevron").toBool() || sansChevron.contains(bouton->objectName()) || bouton->inherits("KContextualHelpButton")) {
        return MenuKind::None;
    }
    const bool aMenu = bouton->menu() != nullptr || (bouton->defaultAction() && bouton->defaultAction()->menu() != nullptr);
    if (!aMenu) {
        return MenuKind::None;
    }
    return bouton->popupMode() == QToolButton::MenuButtonPopup ? MenuKind::Split : MenuKind::Whole;
}

KoraCutStyle::KoraCutStyle(QStyle *baseStyle)
    : QProxyStyle(baseStyle)
{
}

bool KoraCutStyle::drawItemViewPanel(const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    const auto *vopt = qstyleoption_cast<const QStyleOptionViewItem *>(option);
    if (!vopt) {
        return false;
    }
    const bool selected = vopt->state & QStyle::State_Selected;
    const bool survol = (vopt->state & QStyle::State_MouseOver) && (vopt->state & QStyle::State_Enabled);
    if (!selected && !survol) {
        return true; // Au repos, une ligne n'a pas de fond. Rien à peindre, mais rien à déléguer non plus.
    }

    // Rayon 4, inséré de 2 px en haut et en bas : la pastille respire dans une ligne de 26 px.
    QRectF r = QRectF(vopt->rect).adjusted(0, 2, 0, -2);
    const qreal rayon = KoraCutMetrics::RadiusControl;
    // Une ligne d'arbre à plusieurs colonnes est peinte colonne par colonne. Si on arrondissait
    // chaque morceau, une ligne donnerait un chapelet de pastilles : on n'arrondit que les bouts.
    bool gauche = true;
    bool droite = true;
    switch (vopt->viewItemPosition) {
    case QStyleOptionViewItem::Beginning:
        droite = false;
        break;
    case QStyleOptionViewItem::Middle:
        gauche = false;
        droite = false;
        break;
    case QStyleOptionViewItem::End:
        gauche = false;
        break;
    default:
        break;
    }
    if (gauche) {
        r.adjust(2, 0, 0, 0);
    }
    if (droite) {
        r.adjust(0, 0, -2, 0);
    }
    if (r.width() <= 0 || r.height() <= 0) {
        return true;
    }

    // Les coins à laisser carrés s'obtiennent en débordant le rectangle de ce côté et en rognant
    // au passage : l'arrondi tombe hors de la zone visible. C'est la même image qu'une union de
    // chemins, sans construire ni unir deux `QPainterPath` par cellule, ce qui était la partie la
    // plus chère du dessin d'une ligne d'arbre à plusieurs colonnes. La cible du projet est une
    // machine à quatre cœurs, le défilement d'une liste est le chemin le plus chaud de l'interface.
    QRectF dessin = r;
    if (!gauche) {
        dessin.adjust(-rayon, 0, 0, 0);
    }
    if (!droite) {
        dessin.adjust(0, 0, rayon, 0);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(r.toAlignedRect(), Qt::IntersectClip);
    painter->setPen(Qt::NoPen);
    if (selected) {
        painter->setBrush(selectionColor());
        painter->drawRoundedRect(dessin, rayon, rayon);
    }
    if (survol) {
        painter->setBrush(survolColor());
        painter->drawRoundedRect(dessin, rayon, rayon);
    }
    painter->restore();
    Q_UNUSED(widget)
    return true;
}

bool KoraCutStyle::drawBranch(const QStyleOption *option, QPainter *painter) const
{
    // Guides d'arborescence : on ne peint rien. Une branche sans enfant n'a plus ni tiret ni
    // pointillé vertical, l'indentation suffit à dire la profondeur.
    if (!(option->state & QStyle::State_Children)) {
        return true;
    }
    const bool ouvert = option->state & QStyle::State_Open;
    // Chevron au trait, même vocabulaire que le jeu d'icônes : 1,5 px, bouts ronds, pas de remplissage.
    const QRectF r(option->rect);
    const qreal cote = qMin<qreal>(qMin(r.width(), r.height()) * 0.34, 4.0);
    const QPointF centre = r.center();
    QPainterPath chevron;
    if (ouvert) {
        chevron.moveTo(centre.x() - cote, centre.y() - cote / 2);
        chevron.lineTo(centre.x(), centre.y() + cote / 2);
        chevron.lineTo(centre.x() + cote, centre.y() - cote / 2);
    } else {
        chevron.moveTo(centre.x() - cote / 2, centre.y() - cote);
        chevron.lineTo(centre.x() + cote / 2, centre.y());
        chevron.lineTo(centre.x() - cote / 2, centre.y() + cote);
    }
    QColor trait = option->palette.color(QPalette::Text);
    trait.setAlpha((option->state & QStyle::State_MouseOver) ? 255 : 158);
    QPen stylo(trait, 1.5);
    stylo.setCapStyle(Qt::RoundCap);
    stylo.setJoinStyle(Qt::RoundJoin);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(stylo);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(chevron);
    painter->restore();
    return true;
}

bool KoraCutStyle::drawToolButtonArrow(const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    const MenuKind genre = menuKind(widget);
    if (genre == MenuKind::None) {
        return false;
    }
    // On ne se sert pas du rectangle qu'on nous passe : selon le chemin emprunté, Qt y met soit
    // le coin bas-droit du bouton, soit la bande du sous-contrôle. On veut toujours la même
    // chose, la gouttière de droite sur toute la hauteur, chevron centré dedans.
    const QRect plein = widget->rect();
    if (plein.width() <= KoraCutMetrics::MenuGutter + 4 || plein.height() < 8) {
        return false;
    }
    const QRect gouttiere(plein.right() - KoraCutMetrics::MenuGutter + 1, plein.top(), KoraCutMetrics::MenuGutter, plein.height());
    const bool survol = option->state & QStyle::State_MouseOver;
    const QColor texte = option->palette.color(QPalette::ButtonText);

    if (genre == MenuKind::Split) {
        // Deux zones cliquables, et le filet est la seule chose qui le dit (NN/g, Split Buttons).
        // Il s'arrête à 4 px des bords pour ne pas toucher le fond arrondi du bouton.
        QColor filet = texte;
        filet.setAlpha(31);
        painter->save();
        painter->setPen(QPen(filet, 1));
        const qreal x = gouttiere.left() - 0.5;
        painter->drawLine(QPointF(x, gouttiere.top() + 4), QPointF(x, gouttiere.bottom() - 3));
        painter->restore();
    }
    peintChevron(painter, gouttiere, texte, survol);
    return true;
}

QSize KoraCutStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &contentsSize, const QWidget *widget) const
{
    QSize taille = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    if (type == CT_ToolButton) {
        // Un bouton à menu a besoin de sa gouttière. Qt ne la réserve que pour le mode à deux
        // zones (`MenuButtonPopup`, par `PM_MenuButtonIndicator`) ; pour les autres il ne réserve
        // rien du tout, et c'est de là que vient le chevron posé sur le glyphe.
        if (menuKind(widget) == MenuKind::Whole) {
            taille.rwidth() += KoraCutMetrics::MenuGutter;
        }
    }
    return taille;
}

void KoraCutStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const
{
    if (control == CC_SpinBox) {
        if (const auto *champ = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
            // Les deux flèches d'un champ à pas, dont le timecode des moniteurs. Le style du
            // système en faisait deux grands triangles bleus, c'est-à-dire la couleur de la tête
            // de lecture et des boutons primaires, pour deux boutons de réglage ; Fusion en fait
            // deux triangles gris. On reprend le chevron de la charte, même taille et même
            // couleur que partout ailleurs. Les deux flèches ne se dessinent pas par
            // `PE_IndicatorSpinUp` : ni le style de macOS ni Fusion n'y passent, ils les peignent
            // eux-mêmes au fond de `CC_SpinBox`. C'est donc ici qu'il faut les prendre.
            QStyleOptionSpinBox sansFleches = *champ;
            sansFleches.subControls &= ~(SC_SpinBoxUp | SC_SpinBoxDown);
            QProxyStyle::drawComplexControl(control, &sansFleches, painter, widget);

            // `ButtonText` et non `Text` : la feuille de style y recopie la couleur `color` de la
            // règle du champ, alors que `Text` est resté celui de la palette par défaut de Qt,
            // d'où des chevrons bleus sur le timecode des moniteurs.
            const QColor texte = champ->palette.color(QPalette::ButtonText);
            if (champ->subControls & SC_SpinBoxUp) {
                const QRect haut = subControlRect(CC_SpinBox, champ, SC_SpinBoxUp, widget);
                peintChevron(painter, haut, texte, champ->activeSubControls & SC_SpinBoxUp, true);
            }
            if (champ->subControls & SC_SpinBoxDown) {
                const QRect bas = subControlRect(CC_SpinBox, champ, SC_SpinBoxDown, widget);
                peintChevron(painter, bas, texte, champ->activeSubControls & SC_SpinBoxDown, false);
            }
            return;
        }
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

void KoraCutStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    switch (element) {
    case PE_FrameFocusRect: {
        // WCAG 2.4.13 : périmètre de 2 px, contraste 3:1 entre focalisé et non focalisé.
        // L'accent sur la surface donne 4,67:1.
        QRectF r = QRectF(option->rect).adjusted(1, 1, -1, -1);
        if (r.width() <= 0 || r.height() <= 0) {
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen stylo(accentColor(), 2);
        painter->setPen(stylo);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(r, KoraCutMetrics::RadiusControl, KoraCutMetrics::RadiusControl);
        painter->restore();
        return;
    }
    case PE_PanelItemViewItem:
        if (drawItemViewPanel(option, painter, widget)) {
            return;
        }
        break;
    case PE_PanelItemViewRow:
        // La ligne entière ne se peint pas : c'est la cellule qui porte la sélection, sinon
        // le fond est posé deux fois et l'arrondi disparaît sous un rectangle plein.
        return;
    case PE_IndicatorBranch:
        if (drawBranch(option, painter)) {
            return;
        }
        break;
    case PE_IndicatorArrowDown:
        if (drawToolButtonArrow(option, painter, widget)) {
            return;
        }
        // La flèche d'une liste déroulante. Avec la feuille de style, Qt la demande au style du
        // système, qui sur le Mac ne peint rien à cet endroit : « Mode normal », « auto », « SDL »
        // se lisaient comme des champs de texte (25 septembre 2026). Même chevron que partout.
        if (qobject_cast<const QComboBox *>(widget) && option->rect.width() > 4 && option->rect.height() > 4) {
            const bool survol = (option->state & QStyle::State_MouseOver) && (option->state & QStyle::State_Enabled);
            QColor texte = option->palette.color(QPalette::ButtonText);
            if (!(option->state & QStyle::State_Enabled)) {
                texte.setAlpha(97);
            }
            peintChevron(painter, option->rect, texte, survol);
            return;
        }
        break;

    default:
        break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

// La flèche « plus d'outils » d'une barre trop étroite (barre de la timeline dans la disposition
// verticale, 25 septembre 2026) : le style de base rendait une icône vide, et les boutons qui ne
// tenaient pas (Coupe auto, Rendu…) disparaissaient sans aucun moyen d'y accéder.
QIcon KoraCutStyle::standardIcon(StandardPixmap standardIcon, const QStyleOption *option, const QWidget *widget) const
{
    if (standardIcon == SP_ToolBarHorizontalExtensionButton) {
        const QIcon icon = QIcon::fromTheme(QStringLiteral("go-next"));
        if (!icon.isNull()) {
            return icon;
        }
    } else if (standardIcon == SP_ToolBarVerticalExtensionButton) {
        const QIcon icon = QIcon::fromTheme(QStringLiteral("go-down"));
        if (!icon.isNull()) {
            return icon;
        }
    }
    return QProxyStyle::standardIcon(standardIcon, option, widget);
}

int KoraCutStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_ToolBarItemSpacing:
        return KoraCutMetrics::SpaceTight;
    case PM_ToolBarSeparatorExtent:
        // Les groupes d'une barre d'outils se séparent par une gouttière, pas par un trait.
        return KoraCutMetrics::SpaceGroup;
    case PM_LayoutHorizontalSpacing:
    case PM_LayoutVerticalSpacing:
        return KoraCutMetrics::SpaceGroup;
    case PM_ButtonDefaultIndicator:
        return 0;
    case PM_MenuButtonIndicator:
        // La gouttière du chevron, pour le mode à deux zones. Qt s'en sert à la fois pour la
        // largeur du bouton et pour découper `SC_ToolButtonMenu`.
        return KoraCutMetrics::MenuGutter;
    default:
        break;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

int KoraCutStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *returnData) const
{
    switch (hint) {
    case SH_ItemView_ShowDecorationSelected:
        // La sélection couvre aussi la vignette : une pastille qui s'arrête avant l'icône
        // donne une ligne coupée en deux.
        return 1;
    case SH_ItemView_DrawDelegateFrame:
        return 0;
    case SH_DialogButtonBox_ButtonsHaveIcons:
        // Annuler, OK, Fermer : du texte seul, comme sur macOS et Windows 11. KGuiItem lit ce réglage aussi.
        return 0;
    case SH_FormLayoutLabelAlignment:
        // Libellés alignés à gauche : un bord franc des deux côtés (document 11, § 3.10).
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    default:
        break;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

void KoraCutStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    if (!widget) {
        return;
    }
    // Un bouton de fenêtre qui a reçu son icône avant que le style ne soit posé (KStandardGuiItem).
    if (auto *button = qobject_cast<QPushButton *>(widget); button && qobject_cast<QDialogButtonBox *>(button->parentWidget())) {
        button->setIcon(QIcon());
    }
    // Les formulaires des fenêtres de Kdenlive (fichiers .ui) forcent l'alignement à droite : on le remet
    // à gauche, une fois, quand la fenêtre est préparée.
    if (widget->isWindow() && widget->layout()) {
        QList<QFormLayout *> forms = widget->findChildren<QFormLayout *>();
        for (QFormLayout *form : std::as_const(forms)) {
            form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            // Le « : » en fin de libellé ne sert plus quand les libellés font une colonne à gauche.
            for (int row = 0; row < form->rowCount(); ++row) {
                QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
                auto *label = item ? qobject_cast<QLabel *>(item->widget()) : nullptr;
                if (label && label->textFormat() != Qt::RichText && !label->text().contains(QLatin1Char('<'))) {
                    QString t = label->text().trimmed();
                    if (t.endsWith(QLatin1Char(':'))) {
                        t.chop(1);
                        label->setText(t.trimmed());
                    }
                }
            }
        }
    }
}

int KoraCutStyle::layoutSpacing(QSizePolicy::ControlType control1, QSizePolicy::ControlType control2, Qt::Orientation orientation,
                                const QStyleOption *option, const QWidget *widget) const
{
    // 12 px entre un contrôle et son libellé, 4 px entre deux contrôles de même nature,
    // 8 px par défaut. Grille de base 4 px, échelle fermée.
    if (control1 == QSizePolicy::Label || control2 == QSizePolicy::Label) {
        return KoraCutMetrics::SpaceZone;
    }
    if (control1 == control2) {
        return KoraCutMetrics::SpaceTight;
    }
    Q_UNUSED(orientation)
    Q_UNUSED(option)
    Q_UNUSED(widget)
    return KoraCutMetrics::SpaceGroup;
}

namespace {
// Le trait est peint par-dessus le bouton, dans le même évènement de peinture : on laisse le bouton
// se peindre (feuille de style comprise), puis on trace le trait. Un widget enfant posé au bas du
// bouton, essayé d'abord, n'était jamais peint (23 septembre 2026). La couleur est celle de
// sélection du thème, lue dans son fichier .colors (#0D99FF dans les thèmes KoraCut) : la palette de
// l'application porte une surbrillance translucide, faite pour les listes.
class AccentWatcher : public QObject
{
public:
    using QObject::QObject;

    void watchAll(QToolBar *toolbar)
    {
        const auto buttons = toolbar->findChildren<QToolButton *>(Qt::FindDirectChildrenOnly);
        for (QToolButton *button : buttons) {
            if (!button->property("koracutAccent").toBool()) {
                button->setProperty("koracutAccent", true);
                button->installEventFilter(this);
                button->update();
            }
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ChildAdded) {
            // Un bouton ajouté à la barre après coup : on attend qu'il soit construit.
            if (auto *toolbar = qobject_cast<QToolBar *>(watched)) {
                QTimer::singleShot(0, this, [this, toolbar]() { watchAll(toolbar); });
            }
            return false;
        }
        if (event->type() != QEvent::Paint || m_painting) {
            return false;
        }
        auto *button = qobject_cast<QToolButton *>(watched);
        if (!button || !button->isChecked()) {
            return false;
        }
        m_painting = true;
        QCoreApplication::sendEvent(button, event);
        m_painting = false;
        QPainter painter(button);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(KColorScheme(QPalette::Active, KColorScheme::Selection).background().color());
        // Au bas de ce qui se voit du bouton : s'il dépasse de sa barre, ses dernières lignes sont
        // rognées, et le trait avec (c'est ce qui le cachait jusqu'au 23 septembre 2026).
        const int bottom = qMin(button->height(), button->visibleRegion().boundingRect().bottom() + 1);
        painter.drawRoundedRect(QRectF(4, bottom - 2, qMax(0, button->width() - 8), 2), 1, 1);
        return true;
    }

private:
    bool m_painting = false;
};
}

void KoraCutStyle::addCheckedAccent(QToolBar *toolbar)
{
    auto *watcher = new AccentWatcher(toolbar);
    toolbar->installEventFilter(watcher);
    watcher->watchAll(toolbar);
}
