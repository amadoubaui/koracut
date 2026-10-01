/*
    SPDX-FileCopyrightText: 2016 Jean-Baptiste Mardelle <jb@kdenlive.org>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "previewmanager.h"
#include "koracut/koracuthwdecode.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "dialogs/wizard.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "koracut/koracutrender.h"
#include "mainwindow.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "xml/xml.hpp"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <QCollator>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QSaveFile>
#include <QThread>
#include <QStandardPaths>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <sys/resource.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

#if defined(Q_OS_WIN)
namespace {
/** KoraCut : suspendre et reprendre un processus sous Windows, comme SIGSTOP et SIGCONT ailleurs.
 *  NtSuspendProcess et NtResumeProcess (ntdll) sont ce qu'utilisent le Gestionnaire des tâches et
 *  Process Explorer ; on les cherche à l'exécution, et sans eux on retombe sur l'arrêt du rendu. */
bool koraCutSuspendProcess(qint64 pid, bool suspend)
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
} // namespace
#endif

PreviewManager::PreviewManager(Mlt::Tractor *tractor, QUuid uuid, QObject *parent)
    : QObject(parent)
    , workingPreview(-1)
    , m_tractor(tractor)
    , m_uuid(uuid)
    , m_previewTrack(nullptr)
    , m_overlayTrack(nullptr)
    , m_warnOnCrash(true)
    , m_previewTrackIndex(-1)
    , m_initialized(false)
{
    m_previewGatherTimer.setSingleShot(true);
    m_previewGatherTimer.setInterval(200);
    QObject::connect(&m_previewProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &PreviewManager::processEnded);

    if (KdenliveSettings::koracutrendererpath().isEmpty() || !QFileInfo::exists(KdenliveSettings::koracutrendererpath())) {
        KdenliveSettings::setKoracutrendererpath(QString());
        Wizard::fixKdenliveRenderPath();
        if (KdenliveSettings::koracutrendererpath().isEmpty()) {
            KMessageBox::error(QApplication::activeWindow(),
                               i18n("Could not find the koracut_render application, something is wrong with your installation. Rendering will not work"));
        }
    }

    connect(this, &PreviewManager::abortPreview, &m_previewProcess, &QProcess::kill, Qt::DirectConnection);
    connect(&m_previewProcess, &QProcess::readyReadStandardError, this, &PreviewManager::receivedStderr);
}

PreviewManager::~PreviewManager()
{
    if (m_initialized) {
        abortRendering();
        if (m_undoDir.dirName() == QLatin1String("undo")) {
            m_undoDir.removeRecursively();
        }
        if ((pCore->currentDoc()->url().isEmpty() && m_cacheDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) ||
            m_cacheDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
            if (m_cacheDir.dirName() == QLatin1String("preview")) {
                m_cacheDir.removeRecursively();
            }
        }
    }
    delete m_overlayTrack;
    delete m_previewTrack;
}

bool PreviewManager::initialize()
{
    // Make sure our document id does not contain .. tricks
    bool ok;
    KdenliveDoc *doc = pCore->currentDoc();
    QString documentId = QDir::cleanPath(doc->getDocumentProperty(QStringLiteral("documentid")));
    documentId.toLongLong(&ok, 10);
    if (!ok || documentId.isEmpty()) {
        // Something is wrong, documentId should be a number (ms since epoch), abort
        pCore->displayMessage(i18n("Wrong document ID, cannot create temporary folder"), ErrorMessage);
        return false;
    }
    m_cacheDir = doc->getCacheDir(CachePreview, &ok, m_uuid);
    qDebug() << ":: GOT CACHE DIR: " << m_cacheDir.absolutePath();
    if (!ok || !m_cacheDir.exists()) {
        pCore->displayMessage(i18n("Cannot read folder %1", m_cacheDir.absolutePath()), ErrorMessage);
        return false;
    }
    if (m_uuid == doc->uuid()) {
        if (m_cacheDir.dirName() != QLatin1String("preview") || m_cacheDir == QDir() ||
            (!m_cacheDir.exists(QStringLiteral("undo")) && !m_cacheDir.mkdir(QStringLiteral("undo"))) || !m_cacheDir.absolutePath().contains(documentId)) {
            pCore->displayMessage(i18n("Something is wrong with cache folder %1", m_cacheDir.absolutePath()), ErrorMessage);
            return false;
        }
    } else {
        if (m_cacheDir.dirName().toLatin1() != QCryptographicHash::hash(m_uuid.toByteArray(), QCryptographicHash::Md5).toHex() || m_cacheDir == QDir() ||
            (!m_cacheDir.exists(QStringLiteral("undo")) && !m_cacheDir.mkdir(QStringLiteral("undo"))) || !m_cacheDir.absolutePath().contains(documentId)) {
            pCore->displayMessage(i18n("Something is wrong with cache folder %1", m_cacheDir.absolutePath()), ErrorMessage);
            return false;
        }
    }
    if (!loadParams()) {
        pCore->displayMessage(i18n("Invalid timeline preview parameters"), ErrorMessage);
        return false;
    }
    m_undoDir = QDir(m_cacheDir.absoluteFilePath(QStringLiteral("undo")));

    // Make sure our cache dirs are inside the temporary folder
    if (!m_cacheDir.makeAbsolute() || !m_undoDir.makeAbsolute() || !m_undoDir.mkpath(QStringLiteral("."))) {
        pCore->displayMessage(i18n("Something is wrong with cache folders"), ErrorMessage);
        return false;
    }

    connect(this, &PreviewManager::cleanupOldPreviews, this, &PreviewManager::doCleanupOldPreviews);
    connect(doc, &KdenliveDoc::removeInvalidUndo, this, &PreviewManager::slotRemoveInvalidUndo, Qt::DirectConnection);
    m_previewTimer.setSingleShot(true);
    // KoraCut : 1 s d'inactivité après un geste (3 s chez Kdenlive), le rendu attend que la souris soit relâchée.
    m_previewTimer.setInterval(1000);
    connect(&m_previewTimer, &QTimer::timeout, this, &PreviewManager::slotAutoStart);
    // KoraCut : pendant la lecture et pendant un geste, le rendu se met en pause (le processus est
    // suspendu, rien n'est perdu), puis reprend. Sous Windows aussi depuis le 30 septembre 2026
    // (NtSuspendProcess) ; il ne s'arrête et repart après que si la suspension est refusée.
    m_playbackWatch.setInterval(250);
    connect(&m_playbackWatch, &QTimer::timeout, this, [this]() {
        if (m_previewProcess.state() == QProcess::NotRunning) {
            m_playbackWatch.stop();
            return;
        }
        const bool busy = userIsBusy();
#if defined(Q_OS_UNIX)
        if (busy != m_suspended) {
            ::kill(pid_t(m_previewProcess.processId()), busy ? SIGSTOP : SIGCONT);
            m_suspended = busy;
        }
#elif defined(Q_OS_WIN)
        if (busy != m_suspended) {
            if (koraCutSuspendProcess(m_previewProcess.processId(), busy)) {
                m_suspended = busy;
            } else if (busy) {
                abortRendering();
                m_previewTimer.start();
            }
        }
#else
        if (busy) {
            abortRendering();
            m_previewTimer.start();
        }
#endif
    });
    {
        bool rootOk = false;
        const QDir previewRoot = doc->getCacheDir(CachePreview, &rootOk);
        if (rootOk) {
            KoraCutRender::touchOpenedMarker(previewRoot.absolutePath());
        }
    }
    connect(this, &PreviewManager::previewRender, this, &PreviewManager::gotPreviewRender, Qt::DirectConnection);
    connect(&m_previewGatherTimer, &QTimer::timeout, this, &PreviewManager::slotProcessDirtyChunks);
    m_initialized = true;
    return true;
}

