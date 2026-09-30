/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutclipstrip.h"
#include "koracutcolorwidgets.h"
#include "koracutgradecontroller.h"

#include "core.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>
#include <QKeySequence>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

using KoraCutColorUi::tone;

namespace {
// Les vignettes suivent la hauteur de la bande (16:9) : la bande n'a jamais de vide sous ses vignettes.
constexpr int kTileMinH = 48;
constexpr int kTileMaxH = 180; // sur un grand écran la bande est plus haute : les vignettes la remplissent
constexpr int kGap = 10;
constexpr int kTop = 18;
} // namespace

KoraCutClipStrip::KoraCutClipStrip(QWidget *parent)
    : QWidget(parent)
{
    // Une rangée de vignettes, comme la bande des plans de Resolve : la hauteur de son contenu (numéro,
    // vignette, nom), pas plus ; le reste de la hauteur va à l'image et aux palettes.
    setMinimumHeight(kTop + kTileMinH + 24);
    setMaximumHeight(kTop + kTileMaxH + 24);
    setToolTip(i18nc("@info:tooltip",
                     "Click: grade this clip. Command-click: select several clips. Middle-click a clip: copy its grade to the selected clips."));
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(250);
    connect(&m_refresh, &QTimer::timeout, this, &KoraCutClipStrip::reload);
    auto *c = KoraCutGradeController::self();
    connect(c, &KoraCutGradeController::clipsChanged, this, [this]() { m_refresh.start(); });
    connect(c, &KoraCutGradeController::graphChanged, this, [this]() {
        // Seule la vignette du plan courant change : on la refait sans tout recharger.
        const int cid = KoraCutGradeController::self()->currentClip();
        if (cid >= 0) {
            m_images.insert(cid, KoraCutGradeController::self()->clipImage(cid, int(tileW() * devicePixelRatioF()), true));
            update();
        }
    });
    connect(c, &KoraCutGradeController::clipChanged, this, [this](int cid) {
        m_selected = {cid};
        // Le plan courant reste visible.
        const int i = int(m_clips.indexOf(cid));
        if (i >= 0) {
            const QRect r = tileRect(i);
            if (r.left() < 0 || r.right() > width()) {
                m_scroll = std::max(0, i * (tileW() + kGap) - width() / 3);
            }
        }
        update();
    });
    reload();
}

int KoraCutClipStrip::tileH() const
{
    return std::clamp(height() - kTop - 24, kTileMinH, kTileMaxH);
}

int KoraCutClipStrip::tileW() const
{
    return tileH() * 16 / 9;
}

void KoraCutClipStrip::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Les images sont calculées à la taille des vignettes (rien de plus en mémoire) : on les refait si elle change.
    if (tileH() != m_imagesH) {
        m_imagesH = tileH();
        m_refresh.start();
    }
}

QSize KoraCutClipStrip::sizeHint() const
{
    return {800, kTop + 72 + 24};
}

void KoraCutClipStrip::reload()
{
    auto *c = KoraCutGradeController::self();
    m_clips = c->videoClips();
    m_images.clear();
    for (int cid : std::as_const(m_clips)) {
        m_images.insert(cid, c->clipImage(cid, int(tileW() * devicePixelRatioF()), true));
    }
    update();
}

QRect KoraCutClipStrip::tileRect(int index) const
{
    return QRect(kGap + index * (tileW() + kGap) - m_scroll, kTop, tileW(), tileH());
}

