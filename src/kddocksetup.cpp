/*
    SPDX-FileCopyrightText: 2025 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "kddocksetup.h"
#include "core.h"
#include "kdenlivesettings.h"

#include "koracut/koracutmetrics.h"
#include "koracut/koracutpanelfocus.h"

#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/TabBar.h>

#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QProxyStyle>
#include <QResizeEvent>
#include <QStyleFactory>
#include <QStyleOptionTab>
#include <QStylePainter>

namespace {

/** @brief L'icône d'un panneau, par son nom interne.
 *
 * KoraCut, 21 septembre 2026. À la largeur par défaut du dock gauche, les cinq onglets (Médias,
 * Compositions, Effets, Propriétés, Historique) ne tiennent pas : deux flèches de défilement
 * apparaissaient et « Propriétés » était coupé. Sous la largeur nécessaire, les onglets passent
 * donc en icône seule avec infobulle, comme les modules de la barre du haut le font déjà sous
 * 1280 px. Sans icône, un onglet compact serait une case vide : c'est cette table qui l'évite.
 */
QString iconePourPanneau(const QString &nom)
{
    static const QHash<QString, QString> table = {
        {QStringLiteral("project_bin"), QStringLiteral("folder-videos")},
        {QStringLiteral("transition_list"), QStringLiteral("composite-track-on")},
        {QStringLiteral("effect_list"), QStringLiteral("show-all-effects")},
        {QStringLiteral("effect_stack"), QStringLiteral("document-properties")},
        {QStringLiteral("undo_history"), QStringLiteral("edit-undo")},
        // 22 septembre 2026 : ces deux panneaux n'avaient pas d'icône, leur onglet compact
        // montrait un bout de libellé coupé (« ›ri », « ote »).
        {QStringLiteral("clip_properties"), QStringLiteral("dialog-information")},
        {QStringLiteral("notes_widget"), QStringLiteral("document-edit")},
        {QStringLiteral("clipmonitor"), QStringLiteral("camera-video")},
        {QStringLiteral("projectmonitor"), QStringLiteral("media-playback-start")},
        {QStringLiteral("library"), QStringLiteral("folder")},
        {QStringLiteral("textedit"), QStringLiteral("audio-input-microphone")},
        {QStringLiteral("timeremap"), QStringLiteral("speedometer")},
        {QStringLiteral("markers"), QStringLiteral("flag")},
        {QStringLiteral("subtitles"), QStringLiteral("add-subtitle")},
        {QStringLiteral("mixer"), QStringLiteral("audio-volume-high")},
        {QStringLiteral("media_browser"), QStringLiteral("file")},
        {QStringLiteral("onlineresources"), QStringLiteral("cloud-download")},
        {QStringLiteral("screengrab"), QStringLiteral("media-record")},
        {QStringLiteral("audiospectrum"), QStringLiteral("measure")},
        // 25 septembre 2026 : les panneaux KoraCut du bloc de droite. Sans icône, l'onglet compact
        // de « Agents » était une case vide.
        {QStringLiteral("agents"), QStringLiteral("dialog-messages")},
        {QStringLiteral("console"), QStringLiteral("system-run")},
        {QStringLiteral("terminal"), QStringLiteral("utilities-terminal")},
        {QStringLiteral("motion"), QStringLiteral("motion_path_animations")},
        {QStringLiteral("script"), QStringLiteral("text-plain")},
        // Chantier 10 : le panneau Diagnostic (jauge Lucide).
        {QStringLiteral("diagnostic"), QStringLiteral("speedometer")},
    };
    // Un chutier supplémentaire s'appelle « project_bin2 », « project_bin3 »…
    if (nom.startsWith(QLatin1String("project_bin"))) {
        return QStringLiteral("folder-videos");
    }
    return table.value(nom);
}

} // namespace

