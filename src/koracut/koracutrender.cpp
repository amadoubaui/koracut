/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutrender.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"
#include "monitor/monitorproxy.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/previewmanager.h"
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
#include <QFileInfo>
#include <QJsonArray>
#include <QMenu>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>

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

void applyFirstLaunchDefaults()
{
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
            QJsonObject result{{QStringLiteral("ok"), true},
                               {QStringLiteral("duree_ms"), ms},
                               {QStringLiteral("images_attendues"), expected},
                               {QStringLiteral("images_affichees"), shown},
                               {QStringLiteral("images_sautees"), std::max(0, covered - shown)},
                               {QStringLiteral("retard"), std::max(0, expected - covered)},
                               {QStringLiteral("ips"), ms > 0 ? qRound(10. * shown * 1000. / double(ms)) / 10. : 0.},
                               {QStringLiteral("profil"), MachineProfile::levelId(MachineProfile::effectiveLevel())},
                               {QStringLiteral("apercu_min"), MachineProfile::scalingHeight(lowestScalingValue(state->lowestScaling))},
                               {QStringLiteral("decodage"), KdenliveSettings::hwDecoding().isEmpty() ? QStringLiteral("logiciel") : KdenliveSettings::hwDecoding()},
                               {QStringLiteral("memoire_max_mo"), peakMemoryMb()},
                               // Temps processeur du processus pendant la lecture, rapporté à la durée : le nombre de
                               // cœurs occupés. Une machine à 4 cœurs ne suit plus au-delà de 3 environ.
                               {QStringLiteral("coeurs_occupes"), ms > 0 ? qRound(100. * cpu * 1000. / double(ms)) / 100. : 0.}};
            done(result);
        });
    });
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
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setPopupMode(QToolButton::MenuButtonPopup);
    setAutoRaise(true);
    // La largeur ne saute pas quand le pourcentage change : on réserve celle du plus long libellé.
    const QFontMetrics metrics(font());
    int widest = 0;
    for (const QString &label : {i18nc("@action:intoolbar render state", "Render"), i18nc("@action:intoolbar render state, %1 is a percentage", "Render %1%", 100),
                                 i18nc("@action:intoolbar render state", "Render up to date")}) {
        widest = std::max(widest, metrics.horizontalAdvance(label));
    }
    setMinimumWidth(widest + 16 + 36);
    m_poll.setInterval(400);
    connect(&m_poll, &QTimer::timeout, this, &KoraCutRenderButton::refresh);
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

void KoraCutRenderButton::refresh()
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
    if (state == m_state && percent == m_percent) {
        return;
    }
    m_state = state;
    m_percent = percent;
    const double fps = pCore ? std::max(1., pCore->getCurrentFps()) : 25.;
    const int toRender = qRound(dirtyCount * KdenliveSettings::timelinechunks() / fps);
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
