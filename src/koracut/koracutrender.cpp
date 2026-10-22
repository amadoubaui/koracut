/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutrender.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "bin/projectitemmodel.h"
#include "kdenlivesettings.h"
#include "koracut/koracuthwdecode.h"
#include "koracut/koracutlightcopies.h"
#include "koracut/color/koracutgradecontroller.h"
#include "monitor/scopes/sharedframe.h"
#include "utils/thumbnailcache.hpp"
#include "mainwindow.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"
#include "monitor/monitorproxy.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/previewmanager.h"
#include "timeline2/view/qmltypes/thumbnailprovider.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"
#include "utils/machineprofile.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KSharedConfig>
#include <QAction>
#include <QDate>
#include <QDateTime>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QQuickWindow>
#include <QFileInfo>
#include <QJsonArray>
#include <QMenu>
#include <QProcess>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <csignal>
#include <memory>
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif

namespace {

KConfigGroup koraCutGroup()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
}

const QString kOpenedMarker = QStringLiteral(".koracut-ouvert");

PreviewManager *currentPreviewManager(TimelineController **controller = nullptr)
{
    if (!pCore || !pCore->window() || !pCore->currentDoc()) {
        return nullptr;
    }
    TimelineWidget *widget = pCore->window()->getCurrentTimeline();
    if (!widget || !widget->controller() || !widget->controller()->getModel()) {
        return nullptr;
    }
    if (controller) {
        *controller = widget->controller();
    }
    auto model = widget->controller()->getModel();
    return model->hasTimelinePreview() ? model->previewManager().get() : nullptr;
}

QJsonObject timeJson(int frames)
{
    const double fps = pCore ? pCore->getCurrentFps() : 25.;
    const int base = std::max(1, int(qRound(fps)));
    const int f = frames % base;
    const int s = frames / base;
    return QJsonObject{{QStringLiteral("i"), frames},
                       {QStringLiteral("tc"), QStringLiteral("%1:%2:%3:%4")
                                                  .arg(s / 3600, 2, 10, QLatin1Char('0'))
                                                  .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
                                                  .arg(s % 60, 2, 10, QLatin1Char('0'))
                                                  .arg(f, 2, 10, QLatin1Char('0'))}};
}

// Des débuts de morceaux vers des plages { in, out } (out exclu).
QJsonArray rangesJson(QVariantList chunks, int chunkSize)
{
    QList<int> list;
    for (const QVariant &v : std::as_const(chunks)) {
        list << v.toInt();
    }
    std::sort(list.begin(), list.end());
    QJsonArray result;
    int start = -1;
    int last = -1;
    auto flush = [&]() {
        if (start >= 0) {
            result.append(QJsonObject{{QStringLiteral("in"), timeJson(start)}, {QStringLiteral("out"), timeJson(last + chunkSize)}});
        }
    };
    for (int c : std::as_const(list)) {
        if (start >= 0 && c == last + chunkSize) {
            last = c;
            continue;
        }
        flush();
        start = c;
        last = c;
    }
    flush();
    return result;
}

/** Le cran previewScaling d'un rang (0 → 1:1, 3 → 540p). */
int lowestScalingValue(int rank)
{
    static const int values[] = {0, 1, 2, 4, 8, 16};
    return values[std::clamp(rank, 0, 5)];
}

/** Temps processeur consommé par le processus (utilisateur + système), en secondes. */
double processCpuSeconds()
{
#if defined(Q_OS_UNIX)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return double(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) + double(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
    }
#elif defined(Q_OS_WIN)
    FILETIME creation, exit, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        const auto toSeconds = [](const FILETIME &t) {
            return double((quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 1e7; // unités de 100 ns
        };
        return toSeconds(kernel) + toSeconds(user);
    }
#endif
    return 0.;
}

/** Mémoire résidente la plus haute du processus depuis son lancement, en Mo. */
int peakMemoryMb()
{
#if defined(Q_OS_UNIX)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(Q_OS_MACOS)
        return int(usage.ru_maxrss / (1024 * 1024));
#else
        return int(usage.ru_maxrss / 1024);
#endif
    }
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return int(counters.PeakWorkingSetSize / (1024 * 1024));
    }
#endif
    return 0;
}

} // namespace