bool PreviewManager::buildPreviewTrack()
{
    if (m_previewTrack != nullptr) {
        return false;
    }
    // Create overlay track
    qDebug() << "/// BUILDING PREVIEW TRACK\n----------------------\n----------------__";
    m_previewTrack = new Mlt::Playlist(pCore->getProjectProfile());
    m_previewTrack->set("kdenlive:playlistid", "timeline_preview");
    m_tractor->lock();
    reconnectTrack();
    m_tractor->unlock();
    return true;
}

void PreviewManager::loadChunks(QVariantList previewChunks, QVariantList dirtyChunks, Mlt::Playlist &playlist)
{
    if (previewChunks.isEmpty()) {
        previewChunks = m_renderedChunks;
    }
    if (dirtyChunks.isEmpty()) {
        dirtyChunks = m_dirtyChunks;
    }

    QStringList existingChuncks;
    if (!previewChunks.isEmpty()) {
        existingChuncks = m_cacheDir.entryList(QDir::Files);
    }

    int max = playlist.count();
    std::shared_ptr<Mlt::Producer> clip;
    m_tractor->lock();
    if (max == 0) {
        // Empty timeline preview, mark all as dirty
        for (auto &prev : previewChunks) {
            dirtyChunks << prev;
        }
    }
    for (int i = 0; i < max; i++) {
        if (playlist.is_blank(i)) {
            continue;
        }
        int position = playlist.clip_start(i);
        if (previewChunks.contains(QString::number(position))) {
            if (existingChuncks.contains(QStringLiteral("%1.%2").arg(position).arg(m_extension))) {
                clip.reset(playlist.get_clip(i));
                m_renderedChunks << position;
                m_previewTrack->insert_at(position, clip.get(), 1);
            } else {
                dirtyChunks << position;
            }
        }
    }
    m_previewTrack->consolidate_blanks();
    m_tractor->unlock();
    if (!dirtyChunks.isEmpty()) {
        std::sort(dirtyChunks.begin(), dirtyChunks.end(), chunkSort);
        QMutexLocker lock(&m_dirtyMutex);
        for (const auto &i : std::as_const(dirtyChunks)) {
            if (!m_dirtyChunks.contains(i)) {
                m_dirtyChunks << i;
            }
        }
        Q_EMIT dirtyChunksChanged();
    }
    if (!previewChunks.isEmpty()) {
        Q_EMIT renderedChunksChanged();
    }
}

void PreviewManager::deletePreviewTrack()
{
    m_tractor->lock();
    disconnectTrack();
    delete m_previewTrack;
    m_previewTrack = nullptr;
    m_dirtyChunks.clear();
    m_renderedChunks.clear();
    Q_EMIT dirtyChunksChanged();
    Q_EMIT renderedChunksChanged();
    m_tractor->unlock();
}

const QDir PreviewManager::getCacheDir() const
{
    return m_cacheDir;
}

void PreviewManager::reconnectTrack()
{
    disconnectTrack();
    if (!m_previewTrack && !m_overlayTrack) {
        m_previewTrackIndex = -1;
        return;
    }
    m_previewTrackIndex = m_tractor->count();
    int increment = 0;
    if (m_previewTrack) {
        m_tractor->insert_track(*m_previewTrack, m_previewTrackIndex);
        std::shared_ptr<Mlt::Producer> tk(m_tractor->track(m_previewTrackIndex));
        tk->set("hide", 2);
        // tk->set("kdenlive:playlistid", "timeline_preview");
        increment++;
    }
    if (m_overlayTrack) {
        m_tractor->insert_track(*m_overlayTrack, m_previewTrackIndex + increment);
        std::shared_ptr<Mlt::Producer> tk(m_tractor->track(m_previewTrackIndex + increment));
        tk->set("hide", 2);
        // tk->set("kdenlive:playlistid", "timeline_overlay");
    }
}

void PreviewManager::disconnectTrack()
{
    if (m_previewTrackIndex > -1) {
        Mlt::Producer *prod = m_tractor->track(m_previewTrackIndex);
        if (strcmp(prod->get("kdenlive:playlistid"), "timeline_preview") == 0 || strcmp(prod->get("kdenlive:playlistid"), "timeline_overlay") == 0) {
            m_tractor->remove_track(m_previewTrackIndex);
        }
        delete prod;
        if (m_tractor->count() == m_previewTrackIndex + 1) {
            // overlay track still here, remove
            Mlt::Producer *trkprod = m_tractor->track(m_previewTrackIndex);
            if (strcmp(trkprod->get("kdenlive:playlistid"), "timeline_overlay") == 0) {
                m_tractor->remove_track(m_previewTrackIndex);
            }
            delete trkprod;
        }
    }
    m_previewTrackIndex = -1;
}

void PreviewManager::disable()
{
    if (m_previewTrackIndex > -1) {
        if (m_previewTrack) {
            m_previewTrack->set("hide", 3);
        }
        if (m_overlayTrack) {
            m_overlayTrack->set("hide", 3);
        }
    }
}

void PreviewManager::enable()
{
    if (m_previewTrackIndex > -1) {
        if (m_previewTrack) {
            m_previewTrack->set("hide", 2);
        }
        if (m_overlayTrack) {
            m_overlayTrack->set("hide", 2);
        }
    }
}

