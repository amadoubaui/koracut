/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// Plusieurs séquences visibles à la fois dans le panneau Timeline (document 13, § 5, second
// temps, 24 septembre 2026). Tout ce qui ne connaît pas la timeline est ici : la découpe de la
// zone en vues (des QSplitter imbriqués), les onglets à la Chrome, le voile d'une vue inactive
// et le repère de dépôt pendant un glisser. La logique des séquences reste dans TimelineTabs.

#include <QAbstractButton>
#include <QJsonObject>
#include <QPointer>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

#include <functional>

class QLineEdit;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace KoraCutSplit {

/** Où poser une vue par rapport à une autre. Center : dans ses onglets. */
enum class Side { Center, Left, Right, Top, Bottom };

/** « gauche », « droite », « haut », « bas », « onglet » (et left, right, top, bottom, tab). */
Side sideFromName(const QString &name, bool *ok = nullptr);
QString sideName(Side side);
/** La zone de dépôt sous le point : la bande des onglets (header px) vaut Center, puis le bord le
 *  plus proche s'il est à moins de 30 % de la vue, sinon Center. */
Side sideAt(const QRect &area, const QPoint &point, int header);
/** La moitié de la vue qu'une vue posée de ce côté occuperait (toute la vue pour Center). */
QRect zoneRect(const QRect &area, Side side);

} // namespace KoraCutSplit

/** @brief La zone qui porte les vues : un seul widget au départ, des QSplitter quand on découpe. */
class KoraCutTimelineSplitHost : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutTimelineSplitHost(QWidget *parent = nullptr);
    void setRoot(QWidget *root);
    QWidget *root() const { return m_root; }
    /** Pose moving à côté de target (nul : de toute la zone). Center ne fait rien. */
    void place(QWidget *moving, QWidget *target, KoraCutSplit::Side side);
    /** Retire la vue de la découpe et referme les séparateurs devenus inutiles. Le widget reste
     *  enfant de la zone, caché : à l'appelant de le détruire. */
    void remove(QWidget *view);
    /** La découpe en texte, pour les tests et la porte : « H[a,V[b,c]] » (noms d'objet). */
    QString describe() const;
    /** La découpe en JSON léger, pour l'enregistrer dans le projet :
     *  { "sens": "h"|"v", "vues": [...], "tailles": [0.5, 0.5] } pour un séparateur, et ce que rend
     *  leaf pour une vue. */
    QJsonObject serialize(const std::function<QJsonObject(QWidget *)> &leaf) const;
    /** Repose une découpe enregistrée. La zone ne doit pas être découpée ; leaf rend le widget de
     *  chaque vue (nul : vue ignorée), et doit rendre la vue principale actuelle pour l'une d'elles. */
    bool restore(const QJsonObject &node, const std::function<QWidget *(const QJsonObject &)> &leaf);
    /** Le repère de dépôt, par-dessus tout le reste. */
    void showDropZone(const QRect &zone, const QString &label, const QPoint &cursor, bool valid);
    void hideDropZone();

private:
    QSplitter *newSplitter(Qt::Orientation orientation);
    QWidget *build(const QJsonObject &node, const std::function<QWidget *(const QJsonObject &)> &leaf);
    static void equalize(QSplitter *splitter);
    QVBoxLayout *m_layout;
    QWidget *m_root{nullptr};
    class DropOverlay;
    DropOverlay *m_overlay;
};

/** @brief La barre des onglets de séquences (27 septembre 2026, demande d'Amadou Ba).
 *
 *  Pas de trait d'accent : l'onglet choisi se lit par sa surface, celle de la timeline qu'il
 *  prolonge (comme un onglet de navigateur), et par son texte plein. Les autres restent sur le fond,
 *  en texte secondaire, séparés par un trait court qui s'efface à côté de l'onglet choisi ou survolé.
 *  La feuille de style dessine les onglets ; cette classe borne leur largeur et pose les séparateurs. */
