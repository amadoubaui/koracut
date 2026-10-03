/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutperformance.h"
#include "koracuthwdecode.h"

#include "core.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "monitor/monitormanager.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"

#include <KConfig>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QAction>
#include <QIcon>
#include <QPointer>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>
#include <mutex>
#include <mlt++/Mlt.h>
#include <QFile>

namespace {

QStringList proxyProfileNames(QMap<QString, QString> *values = nullptr)
{
    KConfig conf(QStringLiteral("encodingprofiles.rc"), KConfig::CascadeConfig, QStandardPaths::AppDataLocation);
    const QMap<QString, QString> map = KConfigGroup(&conf, QStringLiteral("proxy")).entryMap();
    if (values) {
        *values = map;
    }
    return map.keys();
}

/** Une étape de l'essai du décodage : ffmpeg, durée, réussite. */
struct DecodeRun
{
    bool ok{false};
    qint64 ms{0};
    QString error;
};

DecodeRun runFfmpeg(const QString &program, const QStringList &args, int timeoutMs)
{
    DecodeRun run;
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    QElapsedTimer clock;
    clock.start();
    proc.start(program, args);
    if (!proc.waitForStarted(3000)) {
        run.error = QStringLiteral("ffmpeg ne démarre pas");
        return run;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        run.error = QStringLiteral("trop long");
        return run;
    }
    run.ms = clock.elapsed();
    const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    // ffmpeg retombe en logiciel sans le dire autrement que par ces messages.
    const bool fellBack = err.contains(QLatin1String("Failed setup"), Qt::CaseInsensitive) || err.contains(QLatin1String("hwaccel"), Qt::CaseInsensitive) ||
                          err.contains(QLatin1String("Error"), Qt::CaseInsensitive);
    run.ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && !fellBack;
    if (!run.ok) {
        run.error = err.section(QLatin1Char('\n'), 0, 0).left(160);
        if (run.error.isEmpty()) {
            run.error = QStringLiteral("code %1").arg(proc.exitCode());
        }
    }
    return run;
}

} // namespace

namespace KoraCutPerformance {

KConfigGroup group()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
}

MachineProfile::Level level()
{
    return MachineProfile::effectiveLevel();
}

MachineProfile::Recommendation recommendation(MachineProfile::Level lvl)
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    MachineProfile::Recommendation r = MachineProfile::recommend(lvl, MachineProfile::detect(cache));
    r.proxyProfile = MachineProfile::proxyProfileFor(lvl, KdenliveSettings::supportedHWCodecs(), proxyProfileNames());
    return r;
}

void applyProfile(MachineProfile::Level lvl)
{
    const MachineProfile::Recommendation r = recommendation(lvl);
    KdenliveSettings::setPreviewScaling(r.previewScaling);
    KdenliveSettings::setEnableproxy(r.enableProxy);
    KdenliveSettings::setGenerateproxy(r.generateProxy);
    KdenliveSettings::setProxyminsize(r.proxyMinSize);
    KdenliveSettings::setProxyscale(r.proxyScale);
    KdenliveSettings::setProxythreads(r.proxyThreads);
    KdenliveSettings::setProcessingthreads(r.processingThreads);
    KdenliveSettings::setMaxcachesize(r.maxCacheSizeMb);
    KdenliveSettings::setCleanCacheMonths(r.cacheCleanupMonths);
    KdenliveSettings::setVideothumbnails(r.videoThumbnails);
    KdenliveSettings::setMonitor_dropframes(true);
    // Profil de proxy « Automatique » : KdenliveDoc::initProxySettings choisit alors selon le profil machine.
    KdenliveSettings::setProxy_profile(0);
    KdenliveSettings::setProxyparams(QString());
    KdenliveSettings::setProxyextension(QString());
    KConfigGroup g = group();
    g.writeEntry("apercuAdaptatif", MachineProfile::scalingForHeight(r.adaptivePreviewFloorHeight));
    g.writeEntry("dropDiviseur", r.dropDivisor);
    g.writeEntry("proxyLourdLargeur", r.proxyHeavyMinWidth);
    g.writeEntry("effetsLourdsSimplifies", r.simplifiedHeavyEffects);
    g.writeEntry("cacheProducteursMax", r.producerCacheMax);
    g.writeEntry("profilApplique", MachineProfile::levelId(lvl));
    KdenliveSettings::self()->save();
    g.sync();
    if (pCore && pCore->monitorManager()) {
        Q_EMIT pCore->monitorManager()->scalingChanged();
        Q_EMIT pCore->monitorManager()->updatePreviewScaling();
    }
}

