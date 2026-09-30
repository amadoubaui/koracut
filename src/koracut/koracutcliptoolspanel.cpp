/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutcliptoolspanel.h"
#include "koracutagenttools.h"
#include "koracutcliptools.h"
#include "koracutmetrics.h"
#include "koracutstabilizer.h"
#include "koracutthemes.h"

#include "core.h"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinewidget.h"

#include <KLocalizedString>

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

using KoraCutClipTools::Tool;

namespace {

constexpr int SwitchW = 28;
constexpr int SwitchH = 16;

/** Un interrupteur : piste arrondie, bouton rond, accent quand il est ouvert (comme les Préférences). */
class ToolSwitch : public QAbstractButton
{
public:
    explicit ToolSwitch(QWidget *parent)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(SwitchW + 2, SwitchH + 4);
        setFocusPolicy(Qt::TabFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (!isEnabled()) {
            p.setOpacity(0.38);
        }
        const QRectF track(1, (height() - SwitchH) / 2.0, SwitchW, SwitchH);
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(KoraCutThemes::token(QStringLiteral("accent")));
        } else {
            p.setPen(QPen(KoraCutThemes::token(QStringLiteral("bordure")), 1));
            p.setBrush(KoraCutThemes::token(QStringLiteral("champ")));
        }
        p.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5), track.height() / 2, track.height() / 2);
        const qreal d = track.height() - 6;
        const qreal x = isChecked() ? track.right() - 3 - d : track.left() + 3;
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? KoraCutThemes::token(QStringLiteral("texte_sur_accent")) : KoraCutThemes::token(QStringLiteral("texte_secondaire")));
        p.drawEllipse(QRectF(x, track.top() + 3, d, d));
        if (hasFocus() && window()->testAttribute(Qt::WA_KeyboardFocusChange)) {
            p.setOpacity(1);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(KoraCutThemes::token(QStringLiteral("accent")), 2));
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), (height() - 2) / 2.0, (height() - 2) / 2.0);
        }
    }
};

/** Une ligne d'outil : toute la ligne répond au clic quand son contrôle est un interrupteur. */
class ToolRow : public QWidget
{
public:
    ToolRow(QWidget *parent, QAbstractButton *toggle)
        : QWidget(parent)
        , m_toggle(toggle)
    {
        setObjectName(QStringLiteral("koracutToolRow"));
        setAttribute(Qt::WA_StyledBackground, true);
        if (m_toggle) {
            setCursor(Qt::PointingHandCursor);
        }
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_toggle && m_toggle->isEnabled() && rect().contains(event->position().toPoint())) {
            m_toggle->click();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QAbstractButton *m_toggle;
};

QLabel *groupLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text.toUpper(), parent);
    label->setObjectName(QStringLiteral("koracutToolGroup"));
    return label;
}

} // namespace

