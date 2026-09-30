/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutautocutdialog.h"
#include "koracutautocut.h"
#include "koracutducking.h"
#include "koracuttimelineaudio.h"
#include "koracuttranscriber.h"

#include "bin/model/markerlistmodel.hpp"
#include "core.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "project/projectmanager.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

// Les marqueurs de la coupe automatique : catégorie 9 (rouge) des catégories par défaut, et un
// commentaire qui commence toujours pareil, pour ne jamais toucher aux marqueurs de l'utilisateur.
constexpr int SilenceMarkerType = 8;   // rouge
constexpr int FillerMarkerType = 6;    // orange
constexpr int AmbiguousMarkerType = 4; // jaune : à vérifier
constexpr int RepeatMarkerType = 0;    // violet : prise refaite
const QString &markerPrefix()
{
    static const QString prefix = QStringLiteral("Coupe auto · ");
    return prefix;
}
const char *MuteProperty = "kdenlive:koracut_autocut_mute";

KConfigGroup settingsGroup()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut Coupe automatique"));
}

QString seconds(int frames, double fps)
{
    return QLocale().toString(frames / fps, 'f', 1);
}

/** Les marqueurs posés par la coupe automatique dans la séquence. */
QList<CommentedTime> ourMarkers(const std::shared_ptr<TimelineItemModel> &timeline)
{
    QList<CommentedTime> ours;
    const QList<CommentedTime> all = timeline->getGuideModel()->getAllMarkers();
    for (const CommentedTime &m : all) {
        if (m.comment().startsWith(markerPrefix()) && m.duration() > GenTime()) {
            ours << m;
        }
    }
    return ours;
}

/** Retire de la pile d'un clip les effets muets posés par la coupe automatique. */
void removeMuteEffects(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, Fun &undo, Fun &redo)
{
    auto stack = timeline->getClipEffectStackModel(clipId);
    if (!stack) {
        return;
    }
    for (int row = stack->rowCount() - 1; row >= 0; --row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect && effect->filter().get_int(MuteProperty) == 1) {
            QString name;
            stack->removeEffectWithUndo(effect, name, undo, redo);
        }
    }
}

}