bool PreviewManager::loadParams()
{
    KdenliveDoc *doc = pCore->currentDoc();
    m_extension = doc->getDocumentProperty(QStringLiteral("previewextension"));
    m_consumerParams = doc->getDocumentProperty(QStringLiteral("previewparameters")).split(QLatin1Char(' '), Qt::SkipEmptyParts);

    if (m_consumerParams.isEmpty() || m_extension.isEmpty()) {
        // KoraCut : H.264 tout-intra d'abord (mesuré le 24 septembre 2026 : plus rapide à écrire et à relire
        // que DNxHD, 2,4 fois plus léger, même qualité), le choix de Kdenlive sinon.
        if (!selectKoraCutPreviewProfile()) {
            doc->selectPreviewProfile();
        }
        m_consumerParams = doc->getDocumentProperty(QStringLiteral("previewparameters")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        m_extension = doc->getDocumentProperty(QStringLiteral("previewextension"));
    }
    if (m_consumerParams.isEmpty() || m_extension.isEmpty()) {
        return false;
    }
    // Remove the r= and s= parameter (forcing framerate / frame size) as it causes rendering failure.
    // These parameters should be provided by MLT's profile
    // NOTE: this is still required for DNxHD so leave it
    /*for (int i = 0; i < m_consumerParams.count(); i++) {
        if (m_consumerParams.at(i).startsWith(QStringLiteral("r=")) || m_consumerParams.at(i).startsWith(QStringLiteral("s="))) {
            m_consumerParams.removeAt(i);
            i--;
        }
    }*/
    if (doc->getDocumentProperty(QStringLiteral("resizepreview")).toInt() != 0) {
        int resizeWidth = doc->getDocumentProperty(QStringLiteral("previewheight")).toInt();
        // KoraCut : largeur et hauteur paires, sinon libx264 refuse l'image.
        resizeWidth -= resizeWidth % 2;
        int width = int(resizeWidth * pCore->getCurrentDar());
        width -= width % 2;
        m_consumerParams << QStringLiteral("s=%1x%2").arg(width).arg(resizeWidth);
    }
    // KoraCut : deux fils au plus pour les effets (un seul sur machine modeste) : le rendu ne doit pas charger la machine.
    bool hasThreads = false;
    for (const QString &param : std::as_const(m_consumerParams)) {
        if (param.startsWith(QLatin1String("real_time="))) {
            hasThreads = true;
        }
    }
    if (!hasThreads) {
        const int threads = KoraCutRender::isModestMachine() ? 1 : std::clamp(QThread::idealThreadCount() / 4, 1, 2);
        m_consumerParams << QStringLiteral("real_time=-%1").arg(threads);
    }
    m_consumerParams << QStringLiteral("an=1");
    m_consumerParams << QStringLiteral("audio_off=1");
    if (KdenliveSettings::gpu_accel()) {
        m_consumerParams << QStringLiteral("glsl.=1");
    }
    return true;
}

void PreviewManager::invalidatePreviews()
{
    QMutexLocker lock(&m_previewMutex);
    bool timer = KdenliveSettings::autopreview();
    if (m_previewTimer.isActive()) {
        m_previewTimer.stop();
        timer = true;
    }
    KdenliveDoc *doc = pCore->currentDoc();
    int stackIx = doc->commandStack()->index();
    int stackMax = doc->commandStack()->count();
    if (stackIx == stackMax && !m_undoDir.exists(QString::number(stackIx - 1))) {
        // We just added a new command in stack, archive existing chunks
        int ix = stackIx - 1;
        m_undoDir.mkdir(QString::number(ix));
        bool foundPreviews = false;
        for (const auto &i : m_dirtyChunks) {
            QString current = QStringLiteral("%1.%2").arg(i.toInt()).arg(m_extension);
            if (archiveChunkFile(current, ix)) {
                foundPreviews = true;
            }
        }
        if (!foundPreviews) {
            // No preview files found, remove undo folder
            m_undoDir.rmdir(QString::number(ix));
        } else {
            // new chunks archived, cleanup old ones
            Q_EMIT cleanupOldPreviews();
        }
        if (!m_dirtyChunksToRemove.isEmpty()) {
            for (int ix : std::as_const(m_dirtyChunksToRemove)) {
                m_dirtyChunks.removeAll(ix);
            }
            m_dirtyChunksToRemove.clear();
            Q_EMIT dirtyChunksChanged();
        }
    } else {
        // Restore existing chunks, delete others
        // Check if we just undo the last stack action, then backup, otherwise delete
        bool lastUndo = false;
        if (stackIx + 1 == stackMax) {
            if (!m_undoDir.exists(QString::number(stackMax))) {
                lastUndo = true;
                bool foundPreviews = false;
                m_undoDir.mkdir(QString::number(stackMax));
                for (const auto &i : m_dirtyChunks) {
                    QString current = QStringLiteral("%1.%2").arg(i.toInt()).arg(m_extension);
                    if (archiveChunkFile(current, stackMax)) {
                        foundPreviews = true;
                    }
                }
                if (!foundPreviews) {
                    m_undoDir.rmdir(QString::number(stackMax));
                }
            }
        }
        bool moveFile = true;
        QDir tmpDir = m_undoDir;
        if (!tmpDir.cd(QString::number(stackIx))) {
            moveFile = false;
        }
        QVariantList foundChunks;
        for (const auto &i : m_dirtyChunks) {
            QString cacheFileName = QStringLiteral("%1.%2").arg(i.toInt()).arg(m_extension);
            if (!lastUndo) {
                m_cacheDir.remove(cacheFileName);
            }
            if (moveFile) {
                if (QFile::copy(tmpDir.absoluteFilePath(cacheFileName), m_cacheDir.absoluteFilePath(cacheFileName))) {
                    foundChunks << i;
                } else {
                    qDebug() << "// ERROR PROCESSE CHUNK: " << i << ", " << cacheFileName;
                }
            }
        }
        if (!m_dirtyChunksToRemove.isEmpty()) {
            for (int ix : std::as_const(m_dirtyChunksToRemove)) {
                m_dirtyChunks.removeAll(ix);
            }
            m_dirtyChunksToRemove.clear();
            Q_EMIT dirtyChunksChanged();
        }
        if (!foundChunks.isEmpty()) {
            std::sort(foundChunks.begin(), foundChunks.end(), chunkSort);
            m_dirtyMutex.lock();
            for (auto &ck : foundChunks) {
                m_dirtyChunks.removeAll(ck);
                m_renderedChunks << ck;
            }
            m_dirtyMutex.unlock();
            Q_EMIT dirtyChunksChanged();
            Q_EMIT renderedChunksChanged();
            reloadChunks(foundChunks);
        }
    }
    doc->setModified(true);
    if (timer) {
        m_previewTimer.start();
    }
}

void PreviewManager::doCleanupOldPreviews()
{
    if (m_undoDir.dirName() != QLatin1String("undo")) {
        return;
    }
    QStringList dirs = m_undoDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // Use QCollator to do a natural sorting so that 10 is after 2
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(dirs.begin(), dirs.end(), [&collator](const QString &file1, const QString &file2) { return collator.compare(file1, file2) < 0; });
    bool ok;
    // KoraCut : on ne garde que les étapes demandées (0 par défaut, le disque d'abord).
    while (dirs.count() > KoraCutRender::undoStepsKept()) {
        QDir tmp = m_undoDir;
        QString dirName = dirs.takeFirst();
        dirName.toInt(&ok);
        if (ok && tmp.cd(dirName)) {
            tmp.removeRecursively();
        }
    }
}

void PreviewManager::clearPreviewRange(bool resetZones)
{
    m_previewGatherTimer.stop();
    abortRendering();

    // Mark all chunks as dirty
    QList<int> toRemove;
    QMutexLocker lock(&m_dirtyMutex);
    for (auto &frame : m_renderedChunks) {
        toRemove << frame.toInt();
    }
    if (!toRemove.isEmpty() && !resetZones) {
        loadParams();
        return;
    }
    for (auto &frame : m_dirtyChunks) {
        toRemove << frame.toInt();
    }
    Fun undo = [this, dirty = toRemove]() {
        for (int ix : std::as_const(dirty)) {
            m_dirtyChunks << ix;
        }
        m_previewGatherTimer.start();
        return true;
    };
    Fun redo = [this, dirty = toRemove, resetZones]() {
        m_tractor->lock();
        bool hasPreview = m_previewTrack != nullptr;
        for (auto &frame : dirty) {
            if (m_renderedChunks.contains(frame)) {
                m_renderedChunks.removeAll(frame);
                m_dirtyChunks << frame;
            } else if (resetZones) {
                m_dirtyChunks.removeAll(frame);
            }
            if (!hasPreview) {
                continue;
            }
            int trackIx = m_previewTrack->get_clip_index_at(frame);
            if (!m_previewTrack->is_blank(trackIx)) {
                Mlt::Producer *prod = m_previewTrack->replace_with_blank(trackIx);
                delete prod;
            }
        }
        Q_EMIT renderedChunksChanged();
        if (resetZones) {
            m_dirtyChunksToRemove = dirty;
        } else {
            // Reload preview params
            loadParams();
            Q_EMIT dirtyChunksChanged();
        }
        if (hasPreview) {
            m_previewTrack->consolidate_blanks();
        }
        m_tractor->unlock();
        m_previewGatherTimer.start();
        return true;
    };

    m_previewGatherTimer.stop();
    abortRendering();
    redo();
    if (resetZones) {
        // KoraCut : les zones parties, leurs fichiers partent aussi. koracut_render saute un fichier
        // déjà présent : un vieux morceau resté sur le disque serait relu comme s'il était à jour.
        QDirIterator it(m_cacheDir.absolutePath(), {QStringLiteral("*.%1").arg(m_extension)}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile::remove(it.next());
        }
    }
    pCore->pushUndo(undo, redo, i18n("Remove All Preview Zones"));
}

void PreviewManager::addPreviewRange(const QPoint zone, bool add)
{
    int chunkSize = KdenliveSettings::timelinechunks();
    int startChunk = zone.x() / chunkSize;
    int endChunk = int(rintl(zone.y() / chunkSize));
    QList<int> toRemove;
    QMutexLocker lock(&m_dirtyMutex);
    for (int i = startChunk; i <= endChunk; i++) {
        int frame = i * chunkSize;
        if (add) {
            // KoraCut : demandée à la main, la zone reste rendue même sans effet.
            m_evictedChunks.remove(frame);
            m_autoChunks.remove(frame);
            if (!m_renderedChunks.contains(frame) && !m_dirtyChunks.contains(frame)) {
                m_dirtyChunks << frame;
            }
        } else {
            toRemove << frame;
        }
    }
    if (add) {
        Q_EMIT dirtyChunksChanged();
        if (m_previewProcess.state() == QProcess::NotRunning && KdenliveSettings::autopreview()) {
            m_previewTimer.start();
        }
    } else {
        // Remove processed chunks
        if (toRemove.isEmpty()) {
            // Nothing to do, abort
            return;
        }
        bool isRendering = m_previewProcess.state() != QProcess::NotRunning;
        Fun undo = [this, dirty = toRemove]() {
            for (int ix : std::as_const(dirty)) {
                m_dirtyChunks << ix;
            }
            m_previewGatherTimer.start();
            return true;
        };
        Fun redo = [this, dirty = toRemove, isRendering]() {
            m_tractor->lock();
            bool hasPreview = m_previewTrack != nullptr;
            for (auto &frame : dirty) {
                if (m_renderedChunks.contains(frame)) {
                    m_renderedChunks.removeAll(frame);
                    m_dirtyChunks << frame;
                } else {
                    m_dirtyChunks.removeAll(frame);
                }
                if (!hasPreview) {
                    continue;
                }
                int trackIx = m_previewTrack->get_clip_index_at(frame);
                if (!m_previewTrack->is_blank(trackIx)) {
                    Mlt::Producer *prod = m_previewTrack->replace_with_blank(trackIx);
                    delete prod;
                }
            }
            Q_EMIT renderedChunksChanged();
            m_dirtyChunksToRemove = dirty;
            if (hasPreview) {
                m_previewTrack->consolidate_blanks();
            }
            m_tractor->unlock();
            m_previewGatherTimer.start();
            if (isRendering || KdenliveSettings::autopreview()) {
                m_previewTimer.start();
            }
            return true;
        };

        m_previewGatherTimer.stop();
        abortRendering();
        redo();
        pCore->pushUndo(undo, redo, i18n("Remove Preview Zone"));
    }
}

void PreviewManager::abortRendering()
{
    if (m_previewProcess.state() == QProcess::NotRunning) {
        return;
    }
    // Don't display error message on voluntary abort
    m_warnOnCrash = false;
#if defined(Q_OS_UNIX)
    if (m_suspended) {
        ::kill(pid_t(m_previewProcess.processId()), SIGCONT);
        m_suspended = false;
    }
#elif defined(Q_OS_WIN)
    if (m_suspended) {
        koraCutSuspendProcess(m_previewProcess.processId(), false);
        m_suspended = false;
    }
#endif
    Q_EMIT abortPreview();
    m_previewProcess.waitForFinished();
    if (m_previewProcess.state() != QProcess::NotRunning) {
        m_previewProcess.kill();
        m_previewProcess.waitForFinished();
    }
    // Re-init time estimation
    Q_EMIT previewRender(-1, QString(), 1000);
}

bool PreviewManager::hasDefinedRange() const
{
    return (!m_renderedChunks.isEmpty() || !m_dirtyChunks.isEmpty());
}

void PreviewManager::startPreviewRender()
{
    QMutexLocker lock(&m_previewMutex);
    if (!m_dirtyChunks.isEmpty()) {
        // Abort any rendering
        abortRendering();
        m_waitingThumbs.clear();
        // clear log
        m_errorLog.clear();
        const QString sceneList = m_cacheDir.absoluteFilePath(QStringLiteral("preview.mlt"));
        if (!KdenliveSettings::proxypreview() && pCore->currentDoc()->useProxy()) {
            const QString playlist =
                pCore->projectItemModel()->sceneList(m_cacheDir.absolutePath(), QString(), pCore->currentDoc()->getTimeline(m_uuid)->tractor(), -1).first;
            QDomDocument doc;
            doc.setContent(playlist);
            KdenliveDoc::useOriginals(doc);
            if (!Xml::docContentToFile(doc, sceneList)) {
                return;
            }
        } else {
            pCore->currentDoc()->getTimeline(m_uuid)->sceneList(m_cacheDir.absolutePath(), sceneList);
        }
        m_previewTimer.stop();
        doPreviewRender(sceneList);
    }
}

void PreviewManager::receivedStderr()
{
    QStringList resultList = QString::fromLocal8Bit(m_previewProcess.readAllStandardError()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto &result : resultList) {
        if (result.startsWith(QLatin1String("START:"))) {
            if (m_previewProcess.state() == QProcess::Running) {
                workingPreview = result.section(QLatin1String("START:"), 1).simplified().toInt();
                Q_EMIT workingPreviewChanged();
            }
        } else if (result.startsWith(QLatin1String("DONE:"))) {
            int chunk = result.section(QLatin1String("DONE:"), 1).simplified().toInt();
            m_processedChunks++;
            QString fileName = QStringLiteral("%1.%2").arg(chunk).arg(m_extension);
            Q_EMIT previewRender(chunk, m_cacheDir.absoluteFilePath(fileName), 1000 * m_processedChunks / m_chunksToRender);
        } else {
            m_errorLog.append(result);
        }
    }
}

void PreviewManager::doPreviewRender(const QString &scene)
{
    // initialize progress bar
    if (m_dirtyChunks.isEmpty()) {
        return;
    }
    QMutexLocker lock(&m_dirtyMutex);
    Q_ASSERT(m_previewProcess.state() == QProcess::NotRunning);
    std::sort(m_dirtyChunks.begin(), m_dirtyChunks.end(), chunkSort);
    // KoraCut : de la tête de lecture vers la fin, puis en remontant vers le début.
    const QStringList dirtyChunks = orderedChunkList();
    m_chunksToRender = m_dirtyChunks.count();
    m_processedChunks = 0;
    m_lastRequested.clear();
    for (const QVariant &c : std::as_const(m_dirtyChunks)) {
        m_lastRequested.insert(c.toInt());
    }
    int chunkSize = KdenliveSettings::timelinechunks();
    lock.unlock();
    makeRoomFor(m_chunksToRender);
    lock.relock();
    QStringList args{QStringLiteral("preview-chunks"),
                     scene,
                     m_cacheDir.absolutePath(),
                     dirtyChunks.join(QLatin1Char(',')),
                     QString::number(chunkSize - 1),
                     pCore->getCurrentProfilePath(),
                     m_extension,
                     m_consumerParams.join(QLatin1Char(' '))};
    pCore->currentDoc()->previewProgress(0);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // KoraCut : jamais la puce pour un projet dont un clip ne se décode pas avec elle (image blanche sinon).
    KoraCutHwDecode::applyTo(env);
    m_previewProcess.setProcessEnvironment(env);
    // KoraCut : le rendu tourne en priorité basse, l'interface et la lecture passent devant.
#if defined(Q_OS_UNIX)
    m_previewProcess.setChildProcessModifier([]() { ::setpriority(PRIO_PROCESS, 0, 10); });
#elif defined(Q_OS_WIN)
    m_previewProcess.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= BELOW_NORMAL_PRIORITY_CLASS; });