KoraCutClipToolsPanel::KoraCutClipToolsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("koracutClipTools"));
    setAttribute(Qt::WA_StyledBackground, true);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(30);
    connect(&m_refreshTimer, &QTimer::timeout, this, &KoraCutClipToolsPanel::refresh);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone);
    lay->setSpacing(KoraCutMetrics::SpaceTight);

    auto *title = new QLabel(i18nc("@title inspector section", "Tools"), this);
    title->setObjectName(QStringLiteral("koracutToolTitle"));
    lay->addWidget(title);

    // ---- Son -------------------------------------------------------------------------------
    m_sound = new QWidget(this);
    auto *sound = new QVBoxLayout(m_sound);
    sound->setContentsMargins(0, 0, 0, 0);
    sound->setSpacing(KoraCutMetrics::SpaceTight);
    sound->addWidget(groupLabel(i18nc("@title tools group", "Sound"), m_sound));

    m_denoiseSwitch = new ToolSwitch(m_sound);
    sound->addWidget(toolRow(QStringLiteral("koracut-outil-bruit"), i18nc("@label", "Reduce noise"),
                             i18nc("@info:tooltip", "Removes the background noise under the voice: fan, street, hiss."), m_denoiseSwitch));
    m_denoiseGroup = new QButtonGroup(this);
    {
        QList<QPair<QString, QString>> items;
        QList<QString> tips;
        for (const auto &c : KoraCutClipTools::denoiseLevels()) {
            items << qMakePair(c.id, c.label);
            tips << c.hint;
        }
        m_denoiseLevels = segments(items, tips, m_denoiseGroup);
        sound->addWidget(m_denoiseLevels);
    }
    connect(m_denoiseSwitch, &QAbstractButton::clicked, this, [this](bool on) {
        if (!m_updating) {
            applySound(int(Tool::Denoise), on ? QStringLiteral("moyen") : QString());
        }
    });
    connect(m_denoiseGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *b) {
        if (!m_updating) {
            applySound(int(Tool::Denoise), b->property("koracutChoice").toString());
        }
    });

    m_enhanceSwitch = new ToolSwitch(m_sound);
    sound->addWidget(toolRow(QStringLiteral("koracut-outil-voix"), i18nc("@label", "Enhance voice"),
                             i18nc("@info:tooltip", "A clearer, fuller voice: low rumble removed, more presence, softer hissing sounds, even level."),
                             m_enhanceSwitch));
    connect(m_enhanceSwitch, &QAbstractButton::clicked, this, [this](bool on) {
        if (!m_updating) {
            applySound(int(Tool::Enhance), on ? QStringLiteral("standard") : QString());
        }
    });

    m_normalizeButton = new QPushButton(i18nc("@action:button", "Apply"), m_sound);
    m_normalizeButton->setObjectName(QStringLiteral("koracutToolButton"));
    m_normalizeButton->setCursor(Qt::PointingHandCursor);
    sound->addWidget(toolRow(QStringLiteral("koracut-outil-normaliser"), i18nc("@label", "Normalize volume"),
                             i18nc("@info:tooltip", "Brings this clip to -16 LUFS, the usual level of a spoken voice. The analysis reads the whole clip."),
                             m_normalizeButton));
    connect(m_normalizeButton, &QPushButton::clicked, this, &KoraCutClipToolsPanel::normalize);

    auto *effectsHead = new QHBoxLayout();
    effectsHead->setContentsMargins(0, KoraCutMetrics::SpaceTight, 0, 0);
    auto *effectsLabel = new QLabel(i18nc("@label", "Voice effects"), m_sound);
    effectsLabel->setObjectName(QStringLiteral("koracutToolLabel"));
    effectsHead->addWidget(effectsLabel);
    effectsHead->addStretch();
    m_effectClear = new QToolButton(m_sound);
    m_effectClear->setObjectName(QStringLiteral("koracutToolLink"));
    m_effectClear->setText(i18nc("@action:button remove the voice effect", "Remove"));
    m_effectClear->setCursor(Qt::PointingHandCursor);
    connect(m_effectClear, &QToolButton::clicked, this, [this]() { applySound(int(Tool::VoiceEffect), QString()); });
    effectsHead->addWidget(m_effectClear);
    sound->addLayout(effectsHead);

    auto *cards = new QGridLayout();
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(KoraCutMetrics::SpaceTight);
    m_effectGroup = new QButtonGroup(this);
    m_effectGroup->setExclusive(false);
    int n = 0;
    constexpr int columns = 4;
    for (const auto &c : KoraCutClipTools::voiceEffects()) {
        auto *card = new QToolButton(m_sound);
        card->setObjectName(QStringLiteral("koracutToolCard"));
        card->setCheckable(true);
        card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        card->setIcon(QIcon::fromTheme(c.icon));
        card->setIconSize(QSize(KoraCutMetrics::IconDefault, KoraCutMetrics::IconDefault));
        card->setText(c.label);
        card->setToolTip(c.hint);
        card->setCursor(Qt::PointingHandCursor);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        card->setMinimumWidth(48);
        card->setFixedHeight(48);
        card->setProperty("koracutChoice", c.id);
        card->setAccessibleName(c.label);
        m_effectGroup->addButton(card);
        m_effectCards << card;
        cards->addWidget(card, n / columns, n % columns);
        ++n;
    }
    for (int c = 0; c < columns; ++c) {
        cards->setColumnStretch(c, 1);
    }
    sound->addLayout(cards);
    connect(m_effectGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *b) {
        if (m_updating) {
            return;
        }
        // Un clic sur l'effet posé le retire, un clic sur un autre le remplace.
        applySound(int(Tool::VoiceEffect), b->isChecked() ? b->property("koracutChoice").toString() : QString());
    });
    lay->addWidget(m_sound);

    // ---- Image -----------------------------------------------------------------------------
    m_image = new QWidget(this);
    auto *image = new QVBoxLayout(m_image);
    image->setContentsMargins(0, KoraCutMetrics::SpaceGroup, 0, 0);
    image->setSpacing(KoraCutMetrics::SpaceTight);
    image->addWidget(groupLabel(i18nc("@title tools group", "Picture"), m_image));
    m_stabilizeSwitch = new ToolSwitch(m_image);
    image->addWidget(toolRow(QStringLiteral("koracut-outil-stabiliser"), i18nc("@label", "Stabilize"),
                             i18nc("@info:tooltip", "Removes the shakes of a shot filmed by hand. The movement is analyzed once, then each level is instant."),
                             m_stabilizeSwitch));
    m_stabilizeGroup = new QButtonGroup(this);
    {
        QList<QPair<QString, QString>> items;
        QList<QString> tips;
        for (const auto &c : KoraCutStabilizer::levels()) {
            items << qMakePair(c.id, c.label);
            tips << c.hint;
        }
        m_stabilizeLevels = segments(items, tips, m_stabilizeGroup);
        image->addWidget(m_stabilizeLevels);
    }
    m_stabilizeProgressRow = new QWidget(m_image);
    auto *progressLay = new QHBoxLayout(m_stabilizeProgressRow);
    progressLay->setContentsMargins(0, 0, 0, 0);
    progressLay->setSpacing(KoraCutMetrics::SpaceGroup);
    m_stabilizeState = new QLabel(m_stabilizeProgressRow);
    m_stabilizeState->setObjectName(QStringLiteral("koracutToolHint"));
    m_stabilizeProgress = new QProgressBar(m_stabilizeProgressRow);
    m_stabilizeProgress->setObjectName(QStringLiteral("koracutToolProgress"));
    m_stabilizeProgress->setRange(0, 100);
    m_stabilizeProgress->setTextVisible(false);
    m_stabilizeProgress->setFixedHeight(4);
    auto *cancel = new QToolButton(m_stabilizeProgressRow);
    cancel->setObjectName(QStringLiteral("koracutToolLink"));
    cancel->setText(i18nc("@action:button", "Cancel"));
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QToolButton::clicked, this, [this]() {
        if (auto tl = timeline()) {
            KoraCutStabilizer::instance()->cancel(tl, m_clipId);
        }
        scheduleRefresh();
    });
    auto *progressCol = new QVBoxLayout();
    progressCol->setSpacing(2);
    progressCol->addWidget(m_stabilizeState);
    progressCol->addWidget(m_stabilizeProgress);
    progressLay->addLayout(progressCol, 1);
    progressLay->addWidget(cancel);
    image->addWidget(m_stabilizeProgressRow);
    connect(m_stabilizeSwitch, &QAbstractButton::clicked, this, [this](bool on) {
        if (!m_updating) {
            stabilize(on ? QStringLiteral("moyen") : QString());
        }
    });
    connect(m_stabilizeGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *b) {
        if (!m_updating) {
            stabilize(b->property("koracutChoice").toString());
        }
    });
    lay->addWidget(m_image);

    auto *stab = KoraCutStabilizer::instance();
    connect(stab, &KoraCutStabilizer::progressChanged, this, [this](int cid, int percent) {
        auto tl = timeline();
        if (tl && KoraCutClipTools::videoTarget(tl, m_clipId) == cid) {
            if (!m_stabilizeProgressRow->isVisible()) {
                // Lancée par un agent ou la porte : la section passe en mode « analyse ».
                scheduleRefresh();
            }
            m_stabilizeProgress->setValue(percent);
            m_stabilizeState->setText(i18nc("@info", "Analyzing the movement… %1%", percent));
        }
    });
    connect(stab, &KoraCutStabilizer::finished, this, [this](int cid, bool ok, const QString &message) {
        auto tl = timeline();
        if (!ok && !message.isEmpty()) {
            pCore->displayMessage(message, ErrorMessage, 6000);
        }
        if (tl && KoraCutClipTools::videoTarget(tl, m_clipId) == cid) {
            scheduleRefresh();
        }
    });
    hide();
}