class KdenliveDockTabBar : public KDDockWidgets::QtWidgets::TabBar
{
public:
    explicit KdenliveDockTabBar(KDDockWidgets::Core::TabBar *controller, KDDockWidgets::Core::View *parent = nullptr)
        : KDDockWidgets::QtWidgets::TabBar(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent))
    {
        auto parentWidget = KDDockWidgets::QtCommon::View_qt::asQWidget(parent);
        setProperty("_breeze_force_frame", false);
        setDocumentMode(true);
        parentWidget->setProperty("_breeze_force_frame", false);
        setContextMenuPolicy(Qt::CustomContextMenu);
        // The constructor of KDDockWidgets::QtWidgets::TabBar makes a QProxyStyle
        // that ends up taking ownership of the style for the entire application!
        if (QProxyStyle *proxy_style = qobject_cast<QProxyStyle *>(style())) {
            proxy_style->baseStyle()->setParent(qApp);
            proxy_style->setBaseStyle(QStyleFactory::create(qApp->style()->name()));
        }
        setPalette(qApp->palette());
        // KoraCut : un onglet de panneau ne s'ampute pas. Il se raccourcit à la source (les
        // libellés sont courts depuis le 21/09/2026) ou il défile, jamais « Ef… ».
        // NN/g, Icon Usability : un libellé doit être lisible sans interaction.
        setElideMode(Qt::ElideNone);
        setUsesScrollButtons(true);
        setExpanding(false);

        connect(this, &QWidget::customContextMenuRequested, []() { Q_EMIT pCore.get()->switchTitleBars(); });
        // KoraCut : trois clics rapides sur un onglet agrandissent son panneau, trois de plus le remettent.
        KoraCutPanelFocus::watchTripleClick(this, [this](const QPoint &pos) {
            const int index = tabAt(pos);
            return index >= 0 ? tabBar()->dockWidgetAt(index) : tabBar()->currentDockWidget();
        });
        connect(this, &KDDockWidgets::QtWidgets::TabBar::countChanged, [&]() {
            if (!KdenliveSettings::showtitlebars()) {
                pCore->startHideBarsTimer();
            }
        });
    }

protected:
    /** @brief Largeur réelle du libellé plus 24 px, hauteur fixée au rythme de la charte.
     *
     * La `QTabBar` par défaut calcule sa largeur puis tronque quand la barre est trop étroite.
     * Ici on donne la largeur qu'il faut : avec `ElideNone` et les boutons de défilement, la barre
     * défile au lieu d'amputer. 30 px de haut, le rythme vertical d'un onglet de panneau.
     *
     * KoraCut, 21 septembre 2026 : en mode compact, l'onglet se réduit à son icône.
     */
    QSize tabSizeHint(int index) const override
    {
        QSize taille = KDDockWidgets::QtWidgets::TabBar::tabSizeHint(index);
        taille.setHeight(KoraCutMetrics::RowTab);
        if (m_mode == Mode::Icones) {
            taille.setWidth(KoraCutMetrics::IconDefault + 2 * KoraCutMetrics::SpaceZone);
            return taille;
        }
        taille.setWidth(largeurOnglet(index, m_mode));
        return taille;
    }

    /** Hors mode complet, on peint l'onglet nous-mêmes : même fond, sans l'icône ou sans le libellé. */
    void paintEvent(QPaintEvent *event) override
    {
        // On décide ici parce que c'est le seul moment où la largeur est sûrement la bonne :
        // à l'insertion d'un onglet la barre n'est pas encore dimensionnée, et le conteneur ne
        // renvoie pas toujours un évènement de redimensionnement ensuite. Le calcul est la somme
        // de cinq ou six largeurs, et il ne relance une mise en page que s'il change.
        majMode(largeurDisponible());
        if (m_mode == Mode::Complet) {
            KDDockWidgets::QtWidgets::TabBar::paintEvent(event);
            return;
        }
        QStylePainter peintre(this);
        for (int i = 0; i < count(); ++i) {
            QStyleOptionTab onglet;
            initStyleOption(&onglet, i);
            if (m_mode == Mode::Icones) {
                onglet.text.clear();
                onglet.iconSize = QSize(KoraCutMetrics::IconDefault, KoraCutMetrics::IconDefault);
            } else {
                onglet.icon = QIcon();
            }
            peintre.drawControl(QStyle::CE_TabBarTab, onglet);
        }
    }

    void resizeEvent(QResizeEvent *event) override
    {
        // La place du conteneur, jamais la largeur de la barre elle-même : voir largeurDisponible().
        // Avec event->size(), une barre passée en compact le restait pour toujours (vu le
        // 22/09/2026 après un changement de module ou un panneau agrandi puis remis en place).
        majMode(largeurDisponible());
        KDDockWidgets::QtWidgets::TabBar::resizeEvent(event);
    }

    void tabInserted(int index) override
    {
        KDDockWidgets::QtWidgets::TabBar::tabInserted(index);
        poseIconeEtInfobulle(index);
    }