#endif
    m_previewProcess.start(KdenliveSettings::koracutrendererpath(), args);
    if (m_previewProcess.waitForStarted()) {
        qDebug() << " -  - -STARTING PREVIEW JOBS . . . STARTED: " << args;
    }
    m_suspended = false;
    m_playbackWatch.start();
}

void PreviewManager::processEnded(int exitCode, QProcess::ExitStatus status)
{
    const QString sceneList = m_cacheDir.absoluteFilePath(QStringLiteral("preview.mlt"));
    QFile::remove(sceneList);
    if (pCore->window() && (status == QProcess::QProcess::CrashExit || exitCode != 0)) {
        Q_EMIT previewRender(0, m_errorLog, -1);
        if (workingPreview >= 0) {
            const QString fileName = QStringLiteral("%1.%2").arg(workingPreview).arg(m_extension);
            if (m_cacheDir.exists(fileName)) {
                m_cacheDir.remove(fileName);
            }
        }
    } else {
        // Normal exit and exit code 0: everything okay
        pCore->currentDoc()->previewProgress(1000);
    }
    workingPreview = -1;
    m_warnOnCrash = true;
    m_playbackWatch.stop();
    m_suspended = false;
    Q_EMIT workingPreviewChanged();
    // KoraCut : des morceaux arrivés pendant le rendu (un autre geste) partent à leur tour.
    if (status == QProcess::NormalExit && exitCode == 0 && KdenliveSettings::autopreview()) {
        QMutexLocker lock(&m_dirtyMutex);
        bool newWork = false;
        for (const QVariant &c : std::as_const(m_dirtyChunks)) {
            if (!m_lastRequested.contains(c.toInt())) {
                newWork = true;
                break;
            }
        }
        if (newWork) {
            m_previewTimer.start();
        }
    }
}

