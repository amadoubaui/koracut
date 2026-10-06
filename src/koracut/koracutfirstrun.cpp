/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutfirstrun.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "koracuticons.h"
#include "koracutnewprojectdialog.h"
#include "koracutvideotypes.h"
#include "mainwindow.h"
#include "porte/koracutporte.h"
#include "project/projectmanager.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "utils/uiutils.h"

#include <KColorScheme>
#include <KLocalizedString>
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace KoraCutFirstRun;

namespace {

QColor accentColor()
{
    return KColorScheme(QPalette::Active, KColorScheme::Selection).background().color();
}

/** @brief Le libellé sans le « & » que KDE ajoute aux boutons pour les raccourcis au clavier. */
QString plain(const QAbstractButton *button)
{
    return KLocalizedString::removeAcceleratorMarker(button->text());
}

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

/** @brief Un interrupteur : la piste, le bouton, le texte à droite. Espace ou clic bascule. */
class Switch : public QAbstractButton
{
public:
    explicit Switch(const QString &label, QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setText(label);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        return {kInset + kTrackWidth + kGap + fm.horizontalAdvance(plain(this)) + 4, qMax(kTrackHeight + 8, fm.height() + 6)};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(QPalette::WindowText);
        const QColor accent = accentColor();
        const QRectF track(kInset, (height() - kTrackHeight) / 2.0, kTrackWidth, kTrackHeight);
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? accent : withAlpha(ink, underMouse() ? 0.36 : 0.28));
        p.drawRoundedRect(track, kTrackHeight / 2.0, kTrackHeight / 2.0);
        const qreal knob = kTrackHeight - 4;
        const qreal x = isChecked() ? track.right() - knob - 2 : track.left() + 2;
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(x, track.top() + 2, knob, knob));
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(withAlpha(accent, 0.7), 1.5));
            p.drawRoundedRect(track.adjusted(-2.25, -2.25, 2.25, 2.25), kTrackHeight / 2.0 + 2.25, kTrackHeight / 2.0 + 2.25);
        }
        p.setPen(ink);
        const int left = kInset + kTrackWidth + kGap;
        p.drawText(QRect(left, 0, width() - left, height()), Qt::AlignLeft | Qt::AlignVCenter, plain(this));
    }
    void enterEvent(QEnterEvent *event) override
    {
        QAbstractButton::enterEvent(event);
        update();
    }
    void leaveEvent(QEvent *event) override
    {
        QAbstractButton::leaveEvent(event);
        update();
    }

private:
    static constexpr int kInset = 3; // la place de l'anneau de focus
    static constexpr int kTrackWidth = 30;
    static constexpr int kTrackHeight = 18;
    static constexpr int kGap = 10;
};