namespace KoraCutRender {

bool isModestMachine()
{
    // Le profil en service : celui que la personne a choisi dans les Préférences, sinon la détection.
    return MachineProfile::effectiveLevel() == MachineProfile::Level::Modest;
}

void healAfterHardwareRefusal(const QString &path, bool legacy)
{
    KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    if (!doc || path.isEmpty()) {
        return;
    }
    bool ok = false;
    const QDir base = doc->getCacheDir(CacheBase, &ok);
    if (!ok || !base.exists()) {
        return;
    }
    // Un fichier par projet, dans son cache : s'il part avec le cache, il n'y a plus rien à soigner non plus.
    const QString markerPath = base.absoluteFilePath(QStringLiteral("koracut-decodage-logiciel.txt"));
    QStringList healed;
    {
        QFile marker(markerPath);
        if (marker.open(QIODevice::ReadOnly)) {
            healed = QString::fromUtf8(marker.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }
    }
    if (healed.contains(path)) {
        return;
    }
    healed << path;
    {
        QFile marker(markerPath);
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            marker.write(healed.join(QLatin1Char('\n')).toUtf8());
        }
    }
    if (!legacy) {
        // Importé maintenant : rien n'a pu être fait avec la puce, l'essai est passé avant.
        return;
    }
    const QStringList ids = pCore->projectItemModel()->getClipByUrl(QFileInfo(path));
    for (const QString &id : ids) {
        ThumbnailCache::get()->invalidateThumbsForClip(id);
    }
    // Les essais arrivent un par un : les rendus partent une seule fois, quand la rafale est passée.
    static QTimer *timer = nullptr;
    static int count = 0;
    ++count;
    if (timer == nullptr) {
        timer = new QTimer(pCore.get());
        timer->setSingleShot(true);
        timer->setInterval(1500);
        QObject::connect(timer, &QTimer::timeout, pCore.get(), []() {
            KdenliveDoc *current = pCore->currentDoc();
            if (!current) {
                count = 0;
                return;
            }
            if (current->loading) {
                timer->start();
                return;
            }
            int managers = 0;
            const QList<QUuid> uuids = current->getTimelinesUuids();
            for (const QUuid &uuid : uuids) {
                const std::shared_ptr<TimelineItemModel> model = current->getTimeline(uuid, true);
                if (model && model->hasTimelinePreview()) {
                    model->previewManager()->koraCutDiscardAll();
                    ++managers;
                }
            }
            // Les séquences fermées : leurs morceaux partent du disque, ils seront refaits à l'ouverture.
            bool rootOk = false;
            const QDir root = current->getCacheDir(CachePreview, &rootOk);
            int files = 0;
            if (rootOk && root.exists() && root.dirName() == QLatin1String("preview")) {
                static const QRegularExpression chunk(QStringLiteral("^\\d+\\.\\w+$"));
                QDirIterator it(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString file = it.next();
                    if (chunk.match(QFileInfo(file).fileName()).hasMatch() && QFile::remove(file)) {
                        ++files;
                    }
                }
            }
            qInfo().noquote() << "KORACUT-APERCU rendus effacés :" << count << "clip(s) que la puce ne décode pas," << managers << "séquence(s)," << files
                              << "fichier(s) en plus";
            count = 0;
        });
    }
    timer->start();
}

namespace {
// 2 octobre 2026 (document 6, journal) : un rendu d'aperçu suspendu (SIGSTOP pendant la lecture) dont
// l'application a disparu sans le reprendre (plantage, arrêt forcé) reste arrêté pour toujours, rattaché au
// système. Deux tournaient encore sur ce Mac, l'un depuis plus d'un jour. Au lancement, on retire ceux-là
// seulement : un koracut_render en mode preview-chunks, suspendu, dont le parent est le système (PID 1).
void removeOrphanRenders()
{
#if defined(Q_OS_UNIX)
    auto *ps = new QProcess(pCore.get());
    QObject::connect(ps, &QProcess::finished, ps, [ps]() {
        const QStringList lines = QString::fromLocal8Bit(ps->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList f = line.simplified().split(QLatin1Char(' '));
            if (f.size() < 5 || f.at(1) != QLatin1String("1") || !f.at(2).startsWith(QLatin1Char('T'))) {
                continue;
            }
            if (!f.at(3).endsWith(QLatin1String("koracut_render")) || f.at(4) != QLatin1String("preview-chunks")) {
                continue;
            }
            const pid_t pid = pid_t(f.at(0).toLongLong());
            if (pid > 1) {
                ::kill(pid, SIGKILL);
                qInfo().noquote() << "KORACUT-PERF rendu d'aperçu orphelin retiré" << pid;
            }
        }
        ps->deleteLater();
    });
    QObject::connect(ps, &QProcess::errorOccurred, ps, &QObject::deleteLater);
    ps->start(QStringLiteral("ps"), {QStringLiteral("-axo"), QStringLiteral("pid=,ppid=,stat=,command=")});
#endif
}
} // namespace

void applyFirstLaunchDefaults()
{
    // Document 6, journal du 1er octobre 2026 : un clip refusé par la puce fait refaire ses caches.
    static bool watching = false;
    if (!watching) {
        watching = true;
        removeOrphanRenders();
        KoraCutLightCopies::removeOrphans();
        QObject::connect(KoraCutHwDecode::instance(), &KoraCutHwDecode::clipRefused, pCore.get(),
                         [](const QString &path, bool legacy) { healAfterHardwareRefusal(path, legacy); });
    }
    KConfigGroup group = koraCutGroup();
    if (group.readEntry("apercuAutoInitialise", false)) {
        return;
    }
    const bool modest = isModestMachine();
    KdenliveSettings::setAutopreview(!modest);
    // Écrit tout de suite : sans cela, un arrêt brutal perdrait le défaut, et le témoin dirait qu'il est posé.
    KdenliveSettings::self()->save();
    group.writeEntry("apercuAutoInitialise", true);
    group.sync();
    if (!modest) {
        return;
    }
    // Machine modeste : le rendu automatique reste coupé (il prend le processeur pendant qu'on
    // monte), mais on le propose une fois, là où la personne regarde.
    QTimer::singleShot(4000, pCore.get(), []() {
        if (!pCore || !pCore->window()) {
            return;
        }
        QAction *autoAction = pCore->window()->actionCollection()->action(QStringLiteral("koracut_render_auto"));
        if (!autoAction) {
            return;
        }
        auto *enable = new QAction(i18nc("@action:button", "Turn On Automatic Render"), pCore->window());
        QObject::connect(enable, &QAction::triggered, autoAction, [autoAction]() {
            if (!autoAction->isChecked()) {
                autoAction->trigger();
            }
        });
        pCore->displayBinMessage(i18nc("@info", "Your computer is modest: automatic render is off so editing stays smooth. To play a busy passage "
                                                "without stutter, click Render in the timeline toolbar."),
                                 KMessageWidget::Information, {enable}, true);
    });
}

qint64 folderSize(const QString &dir)
{
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

qint64 cacheCapBytes(const QString &previewRoot, qint64 currentSize)
{
    const qint64 mb = 1024LL * 1024LL;
    const int configured = koraCutGroup().readEntry("apercuPlafondMo", 0);
    if (configured > 0) {
        return configured * mb;
    }
    // Automatique : 5 Go, ou 10 % de l'espace libre (en comptant ce que le cache occupe déjà) si c'est moins.
    qint64 cap = 5000LL * mb;
    const QStorageInfo storage(previewRoot);
    if (storage.isValid() && storage.isReady()) {
        cap = std::min(cap, (storage.bytesAvailable() + currentSize) / 10);
    }
    return std::max<qint64>(cap, 200LL * mb);
}

int undoStepsKept()
{
    return std::clamp(koraCutGroup().readEntry("apercuEtapesAnnulation", 0), 0, 5);
}

QString openedMarkerName()
{
    return kOpenedMarker;
}

void touchOpenedMarker(const QString &previewRoot)
{
    QFile marker(QDir(previewRoot).absoluteFilePath(kOpenedMarker));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        marker.close();
    }
}

void cleanupOldProjects(const QString &currentDocumentId)
{
    KConfigGroup group = koraCutGroup();
    const QDate today = QDate::currentDate();
    if (group.readEntry("apercuDernierNettoyage", QDate()) == today) {
        return;
    }
    group.writeEntry("apercuDernierNettoyage", today);
    const int days = std::max(1, group.readEntry("apercuNettoyageJours", 30));
    const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (root.isEmpty()) {
        return;
    }
    // Chantier 10 : les vignettes et formes d'onde d'un projet non ouvert depuis cleanCacheMonths (2 mois sur
    // machine modeste, 6 ailleurs) partent aussi ; elles se refont à la réouverture. Proxys et masques restent.
    const int thumbDays = std::max(30, KdenliveSettings::cleanCacheMonths() * 30);
    // Sur le disque, en tâche de fond : l'interface ne l'attend pas.
    (void)QtConcurrent::run([root, days, thumbDays, currentDocumentId]() {
        const QDateTime limit = QDateTime::currentDateTime().addDays(-days);
        const QDateTime thumbLimit = QDateTime::currentDateTime().addDays(-thumbDays);
        const QStringList entries = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : entries) {
            bool numeric = false;
            name.toLongLong(&numeric);
            // Seuls les dossiers de projet (un identifiant numérique) et leur sous-dossier preview.
            if (!numeric || name == currentDocumentId) {
                continue;
            }
            QDir preview(QDir(root).absoluteFilePath(name + QStringLiteral("/preview")));
            const QDir project(QDir(root).absoluteFilePath(name));
            if (!preview.exists() || preview.dirName() != QLatin1String("preview")) {
                // Pas de dossier de rendu : l'âge du dossier du projet décide pour les vignettes.
                if (QFileInfo(project.absolutePath()).lastModified() < thumbLimit) {
                    for (const QString &sub : {QStringLiteral("videothumbs"), QStringLiteral("audiothumbs"), QStringLiteral("workfiles")}) {
                        QDir(project.absoluteFilePath(sub)).removeRecursively();
                    }
                }
                continue;
            }
            QDateTime seen = QFileInfo(preview.absolutePath()).lastModified();
            const QFileInfo marker(preview.absoluteFilePath(kOpenedMarker));
            if (marker.exists()) {
                seen = marker.lastModified();
            } else {
                const QFileInfoList files = preview.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
                if (!files.isEmpty() && files.constFirst().lastModified() > seen) {
                    seen = files.constFirst().lastModified();
                }
            }
            if (seen < thumbLimit) {
                for (const QString &sub : {QStringLiteral("videothumbs"), QStringLiteral("audiothumbs"), QStringLiteral("workfiles")}) {
                    QDir(project.absoluteFilePath(sub)).removeRecursively();
                }
            }
            if (seen < limit) {
                preview.removeRecursively();
            }
        }
    });
}