void PreviewManager::slotProcessDirtyChunks()
{
    if (m_dirtyChunks.isEmpty()) {
        return;
    }
    invalidatePreviews();
    if (KdenliveSettings::autopreview()) {
        m_previewTimer.start();
    }
}

void PreviewManager::slotRemoveInvalidUndo(int ix)
{
    QMutexLocker lock(&m_previewMutex);
    if (m_undoDir.dirName() != QLatin1String("undo")) {
        // Make sure we delete correct folder
        return;
    }
    QStringList dirs = m_undoDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    bool ok;
    for (const QString &dir : std::as_const(dirs)) {
        if (dir.toInt(&ok) >= ix && ok) {
            QDir tmp = m_undoDir;
            if (tmp.cd(dir)) {
                tmp.removeRecursively();
            }
        }
    }
}

void PreviewManager::invalidatePreview(int startFrame, int endFrame)
{
    if (m_previewTrack == nullptr) {
        return;
    }
    int chunkSize = KdenliveSettings::timelinechunks();
    int start = startFrame - startFrame % chunkSize;
    int end = endFrame - endFrame % chunkSize;
    // KoraCut : une zone écartée (cache plein, rendus effacés) qui change redevient candidate au rendu automatique.
    for (int i = start; i <= end; i += chunkSize) {
        m_evictedChunks.remove(i);
    }
    bool timerWasRunning = m_previewGatherTimer.isActive();
    m_previewGatherTimer.stop();
    bool previewWasRunning = m_previewProcess.state() == QProcess::Running;
    bool alreadyRendered = false;
    bool wasInDirtyZone = false;
    if (!m_renderedChunks.isEmpty()) {
        // Check if the invalidated zone was already rendered
        std::sort(m_renderedChunks.begin(), m_renderedChunks.end(), chunkSort);
        if (start <= m_renderedChunks.last().toInt() && end >= m_renderedChunks.first().toInt()) {
            alreadyRendered = true;
        } else if (workingPreview >= start && workingPreview <= end) {
            alreadyRendered = true;
        }
    }
    if (!alreadyRendered && !m_dirtyChunks.isEmpty()) {
        // Check if the invalidate zone is in the current todo list (dirtychunks)
        std::sort(m_dirtyChunks.begin(), m_dirtyChunks.end(), chunkSort);
        if (start <= m_dirtyChunks.last().toInt() && end >= m_dirtyChunks.first().toInt()) {
            wasInDirtyZone = true;
        }
    }
    if (alreadyRendered) {
        if (previewWasRunning) {
            abortRendering();
        }
        m_tractor->lock();
        bool chunksChanged = false;
        for (int i = start; i <= end; i += chunkSize) {
            if (m_renderedChunks.contains(i)) {
                int ix = m_previewTrack->get_clip_index_at(i);
                if (m_previewTrack->is_blank(ix)) {
                    continue;
                }
                Mlt::Producer *prod = m_previewTrack->replace_with_blank(ix);
                delete prod;
                QVariant val(i);
                m_renderedChunks.removeAll(val);
                if (!m_dirtyChunks.contains(val)) {
                    QMutexLocker lock(&m_dirtyMutex);
                    m_dirtyChunks << val;
                    chunksChanged = true;
                }
            }
        }
        m_tractor->unlock();
        if (chunksChanged) {
            m_previewTrack->consolidate_blanks();
            Q_EMIT renderedChunksChanged();
            Q_EMIT dirtyChunksChanged();
        }
    } else if (wasInDirtyZone) {
        // Abort rendering, playlist needs to be recreated
        if (previewWasRunning) {
            abortRendering();
        }
    } else if (!timerWasRunning) {
        // Invalidated zone outside our rendered zones
        return;
    }
    m_previewGatherTimer.start();
}

