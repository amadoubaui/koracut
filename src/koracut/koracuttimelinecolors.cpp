/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracuttimelinecolors.h"

#include "koracutagenttools.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRegularExpression>

#include <unordered_set>

namespace KoraCutTimelineColors {

const QList<Swatch> &palette()
{
    // Les quatre couleurs de pistes de la charte d'abord, puis des teintes de même poids, toutes
    // lisibles sous un nom écrit en blanc sur #1E1E1E. Pas de noir ni de blanc : on ne verrait plus le clip.
    static const QList<Swatch> list = {
        {QStringLiteral("bleu"), QColor(0x0D, 0x99, 0xFF)},      {QStringLiteral("violet"), QColor(0x7C, 0x5C, 0xFF)},
        {QStringLiteral("vert"), QColor(0x14, 0xA3, 0x7F)},      {QStringLiteral("orange"), QColor(0xE0, 0x7B, 0x39)},
        {QStringLiteral("rouge"), QColor(0xE5, 0x48, 0x4D)},     {QStringLiteral("jaune"), QColor(0xC2, 0x94, 0x22)},
        {QStringLiteral("rose"), QColor(0xD6, 0x45, 0x7E)},      {QStringLiteral("turquoise"), QColor(0x1F, 0xA2, 0xB0)},
        {QStringLiteral("olive"), QColor(0x7A, 0x9A, 0x3A)},     {QStringLiteral("brun"), QColor(0x9A, 0x6A, 0x43)},
        {QStringLiteral("bleu_nuit"), QColor(0x42, 0x63, 0xC9)}, {QStringLiteral("gris"), QColor(0x7A, 0x7F, 0x87)},
    };
    return list;
}

QString label(const QString &id)
{
    if (id == QLatin1String("bleu")) return i18nc("@item:inmenu clip color", "Blue");
    if (id == QLatin1String("violet")) return i18nc("@item:inmenu clip color", "Violet");
    if (id == QLatin1String("vert")) return i18nc("@item:inmenu clip color", "Green");
    if (id == QLatin1String("orange")) return i18nc("@item:inmenu clip color", "Orange");
    if (id == QLatin1String("rouge")) return i18nc("@item:inmenu clip color", "Red");
    if (id == QLatin1String("jaune")) return i18nc("@item:inmenu clip color", "Yellow");
    if (id == QLatin1String("rose")) return i18nc("@item:inmenu clip color", "Pink");
    if (id == QLatin1String("turquoise")) return i18nc("@item:inmenu clip color", "Teal");
    if (id == QLatin1String("olive")) return i18nc("@item:inmenu clip color", "Olive");
    if (id == QLatin1String("brun")) return i18nc("@item:inmenu clip color", "Brown");
    if (id == QLatin1String("bleu_nuit")) return i18nc("@item:inmenu clip color", "Navy");
    if (id == QLatin1String("gris")) return i18nc("@item:inmenu clip color", "Gray");
    return id;
}

QString normalize(const QString &input, bool *ok)
{
    *ok = true;
    const QString text = input.trimmed().toLower();
    if (text.isEmpty() || text == QLatin1String("defaut") || text == QLatin1String("piste")) {
        return QString();
    }
    for (const Swatch &s : palette()) {
        if (s.id == text) {
            return s.color.name().toUpper();
        }
    }
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-f]{6}$"));
    if (hex.match(text).hasMatch()) {
        return text.toUpper();
    }
    *ok = false;
    return QString();
}

QString nameOf(const QString &hex)
{
    if (hex.isEmpty()) {
        return QString();
    }
    const QColor c(hex);
    for (const Swatch &s : palette()) {
        if (s.color == c) {
            return s.id;
        }
    }
    return c.isValid() ? c.name().toUpper() : hex;
}

} // namespace KoraCutTimelineColors

namespace {

// Une pastille de 16 px (32 px de pixels pour les écrans HiDPI), rayon 3 comme les clips. La couleur
// choisie porte un anneau blanc : la coche d'un menu ne se voit pas à côté d'une icône. Une pastille
// invalide dessine un carré vide, celui de « Couleur de la piste » et « Par défaut ».
QIcon swatchIcon(const QColor &color, bool selected)
{
    QPixmap pix(32, 32);
    pix.setDevicePixelRatio(2.);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    if (color.isValid()) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(3, 3, 10, 10), 2, 2);
    } else {
        p.setPen(QPen(QColor(0x9A, 0x9A, 0x9A), 1.));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(3.5, 3.5, 9, 9), 2, 2);
    }
    if (selected) {
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(0.75, 0.75, 14.5, 14.5), 3.5, 3.5);
    }
    return QIcon(pix);
}

} // namespace