private:
    /** La place réellement offerte à la barre d'onglets.
     *
     * On ne peut pas se fier à `width()` : en mode compact la barre demande moins de place, le
     * conteneur la lui donne, et la condition « ça ne tient pas » reste vraie pour toujours. On
     * mesure donc le conteneur, dont la largeur ne dépend pas du mode. Les 24 px retirés sont la
     * marge du conteneur et le bouton de fermeture quand il est là.
     */
    int largeurDisponible() const
    {
        const QWidget *conteneur = parentWidget();
        const int dispo = conteneur ? conteneur->width() - 24 : width();
        return qMax(dispo, 0);
    }

    /** Les trois façons d'écrire un onglet, de la plus large à la plus étroite. */
    enum class Mode { Complet, Texte, Icones };

    /** La largeur d'un onglet dans un mode donné, marges de la feuille de style comprises.
     *
     * KoraCut, 25 septembre 2026 : l'ancien calcul ne comptait que le libellé et 24 px. Il
     * oubliait l'icône posée à côté du nom et les 2 px de marge de chaque côté : la barre se
     * croyait assez large, gardait les noms, et « Propriétés » finissait coupé derrière des
     * flèches de défilement, même à 1440 px. On mesure maintenant avec la police de l'onglet
     * (la feuille de style l'écrit en 12 px, plus petit que celle de la barre : on prend la plus
     * grande des deux pour ne jamais sous-estimer).
     */
    int largeurOnglet(int index, Mode mode) const
    {
        QFont petite = font();
        petite.setPixelSize(12);
        const int texte = qMax(fontMetrics().horizontalAdvance(tabText(index)), QFontMetrics(petite).horizontalAdvance(tabText(index)));
        // 12 px de rembourrage et 2 px de marge de chaque côté (QTabBar::tab dans koracut.qss).
        int largeur = texte + 2 * KoraCutMetrics::SpaceZone + 4;
        if (mode == Mode::Complet && !tabIcon(index).isNull()) {
            largeur += KoraCutMetrics::IconDefault + 6;
        }
        return largeur;
    }

    int largeurTotale(Mode mode) const
    {
        int total = 0;
        for (int i = 0; i < count(); ++i) {
            total += largeurOnglet(i, mode);
        }
        return total;
    }

    /** Pose l'icône du panneau et l'infobulle qui rend son nom quand il n'est plus écrit. */
    void poseIconeEtInfobulle(int index)
    {
        if (index < 0 || index >= count()) {
            return;
        }
        QString nom;
        if (auto *controleur = tabBar()) {
            if (auto *panneau = controleur->dockWidgetAt(index)) {
                nom = panneau->uniqueName();
            }
        }
        const QString icone = iconePourPanneau(nom);
        if (!icone.isEmpty() && tabIcon(index).isNull()) {
            setTabIcon(index, QIcon::fromTheme(icone));
        }
        setTabToolTip(index, tabText(index));
    }

    /** Choisit le mode le plus lisible qui tient : icône et nom, nom seul, icône seule. */
    void majMode(int largeurDisponible)
    {
        Mode mode = Mode::Complet;
        if (count() > 1 && largeurDisponible > 0) {
            if (largeurTotale(Mode::Complet) > largeurDisponible) {
                mode = largeurTotale(Mode::Texte) > largeurDisponible ? Mode::Icones : Mode::Texte;
            }
        }
        if (mode == m_mode) {
            return;
        }
        m_mode = mode;
        for (int i = 0; i < count(); ++i) {
            poseIconeEtInfobulle(i);
        }
        // QTabBar garde en cache la largeur de chaque onglet : sans ce rappel, il peignait des
        // libellés entiers dans des cases de largeur compacte. setIconSize marque la mise en page
        // à refaire, même quand la taille ne change pas.
        setIconSize(iconSize());
        updateGeometry();
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
    }

    Mode m_mode = Mode::Complet;
};

class KdenliveDockGroup : public KDDockWidgets::QtWidgets::Group
{
public:
    explicit KdenliveDockGroup(KDDockWidgets::Core::Group *controller, KDDockWidgets::Core::View *parent = nullptr)
        : KDDockWidgets::QtWidgets::Group(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent))
    {
    }
    void paintEvent(QPaintEvent *) override {}
};

