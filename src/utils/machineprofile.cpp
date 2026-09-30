/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "machineprofile.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QThread>

#include <algorithm>

#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#endif

namespace {
qint64 physicalRamMb()
{
#if defined(Q_OS_MACOS)
    int64_t bytes = 0;
    size_t len = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0) {
        return qint64(bytes / (1024 * 1024));
    }
    return 0;
#elif defined(Q_OS_WIN)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return qint64(status.ullTotalPhys / (1024 * 1024));
    }
    return 0;
#elif defined(Q_OS_LINUX)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return qint64((quint64(info.totalram) * info.mem_unit) / (1024 * 1024));
    }
    return 0;
#else
    return 0;
#endif
}

QString resolveCachePath(const QString &cachePath)
{
    if (!cachePath.isEmpty()) {
        return cachePath;
    }
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}
} // namespace

MachineProfile::Hardware MachineProfile::detect(const QString &cachePath)
{
    Hardware hw;
    hw.logicalCores = std::max(1, QThread::idealThreadCount());
    hw.ramMb = physicalRamMb();
    hw.cpuArchitecture = QSysInfo::currentCpuArchitecture();
#if defined(Q_OS_MACOS)
    hw.isAppleSilicon = hw.cpuArchitecture.startsWith(QLatin1String("arm"));
#endif
    const QStorageInfo storage(resolveCachePath(cachePath));
    if (storage.isValid() && storage.isReady()) {
        hw.cacheVolumeFreeMb = storage.bytesAvailable() / (1024 * 1024);
    }
    return hw;
}

double MachineProfile::measureDiskWriteSpeed(const QString &cachePath)
{
    const QString dir = resolveCachePath(cachePath);
    if (!QDir().mkpath(dir)) {
        return 0.;
    }
    QTemporaryFile file(dir + QStringLiteral("/koracut-disktest-XXXXXX"));
    if (!file.open()) {
        return 0.;
    }
    // Blocs de 4 Mo, pendant 1 seconde ou jusqu'à 64 Mo : assez pour distinguer un disque dur
    // ou une eMMC (moins de 80 Mo/s) d'un SSD, sans gêner l'utilisateur.
    const QByteArray block(4 * 1024 * 1024, 'k');
    const qint64 maxBytes = 64LL * 1024 * 1024;
    qint64 written = 0;
    QElapsedTimer timer;
    timer.start();
    while (written < maxBytes && timer.elapsed() < 1000) {
        const qint64 n = file.write(block);
        if (n <= 0) {
            break;
        }
        written += n;
        file.flush();
    }
    const qint64 ms = std::max<qint64>(1, timer.elapsed());
    return (double(written) / (1024. * 1024.)) / (double(ms) / 1000.);
}

MachineProfile::Level MachineProfile::classify(const Hardware &hw)
{
    const bool slowDisk = hw.diskWriteMbPerSec > 0. && hw.diskWriteMbPerSec < 80.;
    const bool lowRam = hw.ramMb > 0 && hw.ramMb < 12 * 1024;
    if (hw.logicalCores <= 4 || lowRam || slowDisk) {
        return Level::Modest;
    }
    const bool bigRam = hw.ramMb > 24 * 1024;
    if (hw.isAppleSilicon || hw.hasDedicatedHwEncoder || bigRam) {
        return Level::Comfortable;
    }
    return Level::Medium;
}