KoraCutColorMenu::KoraCutColorMenu(QWidget *parent)
    : QObject(parent)
    , m_clip(new QMenu(i18nc("@title:menu", "Clip Color"), parent))
    , m_track(new QMenu(i18nc("@title:menu", "Track Color"), parent))
{
    m_clip->setObjectName(QStringLiteral("koracut_clip_color"));
    m_track->setObjectName(QStringLiteral("koracut_track_color"));
    fill(m_clip, true);
    fill(m_track, false);
}

void KoraCutColorMenu::fill(QMenu *menu, bool forClip)
{
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    QAction *reset = menu->addAction(forClip ? i18nc("@item:inmenu clip color", "Same as Track") : i18nc("@item:inmenu track color", "Default"));
    reset->setData(QStringLiteral("defaut"));
    reset->setIcon(swatchIcon(QColor(), false));
    reset->setProperty("koracut_icon", QVariant::fromValue(swatchIcon(QColor(), false)));
    reset->setProperty("koracut_icon_on", QVariant::fromValue(swatchIcon(QColor(), true)));
    reset->setCheckable(true);
    group->addAction(reset);
    menu->addSeparator();
    for (const auto &s : KoraCutTimelineColors::palette()) {
        QAction *a = menu->addAction(swatchIcon(s.color, false), KoraCutTimelineColors::label(s.id));
        a->setProperty("koracut_icon", QVariant::fromValue(swatchIcon(s.color, false)));
        a->setProperty("koracut_icon_on", QVariant::fromValue(swatchIcon(s.color, true)));
        a->setData(s.id);
        a->setCheckable(true);
        group->addAction(a);
    }
    connect(menu, &QMenu::triggered, this, [this, forClip](QAction *a) { apply(forClip, a->data().toString()); });
}

void KoraCutColorMenu::check(QMenu *menu, const QString &current)
{
    const QString id = current.isEmpty() ? QStringLiteral("defaut") : KoraCutTimelineColors::nameOf(current);
    for (QAction *a : menu->actions()) {
        if (a->isCheckable()) {
            const bool on = a->data().toString() == id;
            a->setChecked(on);
            a->setIcon(a->property(on ? "koracut_icon_on" : "koracut_icon").value<QIcon>());
        }
    }
}

void KoraCutColorMenu::prepareClip(const std::shared_ptr<TimelineItemModel> &timeline, int cid)
{
    m_timeline = timeline;
    m_clips.clear();
    if (!timeline || !timeline->isClip(cid)) {
        m_clip->setEnabled(false);
        return;
    }
    const std::unordered_set<int> selection = timeline->getCurrentSelection();
    if (selection.count(cid) > 0) {
        for (int id : selection) {
            if (timeline->isClip(id)) {
                m_clips << id;
            }
        }
    }
    if (m_clips.isEmpty()) {
        m_clips << cid;
    }
    m_clip->setEnabled(true);
    // Une seule coche quand tous les clips ont la même couleur, aucune sinon.
    QString current = TimelineFunctions::koraCutClipColor(timeline, m_clips.first());
    for (int id : std::as_const(m_clips)) {
        if (TimelineFunctions::koraCutClipColor(timeline, id) != current) {
            current = QStringLiteral("?");
            break;
        }
    }
    check(m_clip, current);
}

void KoraCutColorMenu::prepareTrack(const std::shared_ptr<TimelineItemModel> &timeline, int tid)
{
    m_timeline = timeline;
    m_trackId = tid;
    const bool valid = timeline && timeline->isTrack(tid);
    m_track->setEnabled(valid);
    if (valid) {
        check(m_track, timeline->getTrackProperty(tid, QStringLiteral("kdenlive:koracut_color")).toString());
    }
}

void KoraCutColorMenu::apply(bool forClip, const QString &id)
{
    auto timeline = m_timeline.lock();
    if (!timeline) {
        return;
    }
    if (forClip) {
        QJsonArray ids;
        for (int cid : std::as_const(m_clips)) {
            ids.append(cid);
        }
        KoraCutAgentTools::call(timeline, QStringLiteral("couleur_clip"), QJsonObject{{QStringLiteral("clip_ids"), ids}, {QStringLiteral("couleur"), id}});
    } else {
        KoraCutAgentTools::call(timeline, QStringLiteral("couleur_piste"), QJsonObject{{QStringLiteral("piste_id"), m_trackId}, {QStringLiteral("couleur"), id}});
    }
}
