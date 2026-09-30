/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QPointer>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QWidget>

#include <functional>

class QAbstractButton;
class QCheckBox;
class QDialogButtonBox;
class QFormLayout;
class QLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QScrollArea;
class QVBoxLayout;
class KPageDialog;
class KPageWidget;
class KPageWidgetItem;

/** @file koracutpreferences.h
 * @brief La fenêtre Préférences de KoraCut (25 septembre 2026, document 11, journal).
 *
 * Kdenlive montrait une colonne d'icônes géantes et des formulaires à libellés alignés à droite.
 * On garde son dialogue (KConfigDialog : la liaison automatique des champs `kcfg_*` avec
 * `kdenlivesettings.kcfg`), mais on remplace ce qui se voit :
 * - une barre latérale étroite, des groupes titrés, une ligne par section (icône 16 px + libellé) ;
 * - une recherche qui filtre par réglage, pas par page, et met en évidence ce qu'elle trouve ;
 * - des pages : un titre, une phrase d'aide, des cartes de réglages, libellé à gauche et contrôle à droite ;
 * - l'enregistrement immédiat, un bouton Fermer, « Rétablir » par page.
 *
 * Pour les IA (porte de commande) : chaque section a un identifiant (`apparence`, `timeline`…),
 * chaque contrôle un nom stable `prefs.<section>.<réglage>` (propriété `koracutId`, ou objectName
 * quand ce n'est pas un champ `kcfg_`, dont le nom doit rester celui du réglage), et le dialogue
 * expose `koracutSections`, `koracutSection`, `koraCutShowSection()` et `koraCutSearch()`.
 */

/** @brief Les couleurs de la fenêtre, lues dans le thème à l'écran (jetons de KoraCutThemes). */
namespace KoraCutPrefsColors
{
QColor token(const char *name, int alpha = -1);
/** @brief La feuille des éléments à cartes (titres, aides, lignes, contrôle segmenté), tirée du thème. */
QString styleSheet();
/** @brief Habille une fenêtre à cartes : fond « fond » peint, feuille des cartes, suivi des changements
 * de thème. Pour les fenêtres KoraCut qui reprennent le langage des Préférences (baisse de la musique…). */
void applyCardWindow(QWidget *window);
}

/** @brief Une ligne de réglage, dans une carte. */
class KoraCutPrefsRow : public QWidget
{
    Q_OBJECT
public:
    enum class Kind {
        Inline, // libellé à gauche, contrôle à droite
        Stacked, // libellé en haut, contrôle sur toute la largeur (chemins, listes)
        Block, // un bloc sans libellé (une liste, un éditeur)
    };
    KoraCutPrefsRow(const QString &label, const QString &help, QWidget *control, Kind kind, QWidget *parent = nullptr);

    QWidget *control() const { return m_control; }
    /** @brief Le texte où cherche la recherche : libellé, aide, textes du contrôle, mots-clés. */
    QString searchText() const;
    void addKeywords(const QString &words) { m_keywords += QLatin1Char(' ') + words; }
    void setMatched(bool matched);
    bool isMatched() const { return m_matched; }
    void setFirst(bool first);
    QString label() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLabel *m_label{nullptr};
    QLabel *m_help{nullptr};
    QPointer<QWidget> m_control;
    QPointer<QCheckBox> m_toggle;
    QString m_keywords;
    Kind m_kind;
    bool m_first{true};
    bool m_matched{false};
    friend class KoraCutPrefsCard;
};

/** @brief Une carte : un titre discret au-dessus, des lignes séparées par un filet. */
class KoraCutPrefsCard : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutPrefsCard(const QString &title, QWidget *parent = nullptr);
    /** @brief Libellé à gauche, contrôle à droite. @p key : le nom stable pour les IA. */
    KoraCutPrefsRow *addRow(const QString &label, QWidget *control, const QString &help = QString(), const QString &key = QString());
    /** @brief Une case à cocher devient un interrupteur à droite, son texte passe à gauche. */
    KoraCutPrefsRow *addToggle(QCheckBox *box, const QString &help = QString(), const QString &key = QString(), const QString &label = QString());
    /** @brief Libellé en haut, contrôle sur toute la largeur. */
    KoraCutPrefsRow *addStacked(const QString &label, QWidget *control, const QString &help = QString(), const QString &key = QString());
    /** @brief Un bloc sur toute la largeur, sans libellé. */
    KoraCutPrefsRow *addBlock(QWidget *widget, const QString &key = QString());
    /** @brief Reprend les lignes d'un formulaire de Kdenlive telles quelles (libellé, champ), dans l'ordre. */
    void addForm(QFormLayout *form);
    /** @brief Plusieurs contrôles côte à côte, dans un seul conteneur (à passer à addRow). */
    static QWidget *line(const QList<QWidget *> &widgets, QWidget *parent = nullptr);
    QList<KoraCutPrefsRow *> rows() const { return m_rows; }
    QString title() const;
    void setSection(const QString &section) { m_section = section; }
    /** @brief Préfixe des noms stables hors Préférences (« baisse. » donne « baisse.<réglage> »). */
    void setIdPrefix(const QString &prefix) { m_prefix = prefix; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    KoraCutPrefsRow *add(KoraCutPrefsRow *row, const QString &key);
    void refreshFirst();
    QLabel *m_title{nullptr};
    QWidget *m_body{nullptr};
    QVBoxLayout *m_rowsLayout{nullptr};
    QList<KoraCutPrefsRow *> m_rows;
    QString m_section;
    QString m_prefix;
};