MachineProfile::Recommendation MachineProfile::recommend(Level level, const Hardware &hw)
{
    Recommendation r;
    r.level = level;
    const int cores = std::max(1, hw.logicalCores);
    switch (level) {
    case Level::Modest:
        r.previewScaling = 4;
        r.adaptivePreviewFloorHeight = 360;
        r.enableProxy = true;
        r.generateProxy = true;
        r.proxyMinSize = 1280;
        r.proxyScale = 640;
        r.proxyForHeavyCodecs = true;
        r.proxyThreads = 1;
        r.previewRenderHeight = 540;
        r.autoPreview = true;
        r.processingThreads = 2;
        r.maxCacheSizeMb = 2000;
        r.cacheCleanupMonths = 2;
        r.videoThumbnails = false;
        r.simplifiedHeavyEffects = true;
        r.dropDivisor = 2;
        r.proxyHeavyMinWidth = 0;
        // Au plus un décodeur ouvert par cœur (au moins 4) : c'est de la mémoire sur une machine à 8 Go.
        r.producerCacheMax = std::max(4, cores);
        break;
    case Level::Medium:
        r.previewScaling = 2;
        r.adaptivePreviewFloorHeight = 540;
        r.enableProxy = true;
        r.generateProxy = true;
        r.proxyMinSize = 1920;
        r.proxyScale = 960;
        r.proxyForHeavyCodecs = true;
        r.proxyThreads = 2;
        r.previewRenderHeight = 720;
        r.autoPreview = true;
        r.processingThreads = 4;
        r.maxCacheSizeMb = 5000;
        r.cacheCleanupMonths = 4;
        r.videoThumbnails = true;
        r.simplifiedHeavyEffects = false;
        r.dropDivisor = 4;
        r.proxyHeavyMinWidth = 1920;
        break;
    case Level::Comfortable:
        r.previewScaling = 1;
        r.adaptivePreviewFloorHeight = 0;
        r.enableProxy = true;
        r.generateProxy = false;
        r.proxyMinSize = 3840;
        r.proxyScale = 1280;
        r.proxyForHeavyCodecs = false;
        r.proxyThreads = std::min(4, std::max(1, cores / 2));
        r.previewRenderHeight = 0;
        r.autoPreview = true;
        r.processingThreads = std::max(2, cores - 2);
        r.maxCacheSizeMb = 10000;
        r.cacheCleanupMonths = 6;
        r.videoThumbnails = true;
        r.simplifiedHeavyEffects = false;
        r.dropDivisor = 4;
        r.proxyHeavyMinWidth = 3840;
        break;
    }
    // Le cache suit le disque (10 % du libre, entre 2 et 20 Go), jamais plus que ce que le profil propose
    // sur une machine modeste (son disque est souvent petit et plein).
    if (hw.cacheVolumeFreeMb > 0) {
        const int budget = cacheBudgetMb(hw.cacheVolumeFreeMb);
        r.maxCacheSizeMb = level == Level::Modest ? std::min(r.maxCacheSizeMb, budget) : budget;
    }
    return r;
}

QString MachineProfile::levelName(Level level)
{
    switch (level) {
    case Level::Modest:
        return i18nc("@item machine performance profile", "Modest");
    case Level::Comfortable:
        return i18nc("@item machine performance profile", "Comfortable");
    case Level::Medium:
    default:
        return i18nc("@item machine performance profile", "Medium");
    }
}

QString MachineProfile::describe(const Hardware &hw)
{
    const QString ram = hw.ramMb > 0 ? i18n("%1 GB of memory", QString::number(double(hw.ramMb) / 1024., 'f', 0)) : i18n("memory unknown");
    const QString disk = hw.diskWriteMbPerSec > 0. ? i18n("disk at %1 MB/s", QString::number(hw.diskWriteMbPerSec, 'f', 0)) : i18n("disk not measured");
    return i18n("%1 cores, %2, %3, %4", hw.logicalCores, ram, hw.cpuArchitecture, disk);
}

// ---- Profil en service ---------------------------------------------------------------------

QString MachineProfile::levelId(Level level)
{
    switch (level) {
    case Level::Modest:
        return QStringLiteral("modeste");
    case Level::Comfortable:
        return QStringLiteral("confortable");
    case Level::Medium:
    default:
        return QStringLiteral("moyen");
    }
}

MachineProfile::Level MachineProfile::levelFromId(const QString &id, bool *ok)
{
    const QString v = id.trimmed().toLower();
    if (ok) {
        *ok = true;
    }
    if (v == QLatin1String("modeste") || v == QLatin1String("modest")) {
        return Level::Modest;
    }
    if (v == QLatin1String("moyen") || v == QLatin1String("moyenne") || v == QLatin1String("medium")) {
        return Level::Medium;
    }
    if (v == QLatin1String("confortable") || v == QLatin1String("comfortable")) {
        return Level::Comfortable;
    }
    if (ok) {
        *ok = false;
    }
    return Level::Medium;
}

