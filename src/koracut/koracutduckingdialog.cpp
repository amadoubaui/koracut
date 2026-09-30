/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutduckingdialog.h"
#include "koracutducking.h"
#include "koracutpreferences.h"
#include "koracuttimelineaudio.h"

#include "lib/audio/audioStreamInfo.h"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"
#include "mainwindow.h"
#include "definitions.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "project/projectmanager.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

enum Role { Ignore = 0, Voice = 1, Music = 2 };
const char *DuckingProperty = "kdenlive:koracut_ducking";

KConfigGroup settingsGroup()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut Ducking"));
}

/** Rôle deviné d'après le nom de la piste. Sans indice : A1 voix, A2 musique, le reste ignoré. */
Role guessRole(const QString &name, int audioIndex)
{
    const QString n = name.toLower();
    for (const char *k : {"voix", "voice", "dialog", "parole", "interview", "narration"}) {
        if (n.contains(QLatin1String(k))) {
            return Voice;
        }
    }
    for (const char *k : {"musique", "music", "zik", "beat", "fond", "bgm"}) {
        if (n.contains(QLatin1String(k))) {
            return Music;
        }
    }
    return audioIndex == 0 ? Voice : (audioIndex == 1 ? Music : Ignore);
}


/** Retire de la pile de @p clipId les effets posés par la baisse automatique. */
int removeDuckingEffects(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, Fun &undo, Fun &redo)
{
    auto stack = timeline->getClipEffectStackModel(clipId);
    if (!stack) {
        return 0;
    }
    int removed = 0;
    for (int row = stack->rowCount() - 1; row >= 0; --row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect && effect->filter().get_int(DuckingProperty) == 1) {
            QString name;
            stack->removeEffectWithUndo(effect, name, undo, redo);
            ++removed;
        }
    }
    return removed;
}

}