int KoraCutClipStrip::tileAt(const QPoint &pos) const
{
    for (int i = 0; i < m_clips.size(); ++i) {
        if (tileRect(i).adjusted(0, -kTop, 0, 20).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void KoraCutClipStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), tone("panneau"));
    auto *c = KoraCutGradeController::self();
    auto t = c->timeline();
    if (m_clips.isEmpty()) {
        p.setPen(tone("texte_secondaire"));
        p.drawText(rect(), Qt::AlignCenter, i18nc("@info", "No video clip in this sequence."));
        return;
    }
    QFont small = font();
    if (small.pixelSize() > 0) {
        small.setPixelSize(std::max(9, small.pixelSize() - 1));
    } else {
        small.setPointSizeF(std::max(7.5, small.pointSizeF() - 1.));
    }
    p.setFont(small);
    for (int i = 0; i < m_clips.size(); ++i) {
        const QRect r = tileRect(i);
        if (r.right() < 0 || r.left() > width()) {
            continue;
        }
        const int cid = m_clips.at(i);
        const bool current = cid == c->currentClip();
        const bool selected = m_selected.contains(cid);
        // Au-dessus : le numéro du plan (encadré s'il est étalonné), la piste.
        const QRect num(r.left(), 2, 24, kTop - 4);
        const bool graded = c->hasGrade(cid);
        p.setPen(graded ? QPen(tone("accent"), 1) : Qt::NoPen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(num, 3, 3);
        p.setPen(tone("texte"));
        p.drawText(num, Qt::AlignCenter, i < 9 ? QStringLiteral("0%1").arg(i + 1) : QString::number(i + 1));
        if (t) {
            p.setPen(tone("texte_secondaire"));
            p.drawText(QRect(num.right() + 6, 2, r.width() - 30, kTop - 4), Qt::AlignLeft | Qt::AlignVCenter,
                       t->getTrackTagById(t->getClipTrackId(cid)));
        }
        // La vignette, étalonnée.
        QPainterPath clip;
        clip.addRoundedRect(QRectF(r), 4, 4);
        p.save();
        p.setClipPath(clip);
        const QImage img = m_images.value(cid);
        if (!img.isNull()) {
            QImage scaled = img;
            scaled.setDevicePixelRatio(devicePixelRatioF());
            const QSizeF s = scaled.deviceIndependentSize();
            const double k = std::max(r.width() / s.width(), r.height() / s.height());
            const QSizeF d = s * k;
            p.drawImage(QRectF(r.center().x() - d.width() / 2, r.center().y() - d.height() / 2, d.width(), d.height()), scaled);
        } else {
            p.fillRect(r, tone("champ"));
        }
        p.restore();
        if (current || selected) {
            p.setPen(QPen(current ? tone("accent") : tone("texte"), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), 4, 4);
        }
        // En dessous : le nom du plan.
        if (t) {
            p.setPen(tone("texte_secondaire"));
            const QString name = t->getClipName(cid);
            p.drawText(QRect(r.left(), r.bottom() + 3, r.width(), 16), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(small).elidedText(name, Qt::ElideRight, r.width()));
        }
    }
}

void KoraCutClipStrip::mousePressEvent(QMouseEvent *e)
{
    const int i = tileAt(e->position().toPoint());
    if (i < 0) {
        return;
    }
    auto *c = KoraCutGradeController::self();
    const int cid = m_clips.at(i);
    if (e->button() == Qt::MiddleButton) {
        // Resolve : clic milieu sur un plan = son grade va sur les plans sélectionnés.
        QList<int> targets(m_selected.cbegin(), m_selected.cend());
        targets.removeAll(cid);
        if (targets.isEmpty() && c->currentClip() >= 0 && c->currentClip() != cid) {
            targets << c->currentClip();
        }
        c->copyGrade(cid, targets, e->modifiers() & Qt::ShiftModifier);
        return;
    }
    if (e->button() != Qt::LeftButton) {
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) {
        if (m_selected.contains(cid)) {
            m_selected.remove(cid);
        } else {
            m_selected.insert(cid);
        }
        update();
        return;
    }
    m_selected = {cid};
    c->setCurrentClip(cid, true);
}

void KoraCutClipStrip::contextMenuEvent(QContextMenuEvent *e)
{
    const int i = tileAt(e->pos());
    auto *c = KoraCutGradeController::self();
    QMenu menu(this);
    const int cid = i >= 0 ? m_clips.at(i) : c->currentClip();
    QList<int> targets(m_selected.cbegin(), m_selected.cend());
    targets.removeAll(cid);
    menu.addAction(i18nc("@action", "Apply This Grade to Selected Clips"), this, [c, cid, targets]() { c->copyGrade(cid, targets, false); })->setEnabled(!targets.isEmpty());
    menu.addAction(i18nc("@action", "Append This Grade to Selected Clips"), this, [c, cid, targets]() { c->copyGrade(cid, targets, true); })->setEnabled(!targets.isEmpty());
    menu.addAction(i18nc("@action", "Apply This Grade to All Clips"), this, [c, cid, this]() { c->copyGrade(cid, m_clips, false); });
    menu.addSeparator();
    menu.addAction(i18nc("@action", "Grab Still"), this, [c]() { c->grabStill(); });
    menu.exec(e->globalPos());
}

void KoraCutClipStrip::wheelEvent(QWheelEvent *e)
{
    const int delta = e->pixelDelta().isNull() ? -e->angleDelta().y() / 2 : -(e->pixelDelta().x() + e->pixelDelta().y());
    const int maxScroll = std::max(0, int(m_clips.size()) * (tileW() + kGap) + kGap - width());
    m_scroll = std::clamp(m_scroll + delta, 0, maxScroll);
    update();
}

// ---- Galerie ------------------------------------------------------------------------------------------

KoraCutGallery::KoraCutGallery(QWidget *parent)
    : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 4, 6, 6);
    auto *head = new QHBoxLayout;
    auto *grab = new QToolButton(this);
    grab->setText(i18nc("@action:button take a still of the current clip", "Grab Still"));
    grab->setToolTip(i18nc("@info:tooltip %1 is the shortcut", "Keep the image and the grade of the current clip (%1)",
                           QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G).toString(QKeySequence::NativeText)));
    grab->setAutoRaise(true);
    auto *split = new QToolButton(this);
    split->setText(i18nc("@action:button before and after split screen", "Before / After"));
    split->setToolTip(i18nc("@info:tooltip %1 is the shortcut", "Split the viewer: the left half without the grade (%1)",
                            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_W).toString(QKeySequence::NativeText)));
    split->setCheckable(true);
    split->setAutoRaise(true);
    head->addWidget(grab);
    head->addStretch(1);
    head->addWidget(split);
    v->addLayout(head);
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(128, 72));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(6);
    m_list->setToolTip(i18nc("@info:tooltip", "Double-click a still to apply its grade to the current clip."));
    v->addWidget(m_list, 1);
    auto *c = KoraCutGradeController::self();
    connect(grab, &QToolButton::clicked, c, &KoraCutGradeController::grabStill);
    connect(split, &QToolButton::toggled, c, [c](bool on) { c->setCompare(on); });
    connect(c, &KoraCutGradeController::stillsChanged, this, &KoraCutGallery::reload);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) { KoraCutGradeController::self()->applyStill(m_list->row(it), false); });
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *it = m_list->itemAt(pos);
        if (!it) {
            return;
        }
        const int row = m_list->row(it);
        auto *ctl = KoraCutGradeController::self();
        QMenu menu(this);
        menu.addAction(i18nc("@action", "Apply Grade"), this, [ctl, row]() { ctl->applyStill(row, false); });
        menu.addAction(i18nc("@action", "Append Node Graph"), this, [ctl, row]() { ctl->applyStill(row, true); });
        menu.addSeparator();
        menu.addAction(i18nc("@action", "Delete Still"), this, [ctl, row]() { ctl->removeStill(row); });
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    reload();
}

void KoraCutGallery::reload()
{
    m_list->clear();
    const auto stills = KoraCutGradeController::self()->stills();
    for (const auto &s : stills) {
        auto *it = new QListWidgetItem(QIcon(QPixmap::fromImage(s.image)), s.label, m_list);
        it->setToolTip(s.label);
    }
}
