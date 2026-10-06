/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutlightcopies.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "jobs/taskmanager.h"
#include "kdenlivesettings.h"
#include "koracuthwdecode.h"
#include "koracutperformance.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "utils/machineprofile.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QThread>
#include <algorithm>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <sys/types.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {
const qint64 kMega = 1024 * 1024;
// Ce qui signe nos ffmpeg dans la liste des processus, et nos fichiers dans le dossier des proxys.
const QLatin1String kMarker("koracut-copie-legere");

#if defined(Q_OS_WIN)
// Même moyen que le rendu d'aperçu (previewmanager.cpp) : NtSuspendProcess et NtResumeProcess.
bool suspendWindowsProcess(qint64 pid, bool suspend)
{
    using NtProcessFn = LONG(NTAPI *)(HANDLE);
    static const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    static const auto suspendFn = ntdll ? reinterpret_cast<NtProcessFn>(reinterpret_cast<void *>(GetProcAddress(ntdll, "NtSuspendProcess"))) : nullptr;
    static const auto resumeFn = ntdll ? reinterpret_cast<NtProcessFn>(reinterpret_cast<void *>(GetProcAddress(ntdll, "NtResumeProcess"))) : nullptr;
    const NtProcessFn fn = suspend ? suspendFn : resumeFn;
    if (!fn || pid <= 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, DWORD(pid));
    if (!process) {
        return false;
    }
    const LONG status = fn(process);
    CloseHandle(process);
    return status >= 0;
}
#endif

/** La personne lit ou fait un geste : l'encodage attend (même règle que le rendu d'aperçu, plus le moniteur de clip). */
bool userIsBusy()
{
    if (QGuiApplication::mouseButtons() != Qt::NoButton) {
        return true;
    }
    if (!pCore || !pCore->monitorManager()) {
        return false;
    }
    Monitor *project = pCore->monitorManager()->projectMonitor();
    Monitor *clip = pCore->monitorManager()->clipMonitor();
    return (project && project->isPlaying()) || (clip && clip->isPlaying());
}
} // namespace

// ---- Les règles -----------------------------------------------------------------------------