void measurePlayback(int from, int seconds, const std::function<void(const QJsonObject &)> &done)
{
    if (!pCore || !pCore->monitorManager() || !pCore->monitorManager()->projectMonitor()) {
        done(QJsonObject{{QStringLiteral("ok"), false}});
        return;
    }
    Monitor *monitor = pCore->monitorManager()->projectMonitor();
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
    monitor->requestSeek(from);
    struct State
    {
        QSet<int> positions;
        int first{-1};
        int last{-1};
        int lowestScaling{0};
        double cpuStart{0.};
        QElapsedTimer clock;
        QMetaObject::Connection connection;
    };
    auto state = std::make_shared<State>();
    QTimer::singleShot(1000, monitor, [monitor, state, seconds, done]() {
        MonitorProxy *proxy = monitor->getControllerProxy();
        state->connection = QObject::connect(proxy, &MonitorProxy::positionChanged, monitor, [state](int pos) {
            if (!state->clock.isValid()) {
                return;
            }
            state->positions.insert(pos);
            if (state->first < 0) {
                state->first = pos;
            }
            state->last = pos;
        });
        state->clock.start();
        state->cpuStart = processCpuSeconds();
        monitor->switchPlay(true);
        // Le cran d'aperçu le plus bas atteint pendant la lecture (aperçu adaptatif).
        auto *watch = new QTimer(monitor);
        QObject::connect(watch, &QTimer::timeout, monitor, [state]() {
            state->lowestScaling = std::max(state->lowestScaling, MachineProfile::AdaptiveScaling::rank(KdenliveSettings::previewScaling()));
        });
        watch->start(250);
        QTimer::singleShot(seconds * 1000, monitor, [monitor, state, done, watch]() {
            watch->deleteLater();
            const qint64 ms = state->clock.elapsed();
            const double cpu = processCpuSeconds() - state->cpuStart;
            monitor->switchPlay(false);
            QObject::disconnect(state->connection);
            const double fps = pCore->getCurrentFps();
            const int covered = state->first < 0 ? 0 : state->last - state->first + 1;
            const int shown = int(state->positions.size());
            const int expected = int(qRound(fps * double(ms) / 1000.));
            // Où manquent les images : « position:nombre » pour chaque trou (les coupes se voient ici).
            QStringList holes;
            if (state->first >= 0) {
                int run = 0;
                for (int p = state->first; p <= state->last + 1; ++p) {
                    if (p <= state->last && !state->positions.contains(p)) {
                        ++run;
                    } else if (run > 0) {
                        holes << QStringLiteral("%1:%2").arg(p - run).arg(run);
                        run = 0;
                    }
                }
            }
            QJsonObject result{{QStringLiteral("trous"), holes.join(QLatin1Char(','))},{QStringLiteral("ok"), true},
                               {QStringLiteral("duree_ms"), ms},
                               {QStringLiteral("images_attendues"), expected},
                               {QStringLiteral("images_affichees"), shown},
                               {QStringLiteral("images_sautees"), std::max(0, covered - shown)},
                               {QStringLiteral("retard"), std::max(0, expected - covered)},
                               {QStringLiteral("ips"), ms > 0 ? qRound(10. * shown * 1000. / double(ms)) / 10. : 0.},
                               {QStringLiteral("profil"), MachineProfile::levelId(MachineProfile::effectiveLevel())},
                               {QStringLiteral("apercu_min"), MachineProfile::scalingHeight(lowestScalingValue(state->lowestScaling))},
                               {QStringLiteral("decodage"), KoraCutHwDecode::effective().isEmpty() ? QStringLiteral("logiciel") : KoraCutHwDecode::effective()},
                               {QStringLiteral("memoire_max_mo"), peakMemoryMb()},
                               // Temps processeur du processus pendant la lecture, rapporté à la durée : le nombre de
                               // cœurs occupés. Une machine à 4 cœurs ne suit plus au-delà de 3 environ.
                               {QStringLiteral("coeurs_occupes"), ms > 0 ? qRound(100. * cpu * 1000. / double(ms)) / 100. : 0.}};
            done(result);
        });
    });
}