KoraCutDuckingDialog::KoraCutDuckingDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Automatic Music Ducking"));
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(i18nc("@info",
                                   "KoraCut finds where someone speaks on the voice tracks and lowers the music there. Each music clip gets a keyframed Volume effect that you can adjust afterwards."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // KoraCut, 25 septembre 2026 : des cartes, comme les Préférences (libellé à gauche, contrôle à
    // droite), et un nom stable par champ pour la porte de commande (baisse.piste.A1, baisse.niveau…).
    setObjectName(QStringLiteral("koracutDuckingDialog"));
    KoraCutPrefsColors::applyCardWindow(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(20);
    intro->setObjectName(QStringLiteral("koracutPrefsHelp"));

    // Les pistes audio de la séquence ouverte, avec leur rôle.
    auto *tracksCard = new KoraCutPrefsCard(i18nc("@title:group", "Tracks"), this);
    tracksCard->setIdPrefix(QStringLiteral("baisse."));
    auto timeline = pCore->projectManager() ? pCore->projectManager()->getTimeline() : nullptr;
    if (timeline) {
        // Seulement les pistes qui ont des clips : dix lignes « Ignorer » noieraient les deux qui comptent.
        QList<int> tracks;
        const QList<int> allTracks = timeline->getTracksIds(true);
        for (int tid : allTracks) {
            if (!KoraCutTimelineAudio::clipsOnTrack(timeline, tid).isEmpty()) {
                tracks << tid;
            }
        }
        if (tracks.isEmpty()) {
            tracks = allTracks;
        }
        const KConfigGroup group = settingsGroup();
        int index = 0;
        for (int tid : tracks) {
            const QString tag = timeline->getTrackTagById(tid);
            const QString name = timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString();
            auto *role = new QComboBox;
            role->setMinimumWidth(160);
            role->addItem(i18nc("@item:inlistbox rôle d'une piste", "Ignore"), Ignore);
            role->addItem(i18nc("@item:inlistbox rôle d'une piste", "Voice"), Voice);
            role->addItem(i18nc("@item:inlistbox rôle d'une piste", "Music"), Music);
            const int saved = group.readEntry(QStringLiteral("role_%1").arg(tag), -1);
            role->setCurrentIndex(role->findData(saved >= 0 ? saved : int(guessRole(name, index))));
            const QString label = name.isEmpty() ? tag : QStringLiteral("%1 · %2").arg(tag, name);
            tracksCard->addRow(label, role, QString(), QStringLiteral("piste.%1").arg(tag));
            m_roles.insert(tid, role);
            ++index;
        }
    }
    if (m_roles.isEmpty()) {
        tracksCard->addBlock(new QLabel(i18nc("@info", "No audio track in this sequence.")));
    }
    layout->addWidget(tracksCard);

    // Les réglages. Les valeurs par défaut sont celles d'un montage parlé courant.
    auto *settingsCard = new KoraCutPrefsCard(i18nc("@title:group", "Settings"), this);
    settingsCard->setIdPrefix(QStringLiteral("baisse."));
    auto makeMs = [](int max) {
        auto *s = new QSpinBox;
        s->setRange(0, max);
        s->setSingleStep(50);
        s->setSuffix(i18nc("@item:valuesuffix millisecondes", " ms"));
        return s;
    };
    m_duck = new QDoubleSpinBox;
    m_duck->setRange(1., 40.);
    m_duck->setDecimals(1);
    m_duck->setSuffix(i18nc("@item:valuesuffix décibels", " dB"));
    m_threshold = new QDoubleSpinBox;
    m_threshold->setRange(-70., -10.);
    m_threshold->setDecimals(0);
    m_threshold->setSuffix(i18nc("@item:valuesuffix décibels", " dB"));
    m_anticipation = makeMs(2000);
    m_hold = makeMs(5000);
    m_fade = makeMs(3000);
    m_mergeGap = makeMs(5000);
    for (QAbstractSpinBox *box : {static_cast<QAbstractSpinBox *>(m_duck), static_cast<QAbstractSpinBox *>(m_threshold),
                                  static_cast<QAbstractSpinBox *>(m_anticipation), static_cast<QAbstractSpinBox *>(m_hold),
                                  static_cast<QAbstractSpinBox *>(m_fade), static_cast<QAbstractSpinBox *>(m_mergeGap)}) {
        box->setMinimumWidth(110);
        box->setAlignment(Qt::AlignRight);
    }
    settingsCard->addRow(i18nc("@label:spinbox", "Lower the music by"), m_duck, QString(), QStringLiteral("niveau"));
    settingsCard->addRow(i18nc("@label:spinbox", "Voice detected above"), m_threshold,
                         i18nc("@info:tooltip", "Raise this threshold if the music lowers on background noise, lower it if words are missed."),
                         QStringLiteral("seuil"));
    settingsCard->addRow(i18nc("@label:spinbox", "Start lowering before the voice"), m_anticipation, QString(), QStringLiteral("avance"));
    settingsCard->addRow(i18nc("@label:spinbox", "Stay low after the voice"), m_hold, QString(), QStringLiteral("maintien"));
    settingsCard->addRow(i18nc("@label:spinbox", "Duration of the fade down and up"), m_fade, QString(), QStringLiteral("fondu"));
    settingsCard->addRow(i18nc("@label:spinbox", "Do not come back up for a silence shorter than"), m_mergeGap,
                         i18nc("@info:tooltip", "Keeps the music from coming back up at every breath."), QStringLiteral("silence"));
    layout->addWidget(settingsCard);

    const KConfigGroup group = settingsGroup();
    const KoraCutDucking::Settings defaults;
    m_duck->setValue(group.readEntry("duckDb", defaults.duckDb));
    m_threshold->setValue(group.readEntry("thresholdDb", defaults.thresholdDb));
    m_anticipation->setValue(group.readEntry("anticipationMs", defaults.anticipationMs));
    m_hold->setValue(group.readEntry("holdMs", defaults.holdMs));
    m_fade->setValue(group.readEntry("fadeMs", defaults.fadeMs));
    m_mergeGap->setValue(group.readEntry("mergeGapMs", defaults.mergeGapMs));

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    layout->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *applyButton = buttons->addButton(i18nc("@action:button", "Analyze and Lower the Music"), QDialogButtonBox::AcceptRole);
    QPushButton *removeButton = buttons->addButton(i18nc("@action:button", "Remove the Ducking"), QDialogButtonBox::ActionRole);
    QPushButton *defaultsButton = buttons->addButton(i18nc("@action:button", "Default Settings"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Close);
    applyButton->setDefault(true);
    applyButton->setEnabled(timeline != nullptr);
    removeButton->setEnabled(timeline != nullptr);
    // Appliquer ne ferme pas la fenêtre : on écoute, on ajuste, on relance.
    disconnect(buttons, &QDialogButtonBox::accepted, nullptr, nullptr);
    connect(applyButton, &QPushButton::clicked, this, &KoraCutDuckingDialog::apply);
    connect(removeButton, &QPushButton::clicked, this, &KoraCutDuckingDialog::removeDucking);
    connect(defaultsButton, &QPushButton::clicked, this, &KoraCutDuckingDialog::restoreDefaults);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void KoraCutDuckingDialog::restoreDefaults()
{
    const KoraCutDucking::Settings d;
    m_duck->setValue(d.duckDb);
    m_threshold->setValue(d.thresholdDb);
    m_anticipation->setValue(d.anticipationMs);
    m_hold->setValue(d.holdMs);
    m_fade->setValue(d.fadeMs);
    m_mergeGap->setValue(d.mergeGapMs);
}

void KoraCutDuckingDialog::saveSettings()
{
    KConfigGroup group = settingsGroup();
    group.writeEntry("duckDb", m_duck->value());
    group.writeEntry("thresholdDb", m_threshold->value());
    group.writeEntry("anticipationMs", m_anticipation->value());
    group.writeEntry("holdMs", m_hold->value());
    group.writeEntry("fadeMs", m_fade->value());
    group.writeEntry("mergeGapMs", m_mergeGap->value());
    auto timeline = pCore->projectManager()->getTimeline();
    for (auto it = m_roles.cbegin(); it != m_roles.cend(); ++it) {
        const int role = it.value()->currentData().toInt();
        group.writeEntry(QStringLiteral("role_%1").arg(timeline->getTrackTagById(it.key())), role);
        // KoraCut, 23 septembre 2026 : le rôle suit aussi la piste dans le projet, pour la couleur
        // des clips (musique en orange, voix en vert ; TimelineController::audioColorForTrack).
        timeline->setTrackProperty(it.key(), QStringLiteral("kdenlive:koracut_role"),
                                   role == Music ? QStringLiteral("music") : role == Voice ? QStringLiteral("voice") : QString());
    }
    group.sync();
    if (pCore->window() && pCore->window()->getCurrentTimeline()) {
        pCore->window()->getCurrentTimeline()->controller()->refreshTrackColors();
    }
}

void KoraCutDuckingDialog::removeDucking()
{
    auto timeline = pCore->projectManager()->getTimeline();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    int removed = 0;
    for (auto it = m_roles.cbegin(); it != m_roles.cend(); ++it) {
        for (int cid : KoraCutTimelineAudio::clipsOnTrack(timeline, it.key())) {
            removed += removeDuckingEffects(timeline, cid, undo, redo);
        }
    }
    if (removed > 0) {
        pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Remove Automatic Music Ducking"));
        m_result->setText(i18ncp("@info", "Ducking removed on %1 clip.", "Ducking removed on %1 clips.", removed));
    } else {
        m_result->setText(i18nc("@info", "No automatic ducking to remove."));
    }
}

void KoraCutDuckingDialog::apply()
{
    saveSettings();
    auto timeline = pCore->projectManager()->getTimeline();
    const double fps = pCore->getCurrentFps();
    KoraCutDucking::Settings settings;
    settings.duckDb = m_duck->value();
    settings.thresholdDb = m_threshold->value();
    settings.anticipationMs = m_anticipation->value();
    settings.holdMs = m_hold->value();
    settings.fadeMs = m_fade->value();
    settings.mergeGapMs = m_mergeGap->value();

    QList<int> voiceTracks, musicTracks;
    for (auto it = m_roles.cbegin(); it != m_roles.cend(); ++it) {
        const int role = it.value()->currentData().toInt();
        if (role == Voice) {
            voiceTracks << it.key();
        } else if (role == Music) {
            musicTracks << it.key();
        }
    }
    if (voiceTracks.isEmpty() || musicTracks.isEmpty()) {
        m_result->setText(i18nc("@info", "Choose at least one voice track and one music track."));
        return;
    }

    // 1. Le niveau de voix à chaque image de la timeline : le plus fort de toutes les pistes de voix.
    int missingLevels = 0;
    const QVector<double> voiceDb = KoraCutTimelineAudio::voiceLevels(timeline, voiceTracks, &missingLevels);
    const QList<KoraCutDucking::Interval> ducked = KoraCutDucking::duckedIntervals(voiceDb, settings, fps);

    // 2. Sur chaque clip de musique : on retire l'ancienne baisse, on pose la nouvelle.
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    int touchedClips = 0;
    for (int tid : std::as_const(musicTracks)) {
        for (int cid : KoraCutTimelineAudio::clipsOnTrack(timeline, tid)) {
            removeDuckingEffects(timeline, cid, undo, redo);
            const auto keyframes =
                KoraCutDucking::clipKeyframes(ducked, timeline->getClipPosition(cid), timeline->getClipPlaytime(cid), settings, fps);
            if (keyframes.isEmpty()) {
                continue;
            }
            auto stack = timeline->getClipEffectStackModel(cid);
            if (!stack) {
                continue;
            }
            QMap<QString, QString> params;
            params.insert(QStringLiteral("level"), KoraCutDucking::animationString(keyframes, timeline->getClipIn(cid)));
            params.insert(QString::fromLatin1(DuckingProperty), QStringLiteral("1"));
            if (stack->appendEffectWithParams(QStringLiteral("volume"), params, undo, redo)) {
                ++touchedClips;
            }
        }
    }
    if (touchedClips > 0) {
        pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Automatic Music Ducking"));
    }
    QString text;
    if (ducked.isEmpty()) {
        text = i18nc("@info", "No voice detected above %1 dB. Lower the threshold, or check the voice tracks.", m_threshold->value());
    } else {
        text = i18ncp("@info", "%1 voice passage, ", "%1 voice passages, ", ducked.size()) +
               i18ncp("@info", "music lowered on %1 clip.", "music lowered on %1 clips.", touchedClips);
    }
    if (missingLevels > 0) {
        text += QLatin1Char(' ') + i18ncp("@info", "%1 voice clip does not have its audio levels yet: wait for the analysis to finish, then try again.",
                                          "%1 voice clips do not have their audio levels yet: wait for the analysis to finish, then try again.",
                                          missingLevels);
    }
    m_result->setText(text);
}