namespace KoraCutLightCopy {

bool isIntraCodec(const QString &codec)
{
    const QString c = codec.toLower();
    static const QStringList intra = {QStringLiteral("prores"), QStringLiteral("dnxh"), QStringLiteral("mjpeg"), QStringLiteral("cfhd"),  QStringLiteral("dvvideo"),
                                      QStringLiteral("ffv1"),   QStringLiteral("huffyuv"), QStringLiteral("png"), QStringLiteral("qtrle"), QStringLiteral("rawvideo"),
                                      QStringLiteral("jpeg2000"), QStringLiteral("magicyuv"), QStringLiteral("utvideo"), QStringLiteral("hap")};
    return std::any_of(intra.cbegin(), intra.cend(), [&c](const QString &name) { return c.contains(name); });
}

bool isHeavy(const Source &source)
{
    if (source.hasAlpha || source.width <= 0 || source.height <= 0 || isIntraCodec(source.codec)) {
        return false;
    }
    if (MachineProfile::isHeavyCodec(source.codec, source.pixFormat)) {
        return true;
    }
    // 4K et plus, quel que soit le codec : petit côté de 2160, ou grand côté de 3840.
    return std::min(source.width, source.height) >= 2160 || std::max(source.width, source.height) >= 3840;
}

int targetShortSide(const QSize &previewFrame)
{
    const int side = std::min(previewFrame.width(), previewFrame.height());
    return side >= 720 ? 720 : 540;
}

int shortSideFor(int width, int height, int target)
{
    int side = std::min(target, std::min(width, height));
    side -= side % 2;
    return std::max(2, side);
}

Encoder encoderFor(const QStringList &supportedHwCodecs)
{
#if defined(Q_OS_MACOS)
    if (supportedHwCodecs.contains(QStringLiteral("h264_videotoolbox"))) {
        return Encoder::VideoToolbox;
    }
#else
    if (supportedHwCodecs.contains(QStringLiteral("h264_nvenc"))) {
        return Encoder::Nvenc;
    }
    if (supportedHwCodecs.contains(QStringLiteral("h264_qsv"))) {
        return Encoder::Qsv;
    }
    if (supportedHwCodecs.contains(QStringLiteral("h264_amf"))) {
        return Encoder::Amf;
    }
#endif
    return Encoder::Software;
}

QString encoderId(Encoder encoder)
{
    switch (encoder) {
    case Encoder::VideoToolbox:
        return QStringLiteral("h264_videotoolbox");
    case Encoder::Nvenc:
        return QStringLiteral("h264_nvenc");
    case Encoder::Qsv:
        return QStringLiteral("h264_qsv");
    case Encoder::Amf:
        return QStringLiteral("h264_amf");
    case Encoder::Software:
        break;
    }
    return QStringLiteral("libx264");
}

QString hwDecoderFor(Encoder encoder)
{
    // Mesuré sur M2 Pro : décoder le HEVC 10 bits par la puce divise par sept le temps de processeur de
    // l'encodage (1,95 s au lieu de 14,6 s pour 16 s de rush). Ailleurs, pas mesuré : décodage logiciel.
    return encoder == Encoder::VideoToolbox ? QStringLiteral("videotoolbox") : QString();
}

QStringList ffmpegArguments(const QString &source, const QString &dest, int shortSide, Encoder encoder, const QString &hwDecoder, bool keepCodedOrientation,
                            int decodeThreads, const QStringList &streams)
{
    QStringList args = {QStringLiteral("-hide_banner"), QStringLiteral("-y"),         QStringLiteral("-nostdin"),           QStringLiteral("-v"),
                        QStringLiteral("error"),        QStringLiteral("-nostats"),   QStringLiteral("-progress"),          QStringLiteral("pipe:1"),
                        QStringLiteral("-protocol_whitelist"), QStringLiteral("file,pipe")};
    if (keepCodedOrientation) {
        args << QStringLiteral("-noautorotate");
    }
    if (!hwDecoder.isEmpty()) {
        args << QStringLiteral("-hwaccel") << hwDecoder;
    } else if (decodeThreads > 0) {
        args << QStringLiteral("-threads") << QString::number(decodeThreads);
    }
    args << QStringLiteral("-i") << source;
    // Le petit côté à la taille voulue, quel que soit le sens de l'image ; 8 bits 4:2:0.
    const QString side = QString::number(shortSide);
    const QString format = encoder == Encoder::Software ? QStringLiteral("yuv420p") : QStringLiteral("nv12");
    args << QStringLiteral("-vf") << QStringLiteral("scale=w='if(gt(iw,ih),-2,%1)':h='if(gt(iw,ih),%1,-2)',format=%2").arg(side, format);
    args << QStringLiteral("-fps_mode") << QStringLiteral("cfr") << QStringLiteral("-c:v") << encoderId(encoder);
    // Tout-intra, sans image B : chaque image se décode seule.
    args << QStringLiteral("-g") << QStringLiteral("1") << QStringLiteral("-bf") << QStringLiteral("0");
    switch (encoder) {
    case Encoder::VideoToolbox:
        // Profil baseline (CAVLC) : relu deux fois plus vite que le profil high, pour 8 % de place en plus.
        args << QStringLiteral("-profile:v") << QStringLiteral("baseline") << QStringLiteral("-q:v") << QStringLiteral("50");
        break;
    case Encoder::Nvenc:
        args << QStringLiteral("-rc") << QStringLiteral("constqp") << QStringLiteral("-qp") << QStringLiteral("26");
        break;
    case Encoder::Qsv:
    case Encoder::Amf:
        // Les paramètres des profils de proxy de Kdenlive (data/encodingprofiles.rc).
        args << QStringLiteral("-qp") << QStringLiteral("26");
        break;
    case Encoder::Software:
        args << QStringLiteral("-preset") << QStringLiteral("ultrafast") << QStringLiteral("-tune") << QStringLiteral("fastdecode") << QStringLiteral("-crf")
             << QStringLiteral("23");
        break;
    }
    // Le son comme les proxys de Kdenlive : mêmes flux, dans le même ordre, sans les flux de données.
    args << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("128k");
    args << QStringLiteral("-sn") << QStringLiteral("-dn");
    if (streams.isEmpty()) {
        args << QStringLiteral("-map") << QStringLiteral("0:V:0") << QStringLiteral("-map") << QStringLiteral("0:a:0?");
    } else {
        for (const QString &stream : streams) {
            args << QStringLiteral("-map") << QStringLiteral("0:%1").arg(stream);
        }
    }
    args << QStringLiteral("-metadata") << QStringLiteral("comment=%1").arg(kMarker);
    args << dest;
    return args;
}

qint64 estimateBytes(double seconds, int shortSide, Encoder encoder)
{
    double megaPerMinute = encoder == Encoder::Software ? 80. : 50.;
    megaPerMinute *= double(shortSide) * shortSide / (540. * 540.) * (shortSide > 540 ? 0.8 : 1.);
    return qint64(std::max(1., seconds) / 60. * megaPerMinute * kMega);
}

QString copyFileName(const QString &hash, int shortSide)
{
    return QStringLiteral("%1-kc%2.mp4").arg(hash).arg(shortSide);
}

bool isCopyFile(const QString &fileName)
{
    static const QRegularExpression re(QStringLiteral("^.+-kc\\d+\\.mp4$"));
    return re.match(QFileInfo(fileName).fileName()).hasMatch();
}

Plan plan(const QList<File> &files, qint64 freeBytes, qint64 neededBytes, const Budget &budget)
{
    Plan result;
    qint64 used = 0;
    for (const File &f : files) {
        used += f.size;
    }
    QList<File> candidates;
    for (const File &f : files) {
        if (!f.inUse) {
            candidates << f;
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const File &a, const File &b) { return a.lastUsedSecs < b.lastUsedSecs; });
    qint64 free = freeBytes;
    // D'abord le plafond : les plus anciennes copies partent, tant qu'il le faut.
    while (budget.capBytes > 0 && used + neededBytes > budget.capBytes && !candidates.isEmpty()) {
        const File f = candidates.takeFirst();
        result.evict << f.path;
        used -= f.size;
        free += f.size;
    }
    if (budget.capBytes > 0 && used + neededBytes > budget.capBytes) {
        result.verdict = Verdict::CapReached;
        result.evict.clear();
        return result;
    }
    // Puis le disque : on ne descend jamais sous le plancher, même en effaçant d'anciennes copies.
    while (free - neededBytes < budget.minFreeBytes && !candidates.isEmpty()) {
        const File f = candidates.takeFirst();
        result.evict << f.path;
        free += f.size;
    }
    if (free - neededBytes < budget.minFreeBytes) {
        result.verdict = Verdict::DiskFull;
        result.evict.clear();
    }
    return result;
}

bool onLowBattery()
{
    const int floor = 30;
#if defined(Q_OS_MACOS)
    QProcess pmset;
    pmset.start(QStringLiteral("/usr/bin/pmset"), {QStringLiteral("-g"), QStringLiteral("batt")});
    if (!pmset.waitForFinished(400)) {
        pmset.kill();
        pmset.waitForFinished(100);
        return false;
    }
    const QString out = QString::fromLocal8Bit(pmset.readAllStandardOutput());
    if (!out.contains(QLatin1String("Battery Power"))) {
        return false;
    }
    static const QRegularExpression percent(QStringLiteral("(\\d+)%"));
    const QRegularExpressionMatch m = percent.match(out);
    return m.hasMatch() && m.captured(1).toInt() < floor;
#elif defined(Q_OS_WIN)
    SYSTEM_POWER_STATUS status;
    if (!GetSystemPowerStatus(&status)) {
        return false;
    }
    return status.ACLineStatus == 0 && status.BatteryLifePercent != 255 && status.BatteryLifePercent < floor;
#else
    const QDir supplies(QStringLiteral("/sys/class/power_supply"));
    const QStringList batteries = supplies.entryList({QStringLiteral("BAT*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : batteries) {
        QFile statusFile(supplies.filePath(name + QStringLiteral("/status")));
        QFile capacityFile(supplies.filePath(name + QStringLiteral("/capacity")));
        if (!statusFile.open(QIODevice::ReadOnly) || !capacityFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        const bool discharging = QString::fromLatin1(statusFile.readAll()).trimmed() == QLatin1String("Discharging");
        const int capacity = QString::fromLatin1(capacityFile.readAll()).trimmed().toInt();
        if (discharging && capacity > 0 && capacity < floor) {
            return true;
        }
    }
    return false;
#endif
}

void Process::setPid(qint64 pid)
{
    QMutexLocker lock(&m_mutex);
    m_pid = pid;
    m_applied = false;
    apply();
}

void Process::setSuspended(bool suspended)
{
    QMutexLocker lock(&m_mutex);
    m_wanted = suspended;
    apply();
}

bool Process::suspended() const
{
    QMutexLocker lock(&m_mutex);
    return m_applied;
}

void Process::apply()
{
    if (m_pid <= 0 || m_wanted == m_applied) {
        return;
    }
#if defined(Q_OS_UNIX)
    if (::kill(pid_t(m_pid), m_wanted ? SIGSTOP : SIGCONT) == 0) {
        m_applied = m_wanted;
    }
#elif defined(Q_OS_WIN)
    if (suspendWindowsProcess(m_pid, m_wanted)) {
        m_applied = m_wanted;
    }
#endif
}

} // namespace KoraCutLightCopy

using namespace KoraCutLightCopy;

// ---- La tâche -------------------------------------------------------------------------------

KoraCutLightCopyTask::KoraCutLightCopyTask(const ObjectId &owner, const Request &request, const std::shared_ptr<KoraCutLightCopy::Process> &process,
                                           QObject *clip, QObject *receiver)
    : AbstractTask(owner, AbstractTask::LIGHTCOPYJOB, clip)
    , m_request(request)
    , m_process(process)
    , m_receiver(receiver)
{
    m_description = i18nc("@info a background task on a video", "Light copy");
}

bool KoraCutLightCopyTask::attempt(Encoder encoder, const QString &hwDecoder, const QString &partial, QString *log)
{
    QFile::remove(partial);
    // Décodage logiciel : la moitié des cœurs au plus, l'ordinateur reste disponible.
    const int threads = std::max(2, QThread::idealThreadCount() / 2);
    const QStringList args = ffmpegArguments(m_request.source, partial, m_request.shortSide, encoder, hwDecoder, m_request.keepCodedOrientation, threads,
                                             m_request.streams);
    QProcess process;
    QObject::connect(this, &KoraCutLightCopyTask::jobCanceled, &process, &QProcess::kill, Qt::DirectConnection);
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, [this, &process]() {
        // « out_time_us=1234567 » : où en est l'encodage.
        const QByteArray out = process.readAllStandardOutput();
        const int at = out.lastIndexOf("out_time_us=");
        if (at < 0 || m_request.seconds <= 0) {
            return;
        }
        const double seconds = out.mid(at + 12, out.indexOf('\n', at) - at - 12).trimmed().toDouble() / 1e6;
        const int value = std::clamp(int(100. * seconds / m_request.seconds), 0, 99);
        if (value != m_progress) {
            m_progress = value;
            QMetaObject::invokeMethod(m_object, "updateJobProgress");
        }
    });
    process.start(m_request.ffmpeg, args, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        *log += QStringLiteral("ffmpeg n'a pas démarré : %1\n").arg(process.errorString());
        return false;
    }
    AbstractTask::setPreferredPriority(process.processId());
    m_process->setPid(process.processId());
    process.waitForFinished(-1);
    m_process->setPid(0);
    const QString err = QString::fromUtf8(process.readAllStandardError());
    bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && QFileInfo(partial).size() > 0 && !m_isCanceled;
    // La puce peut accepter un flux puis échouer à le décoder sans que ffmpeg s'arrête (document 6, 1er octobre 2026).
    if (ok && !hwDecoder.isEmpty() && KoraCutHwDecode::outputSaysRefused(err)) {
        ok = false;
    }
    if (!ok) {
        *log += QStringLiteral("%1%2 : %3\n").arg(encoderId(encoder), hwDecoder.isEmpty() ? QString() : QStringLiteral(" + ") + hwDecoder, err.left(300).trimmed());
        QFile::remove(partial);
    }
    return ok;
}

void KoraCutLightCopyTask::run()
{
    AbstractTaskDone whenFinished(m_owner.itemId, this);
    const Request &r = m_request;
    QElapsedTimer clock;
    clock.start();
    QString log;
    QString used;
    auto report = [&](bool ok) {
        if (m_receiver) {
            QMetaObject::invokeMethod(m_receiver, "taskFinished", Qt::QueuedConnection, Q_ARG(QString, r.binId), Q_ARG(QString, r.source), Q_ARG(QString, r.dest),
                                      Q_ARG(bool, ok), Q_ARG(bool, bool(m_isCanceled)), Q_ARG(int, int(clock.elapsed())), Q_ARG(QString, used), Q_ARG(QString, log));
        }
    };
    if (m_isCanceled || pCore->taskManager.isBlocked()) {
        m_isCanceled.testAndSetAcquire(0, 1);
        report(false);
        return;
    }
    QMutexLocker lock(&m_runMutex);
    m_running = true;
    m_progress = 0;
    QDir().mkpath(QFileInfo(r.dest).absolutePath());
    const QString partial = r.dest.left(r.dest.size() - 4) + QStringLiteral("-part.mp4");
    // Du plus léger pour le processeur au plus sûr : puce pour lire et écrire, puce pour écrire, tout logiciel.
    const Encoder hw = encoderFor(KdenliveSettings::supportedHWCodecs());
    QList<std::pair<Encoder, QString>> attempts;
    if (hw != Encoder::Software) {
        if (!hwDecoderFor(hw).isEmpty()) {
            attempts.append({hw, hwDecoderFor(hw)});
        }
        attempts.append({hw, QString()});
    }
    attempts.append({Encoder::Software, QString()});
    bool ok = false;
    for (const auto &a : std::as_const(attempts)) {
        if (m_isCanceled) {
            break;
        }
        if (attempt(a.first, a.second, partial, &log)) {
            ok = true;
            used = encoderId(a.first) + (a.second.isEmpty() ? QString() : QStringLiteral("+") + a.second);
            break;
        }
    }
    if (ok) {
        QFile::remove(r.dest);
        ok = QFile::rename(partial, r.dest);
    }
    if (!ok) {
        QFile::remove(partial);
    }
    m_progress = 100;
    QMetaObject::invokeMethod(m_object, "updateJobProgress");
    report(ok);
}

// ---- La file --------------------------------------------------------------------------------

KoraCutLightCopies *KoraCutLightCopies::instance()
{
    static QPointer<KoraCutLightCopies> self;
    if (!self) {
        self = new KoraCutLightCopies;
    }
    return self;
}

KoraCutLightCopies::KoraCutLightCopies()
    : QObject(pCore.get())
{
    m_tick.setSingleShot(true);
    connect(&m_tick, &QTimer::timeout, this, &KoraCutLightCopies::tick);
    // Pendant la lecture et les gestes, l'encodage est suspendu (rien n'est perdu), puis il reprend.
    m_watch.setInterval(250);
    connect(&m_watch, &QTimer::timeout, this, [this]() {
        if (!m_process || m_currentBinId.isEmpty()) {
            m_watch.stop();
            return;
        }
        m_process->setSuspended(userIsBusy());
    });
}

bool KoraCutLightCopies::enabled()
{
    if (qEnvironmentVariableIsSet("KORACUT_COPIES")) {
        return qEnvironmentVariable("KORACUT_COPIES") != QLatin1String("non");
    }
    return KoraCutPerformance::group().readEntry("copiesLegeres", true);
}

void KoraCutLightCopies::setEnabled(bool on)
{
    KConfigGroup g = KoraCutPerformance::group();
    g.writeEntry("copiesLegeres", on);
    g.sync();
    KoraCutLightCopies *self = instance();
    if (on) {
        self->m_blocked.clear();
        self->rescan();
        self->schedule(300);
    } else {
        self->m_pending.clear();
        self->m_ready.clear();
        self->revertAll();
    }
}

KoraCutLightCopy::Budget KoraCutLightCopies::budget()
{
    const KConfigGroup g = KoraCutPerformance::group();
    Budget b;
    b.capBytes = qint64(std::max(256, g.readEntry("copiesLegeresPlafondMo", 6144))) * kMega;
    b.minFreeBytes = qint64(std::max(0, g.readEntry("copiesLegeresLibreMinMo", 8192))) * kMega;
    return b;
}

void KoraCutLightCopies::noteTimelineClip(const QString &binId)
{
    // Appelé à chaque plan créé (chargement, glisser d'une piste à l'autre) : rien de coûteux ici,
    // c'est tick() qui regarde le réglage et le clip.
    if (!pCore || binId.isEmpty()) {
        return;
    }
    KoraCutLightCopies *self = instance();
    if (!self->m_pending.contains(binId)) {
        self->m_pending << binId;
    }
    self->schedule();
}

void KoraCutLightCopies::schedule(int msecs)
{
    if (!m_tick.isActive() || m_tick.remainingTime() > msecs) {
        m_tick.start(msecs);
    }
}

int KoraCutLightCopies::shortSide() const
{
    const int forced = KoraCutPerformance::group().readEntry("copiesLegeresHauteur", 0);
    if (forced >= 180) {
        return forced - forced % 2;
    }
    // L'image que le moniteur calcule : celle du projet, ramenée à la hauteur d'aperçu choisie.
    const QSize frame = pCore->getCurrentFrameDisplaySize();
    const int scaling = KdenliveSettings::previewScaling();
    // Cran 1 : 1080 de haut au plus (VideoWidget::updateScaling), 2 : 720, 4 : 540…
    int height = scaling >= 1 ? MachineProfile::scalingHeight(scaling) : frame.height();
    height = std::min(height, frame.height());
    const int width = frame.height() > 0 ? frame.width() * height / frame.height() : height;
    return targetShortSide(QSize(width, height));
}

QString KoraCutLightCopies::destFor(const std::shared_ptr<ProjectClip> &clip, int side) const
{
    bool ok = false;
    const QDir dir = pCore->currentDoc()->getCacheDir(CacheProxy, &ok);
    if (!ok) {
        return {};
    }
    QString hash = clip->hash();
    if (hash.isEmpty()) {
        const QFileInfo info(clip->url());
        hash = QString::fromLatin1(
            QCryptographicHash::hash(QStringLiteral("%1/%2/%3").arg(info.absoluteFilePath()).arg(info.size()).arg(info.lastModified().toSecsSinceEpoch()).toUtf8(),
                                     QCryptographicHash::Md5)
                .toHex());
    }
    return dir.absoluteFilePath(copyFileName(hash, side));
}

QList<KoraCutLightCopy::File> KoraCutLightCopies::files() const
{
    QList<File> list;
    if (!pCore || !pCore->currentDoc()) {
        return list;
    }
    bool ok = false;
    const QDir dir = pCore->currentDoc()->getCacheDir(CacheProxy, &ok);
    if (!ok) {
        return list;
    }
    QSet<QString> held;
    for (const QString &id : pCore->projectItemModel()->getAllClipIds()) {
        if (auto clip = pCore->projectItemModel()->getClipByBinID(id)) {
            const QString proxy = clip->getProducerProperty(QStringLiteral("kdenlive:proxy"));
            if (proxy.size() > 2) {
                held << QFileInfo(proxy).fileName();
            }
        }
    }
    for (auto it = m_ready.constBegin(); it != m_ready.constEnd(); ++it) {
        held << QFileInfo(it.value()).fileName();
    }
    const QFileInfoList infos = dir.entryInfoList({QStringLiteral("*-kc*.mp4")}, QDir::Files);
    for (const QFileInfo &info : infos) {
        if (!isCopyFile(info.fileName())) {
            continue;
        }
        File f;
        f.path = info.absoluteFilePath();
        f.size = info.size();
        f.lastUsedSecs = info.lastModified().toSecsSinceEpoch();
        f.inUse = held.contains(info.fileName());
        list << f;
    }
    return list;
}

void KoraCutLightCopies::rescan()
{
    if (!pCore || !pCore->currentDoc() || !pCore->projectItemModel()) {
        return;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const QString &id : pCore->projectItemModel()->getAllClipIds()) {
        auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip) {
            continue;
        }
        // Une copie tenue par un projet qu'on ouvre redevient récente : elle partira après les autres.
        const QString proxy = clip->getProducerProperty(QStringLiteral("kdenlive:proxy"));
        if (proxy.size() > 2 && isCopyFile(proxy)) {
            const QFileInfo info(proxy);
            if (info.exists() && now - info.lastModified().toSecsSinceEpoch() > 3600) {
                QFile f(proxy);
                if (f.open(QIODevice::ReadWrite)) {
                    f.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
                }
            }
            continue;
        }
        if (clip->isIncludedInTimeline() && !m_pending.contains(id)) {
            m_pending << id;
        }
    }
    // Un encodage coupé net (plantage, arrêt forcé) laisse un fichier partiel : il part après six heures.
    bool ok = false;
    const QDir dir = pCore->currentDoc()->getCacheDir(CacheProxy, &ok);
    if (ok) {
        const QFileInfoList parts = dir.entryInfoList({QStringLiteral("*-kc*-part.mp4")}, QDir::Files);
        for (const QFileInfo &info : parts) {
            if (now - info.lastModified().toSecsSinceEpoch() > 6 * 3600) {
                QFile::remove(info.absoluteFilePath());
            }
        }
    }
}

void KoraCutLightCopies::block(const QString &reason, const QString &message)
{
    m_blocked = reason;
    qWarning().noquote() << "KORACUT-COPIES en attente :" << reason;
    if (!m_told.contains(reason) && !message.isEmpty()) {
        m_told << reason;
        pCore->displayMessage(message, InformationMessage, 8000);
    }
}

void KoraCutLightCopies::applyReady()
{
    // Une copie à la fois : poser la copie recharge le clip et ses plans dans la timeline.
    const QString binId = m_ready.firstKey();
    const QString dest = m_ready.take(binId);
    auto clip = pCore->projectItemModel()->getClipByBinID(binId);
    if (!clip || !clip->statusReady() || !QFileInfo::exists(dest)) {
        return;
    }
    const QString proxy = clip->getProducerProperty(QStringLiteral("kdenlive:proxy"));
    if (clip->hasProxy() || proxy == QLatin1String("-") || QFileInfo(dest).fileName() != QFileInfo(destFor(clip, shortSide())).fileName()) {
        return;
    }
    keepClean();
    clip->setProperties({{QStringLiteral("kdenlive:proxy"), dest}});
}

void KoraCutLightCopies::keepClean()
{
    KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    if (!doc || !pCore->undoStack()) {
        return;
    }
    if (!m_cleanTimer.isSingleShot()) {
        m_cleanTimer.setSingleShot(true);
        m_cleanTimer.setInterval(2500);
        connect(&m_cleanTimer, &QTimer::timeout, this, [this]() {
            KdenliveDoc *current = pCore ? pCore->currentDoc() : nullptr;
            const int wanted = m_cleanUndoIndex;
            m_cleanUndoIndex = -1;
            // Rien n'a été fait par la personne entre-temps : même indice d'historique.
            if (current && wanted >= 0 && pCore->undoStack() && pCore->undoStack()->index() == wanted && current->isModified()) {
                current->setModified(false);
            }
        });
    }
    if (m_cleanUndoIndex < 0) {
        if (doc->isModified()) {
            return; // la personne a déjà des changements non enregistrés : on ne touche à rien
        }
        m_cleanUndoIndex = pCore->undoStack()->index();
    }
    m_cleanTimer.start();
}

void KoraCutLightCopies::revertAll()
{
    if (!pCore || !pCore->currentDoc() || !pCore->projectItemModel()) {
        return;
    }
    if (!m_currentBinId.isEmpty()) {
        pCore->taskManager.discardJobs(ObjectId(KdenliveObjectType::BinClip, m_currentBinId.toInt(), QUuid()), AbstractTask::LIGHTCOPYJOB);
    }
    keepClean();
    for (const QString &id : pCore->projectItemModel()->getAllClipIds()) {
        auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (clip && clip->hasProxy() && isCopyFile(clip->getProducerProperty(QStringLiteral("kdenlive:proxy")))) {
            // Vide, et pas « - » : « - » veut dire que la personne a refusé le proxy de ce clip.
            clip->setProperties({{QStringLiteral("kdenlive:proxy"), QString()}});
        }
    }
}

void KoraCutLightCopies::tick()
{
    if (!pCore || !pCore->currentDoc() || !pCore->projectItemModel() || pCore->closing) {
        return;
    }
    KdenliveDoc *doc = pCore->currentDoc();
    if (doc->loading || pCore->taskManager.isBlocked()) {
        schedule(1500);
        return;
    }
    if (m_doc != doc) {
        // Un autre projet : les identifiants du chutier ne valent plus rien, on relit tout.
        m_doc = doc;
        m_pending.clear();
        m_ready.clear();
        m_blocked.clear();
        m_done = 0;
        m_encodeMs = 0;
        m_announced = false;
        rescan();
    }
    if (!enabled()) {
        m_pending.clear();
        return;
    }
    if (!m_ready.isEmpty()) {
        if (userIsBusy()) {
            schedule(500);
            return;
        }
        applyReady();
        schedule(m_ready.isEmpty() ? 300 : 400);
        return;
    }
    if (!m_currentBinId.isEmpty()) {
        return; // taskFinished relancera
    }
    if (!startNext() && m_currentBinId.isEmpty() && m_pending.isEmpty() && m_announced) {
        m_announced = false;
        qint64 used = 0;
        const auto all = files();
        for (const File &f : all) {
            used += f.size;
        }
        qWarning().noquote() << QStringLiteral("KORACUT-COPIES fin copies=%1 encodage_ms=%2 place_mo=%3 encodeur=%4")
                                    .arg(m_done)
                                    .arg(m_encodeMs)
                                    .arg(used / kMega)
                                    .arg(m_lastEncoder);
        pCore->displayMessage(i18ncp("@info:status", "The light copy of %1 heavy video is ready: scrubbing is now smooth.",
                                     "The light copies of %1 heavy videos are ready: scrubbing is now smooth.", m_done),
                              InformationMessage, 6000);
    }
}

bool KoraCutLightCopies::startNext()
{
    const int side = shortSide();
    const QUuid active = pCore->currentTimelineId();
    auto timeline = pCore->currentDoc()->getTimeline(active, true);
    struct Candidate {
        QString binId;
        std::shared_ptr<ProjectClip> clip;
        QString dest;
        int position{INT_MAX};
        int side{540};
    };
    QList<Candidate> candidates;
    QStringList keep;
    for (const QString &id : std::as_const(m_pending)) {
        auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip) {
            continue;
        }
        if (!clip->statusReady()) {
            keep << id; // pas encore chargé : on y revient
            continue;
        }
        const ClipType::ProducerType type = clip->clipType();
        const QString proxy = clip->getProducerProperty(QStringLiteral("kdenlive:proxy"));
        if ((type != ClipType::AV && type != ClipType::Video) || !clip->isIncludedInTimeline() || clip->hasProxy() || proxy == QLatin1String("-") ||
            m_failed.contains(clip->url()) || !QFileInfo(clip->url()).isFile() ||
            pCore->taskManager.hasPendingJob(ObjectId(KdenliveObjectType::BinClip, id.toInt(), QUuid()), AbstractTask::PROXYJOB)) {
            continue;
        }
        Source source;
        source.codec = clip->videoCodecProperty(QStringLiteral("name"));
        source.pixFormat = clip->videoCodecProperty(QStringLiteral("pix_fmt"));
        source.width = clip->getProducerIntProperty(QStringLiteral("meta.media.width"));
        source.height = clip->getProducerIntProperty(QStringLiteral("meta.media.height"));
        source.hasAlpha = clip->hasAlpha();
        if (!isHeavy(source)) {
            continue;
        }
        Candidate c;
        c.binId = id;
        c.clip = clip;
        c.side = shortSideFor(source.width, source.height, side);
        c.dest = destFor(clip, c.side);
        if (c.dest.isEmpty()) {
            continue;
        }
        if (QFileInfo(c.dest).size() > 0) {
            // Déjà faite (un autre projet, une session précédente) : il n'y a qu'à la poser.
            m_ready.insert(id, c.dest);
            continue;
        }
        // Les plans les plus proches du début de la séquence ouverte d'abord.
        if (timeline) {
            const QList<int> instances = clip->timelineInstances(active);
            for (int cid : instances) {
                if (timeline->isClip(cid)) {
                    c.position = std::min(c.position, timeline->getClipPosition(cid));
                }
            }
        }
        candidates << c;
        keep << id;
    }
    m_pending = keep;
    if (!m_ready.isEmpty()) {
        schedule(200);
    }
    if (candidates.isEmpty()) {
        // Des clips encore en chargement : on repasse, sans insister plus de deux minutes.
        if (!m_pending.isEmpty() && ++m_retries <= 40) {
            schedule(3000);
        } else {
            m_pending.clear();
        }
        return false;
    }
    m_retries = 0;
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.position < b.position; });
    const Candidate next = candidates.constFirst();
    // KORACUT_COPIES=forcer : le banc d'essai passe outre la batterie.
    if (qEnvironmentVariable("KORACUT_COPIES") != QLatin1String("forcer") && onLowBattery()) {
        block(QStringLiteral("batterie"), i18nc("@info:status", "Light copies are waiting: the battery is low."));
        schedule(120000);
        return false;
    }
    const Encoder encoder = encoderFor(KdenliveSettings::supportedHWCodecs());
    const double seconds = next.clip->duration().seconds();
    const qint64 needed = estimateBytes(seconds, next.side, encoder);
    bool ok = false;
    const QDir dir = pCore->currentDoc()->getCacheDir(CacheProxy, &ok);
    const Budget b = budget();
    const Plan p = plan(files(), QStorageInfo(dir.absolutePath()).bytesAvailable(), needed, b);
    if (p.verdict == Verdict::DiskFull) {
        block(QStringLiteral("disque"),
              i18nc("@info:status", "Light copies are paused: less than %1 GB free on the disk.", QString::number(b.minFreeBytes / 1024. / kMega, 'f', 0)));
        return false;
    }
    if (p.verdict == Verdict::CapReached) {
        block(QStringLiteral("plafond"), i18nc("@info:status", "Light copies are paused: the %1 GB limit is reached by this project.",
                                               QString::number(b.capBytes / 1024. / kMega, 'f', 0)));
        return false;
    }
    for (const QString &path : p.evict) {
        QFile::remove(path);
        qInfo().noquote() << "KORACUT-COPIES ancienne copie retirée" << path;
    }
    m_blocked.clear();
    KoraCutLightCopyTask::Request request;
    request.binId = next.binId;
    request.source = next.clip->url();
    request.dest = next.dest;
    request.shortSide = next.side;
    // Rotation imposée par la personne sur le clip : MLT l'appliquera à la copie comme à l'original.
    request.keepCodedOrientation = next.clip->hasProducerProperty(QStringLiteral("rotate")) || next.clip->hasProducerProperty(QStringLiteral("autorotate"));
    request.seconds = seconds;
    // Les flux que MLT a su lire, dans l'ordre de la source : la vidéo du clip et les sons qui ont un décodeur.
    const int streamCount = next.clip->getProducerIntProperty(QStringLiteral("meta.media.nb_streams"));
    const int videoIndex = next.clip->getProducerIntProperty(QStringLiteral("video_index"));
    bool hasVideo = false;
    for (int i = 0; i < streamCount; ++i) {
        const QString type = next.clip->getProducerProperty(QStringLiteral("meta.media.%1.stream.type").arg(i));
        const QString codec = next.clip->getProducerProperty(QStringLiteral("meta.media.%1.codec.name").arg(i));
        if (type == QLatin1String("video") && i == videoIndex) {
            request.streams << QString::number(i);
            hasVideo = true;
        } else if (type == QLatin1String("audio") && !codec.isEmpty() && codec != QLatin1String("none") && codec != QLatin1String("unknown")) {
            request.streams << QString::number(i);
        }
    }
    if (!hasVideo) {
        request.streams.clear();
    }
    request.ffmpeg = KdenliveSettings::ffmpegpath();
    if (!QFileInfo(request.ffmpeg).isFile()) {
        return false;
    }
    m_pending.removeAll(next.binId);
    m_currentBinId = next.binId;
    m_currentName = next.clip->clipName();
    m_process = std::make_shared<Process>();
    if (!m_announced) {
        m_announced = true;
        pCore->displayMessage(i18ncp("@info:status", "Preparing a light copy of %1 heavy video in the background, for smooth scrubbing…",
                                     "Preparing light copies of %1 heavy videos in the background, for smooth scrubbing…", int(candidates.size())),
                              InformationMessage, 6000);
    }
    const ObjectId owner(KdenliveObjectType::BinClip, next.binId.toInt(), QUuid());
    auto *task = new KoraCutLightCopyTask(owner, request, m_process, next.clip.get(), this);
    pCore->taskManager.startTask(owner.itemId, task);
    m_watch.start();
    return true;
}