QWidget *KoraCutClipToolsPanel::toolRow(const QString &icon, const QString &text, const QString &tip, QWidget *control)
{
    auto *toggle = dynamic_cast<ToolSwitch *>(control);
    auto *row = new ToolRow(control->parentWidget(), toggle);
    row->setToolTip(tip);
    row->setMinimumHeight(KoraCutMetrics::RowInspector + 4);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(KoraCutMetrics::SpaceGroup);
    auto *glyph = new QLabel(row);
    glyph->setPixmap(QIcon::fromTheme(icon).pixmap(KoraCutMetrics::IconDefault, KoraCutMetrics::IconDefault));
    glyph->setFixedSize(KoraCutMetrics::IconDefault, KoraCutMetrics::IconDefault);
    h->addWidget(glyph);
    auto *label = new QLabel(text, row);
    label->setObjectName(QStringLiteral("koracutToolName"));
    h->addWidget(label, 1);
    control->setParent(row);
    control->setToolTip(tip);
    control->setAccessibleName(text);
    h->addWidget(control);
    return row;
}

QWidget *KoraCutClipToolsPanel::segments(const QList<QPair<QString, QString>> &items, const QList<QString> &tips, QButtonGroup *group)
{
    auto *box = new QWidget(this);
    auto *h = new QHBoxLayout(box);
    // En retrait sous le nom de l'outil (icône + espace).
    h->setContentsMargins(KoraCutMetrics::IconDefault + KoraCutMetrics::SpaceGroup, 0, 0, KoraCutMetrics::SpaceTight);
    h->setSpacing(0);
    group->setExclusive(true);
    for (int i = 0; i < items.size(); ++i) {
        auto *b = new QToolButton(box);
        b->setCheckable(true);
        b->setText(items.at(i).second);
        b->setToolTip(tips.value(i));
        b->setProperty("koracutChoice", items.at(i).first);
        b->setProperty("koracutSegment", i == 0 ? QStringLiteral("premier") : (i == items.size() - 1 ? QStringLiteral("dernier") : QStringLiteral("milieu")));
        b->setObjectName(QStringLiteral("koracutToolSegment"));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setCursor(Qt::PointingHandCursor);
        group->addButton(b);
        h->addWidget(b, 1);
    }
    return box;
}