/** @brief Une grande carte de format : le dessin de l'écran, le nom, les usages. */
class FormatCard : public QAbstractButton
{
public:
    FormatCard(bool vertical, const QString &title, const QString &uses, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_vertical(vertical)
        , m_uses(uses)
    {
        setText(title);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleDescription(uses);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override { return {264, 172}; }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(QPalette::WindowText);
        const QColor accent = accentColor();
        const bool on = isChecked();
        const QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);
        // Le fond : un voile de la couleur du texte, lisible en clair comme en sombre.
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(ink, on ? 0.09 : (underMouse() ? 0.07 : 0.04)));
        p.drawRoundedRect(card, 9, 9);
        p.setBrush(Qt::NoBrush);
        if (on) {
            p.setPen(QPen(accent, 2));
            p.drawRoundedRect(card, 9, 9);
        } else {
            p.setPen(QPen(withAlpha(ink, hasFocus() ? 0.5 : 0.14), 1));
            p.drawRoundedRect(card.adjusted(-0.5, -0.5, 0.5, 0.5), 9, 9);
        }
        if (hasFocus() && on) {
            p.setPen(QPen(withAlpha(accent, 0.45), 1));
            p.drawRoundedRect(card.adjusted(3, 3, -3, -3), 6.5, 6.5);
        }
        // Le dessin : un téléphone debout ou un écran couché, à plat, sans dégradé.
        const QRectF art(card.left(), card.top() + 14, card.width(), 92);
        const QColor line = on ? accent : withAlpha(ink, 0.6);
        const QColor fill = on ? withAlpha(accent, 0.2) : withAlpha(ink, 0.08);
        QRectF screen;
        if (m_vertical) {
            const QSizeF body(50, 88);
            const QRectF phone(art.center().x() - body.width() / 2, art.center().y() - body.height() / 2, body.width(), body.height());
            p.setPen(QPen(line, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(phone, 9, 9);
            screen = phone.adjusted(4.5, 4.5, -4.5, -4.5);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(screen, 5, 5);
            // L'encoche du téléphone.
            p.setBrush(line);
            p.drawRoundedRect(QRectF(phone.center().x() - 7, phone.top() + 7.5, 14, 2.5), 1.25, 1.25);
        } else {
            const QSizeF body(136, 76.5);
            const QRectF tv(art.center().x() - body.width() / 2, art.top() + 1, body.width(), body.height());
            p.setPen(QPen(line, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(tv, 7, 7);
            screen = tv.adjusted(4.5, 4.5, -4.5, -4.5);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(screen, 3.5, 3.5);
            // Le pied de l'écran.
            p.setPen(QPen(line, 1.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(tv.center().x(), tv.bottom() + 1), QPointF(tv.center().x(), tv.bottom() + 9));
            p.drawLine(QPointF(tv.center().x() - 20, tv.bottom() + 9.5), QPointF(tv.center().x() + 20, tv.bottom() + 9.5));
        }
        // Le triangle de lecture, au milieu de l'image.
        QPainterPath play;
        const QPointF c = screen.center();
        play.moveTo(c.x() - 5, c.y() - 7);
        play.lineTo(c.x() + 7, c.y());
        play.lineTo(c.x() - 5, c.y() + 7);
        play.closeSubpath();
        p.setPen(QPen(line, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(on ? line : QColor(Qt::transparent));
        p.drawPath(play);
        // La coche de la carte choisie, en haut à droite.
        if (on) {
            const QRectF badge(card.right() - 28, card.top() + 10, 18, 18);
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawEllipse(badge);
            QPainterPath check;
            check.moveTo(badge.left() + 5, badge.center().y() + 0.5);
            check.lineTo(badge.left() + 8, badge.center().y() + 3.5);
            check.lineTo(badge.right() - 4.5, badge.center().y() - 3);
            p.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(check);
        }
        // Le nom, puis les usages en plus petit.
        QFont title = font();
        title.setPixelSize(14);
        title.setWeight(QFont::DemiBold);
        p.setFont(title);
        p.setPen(ink);
        const QRectF titleRect(card.left() + 12, art.bottom() + 8, card.width() - 24, QFontMetrics(title).height());
        p.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, plain(this));
        QFont small = font();
        small.setPixelSize(11);
        p.setFont(small);
        p.setPen(withAlpha(ink, 0.66));
        const QRectF usesRect(titleRect.left(), titleRect.bottom() + 3, titleRect.width(), QFontMetrics(small).height());
        p.drawText(usesRect, Qt::AlignHCenter | Qt::AlignVCenter, QFontMetrics(small).elidedText(m_uses, Qt::ElideRight, int(usesRect.width())));
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        // Double-clic : on choisit la carte et on passe tout de suite au type de vidéo.
        QAbstractButton::mouseDoubleClickEvent(event);
        setChecked(true);
        if (auto *dialog = qobject_cast<KoraCutFirstRunDialog *>(window())) {
            QMetaObject::invokeMethod(dialog, [dialog]() { dialog->play(QStringLiteral("bouton:type")); }, Qt::QueuedConnection);
        }
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        // Entrée sur une carte : on la choisit, puis la fenêtre valide (son bouton principal).
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            setChecked(true);
            event->ignore();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }
    void enterEvent(QEnterEvent *event) override
    {
        QAbstractButton::enterEvent(event);
        update();
    }
    void leaveEvent(QEvent *event) override
    {
        QAbstractButton::leaveEvent(event);
        update();
    }

private:
    bool m_vertical;
    QString m_uses;
};

/** @brief Un projet vide dans ce sens : dix pistes et dix pistes sans nom, la cadence du pays. */
KoraCutProjectPlan emptyPlan(const QString &format)
{
    KoraCutProjectPlan plan;
    plan.frame = KoraCutVideoTypes::frameSize(format);
    // 25 i/s là où le courant est à 50 Hz ; ailleurs 29,97 à l'horizontale et 30 à la verticale.
    const bool pal = ProjectManager::getDefaultProjectFormat().endsWith(QLatin1String("_25"));
    plan.setRate(pal ? 25.0 : (format == QLatin1String("vertical") ? 30.0 : 29.97));
    for (int i = 0; i < KdenliveSettings::videotracks(); ++i) {
        plan.videoTracks << QString();
    }
    for (int i = 0; i < KdenliveSettings::audiotracks(); ++i) {
        plan.audioTracks << QString();
    }
    return plan;
}

/** @brief Le projet ouvert est-il encore le projet vide du lancement, jamais touché ? */
bool untouchedBlankProject()
{
    KdenliveDoc *doc = pCore->currentDoc();
    return doc && !doc->isModified() && doc->url().isEmpty() && doc->getDocumentProperty(QStringLiteral("koracut:videotype")).isEmpty();
}

void logState(const QString &step)
{
    QString tracks = QStringLiteral("?");
    if (auto timeline = pCore->projectManager()->getTimeline()) {
        tracks = QStringLiteral("%1/%2").arg(timeline->getTracksIds(false).size()).arg(timeline->getTracksIds(true).size());
    }
    const QSize frame = pCore->getCurrentFrameSize();
    qWarning().noquote() << "KORACUT-ACCUEIL" << step << QStringLiteral("format_prefere=%1").arg(KoraCutVideoTypes::preferredFormat())
                         << QStringLiteral("profil_defaut=%1").arg(KdenliveSettings::default_profile())
                         << QStringLiteral("image=%1x%2").arg(frame.width()).arg(frame.height()) << QStringLiteral("ips=%1").arg(pCore->getCurrentFps())
                         << QStringLiteral("pistes=%1").arg(tracks) << QStringLiteral("theme=%1").arg(KdenliveSettings::koracut_theme_mode())
                         << QStringLiteral("recents=%1").arg(KdenliveSettings::showWelcome());
}

void apply(MainWindow *window, const Choice &choice)
{
    if (choice.outcome == Outcome::Dismissed) {
        qWarning().noquote() << "KORACUT-ACCUEIL retiree_par_la_porte";
        return;
    }
    ProjectManager *manager = pCore->projectManager();
    KoraCutProjectPlan plan;
    if (!choice.format.isEmpty()) {
        KoraCutVideoTypes::setPreferredFormat(choice.format);
        plan = emptyPlan(choice.format);
        const QString profile = ProjectManager::koraCutProfileFor(plan);
        if (!profile.isEmpty()) {
            KdenliveSettings::setDefault_profile(profile);
        }
    }
    KdenliveSettings::setShowWelcome(choice.showRecents);
    KdenliveSettings::self()->save();
    if (choice.terminal) {
        QMetaObject::invokeMethod(window, "koraCutRevealTerminalDock", Qt::QueuedConnection);
    }
    // Le projet vide ouvert au lancement est horizontal : on le refait dans le sens choisi, tant
    // que personne n'y a touché.
    const auto matchFormat = [manager, &choice, &plan]() {
        if (!choice.format.isEmpty() && untouchedBlankProject() && pCore->getCurrentFrameSize() != plan.frame) {
            manager->newFileFromPlan(plan);
        }
    };
    switch (choice.outcome) {
    case Outcome::OpenProject:
        QMetaObject::invokeMethod(manager, "slotOpenFile", Qt::QueuedConnection);
        break;
    case Outcome::ChooseType:
        manager->newProjectWizard();
        // Annulée : il reste le projet vide, qu'on met au moins dans le bon sens.
        matchFormat();
        break;
    default:
        matchFormat();
        break;
    }
    QTimer::singleShot(600, window, []() { logState(QStringLiteral("fin")); });
}

QString shotPath(const QString &suffix)
{
    QString path = qEnvironmentVariable("KORACUT_SHOT_SPLASH");
    if (path.isEmpty()) {
        return {};
    }
    if (path.endsWith(QLatin1String(".png"))) {
        path.chop(4);
    }
    return path + suffix + QStringLiteral(".png");
}

void open(MainWindow *window, const QStringList &script)
{
    auto *dialog = new KoraCutFirstRunDialog(window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    // La porte de commande passe avant l'accueil : un client qui se connecte veut piloter, pas
    // attendre qu'on réponde à une fenêtre.
    if (KoraCutPorte *porte = window->koraCutPorte()) {
        QObject::connect(porte, &KoraCutPorte::clientOpened, dialog, [dialog]() { dialog->dismiss(); });
    }
    QObject::connect(dialog, &QDialog::finished, window, [window, dialog]() { apply(window, dialog->choice()); });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    qWarning().noquote() << "KORACUT-ACCUEIL ouverte" << QStringLiteral("taille=%1x%2").arg(dialog->width()).arg(dialog->height());

    // Essais sans souris (voir l'en-tête).
    const QString first = shotPath(QStringLiteral("-accueil"));
    if (!first.isEmpty()) {
        QTimer::singleShot(700, dialog, [dialog, first]() {
            dialog->grab().save(first);
            qWarning().noquote() << "KORACUT-ACCUEIL capture" << first << QStringLiteral("taille=%1x%2").arg(dialog->width()).arg(dialog->height());
        });
    }
    if (script.isEmpty()) {
        return;
    }
    QTimer::singleShot(900, dialog, [dialog, script]() {
        for (const QString &word : script) {
            dialog->play(word);
        }
    });
    const QString second = shotPath(QStringLiteral("-accueil-choix"));
    if (!second.isEmpty()) {
        QTimer::singleShot(1500, dialog, [dialog, second]() { dialog->grab().save(second); });
    }
    const QString last = script.last();
    if (last == QLatin1String("type")) {
        const QString third = shotPath(QStringLiteral("-nouveau"));
        QTimer::singleShot(3200, window, [third]() {
            QWidget *modal = QApplication::activeModalWidget();
            if (modal) {
                qWarning().noquote() << "KORACUT-ACCUEIL suite" << modal->metaObject()->className() << QStringLiteral("taille=%1x%2").arg(modal->width()).arg(modal->height());
                if (!third.isEmpty()) {
                    modal->grab().save(third);
                }
            }
        });
    }
    QTimer::singleShot(1800, dialog, [dialog, last]() {
        if (last == QLatin1String("echap")) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(dialog, &press);
        } else if (last == QLatin1String("entree")) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(dialog->focusWidget() ? dialog->focusWidget() : dialog, &press);
        } else {
            dialog->play(QStringLiteral("bouton:%1").arg(last));
        }
    });
}

} // namespace

void KoraCutFirstRun::schedule(MainWindow *window)
{
    if (!pCore || !window || !pCore->koraCutFirstLaunchWelcome()) {
        return;
    }
    pCore->koraCutFirstLaunchWelcomeDone();
    const QStringList script = qEnvironmentVariable("KORACUT_ACCUEIL").split(QLatin1Char(','), Qt::SkipEmptyParts);
    // Une capture de développement (KORACUT_SHOT) ne doit pas trouver une fenêtre devant elle.
    if (qEnvironmentVariableIsSet("KORACUT_SHOT") && script.isEmpty()) {
        return;
    }
    // Un peu après l'affichage de la fenêtre principale, le temps qu'elle se dessine.
    QTimer::singleShot(250, window, [window, script]() { open(window, script); });
}

KoraCutFirstRunDialog::KoraCutFirstRunDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("koracutWelcome"));
    setWindowTitle(i18nc("@title:window first launch", "Welcome"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(28, 24, 28, 20);
    lay->setSpacing(0);
    lay->setSizeConstraint(QLayout::SetFixedSize);

    // 1. Le mot de bienvenue. La marque : le symbole seul.
    m_symbol = new QLabel(this);
    m_symbol->setFixedSize(20, 20);
    lay->addWidget(m_symbol);
    lay->addSpacing(12);
    auto *title = new QLabel(i18nc("@title first launch", "Welcome to KoraCut"), this);
    title->setObjectName(QStringLiteral("koracutWelcomeTitle"));
    lay->addWidget(title);
    lay->addSpacing(4);
    auto *lead = new QLabel(i18nc("@info first launch", "Pick the shape of your videos, KoraCut sets up the rest."), this);
    lead->setObjectName(QStringLiteral("koracutWelcomeLead"));
    lay->addWidget(lead);
    lay->addSpacing(18);

    // 2. Ce qu'on active ou non, au-dessus du choix du sens (demande d'Amadou Ba).
    m_dark = new Switch(i18nc("@option:check assistant de premier lancement", "Always dark, even when the system is light"), this);
    m_dark->setChecked(KdenliveSettings::koracut_theme_mode() == QLatin1String("dark"));
    m_recents = new Switch(i18nc("@option:check first launch", "Show my recent projects when KoraCut opens"), this);
    m_recents->setChecked(KdenliveSettings::showWelcome());
    m_terminal = new Switch(i18nc("@option:check first launch", "Open the agents' terminal (Claude Code, Codex…)"), this);
    for (QAbstractButton *s : {m_dark, m_recents, m_terminal}) {
        lay->addWidget(s);
        lay->addSpacing(2);
    }
    connect(m_dark, &QAbstractButton::toggled, this, [](bool dark) {
        // Allumé : toujours sombre ; éteint : on suit le système. Appliqué tout de suite, on voit ce qu'on choisit.
        KdenliveSettings::setKoracut_theme_mode(dark ? QStringLiteral("dark") : QStringLiteral("system"));
        UiUtils::applyThemeMode();
    });
    lay->addSpacing(16);

    auto *section = new QLabel(i18nc("@label first launch", "Your videos are mostly"), this);
    section->setObjectName(QStringLiteral("koracutWelcomeSection"));
    lay->addWidget(section);
    lay->addSpacing(8);
    auto *cards = new QHBoxLayout;
    cards->setSpacing(12);
    m_vertical = new FormatCard(true, i18nc("@option:radio video shape", "Vertical"), i18nc("@info uses of a vertical video", "Phone, Reels, TikTok, Shorts"), this);
    m_horizontal = new FormatCard(false, i18nc("@option:radio video shape", "Horizontal"), i18nc("@info uses of a horizontal video", "YouTube, television"), this);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_vertical);
    group->addButton(m_horizontal);
    cards->addWidget(m_vertical);
    cards->addWidget(m_horizontal);
    lay->addLayout(cards);
    lay->addSpacing(8);
    auto *note = new QLabel(i18nc("@info first launch", "Only a starting point: you choose again for every project."), this);
    note->setObjectName(QStringLiteral("koracutWelcomeNote"));
    lay->addWidget(note);
    lay->addSpacing(20);

    // 3. La suite : un type de vidéo, un projet existant, ou rien.
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    auto *openProject = new QPushButton(i18nc("@action:button first launch", "Open a Project…"), this);
    openProject->setAutoDefault(false);
    m_empty = new QPushButton(this);
    m_empty->setObjectName(QStringLiteral("koracutWelcomeQuiet"));
    m_empty->setAutoDefault(false);
    m_primary = new QPushButton(i18nc("@action:button first launch", "Choose a Video Type"), this);
    m_primary->setDefault(true);
    m_primary->setMinimumHeight(30);
    openProject->setMinimumHeight(30);
    m_empty->setMinimumHeight(30);
    // Le bouton principal est dessiné un peu plus large que ce que son texte annonce : sans cette
    // marge, « Choisir un type de vidéo » perdait sa première et sa dernière lettre.
    m_primary->ensurePolished();
    m_primary->setMinimumWidth(m_primary->sizeHint().width() + 24);
    buttons->addWidget(openProject);
    buttons->addStretch();
    buttons->addWidget(m_empty);
    buttons->addWidget(m_primary);
    lay->addLayout(buttons);

    connect(openProject, &QPushButton::clicked, this, [this]() { finish(Outcome::OpenProject); });
    connect(m_empty, &QPushButton::clicked, this, [this]() { finish(Outcome::Empty); });
    connect(m_primary, &QPushButton::clicked, this, [this]() { finish(Outcome::ChooseType); });
    connect(group, &QButtonGroup::buttonToggled, this, [this]() { refreshButtons(); });

    // L'ordre du clavier : les interrupteurs, les cartes, puis les boutons. Entrée valide d'où qu'on soit.
    setTabOrder({m_primary, m_dark, m_recents, m_terminal, m_vertical, m_horizontal, openProject, m_empty});
    refreshButtons();
    refreshSymbol();
    m_primary->setFocus();
}

void KoraCutFirstRunDialog::refreshSymbol()
{
    if (m_symbol) {
        m_symbol->setPixmap(KoraCutIcons::pixmap(QStringLiteral(":/koracut/koracut-symbole.svg"), 20, palette().color(QPalette::WindowText), devicePixelRatioF()));
    }
}

void KoraCutFirstRunDialog::refreshButtons()
{
    const bool chosen = m_vertical->isChecked() || m_horizontal->isChecked();
    m_empty->setText(chosen ? i18nc("@action:button first launch", "Start with an Empty Project") : i18nc("@action:button first launch", "Start Without Choosing"));
}

void KoraCutFirstRunDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshSymbol();
    }
}