void PreviewManager::reloadChunks(const QVariantList &chunks)
{
    if (m_previewTrack == nullptr || chunks.isEmpty()) {
        return;
    }
    m_tractor->lock();
    for (const auto &ix : chunks) {
        if (m_previewTrack->is_blank_at(ix.toInt())) {
            QString fileName = m_cacheDir.absoluteFilePath(QStringLiteral("%1.%2").arg(ix.toInt()).arg(m_extension));
            fileName.prepend(QStringLiteral("avformat:"));
            Mlt::Producer prod(pCore->getProjectProfile(), fileName.toUtf8().constData());
            if (prod.is_valid()) {
                // m_ruler->updatePreview(ix, true);
                prod.set("mlt_service", "avformat-novalidate");
                m_previewTrack->insert_at(ix.toInt(), &prod, 1);
            }
        }
    }
    m_previewTrack->consolidate_blanks();
    m_tractor->unlock();
}

void PreviewManager::gotPreviewRender(int frame, const QString &file, int progress)
{
    if (m_previewTrack == nullptr) {
        return;
    }
    if (frame < 0) {
        pCore->currentDoc()->previewProgress(1000);
        return;
    }
    if (file.isEmpty() || progress < 0) {
        pCore->currentDoc()->previewProgress(progress);
        if (progress < 0) {
            if (m_warnOnCrash) {
                pCore->displayMessage(i18n("Preview rendering failed, check your parameters. %1Show details...%2",
                                           QStringLiteral("<a href=\"%1\">").arg(QUrl::toPercentEncoding(file)), QStringLiteral("</a>")),
                                      MltError);
            } else {
                // TODO display info about stopped preview job
            }
        }
        return;
    }
    if (m_previewTrack->is_blank_at(frame)) {
        Mlt::Producer prod(pCore->getProjectProfile(), QStringLiteral("avformat:%1").arg(file).toUtf8().constData());
        if (prod.is_valid() && prod.get_length() == KdenliveSettings::timelinechunks()) {
            m_dirtyMutex.lock();
            m_dirtyChunks.removeAll(QVariant(frame));
            m_dirtyMutex.unlock();
            m_renderedChunks << frame;
            Q_EMIT renderedChunksChanged();
            prod.set("mlt_service", "avformat-novalidate");
            m_tractor->lock();
            m_previewTrack->insert_at(frame, &prod, 1);
            m_previewTrack->consolidate_blanks();
            m_tractor->unlock();
            pCore->currentDoc()->previewProgress(progress);
            pCore->currentDoc()->setModified(true);
            // KoraCut : au plafond du cache, le rendu s'arrête ; ce qui reste est écarté jusqu'au prochain changement.
            m_cacheEstimate += QFileInfo(file).size();
            if (m_cacheEstimate > cacheCapBytes()) {
                m_warnOnCrash = false;
                Q_EMIT abortPreview();
                QMutexLocker lock(&m_dirtyMutex);
                for (const QVariant &c : std::as_const(m_dirtyChunks)) {
                    m_evictedChunks.insert(c.toInt());
                    m_autoChunks.remove(c.toInt());
                }
                m_dirtyChunks.clear();
                lock.unlock();
                Q_EMIT dirtyChunksChanged();
                if (!m_capWarned) {
                    m_capWarned = true;
                    pCore->displayMessage(i18nc("@info:status", "The render cache is full (%1 MB): the rest is not rendered. Clear renders, or raise the limit.",
                                                cacheCapBytes() / 1048576),
                                          InformationMessage, 8000);
                }
            }
        } else {
            qCDebug(KDENLIVE_LOG) << "* * * INVALID PROD: " << file;
            corruptedChunk(frame, file);
        }
    } else {
        qCDebug(KDENLIVE_LOG) << "* * * NON EMPTY PROD: " << frame;
    }
}

void PreviewManager::corruptedChunk(int frame, const QString &fileName)
{
    Q_EMIT abortPreview();
    m_previewProcess.waitForFinished();
    if (workingPreview >= 0) {
        workingPreview = -1;
        Q_EMIT workingPreviewChanged();
    }
    Q_EMIT previewRender(0, m_errorLog, -1);
    m_cacheDir.remove(fileName);
    if (!m_dirtyChunks.contains(frame)) {
        QMutexLocker lock(&m_dirtyMutex);
        m_dirtyChunks << frame;
        std::sort(m_dirtyChunks.begin(), m_dirtyChunks.end(), chunkSort);
    }
}

int PreviewManager::setOverlayTrack(Mlt::Playlist *overlay)
{
    m_overlayTrack = overlay;
    m_overlayTrack->set("kdenlive:playlistid", "timeline_overlay");
    reconnectTrack();
    return m_previewTrackIndex;
}

void PreviewManager::removeOverlayTrack()
{
    delete m_overlayTrack;
    m_overlayTrack = nullptr;
    reconnectTrack();
}