namespace {
// Attend une image affichée qui vérifie @p accept, au plus @p timeoutMs ; rend le temps écoulé en ms (-1 : rien).
double waitForFrame(const std::function<void()> &gesture, const std::function<bool(int)> &accept, int timeoutMs = 4000)
{
    MonitorManager *manager = pCore->monitorManager();
    QEventLoop loop;
    QElapsedTimer clock;
    double elapsed = -1.;
    auto connection = QObject::connect(manager, &MonitorManager::frameDisplayed, &loop, [&](const SharedFrame &frame) {
        if (elapsed < 0. && accept(frame.get_position())) {
            elapsed = double(clock.nsecsElapsed()) / 1e6;
            loop.quit();
        }
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    clock.start();
    gesture();
    if (elapsed < 0.) {
        loop.exec();
    }
    QObject::disconnect(connection);
    return elapsed;
}

QJsonObject summary(QList<double> values)
{
    QJsonArray all;
    int failed = 0;
    QList<double> ok;
    for (double v : values) {
        all.append(qRound(v * 10.) / 10.);
        if (v < 0.) {
            ++failed;
        } else {
            ok << v;
        }
    }
    std::sort(ok.begin(), ok.end());
    QJsonObject o{{QStringLiteral("ms"), all}};
    if (!ok.isEmpty()) {
        o.insert(QStringLiteral("mediane_ms"), qRound(ok.at(ok.size() / 2) * 10.) / 10.);
        o.insert(QStringLiteral("max_ms"), qRound(ok.last() * 10.) / 10.);
    }
    if (failed > 0) {
        o.insert(QStringLiteral("sans_image"), failed);
    }
    return o;
}

// Glisser la tête de lecture : voir measureGestures, point 6.
QJsonObject measureDrag(Monitor *monitor, int duration)
{
    // Glisser la tête de lecture (4 octobre 2026) : trois secondes d'événements à 60 par seconde, aller puis
    // retour, comme une main sur la règle. « appel » : ce que chaque événement prend au fil d'interface ;
    // « intervalle » : l'écart réel entre deux événements (16 ms si rien ne bloque : c'est la fluidité du curseur) ;
    // « images » : ce que le moniteur a montré pendant le geste, et son retard sur la main.
    QJsonObject drag;
    if (MonitorProxy *proxy = monitor->getControllerProxy()) {
        const int span = std::clamp(duration / 3, 30, 300);
        const int from = std::clamp(int(0.3 * duration), 0, std::max(0, duration - 2));
        const int steps = 180;
        QList<double> calls, gaps, lags;
        QSet<int> distinct;
        int shownFrames = 0;
        // Les images montrées pendant l'aller (la main avance) ; le reste l'est pendant le retour (elle recule).
        int forwardFrames = 0;
        int step = 0;
        int requested = from;
        QElapsedTimer tick, whole, released;
        double sharpMs = -1.;
        int sharpWidth = 0, lightWidth = 0;
        auto connection = QObject::connect(pCore->monitorManager(), &MonitorManager::frameDisplayed, pCore->monitorManager(), [&](const SharedFrame &frame) {
            if (released.isValid()) {
                // Après le relâcher : la dernière image reçue est la nette (pleine taille, étalonnée).
                sharpMs = double(released.nsecsElapsed()) / 1e6;
                sharpWidth = frame.get_image_width();
                return;
            }
            ++shownFrames;
            if (step * 2 <= steps) {
                ++forwardFrames;
            }
            distinct.insert(frame.get_position());
            lags << std::abs(frame.get_position() - requested);
            lightWidth = lightWidth == 0 ? frame.get_image_width() : std::min(lightWidth, frame.get_image_width());
        });
        QEventLoop loop;
        QTimer timer;
        timer.setTimerType(Qt::PreciseTimer);
        timer.setInterval(16);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
            if (tick.isValid()) {
                gaps << double(tick.nsecsElapsed()) / 1e6;
            }
            tick.start();
            const double phase = double(step) / double(steps);
            requested = from + int((phase < 0.5 ? phase * 2. : (1. - phase) * 2.) * span);
            QElapsedTimer call;
            call.start();
            proxy->setPosition(requested);
            calls << double(call.nsecsElapsed()) / 1e6;
            if (++step >= steps) {
                timer.stop();
                loop.quit();
            }
        });
        whole.start();
        timer.start();
        loop.exec();
        const double dragMs = double(whole.nsecsElapsed()) / 1e6;
        const int last = requested;
        // Au relâcher : le temps jusqu'à l'image exacte.
        released.start();
        const double settle = waitForFrame([]() {}, [last](int p) { return p == last; }, 3000);
        // Puis l'image nette : l'aperçu allégé du glisser est remplacé peu après la dernière demande.
        {
            QEventLoop calm;
            QTimer::singleShot(900, &calm, &QEventLoop::quit);
            calm.exec();
        }
        QObject::disconnect(connection);
        std::sort(gaps.begin(), gaps.end());
        int late = 0;
        for (double g : std::as_const(gaps)) {
            if (g > 34.) {
                ++late;
            }
        }
        double lagSum = 0.;
        for (double l : std::as_const(lags)) {
            lagSum += l;
        }
        drag = QJsonObject{{QStringLiteral("evenements"), steps},
                         {QStringLiteral("duree_ms"), qRound(dragMs)},
                         {QStringLiteral("appel"), summary(calls).value(QStringLiteral("mediane_ms"))},
                         {QStringLiteral("appel_max_ms"), summary(calls).value(QStringLiteral("max_ms"))},
                         {QStringLiteral("intervalle_median_ms"), gaps.isEmpty() ? 0. : qRound(gaps.at(gaps.size() / 2) * 10.) / 10.},
                         {QStringLiteral("intervalle_p95_ms"), gaps.isEmpty() ? 0. : qRound(gaps.at(int(gaps.size() * 0.95)) * 10.) / 10.},
                         {QStringLiteral("intervalle_max_ms"), gaps.isEmpty() ? 0. : qRound(gaps.last() * 10.) / 10.},
                         {QStringLiteral("evenements_en_retard"), late},
                         {QStringLiteral("images"), shownFrames},
                         {QStringLiteral("images_distinctes"), int(distinct.size())},
                         {QStringLiteral("images_par_seconde_aller"), dragMs > 0. ? qRound(10. * forwardFrames * 2000. / dragMs) / 10. : 0.},
                         {QStringLiteral("images_par_seconde_retour"), dragMs > 0. ? qRound(10. * (shownFrames - forwardFrames) * 2000. / dragMs) / 10. : 0.},
                         {QStringLiteral("images_par_seconde"), dragMs > 0. ? qRound(10. * shownFrames * 1000. / dragMs) / 10. : 0.},
                         {QStringLiteral("retard_moyen_images"), lags.isEmpty() ? -1. : qRound(10. * lagSum / lags.size()) / 10.},
                         {QStringLiteral("image_exacte_au_relacher_ms"), qRound(settle * 10.) / 10.},
                         {QStringLiteral("image_nette_au_relacher_ms"), qRound(sharpMs * 10.) / 10.},
                         {QStringLiteral("largeur_pendant"), lightWidth},
                         {QStringLiteral("largeur_apres"), sharpWidth}};
    }
    return drag;
}
// La fluidité de la timeline elle-même (22 octobre 2026, KC-011) : zoom, défilement, glisser d'un plan, à 60
// événements par seconde, comme une main. « images_par_seconde » : ce que la timeline a vraiment dessiné pendant le
// geste (QQuickWindow::afterRendering : la timeline est un QQuickWidget, dessiné hors écran) ; « image_max_ms » : le plus long écart entre deux images, la saccade qu'on voit ;
// « appel » : ce que chaque événement a pris au fil de l'interface.
QJsonObject sweepTimeline(TimelineWidget *widget, int steps, const std::function<void(double phase)> &step)
{
    // La timeline peut disparaître pendant la boucle (fermeture, changement de séquence) : on s'arrête alors.
    const QPointer<TimelineWidget> alive(widget);
    QQuickWindow *window = widget->quickWindow();
    QList<qint64> frames;
    QElapsedTimer clock;
    clock.start();
    const auto connection = window ? QObject::connect(window, &QQuickWindow::afterRendering, widget, [&]() { frames << clock.nsecsElapsed(); },
                                                      Qt::DirectConnection)
                                   : QMetaObject::Connection();
    QList<double> calls;
    int n = 0;
    QEventLoop loop;
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(16);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        if (!alive) {
            timer.stop();
            loop.quit();
            return;
        }
        QElapsedTimer call;
        call.start();
        step(double(n) / double(steps));
        calls << double(call.nsecsElapsed()) / 1e6;
        if (++n >= steps) {
            timer.stop();
            loop.quit();
        }
    });
    const qint64 start = clock.nsecsElapsed();
    timer.start();
    loop.exec();
    const double ms = double(clock.nsecsElapsed() - start) / 1e6;
    // Les dernières images du geste arrivent juste après : un tour de boucle pour les compter.
    {
        QEventLoop calm;
        QTimer::singleShot(100, &calm, &QEventLoop::quit);
        calm.exec();
    }
    QObject::disconnect(connection);
    double maxGap = 0.;
    int jank = 0;
    for (int i = 1; i < frames.size(); ++i) {
        const double gap = double(frames.at(i) - frames.at(i - 1)) / 1e6;
        maxGap = std::max(maxGap, gap);
        if (gap > 34.) {
            ++jank;
        }
    }
    const QJsonObject callStats = summary(calls);
    return QJsonObject{{QStringLiteral("evenements"), steps},
                       {QStringLiteral("duree_ms"), qRound(ms)},
                       {QStringLiteral("images"), int(frames.size())},
                       {QStringLiteral("images_par_seconde"), ms > 0. ? qRound(10. * frames.size() * 1000. / ms) / 10. : 0.},
                       {QStringLiteral("image_max_ms"), qRound(maxGap * 10.) / 10.},
                       {QStringLiteral("images_en_retard"), jank},
                       {QStringLiteral("appel"), callStats.value(QStringLiteral("mediane_ms"))},
                       {QStringLiteral("appel_max_ms"), callStats.value(QStringLiteral("max_ms"))}};
}
} // namespace