Choice KoraCutFirstRunDialog::choice() const
{
    Choice c;
    c.format = m_vertical->isChecked() ? QStringLiteral("vertical") : (m_horizontal->isChecked() ? QStringLiteral("horizontal") : QString());
    c.showRecents = m_recents->isChecked();
    c.terminal = m_terminal->isChecked();
    c.outcome = m_outcome;
    return c;
}

void KoraCutFirstRunDialog::finish(Outcome outcome)
{
    m_outcome = outcome;
    m_finished = true;
    accept();
}

void KoraCutFirstRunDialog::dismiss()
{
    if (m_finished) {
        return;
    }
    m_outcome = Outcome::Dismissed;
    m_finished = true;
    reject();
}

void KoraCutFirstRunDialog::done(int result)
{
    // Échap ou la croix de la fenêtre : on passe, comme « Commencer sans rien choisir ».
    if (!m_finished) {
        m_outcome = Outcome::Empty;
        m_finished = true;
    }
    QDialog::done(result);
}

bool KoraCutFirstRunDialog::play(const QString &word)
{
    if (word == QLatin1String("vertical")) {
        m_vertical->click();
    } else if (word == QLatin1String("horizontal")) {
        m_horizontal->click();
    } else if (word == QLatin1String("sombre")) {
        m_dark->setChecked(true);
    } else if (word == QLatin1String("systeme")) {
        m_dark->setChecked(false);
    } else if (word == QLatin1String("sans-recents")) {
        m_recents->setChecked(false);
    } else if (word == QLatin1String("terminal")) {
        m_terminal->setChecked(true);
    } else if (word == QLatin1String("bouton:type")) {
        m_primary->click();
    } else if (word == QLatin1String("bouton:vide")) {
        m_empty->click();
    } else if (word == QLatin1String("bouton:ouvrir")) {
        finish(Outcome::OpenProject);
    } else {
        return false;
    }
    return true;
}