class KdenliveDockStack : public KDDockWidgets::QtWidgets::Stack
{
public:
    explicit KdenliveDockStack(KDDockWidgets::Core::Stack *controller, KDDockWidgets::Core::View *parent = nullptr)
        : KDDockWidgets::QtWidgets::Stack(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent))
    {
    }
    void paintEvent(QPaintEvent *) override {}
};

class KdenliveDockTitleBar : public KDDockWidgets::QtWidgets::TitleBar
{
public:
    explicit KdenliveDockTitleBar(KDDockWidgets::Core::TitleBar *controller, KDDockWidgets::Core::View *parent = nullptr)
        : KDDockWidgets::QtWidgets::TitleBar(controller, parent)
        , m_controller(controller)
    {
        KoraCutPanelFocus::watchTripleClick(this, [this](const QPoint &) {
            const auto docks = m_controller->dockWidgets();
            return docks.isEmpty() ? nullptr : docks.constFirst();
        });
        connect(pCore.get(), &Core::hideBars, this, [this](bool hide) {
            if (hide) {
#if defined(Q_OS_WIN)
                auto parentWidget = m_controller->view()->parentView();
                if (parentWidget && parentWidget->asFloatingWindowController() != nullptr) {
                    // Floating window, don't show as we already have the widget titlebar
                    return;
                }
#endif
            } else {
                if (m_controller->dockWidgets().size() > 1) {
                    // Don't show title bar when there are tabbed widgets
                    return;
                }
                auto parentWidget = m_controller->view()->parentView();
                if (parentWidget && parentWidget->asFloatingWindowController() != nullptr) {
                    // Floating window, don't show as we already have the widget titlebar
                    return;
                }
            }
            setVisible(!hide);
        });
    }

private:
    KDDockWidgets::Core::TitleBar *const m_controller;
};

class KdenliveDockSeparator : public KDDockWidgets::QtWidgets::Separator
{
public:
    explicit KdenliveDockSeparator(KDDockWidgets::Core::Separator *controller, KDDockWidgets::Core::View *parent)
        : KDDockWidgets::QtWidgets::Separator(controller, parent)
        , m_controller(controller)
    {
    }

    ~KdenliveDockSeparator() override;

    void enterEvent(KDDockWidgets::Qt5Qt6Compat::QEnterEvent *event) override
    {
        hovered = true;
        KDDockWidgets::QtWidgets::Separator::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        hovered = false;
        KDDockWidgets::QtWidgets::Separator::leaveEvent(event);
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QColor separatorColor = hovered ? palette().highlight().color() : palette().midlight().color();
        if (hovered) {
            separatorColor.setAlpha(128);
        }
        QPen pen(separatorColor);
        pen.setWidth(2);
        if (m_controller->isVertical()) {
            // Vertical rect
            p.fillRect(QWidget::rect(), palette().window());
            p.setPen(pen);
            p.drawLine(QWidget::rect().x(), QWidget::rect().y() + QWidget::rect().height() / 2, QWidget::rect().right(),
                       QWidget::rect().y() + QWidget::rect().height() / 2);
        } else {
            // Horizontal rect
            p.fillRect(QWidget::rect(), palette().window());
            p.setPen(pen);
            p.drawLine(QWidget::rect().x() + QWidget::rect().width() / 2, QWidget::rect().top(), QWidget::rect().x() + QWidget::rect().width() / 2,
                       QWidget::rect().bottom());
        }
    }

private:
    KDDockWidgets::Core::Separator *const m_controller;
    bool hovered{false};
};

KdenliveDockSeparator::~KdenliveDockSeparator() = default;

KDDockWidgets::Core::View *CustomWidgetFactory::createTitleBar(KDDockWidgets::Core::TitleBar *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockTitleBar(controller, parent);
}

KDDockWidgets::Core::View *CustomWidgetFactory::createGroup(KDDockWidgets::Core::Group *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockGroup(controller, parent);
}

KDDockWidgets::Core::View *CustomWidgetFactory::createStack(KDDockWidgets::Core::Stack *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockStack(controller, parent);
}

KDDockWidgets::Core::View *CustomWidgetFactory::createSeparator(KDDockWidgets::Core::Separator *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockSeparator(controller, parent);
}

KDDockWidgets::Core::View *CustomWidgetFactory::createTabBar(KDDockWidgets::Core::TabBar *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockTabBar(controller, parent);
}