QJsonObject measureTimelineFluidity()
{
    TimelineWidget *widget = pCore && pCore->window() ? pCore->window()->getCurrentTimeline() : nullptr;
    if (!widget || !widget->model() || !widget->controller()) {
        return QJsonObject{{QStringLiteral("ok"), false}};
    }
    const QPointer<TimelineWidget> guard(widget);
    TimelineController *controller = widget->controller();
    const auto model = widget->model();
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("plans"), int(model->getClipsCount())}, {QStringLiteral("largeur"), widget->width()}};
    // Le zoom : deux allers-retours d'un facteur 4 autour du zoom de départ, comme un pincement continu.
    const double scale = controller->scaleFactor();
    result.insert(QStringLiteral("zoom"), sweepTimeline(widget, 120, [controller, scale](double phase) {
        controller->setScaleFactorOnMouse(scale * std::pow(2., 2. * std::sin(phase * 4. * M_PI)), false);
    }));
    if (!guard) {
        return result;
    }
    // Le pincement par le zoom continu (KC-011) : deux événements par image, comme un trackpad à 120 Hz ; les deux
    // sont regroupés en un seul changement d'échelle.
    if (guard) {
        result.insert(QStringLiteral("pincement"), sweepTimeline(widget, 120, [controller](double phase) {
            const double factor = std::pow(2., 0.5 * 4. * M_PI / 120. * std::cos(phase * 4. * M_PI));
            controller->koraCutZoomBy(std::sqrt(factor), false);
            controller->koraCutZoomBy(std::sqrt(factor), false);
        }));
        QEventLoop calm;
        QTimer::singleShot(50, &calm, &QEventLoop::quit);
        calm.exec();
        result.insert(QStringLiteral("pincement_echelle_finale"), qRound(1000. * controller->scaleFactor() / scale) / 1000.);
        if (MainWindow *window = pCore->window()) {
            result.insert(QStringLiteral("niveau_curseur"), window->koraCutZoomLevel());
        }
        controller->setScaleFactorOnMouse(scale, false);
    }
    // Ce que le zoom coûte après le geste : les vignettes redemandées et décodées pendant qu'il se pose.
    {
        const int requests = ThumbnailProvider::koraCutRequests();
        const int decodes = ThumbnailProvider::koraCutDecodes();
        controller->setScaleFactorOnMouse(scale * 1.7, false);
        QEventLoop settle;
        QTimer::singleShot(1500, &settle, &QEventLoop::quit);
        settle.exec();
        controller->setScaleFactorOnMouse(scale, false);
        QEventLoop back;
        QTimer::singleShot(1500, &back, &QEventLoop::quit);
        back.exec();
        QJsonObject zoom = result.value(QStringLiteral("zoom")).toObject();
        zoom.insert(QStringLiteral("vignettes_demandees_apres"), ThumbnailProvider::koraCutRequests() - requests);
        zoom.insert(QStringLiteral("vignettes_decodees_apres"), ThumbnailProvider::koraCutDecodes() - decodes);
        result.insert(QStringLiteral("zoom"), zoom);
    }
    if (!guard) {
        return result;
    }
    // Le défilement et le glisser se font zoomés (six fois le zoom de départ, au moins 2 pixels par image) : au zoom
    // « tout voir », il n'y a rien à faire défiler et un plan bouge d'un pixel.
    const double close = std::max(scale * 6., 2.);
    controller->setScaleFactorOnMouse(close, false);
    // Le défilement : de gauche à droite puis retour, sur toute la longueur de la séquence.
    const int span = std::max(0, int(model->duration() * close) - widget->width());
    result.insert(QStringLiteral("defilement"), sweepTimeline(widget, 120, [controller, span](double phase) {
        controller->setScrollPos(int(span * (phase < 0.5 ? phase * 2. : (1. - phase) * 2.)));
    }));
    if (!guard) {
        return result;
    }
    controller->setScrollPos(0);
    // Glisser un plan : un plan vidéo qui a 3 s de libre derrière lui (sinon le dernier d'une piste), poussé vers la
    // droite puis ramené, par le chemin de la souris (suggestClipMove, sans étape d'annulation), puis remis à sa place.
    const int reach = int(3 * pCore->getCurrentFps());
    int clip = -1;
    for (int tid : model->getTracksIds(false)) {
        if (model->trackIsLocked(tid)) {
            continue;
        }
        for (int c : model->getItemsInRange(tid, 0, -1, false)) {
            if (!model->isClip(c)) {
                continue;
            }
            const int end = model->getClipPosition(c) + model->getClipPlaytime(c);
            bool free = true;
            for (int other : model->getItemsInRange(tid, end, end + reach, false)) {
                free = free && (other == c || !model->isClip(other));
            }
            if (free) {
                clip = c;
                break;
            }
        }
        if (clip >= 0) {
            break;
        }
    }
    if (clip >= 0) {
        // Le plan au milieu de la vue, pour que son glisser se voie.
        controller->setScrollPos(std::max(0, int(model->getClipPosition(clip) * close) - widget->width() / 3));
        QEventLoop settle;
        QTimer::singleShot(300, &settle, &QEventLoop::quit);
        settle.exec();
    }
    if (clip >= 0) {
        const int track = model->getClipTrackId(clip);
        const int origin = model->getClipPosition(clip);
        result.insert(QStringLiteral("glisser_plan"), sweepTimeline(widget, 120, [model, clip, track, origin, reach](double phase) {
            const int pos = origin + int(reach * (phase < 0.5 ? phase * 2. : (1. - phase) * 2.));
            model->suggestClipMove(clip, track, pos, -1, -1);
        }));
        if (model->getClipPosition(clip) != origin || model->getClipTrackId(clip) != track) {
            model->requestClipMove(clip, track, origin, true, true, false);
        }
        result.insert(QStringLiteral("plan_remis"), model->getClipPosition(clip) == origin && model->getClipTrackId(clip) == track);
        // Changer de piste pendant le glisser : une piste vidéo voisine libre à cet endroit, un aller-retour toutes les
        // quatre images (le plan passe d'une piste à l'autre, comme la main qui monte et descend).
        int other = -1;
        const int length = model->getClipPlaytime(clip);
        for (int tid : model->getTracksIds(false)) {
            if (tid == track || model->trackIsLocked(tid)) {
                continue;
            }
            bool free = true;
            for (int item : model->getItemsInRange(tid, origin, origin + length, false)) {
                free = free && !model->isClip(item);
            }
            if (free) {
                other = tid;
                break;
            }
        }
        if (other >= 0) {
            result.insert(QStringLiteral("glisser_piste"), sweepTimeline(widget, 120, [model, clip, track, other, origin](double phase) {
                const int step = int(phase * 120.);
                model->suggestClipMove(clip, (step / 4) % 2 ? other : track, origin, -1, -1);
            }));
            if (model->getClipPosition(clip) != origin || model->getClipTrackId(clip) != track) {
                model->requestClipMove(clip, track, origin, true, true, false);
            }
            result.insert(QStringLiteral("plan_remis_piste"), model->getClipPosition(clip) == origin && model->getClipTrackId(clip) == track);
        }
    }
    if (guard) {
        controller->setScaleFactorOnMouse(scale, false);
        controller->setScrollPos(0);
    }
    return result;
}