QPair<QStringList, QStringList> PreviewManager::previewChunks()
{
    QMutexLocker lock(&m_dirtyMutex);
    std::sort(m_renderedChunks.begin(), m_renderedChunks.end(), chunkSort);
    const QStringList renderedChunks = getCompressedList(m_renderedChunks);
    std::sort(m_dirtyChunks.begin(), m_dirtyChunks.end(), chunkSort);
    const QStringList dirtyChunks = getCompressedList(m_dirtyChunks);
    lock.unlock();
    return {renderedChunks, dirtyChunks};
}

const QStringList PreviewManager::getCompressedList(const QVariantList items) const
{
    QStringList resultString;
    int lastFrame = -1;
    QString currentString;
    for (const QVariant &frame : items) {
        int current = frame.toInt();
        if (current - KdenliveSettings::timelinechunks() == lastFrame) {
            lastFrame = current;
            if (frame == items.last()) {
                currentString.append(QStringLiteral("-%1").arg(lastFrame));
                resultString << currentString;
                currentString.clear();
            }
            continue;
        }
        if (currentString.isEmpty()) {
            currentString = frame.toString();
        } else if (currentString == QString::number(lastFrame)) {
            // Only one chunk, store it
            resultString << currentString;
            currentString = frame.toString();
        } else {
            // Range, store
            currentString.append(QStringLiteral("-%1").arg(lastFrame));
            resultString << currentString;
            currentString = frame.toString();
        }
        lastFrame = current;
    }
    if (!currentString.isEmpty()) {
        resultString << currentString;
    }
    return resultString;
}

bool PreviewManager::hasOverlayTrack() const
{
    return m_overlayTrack != nullptr;
}

bool PreviewManager::hasPreviewTrack() const
{
    return m_previewTrack != nullptr;
}

int PreviewManager::addedTracks() const
{
    if (m_previewTrack) {
        if (m_overlayTrack) {
            return 2;
        }
        return 1;
    } else if (m_overlayTrack) {
        return 1;
    }
    return -1;
}

bool PreviewManager::isRunning() const
{
    return workingPreview >= 0 || m_previewProcess.state() != QProcess::NotRunning;
}

// ---- KoraCut, rendu d'aperçu (24 septembre 2026, chantier 10) ---------------------------------

QVariantList PreviewManager::renderedList() const
{
    QMutexLocker lock(&m_dirtyMutex);
    return m_renderedChunks;
}

QVariantList PreviewManager::dirtyList() const
{
    QMutexLocker lock(&m_dirtyMutex);
    return m_dirtyChunks;
}

int PreviewManager::progressPermille() const
{
    return m_chunksToRender > 0 ? std::clamp(1000 * m_processedChunks / m_chunksToRender, 0, 1000) : 0;
}

bool PreviewManager::isPending() const
{
    return isRunning() || m_previewTimer.isActive() || m_previewGatherTimer.isActive();
}

qint64 PreviewManager::cacheBytes() const
{
    return KoraCutRender::folderSize(m_cacheDir.absolutePath());
}

qint64 PreviewManager::cacheCapBytes() const
{
    bool ok = false;
    const QDir root = pCore->currentDoc() ? pCore->currentDoc()->getCacheDir(CachePreview, &ok) : m_cacheDir;
    return KoraCutRender::cacheCapBytes(ok ? root.absolutePath() : m_cacheDir.absolutePath(), m_cacheEstimate);
}

QString PreviewManager::consumerParameters() const
{
    return m_consumerParams.join(QLatin1Char(' ')) + QStringLiteral(";") + m_extension;
}

bool PreviewManager::archiveChunkFile(const QString &fileName, int undoIndex)
{
    if (!m_cacheDir.exists(fileName)) {
        return false;
    }
    if (KoraCutRender::undoStepsKept() > 0) {
        return m_cacheDir.rename(fileName, QStringLiteral("undo/%1/%2").arg(undoIndex).arg(fileName));
    }
    // Par défaut, un morceau invalidé part tout de suite : on ne garde pas d'historique sur le disque.
    m_cacheDir.remove(fileName);
    return false;
}

void PreviewManager::slotAutoStart()
{
    // Un geste en cours (clic maintenu, glisser) ou la lecture : on attend.
    if (userIsBusy()) {
        m_previewTimer.start();
        return;
    }
    startPreviewRender();
}

void PreviewManager::setAutoChunks(const QList<int> &heavyChunks)
{
    if (!m_initialized || m_previewTrack == nullptr) {
        return;
    }
    const QSet<int> heavy(heavyChunks.cbegin(), heavyChunks.cend());
    bool changed = false;
    QMutexLocker lock(&m_dirtyMutex);
    // Ce qui ne porte plus d'effet redevient de la lecture directe : on ne le rend pas.
    const QSet<int> previous = m_autoChunks;
    for (int chunk : previous) {
        if (heavy.contains(chunk)) {
            continue;
        }
        m_autoChunks.remove(chunk);
        if (m_dirtyChunks.contains(QVariant(chunk)) && chunk != workingPreview) {
            m_dirtyChunks.removeAll(QVariant(chunk));
            m_cacheDir.remove(QStringLiteral("%1.%2").arg(chunk).arg(m_extension));
            changed = true;
        }
    }
    for (int chunk : heavy) {
        const QVariant v(chunk);
        if (m_evictedChunks.contains(chunk) || m_renderedChunks.contains(v) || m_dirtyChunks.contains(v)) {
            continue;
        }
        m_dirtyChunks << v;
        m_autoChunks.insert(chunk);
        changed = true;
    }
    const bool hasWork = !m_dirtyChunks.isEmpty();
    lock.unlock();
    if (changed) {
        Q_EMIT dirtyChunksChanged();
    }
    if (!KdenliveSettings::autopreview() || !hasWork) {
        return;
    }
    if (m_previewProcess.state() == QProcess::NotRunning && !m_previewTimer.isActive() && !m_previewGatherTimer.isActive()) {
        // Le contrôleur a déjà attendu la seconde d'inactivité : on part tout de suite.
        slotAutoStart();
    } else if (changed && m_previewProcess.state() != QProcess::NotRunning) {
        // Des morceaux nouveaux pendant un rendu : processEnded les reprendra.
    }
}

void PreviewManager::clearRenderFiles(int duration)
{
    const int chunkSize = KdenliveSettings::timelinechunks();
    clearPreviewRange(true);
    {
        QMutexLocker lock(&m_dirtyMutex);
        m_autoChunks.clear();
        for (int frame = 0; frame <= duration; frame += chunkSize) {
            m_evictedChunks.insert(frame);
        }
    }
    if (m_undoDir.dirName() == QLatin1String("undo")) {
        const QStringList dirs = m_undoDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &dir : dirs) {
            QDir tmp = m_undoDir;
            if (tmp.cd(dir)) {
                tmp.removeRecursively();
            }
        }
    }
    m_cacheEstimate = 0;
    m_capWarned = false;
}