std::shared_ptr<TimelineItemModel> KoraCutClipToolsPanel::timeline() const
{
    return m_timeline.lock();
}

void KoraCutClipToolsPanel::showClip(int clipId)
{
    for (const auto &c : std::as_const(m_watch)) {
        disconnect(c);
    }
    m_watch.clear();
    m_clipId = clipId;
    m_timeline.reset();
    if (clipId > -1 && pCore->window() && pCore->window()->getCurrentTimeline()) {
        auto tl = pCore->window()->getCurrentTimeline()->model();
        if (tl && tl->isClip(clipId)) {
            m_timeline = tl;
            // Un changement de média (stabilisation, annulation) ou de clip rafraîchit la section.
            m_watch << connect(tl.get(), &QAbstractItemModel::dataChanged, this, &KoraCutClipToolsPanel::scheduleRefresh);
            m_watch << connect(tl.get(), &QAbstractItemModel::modelReset, this, &KoraCutClipToolsPanel::scheduleRefresh);
            m_watch << connect(tl.get(), &QAbstractItemModel::rowsRemoved, this, &KoraCutClipToolsPanel::scheduleRefresh);
        }
    }
    watchStack();
    refresh();
}

void KoraCutClipToolsPanel::watchStack()
{
    auto tl = timeline();
    const int cid = tl ? KoraCutClipTools::audioTarget(tl, m_clipId) : -1;
    if (cid < 0) {
        return;
    }
    const auto stack = tl->getClipEffectStackModel(cid);
    if (!stack) {
        return;
    }
    // Un effet retiré ou déplacé à la main dans la pile, un Annuler : l'état des outils suit.
    m_watch << connect(stack.get(), &QAbstractItemModel::rowsInserted, this, &KoraCutClipToolsPanel::scheduleRefresh);
    m_watch << connect(stack.get(), &QAbstractItemModel::rowsRemoved, this, &KoraCutClipToolsPanel::scheduleRefresh);
    m_watch << connect(stack.get(), &QAbstractItemModel::modelReset, this, &KoraCutClipToolsPanel::scheduleRefresh);
}

void KoraCutClipToolsPanel::scheduleRefresh()
{
    m_refreshTimer.start();
}