void measureGestures(const std::function<void(const QJsonObject &)> &done, int dragOnly)
{
    Monitor *monitor = pCore && pCore->monitorManager() ? pCore->monitorManager()->projectMonitor() : nullptr;
    TimelineWidget *widget = pCore && pCore->window() ? pCore->window()->getCurrentTimeline() : nullptr;
    if (!monitor || !widget || !widget->model()) {
        done(QJsonObject{{QStringLiteral("ok"), false}});
        return;
    }
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
    auto timeline = widget->model();
    const int duration = timeline->duration();
    auto at = [duration](double f) { return std::clamp(int(f * duration), 0, std::max(0, duration - 2)); };
    QJsonObject result{{QStringLiteral("ok"), true}};
    if (dragOnly > 0) {
        // « apercu:gestes:glisser:N » : le glisser seul, N fois, une ligne par passe (banc du 4 octobre 2026).
        for (int i = 0; i < dragOnly; ++i) {
            QEventLoop calm;
            QTimer::singleShot(600, &calm, &QEventLoop::quit);
            calm.exec();
            done(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("glisser"), measureDrag(monitor, duration)}});
        }
        return;
    }

    // 1. Sauts de la tête de lecture, à l'arrêt : du clic à l'image affichée.
    QList<double> seeks;
    for (double f : {0.21, 0.66, 0.05, 0.9, 0.43, 0.12, 0.77, 0.3, 0.55, 0.97}) {
        const int pos = at(f);
        seeks << waitForFrame([monitor, pos]() { monitor->requestSeek(pos); }, [pos](int p) { return p == pos; });
    }
    result.insert(QStringLiteral("saut"), summary(seeks));

    // 2. Départ de la lecture : de « Lire » à la première image qui avance.
    QList<double> starts;
    for (double f : {0.0, 0.3, 0.6}) {
        const int pos = at(f);
        waitForFrame([monitor, pos]() { monitor->requestSeek(pos); }, [pos](int p) { return p == pos; });
        starts << waitForFrame([monitor]() { monitor->switchPlay(true); }, [pos](int p) { return p > pos; });
        monitor->switchPlay(false);
        QEventLoop pause;
        QTimer::singleShot(400, &pause, &QEventLoop::quit);
        pause.exec();
    }
    result.insert(QStringLiteral("lecture_depart"), summary(starts));

    // Le plan de la piste vidéo la plus basse sous la tête, et un plan d'une piste plus haute : on cherche
    // un endroit où deux pistes vidéo se superposent (sinon, le premier plan venu).
    int here = at(0.25);
    int base = -1, upper = -1;
    for (int step = 0; step < 40 && upper < 0; ++step) {
        const int pos = at(0.02 + 0.024 * step);
        int b = -1, u = -1;
        for (int tid : timeline->getTracksIds(false)) {
            const int cid = timeline->getClipByPosition(tid, pos);
            if (cid >= 0 && b < 0) {
                b = cid;
            } else if (cid >= 0 && u < 0) {
                u = cid;
            }
        }
        if (b >= 0 && (base < 0 || u >= 0)) {
            base = b;
            upper = u;
            here = pos;
        }
    }
    waitForFrame([monitor, here]() { monitor->requestSeek(here); }, [here](int p) { return p == here; });
    const int shown = here;
    auto refresh = [shown](int p) { return p == shown; };
    // Entre deux gestes, une personne laisse passer un instant : 400 ms de calme avant chacun.
    auto idle = []() {
        QEventLoop calm;
        QTimer::singleShot(400, &calm, &QEventLoop::quit);
        calm.exec();
    };

    // 3. Rogner le début du plan d'une image, puis le remettre.
    if (base >= 0) {
        QList<double> trims, calls;
        for (int i = 0; i < 4; ++i) {
            const int size = timeline->getItemPlaytime(base);
            idle();
            QElapsedTimer call;
            trims << waitForFrame(
                [&]() {
                    call.start();
                    // Par le début : l'image sous la tête change, le moniteur doit la refaire.
                    timeline->requestItemResize(base, i % 2 == 0 ? size - 1 : size + 1, false, true);
                    calls << double(call.nsecsElapsed()) / 1e6;
                },
                refresh);
        }
        QJsonObject t = summary(trims);
        t.insert(QStringLiteral("appel"), summary(calls));
        result.insert(QStringLiteral("rogner"), t);
    }

    // 4. Déplacer un plan d'une piste haute d'une image, puis le remettre.
    if (upper >= 0) {
        QList<double> moves, calls;
        int refused = 0;
        for (int i = 0; i < 4; ++i) {
            const int tid = timeline->getClipTrackId(upper);
            const int pos = timeline->getItemPosition(upper);
            idle();
            QElapsedTimer call;
            moves << waitForFrame(
                [&]() {
                    call.start();
                    if (!timeline->requestClipMove(upper, tid, i % 2 == 0 ? pos + 1 : pos - 1, true, true, true)) {
                        ++refused;
                    }
                    calls << double(call.nsecsElapsed()) / 1e6;
                },
                refresh);
        }
        QJsonObject m = summary(moves);
        m.insert(QStringLiteral("appel"), summary(calls));
        if (refused > 0) {
            m.insert(QStringLiteral("refuses"), refused);
        }
        result.insert(QStringLiteral("deplacer"), m);
    }

    // 5. Régler l'étalonnage du plan (gain du premier correcteur), comme un curseur qu'on bouge.
    if (base >= 0 && KoraCutGradeController::self()) {
        QJsonObject graph = KoraCutGradeController::self()->graphFor(base);
        QJsonArray nodes = graph.value(QStringLiteral("noeuds")).toArray();
        int index = -1;
        for (int k = 0; k < nodes.size() && index < 0; ++k) {
            if (nodes.at(k).toObject().value(QStringLiteral("type")).toString(QStringLiteral("correcteur")) == QLatin1String("correcteur")) {
                index = k;
            }
        }
        if (index >= 0) {
            QList<double> edits;
            for (int i = 0; i < 6; ++i) {
                QJsonObject n = nodes.at(index).toObject();
                QJsonObject prim = n.value(QStringLiteral("primaires")).toObject();
                QJsonArray gain = prim.value(QStringLiteral("gain")).toArray();
                if (gain.size() < 4) {
                    gain = QJsonArray{1., 1., 1., 1.};
                }
                gain[0] = gain.at(0).toDouble() * (i % 2 == 0 ? 1.02 : 1. / 1.02);
                prim.insert(QStringLiteral("gain"), gain);
                n.insert(QStringLiteral("primaires"), prim);
                nodes[index] = n;
                graph.insert(QStringLiteral("noeuds"), nodes);
                const QJsonObject g = graph;
                idle();
                edits << waitForFrame([base, g]() { KoraCutGradeController::self()->setGraphFor(base, g, QStringLiteral("mesure")); }, refresh);
            }
            result.insert(QStringLiteral("regler_etalonnage"), summary(edits));
        }
    }

    // 6. Glisser la tête de lecture (4 octobre 2026) : voir measureDrag.
    idle();
    result.insert(QStringLiteral("glisser"), measureDrag(monitor, duration));
    done(result);
}