MachineProfile::Level MachineProfile::detectedLevel()
{
    static const Level level = [] {
        Hardware hw = detect();
        // Encodeur dédié (NVENC, AMF) trouvé par l'assistant de premier lancement.
        const QStringList codecs = KSharedConfig::openConfig()->group(QStringLiteral("project")).readEntry("SupportedHWCodecs", QStringList());
        for (const QString &c : codecs) {
            if (c.endsWith(QLatin1String("_nvenc")) || c.endsWith(QLatin1String("_amf"))) {
                hw.hasDedicatedHwEncoder = true;
            }
        }
        return classify(hw);
    }();
    return level;
}

bool MachineProfile::isForced()
{
    bool ok = false;
    levelFromId(qEnvironmentVariable("KORACUT_PROFIL"), &ok);
    if (ok) {
        return true;
    }
    levelFromId(KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("profilMachine", QString()), &ok);
    return ok;
}

MachineProfile::Level MachineProfile::effectiveLevel()
{
    bool ok = false;
    Level level = levelFromId(qEnvironmentVariable("KORACUT_PROFIL"), &ok);
    if (ok) {
        return level;
    }
    level = levelFromId(KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("profilMachine", QString()), &ok);
    return ok ? level : detectedLevel();
}

// ---- Proxys --------------------------------------------------------------------------------

bool MachineProfile::isHeavyCodec(const QString &codecName, const QString &pixFormat)
{
    const QString c = codecName.toLower();
    if (c.contains(QLatin1String("hevc")) || c.contains(QLatin1String("h265")) || c.contains(QLatin1String("av1")) || c.contains(QLatin1String("dav1d")) ||
        c.contains(QLatin1String("vp9"))) {
        return true;
    }
    // yuv420p10le, p010le, yuv422p12le… : plus de 8 bits par composante.
    const QString f = pixFormat.toLower();
    return f.contains(QLatin1String("p10")) || f.contains(QLatin1String("p12")) || f.contains(QLatin1String("p16")) || f.startsWith(QLatin1String("p010"));
}

bool MachineProfile::wantsProxy(const ProxyRule &rule, int width, const QString &codecName, const QString &pixFormat)
{
    if (!rule.generate) {
        return false;
    }
    if (width < 0 || width > rule.minWidth) {
        return true;
    }
    return rule.heavyMinWidth >= 0 && width >= rule.heavyMinWidth && isHeavyCodec(codecName, pixFormat);
}

QString MachineProfile::proxyProfileFor(Level level, const QStringList &hwCodecs, const QStringList &available)
{
    const auto has = [&available](const QString &name) { return available.contains(name); };
    QString hardware;
    for (const QString &c : hwCodecs) {
        if (!c.startsWith(QLatin1String("h264_"))) {
            continue;
        }
        const QString name = QStringLiteral("x264-") + c.section(QLatin1Char('_'), 1);
        if (has(name)) {
            hardware = name;
            break;
        }
    }
    const QString light = QStringLiteral("KoraCut léger");
    switch (level) {
    case Level::Modest:
        // Mesuré le 29 septembre 2026 : le plus rapide à relire parmi les H.264, et le processeur
        // d'une machine modeste encode mieux en ultrafast qu'un encodeur matériel intégré inconnu.
        return has(light) ? light : QString();
    case Level::Medium:
        if (!hardware.isEmpty()) {
            return hardware;
        }
        return has(light) ? light : QString();
    case Level::Comfortable:
    default:
        if (!hardware.isEmpty()) {
            return hardware;
        }
        if (has(QStringLiteral("KoraCut ProRes Proxy"))) {
            return QStringLiteral("KoraCut ProRes Proxy");
        }
        return has(light) ? light : QString();
    }
}

QStringList MachineProfile::hwDecoderCandidates(const QStringList &hwCodecs)
{
#if defined(Q_OS_MACOS)
    Q_UNUSED(hwCodecs)
    return {QStringLiteral("videotoolbox")};
#elif defined(Q_OS_WIN)
    // D3D11VA passe par le pilote Direct3D : Intel, AMD et NVIDIA, sans pilote propriétaire exigé.
    Q_UNUSED(hwCodecs)
    return {QStringLiteral("d3d11va")};
#else
    QStringList list;
    if (hwCodecs.join(QLatin1Char(' ')).contains(QLatin1String("_nvenc"))) {
        list << QStringLiteral("nvdec");
    }
    list << QStringLiteral("vaapi");
    return list;
#endif
}

