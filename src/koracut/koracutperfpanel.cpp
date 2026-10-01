/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutperfpanel.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "kdenlivesettings.h"
#include "koracutmetrics.h"
#include "koracutperformance.h"
#include "koracutpreferences.h"
#include "koracutrender.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"
#include "utils/machineprofile.h"

#include <KLocalizedString>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <QVBoxLayout>

#if defined(Q_OS_MACOS)
#include <mach/mach.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif

namespace {

/** Mémoire résidente du processus, en Mo (0 si inconnue). */
int residentMemoryMb()
{
#if defined(Q_OS_MACOS)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return int(info.resident_size / (1024 * 1024));
    }
    return 0;
#elif defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/self/statm"));
    if (f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> parts = f.readAll().split(' ');
        if (parts.size() > 1) {
            return int(parts.at(1).toLongLong() * sysconf(_SC_PAGESIZE) / (1024 * 1024));
        }
    }
    return 0;
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS pmc;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return int(pmc.WorkingSetSize / (1024 * 1024));
    }
    return 0;
#else
    return 0;
#endif
}

Monitor *projectMonitor()
{
    return pCore && pCore->monitorManager() ? pCore->monitorManager()->projectMonitor() : nullptr;
}

} // namespace

KoraCutPerfPanel::KoraCutPerfPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("koracutDiagnostic"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);
    auto *content = new QWidget;
    scroll->setWidget(content);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup);
    layout->setSpacing(KoraCutMetrics::SpaceGroup);

    auto *machine = new KoraCutPrefsCard(i18nc("@title:group diagnostic panel", "This computer"), content);
    machine->setIdPrefix(QStringLiteral("diagnostic."));
    m_profile = addValue(machine, i18nc("@label diagnostic panel", "Profile"), QStringLiteral("profil"));
    m_memory = addValue(machine, i18nc("@label diagnostic panel", "Memory used by KoraCut"), QStringLiteral("memoire"));
    layout->addWidget(machine);

    auto *play = new KoraCutPrefsCard(i18nc("@title:group diagnostic panel", "Playback"), content);
    play->setIdPrefix(QStringLiteral("diagnostic."));
    m_fps = addValue(play, i18nc("@label diagnostic panel, frames per second actually shown", "Images per second"), QStringLiteral("ips"));
    m_dropped = addValue(play, i18nc("@label diagnostic panel", "Skipped images"), QStringLiteral("sautees"),
                         i18nc("@info diagnostic panel", "During the last second of playback."));
    m_frameTime = addValue(play, i18nc("@label diagnostic panel", "Time per image"), QStringLiteral("temps_image"),
                           i18nc("@info diagnostic panel", "Average and longest wait between two images shown: decoding, effects and display together."));
    m_preview = addValue(play, i18nc("@label diagnostic panel", "Preview size"), QStringLiteral("apercu"));
    m_effects = addValue(play, i18nc("@label diagnostic panel", "Heavy effects"), QStringLiteral("effets"));
    layout->addWidget(play);

    auto *engine = new KoraCutPrefsCard(i18nc("@title:group diagnostic panel", "Engine"), content);
    engine->setIdPrefix(QStringLiteral("diagnostic."));
    m_decoder = addValue(engine, i18nc("@label diagnostic panel", "Video decoding"), QStringLiteral("decodage"));
    m_proxies = addValue(engine, i18nc("@label diagnostic panel", "Proxies"), QStringLiteral("proxys"));
    m_threads = addValue(engine, i18nc("@label diagnostic panel", "Threads"), QStringLiteral("fils"));
    m_cache = addValue(engine, i18nc("@label diagnostic panel", "Cache"), QStringLiteral("cache"));
    layout->addWidget(engine);

    auto *test = new KoraCutPrefsCard(i18nc("@title:group diagnostic panel", "Test"), content);
    test->setIdPrefix(QStringLiteral("diagnostic."));
    m_test = new QPushButton(i18nc("@action:button", "Test This Computer"));
    m_test->setObjectName(QStringLiteral("testerMachine"));
    m_apply = new QPushButton;
    m_apply->setObjectName(QStringLiteral("appliquerProfil"));
    m_apply->hide();
    test->addStacked(i18nc("@label diagnostic panel", "Plays ten seconds of the sequence from the playhead"), KoraCutPrefsCard::line({m_test, m_apply}), QString(),
                     QStringLiteral("tester"));
    m_verdict = new QLabel(i18nc("@info diagnostic panel", "No test yet."));
    m_verdict->setObjectName(QStringLiteral("koracutPrefsRowHelp"));
    m_verdict->setWordWrap(true);
    test->addBlock(m_verdict, QStringLiteral("verdict"));
    layout->addWidget(test);
    layout->addStretch(1);

    connect(m_test, &QPushButton::clicked, this, &KoraCutPerfPanel::runTest);
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        if (!m_proposed.isEmpty()) {
            KoraCutPerformance::chooseProfile(m_proposed);
            if (Monitor *m = projectMonitor()) {
                m->koraCutConfigureAdaptive();
            }
            m_apply->hide();
            refresh();
        }
    });
    KoraCutPrefsColors::applyCardWindow(this);
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &KoraCutPerfPanel::refresh);
}