void chooseProfile(const QString &id)
{
    bool ok = false;
    const MachineProfile::Level chosen = MachineProfile::levelFromId(id, &ok);
    KConfigGroup g = group();
    if (ok) {
        g.writeEntry("profilMachine", MachineProfile::levelId(chosen));
    } else {
        g.deleteEntry("profilMachine");
    }
    g.sync();
    applyProfile(level());
}

void applyFirstLaunchDefaults(bool firstRun)
{
    const QString applied = group().readEntry("profilApplique", QString());
    bool forced = false;
    const MachineProfile::Level env = MachineProfile::levelFromId(qEnvironmentVariable("KORACUT_PROFIL"), &forced);
    // Une configuration déjà en place n'est pas réécrite en silence : le profil se change dans les
    // Préférences. Seul KORACUT_PROFIL (banc d'essai) repose les défauts d'un profil différent.
    if (forced ? applied == MachineProfile::levelId(env) : (!firstRun || !applied.isEmpty())) {
        return;
    }
    applyProfile(level());
    // L'essai du décodage matériel prend une ou deux secondes : il part après l'ouverture de la fenêtre.
    QTimer::singleShot(3000, pCore.get(), []() { testHwDecoding(); });
}

int adaptiveFloor()
{
    const KConfigGroup g = group();
    if (g.hasKey("apercuAdaptatif")) {
        return g.readEntry("apercuAdaptatif", 0);
    }
    // Pas de choix enregistré (configuration antérieure au 29 septembre 2026, que les défauts du profil
    // n'ont jamais réécrite) : le plancher du profil en service, sans rien écrire.
    return MachineProfile::scalingForHeight(MachineProfile::recommend(level(), MachineProfile::Hardware()).adaptivePreviewFloorHeight);
}

int dropDivisor()
{
    return std::clamp(group().readEntry("dropDiviseur", 4), 1, 8);
}

int proxyHeavyMinWidth()
{
    return group().readEntry("proxyLourdLargeur", -1);
}

bool simplifiedHeavyEffects()
{
    return group().readEntry("effetsLourdsSimplifies", false);
}

int producerCacheMax()
{
    return group().readEntry("cacheProducteursMax", 0);
}

namespace {
const char *kDecoderCache = "producer_avformat";
const int kMltCacheMax = 200; // MAX_CACHE_SIZE de mlt_cache.c : au-delà, MLT ignore la demande
std::mutex s_roomMutex;
int s_roomDepth = 0;
int s_roomBase = 0;
int s_roomExtra = 0;
} // namespace

DecoderCacheRoom::DecoderCacheRoom(int producers)
    : m_extra(qMax(0, producers))
{
    const std::lock_guard<std::mutex> lock(s_roomMutex);
    if (s_roomDepth++ == 0) {
        s_roomBase = mlt_service_cache_get_size(nullptr, kDecoderCache);
        s_roomExtra = 0;
    }
    s_roomExtra += m_extra;
    mlt_service_cache_set_size(nullptr, kDecoderCache, qMin(kMltCacheMax, s_roomBase + s_roomExtra));
}

DecoderCacheRoom::DecoderCacheRoom(const QString &sceneXml)
    : DecoderCacheRoom(producersIn(sceneXml))
{
}

DecoderCacheRoom::~DecoderCacheRoom()
{
    const std::lock_guard<std::mutex> lock(s_roomMutex);
    s_roomExtra -= m_extra;
    --s_roomDepth;
    mlt_service_cache_set_size(nullptr, kDecoderCache, s_roomDepth == 0 ? s_roomBase : qMin(kMltCacheMax, s_roomBase + s_roomExtra));
}