bool waitForRender(int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    QEventLoop loop;
    QTimer tick;
    tick.setInterval(250);
    int idleTicks = 0;
    bool finished = false;
    QObject::connect(&tick, &QTimer::timeout, &loop, [&]() {
        PreviewManager *manager = currentPreviewManager();
        if (!manager) {
            finished = true;
            loop.quit();
            return;
        }
        if (manager->isPending()) {
            idleTicks = 0;
        } else if (manager->dirtyList().isEmpty()) {
            finished = true;
            loop.quit();
            return;
        } else if (++idleTicks > 12) {
            // Des morceaux restent à rendre mais rien ne tourne depuis 3 s : un échec, ou le plafond du cache.
            loop.quit();
            return;
        }
        if (clock.elapsed() > timeoutMs) {
            loop.quit();
        }
    });
    tick.start();
    loop.exec();
    return finished;
}

QJsonObject stateJson()
{
    TimelineController *controller = nullptr;
    PreviewManager *manager = currentPreviewManager(&controller);
    const int chunk = KdenliveSettings::timelinechunks();
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("automatique"), KdenliveSettings::autopreview()}};
    if (!manager) {
        result.insert(QStringLiteral("en_cours"), false);
        result.insert(QStringLiteral("rendues"), QJsonArray());
        result.insert(QStringLiteral("a_rendre"), QJsonArray());
        result.insert(QStringLiteral("remarque"), QStringLiteral("Aucun rendu dans cette séquence pour l'instant."));
        return result;
    }
    const QVariantList rendered = manager->renderedList();
    const QVariantList dirty = manager->dirtyList();
    const double fps = std::max(1., pCore->getCurrentFps());
    result.insert(QStringLiteral("en_cours"), manager->isRunning());
    result.insert(QStringLiteral("progression"), manager->isRunning() ? manager->progressPermille() / 10 : (dirty.isEmpty() ? 100 : 0));
    if (manager->workingPreview >= 0) {
        result.insert(QStringLiteral("morceau_en_cours"), timeJson(manager->workingPreview));
    }
    result.insert(QStringLiteral("rendues"), rangesJson(rendered, chunk));
    result.insert(QStringLiteral("a_rendre"), rangesJson(dirty, chunk));
    result.insert(QStringLiteral("secondes_rendues"), qRound(10. * rendered.size() * chunk / fps) / 10.);
    result.insert(QStringLiteral("secondes_a_rendre"), qRound(10. * dirty.size() * chunk / fps) / 10.);
    const qint64 bytes = manager->cacheBytes();
    result.insert(QStringLiteral("cache"), QJsonObject{{QStringLiteral("octets"), bytes},
                                                       {QStringLiteral("mo"), qRound(10. * double(bytes) / 1048576.) / 10.},
                                                       {QStringLiteral("plafond_mo"), manager->cacheCapBytes() / 1048576},
                                                       {QStringLiteral("dossier"), manager->getCacheDir().absolutePath()}});
    result.insert(QStringLiteral("profil"), manager->consumerParameters());
    return result;
}

} // namespace KoraCutRender