QLabel *KoraCutPerfPanel::addValue(KoraCutPrefsCard *card, const QString &label, const QString &key, const QString &help)
{
    auto *value = new QLabel(QStringLiteral("…"));
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setWordWrap(true);
    // Le panneau vit dans un dock étroit : libellé en haut, valeur dessous, rien ne dépasse.
    card->addStacked(label, value, help, key);
    value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return value;
}

void KoraCutPerfPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
    m_timer.start();
}

void KoraCutPerfPanel::hideEvent(QHideEvent *event)
{
    // Rien ne tourne quand personne ne regarde.
    m_timer.stop();
    QWidget::hideEvent(event);
}

QJsonObject KoraCutPerfPanel::snapshot()
{
    QJsonObject o = KoraCutPerformance::stateJson();
    if (Monitor *m = projectMonitor()) {
        const Monitor::KoraCutPlaybackStats st = m->koraCutStats(false);
        o.insert(QStringLiteral("lecture"), st.playing);
        o.insert(QStringLiteral("ips"), st.fps);
        o.insert(QStringLiteral("images_sautees"), st.dropped);
        o.insert(QStringLiteral("temps_image_ms"), qRound(st.frameMs * 10.) / 10.);
        o.insert(QStringLiteral("temps_image_max_ms"), qRound(st.frameMaxMs * 10.) / 10.);
        o.insert(QStringLiteral("apercu_hauteur"), MachineProfile::scalingHeight(st.scaling));
        o.insert(QStringLiteral("apercu_adapte"), st.adapted);
    }
    o.insert(QStringLiteral("effets_en_pause"), KoraCutPerformance::HeavyEffects::count());
    int videos = 0;
    int proxied = 0;
    if (pCore && pCore->projectItemModel()) {
        for (const QString &id : pCore->projectItemModel()->getAllClipIds()) {
            std::shared_ptr<ProjectClip> clip = pCore->projectItemModel()->getClipByBinID(id);
            if (clip && (clip->clipType() == ClipType::AV || clip->clipType() == ClipType::Video)) {
                videos++;
                if (clip->hasProxy()) {
                    proxied++;
                }
            }
        }
    }
    o.insert(QStringLiteral("videos"), videos);
    o.insert(QStringLiteral("videos_en_proxy"), proxied);
    o.insert(QStringLiteral("memoire_mo"), residentMemoryMb());
    return o;
}

void KoraCutPerfPanel::refresh()
{
    Monitor *monitor = projectMonitor();
    Monitor::KoraCutPlaybackStats st;
    if (monitor) {
        st = monitor->koraCutStats(true);
    }
    const MachineProfile::Level level = KoraCutPerformance::level();
    QString profile = MachineProfile::levelName(level);
    if (!MachineProfile::isForced()) {
        profile = i18nc("@info diagnostic panel, %1 is a machine profile", "%1 (detected)", profile);
    }
    m_profile->setText(profile);
    m_profile->setToolTip(MachineProfile::describe(MachineProfile::detect()));
    m_memory->setText(i18nc("@info diagnostic panel, memory in megabytes", "%1 MB", residentMemoryMb()));
    const double fps = pCore ? pCore->getCurrentFps() : 0.;
    if (st.playing) {
        m_fps->setText(i18nc("@info diagnostic panel, %1 shown, %2 expected", "%1 of %2", QString::number(st.fps, 'f', 0), QString::number(fps, 'f', 0)));
        m_dropped->setText(QString::number(st.dropped));
        m_frameTime->setText(i18nc("@info diagnostic panel, milliseconds", "%1 ms (at most %2 ms)", QString::number(st.frameMs, 'f', 1),
                                   QString::number(st.frameMaxMs, 'f', 0)));
    } else {
        const QString idle = i18nc("@info diagnostic panel, nothing measured while paused", "Paused");
        m_fps->setText(idle);
        m_dropped->setText(QStringLiteral("·"));
        m_frameTime->setText(QStringLiteral("·"));
    }
    const int height = MachineProfile::scalingHeight(KdenliveSettings::previewScaling());
    QString preview = height > 0 ? QStringLiteral("%1p").arg(height) : i18nc("@info full resolution preview", "Full");
    if (st.adapted) {
        preview = i18nc("@info diagnostic panel, %1 is a preview size", "%1, lowered while playing", preview);
    }
    m_preview->setText(preview);
    const int paused = KoraCutPerformance::HeavyEffects::count();
    m_effects->setText(paused > 0 ? i18ncp("@info diagnostic panel", "%1 paused while playing", "%1 paused while playing", paused)
                                  : (KoraCutPerformance::simplifiedHeavyEffects() ? i18nc("@info diagnostic panel", "Paused while playing")
                                                                                 : i18nc("@info diagnostic panel", "Always shown")));
    QString decoderTip;
    m_decoder->setText(KoraCutPerformance::decodingLabel(&decoderTip));
    m_decoder->setToolTip(decoderTip);
    const QJsonObject snap = snapshot();
    m_proxies->setText(i18nc("@info diagnostic panel, %1 proxied videos out of %2", "%1 of %2 videos", snap.value(QStringLiteral("videos_en_proxy")).toInt(),
                             snap.value(QStringLiteral("videos")).toInt()));
    m_threads->setText(i18nc("@info diagnostic panel", "%1 cores, playback 1, export %2, proxies %3", QThread::idealThreadCount(),
                             KdenliveSettings::processingthreads(), KdenliveSettings::proxythreads()));
    m_cache->setText(i18nc("@info diagnostic panel, cache limit in megabytes", "Up to %1 MB", KdenliveSettings::maxcachesize()));
}