int DecoderCacheRoom::producersIn(const QString &sceneXml)
{
    // Un fichier .mlt imbriqué (plan stabilisé) ouvre lui-même au moins un décodeur : d'où la marge.
    return int(sceneXml.count(QLatin1String("<producer ")) + sceneXml.count(QLatin1String("<chain "))) + 16;
}

int DecoderCacheRoom::baseSize()
{
    const std::lock_guard<std::mutex> lock(s_roomMutex);
    return s_roomDepth > 0 ? s_roomBase : mlt_service_cache_get_size(nullptr, kDecoderCache);
}

QString decodingLabel(QString *tooltip)
{
    const QString hw = KdenliveSettings::hwDecoding();
    if (tooltip) {
        *tooltip = group().readEntry("decodageMaterielEssai", QString());
    }
    if (hw.isEmpty()) {
        return i18nc("@info video decoding done by the processor", "Processor (software)");
    }
    if (!KoraCutHwDecode::effective().isEmpty()) {
        return i18nc("@info hardware video decoding, %1 is its name", "Hardware (%1)", hw);
    }
    const QList<KoraCutHwDecode::Clip> clips = KoraCutHwDecode::softwareClips();
    int refused = 0;
    QStringList lines;
    for (const KoraCutHwDecode::Clip &clip : clips) {
        if (clip.state != KoraCutHwDecode::State::Refused) {
            continue;
        }
        ++refused;
        if (lines.size() < 12) {
            lines << (clip.format.isEmpty() ? clip.name : QStringLiteral("%1 (%2)").arg(clip.name, clip.format));
        }
    }
    if (tooltip && !lines.isEmpty()) {
        if (refused > lines.size()) {
            lines << QStringLiteral("…");
        }
        *tooltip = lines.join(QLatin1Char('\n'));
    }
    if (refused == 0) {
        return i18nc("@info diagnostic panel, clips are being checked before the graphics chip is used", "Processor, while the clips are checked");
    }
    return i18ncp("@info diagnostic panel, the graphics chip cannot decode some clips of this project",
                  "Processor for this project: the chip cannot decode %1 clip", "Processor for this project: the chip cannot decode %1 clips", refused);
}

void testHwDecoding(const std::function<void(const QString &, const QString &)> &done)
{
    const QStringList candidates = MachineProfile::hwDecoderCandidates(KdenliveSettings::supportedHWCodecs());
    const QString ffmpeg = KdenliveSettings::ffmpegpath();
    if (ffmpeg.isEmpty() || candidates.isEmpty()) {
        if (done) {
            done(QString(), QStringLiteral("pas de ffmpeg ou pas de décodeur à essayer"));
        }
        return;
    }
    // Tout se passe dans un fil à part : l'essai lance plusieurs ffmpeg à la suite.
    auto *thread = QThread::create([ffmpeg, candidates, done]() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(dir);
        const QString clip = dir + QStringLiteral("/koracut-essai-decodage.mp4");
        if (!QFileInfo::exists(clip)) {
            // Deux secondes de 1080p en H.264 : ce que la personne importera le plus souvent.
            runFfmpeg(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
                       QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc2=s=1920x1080:r=25"), QStringLiteral("-t"), QStringLiteral("2"),
                       QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
                       QStringLiteral("ultrafast"), clip},
                      20000);
        }
        const auto decode = [&clip, &ffmpeg](const QString &hw) {
            QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error")};
            if (!hw.isEmpty()) {
                args << QStringLiteral("-hwaccel") << (hw == QLatin1String("nvdec") ? QStringLiteral("cuda") : hw);
            }
            args << QStringLiteral("-i") << clip << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
            return runFfmpeg(ffmpeg, args, 20000);
        };
        const DecodeRun software = decode(QString());
        QString chosen;
        QStringList report;
        report << QStringLiteral("logiciel %1 ms").arg(software.ok ? QString::number(software.ms) : QStringLiteral("échec"));
        for (const QString &hw : candidates) {
            const DecodeRun run = decode(hw);
            if (!run.ok) {
                report << QStringLiteral("%1 : échec (%2)").arg(hw, run.error);
                continue;
            }
            report << QStringLiteral("%1 %2 ms").arg(hw).arg(run.ms);
            if (chosen.isEmpty()) {
                chosen = hw;
            }
        }
        const QString line = report.join(QStringLiteral(", "));
        QMetaObject::invokeMethod(
            qApp,
            [chosen, line, done]() {
                const QString before = KdenliveSettings::hwDecoding();
                KdenliveSettings::setHwDecoding(chosen);
                KdenliveSettings::self()->save();
                KConfigGroup g = group();
                const QString summary = (chosen.isEmpty() ? QStringLiteral("logiciel") : chosen) + QStringLiteral(" : ") + line;
                g.writeEntry("decodageMaterielEssai", summary);
                g.sync();
                if (before != chosen && pCore) {
                    pCore->updateHwDecoding();
                }
                qInfo().noquote() << "KORACUT-PERF décodage" << (chosen.isEmpty() ? QStringLiteral("logiciel") : chosen) << line;
                if (done) {
                    done(chosen, line);
                }
            },
            Qt::QueuedConnection);
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}

