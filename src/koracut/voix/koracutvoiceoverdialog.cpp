/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutvoiceoverdialog.h"
#include "koracutvoiceover.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutmetrics.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>
#include <QAudioOutput>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QMediaPlayer>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
enum Column { ColScene = 0, ColText = 1, ColResult = 2, ColPlay = 3 };
}

KoraCutVoiceOverDialog::KoraCutVoiceOverDialog(const QList<Item> &items, QWidget *parent)
    : QDialog(parent)
    , m_job(new KoraCutVoiceOver(this))
{
    setObjectName(QStringLiteral("koracutVoiceOverDialog"));
    setWindowTitle(i18nc("@title:window", "Generate the Voice-over"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    resize(720, 520);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(KoraCutMetrics::SpacePanel, KoraCutMetrics::SpacePanel, KoraCutMetrics::SpacePanel, KoraCutMetrics::SpacePanel);
    root->setSpacing(KoraCutMetrics::SpaceZone);

    auto *intro = new QLabel(i18nc("@info", "Each line becomes its own audio file, checked by speech recognition, cleaned and set to the same level. "
                                            "Listen, then place it on the “Voice-over” track."),
                             this);
    intro->setObjectName(QStringLiteral("koracutPrefsHelp"));
    intro->setWordWrap(true);
    root->addWidget(intro);

    // Le moteur local pas encore installé : la taille, l'endroit, et l'accord de la personne.
    m_notice = new QWidget(this);
    auto *noticeLayout = new QVBoxLayout(m_notice);
    noticeLayout->setContentsMargins(0, 0, 0, 0);
    m_noticeText = new QLabel(m_notice);
    m_noticeText->setWordWrap(true);
    m_noticeText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    noticeLayout->addWidget(m_noticeText);
    auto *installRow = new QHBoxLayout;
    m_installButton = new QPushButton(m_notice);
    m_installButton->setObjectName(QStringLiteral("koracutMotionPrimaryButton"));
    m_installBar = new QProgressBar(m_notice);
    m_installBar->setRange(0, 100);
    m_installBar->setVisible(false);
    installRow->addWidget(m_installButton);
    installRow->addWidget(m_installBar, 1);
    noticeLayout->addLayout(installRow);
    root->addWidget(m_notice);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(KoraCutMetrics::SpaceZone);
    form->setVerticalSpacing(KoraCutMetrics::SpaceGroup);
    m_engine = new QComboBox(this);
    for (const QString &engine : KoraCutVoiceOver::engines()) {
        const QJsonObject state = KoraCutVoiceOver::engineState(engine);
        // Un service en ligne n'apparaît que s'il est branché ; le moteur local toujours (on peut l'installer d'ici).
        if (engine != QLatin1String("local") && !state.value(QStringLiteral("pret")).toBool()) {
            continue;
        }
        if (engine == QLatin1String("local") && !KoraCutVoiceOver::localSupported()) {
            continue;
        }
        m_engine->addItem(state.value(QStringLiteral("nom")).toString(), engine);
    }
    const int autoIndex = m_engine->findData(KoraCutVoiceOver::autoEngine());
    if (autoIndex >= 0) {
        m_engine->setCurrentIndex(autoIndex);
    }
    form->addRow(i18nc("@label:listbox", "Engine:"), m_engine);
    m_voice = new QComboBox(this);
    form->addRow(i18nc("@label:listbox", "Voice:"), m_voice);
    m_tone = new QComboBox(this);
    m_tone->addItem(i18nc("@item voice tone", "Calm"), QStringLiteral("pose"));
    m_tone->addItem(i18nc("@item voice tone", "Lively"), QStringLiteral("vivant"));
    m_tone->addItem(i18nc("@item voice tone", "Soft"), QStringLiteral("doux"));
    m_tone->addItem(i18nc("@item voice tone", "Serious"), QStringLiteral("serieux"));
    form->addRow(i18nc("@label:listbox", "Tone:"), m_tone);
    auto *speedRow = new QHBoxLayout;
    m_speed = new QSpinBox(this);
    m_speed->setRange(80, 125);
    m_speed->setValue(100);
    m_speed->setSuffix(QStringLiteral(" %"));
    m_takes = new QSpinBox(this);
    m_takes->setRange(1, 5);
    m_takes->setValue(3);
    m_takes->setToolTip(i18nc("@info:tooltip", "Takes per line: speech recognition keeps the most faithful one."));
    speedRow->addWidget(m_speed);
    speedRow->addSpacing(KoraCutMetrics::SpaceZone);
    speedRow->addWidget(new QLabel(i18nc("@label:spinbox", "Takes:"), this));
    speedRow->addWidget(m_takes);
    speedRow->addStretch(1);
    form->addRow(i18nc("@label:spinbox", "Speed:"), speedRow);
    root->addLayout(form);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(4);
    m_list->setHeaderLabels({i18nc("@title:column", "Scene"), i18nc("@title:column", "Text to say"), i18nc("@title:column", "Result"), QString()});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(false);
    m_list->setWordWrap(true);
    m_list->header()->setSectionResizeMode(ColScene, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(ColText, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(ColResult, QHeaderView::Interactive);
    m_list->header()->setSectionResizeMode(ColPlay, QHeaderView::Fixed);
    m_list->header()->resizeSection(ColResult, 190);
    m_list->header()->resizeSection(ColPlay, 36);
    m_list->header()->setStretchLastSection(false);
    for (const Item &item : items) {
        Row row;
        row.item = item;
        row.tree = new QTreeWidgetItem(m_list);
        row.tree->setText(ColScene, item.label);
        row.tree->setCheckState(ColScene, Qt::Checked);
        row.tree->setText(ColText, item.text);
        row.tree->setToolTip(ColText, i18nc("@info:tooltip", "Double-click to change what the voice says (for example “afebaba point com” for afebaba.com)."));
        row.tree->setFlags(row.tree->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        row.tree->setText(ColResult, item.position >= 0 ? i18nc("@item voice-over row", "Linked scene") : QString());
        m_rows << row;
    }
    root->addWidget(m_list, 1);

    auto *statusRow = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    m_progress->setMaximumWidth(180);
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_progress);
    root->addLayout(statusRow);

    auto *buttons = new QHBoxLayout;
    m_close = new QPushButton(i18nc("@action:button", "Close"), this);
    m_binOnly = new QPushButton(i18nc("@action:button", "Keep in the Bin Only"), this);
    m_place = new QPushButton(i18nc("@action:button", "Place on the Timeline"), this);
    m_generate = new QPushButton(i18nc("@action:button", "Generate"), this);
    m_generate->setObjectName(QStringLiteral("koracutMotionPrimaryButton"));
    m_generate->setDefault(true);
    m_place->setEnabled(false);
    m_binOnly->setEnabled(false);
    m_place->setToolTip(i18nc("@info:tooltip", "At the linked scene when the line has a marker, otherwise one after the other from the playhead. One undo step."));
    buttons->addWidget(m_close);
    buttons->addStretch(1);
    buttons->addWidget(m_binOnly);
    buttons->addWidget(m_place);
    buttons->addWidget(m_generate);
    root->addLayout(buttons);

    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::StoppedState) {
            m_playing = -1;
            for (Row &r : m_rows) {
                if (auto *b = qobject_cast<QToolButton *>(m_list->itemWidget(r.tree, ColPlay))) {
                    b->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
                }
            }
        }
    });

    connect(m_engine, &QComboBox::currentIndexChanged, this, [this]() {
        refreshEngine();
        refreshVoices();
    });
    connect(m_generate, &QPushButton::clicked, this, &KoraCutVoiceOverDialog::startGeneration);
    connect(m_place, &QPushButton::clicked, this, [this]() { placeReady(true); });
    connect(m_binOnly, &QPushButton::clicked, this, [this]() { placeReady(false); });
    connect(m_close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_installButton, &QPushButton::clicked, this, [this]() {
        KoraCutVoiceOver *installer = KoraCutVoiceOver::installer();
        m_installButton->setEnabled(false);
        m_installBar->setVisible(true);
        installer->install();
    });
    KoraCutVoiceOver *installer = KoraCutVoiceOver::installer();
    connect(installer, &KoraCutVoiceOver::installProgress, this, [this](int percent, const QString &step) {
        m_installBar->setVisible(true);
        m_installBar->setValue(percent);
        m_installBar->setFormat(QStringLiteral("%1 · %p %").arg(step));
    });
    connect(installer, &KoraCutVoiceOver::installFinished, this, [this](const QString &error) {
        m_installBar->setVisible(false);
        m_installButton->setEnabled(true);
        if (!error.isEmpty()) {
            m_status->setText(error);
        }
        refreshEngine();
    });

    connect(m_job, &KoraCutVoiceOver::progress, this, [this](const QString &text, int done, int total) {
        m_status->setText(text);
        m_progress->setVisible(total > 0);
        m_progress->setRange(0, std::max(1, total));
        m_progress->setValue(done);
    });
    connect(m_job, &KoraCutVoiceOver::phraseReady, this, &KoraCutVoiceOverDialog::onPhrase);
    connect(m_job, &KoraCutVoiceOver::finished, this, [this]() {
        setBusy(false);
        int ready = 0;
        for (const Row &r : std::as_const(m_rows)) {
            ready += (!r.file.isEmpty() && !r.imported) ? 1 : 0;
        }
        m_status->setText(i18ncp("@info", "%1 voice ready. Listen, then place it.", "%1 voices ready. Listen, then place them.", ready));
    });
    connect(m_job, &KoraCutVoiceOver::failed, this, [this](const QString &, const QString &message, const QString &advice) {
        setBusy(false);
        m_status->setText(advice.isEmpty() ? message : QStringLiteral("%1 %2").arg(message, advice));
    });
    refreshEngine();
    refreshVoices();
}

KoraCutVoiceOverDialog::~KoraCutVoiceOverDialog()
{
    m_job->cancel();
    if (m_player) {
        m_player->stop();
    }
    deleteUnkept();
}

QString KoraCutVoiceOverDialog::currentEngine() const
{
    return m_engine->currentData().toString();
}

void KoraCutVoiceOverDialog::refreshEngine()
{
    const QString engine = currentEngine();
    const QJsonObject state = KoraCutVoiceOver::engineState(engine.isEmpty() ? QStringLiteral("local") : engine);
    const bool ready = state.value(QStringLiteral("pret")).toBool();
    KoraCutVoiceOver *installer = KoraCutVoiceOver::installer();
    if (engine.isEmpty()) {
        m_notice->setVisible(true);
        m_noticeText->setText(i18nc("@info", "No voice engine can run here. Connect ElevenLabs or OpenAI in Settings > AI Connections."));
        m_installButton->setVisible(false);
    } else if (engine == QLatin1String("local") && !ready) {
        m_notice->setVisible(true);
        const bool noPython = KoraCutVoiceOver::systemPython().isEmpty() && !state.value(QStringLiteral("installe")).toBool();
        m_noticeText->setText(noPython ? i18nc("@info", "The voice engine on this computer needs Python 3.10 or later. Install it from python.org, then come back here.")
                                       : i18nc("@info %1 size in GB, %2 and %3 folders",
                                               "The voice engine on this computer is not installed yet. It is a one-time download of about %1 GB, "
                                               "stored in %2 and %3. It is free and works without an account.",
                                               QLocale().toString(KoraCutVoiceOver::installSizeMb() / 1000., 'f', 1), KoraCutVoiceOver::cacheDir(),
                                               KoraCutVoiceOver::modelDir()));
        m_installButton->setVisible(!noPython);
        m_installButton->setText(i18nc("@action:button %1 size in GB", "Download and Install (%1 GB)", QLocale().toString(KoraCutVoiceOver::installSizeMb() / 1000., 'f', 1)));
        m_installButton->setEnabled(!installer->isInstalling());
        m_installBar->setVisible(installer->isInstalling());
        m_installBar->setValue(installer->installPercent());
    } else {
        m_notice->setVisible(false);
    }
    if (!state.value(QStringLiteral("avertissement")).toString().isEmpty()) {
        m_status->setText(state.value(QStringLiteral("avertissement")).toString());
    }
    m_generate->setEnabled(ready && !m_job->isRunning());
}

void KoraCutVoiceOverDialog::refreshVoices()
{
    m_voice->clear();
    const QString engine = currentEngine();
    if (engine == QLatin1String("local")) {
        for (const QJsonValue &v : KoraCutVoiceOver::localVoices()) {
            const QJsonObject o = v.toObject();
            m_voice->addItem(QStringLiteral("%1 · %2").arg(o.value(QStringLiteral("nom")).toString(), o.value(QStringLiteral("description")).toString()),
                             o.value(QStringLiteral("id")).toString());
        }
        return;
    }
    if (engine.isEmpty()) {
        return;
    }
    // Les voix d'un service : une requête en tâche de fond, la liste arrive quand elle arrive.
    m_voice->addItem(i18nc("@item", "Loading the voices…"), QString());
    auto *lister = new KoraCutVoiceOver(this);
    const QString byDefault = KoraCutVoiceOver::defaultVoice(engine);
    connect(lister, &KoraCutVoiceOver::remoteVoices, this, [this, lister, byDefault](const QJsonArray &voices) {
        m_voice->clear();
        for (const QJsonValue &v : voices) {
            const QJsonObject o = v.toObject();
            const QString description = o.value(QStringLiteral("description")).toString();
            m_voice->addItem(description.isEmpty() ? o.value(QStringLiteral("nom")).toString() : QStringLiteral("%1 · %2").arg(o.value(QStringLiteral("nom")).toString(), description),
                             o.value(QStringLiteral("id")).toString());
        }
        const int index = m_voice->findData(byDefault);
        if (index >= 0) {
            m_voice->setCurrentIndex(index);
        }
        lister->deleteLater();
    });
    connect(lister, &KoraCutVoiceOver::failed, this, [this, lister](const QString &, const QString &message, const QString &) {
        m_voice->clear();
        m_status->setText(message);
        lister->deleteLater();
    });
    lister->listRemoteVoices(engine);
}

void KoraCutVoiceOverDialog::setBusy(bool busy)
{
    m_generate->setEnabled(!busy);
    m_engine->setEnabled(!busy);
    m_voice->setEnabled(!busy);
    m_tone->setEnabled(!busy);
    m_speed->setEnabled(!busy);
    m_takes->setEnabled(!busy);
    m_progress->setVisible(busy);
    bool anyReady = false;
    for (const Row &r : std::as_const(m_rows)) {
        anyReady = anyReady || (!r.file.isEmpty() && !r.imported);
    }
    m_place->setEnabled(!busy && anyReady);
    m_binOnly->setEnabled(!busy && anyReady);
    m_generate->setText(busy ? i18nc("@action:button", "Generating…") : i18nc("@action:button", "Generate"));
}

void KoraCutVoiceOverDialog::startGeneration()
{
    if (m_job->isRunning() || currentEngine().isEmpty()) {
        return;
    }
    const QString folder = KoraCutVoiceOver::projectVoiceFolder();
    if (!QDir().mkpath(folder)) {
        m_status->setText(i18nc("@info", "Could not create the folder %1.", folder));
        return;
    }
    KoraCutVoiceRequest request;
    request.engine = currentEngine();
    request.voice = m_voice->currentData().toString();
    request.tone = m_tone->currentData().toString();
    request.speed = m_speed->value() / 100.;
    request.takes = m_takes->value();
    int skip = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        if (r.tree->checkState(ColScene) != Qt::Checked || r.imported) {
            continue;
        }
        const QString text = r.tree->text(ColText).simplified();
        if (text.isEmpty()) {
            continue;
        }
        // Une voix refaite remplace la précédente, qui n'a pas été gardée.
        if (!r.file.isEmpty()) {
            QFile::remove(r.file);
            r.file.clear();
        }
        const QString output = folder + QLatin1Char('/') + KoraCutVoiceOver::fileNameFor(folder, r.item.text.isEmpty() ? text : r.item.text, QString(), skip++);
        // Le texte d'origine reste celui du sous-titre ; ce qui a été retouché ici est ce que la voix dit.
        request.phrases << KoraCutVoicePhrase{QString::number(i), r.item.text.isEmpty() ? text : r.item.text, text == r.item.text.simplified() ? QString() : text, output};
        r.tree->setText(ColResult, i18nc("@item voice-over row", "Waiting…"));
    }
    if (request.phrases.isEmpty()) {
        m_status->setText(i18nc("@info", "No line to say: tick at least one line with text."));
        return;
    }
    setBusy(true);
    m_status->setText(i18nc("@info", "Starting the voice engine…"));
    m_job->generate(request);
}