int MachineProfile::cacheBudgetMb(qint64 freeMb)
{
    if (freeMb <= 0) {
        return 2000;
    }
    const qint64 tenPercent = freeMb / 10;
    qint64 budget = std::clamp<qint64>(tenPercent, 2000, 20000);
    budget = std::min<qint64>(budget, std::max<qint64>(500, freeMb / 4));
    return int(budget);
}

int MachineProfile::scalingHeight(int scaling)
{
    switch (scaling) {
    case 1:
        return 1080;
    case 2:
        return 720;
    case 4:
        return 540;
    case 8:
        return 360;
    case 16:
        return 270;
    default:
        return 0;
    }
}

int MachineProfile::scalingForHeight(int height)
{
    for (int s : {1, 2, 4, 8, 16}) {
        if (scalingHeight(s) == height) {
            return s;
        }
    }
    return 0;
}

// ---- Aperçu adaptatif ----------------------------------------------------------------------

int MachineProfile::AdaptiveScaling::rank(int scaling)
{
    switch (scaling) {
    case 1:
        return 1;
    case 2:
        return 2;
    case 4:
        return 3;
    case 8:
        return 4;
    case 16:
        return 5;
    default:
        return 0;
    }
}

int MachineProfile::AdaptiveScaling::lower(int scaling)
{
    return scaling <= 0 ? 1 : std::min(16, scaling * 2);
}

int MachineProfile::AdaptiveScaling::higher(int scaling)
{
    return scaling <= 1 ? 0 : scaling / 2;
}

void MachineProfile::AdaptiveScaling::configure(int floorScaling)
{
    m_floor = floorScaling > 0 ? floorScaling : 0;
    if (m_floor == 0) {
        m_learned = -1;
    } else if (m_learned >= 0 && rank(m_learned) > rank(m_floor)) {
        m_learned = m_floor;
    }
}

int MachineProfile::AdaptiveScaling::start(int userScaling)
{
    m_user = userScaling;
    m_current = userScaling;
    m_playing = true;
    m_badStreak = 0;
    m_seconds = 0;
    m_droppedTotal = 0;
    // La première seconde remplit les tampons : on ne la juge pas.
    m_skip = 1;
    if (!enabled()) {
        return -1;
    }
    // Un cran retenu plus bas que la résolution choisie (et jamais plus bas que le plancher).
    if (m_learned >= 0 && rank(m_learned) > rank(m_user)) {
        m_current = rank(m_learned) > rank(m_floor) ? m_floor : m_learned;
        return m_current;
    }
    return -1;
}

int MachineProfile::AdaptiveScaling::tick(int dropped, double fps)
{
    if (!m_playing || !enabled()) {
        return -1;
    }
    m_seconds++;
    m_droppedTotal += std::max(0, dropped);
    if (m_skip > 0) {
        m_skip--;
        return -1;
    }
    // Quelques images sautées par seconde se voient à peine ; au-delà de 8 % (2 à 30 i/s), ça saccade.
    const int threshold = std::max(2, int(fps * 0.08 + 0.5));
    if (dropped >= threshold) {
        m_badStreak++;
    } else {
        m_badStreak = 0;
    }
    if (m_badStreak >= 2 && rank(m_current) < rank(m_floor)) {
        m_current = lower(m_current);
        m_learned = m_current;
        m_badStreak = 0;
        // Le changement de taille coûte lui-même quelques images : on laisse passer une seconde.
        m_skip = 1;
        return m_current;
    }
    return -1;
}

int MachineProfile::AdaptiveScaling::stop()
{
    if (!m_playing) {
        return -1;
    }
    m_playing = false;
    // Hystérésis vers le haut : une lecture d'au moins 5 s sans saut au cran retenu autorise la
    // suivante à essayer un cran plus net. Une seule lecture propre ne remonte que d'un cran.
    if (enabled() && m_learned >= 0 && m_seconds >= 5 && m_droppedTotal == 0) {
        const int up = higher(m_learned);
        m_learned = rank(up) <= rank(m_user) ? -1 : up;
    }
    return m_current != m_user ? m_user : -1;
}