QJsonObject stateJson()
{
    const MachineProfile::Level lvl = level();
    const MachineProfile::Hardware hw = MachineProfile::detect();
    return QJsonObject{{QStringLiteral("profil"), MachineProfile::levelId(lvl)},
                       {QStringLiteral("profil_nom"), MachineProfile::levelName(lvl)},
                       {QStringLiteral("profil_detecte"), MachineProfile::levelId(MachineProfile::detectedLevel())},
                       {QStringLiteral("profil_choisi"), MachineProfile::isForced()},
                       {QStringLiteral("machine"), MachineProfile::describe(hw)},
                       {QStringLiteral("coeurs"), hw.logicalCores},
                       {QStringLiteral("memoire_mo"), hw.ramMb},
                       {QStringLiteral("apercu_moniteur"), KdenliveSettings::previewScaling()},
                       {QStringLiteral("apercu_adaptatif"), adaptiveFloor()},
                       {QStringLiteral("images_sautees_max"), QStringLiteral("i/s ÷ %1").arg(dropDivisor())},
                       {QStringLiteral("decodage"), KdenliveSettings::hwDecoding().isEmpty() ? QStringLiteral("logiciel") : KdenliveSettings::hwDecoding()},
                       {QStringLiteral("decodage_essai"), group().readEntry("decodageMaterielEssai", QString())},
                       // Document 6, journal du 1er octobre 2026 : le réglage est une demande, voici ce qui tourne vraiment.
                       {QStringLiteral("decodage_effectif"), KoraCutHwDecode::stateJson().value(QStringLiteral("effectif"))},
                       {QStringLiteral("decodage_clips_logiciel"), KoraCutHwDecode::stateJson().value(QStringLiteral("clips_logiciel"))},
                       {QStringLiteral("proxys_auto"), KdenliveSettings::generateproxy()},
                       {QStringLiteral("proxy_largeur_min"), KdenliveSettings::proxyminsize()},
                       {QStringLiteral("proxy_codec_lourd_largeur"), proxyHeavyMinWidth()},
                       {QStringLiteral("effets_lourds_simplifies"), simplifiedHeavyEffects()},
                       {QStringLiteral("etalonnage_en_pause"), gradePaused()},
                       {QStringLiteral("cache_mo"), KdenliveSettings::maxcachesize()}};
}

// ---- Étalonnage en pause dans l'aperçu (2 octobre 2026) ------------------------------------