void KoraCutVoiceOverDialog::onPhrase(const QJsonObject &phrase)
{
    const int index = phrase.value(QStringLiteral("id")).toString().toInt();
    if (index < 0 || index >= m_rows.size()) {
        return;
    }
    Row &r = m_rows[index];
    if (phrase.contains(QStringLiteral("erreur"))) {
        r.tree->setText(ColResult, phrase.value(QStringLiteral("erreur")).toString());
        return;
    }
    r.file = phrase.value(QStringLiteral("fichier")).toString();
    r.result = phrase;
    const double seconds = phrase.value(QStringLiteral("duree")).toDouble();
    const QJsonValue fidelity = phrase.value(QStringLiteral("fidelite"));
    const QJsonArray doubtful = phrase.value(QStringLiteral("douteux")).toArray();
    QString result = i18nc("@item voice-over row, %1 seconds", "%1 s", QLocale().toString(seconds, 'f', 1));
    if (fidelity.isNull() || fidelity.isUndefined()) {
        result += i18nc("@item voice-over row", " · not checked");
    } else if (fidelity.toDouble() >= 0.95 && doubtful.isEmpty()) {
        result += i18nc("@item voice-over row", " · every word heard");
    } else {
        QStringList words;
        for (const QJsonValue &w : doubtful) {
            words << w.toString();
        }
        result += words.isEmpty() ? i18nc("@item voice-over row", " · listen again") : i18nc("@item voice-over row, %1 words", " · listen again: %1", words.join(QStringLiteral(", ")));
    }
    r.tree->setText(ColResult, result);
    r.tree->setToolTip(ColResult, i18nc("@info:tooltip %1 what speech recognition heard", "Heard: %1", phrase.value(QStringLiteral("entendu")).toString()));
    auto *playButton = new QToolButton(m_list);
    playButton->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    playButton->setToolTip(i18nc("@info:tooltip", "Listen"));
    playButton->setAutoRaise(true);
    connect(playButton, &QToolButton::clicked, this, [this, index]() { play(index); });
    m_list->setItemWidget(r.tree, ColPlay, playButton);
}