KoraCutAutoCutDialog::KoraCutAutoCutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Automatic Cut"));
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(i18nc("@info", "KoraCut syncs the sources, picks the cameras, then finds silences, “ums” and retakes. By default it marks them without cutting anything: you listen, remove the marks of what you keep, then cut what remains."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    // Deux onglets, sinon la fenêtre dépasse la hauteur d'un écran de portable.
    auto *tabs = new QTabWidget(this);
    auto *sourcesPage = new QWidget(tabs);
    auto *sourcesLayout = new QVBoxLayout(sourcesPage);
    auto *searchPage = new QWidget(tabs);
    auto *searchLayout = new QVBoxLayout(searchPage);
    tabs->addTab(sourcesPage, i18nc("@title:tab", "Sources"));
    tabs->addTab(searchPage, i18nc("@title:tab", "What to look for"));
    layout->addWidget(tabs);

    auto timeline = pCore->projectManager() ? pCore->projectManager()->getTimeline() : nullptr;
    const KConfigGroup group = settingsGroup();

    // Les pistes de voix : celles qui ont des clips, toutes cochées la première fois.
    auto *tracksBox = new QGroupBox(i18nc("@title:group", "Voice tracks to listen to"), this);
    auto *tracksLayout = new QVBoxLayout(tracksBox);
    if (timeline) {
        const QList<int> tracks = timeline->getTracksIds(true);
        for (int tid : tracks) {
            if (KoraCutTimelineAudio::clipsOnTrack(timeline, tid).isEmpty()) {
                continue;
            }
            const QString tag = timeline->getTrackTagById(tid);
            const QString name = timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString();
            auto *box = new QCheckBox(name.isEmpty() ? tag : QStringLiteral("%1 · %2").arg(tag, name), tracksBox);
            box->setChecked(group.readEntry(QStringLiteral("piste_%1").arg(tag), true));
            tracksLayout->addWidget(box);
            m_tracks.insert(tid, box);
        }
    }
    if (m_tracks.isEmpty()) {
        tracksLayout->addWidget(new QLabel(i18nc("@info", "No audio track with clips in this sequence."), tracksBox));
    }
    sourcesLayout->addWidget(tracksBox);

    // La synchronisation, avant tout le reste (lot 4).
    auto *syncBox = new QGroupBox(i18nc("@title:group", "Sync"), this);
    auto *syncLayout = new QVBoxLayout(syncBox);
    m_sync = new QCheckBox(i18nc("@option:check", "First sync the microphones and cameras by their sound"), syncBox);
    m_sync->setToolTip(i18nc("@info:tooltip", "The first clip of the first checked voice track is the reference. Every other clip on the audio tracks is aligned to it (its video follows). A clip whose sound does not match the reference closely enough does not move."));
    syncLayout->addWidget(m_sync);
    sourcesLayout->addWidget(syncBox);

    // Le choix de la caméra selon qui parle (lot 5).
    auto *camerasBox = new QGroupBox(i18nc("@title:group", "Cameras"), this);
    auto *camerasForm = new QFormLayout(camerasBox);
    m_cameras = new QCheckBox(i18nc("@option:check", "Choose the camera by who is speaking"), camerasBox);
    m_cameras->setToolTip(i18nc("@info:tooltip", "The camera tracks are cut at each turn of speech, and only the camera of the speaker stays enabled. Nothing is deleted: a disabled shot comes back with one click."));
    camerasForm->addRow(m_cameras);
    // L'ordre qu'on lit à l'écran, pas celui des identifiants : V1 (la plus basse) d'abord, et A1
    // (la plus haute des pistes audio) d'abord. A1 va avec V1, A2 avec V2.
    QList<int> videoTracks;
    QList<int> voiceOrder = m_tracks.keys();
    if (timeline) {
        for (int tid : timeline->getTracksIds(false)) {
            if (!KoraCutTimelineAudio::clipsOnTrack(timeline, tid).isEmpty()) {
                videoTracks << tid;
            }
        }
        std::sort(videoTracks.begin(), videoTracks.end(), [&timeline](int a, int b) { return timeline->getTrackPosition(a) < timeline->getTrackPosition(b); });
        std::sort(voiceOrder.begin(), voiceOrder.end(), [&timeline](int a, int b) { return timeline->getTrackPosition(a) > timeline->getTrackPosition(b); });
    }
    auto fillVideo = [&](QComboBox *combo, bool withNone) {
        if (withNone) {
            combo->addItem(i18nc("@item:inlistbox pas de caméra", "None"), -1);
        }
        for (int tid : std::as_const(videoTracks)) {
            const QString name = timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString();
            const QString tag = timeline->getTrackTagById(tid);
            combo->addItem(name.isEmpty() ? tag : QStringLiteral("%1 · %2").arg(tag, name), tid);
        }
    };
    for (int index = 0; index < voiceOrder.size(); ++index) {
        const int voice = voiceOrder.at(index);
        auto *combo = new QComboBox(camerasBox);
        fillVideo(combo, true);
        // Par défaut, la n-ième piste de voix va avec la n-ième piste vidéo.
        combo->setCurrentIndex(index < videoTracks.size() ? index + 1 : 0);
        camerasForm->addRow(i18nc("@label:listbox", "Camera of %1", m_tracks.value(voice)->text()), combo);
        m_cameraFor.insert(voice, combo);
    }
    m_wide = new QComboBox(camerasBox);
    fillVideo(m_wide, true);
    // Une piste vidéo de plus que de voix : c'est sans doute le plan large.
    m_wide->setCurrentIndex(videoTracks.size() > m_tracks.size() ? m_tracks.size() + 1 : 0);
    camerasForm->addRow(i18nc("@label:listbox", "Wide shot"), m_wide);
    auto makeSeconds = [camerasBox](double value) {
        auto *s = new QDoubleSpinBox(camerasBox);
        s->setRange(0.5, 60.);
        s->setDecimals(1);
        s->setSingleStep(0.5);
        s->setSuffix(i18nc("@item:valuesuffix secondes", " s"));
        s->setValue(value);
        return s;
    };
    m_minShot = makeSeconds(2.);
    m_maxShot = makeSeconds(10.);
    m_maxShot->setToolTip(i18nc("@info:tooltip", "Beyond this, cut to the wide shot for a moment, if there is one."));
    camerasForm->addRow(i18nc("@label:spinbox", "Shortest shot"), m_minShot);
    camerasForm->addRow(i18nc("@label:spinbox", "Longest shot"), m_maxShot);
    const bool enoughCameras = videoTracks.size() >= 2 && m_tracks.size() >= 2;
    m_cameras->setEnabled(enoughCameras);
    if (!enoughCameras) {
        m_cameras->setToolTip(i18nc("@info:tooltip", "You need at least two voice tracks and two video tracks with clips."));
    }
    sourcesLayout->addWidget(camerasBox);
    sourcesLayout->addStretch();

    // Les réglages des silences.
    auto *settingsBox = new QGroupBox(i18nc("@title:group", "Silences"), this);
    auto *form = new QFormLayout(settingsBox);
    m_rythme = new QComboBox(settingsBox);
    m_rythme->addItem(i18nc("@item:inlistbox rythme de coupe", "Relaxed: lets it breathe"));
    m_rythme->addItem(i18nc("@item:inlistbox rythme de coupe", "Normal"));
    m_rythme->addItem(i18nc("@item:inlistbox rythme de coupe", "Punchy: cuts tight"));
    m_rythme->addItem(i18nc("@item:inlistbox rythme de coupe", "Custom"));
    form->addRow(i18nc("@label:listbox", "Pace"), m_rythme);

    m_autoThreshold = new QCheckBox(i18nc("@option:check", "Automatic threshold, measured on the background noise"), settingsBox);
    m_threshold = new QDoubleSpinBox(settingsBox);
    m_threshold->setRange(-70., -10.);
    m_threshold->setDecimals(0);
    m_threshold->setSuffix(i18nc("@item:valuesuffix décibels", " dB"));
    m_threshold->setToolTip(i18nc("@info:tooltip", "Below this level, it is silence. Raise it if room noise keeps the silences from being found."));
    form->addRow(QString(), m_autoThreshold);
    form->addRow(i18nc("@label:spinbox", "Silence below"), m_threshold);
    auto makeMs = [settingsBox](int max) {
        auto *s = new QSpinBox(settingsBox);
        s->setRange(0, max);
        s->setSingleStep(50);
        s->setSuffix(i18nc("@item:valuesuffix millisecondes", " ms"));
        s->setMinimumWidth(110);
        s->setAlignment(Qt::AlignRight);
        return s;
    };
    m_minSilence = makeMs(5000);
    m_minSpeech = makeMs(2000);
    m_padBefore = makeMs(2000);
    m_padAfter = makeMs(2000);
    form->addRow(i18nc("@label:spinbox", "Cut silences longer than"), m_minSilence);
    form->addRow(i18nc("@label:spinbox", "Ignore sounds shorter than"), m_minSpeech);
    form->addRow(i18nc("@label:spinbox", "Keep before speech"), m_padBefore);
    form->addRow(i18nc("@label:spinbox", "Keep after speech"), m_padAfter);
    m_minSpeech->setToolTip(i18nc("@info:tooltip", "A breath, a mouse click: too short to be speech."));
    m_padAfter->setToolTip(i18nc("@info:tooltip", "Let the sentence and the room echo finish, or the cut sounds abrupt."));
    searchLayout->addWidget(settingsBox);

    // Les mots parasites (lot 2) : transcription au mot, en tâche de fond.
    auto *fillersBox = new QGroupBox(i18nc("@title:group", "Filler words and retakes"), this);
    auto *fillersLayout = new QVBoxLayout(fillersBox);
    m_fillers = new QCheckBox(i18nc("@option:check", "Look for “um”, “hmm” and hesitations"), fillersBox);
    m_ambiguous = new QCheckBox(i18nc("@option:check", "Also flag words to check (“so”, “well”, “actually”…)"), fillersBox);
    m_repeats = new QCheckBox(i18nc("@option:check", "Look for retakes (the last one is kept)"), fillersBox);
    m_repeats->setToolTip(i18nc("@info:tooltip", "A sentence of at least three words said again within 15 seconds: the first take and what separates it from the retake are proposed for cutting."));
    // Les listes de mots, séparés par des virgules, modifiables : chacun a ses tics de langage.
    const KoraCutAutoCut::FillerSettings defaults;
    auto *wordsForm = new QFormLayout;
    m_sureWords = new QLineEdit(defaults.sure.join(QStringLiteral(", ")), fillersBox);
    m_suspectWords = new QLineEdit(defaults.suspects.join(QStringLiteral(", ")), fillersBox);
    m_ambiguousWords = new QLineEdit(defaults.ambiguous.join(QStringLiteral(", ")), fillersBox);
    m_sureWords->setToolTip(i18nc("@info:tooltip", "Always cut when found."));
    m_suspectWords->setToolTip(i18nc("@info:tooltip", "Real words that Whisper writes instead of an “um”: cut only when short and isolated between two pauses."));
    m_ambiguousWords->setToolTip(i18nc("@info:tooltip", "Marked in yellow so you decide. A phrase of several words works too (“you know”)."));
    wordsForm->addRow(i18nc("@label:textbox", "Filler words"), m_sureWords);
    wordsForm->addRow(i18nc("@label:textbox", "Suspect words"), m_suspectWords);
    wordsForm->addRow(i18nc("@label:textbox", "Words to check"), m_ambiguousWords);
    m_repeatWindow = new QSpinBox(fillersBox);
    m_repeatWindow->setRange(3, 120);
    m_repeatWindow->setSuffix(i18nc("@item:valuesuffix secondes", " s"));
    m_repeatWindow->setValue(15);
    m_repeatWindow->setToolTip(i18nc("@info:tooltip", "The retake must start within this delay after the first take."));
    m_repeatMismatches = new QSpinBox(fillersBox);
    m_repeatMismatches->setRange(0, 3);
    m_repeatMismatches->setValue(0);
    m_repeatMismatches->setToolTip(i18nc("@info:tooltip", "Number of words that may change between the two takes (“we will talk” then “we will chat”)."));
    wordsForm->addRow(i18nc("@label:spinbox", "Retake within"), m_repeatWindow);
    wordsForm->addRow(i18nc("@label:spinbox", "Different words allowed"), m_repeatMismatches);
    m_modelLabel = new QLabel(fillersBox);
    m_modelLabel->setWordWrap(true);
    m_modelLabel->setEnabled(false);
    const QString model = KoraCutTranscriber::chooseModel();
    m_modelLabel->setText(model.isEmpty() ? i18nc("@info", "No speech recognition model installed: Settings > Speech Recognition.")
                                          : i18nc("@info", "Local transcription with the “%1” model. Expect about a quarter of the sequence length the first time; after that it is kept in memory.",
                                                  KoraCutTranscriber::modelName(model)));
    m_fillers->setEnabled(!model.isEmpty());
    m_repeats->setEnabled(!model.isEmpty());
    // Pas de modèle : on propose de le télécharger ici, sans passer par les Préférences.
    auto *download = new QPushButton(i18nc("@action:button", "Download the “base” model (145 MB)"), fillersBox);
    download->setVisible(model.isEmpty());
    download->setAutoDefault(false);
    fillersLayout->addWidget(download);
    fillersLayout->addWidget(m_fillers);
    fillersLayout->addWidget(m_ambiguous);
    fillersLayout->addWidget(m_repeats);
    fillersLayout->addLayout(wordsForm);
    fillersLayout->addWidget(m_modelLabel);
    searchLayout->addWidget(fillersBox);
    searchLayout->addStretch();
    m_transcriber = new KoraCutTranscriber(this);
    connect(download, &QPushButton::clicked, this, [this, download]() {
        download->setEnabled(false);
        m_modelLabel->setText(i18nc("@info", "Downloading the model…"));
        m_transcriber->downloadModel();
    });
    connect(m_transcriber, &KoraCutTranscriber::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            m_modelLabel->setText(i18nc("@info", "Downloading the model: %1 %", qRound(100. * received / total)));
        }
    });
    connect(m_transcriber, &KoraCutTranscriber::downloadFinished, this, [this, download](const QString &error) {
        if (!error.isEmpty()) {
            m_modelLabel->setText(error);
            download->setEnabled(true);
            return;
        }
        download->hide();
        m_fillers->setEnabled(true);
        m_repeats->setEnabled(true);
        m_fillers->setChecked(true);
        m_repeats->setChecked(true);
        m_modelLabel->setText(i18nc("@info", "“base” model installed. Local transcription; the first time takes about a quarter of the sequence length."));
    });
    connect(m_transcriber, &KoraCutTranscriber::progress, this, [this](const QString &message) { m_result->setText(message); });
    connect(m_transcriber, &KoraCutTranscriber::failed, this, [this](const QString &message) {
        setBusy(false);
        m_result->setText(message);
    });
    connect(m_transcriber, &KoraCutTranscriber::finished, this, [this](const QList<KoraCutAutoCut::Word> &words) {
        setBusy(false);
        applyResults(words);
    });

    // Ce qu'on en fait.
    auto *actionBox = new QGroupBox(i18nc("@title:group", "What to do with what is found"), this);
    auto *actionForm = new QFormLayout(actionBox);
    m_mode = new QComboBox(actionBox);
    m_mode->addItem(i18nc("@item:inlistbox", "Mark them for review (nothing is cut)"));
    m_mode->addItem(i18nc("@item:inlistbox", "Cut and close the gaps"));
    m_mode->addItem(i18nc("@item:inlistbox", "Cut and leave the gaps"));
    m_mode->addItem(i18nc("@item:inlistbox", "Mute them"));
    m_mode->setToolTip(i18nc("@info:tooltip", "Muting keeps the length and the picture: useful when filming, to remove room noise between sentences."));
    actionForm->addRow(i18nc("@label:listbox", "Action"), m_mode);
    m_moveGuides = new QCheckBox(i18nc("@option:check", "Guides follow the cuts"), actionBox);
    actionForm->addRow(QString(), m_moveGuides);
    layout->addWidget(actionBox);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    layout->addWidget(m_result);

    // Valeurs enregistrées, ou celles du rythme Normal.
    const int rythme = group.readEntry("rythme", 1);
    m_rythme->setCurrentIndex(rythme);
    applyRythme(rythme);
    if (rythme == 3) {
        m_minSilence->setValue(group.readEntry("minSilenceMs", 500));
        m_minSpeech->setValue(group.readEntry("minSpeechMs", 200));
        m_padBefore->setValue(group.readEntry("padBeforeMs", 100));
        m_padAfter->setValue(group.readEntry("padAfterMs", 150));
    }
    m_autoThreshold->setChecked(group.readEntry("seuilAuto", true));
    m_threshold->setValue(group.readEntry("seuilDb", -40.));
    m_threshold->setEnabled(!m_autoThreshold->isChecked());
    m_mode->setCurrentIndex(group.readEntry("action", int(Marquer)));
    m_moveGuides->setChecked(group.readEntry("guidesSuivent", true));
    m_fillers->setChecked(m_fillers->isEnabled() && group.readEntry("motsParasites", true));
    m_ambiguous->setChecked(group.readEntry("motsAVerifier", true));
    m_repeats->setChecked(m_repeats->isEnabled() && group.readEntry("prisesRefaites", true));
    m_sureWords->setText(group.readEntry("motsParasitesListe", m_sureWords->text()));
    m_suspectWords->setText(group.readEntry("motsSuspectsListe", m_suspectWords->text()));
    m_ambiguousWords->setText(group.readEntry("motsAVerifierListe", m_ambiguousWords->text()));
    m_repeatWindow->setValue(group.readEntry("repriseFenetre", 15));
    m_repeatMismatches->setValue(group.readEntry("repriseTolerance", 0));
    // Synchroniser par défaut dès que plusieurs pistes audio ont des clips : un podcast à plusieurs micros.
    m_sync->setChecked(group.readEntry("synchroniser", m_tracks.size() > 1));
    m_cameras->setChecked(m_cameras->isEnabled() && group.readEntry("cameras", true));
    m_minShot->setValue(group.readEntry("planMin", 2.));
    m_maxShot->setValue(group.readEntry("planMax", 10.));
    m_ambiguous->setEnabled(m_fillers->isChecked());
    connect(m_fillers, &QCheckBox::toggled, m_ambiguous, &QWidget::setEnabled);

    connect(m_rythme, &QComboBox::currentIndexChanged, this, &KoraCutAutoCutDialog::applyRythme);
    connect(m_autoThreshold, &QCheckBox::toggled, m_threshold, [this](bool on) { m_threshold->setEnabled(!on); });
    for (QSpinBox *s : {m_minSilence, m_minSpeech, m_padBefore, m_padAfter}) {
        connect(s, &QSpinBox::valueChanged, this, [this]() {
            if (!m_fillingRythme) {
                m_rythme->setCurrentIndex(3);
            }
        });
    }

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *apply = buttons->addButton(i18nc("@action:button", "Analyze"), QDialogButtonBox::AcceptRole);
    QPushButton *cut = buttons->addButton(i18nc("@action:button", "Cut What Is Marked"), QDialogButtonBox::ActionRole);
    QPushButton *clear = buttons->addButton(i18nc("@action:button", "Remove the Marks"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    apply->setDefault(true);
    m_buttons = {apply, cut, clear};
    for (QPushButton *b : std::as_const(m_buttons)) {
        b->setEnabled(timeline != nullptr && !m_tracks.isEmpty());
    }
    cut->setToolTip(i18nc("@info:tooltip", "Cuts and closes all silences still marked in red, in a single undo step."));
    disconnect(buttons, &QDialogButtonBox::accepted, nullptr, nullptr);
    connect(apply, &QPushButton::clicked, this, &KoraCutAutoCutDialog::analyseAndApply);
    connect(cut, &QPushButton::clicked, this, &KoraCutAutoCutDialog::cutMarked);
    connect(clear, &QPushButton::clicked, this, &KoraCutAutoCutDialog::removeMarks);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void KoraCutAutoCutDialog::applyRythme(int index)
{
    if (index < 0 || index > 2) {
        return;
    }
    const KoraCutAutoCut::Rythme rythmes[] = {KoraCutAutoCut::Rythme::Pose, KoraCutAutoCut::Rythme::Normal, KoraCutAutoCut::Rythme::Nerveux};
    const KoraCutAutoCut::SilenceSettings s = KoraCutAutoCut::SilenceSettings::forRythme(rythmes[index]);
    m_fillingRythme = true;
    m_minSilence->setValue(s.minSilenceMs);
    m_minSpeech->setValue(s.minSpeechMs);
    m_padBefore->setValue(s.padBeforeMs);
    m_padAfter->setValue(s.padAfterMs);
    m_fillingRythme = false;
}

void KoraCutAutoCutDialog::saveSettings()
{
    KConfigGroup group = settingsGroup();
    group.writeEntry("rythme", m_rythme->currentIndex());
    group.writeEntry("minSilenceMs", m_minSilence->value());
    group.writeEntry("minSpeechMs", m_minSpeech->value());
    group.writeEntry("padBeforeMs", m_padBefore->value());
    group.writeEntry("padAfterMs", m_padAfter->value());
    group.writeEntry("seuilAuto", m_autoThreshold->isChecked());
    group.writeEntry("seuilDb", m_threshold->value());
    group.writeEntry("action", m_mode->currentIndex());
    group.writeEntry("guidesSuivent", m_moveGuides->isChecked());
    group.writeEntry("motsParasites", m_fillers->isChecked());
    group.writeEntry("motsAVerifier", m_ambiguous->isChecked());
    group.writeEntry("prisesRefaites", m_repeats->isChecked());
    group.writeEntry("motsParasitesListe", m_sureWords->text());
    group.writeEntry("motsSuspectsListe", m_suspectWords->text());
    group.writeEntry("motsAVerifierListe", m_ambiguousWords->text());
    group.writeEntry("repriseFenetre", m_repeatWindow->value());
    group.writeEntry("repriseTolerance", m_repeatMismatches->value());
    group.writeEntry("synchroniser", m_sync->isChecked());
    group.writeEntry("cameras", m_cameras->isChecked());
    group.writeEntry("planMin", m_minShot->value());
    group.writeEntry("planMax", m_maxShot->value());
    auto timeline = pCore->projectManager()->getTimeline();
    for (auto it = m_tracks.cbegin(); it != m_tracks.cend(); ++it) {
        group.writeEntry(QStringLiteral("piste_%1").arg(timeline->getTrackTagById(it.key())), it.value()->isChecked());
    }
    group.sync();
}

void KoraCutAutoCutDialog::setBusy(bool busy)
{
    for (QPushButton *b : std::as_const(m_buttons)) {
        b->setEnabled(!busy);
    }
    setCursor(busy ? Qt::BusyCursor : Qt::ArrowCursor);
}

void KoraCutAutoCutDialog::analyseAndApply()
{
    saveSettings();
    auto timeline = pCore->projectManager()->getTimeline();
    const double fps = pCore->getCurrentFps();
    m_voiceTracks.clear();
    for (auto it = m_tracks.cbegin(); it != m_tracks.cend(); ++it) {
        if (it.value()->isChecked()) {
            m_voiceTracks << it.key();
        }
    }
    if (m_voiceTracks.isEmpty()) {
        m_result->setText(i18nc("@info", "Check at least one voice track."));
        return;
    }
    KoraCutAutoCut::SilenceSettings settings;
    settings.autoThreshold = m_autoThreshold->isChecked();
    settings.thresholdDb = m_threshold->value();
    settings.minSilenceMs = m_minSilence->value();
    settings.minSpeechMs = m_minSpeech->value();
    settings.padBeforeMs = m_padBefore->value();
    settings.padAfterMs = m_padAfter->value();

    // 0. La synchronisation, avant tout : les silences et les mots se calculent sur des sources calées.
    m_syncReport.clear();
    if (m_sync->isChecked()) {
        const QList<int> refClips = KoraCutTimelineAudio::clipsOnTrack(timeline, m_voiceTracks.first());
        QList<int> others;
        for (int tid : timeline->getTracksIds(true)) {
            others << KoraCutTimelineAudio::clipsOnTrack(timeline, tid);
        }
        if (!refClips.isEmpty() && others.size() > 1) {
            const int reference = refClips.first();
            Fun undo = []() { return true; };
            Fun redo = []() { return true; };
            bool ok = true;
            const auto outcomes = KoraCutTimelineAudio::syncToReference(timeline, reference, others, 0.5, undo, redo, &ok);
            QStringList lines;
            bool anyMoved = false;
            for (const auto &o : outcomes) {
                const QString tag = timeline->getTrackTagById(timeline->getClipTrackId(o.clipId));
                const QString percent = QString::number(qRound(o.confidence * 100));
                if (o.moved) {
                    anyMoved = anyMoved || o.deltaFrames != 0;
                    lines << (o.deltaFrames == 0 ? i18nc("@info", "%1 already aligned (%2 %)", tag, percent)
                                                 : i18nc("@info", "%1 shifted by %2 s (%3 %)", tag, QLocale().toString(o.deltaFrames / fps, 'f', 2), percent));
                } else {
                    lines << i18nc("@info", "%1 not aligned, sound too different (%2 %)", tag, percent);
                }
            }
            if (!ok) {
                m_syncReport = i18nc("@info", "Sync impossible: a clip could not be moved (locked track or space taken). ");
            } else {
                if (anyMoved) {
                    pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Sync the Sources"));
                }
                m_syncReport = i18nc("@info", "Sync: %1. ", lines.join(QStringLiteral(", ")));
            }
        }
    }

    // 0 bis. Les caméras, sur les sources calées et avant toute coupe.
    m_cameraReport.clear();
    if (m_cameras->isChecked()) {
        applyCameras();
    }

    // 1. Les silences, tout de suite : ils ne demandent que les niveaux déjà calculés.
    m_levels = KoraCutTimelineAudio::voiceLevels(timeline, m_voiceTracks, &m_missing);
    m_silences = KoraCutAutoCut::silenceIntervals(m_levels, settings, fps, &m_usedThreshold);
    if (settings.autoThreshold) {
        m_threshold->setValue(m_usedThreshold);
    }
    // 2. Les mots parasites demandent la transcription : en tâche de fond, la suite arrive par
    //    KoraCutTranscriber::finished.
    if (m_fillers->isChecked() || m_repeats->isChecked()) {
        setBusy(true);
        m_transcriber->start(timeline, m_voiceTracks);
        return;
    }
    applyResults({});
}

void KoraCutAutoCutDialog::applyResults(const QList<KoraCutAutoCut::Word> &words)
{
    auto timeline = pCore->projectManager()->getTimeline();
    const double fps = pCore->getCurrentFps();
    QList<KoraCutAutoCut::Filler> fillers;
    if (m_fillers->isChecked()) {
        KoraCutAutoCut::FillerSettings fillerSettings;
        auto list = [](const QLineEdit *edit) {
            QStringList words;
            const QStringList parts = edit->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &p : parts) {
                const QString w = p.simplified().toLower();
                if (!w.isEmpty()) {
                    words << w;
                }
            }
            return words;
        };
        fillerSettings.sure = list(m_sureWords);
        fillerSettings.suspects = list(m_suspectWords);
        fillerSettings.ambiguous = list(m_ambiguousWords);
        if (!m_ambiguous->isChecked()) {
            fillerSettings.ambiguous.clear();
        }
        fillers = KoraCutAutoCut::findFillers(words, m_levels, m_usedThreshold, fillerSettings, fps);
    }
    QList<KoraCutAutoCut::Repeat> repeats;
    if (m_repeats->isChecked()) {
        KoraCutAutoCut::RepeatSettings repeatSettings;
        repeatSettings.windowMs = m_repeatWindow->value() * 1000;
        repeatSettings.maxMismatches = m_repeatMismatches->value();
        repeats = KoraCutAutoCut::findRepeats(words, repeatSettings, fps);
    }
    // Ce qui part vraiment si l'on coupe : silences, parasites sûrs et premières prises. Les mots à vérifier ne sont
    // que marqués, jamais coupés d'office.
    QList<KoraCutAutoCut::Interval> toCut = m_silences;
    int sureFillers = 0;
    int ambiguousFillers = 0;
    for (const KoraCutAutoCut::Filler &f : std::as_const(fillers)) {
        if (f.kind == KoraCutAutoCut::FillerKind::Ambiguous) {
            ++ambiguousFillers;
        } else {
            toCut << f.frames;
            ++sureFillers;
        }
    }
    for (const KoraCutAutoCut::Repeat &r : std::as_const(repeats)) {
        toCut << r.frames;
    }
    toCut = KoraCutAutoCut::mergeIntervals(toCut);

    const QString missingText =
        m_missing > 0 ? QLatin1Char(' ') + i18ncp("@info", "%1 clip does not have its audio levels yet: wait for the analysis to finish, then try again.",
                                                  "%1 clips do not have their audio levels yet: wait for the analysis to finish, then try again.", m_missing)
                      : QString();
    if (toCut.isEmpty() && ambiguousFillers == 0 && repeats.isEmpty()) {
        m_result->setText(m_syncReport + m_cameraReport + i18nc("@info", "Nothing found with these settings (threshold %1 dB).", QLocale().toString(m_usedThreshold, 'f', 0)) + missingText);
        return;
    }
    QString summary = m_syncReport + m_cameraReport + i18ncp("@info", "%1 silence", "%1 silences", m_silences.size());
    if (m_fillers->isChecked()) {
        summary += QStringLiteral(", ") + i18ncp("@info", "%1 filler word", "%1 filler words", sureFillers);
        if (ambiguousFillers > 0) {
            summary += QStringLiteral(", ") + i18ncp("@info", "%1 word to check", "%1 words to check", ambiguousFillers);
        }
    }
    if (m_repeats->isChecked()) {
        summary += QStringLiteral(", ") + i18ncp("@info", "%1 retake", "%1 retakes", repeats.size());
    }
    summary += i18nc("@info suite du résumé", ": %1 s to remove out of %2 s (threshold %3 dB). ", seconds(KoraCutAutoCut::totalFrames(toCut), fps),
                     seconds(timeline->duration(), fps), QLocale().toString(m_usedThreshold, 'f', 0));

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const Mode mode = Mode(m_mode->currentIndex());
    if (mode == Marquer) {
        auto guides = timeline->getGuideModel();
        // Une nouvelle analyse remplace la précédente : on ne superpose pas deux séries de marques.
        for (const CommentedTime &m : ourMarkers(timeline)) {
            guides->removeMarker(m.time(), undo, redo);
        }
        for (const KoraCutAutoCut::Interval &s : std::as_const(m_silences)) {
            const int length = s.second - s.first;
            guides->addRangeMarkerWithUndo(GenTime(s.first, fps), GenTime(length, fps),
                                           markerPrefix() + i18nc("@info commentaire d'un marqueur", "silence of %1 s", seconds(length, fps)),
                                           SilenceMarkerType, undo, redo);
        }
        for (const KoraCutAutoCut::Filler &f : std::as_const(fillers)) {
            const int length = f.frames.second - f.frames.first;
            if (length <= 0) {
                continue;
            }
            QString comment;
            int type = FillerMarkerType;
            switch (f.kind) {
            case KoraCutAutoCut::FillerKind::Ambiguous:
                comment = i18nc("@info commentaire d'un marqueur", "to check: “%1”", f.text);
                type = AmbiguousMarkerType;
                break;
            case KoraCutAutoCut::FillerKind::VoiceWithoutWord:
                comment = i18nc("@info commentaire d'un marqueur", "voice without a word, probably an “um”");
                break;
            default:
                comment = i18nc("@info commentaire d'un marqueur", "“%1”", f.text);
                break;
            }
            // Un mot parasite au milieu d'un silence déjà marqué : le silence suffit.
            bool insideSilence = false;
            for (const KoraCutAutoCut::Interval &s : std::as_const(m_silences)) {
                insideSilence = insideSilence || (f.frames.first >= s.first && f.frames.second <= s.second);
            }
            if (!insideSilence) {
                guides->addRangeMarkerWithUndo(GenTime(f.frames.first, fps), GenTime(length, fps), markerPrefix() + comment, type, undo, redo);
            }
        }
        for (const KoraCutAutoCut::Repeat &r : std::as_const(repeats)) {
            const int length = r.frames.second - r.frames.first;
            if (length > 0) {
                guides->addRangeMarkerWithUndo(GenTime(r.frames.first, fps), GenTime(length, fps),
                                               markerPrefix() + i18nc("@info commentaire d'un marqueur", "retake: “%1”", r.text), RepeatMarkerType,
                                               undo, redo);
            }
        }
        pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Mark the Proposed Cuts"));
        m_result->setText(summary +
                          i18nc("@info", "Marked in the timeline: red for silences, orange for filler words, yellow for words to check, purple for retakes. Listen, delete the marks of what you keep, then “Cut What Is Marked”.") +
                          missingText);
        return;
    }
    if (mode == RendreMuet) {
        // Un effet Volume à -100 dB sur les clips des pistes de voix, avec des fondus de 30 ms pour
        // ne pas claquer. Même mécanique que la baisse automatique de la musique.
        KoraCutDucking::Settings mute;
        mute.duckDb = 100.;
        mute.fadeMs = 30;
        int touched = 0;
        for (int tid : std::as_const(m_voiceTracks)) {
            for (int cid : KoraCutTimelineAudio::clipsOnTrack(timeline, tid)) {
                removeMuteEffects(timeline, cid, undo, redo);
                const auto keys = KoraCutDucking::clipKeyframes(toCut, timeline->getClipPosition(cid), timeline->getClipPlaytime(cid), mute, fps);
                auto stack = timeline->getClipEffectStackModel(cid);
                if (keys.isEmpty() || !stack) {
                    continue;
                }
                QMap<QString, QString> params;
                params.insert(QStringLiteral("level"), KoraCutDucking::animationString(keys, timeline->getClipIn(cid)));
                params.insert(QString::fromLatin1(MuteProperty), QStringLiteral("1"));
                if (stack->appendEffectWithParams(QStringLiteral("volume"), params, undo, redo)) {
                    ++touched;
                }
            }
        }
        pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Mute What Is Found"));
        m_result->setText(summary + i18ncp("@info", "Muted on %1 clip.", "Muted on %1 clips.", touched) + missingText);
        return;
    }
    const bool liftOnly = mode == CouperTrous;
    if (!TimelineFunctions::removeZonesOnAllTracks(timeline, toCut, liftOnly, m_moveGuides->isChecked(), undo, redo)) {
        m_result->setText(i18nc("@info", "The cut could not be made (a locked track, or a clip that refuses to be cut). Nothing was changed."));
        return;
    }
    pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Automatic Cut"));
    m_result->setText(summary + (liftOnly ? i18nc("@info", "Cut, the gaps remain. A single Undo goes back.")
                                          : i18nc("@info", "Cut and closed. A single Undo goes back.")) +
                      missingText);
}

void KoraCutAutoCutDialog::applyCameras()
{
    auto timeline = pCore->projectManager()->getTimeline();
    const double fps = pCore->getCurrentFps();
    QList<QVector<double>> speakers;
    QList<int> cameraForSpeaker;
    QStringList mapping;
    QVector<double> all;
    for (auto it = m_cameraFor.cbegin(); it != m_cameraFor.cend(); ++it) {
        const int camera = it.value()->currentData().toInt();
        if (camera < 0 || !m_tracks.value(it.key())->isChecked()) {
            continue;
        }
        const QVector<double> levels = KoraCutTimelineAudio::voiceLevels(timeline, {it.key()});
        speakers << levels;
        cameraForSpeaker << camera;
        mapping << QStringLiteral("%1 → %2").arg(timeline->getTrackTagById(it.key()), timeline->getTrackTagById(camera));
        all += levels;
    }
    if (speakers.size() < 2) {
        m_cameraReport = i18nc("@info", "Cameras: you need at least two speakers, each with a camera. ");
        return;
    }
    KoraCutAutoCut::CameraSettings settings;
    settings.thresholdDb = KoraCutAutoCut::estimateThreshold(all);
    settings.minShotMs = int(m_minShot->value() * 1000);
    settings.maxShotMs = int(m_maxShot->value() * 1000);
    const int wideTrack = m_wide->currentData().toInt();
    settings.hasWide = wideTrack > -1;
    const QList<KoraCutAutoCut::Shot> plan = KoraCutAutoCut::cameraPlan(speakers, settings, fps);
    QList<QPair<QPair<int, int>, int>> shots;
    for (const KoraCutAutoCut::Shot &s : plan) {
        shots << qMakePair(qMakePair(s.frames.first, s.frames.second), s.speaker);
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!TimelineFunctions::applyCameraPlan(timeline, shots, cameraForSpeaker, wideTrack, undo, redo)) {
        m_cameraReport = i18nc("@info", "Cameras: the edit could not be made (locked track?). ");
        return;
    }
    pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Cut the Cameras"));
    if (settings.hasWide) {
        mapping << i18nc("@info", "wide shot %1", timeline->getTrackTagById(wideTrack));
    }
    m_cameraReport = i18ncp("@info", "Cameras (%2): %1 shot edited. ", "Cameras (%2): %1 shots edited. ", plan.size(), mapping.join(QStringLiteral(", ")));
}

void KoraCutAutoCutDialog::cutMarked()
{
    saveSettings();
    auto timeline = pCore->projectManager()->getTimeline();
    const double fps = pCore->getCurrentFps();
    const QList<CommentedTime> marks = ourMarkers(timeline);
    if (marks.isEmpty()) {
        m_result->setText(i18nc("@info", "Nothing marked. First run “Analyze” with the action “Mark them for review”."));
        return;
    }
    // Les zones, triées et fusionnées si deux marques se touchent.
    QList<KoraCutAutoCut::Interval> zones;
    for (const CommentedTime &m : marks) {
        const int start = m.time().frames(fps);
        const int end = start + m.duration().frames(fps);
        if (!zones.isEmpty() && start <= zones.last().second) {
            zones.last().second = qMax(zones.last().second, end);
        } else {
            zones << KoraCutAutoCut::Interval(start, end);
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    // Les marques partent d'abord : elles ont fait leur travail.
    auto guides = timeline->getGuideModel();
    for (const CommentedTime &m : marks) {
        guides->removeMarker(m.time(), undo, redo);
    }
    if (!TimelineFunctions::removeZonesOnAllTracks(timeline, zones, false, m_moveGuides->isChecked(), undo, redo)) {
        m_result->setText(i18nc("@info", "The cut could not be made (a locked track, or a clip that refuses to be cut). Nothing was changed."));
        return;
    }
    pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Cut What Is Marked"));
    m_result->setText(i18ncp("@info", "%1 passage cut", "%1 passages cut", zones.size()) +
                      i18nc("@info suite de « N passages coupés »", " (%1 s). A single Undo goes back.", seconds(KoraCutAutoCut::totalFrames(zones), fps)));
}

void KoraCutAutoCutDialog::removeMarks()
{
    auto timeline = pCore->projectManager()->getTimeline();
    const QList<CommentedTime> marks = ourMarkers(timeline);
    if (marks.isEmpty()) {
        m_result->setText(i18nc("@info", "No automatic cut marks in this sequence."));
        return;
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    for (const CommentedTime &m : marks) {
        timeline->getGuideModel()->removeMarker(m.time(), undo, redo);
    }
    pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Remove the Automatic Cut Marks"));
    m_result->setText(i18ncp("@info", "%1 mark removed.", "%1 marks removed.", marks.size()));
}