namespace {
GradePauseState s_gradePause;
std::atomic<int> s_gradeBypass{0};
// QPointer : l'action appartient à la fenêtre, détruite avant la fin du processus.
QPointer<QAction> s_gradePauseAction;

// Les noms lus par le filtre (grade/mlt/filter_koracut_grade.cpp).
void writeGradeGlobals()
{
    if (mlt_properties globals = mlt_global_properties()) {
        mlt_properties_set_int(globals, "koracut_etalonnage_pause", s_gradePause.paused ? 1 : 0);
        mlt_properties_set_int(globals, "koracut_etalonnage_force", s_gradeBypass.load() > 0 ? 1 : 0);
    }
}

void updateGradePauseAction()
{
    if (!s_gradePauseAction) {
        return;
    }
    const bool paused = s_gradePause.paused;
    if (s_gradePauseAction->isChecked() != paused) {
        s_gradePauseAction->setChecked(paused);
    }
    s_gradePauseAction->setIcon(QIcon::fromTheme(paused ? QStringLiteral("koracut-etalonnage-pause") : QStringLiteral("koracut-etalonnage-actif")));
    s_gradePauseAction->setToolTip(
        paused ? i18nc("@info:tooltip", "Grading is paused in the preview: playback is lighter. The export always keeps the grading. Click to bring it back.")
               : i18nc("@info:tooltip", "Pause grading in the preview for lighter playback while you edit. The export always keeps the grading."));
}

void applyGradePause()
{
    writeGradeGlobals();
    updateGradePauseAction();
    if (pCore && pCore->monitorManager()) {
        pCore->monitorManager()->refreshMonitors();
    }
}
} // namespace

bool gradePauseOnModule(GradePauseState &state, bool colorModule)
{
    if (colorModule && state.paused) {
        // On ne peut pas étalonner à l'aveugle : l'étalonnage revient le temps du module.
        state.paused = false;
        state.resumedByColor = true;
        return true;
    }
    if (!colorModule && state.resumedByColor) {
        state.resumedByColor = false;
        state.paused = true;
        return true;
    }
    return false;
}

bool gradePauseSet(GradePauseState &state, bool paused)
{
    state.resumedByColor = false;
    if (state.paused == paused) {
        return false;
    }
    state.paused = paused;
    return true;
}

bool gradePaused()
{
    return s_gradePause.paused;
}

void setGradePaused(bool paused)
{
    if (gradePauseSet(s_gradePause, paused)) {
        qInfo().noquote() << "KORACUT-PERF étalonnage de l'aperçu" << (paused ? "en pause" : "actif");
    }
    applyGradePause();
}

QAction *gradePauseAction()
{
    if (!s_gradePauseAction) {
        QObject *parent = pCore && pCore->window() ? static_cast<QObject *>(pCore->window()) : static_cast<QObject *>(qApp);
        s_gradePauseAction = new QAction(i18nc("@action", "Pause Grading in Preview"), parent);
        s_gradePauseAction->setObjectName(QStringLiteral("koracut_grade_pause"));
        s_gradePauseAction->setCheckable(true);
        s_gradePauseAction->setWhatsThis(xi18nc("@info:whatsthis", "Turns off all grading in the monitors so that playback stays smooth while you edit. "
                                                                  "Click again to bring it back. The export and the background render always keep the grading, "
                                                                  "and the Color module brings it back while you work in it."));
        QObject::connect(s_gradePauseAction, &QAction::triggered, s_gradePauseAction, [](bool checked) { setGradePaused(checked); });
        updateGradePauseAction();
    }
    return s_gradePauseAction;
}

void initGradePause()
{
    gradePauseAction();
    writeGradeGlobals();
    if (!pCore) {
        return;
    }
    QObject::connect(pCore.get(), &Core::loadLayoutById, s_gradePauseAction, [](const QString &id) {
        const bool color = id == QLatin1String("etalonnage");
        if (!gradePauseOnModule(s_gradePause, color)) {
            return;
        }
        applyGradePause();
        if (color) {
            pCore->displayMessage(i18nc("@info:status", "Grading is back so you can see what you do. It pauses again when you leave the Color module."),
                                  InformationMessage, 5000);
        }
    });
}

GradeBypass::GradeBypass()
{
    if (++s_gradeBypass == 1) {
        writeGradeGlobals();
    }
}