void PreviewManager::koraCutDiscardAll()
{
    m_previewGatherTimer.stop();
    abortRendering();
    bool changed = false;
    {
        QMutexLocker lock(&m_dirtyMutex);
        m_tractor->lock();
        const QVariantList rendered = m_renderedChunks;
        for (const QVariant &frame : rendered) {
            if (m_previewTrack != nullptr) {
                const int ix = m_previewTrack->get_clip_index_at(frame.toInt());
                if (!m_previewTrack->is_blank(ix)) {
                    Mlt::Producer *prod = m_previewTrack->replace_with_blank(ix);
                    delete prod;
                }
            }
            if (!m_dirtyChunks.contains(frame)) {
                m_dirtyChunks << frame;
            }
            changed = true;
        }
        m_renderedChunks.clear();
        if (m_previewTrack != nullptr) {
            m_previewTrack->consolidate_blanks();
        }
        m_tractor->unlock();
    }
    // koracut_render saute un fichier déjà présent : les fichiers partent, ceux gardés pour l'annulation aussi.
    QDirIterator it(m_cacheDir.absolutePath(), {QStringLiteral("*.%1").arg(m_extension)}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile::remove(it.next());
    }
    m_cacheEstimate = 0;
    m_capWarned = false;
    if (changed) {
        Q_EMIT renderedChunksChanged();
        Q_EMIT dirtyChunksChanged();
        m_previewGatherTimer.start();
    }
}

QStringList PreviewManager::orderedChunkList()
{
    const int chunkSize = KdenliveSettings::timelinechunks();
    int playhead = pCore->getMonitorPosition(Kdenlive::ProjectMonitor);
    playhead -= playhead % chunkSize;
    QVariantList after;
    QList<int> before;
    for (const QVariant &c : std::as_const(m_dirtyChunks)) {
        if (c.toInt() >= playhead) {
            after << c;
        } else {
            before << c.toInt();
        }
    }
    QStringList ordered = getCompressedList(after);
    // Avant la tête de lecture : du plus proche au plus loin, un morceau à la fois.
    std::sort(before.begin(), before.end(), std::greater<int>());
    for (int c : std::as_const(before)) {
        ordered << QString::number(c);
    }
    return ordered;
}

void PreviewManager::makeRoomFor(int chunkCount)
{
    KdenliveDoc *doc = pCore->currentDoc();
    if (!doc) {
        return;
    }
    bool ok = false;
    const QDir root = doc->getCacheDir(CachePreview, &ok);
    if (!ok || root.dirName() != QLatin1String("preview")) {
        return;
    }
    struct Entry
    {
        QString path;
        qint64 size;
        QDateTime time;
        int rank;
    };
    std::vector<Entry> files;
    qint64 total = 0;
    qint64 ownBytes = 0;
    int ownCount = 0;
    const QString ownDir = m_cacheDir.absolutePath();
    QDirIterator it(root.absolutePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (info.fileName().startsWith(QLatin1Char('.')) || info.suffix() == QLatin1String("mlt")) {
            continue;
        }
        total += info.size();
        // Ordre de départ : l'historique d'annulation, puis les autres séquences, puis les nôtres.
        int rank = 2;
        if (info.absolutePath().contains(QLatin1String("/undo"))) {
            rank = 0;
        } else if (info.absolutePath() != ownDir) {
            rank = 1;
        } else {
            ownBytes += info.size();
            ++ownCount;
        }
        files.push_back({info.absoluteFilePath(), info.size(), info.lastModified(), rank});
    }
    const qint64 perChunk = ownCount > 0 ? ownBytes / ownCount : 2LL * 1024 * 1024;
    const qint64 needed = perChunk * chunkCount;
    const qint64 cap = KoraCutRender::cacheCapBytes(root.absolutePath(), total);
    m_cacheEstimate = total;
    if (total + needed <= cap) {
        return;
    }
    std::sort(files.begin(), files.end(), [](const Entry &a, const Entry &b) { return a.rank != b.rank ? a.rank < b.rank : a.time < b.time; });
    bool trackChanged = false;
    m_tractor->lock();
    for (const Entry &e : files) {
        if (total + needed <= cap) {
            break;
        }
        const QFileInfo info(e.path);
        if (e.rank == 2) {
            bool isNumber = false;
            const int chunk = info.completeBaseName().toInt(&isNumber);
            if (!isNumber || m_dirtyChunks.contains(QVariant(chunk))) {
                continue;
            }
            if (m_renderedChunks.contains(QVariant(chunk)) && m_previewTrack) {
                const int ix = m_previewTrack->get_clip_index_at(chunk);
                if (!m_previewTrack->is_blank(ix)) {
                    delete m_previewTrack->replace_with_blank(ix);
                }
                m_renderedChunks.removeAll(QVariant(chunk));
                trackChanged = true;
            }
            m_evictedChunks.insert(chunk);
            m_autoChunks.remove(chunk);
        }
        if (QFile::remove(e.path)) {
            total -= e.size;
        }
    }
    if (trackChanged && m_previewTrack) {
        m_previewTrack->consolidate_blanks();
    }
    m_tractor->unlock();
    m_cacheEstimate = total;
    if (trackChanged) {
        Q_EMIT renderedChunksChanged();
    }
}

bool PreviewManager::selectKoraCutPreviewProfile()
{
    if (!KdenliveSettings::previewparams().isEmpty()) {
        // Un profil choisi à la main dans les préférences reste le choix de la personne.
        return false;
    }
    KConfig conf(QStringLiteral("encodingprofiles.rc"), KConfig::CascadeConfig, QStandardPaths::AppDataLocation);
    const KConfigGroup group(&conf, QStringLiteral("timelinepreview"));
    const QString profile = group.readEntry("KoraCut H.264 intra", QString());
    if (profile.isEmpty()) {
        return false;
    }
    KdenliveDoc *doc = pCore->currentDoc();
    doc->setDocumentProperty(QStringLiteral("previewparameters"), profile.section(QLatin1Char(';'), 0, 0));
    doc->setDocumentProperty(QStringLiteral("previewextension"), profile.section(QLatin1Char(';'), 1, 1));
    // Machine modeste : rendu à la moitié de la hauteur du projet (540 pour du 1080p), plus vite rendu et relu.
    if (KoraCutRender::isModestMachine() && doc->getDocumentProperty(QStringLiteral("resizepreview")).isEmpty()) {
        const int height = pCore->getCurrentFrameDisplaySize().height() / 2;
        doc->setDocumentProperty(QStringLiteral("resizepreview"), QStringLiteral("1"));
        doc->setDocumentProperty(QStringLiteral("previewheight"), QString::number(height - height % 2));
    }
    return true;
}

bool PreviewManager::userIsBusy()
{
    if (QGuiApplication::mouseButtons() != Qt::NoButton) {
        return true;
    }
    Monitor *monitor = (pCore && pCore->monitorManager()) ? pCore->monitorManager()->projectMonitor() : nullptr;
    return monitor && monitor->isPlaying();
}