void KoraCutVoiceOverDialog::play(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).file.isEmpty()) {
        return;
    }
    if (m_playing == row) {
        m_player->stop();
        return;
    }
    m_player->stop();
    m_player->setSource(QUrl::fromLocalFile(m_rows.at(row).file));
    m_player->play();
    m_playing = row;
    if (auto *b = qobject_cast<QToolButton *>(m_list->itemWidget(m_rows.at(row).tree, ColPlay))) {
        b->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop")));
    }
}

void KoraCutVoiceOverDialog::placeReady(bool onTimeline)
{
    KdenliveDoc *doc = pCore->currentDoc();
    if (!doc) {
        return;
    }
    m_player->stop();
    auto timeline = doc->getTimeline(pCore->currentTimelineId(), true);
    const double fps = pCore->getCurrentFps();
    QStringList files;
    QList<int> positions;
    QList<int> rows;
    QList<bool> flexible;
    // Une scène reliée garde sa place ; les autres se suivent depuis la tête de lecture, 0,3 s d'écart.
    int cursor = pCore->getMonitorPosition();
    const int gap = int(std::lround(0.3 * fps));
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        if (r.file.isEmpty() || r.imported || !QFileInfo::exists(r.file)) {
            continue;
        }
        const int length = int(std::ceil(r.result.value(QStringLiteral("duree")).toDouble() * fps));
        int position = -1;
        if (onTimeline) {
            position = r.item.position >= 0 ? r.item.position : cursor;
            cursor = std::max(cursor, position + length + gap);
        }
        files << r.file;
        positions << position;
        flexible << (r.item.position < 0);
        rows << i;
    }
    if (files.isEmpty()) {
        return;
    }
    const QString undo = i18ncp("@action undo text", "Voice-over: %1 line", "Voice-over: %1 lines", files.size());
    const QJsonObject result = KoraCutVoiceOver::importAndPlace(timeline, files, positions, -1, undo, flexible);
    if (!result.value(QStringLiteral("ok")).toBool()) {
        m_status->setText(result.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("message")).toString());
        return;
    }
    const QJsonArray medias = result.value(QStringLiteral("medias")).toArray();
    int placed = 0;
    QStringList refused;
    for (int k = 0; k < rows.size() && k < medias.size(); ++k) {
        Row &r = m_rows[rows.at(k)];
        r.imported = true;
        const QJsonObject m = medias.at(k).toObject();
        if (m.contains(QStringLiteral("clip_id"))) {
            ++placed;
            r.tree->setText(ColResult, r.tree->text(ColResult) + i18nc("@item voice-over row", " · placed"));
        } else if (m.contains(QStringLiteral("pose_refusee"))) {
            refused << m.value(QStringLiteral("pose_refusee")).toString();
        }
    }
    QString status = onTimeline ? i18ncp("@info", "%1 voice placed on the timeline.", "%1 voices placed on the timeline.", placed)
                                : i18ncp("@info", "%1 voice added to the bin.", "%1 voices added to the bin.", files.size());
    if (!refused.isEmpty()) {
        status += QLatin1Char(' ');
        status += refused.join(QLatin1Char(' '));
    }
    m_status->setText(status);
    setBusy(false);
}

void KoraCutVoiceOverDialog::deleteUnkept()
{
    // Les voix générées mais ni posées ni gardées : rien ne doit traîner sur le disque.
    for (Row &r : m_rows) {
        if (!r.file.isEmpty() && !r.imported) {
            QFile::remove(r.file);
            r.file.clear();
        }
    }
}

void KoraCutVoiceOverDialog::reject()
{
    m_job->cancel();
    QDialog::reject();
}

QString KoraCutVoiceOverDialog::debugState() const
{
    QStringList parts;
    parts << QStringLiteral("moteur=%1").arg(currentEngine()) << QStringLiteral("voix=%1").arg(m_voice->currentData().toString());
    for (const Row &r : m_rows) {
        parts << QStringLiteral("[%1|%2|%3|%4]").arg(r.item.label, r.tree->text(ColResult), QFileInfo(r.file).fileName(), r.imported ? QStringLiteral("gardee") : QStringLiteral("-"));
    }
    parts << QStringLiteral("statut=%1").arg(m_status->text());
    return parts.join(QLatin1Char(' '));
}