class KoraCutSequenceTabBar : public QTabBar
{
    Q_OBJECT
public:
    explicit KoraCutSequenceTabBar(QWidget *parent = nullptr);
    /** Largeurs d'un onglet : naturelle bornée, et plus petite quand la place manque. */
    static constexpr int kMaxWidth = 220;
    static constexpr int kMinWidth = 120;
    static constexpr int kShrunkWidth = 76;
    /** Largeur de tous les onglets à leur taille naturelle, et réduits au plus. */
    int naturalExtent() const;
    int shrunkExtent() const;

protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int m_hovered{-1};
};

/** @brief La croix d'un onglet, dessinée au trait (1,5 px), visible au survol et sur l'onglet choisi. */
class KoraCutTabCloseButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit KoraCutTabCloseButton(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_hover{false};
};

/** @brief Donne à la barre d'un QTabWidget ses gestes à la Chrome : croix au survol, « + » juste
 *  après le dernier onglet, renommage sur place, et glisser un onglet hors de la barre. */
class KoraCutTabBarDriver : public QObject
{
    Q_OBJECT
public:
    KoraCutTabBarDriver(QTabWidget *tabs, bool withAddButton);
    QTabWidget *tabs() const { return m_tabs; }
    QToolButton *addButton() const { return m_add; }
    void setClosable(bool closable);
    void setCloseToolTip(const QString &tip);
    /** Le nombre de séquences ouvertes dans toutes les vues, pour le compteur (0 : celles de la barre). */
    void setTotal(int total) { m_total = total; }
    /** Le panneau qui pilote le moniteur : son onglet choisi garde le texte plein, celui d'une vue
     *  inactive passe en texte secondaire (plus de trait d'accent depuis le 27 septembre 2026). */
    void setActivePane(bool active);
    /** À appeler après un ajout ou un retrait d'onglet. */
    void refresh();
    void startRename(int index);
    /** Autorise un onglet à sortir de sa barre (glisser vers une autre vue). */
    void setDragOutAllowed(std::function<bool(QWidget *page)> allowed) { m_dragOutAllowed = std::move(allowed); }

Q_SIGNALS:
    void closeRequested(QWidget *page);
    void renamed(QWidget *page, const QString &name);
    void addRequested();
    void dragMoved(QWidget *page, const QPoint &globalPos);
    void dropped(QWidget *page, const QPoint &globalPos);
    void dragCancelled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateButtons();
    void placeAddButton();
    QTabWidget *m_tabs;
    QToolButton *m_add{nullptr};
    QWidget *m_strip{nullptr};
    class QLabel *m_count{nullptr};
    bool m_closable{false};
    int m_total{0};
    QString m_closeTip;
    int m_hovered{-1};
    QPointer<QWidget> m_pressPage;
    QPoint m_pressPos;
    bool m_dragging{false};
    bool m_dragCancelled{false};
    QPointer<QLineEdit> m_editor;
    std::function<bool(QWidget *page)> m_dragOutAllowed;
};

/** @brief Le voile posé sur la séquence affichée d'une vue qui ne pilote pas le moniteur : un
 *  clic la rend active. La vue elle-même est désactivée (setEnabled(false)) comme un onglet caché,
 *  le voile doit donc vivre à côté d'elle, dans le QTabWidget, et suivre sa géométrie. */
class KoraCutTimelineVeil : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutTimelineVeil(QTabWidget *pane);
    void setActive(bool shown);

Q_SIGNALS:
    void clicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void follow();
    QTabWidget *m_pane;
    QStackedWidget *m_stack{nullptr};
    bool m_wanted{false};
};

/** @brief Une vue secondaire : les mêmes onglets que la vue principale, sans bouton « + ». */
class KoraCutTimelinePane : public QTabWidget
{
    Q_OBJECT
public:
    explicit KoraCutTimelinePane(QWidget *parent = nullptr);
};
