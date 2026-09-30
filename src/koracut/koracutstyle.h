/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QIcon>
#include <QProxyStyle>

class QToolBar;

/** @brief Le style KoraCut : ce que la feuille de style Qt ne sait pas faire.
 *
 * Trois choses échappent à `koracut.qss`, et ce ne sont pas les moins visibles.
 *
 * 1. **L'anneau de focus.** La réponse officielle du forum Qt est sans ambiguïté : « there is no
 *    way of getting rid of the focus rect through the style sheet. However, you can implement your
 *    own QStyle (or use QProxyStyle) ». On intercepte donc `PE_FrameFocusRect` pour poser l'anneau
 *    de 2 px en accent que demande WCAG 2.4.13 (périmètre 2 px, contraste 3:1).
 *
 * 2. **La sélection arrondie d'une ligne de liste.** `border-radius` sur `::item` n'est pas fiable :
 *    le fond de sélection est peint par le style, pas par la boîte CSS. On intercepte
 *    `PE_PanelItemViewItem`. Le cas des arbres à plusieurs colonnes est traité par
 *    `QStyleOptionViewItem::viewItemPosition` : on n'arrondit que les extrémités de la ligne,
 *    sinon une ligne à trois colonnes donne trois pastilles côte à côte.
 *
 * 3. **Les guides d'arborescence.** Le tiret et le pointillé vertical devant chaque chevron sont la
 *    décoration de branche par défaut de Qt, c'est-à-dire l'explorateur Windows de 2001. On
 *    intercepte `PE_IndicatorBranch` pour ne garder que le chevron, dessiné au trait, comme les
 *    icônes de la charte.
 *
 * 4. **Le chevron d'un bouton à menu.** Qt le dessine dans le coin bas-droit du bouton, donc
 *    par-dessus l'icône, qui occupe déjà tout. On réserve une gouttière à droite
 *    (`PM_MenuButtonIndicator` pour les boutons à deux zones, une règle de padding dans
 *    `koracut.qss` pour les autres) et on dessine notre propre chevron en interceptant
 *    `PE_IndicatorArrowDown`, plus un filet de séparation quand le bouton a deux zones
 *    cliquables. C'est le seul crochet qui tienne : dès que la feuille de style donne une boîte
 *    à `QToolButton`, elle peint l'étiquette elle-même sans repasser par nous, mais elle nous
 *    redonne toujours la main pour la flèche.
 *
 * Posé dans `src/main.cpp` avant la feuille de style : Qt enveloppe alors ce style dans son
 * `QStyleSheetStyle`, qui lui délègue tout ce que le CSS ne couvre pas.
 */
class KoraCutStyle : public QProxyStyle
{
    Q_OBJECT
public:
    /** @brief Trait d'accent de 2 px sous chaque bouton coché de @p toolbar, y compris ceux ajoutés plus tard.
     *
     * Pour la barre d'état, où le bouton ne fait que 15 px de haut : la feuille de style n'y peint
     * pas de bordure partielle, et QStyleSheetStyle ne repasse pas par ce style pour ces boutons.
     * Le trait est un petit cadre enfant (`koracutAccentLine`), coloré par la feuille de style, ce
     * qui laisse les thèmes le recolorer. 23 septembre 2026, « 11 - Refonte UI-UX » 3.8. */
    static void addCheckedAccent(QToolBar *toolbar);
    explicit KoraCutStyle(QStyle *baseStyle = nullptr);

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &contentsSize, const QWidget *widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override;
    QIcon standardIcon(StandardPixmap standardIcon, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override;
    /** @brief 25 septembre 2026 : les boutons de fenêtre sans icône, les formulaires à libellés à gauche
     * (document 11, § 3.10 ; « la grammaire KDE de 2008 » que le diagnostic reprochait à toutes les fenêtres). */
    void polish(QWidget *widget) override;
    using QProxyStyle::polish;
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr, const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
    int layoutSpacing(QSizePolicy::ControlType control1, QSizePolicy::ControlType control2, Qt::Orientation orientation,
                      const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override;

    /** @brief Ce qu'un bouton fait quand on clique dessus. */
    enum class MenuKind {
        None,  /**< pas de menu : rien à réserver */
        Whole, /**< tout le bouton ouvre le menu (InstantPopup, DelayedPopup) */
        Split, /**< l'icône agit, le chevron ouvre le menu (MenuButtonPopup) */
    };
    /** @brief Dit si ce bouton porte un menu, et de quelle façon. Se lit sur le widget et non sur
     *  l'option : la feuille de style efface `HasMenu` avant de nous appeler. */
    static MenuKind menuKind(const QWidget *widget);

private:
    /** Peint le chevron d'un bouton à menu dans sa gouttière, et le filet s'il y a deux zones. */
    bool drawToolButtonArrow(const QStyleOption *option, QPainter *painter, const QWidget *widget) const;
    /** Peint le fond d'une ligne de liste : sélection, survol, et rien d'autre. */
    bool drawItemViewPanel(const QStyleOption *option, QPainter *painter, const QWidget *widget) const;
    /** Peint la branche d'un arbre : le chevron seul, sans guide pointillé. */
    bool drawBranch(const QStyleOption *option, QPainter *painter) const;
};