void KoraCutClipToolsPanel::refresh()
{
    auto tl = timeline();
    if (!tl || !tl->isClip(m_clipId)) {
        hide();
        return;
    }
    const int audio = KoraCutClipTools::audioTarget(tl, m_clipId);
    const int video = KoraCutClipTools::videoTarget(tl, m_clipId);
    if (audio < 0 && video < 0) {
        hide();
        return;
    }
    m_updating = true;
    m_sound->setVisible(audio > -1);
    if (audio > -1) {
        const QString denoise = KoraCutClipTools::current(tl, audio, Tool::Denoise);
        m_denoiseSwitch->setChecked(!denoise.isEmpty());
        m_denoiseLevels->setVisible(!denoise.isEmpty());
        for (QAbstractButton *b : m_denoiseGroup->buttons()) {
            b->setChecked(b->property("koracutChoice").toString() == denoise);
        }
        m_enhanceSwitch->setChecked(!KoraCutClipTools::current(tl, audio, Tool::Enhance).isEmpty());
        const QString effect = KoraCutClipTools::current(tl, audio, Tool::VoiceEffect);
        for (QToolButton *card : std::as_const(m_effectCards)) {
            card->setChecked(card->property("koracutChoice").toString() == effect);
        }
        m_effectClear->setVisible(!effect.isEmpty());
    }
    m_image->setVisible(video > -1);
    if (video > -1) {
        auto *stab = KoraCutStabilizer::instance();
        const int percent = stab->progress(tl, video);
        const QString level = percent > -1 ? stab->pendingLevel(tl, video) : stab->currentLevel(tl, video);
        m_stabilizeSwitch->setChecked(!level.isEmpty());
        m_stabilizeLevels->setVisible(!level.isEmpty());
        for (QAbstractButton *b : m_stabilizeGroup->buttons()) {
            b->setChecked(b->property("koracutChoice").toString() == level);
        }
        m_stabilizeProgressRow->setVisible(percent > -1);
        if (percent > -1) {
            m_stabilizeProgress->setValue(percent);
            m_stabilizeState->setText(i18nc("@info", "Analyzing the movement… %1%", percent));
        }
    }
    m_updating = false;
    show();
}

void KoraCutClipToolsPanel::applySound(int tool, const QString &variant)
{
    auto tl = timeline();
    const int audio = tl ? KoraCutClipTools::audioTarget(tl, m_clipId) : -1;
    if (audio < 0) {
        return;
    }
    const KoraCutClipTools::Result r = KoraCutClipTools::apply(tl, audio, Tool(tool), variant);
    if (!r.ok) {
        pCore->displayMessage(r.message, ErrorMessage, 6000);
    } else if (!r.missing.isEmpty()) {
        pCore->displayMessage(i18nc("@info", "Some effects are missing on this computer and were skipped: %1.", r.missing.join(QStringLiteral(", "))),
                              InformationMessage, 6000);
    }
    // La pile a pu être remplacée (premier effet d'un clip) : on la resuit.
    showClip(m_clipId);
}

void KoraCutClipToolsPanel::normalize()
{
    auto tl = timeline();
    const int audio = tl ? KoraCutClipTools::audioTarget(tl, m_clipId) : -1;
    if (audio < 0) {
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QJsonObject r = KoraCutAgentTools::call(
        tl, QStringLiteral("normaliser_loudness"),
        {{QStringLiteral("cible"), QStringLiteral("clip")}, {QStringLiteral("clip_id"), audio}, {QStringLiteral("lufs_cible"), -16}});
    QApplication::restoreOverrideCursor();
    if (r.value(QStringLiteral("ok")).toBool()) {
        pCore->displayMessage(i18nc("@info", "Volume normalized: %1 LUFS before, %2 LUFS now.", r.value(QStringLiteral("lufs_avant")).toDouble(),
                                    r.value(QStringLiteral("lufs_apres")).toDouble()),
                              OperationCompletedMessage, 5000);
    } else {
        pCore->displayMessage(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("message")).toString(), ErrorMessage, 6000);
    }
    showClip(m_clipId);
}

void KoraCutClipToolsPanel::stabilize(const QString &level)
{
    auto tl = timeline();
    if (!tl) {
        return;
    }
    auto *stab = KoraCutStabilizer::instance();
    const KoraCutStabilizer::Status s = level.isEmpty() ? stab->remove(tl, m_clipId) : stab->start(tl, m_clipId, level);
    if (!s.ok) {
        pCore->displayMessage(s.message, ErrorMessage, 6000);
    }
    scheduleRefresh();
}