void KoraCutPerfPanel::runTest()
{
    if (m_testing) {
        return;
    }
    Monitor *monitor = projectMonitor();
    if (!monitor || !pCore->currentDoc()) {
        m_verdict->setText(i18nc("@info diagnostic panel", "Open a project with some clips to test."));
        return;
    }
    m_testing = true;
    m_test->setEnabled(false);
    m_apply->hide();
    m_verdict->setText(i18nc("@info diagnostic panel", "Testing video decoding…"));
    KoraCutPerformance::testHwDecoding([this, monitor](const QString &decoder, const QString &) {
        m_verdict->setText(i18nc("@info diagnostic panel", "Playing ten seconds…"));
        const int from = monitor->position();
        KoraCutRender::measurePlayback(from, 10, [this, monitor, from, decoder](const QJsonObject &r) {
            monitor->requestSeek(from);
            const int expected = std::max(1, r.value(QStringLiteral("images_attendues")).toInt());
            const int shown = r.value(QStringLiteral("images_affichees")).toInt();
            const double ratio = double(shown) / expected;
            const MachineProfile::Level level = KoraCutPerformance::level();
            QString verdict;
            m_proposed.clear();
            if (shown < 5) {
                verdict = i18nc("@info diagnostic panel", "Nothing was played: put some clips in the sequence, then test again.");
            } else if (ratio >= 0.95) {
                verdict = i18nc("@info diagnostic panel, %1 images per second", "Smooth: %1 images per second. This computer plays this project well with its current settings.",
                                r.value(QStringLiteral("ips")).toDouble());
            } else {
                const QString measured = i18nc("@info diagnostic panel, %1 shown out of %2", "%1 images shown out of %2.", shown, expected);
                if (level != MachineProfile::Level::Modest) {
                    m_proposed = MachineProfile::levelId(level == MachineProfile::Level::Comfortable ? MachineProfile::Level::Medium : MachineProfile::Level::Modest);
                    verdict = measured + QLatin1Char(' ') +
                              i18nc("@info diagnostic panel", "Playback stutters. The lighter profile makes proxies sooner and lowers the preview while playing.");
                } else if (ratio >= 0.8) {
                    verdict = measured + QLatin1Char(' ') +
                              i18nc("@info diagnostic panel", "Almost smooth. For the busy passages, click Render in the timeline toolbar.");
                } else {
                    verdict = measured + QLatin1Char(' ') +
                              i18nc("@info diagnostic panel",
                                    "Playback stutters even with the lightest settings. Wait for the proxies to be made, then click Render in the timeline toolbar for the passages with effects.");
                }
            }
            m_verdict->setText(verdict);
            if (!m_proposed.isEmpty()) {
                m_apply->setText(i18nc("@action:button %1 is a machine profile", "Use the %1 Profile", MachineProfile::levelName(MachineProfile::levelFromId(m_proposed))));
                m_apply->show();
            }
            QJsonObject result = r;
            result.insert(QStringLiteral("verdict"), verdict);
            result.insert(QStringLiteral("decodeur"), decoder.isEmpty() ? QStringLiteral("logiciel") : decoder);
            result.insert(QStringLiteral("profil_conseille"), m_proposed.isEmpty() ? MachineProfile::levelId(level) : m_proposed);
            m_testing = false;
            m_test->setEnabled(true);
            refresh();
            Q_EMIT testFinished(result);
        });
    });
}