GradeBypass::~GradeBypass()
{
    if (--s_gradeBypass == 0) {
        writeGradeGlobals();
    }
}

namespace HeavyEffects {

namespace {
std::mutex s_mutex;
std::vector<std::unique_ptr<Mlt::Filter>> s_paused;
bool s_suspendedForSave{false};

void walk(Mlt::Service &service, int depth, std::vector<std::unique_ptr<Mlt::Filter>> &out)
{
    if (depth > 8 || !service.is_valid()) {
        return;
    }
    for (int i = 0; i < service.filter_count(); ++i) {
        std::unique_ptr<Mlt::Filter> filter(service.filter(i));
        if (!filter || !filter->is_valid()) {
            continue;
        }
        const QString name = QString::fromLatin1(filter->get("mlt_service"));
        if (filter->get_int("disable") == 0 && isHeavy(name)) {
            filter->set("disable", 1);
            filter->set("_koracut_simplifie", 1);
            out.push_back(std::move(filter));
        }
    }
    switch (service.type()) {
    case mlt_service_tractor_type: {
        Mlt::Tractor tractor(service);
        for (int t = 0; t < tractor.count(); ++t) {
            std::unique_ptr<Mlt::Producer> track(tractor.track(t));
            if (track && track->is_valid()) {
                walk(*track, depth + 1, out);
            }
        }
        break;
    }
    case mlt_service_playlist_type: {
        Mlt::Playlist playlist(service);
        for (int c = 0; c < playlist.count(); ++c) {
            std::unique_ptr<Mlt::Producer> clip(playlist.get_clip(c));
            if (!clip || !clip->is_valid() || clip->is_blank()) {
                continue;
            }
            walk(*clip, depth + 1, out);
            // Les effets d'un clip sont posés sur le morceau (cut) ; une séquence imbriquée est un tractor parent.
            std::unique_ptr<Mlt::Producer> parent(new Mlt::Producer(clip->parent()));
            if (parent->is_valid() && parent->type() == mlt_service_tractor_type) {
                walk(*parent, depth + 1, out);
            }
        }
        break;
    }
    default:
        break;
    }
}
} // namespace

const QStringList &list()
{
    static const QStringList names = [] {
        QStringList l;
        QFile f(QStringLiteral(":data/heavy_effects.txt"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                const QString t = line.trimmed();
                if (!t.isEmpty() && !t.startsWith(QLatin1Char('#'))) {
                    l << t;
                }
            }
        }
        return l;
    }();
    return names;
}

bool isHeavy(const QString &service)
{
    return !service.isEmpty() && list().contains(service);
}

int simplifyCurrentTimeline()
{
    if (!pCore || !pCore->window()) {
        return 0;
    }
    TimelineWidget *widget = pCore->window()->getCurrentTimeline();
    if (!widget || !widget->controller() || !widget->controller()->getModel()) {
        return 0;
    }
    return simplify(widget->controller()->getModel()->tractor());
}

int simplify(Mlt::Tractor *tractor)
{
    if (!tractor || !tractor->is_valid()) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    walk(*tractor, 0, s_paused);
    return int(s_paused.size());
}

void restore()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto &f : s_paused) {
        if (f->get_int("_koracut_simplifie")) {
            f->set("disable", 0);
            f->clear("_koracut_simplifie");
        }
    }
    s_paused.clear();
}

int count()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return int(s_paused.size());
}

SaveGuard::SaveGuard()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_paused.empty() || s_suspendedForSave) {
        return;
    }
    for (auto &f : s_paused) {
        f->set("disable", 0);
        f->clear("_koracut_simplifie");
    }
    s_suspendedForSave = true;
    m_active = true;
}

SaveGuard::~SaveGuard()
{
    if (!m_active) {
        return;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto &f : s_paused) {
        f->set("disable", 1);
        f->set("_koracut_simplifie", 1);
    }
    s_suspendedForSave = false;
}

} // namespace HeavyEffects

} // namespace KoraCutPerformance