/** @brief Une page de réglages : titre, aide, « Rétablir », puis des cartes. */
class KoraCutPrefsPage : public QWidget
{
    Q_OBJECT
public:
    /** @p scroll faux : la page tient dans la fenêtre et son dernier bloc prend la hauteur (Connexions IA). */
    KoraCutPrefsPage(const QString &id, const QString &title, const QString &help, bool scroll = true, QWidget *parent = nullptr);
    QString id() const { return m_id; }
    QString title() const { return m_titleText; }
    KoraCutPrefsCard *addCard(const QString &title = QString());
    /** @brief Un bloc qui n'est pas une carte (une page de Kdenlive gardée entière). */
    void addWidget(QWidget *widget, int stretch = 0);
    /** @brief Affiche « Rétablir les valeurs par défaut » en haut de la page. */
    void setRestoreHandler(const std::function<void()> &handler);
    QList<KoraCutPrefsRow *> rows() const;
    /** @brief Fait défiler jusqu'à une ligne. */
    void reveal(QWidget *row);
    /** @brief Des mots de recherche en plus (les deux langues, les noms techniques) : proxy, ffmpeg, whisper… */
    void setKeywords(const QString &words) { m_keywords = words; }
    QString keywords() const { return m_keywords; }
    /** @brief Les textes des blocs qui ne sont pas des lignes (pages de Kdenlive gardées entières). */
    QList<QWidget *> blocks() const { return m_blocks; }

private:
    QString m_id;
    QString m_titleText;
    QScrollArea *m_scroll{nullptr};
    QVBoxLayout *m_content{nullptr};
    QList<KoraCutPrefsCard *> m_cards;
    QList<QWidget *> m_blocks;
    QAbstractButton *m_restore{nullptr};
    std::function<void()> m_restoreHandler;
    bool m_scrolls{true};
    QString m_keywords;
};

/** @brief Le dessin de la barre latérale : titres de groupe, sections, nombre de résultats. */
class KoraCutPrefsNavDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

/** @brief La coque : barre latérale, recherche, pied de fenêtre, enregistrement immédiat. */
class KoraCutPrefsShell : public QObject
{
    Q_OBJECT
public:
    KoraCutPrefsShell(KPageDialog *dialog, KPageWidget *pages, QDialogButtonBox *buttons);

    void addGroup(const QString &id, const QString &title);
    /** @brief Une section de la barre latérale, dans le dernier groupe ajouté. */
    void addSection(KoraCutPrefsPage *page, const QString &icon, KPageWidgetItem *item);
    /** @brief Pose la barre latérale et le pied ; à appeler après la dernière section. */
    void finish();

    bool showSection(const QString &id);
    QString currentSection() const;
    QStringList sectionIds() const;
    KoraCutPrefsPage *page(const QString &id) const;
    /** @brief Filtre les réglages ; rend le nombre de lignes trouvées. */
    int search(const QString &text);
    QLineEdit *searchField() const { return m_search; }

    /** @brief Enregistrement immédiat : @p apply est appelé un court instant après un changement. */
    void setAutoApply(const std::function<bool()> &pending, const std::function<void()> &apply);
    /** @brief Applique tout de suite ce qui attend (à la fermeture). */
    void flush();
    /** @brief Réécrit les couleurs de la fenêtre depuis le thème à l'écran. */
    void restyle();
    /** @brief Un réglage a changé : il sera enregistré un court instant après. */
    void scheduleApply();

    /** @brief Simplifie un texte pour comparer : minuscules, sans accents, sans « & ». */
    static QString fold(const QString &text);

Q_SIGNALS:
    void sectionChanged(const QString &id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Section {
        QString id;
        QString group;
        KoraCutPrefsPage *page{nullptr};
        KPageWidgetItem *item{nullptr};
        QListWidgetItem *nav{nullptr};
    };
    void syncFromPages();
    void applyNow();
    void showSaved();
    QList<QWidget *> matchesIn(KoraCutPrefsPage *page, const QStringList &words, bool mark);

    QPointer<KPageDialog> m_dialog;
    QPointer<KPageWidget> m_pages;
    QPointer<QDialogButtonBox> m_buttons;
    QWidget *m_sidebar{nullptr};
    QLineEdit *m_search{nullptr};
    QListWidget *m_nav{nullptr};
    QLabel *m_empty{nullptr};
    QLabel *m_note{nullptr};
    QList<Section> m_sections;
    QList<QPair<QString, QString>> m_groups;
    QString m_currentGroup;
    QTimer m_applyTimer;
    QTimer m_noteTimer;
    std::function<bool()> m_pending;
    std::function<void()> m_apply;
    bool m_syncing{false};
    bool m_armed{false};
    bool m_dirty{false};
    QString m_lastSearch;
};