void KoraCutLightCopies::taskFinished(const QString &binId, const QString &source, const QString &dest, bool ok, bool canceled, int elapsedMs,
                                      const QString &encoder, const QString &log)
{
    if (m_currentBinId == binId) {
        m_currentBinId.clear();
        m_currentName.clear();
        m_process.reset();
    }
    if (ok) {
        ++m_done;
        m_encodeMs += elapsedMs;
        m_lastEncoder = encoder;
        m_ready.insert(binId, dest);
        qInfo().noquote() << QStringLiteral("KORACUT-COPIES faite %1 en %2 ms, %3 Mo (%4)")
                                 .arg(QFileInfo(source).fileName())
                                 .arg(elapsedMs)
                                 .arg(QFileInfo(dest).size() / double(kMega), 0, 'f', 1)
                                 .arg(encoder);
    } else if (!canceled) {
        // Pas de nouvel essai dans cette session : le clip reste sur son original.
        m_failed << source;
        qWarning().noquote() << "KORACUT-COPIES échec" << source << log;
    }
    if (enabled()) {
        schedule(200);
    }
}

bool KoraCutLightCopies::projectUsesCopies()
{
    if (!pCore || !pCore->projectItemModel()) {
        return false;
    }
    for (const QString &id : pCore->projectItemModel()->getAllClipIds()) {
        auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (clip && clip->hasProxy() && isCopyFile(clip->getProducerProperty(QStringLiteral("kdenlive:proxy")))) {
            return true;
        }
    }
    return false;
}