// ---------------------------------------------------------------------------------------------

KoraCutRenderButton::KoraCutRenderButton(QWidget *parent)
    : QToolButton(parent)
{
    setObjectName(QStringLiteral("koracutRenderButton"));
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setPopupMode(QToolButton::MenuButtonPopup);
    setAutoRaise(true);
    // La largeur ne saute pas quand le pourcentage change : on réserve celle du plus long libellé.
    const QFontMetrics metrics(font());
    int widest = 0;
    for (const QString &label : {i18nc("@action:intoolbar render state", "Render"), i18nc("@action:intoolbar render state, %1 is a percentage", "Render %1%", 100),
                                 i18nc("@action:intoolbar render state", "Render up to date")}) {
        widest = std::max(widest, metrics.horizontalAdvance(label));
    }
    m_textWidth = widest + 16 + 36;
    m_poll.setInterval(400);
    connect(&m_poll, &QTimer::timeout, this, [this]() { refresh(); });
    connect(this, &QToolButton::clicked, this, [this]() {
        PreviewManager *manager = currentPreviewManager();
        if (manager && manager->isRunning()) {
            if (m_stop) {
                m_stop->trigger();
            }
        } else if (m_render) {
            m_render->trigger();
        }
        refresh();
    });
    refresh();
}

void KoraCutRenderButton::setActions(QAction *render, QAction *stop)
{
    m_render = render;
    m_stop = stop;
    refresh();
}

void KoraCutRenderButton::showEvent(QShowEvent *event)
{
    QToolButton::showEvent(event);
    m_poll.start();
    refresh();
}

void KoraCutRenderButton::hideEvent(QHideEvent *event)
{
    m_poll.stop();
    QToolButton::hideEvent(event);
}

void KoraCutRenderButton::refresh(bool force)
{
    // 0 rien à rendre, 1 à rendre, 2 en cours, 3 à jour
    PreviewManager *manager = currentPreviewManager();
    int state = 0;
    int percent = -1;
    int dirtyCount = 0;
    if (manager) {
        dirtyCount = int(manager->dirtyList().size());
        if (manager->isRunning()) {
            state = 2;
            percent = manager->progressPermille() / 10;
        } else if (dirtyCount > 0) {
            state = 1;
        } else if (!manager->renderedList().isEmpty()) {
            state = 3;
        }
    }
    if (!force && state == m_state && percent == m_percent) {
        return;
    }
    m_state = state;
    m_percent = percent;
    const double fps = pCore ? std::max(1., pCore->getCurrentFps()) : 25.;
    const int toRender = qRound(dirtyCount * KdenliveSettings::timelinechunks() / fps);
    // KoraCut (22 octobre 2026, KC-011) : l'icône seule au repos (le libellé est dans l'infobulle) ; le pourcentage
    // à côté de l'icône seulement pendant un rendu, avec une largeur réservée pour qu'il ne saute pas.
    setToolButtonStyle(state == 2 ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    setMinimumWidth(state == 2 ? m_textWidth : 0);
    switch (state) {
    case 2:
        setIcon(QIcon::fromTheme(QStringLiteral("preview-render-off")));
        setText(i18nc("@action:intoolbar render state, %1 is a percentage", "Render %1%", percent));
        setToolTip(i18nc("@info:tooltip", "Rendering so playback stays smooth. Click to stop."));
        break;
    case 1:
        setIcon(QIcon::fromTheme(QStringLiteral("preview-render-on")));
        setText(i18nc("@action:intoolbar render state", "Render"));
        setToolTip(i18ncp("@info:tooltip", "%1 second waits to be rendered. Click to render it now.", "%1 seconds wait to be rendered. Click to render them now.",
                          std::max(1, toRender)));
        break;
    case 3:
        setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok")));
        setText(i18nc("@action:intoolbar render state", "Render up to date"));
        setToolTip(i18nc("@info:tooltip", "The rendered passages play smoothly. Click to render the zone of the Program monitor, or the whole sequence."));
        break;
    default:
        setIcon(QIcon::fromTheme(QStringLiteral("preview-render-on")));
        setText(i18nc("@action:intoolbar render state", "Render"));
        setToolTip(i18nc("@info:tooltip", "Render the zone of the Program monitor, or the whole sequence, so that effects and grading play smoothly"));
        break;
    }
}