bool KoraCutLightCopies::busy()
{
    KoraCutLightCopies *self = instance();
    return !self->m_currentBinId.isEmpty() || !self->m_ready.isEmpty() || (!self->m_pending.isEmpty() && self->m_tick.isActive());
}

QJsonObject KoraCutLightCopies::stateJson()
{
    KoraCutLightCopies *self = instance();
    int done = 0;
    if (pCore && pCore->projectItemModel()) {
        for (const QString &id : pCore->projectItemModel()->getAllClipIds()) {
            auto clip = pCore->projectItemModel()->getClipByBinID(id);
            if (clip && clip->hasProxy() && isCopyFile(clip->getProducerProperty(QStringLiteral("kdenlive:proxy")))) {
                ++done;
            }
        }
    }
    qint64 used = 0;
    const auto all = self->files();
    for (const File &f : all) {
        used += f.size;
    }
    const Budget b = budget();
    QString folder;
    if (pCore && pCore->currentDoc()) {
        bool ok = false;
        folder = pCore->currentDoc()->getCacheDir(CacheProxy, &ok).absolutePath();
    }
    return QJsonObject{{QStringLiteral("actif"), enabled()},
                       {QStringLiteral("faites"), done},
                       {QStringLiteral("en_attente"), int(self->m_pending.size() + self->m_ready.size())},
                       {QStringLiteral("en_cours"), self->m_currentName},
                       {QStringLiteral("en_pause"), self->m_process ? self->m_process->suspended() : false},
                       {QStringLiteral("echecs"), int(self->m_failed.size())},
                       {QStringLiteral("bloque"), self->m_blocked},
                       {QStringLiteral("place_mo"), int(used / kMega)},
                       {QStringLiteral("fichiers"), int(all.size())},
                       {QStringLiteral("plafond_mo"), int(b.capBytes / kMega)},
                       {QStringLiteral("libre_min_mo"), int(b.minFreeBytes / kMega)},
                       {QStringLiteral("petit_cote"), pCore && pCore->currentDoc() ? self->shortSide() : 0},
                       {QStringLiteral("encodeur"), encoderId(encoderFor(KdenliveSettings::supportedHWCodecs()))},
                       {QStringLiteral("encodage_ms"), self->m_encodeMs},
                       {QStringLiteral("dossier"), folder}};
}

void KoraCutLightCopies::removeOrphans()
{
#if defined(Q_OS_UNIX)
    // Comme pour le rendu d'aperçu (koracutrender.cpp) : un ffmpeg suspendu dont l'application a disparu
    // reste arrêté pour toujours. On ne retire que les nôtres : suspendus, rattachés au système, signés.
    auto *ps = new QProcess(pCore.get());
    QObject::connect(ps, &QProcess::finished, ps, [ps]() {
        const QStringList lines = QString::fromLocal8Bit(ps->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList f = line.simplified().split(QLatin1Char(' '));
            if (f.size() < 4 || f.at(1) != QLatin1String("1") || !f.at(2).startsWith(QLatin1Char('T')) || !line.contains(kMarker)) {
                continue;
            }
            const pid_t pid = pid_t(f.at(0).toLongLong());
            if (pid > 1) {
                ::kill(pid, SIGKILL);
                qInfo().noquote() << "KORACUT-COPIES encodage orphelin retiré" << pid;
            }
        }
        ps->deleteLater();
    });
    QObject::connect(ps, &QProcess::errorOccurred, ps, &QObject::deleteLater);
    ps->start(QStringLiteral("ps"), {QStringLiteral("-axo"), QStringLiteral("pid=,ppid=,stat=,command=")});
#endif
}
