/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutmotionmirror.h"
#include "koracutmotioncatalog.h"
#include "koracutmotionproject.h"
#include "koracutmotionrenderer.h"

#include "koracut/koracutagenttools.h"

#include "bin/bin.h"
#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "jobs/taskmanager.h"
#include "mainwindow.h"
#include "project/projectmanager.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <cmath>

namespace {
const QLatin1String kTrackProject("kdenlive:koracut_motion");
const QLatin1String kTrackIndex("kdenlive:koracut_motion_track");
const QLatin1String kTrackAudio("kdenlive:koracut_motion_audio");
const QLatin1String kClipProject("kdenlive:koracut_motion_project");
const QLatin1String kClipElement("kdenlive:koracut_motion_element");
const QLatin1String kClipHash("kdenlive:koracut_motion_hash");
const QLatin1String kClipIn("kdenlive:koracut_motion_in");
// Le nom que le miroir a donné au clip du chutier. Un autre nom (chutier, renommer_media,
// organiser_rushs, la porte) est un nom choisi : le miroir n'y touche plus (28 septembre 2026).
const QLatin1String kClipAutoName("kdenlive:koracut_motion_nom_auto");

// Le nom d'un clip du chutier est-il encore celui que KoraCut lui a donné ?
bool automaticName(const std::shared_ptr<ProjectClip> &bin, const QStringList &knownNames, const QString &project)
{
    const QString current = bin->name().trimmed();
    if (current.isEmpty()) {
        return true;
    }
    const QString given = bin->getProducerProperty(kClipAutoName);
    if (!given.isEmpty()) {
        return current == given;
    }
    // Un projet d'avant le 28 septembre 2026 : pas de trace du nom donné. Le nom brut du fichier de rendu
    // (« h-compteur-300m-02072e7ff640.mov ») et les noms que le miroir a pu donner sont automatiques.
    if (current == QFileInfo(bin->url()).fileName() || knownNames.contains(current)) {
        return true;
    }
    static const QRegularExpression raw(QStringLiteral("^(.+)-[0-9a-f]{6,}\\.(mov|mp4|webm)$"));
    const QRegularExpressionMatch m = raw.match(current);
    return m.hasMatch() && m.captured(1) == project;
}

// Donne au clip son nom automatique, et le retient.
void giveAutomaticName(const std::shared_ptr<ProjectClip> &bin, const QString &name)
{
    if (bin->name() != name) {
        bin->setProperties({{QStringLiteral("kdenlive:clipname"), name}, {QString(kClipAutoName), name}});
        bin->updateTimelineClips({TimelineModel::NameRole});
    } else if (bin->getProducerProperty(kClipAutoName) != name) {
        bin->setProducerProperty(kClipAutoName, name);
    }
}
// Les pistes partagées des compositions d'un seul élément (lot 5) : la valeur de kTrackProject ne peut
// pas être un nom de projet (sanitizeName n'y laisse pas d'astérisque).
const QLatin1String kSharedAnimations("*animations");
const QLatin1String kSharedTransitions("*transitions");
} // namespace

KoraCutMotionMirror::KoraCutMotionMirror(QObject *parent)
    : QObject(parent)
    , m_renderer(new KoraCutMotionRenderer(this))
    , m_reconcile(new QTimer(this))
    , m_rescan(new QTimer(this))
{
    m_reconcile->setSingleShot(true);
    m_reconcile->setInterval(200);
    connect(m_reconcile, &QTimer::timeout, this, &KoraCutMotionMirror::reconcileTimeline);
    m_rescan->setSingleShot(true);
    m_rescan->setInterval(500);
    connect(m_rescan, &QTimer::timeout, this, &KoraCutMotionMirror::rescan);
    // Les outils motion_* des agents passent par ce miroir (un seul par application).
    KoraCutAgentTools::setMotionMirror(this);
    connect(m_renderer, &KoraCutMotionRenderer::rendered, this, &KoraCutMotionMirror::onRendered);
    connect(m_renderer, &KoraCutMotionRenderer::failed, this, &KoraCutMotionMirror::onRenderFailed);
    connect(m_renderer, &KoraCutMotionRenderer::progress, this, [this](const QString &project, const QString &id, int percent) {
        if (m_entries.contains(project)) {
            m_entries[project].progress[id] = percent;
            Q_EMIT elementsChanged(project);
        }
    });
}

KoraCutMotionMirror::~KoraCutMotionMirror()
{
    if (KoraCutAgentTools::motionMirror() == this) {
        KoraCutAgentTools::setMotionMirror(nullptr);
    }
}

int KoraCutMotionMirror::entryPoint(const KoraCutMotion::Element &element, double fps)
{
    // Une vidéo cuit son data-media-start dans le rendu : le clip commence toujours à 0. Un son lit
    // le fichier source, un bloc ou une image son rendu : le point d'entrée est data-media-start ou
    // data-koracut-in (rognage par la gauche, lot 3).
    if (element.kind == KoraCutMotion::Kind::Video || element.kind == KoraCutMotion::Kind::Whole) {
        return 0;
    }
    return frames(element.mediaStart, fps);
}

// ---------------------------------------------------------------------------------------------
// Le projet KoraCut

void KoraCutMotionMirror::clear()
{
    for (const QMetaObject::Connection &c : std::as_const(m_timelineConnections)) {
        disconnect(c);
    }
    m_timelineConnections.clear();
    for (const QPointer<TimelineItemModel> &model : std::as_const(m_watched)) {
        if (model) {
            disconnect(model.data(), nullptr, this, nullptr);
        }
    }
    m_watched.clear();
    m_dirty.clear();
    m_reconcile->stop();
    m_sequence.clear();
    m_mainSequence.clear();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        delete it->sync;
    }
    m_entries.clear();
    m_timeline.reset();
    m_doc = nullptr;
    Q_EMIT projectsChanged();
}

void KoraCutMotionMirror::setDocument(KdenliveDoc *doc, const std::shared_ptr<TimelineItemModel> &timeline)
{
    clear();
    m_doc = doc;
    m_timeline = timeline;
    if (!doc || !timeline) {
        return;
    }
    connect(doc, &QObject::destroyed, this, [this, doc]() {
        if (m_doc == doc) {
            clear();
        }
    });
    m_sequence = timeline->uuid().toString(QUuid::WithoutBraces);
    // La séquence principale : celle des compositions d'avant le suivi des séquences (sans
    // data-koracut-sequence). Retenue dans le projet la première fois, pour ne pas dépendre de la
    // séquence active à l'ouverture.
    m_mainSequence = doc->getDocumentProperty(QStringLiteral("koracut:motion_principale"));
    if (m_mainSequence.isEmpty()) {
        m_mainSequence = m_sequence;
        doc->setDocumentProperty(QStringLiteral("koracut:motion_principale"), m_mainSequence);
    }
    watchModel(timeline);
    // Le dossier motion/ suit le fichier projet : un projet neuf est nommé après connectDocument(),
    // un projet enregistré ailleurs change de dossier. On relit donc à chaque modification du document.
    m_timelineConnections << connect(doc, &KdenliveDoc::docModified, this, [this]() { m_rescan->start(); });
    rescan();
}

void KoraCutMotionMirror::watchModel(const std::shared_ptr<TimelineItemModel> &timeline)
{
    if (!timeline) {
        return;
    }
    const QString sequence = timeline->uuid().toString(QUuid::WithoutBraces);
    if (m_watched.value(sequence).data() == timeline.get()) {
        return;
    }
    m_watched.insert(sequence, QPointer<TimelineItemModel>(timeline.get()));
    // Un clip qui bouge, se rogne ou disparaît : la timeline commande, le fichier suit. Toutes les séquences
    // ouvertes sont écoutées, pas seulement l'active : un Annuler peut défaire un geste d'une autre séquence.
    connect(timeline.get(), &QAbstractItemModel::dataChanged, this, [this, sequence]() { scheduleReconcile(sequence); });
    connect(timeline.get(), &QAbstractItemModel::rowsRemoved, this, [this, sequence]() { scheduleReconcile(sequence); });
    connect(timeline.get(), &QAbstractItemModel::rowsInserted, this, [this, sequence]() { scheduleReconcile(sequence); });
}

QString KoraCutMotionMirror::resolved(const QString &sequence) const
{
    if (!sequence.isEmpty()) {
        return sequence;
    }
    return m_mainSequence.isEmpty() ? m_sequence : m_mainSequence;
}

std::shared_ptr<TimelineItemModel> KoraCutMotionMirror::timelineOf(const QString &sequence) const
{
    if (!m_doc || pCore->currentDoc() != m_doc) {
        return nullptr;
    }
    const QString wanted = resolved(sequence);
    if (wanted.isEmpty() || wanted == m_sequence) {
        return m_timeline.lock();
    }
    // Une autre séquence du projet : sa timeline si elle est ouverte dans un onglet. Fermée, elle attend
    // (son modèle existe encore, mais sa durée et ses groupes ne sont plus tenus à jour).
    const QUuid uuid(wanted);
    if (uuid.isNull()) {
        return nullptr;
    }
    auto tl = m_doc->getTimeline(uuid, true);
    return tl && !tl->isClosed ? tl : nullptr;
}

QString KoraCutMotionMirror::sequenceOf(const QString &project) const
{
    return m_entries.contains(project) ? resolved(m_entries[project].sync->composition().sequence) : QString();
}

std::shared_ptr<TimelineItemModel> KoraCutMotionMirror::timelineFor(const QString &project) const
{
    return m_entries.contains(project) ? timelineOf(m_entries[project].sync->composition().sequence) : nullptr;
}

void KoraCutMotionMirror::flush()
{
    if (m_reconcile->isActive()) {
        m_reconcile->stop();
        reconcileTimeline();
    }
}

void KoraCutMotionMirror::setTimeline(const std::shared_ptr<TimelineItemModel> &timeline)
{
    if (!m_doc || !timeline || timeline == m_timeline.lock()) {
        return;
    }
    // Ce que la séquence quittée vient de changer est écrit avant de passer à l'autre.
    flush();
    m_timeline = timeline;
    m_sequence = timeline->uuid().toString(QUuid::WithoutBraces);
    watchModel(timeline);
    if (pCore->currentDoc() == m_doc) {
        const QList<QUuid> uuids = m_doc->getTimelinesUuids();
        for (const QUuid &uuid : uuids) {
            if (auto other = m_doc->getTimeline(uuid, true); other && !other->isClosed) {
                watchModel(other);
            }
        }
    }
    // Les compositions de cette séquence qui attendaient (onglet fermé pendant leur rendu) sont posées.
    const QStringList names = m_entries.keys();
    for (const QString &name : names) {
        if (m_entries.contains(name) && resolved(m_entries[name].sync->composition().sequence) == m_sequence) {
            applyFromFile(name, false);
        }
    }
    Q_EMIT projectsChanged();
}

void KoraCutMotionMirror::rescan()
{
    if (!m_doc) {
        return;
    }
    const QString root = motionRoot();
    bool changed = false;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        // Hors du dossier motion/ du projet, ou dossier retiré à la main : la composition n'existe plus,
        // son nom est libre tout de suite (avant le 1er octobre 2026 : seulement au lancement suivant).
        if (!it->dir.startsWith(root + QLatin1Char('/')) || !QFile::exists(it->indexPath)) {
            delete it->sync;
            it = m_entries.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    const auto infos = KoraCutMotionProject::list(root);
    QStringList fresh;
    for (const auto &info : infos) {
        if (!m_entries.contains(info.name)) {
            registerProject(info.name, info.dir, info.indexPath);
            fresh << info.name;
            changed = true;
        }
    }
    if (changed) {
        qWarning() << "KoraCut: motion, dossier" << root << "compositions" << m_entries.size();
        Q_EMIT projectsChanged();
    }
    for (const QString &name : std::as_const(fresh)) {
        // À l'ouverture, on complète sans étape d'annulation : ce n'est pas un geste.
        applyFromFile(name, false);
    }
}

QString KoraCutMotionMirror::motionRoot() const
{
    return KoraCutMotionProject::motionRoot(m_doc);
}

std::shared_ptr<TimelineItemModel> KoraCutMotionMirror::timeline() const
{
    if (!m_doc || pCore->currentDoc() != m_doc) {
        return nullptr;
    }
    return m_timeline.lock();
}

double KoraCutMotionMirror::fps() const
{
    return pCore->getCurrentFps();
}

int KoraCutMotionMirror::frames(double seconds, double fps)
{
    return int(std::lround(seconds * fps));
}

void KoraCutMotionMirror::registerProject(const QString &name, const QString &dir, const QString &indexPath)
{
    if (m_entries.contains(name)) {
        return;
    }
    Entry entry;
    entry.name = name;
    entry.dir = dir;
    entry.indexPath = indexPath;
    entry.sync = new KoraCutMotionSync(indexPath, this);
    connect(entry.sync, &KoraCutMotionSync::changedOutside, this, [this, name]() {
        // Un agent, Studio ou le panneau Code a écrit le fichier : la timeline suit, en une étape.
        applyFromFile(name, true, i18nc("@action undo text", "Motion: update %1", name));
    });
    m_entries.insert(name, entry);
}

QStringList KoraCutMotionMirror::projects() const
{
    return m_entries.keys();
}

KoraCutMotionSync *KoraCutMotionMirror::sync(const QString &name) const
{
    return m_entries.contains(name) ? m_entries[name].sync : nullptr;
}

KoraCutMotionMirror::ProjectState KoraCutMotionMirror::projectState(const QString &name) const
{
    ProjectState state;
    if (!m_entries.contains(name)) {
        return state;
    }
    const Entry &entry = m_entries[name];
    state.name = name;
    state.dir = entry.dir;
    state.indexPath = entry.indexPath;
    state.errors = entry.sync->composition().errors;
    const KoraCutMotion::Composition &comp = entry.sync->composition();
    state.element = comp.elementKind;
    state.style = comp.style;
    state.display = KoraCutMotionCatalog::readableName(comp, name);
    state.retired = comp.retired;
    state.cut = comp.cut;
    if (comp.isElement() && !comp.elements.isEmpty()) {
        state.clipId = clipFor(name, comp.elements.first().id);
        state.binId = findBinClip(name, comp.elements.first().id);
    }
    const auto list = elements(name);
    state.elements = list.size();
    for (const ElementState &e : list) {
        if (e.pending) {
            ++state.pending;
        } else if (e.failed) {
            ++state.failed;
        } else if (!e.renderPath.isEmpty() || e.kind == QLatin1String("audio")) {
            ++state.rendered;
        }
    }
    return state;
}

QList<KoraCutMotionMirror::ElementState> KoraCutMotionMirror::elements(const QString &name) const
{
    QList<ElementState> list;
    if (!m_entries.contains(name)) {
        return list;
    }
    const Entry &entry = m_entries[name];
    for (const KoraCutMotion::Element &e : entry.sync->composition().elements) {
        ElementState s;
        s.id = e.id;
        s.kind = e.kindName();
        s.name = e.displayName();
        s.start = e.start;
        s.duration = e.duration;
        s.trackIndex = e.trackIndex;
        s.hash = e.hash;
        s.text = e.text;
        s.src = e.src;
        s.clipId = clipFor(name, e.id);
        s.binId = findBinClip(name, e.id);
        s.pending = m_renderer->isPending(name, e.id);
        s.failed = entry.failed.contains(e.id);
        if (e.kind != KoraCutMotion::Kind::Audio) {
            const QString output = renderOutput(entry, e);
            if (QFileInfo(output).size() > 0) {
                s.renderPath = output;
            }
        }
        list.append(s);
    }
    return list;
}

// ---------------------------------------------------------------------------------------------
// Créer, importer, rendre

QString KoraCutMotionMirror::createProject(const QString &name, QString *error, int width, int height, double wantedFps, const QString &html)
{
    if (!m_doc) {
        if (error) {
            *error = i18n("No project is open.");
        }
        return QString();
    }
    rescan();
    const QSize size = pCore->getCurrentFrameSize();
    const QString index = KoraCutMotionProject::create(motionRoot(), name, width > 0 ? width : size.width(), height > 0 ? height : size.height(),
                                                       wantedFps > 0 ? wantedFps : fps(), error);
    if (index.isEmpty()) {
        return QString();
    }
    if (!html.isEmpty()) {
        // Le HTML fourni (par un agent) remplace le gabarit ; assets/gsap.min.js est déjà là.
        QFile file(index);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (error) {
                *error = QStringLiteral("Le fichier %1 n'a pas pu être écrit.").arg(index);
            }
            return QString();
        }
        file.write(html.toUtf8());
        file.close();
    }
    stampSequence(index);
    const QString clean = KoraCutMotionProject::sanitizeName(name);
    registerProject(clean, QFileInfo(index).absolutePath(), index);
    Q_EMIT projectsChanged();
    applyFromFile(clean, true, i18nc("@action undo text", "Motion: new composition %1", clean));
    return clean;
}

QString KoraCutMotionMirror::importProject(const QString &source, const QString &name, QString *error)
{
    if (!m_doc) {
        if (error) {
            *error = i18n("No project is open.");
        }
        return QString();
    }
    rescan();
    QString wanted = name;
    if (wanted.isEmpty()) {
        const QFileInfo info(source);
        wanted = info.isDir() ? info.fileName() : info.completeBaseName();
    }
    const QString index = KoraCutMotionProject::import(motionRoot(), source, wanted, error);
    if (index.isEmpty()) {
        return QString();
    }
    stampSequence(index);
    const QString clean = KoraCutMotionProject::sanitizeName(wanted);
    registerProject(clean, QFileInfo(index).absolutePath(), index);
    Q_EMIT projectsChanged();
    applyFromFile(clean, true, i18nc("@action undo text", "Motion: import %1", clean));
    return clean;
}

void KoraCutMotionMirror::stampSequence(const QString &indexPath) const
{
    // Créée pendant qu'une autre séquence que la principale est devant : c'est là qu'elle vit. Sans cela,
    // une composition sans data-koracut-sequence part dans la séquence principale du projet.
    if (m_sequence.isEmpty() || m_sequence == m_mainSequence) {
        return;
    }
    QFile file(indexPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QString html = QString::fromUtf8(file.readAll());
    file.close();
    const QList<KoraCutMotion::Tag> tags = KoraCutMotion::tokenize(html);
    for (const KoraCutMotion::Tag &t : tags) {
        if (!t.closing && t.has(QStringLiteral("data-composition-id"))) {
            if (!t.attr(QStringLiteral("data-koracut-sequence")).trimmed().isEmpty()) {
                return;
            }
            html = KoraCutMotion::setAttribute(html, t, QStringLiteral("data-koracut-sequence"), m_sequence);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                file.write(html.toUtf8());
            }
            return;
        }
    }
}

QString KoraCutMotionMirror::trashProject(const QString &project)
{
    if (!m_entries.contains(project)) {
        return QString();
    }
    const QString dir = m_entries[project].dir;
    const QString trash = motionRoot() + QStringLiteral("/.corbeille");
    if (!QDir().mkpath(trash)) {
        return QString();
    }
    QString target = trash + QLatin1Char('/') + project + QLatin1Char('-') + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    for (int n = 2; QFile::exists(target); ++n) {
        target = trash + QLatin1Char('/') + project + QLatin1Char('-') + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QLatin1Char('-') + QString::number(n);
    }
    // Le synchroniseur surveille le fichier : il le lâche avant le déplacement (Windows refuse sinon).
    delete m_entries[project].sync;
    m_entries.remove(project);
    if (!QDir().rename(dir, target)) {
        registerProject(project, dir, dir + QStringLiteral("/index.html"));
        return QString();
    }
    Q_EMIT projectsChanged();
    return target;
}

void KoraCutMotionMirror::renderAll(const QString &name, bool force)
{
    rescan();
    const QStringList names = name.isEmpty() ? m_entries.keys() : QStringList{name};
    for (const QString &n : names) {
        if (!m_entries.contains(n)) {
            continue;
        }
        Entry &entry = m_entries[n];
        entry.failed.clear();
        for (const KoraCutMotion::Element &e : entry.sync->composition().elements) {
            if (e.kind == KoraCutMotion::Kind::Audio || !e.startResolved) {
                continue;
            }
            if (force) {
                QFile::remove(renderOutput(entry, e));
            }
            requestRender(entry, e);
        }
        Q_EMIT elementsChanged(n);
    }
}

int KoraCutMotionMirror::renderElements(const QString &name, const QStringList &ids, bool force)
{
    rescan();
    if (!m_entries.contains(name)) {
        return 0;
    }
    Entry &entry = m_entries[name];
    int count = 0;
    for (const KoraCutMotion::Element &e : entry.sync->composition().elements) {
        if (e.kind == KoraCutMotion::Kind::Audio || !e.startResolved || (!ids.isEmpty() && !ids.contains(e.id))) {
            continue;
        }
        if (force) {
            QFile::remove(renderOutput(entry, e));
        }
        entry.failed.remove(e.id);
        requestRender(entry, e);
        ++count;
    }
    Q_EMIT elementsChanged(name);
    return count;
}

QString KoraCutMotionMirror::renderOutput(const Entry &entry, const KoraCutMotion::Element &element) const
{
    // Un plan opaque rendu d'un seul tenant n'a pas besoin de transparence : MP4, bien plus léger.
    const bool opaque = element.kind == KoraCutMotion::Kind::Whole && entry.sync->composition().opaque;
    return KoraCutMotionProject::cacheDir(m_doc, entry.name) + QLatin1Char('/') + element.id + QLatin1Char('-') + element.hash +
        (opaque ? QStringLiteral(".mp4") : QStringLiteral(".mov"));
}

QString KoraCutMotionMirror::envelopeFor(const Entry &entry, const KoraCutMotion::Element &element) const
{
    using namespace KoraCutMotion;
    if (element.kind == Kind::Whole) {
        return QString();
    }
    const QString &html = entry.sync->html();
    const Composition &comp = entry.sync->composition();
    QList<Tag> tags = tokenize(html);
    QString out = html;
    // De la fin vers le début, pour que les positions restent justes. Les sons et les vidéos des
    // autres éléments se taisent : l'enveloppe ne rend que l'image de celui-ci.
    for (int i = tags.size() - 1; i >= 0; --i) {
        const Tag &t = tags.at(i);
        if (t.closing) {
            continue;
        }
        bool isOther = false;
        for (const Element &e : comp.elements) {
            if (e.tagIndex == i && e.id != element.id) {
                isOther = true;
                break;
            }
        }
        if (isOther && (t.name == QLatin1String("audio") || t.name == QLatin1String("video"))) {
            out = setAttribute(out, t, QStringLiteral("data-volume"), QStringLiteral("0"));
        }
    }
    // L'élément est ramené au début (data-start 0) et la racine dure sa durée : le rendu commence à
    // son image 1, quelle que soit sa place dans la composition. Un clip Motion a donc toujours un
    // point d'entrée à 0, ce qui permet d'échanger son média à chaque nouveau rendu sans toucher au
    // clip de la timeline (ni à l'annulation). C'est aussi ce que Studio fait de ses images clés :
    // elles sont relatives au début du clip. Une timeline GSAP écrite à la main en temps global est
    // l'approximation acceptée par le document 19.
    tags = tokenize(out);
    for (int i = 0; i < tags.size(); ++i) {
        if (!tags.at(i).closing && tags.at(i).has(QStringLiteral("data-composition-id"))) {
            out = setAttribute(out, tags.at(i), QStringLiteral("data-duration"), secondsText(element.duration));
            break;
        }
    }
    tags = tokenize(out);
    if (element.tagIndex >= 0 && element.tagIndex < tags.size()) {
        out = setAttribute(out, tags.at(element.tagIndex), QStringLiteral("data-start"), QStringLiteral("0"));
    }
    QStringList hidden;
    for (const Element &e : comp.elements) {
        if (e.id != element.id && e.kind != Kind::Audio) {
            hidden << QStringLiteral("[id=\"%1\"]").arg(QString(e.id).replace(QLatin1Char('"'), QLatin1String("\\\"")));
        }
    }
    QString style = QStringLiteral("<style id=\"koracut-envelope\">html,body{background:transparent !important}");
    if (!hidden.isEmpty()) {
        style += hidden.join(QLatin1Char(',')) + QStringLiteral("{visibility:hidden !important}");
    }
    style += QStringLiteral("</style>\n");
    const int head = out.indexOf(QLatin1String("</head>"), 0, Qt::CaseInsensitive);
    if (head >= 0) {
        out.insert(head, style);
    } else {
        out.prepend(style);
    }
    return out;
}

void KoraCutMotionMirror::requestRender(Entry &entry, const KoraCutMotion::Element &element)
{
    if (element.kind == KoraCutMotion::Kind::Audio || !element.startResolved || !m_doc) {
        return;
    }
    if (!element.durationKnown && element.kind != KoraCutMotion::Kind::Whole) {
        entry.failed.insert(element.id);
        return;
    }
    KoraCutMotionRenderRequest r;
    r.project = entry.name;
    r.projectDir = entry.dir;
    r.elementId = element.id;
    r.hash = element.hash;
    r.envelopeHtml = envelopeFor(entry, element);
    r.output = renderOutput(entry, element);
    r.format = r.output.endsWith(QLatin1String(".mp4")) ? QStringLiteral("mp4") : QStringLiteral("mov");
    r.fps = fps();
    entry.failed.remove(element.id);
    entry.progress[element.id] = 0;
    m_renderer->request(r);
}

// ---------------------------------------------------------------------------------------------
// Chercher les clips et les pistes

QString KoraCutMotionMirror::findBinClip(const QString &project, const QString &elementId) const
{
    auto model = pCore->projectItemModel();
    if (!model) {
        return QString();
    }
    const std::vector<QString> ids = model->getAllClipIds();
    for (const QString &id : ids) {
        auto clip = model->getClipByBinID(id);
        if (clip && clip->getProducerProperty(kClipProject) == project && clip->getProducerProperty(kClipElement) == elementId) {
            return id;
        }
    }
    return QString();
}

bool KoraCutMotionMirror::isMotionClip(int clipId) const
{
    return !clipInfo(clipId).project.isEmpty();
}

KoraCutMotionMirror::ClipInfo KoraCutMotionMirror::clipInfoIn(const std::shared_ptr<TimelineItemModel> &tl, int clipId)
{
    ClipInfo info;
    if (!tl || clipId < 0 || !tl->isClip(clipId)) {
        return info;
    }
    auto bin = pCore->projectItemModel()->getClipByBinID(tl->getClipBinId(clipId));
    if (!bin) {
        return info;
    }
    info.project = bin->getProducerProperty(kClipProject);
    info.elementId = bin->getProducerProperty(kClipElement);
    return info;
}

KoraCutMotionMirror::ClipInfo KoraCutMotionMirror::clipInfo(int clipId) const
{
    // La séquence active d'abord ; les identifiants de clips sont uniques dans toute l'application, un clip
    // d'une autre séquence ouverte se retrouve donc sans ambiguïté.
    auto active = m_timeline.lock();
    if (active && active->isClip(clipId)) {
        return clipInfoIn(active, clipId);
    }
    for (const QPointer<TimelineItemModel> &model : m_watched) {
        if (model && model.data() != active.get() && model->isClip(clipId)) {
            auto bin = pCore->projectItemModel()->getClipByBinID(model->getClipBinId(clipId));
            ClipInfo info;
            if (bin) {
                info.project = bin->getProducerProperty(kClipProject);
                info.elementId = bin->getProducerProperty(kClipElement);
            }
            return info;
        }
    }
    return ClipInfo();
}

QString KoraCutMotionMirror::projectOfElement(const QString &elementId) const
{
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        if (it->sync->composition().element(elementId)) {
            return it.key();
        }
    }
    return QString();
}

int KoraCutMotionMirror::clipFor(const QString &project, const QString &elementId) const
{
    // Dans la séquence de la composition, jamais ailleurs : c'est ce qui empêche de poser son clip dans la
    // séquence qui se trouve devant quand son rendu se termine.
    auto tl = m_entries.contains(project) ? timelineFor(project) : m_timeline.lock();
    if (!tl) {
        return -1;
    }
    for (bool audio : {false, true}) {
        const QList<int> tracks = tl->getTracksIds(audio);
        for (int tid : tracks) {
            const auto items = tl->getItemsInRange(tid, 0, -1, false);
            for (int cid : items) {
                if (!tl->isClip(cid)) {
                    continue;
                }
                const ClipInfo info = clipInfoIn(tl, cid);
                if (info.project == project && info.elementId == elementId) {
                    return cid;
                }
            }
        }
    }
    return -1;
}

QString KoraCutMotionMirror::trackName(const Entry &entry, int trackIndex, bool audio) const
{
    if (audio) {
        return i18nc("@item track name, sound of a Motion composition", "Motion · %1 · sound", entry.name);
    }
    auto tl = timelineOf(entry.sync->composition().sequence);
    int existing = 0;
    if (tl) {
        for (int tid : tl->getTracksIds(false)) {
            if (tl->getTrackProperty(tid, kTrackProject).toString() == entry.name) {
                ++existing;
            }
        }
    }
    Q_UNUSED(trackIndex);
    if (existing == 0) {
        return i18nc("@item track name of a Motion composition", "Motion · %1", entry.name);
    }
    return i18nc("@item track name of a Motion composition, second track and more", "Motion · %1 · %2", entry.name, existing + 1);
}

int KoraCutMotionMirror::ensureTrack(const Entry &entry, int trackIndex, bool audio, Fun &undo, Fun &redo)
{
    auto tl = timelineOf(entry.sync->composition().sequence);
    if (!tl) {
        return -1;
    }
    const QList<int> tracks = tl->getTracksIds(audio);
    for (int tid : tracks) {
        if (audio) {
            if (tl->getTrackProperty(tid, kTrackAudio).toString() == entry.name) {
                return tid;
            }
        } else if (tl->getTrackProperty(tid, kTrackProject).toString() == entry.name && tl->getTrackProperty(tid, kTrackIndex).toInt() == trackIndex) {
            return tid;
        }
    }
    int tid = -1;
    // Une piste vidéo se pose au-dessus de tout (au-dessus de la vidéo), une piste audio en dessous.
    if (!tl->requestTrackInsertion(audio ? 0 : -1, tid, trackName(entry, trackIndex, audio), audio, undo, redo)) {
        return -1;
    }
    const QString name = entry.name;
    Fun mark = [tl, tid, name, trackIndex, audio]() {
        if (!tl->isTrack(tid)) {
            return true;
        }
        if (audio) {
            tl->setTrackProperty(tid, kTrackAudio, name);
        } else {
            tl->setTrackProperty(tid, kTrackProject, name);
            tl->setTrackProperty(tid, kTrackIndex, QString::number(trackIndex));
        }
        return true;
    };
    mark();
    // Au refaire, la piste renaît sans ses marques : on les repose.
    Fun previousRedo = redo;
    redo = [previousRedo, mark]() { return previousRedo() && mark(); };
    return tid;
}

QString KoraCutMotionMirror::ensureFolder(Fun &undo, Fun &redo)
{
    auto model = pCore->projectItemModel();
    const QString name = i18nc("@item bin folder for Motion renders", "Motion");
    QString id = model->getFolderIdByName(name);
    if (!id.isEmpty()) {
        return id;
    }
    if (model->requestAddFolder(id, name, model->getRootFolder()->clipId(), undo, redo)) {
        return id;
    }
    return QStringLiteral("-1");
}

bool KoraCutMotionMirror::insertClip(const Entry &entry, const KoraCutMotion::Element &element, const QString &binId, int in, int length, Fun &undo, Fun &redo)
{
    auto tl = timelineOf(entry.sync->composition().sequence);
    if (!tl || length <= 0) {
        return false;
    }
    const bool audio = element.kind == KoraCutMotion::Kind::Audio;
    const int position = frames(element.start, fps());
    const int tid = entry.sync->composition().isElement() ? elementTrack(entry.name, position, length, undo, redo)
                                                          : ensureTrack(entry, audio ? 0 : element.trackIndex, audio, undo, redo);
    if (tid < 0) {
        return false;
    }
    int cid = -1;
    Fun localUndo = []() { return true; };
    Fun localRedo = []() { return true; };
    // « V12/in/out » : la partie vidéo seule du clip 12 du chutier, « A12/… » sa partie audio seule.
    const QString spec = QStringLiteral("%1%2/%3/%4").arg(audio ? QStringLiteral("A") : QStringLiteral("V"), binId).arg(in).arg(in + length - 1);
    if (!tl->requestClipInsertion(spec, tid, position, cid, false, true, false, localUndo, localRedo)) {
        // La place est prise (un autre clip déplacé à la main) : rien n'est posé.
        return false;
    }
    if (!audio) {
        // Posé sans étape d'annulation à lui : le modèle ne prévient pas le rendu d'aperçu, et un morceau
        // déjà rendu resterait affiché sans cette animation (vu le 1er octobre 2026). On le dit ici.
        const int end = position + length;
        const Fun invalidate = [tl, position, end]() {
            Q_EMIT tl->invalidateZone(position, end);
            return true;
        };
        invalidate();
        const Fun insertUndo = localUndo;
        const Fun insertRedo = localRedo;
        localUndo = [insertUndo, invalidate]() {
            const bool ok = insertUndo();
            invalidate();
            return ok;
        };
        localRedo = [insertRedo, invalidate]() {
            const bool ok = insertRedo();
            invalidate();
            return ok;
        };
    }
    merge(undo, redo, localUndo, localRedo);
    return true;
}

void KoraCutMotionMirror::merge(Fun &undo, Fun &redo, const Fun &localUndo, const Fun &localRedo)
{
    const Fun previousUndo = undo;
    const Fun previousRedo = redo;
    undo = [localUndo, previousUndo]() { return localUndo() && previousUndo(); };
    redo = [previousRedo, localRedo]() { return previousRedo() && localRedo(); };
}

// ---------------------------------------------------------------------------------------------
// HTML → timeline

bool KoraCutMotionMirror::applyFromFile(const QString &name, bool logUndo, const QString &undoText, Fun *outUndo, Fun *outRedo)
{
    using namespace KoraCutMotion;
    if (!m_entries.contains(name)) {
        return false;
    }
    if (!timeline()) {
        return false;
    }
    Entry &entry = m_entries[name];
    const Composition comp = entry.sync->composition();
    // Les noms du chutier : un clip qui a encore son nom automatique prend celui d'aujourd'hui (le format
    // de la séquence depuis le 25 septembre 2026, le texte de l'élément s'il a changé) ; un clip resté au
    // nom brut de son fichier de rendu est réparé ici, à l'ouverture. Un nom choisi n'est jamais touché.
    for (const Element &e : comp.elements) {
        if (e.kind == Kind::Audio) {
            continue;
        }
        const QString binId = findBinClip(name, e.id);
        auto bin = binId.isEmpty() ? nullptr : pCore->projectItemModel()->getClipByBinID(binId);
        if (!bin || bin->clipStatus() != FileStatus::StatusReady) {
            continue;
        }
        const QString wanted = e.kind == Kind::Whole ? KoraCutMotionCatalog::binClipName(comp, name) : e.displayName();
        QStringList known{wanted, e.displayName(), e.id};
        if (e.kind == Kind::Whole) {
            // L'ancien nom a pu être posé dans une autre langue (« Élément · Busha » lu en anglais) : on
            // reconnaît aussi « <sorte> · <détail> ».
            const QString old = KoraCutMotionCatalog::readableName(comp, name);
            known << old << name;
            const QString detail = comp.isElement() ? old.section(QStringLiteral(" · "), 1, -1) : QString();
            if (!detail.isEmpty() && bin->name().endsWith(QStringLiteral(" · ") + detail) && bin->getProducerProperty(kClipAutoName).isEmpty()) {
                known << bin->name();
            }
        }
        if (automaticName(bin, known, name)) {
            giveAutomaticName(bin, wanted);
        }
    }
    // La timeline de SA séquence, qu'elle soit devant ou non. Onglet fermé : elle attend sa réouverture.
    auto tl = timelineOf(comp.sequence);
    if (!tl) {
        return false;
    }
    watchModel(tl);
    const double projectFps = fps();
    m_applying = true;
    auto undo = std::make_shared<Fun>([]() { return true; });
    auto redo = std::make_shared<Fun>([]() { return true; });
    bool changed = false;

    // Les pistes d'abord, dans l'ordre des data-track-index : la plus haute au-dessus. Une composition
    // d'un seul élément n'a pas de piste à elle : son clip va sur une piste partagée, choisie à la pose.
    QList<int> indexes;
    bool hasAudio = false;
    const bool single = comp.isElement();
    for (const Element &e : single ? QList<Element>() : comp.elements) {
        if (e.kind == Kind::Audio) {
            hasAudio = true;
        } else if (!indexes.contains(e.trackIndex)) {
            indexes.append(e.trackIndex);
        }
    }
    std::sort(indexes.begin(), indexes.end());
    for (int index : indexes) {
        const int before = tl->getTracksCount();
        ensureTrack(entry, index, false, *undo, *redo);
        changed = changed || tl->getTracksCount() != before;
    }
    if (hasAudio) {
        const int before = tl->getTracksCount();
        ensureTrack(entry, 0, true, *undo, *redo);
        changed = changed || tl->getTracksCount() != before;
    }

    QSet<QString> ids;
    for (const Element &e : comp.elements) {
        ids.insert(e.id);
        if (!e.startResolved) {
            continue;
        }
        const int cid = clipFor(name, e.id);
        if (single && comp.retired && cid < 0) {
            // Son clip a été supprimé de la timeline : la composition reste sur le disque, sans clip.
            continue;
        }
        const int wantedPosition = frames(e.start, projectFps);
        const int wantedLength = e.durationKnown ? frames(e.duration, projectFps) : -1;
        if (cid >= 0) {
            // Le clip existe : il suit la place et la durée écrites dans le fichier.
            const int tid = tl->getClipTrackId(cid);
            const bool audio = e.kind == Kind::Audio;
            // Un élément seul reste sur la piste où il est : c'est la personne qui le range.
            const int wantedTrack = single ? tid : ensureTrack(entry, audio ? 0 : e.trackIndex, audio, *undo, *redo);
            if (wantedLength > 0 && tl->getClipPlaytime(cid) != wantedLength) {
                int size = wantedLength;
                tl->requestItemResize(cid, size, true, true, *undo, *redo);
                changed = true;
            }
            if (tl->getClipPosition(cid) != wantedPosition || (wantedTrack >= 0 && tid != wantedTrack)) {
                if (tl->requestClipMove(cid, wantedTrack >= 0 ? wantedTrack : tid, wantedPosition, true, true, true, true, *undo, *redo) ==
                    TimelineModel::MoveSuccess) {
                    changed = true;
                }
            }
            if (audio) {
                continue;
            }
            const QString binId = findBinClip(name, e.id);
            auto bin = binId.isEmpty() ? nullptr : pCore->projectItemModel()->getClipByBinID(binId);
            if (bin && bin->getProducerProperty(kClipHash) != e.hash) {
                // Le contenu a changé : nouveau rendu en fond, l'ancien reste affiché en attendant.
                requestRender(entry, e);
            }
            continue;
        }
        if (e.kind == Kind::Audio) {
            const QString path = QFileInfo(entry.dir + QLatin1Char('/') + e.src).absoluteFilePath();
            if (!QFile::exists(path)) {
                entry.failed.insert(e.id);
                continue;
            }
            const QString existing = findBinClip(name, e.id);
            if (!existing.isEmpty()) {
                auto bin = pCore->projectItemModel()->getClipByBinID(existing);
                const int in = frames(e.mediaStart, projectFps);
                const int length = wantedLength > 0 ? wantedLength : int(bin->frameDuration()) - in;
                if (insertClip(entry, e, existing, in, length, *undo, *redo)) {
                    entry.placed.insert(e.id);
                    changed = true;
                }
                continue;
            }
            const QString folder = ensureFolder(*undo, *redo);
            const Element element = e;
            const QString project = name;
            const int in = frames(e.mediaStart, projectFps);
            const bool log = logUndo;
            const QString text = undoText;
            ClipCreator::createClipFromFile(path, folder, pCore->projectItemModel(), *undo, *redo, [this, project, element, in, undo, redo, log, text](const QString &binId) {
                if (!m_entries.contains(project)) {
                    return;
                }
                auto bin = pCore->projectItemModel()->getClipByBinID(binId);
                if (!bin) {
                    return;
                }
                bin->setProducerProperty(kClipProject, project);
                bin->setProducerProperty(kClipElement, element.id);
                bin->setProducerProperty(kClipHash, element.hash);
                bin->setProducerProperty(kClipIn, QString::number(in));
                const int length = element.durationKnown ? frames(element.duration, fps()) : int(bin->frameDuration()) - in;
                m_applying = true;
                if (insertClip(m_entries[project], element, binId, in, length, *undo, *redo)) {
                    m_entries[project].placed.insert(element.id);
                }
                m_applying = false;
                if (log) {
                    pCore->pushUndo(*undo, *redo, text.isEmpty() ? i18nc("@action undo text", "Motion: update %1", project) : text);
                }
                Q_EMIT elementsChanged(project);
            });
            continue;
        }
        // Un élément visuel sans clip : le rendu existe déjà (réouverture, annulation) ou il se lance.
        const QString existing = findBinClip(name, e.id);
        auto bin = existing.isEmpty() ? nullptr : pCore->projectItemModel()->getClipByBinID(existing);
        if (bin && bin->getProducerProperty(kClipHash) == e.hash && bin->clipStatus() == FileStatus::StatusReady) {
            const int in = entryPoint(e, projectFps);
            const int length = wantedLength > 0 ? wantedLength : int(bin->frameDuration()) - in;
            if (insertClip(entry, e, existing, in, length, *undo, *redo)) {
                entry.placed.insert(e.id);
                changed = true;
            }
        } else {
            requestRender(entry, e);
        }
    }

    // Les éléments retirés du fichier : leurs clips partent.
    for (bool audio : {false, true}) {
        const QList<int> tracks = tl->getTracksIds(audio);
        for (int tid : tracks) {
            const auto items = tl->getItemsInRange(tid, 0, -1, false);
            for (int cid : items) {
                const ClipInfo info = clipInfoIn(tl, cid);
                if (info.project == name && !ids.contains(info.elementId)) {
                    const int from = tl->getClipPosition(cid);
                    const int to = from + tl->getClipPlaytime(cid);
                    tl->requestItemDeletion(cid, *undo, *redo, false);
                    if (!audio) {
                        // Même raison qu'à la pose : sans cela, le rendu d'aperçu garderait l'animation retirée.
                        const Fun invalidate = [tl, from, to]() {
                            Q_EMIT tl->invalidateZone(from, to);
                            return true;
                        };
                        invalidate();
                        merge(*undo, *redo, invalidate, invalidate);
                    }
                    entry.placed.remove(info.elementId);
                    changed = true;
                }
            }
        }
    }
    // Ce qui a un clip maintenant est « posé » : sa disparition à la main sera un vrai retrait.
    for (const Element &e : comp.elements) {
        if (clipFor(name, e.id) >= 0) {
            entry.placed.insert(e.id);
        }
    }
    m_applying = false;
    if (outUndo && outRedo) {
        merge(*outUndo, *outRedo, *undo, *redo);
    } else if (changed && logUndo) {
        pCore->pushUndo(*undo, *redo, undoText.isEmpty() ? i18nc("@action undo text", "Motion: update %1", name) : undoText);
    }
    Q_EMIT elementsChanged(name);
    return changed;
}

void KoraCutMotionMirror::onRendered(const QString &project, const QString &elementId, const QString &hash, const QString &path)
{
    using namespace KoraCutMotion;
    if (!m_entries.contains(project)) {
        return;
    }
    if (!timeline()) {
        return;
    }
    Entry &entry = m_entries[project];
    entry.progress.remove(elementId);
    const Element *e = entry.sync->composition().element(elementId);
    if (!e || e->hash != hash) {
        // Un rendu d'une version déjà dépassée : il reste en cache, il ne pose rien.
        Q_EMIT elementsChanged(project);
        return;
    }
    const Element element = *e;
    // Le nom montré dans le chutier et la timeline : la composition entière porte le nom du projet
    // Motion, un élément son texte, son fichier ou son id.
    const QString shown = element.kind == Kind::Whole ? KoraCutMotionCatalog::binClipName(entry.sync->composition(), project) : element.displayName();
    const bool retired = entry.sync->composition().isElement() && entry.sync->composition().retired;
    const QString existing = findBinClip(project, elementId);
    auto existingBin = existing.isEmpty() ? nullptr : pCore->projectItemModel()->getClipByBinID(existing);
    if (existingBin && (existingBin->clipStatus() == FileStatus::StatusWaiting ||
                        pCore->taskManager.hasPendingJob(ObjectId(KdenliveObjectType::BinClip, existing.toInt(), QUuid()), AbstractTask::LOADJOB))) {
        // Le clip du chutier est encore en train de charger le rendu précédent : échanger son média
        // maintenant bloquerait l'application (discardJobs attend une tâche qui attend le fil principal).
        // On repasse dans un instant.
        QTimer::singleShot(300, this, [this, project, elementId, hash, path]() { onRendered(project, elementId, hash, path); });
        return;
    }
    if (existingBin) {
        if (existingBin->getProducerProperty(kClipHash) != hash || existingBin->url() != path) {
            // Le clip du chutier existe : on échange son média, le clip de la timeline ne bouge pas
            // (même id, même place, même durée) et l'annulation n'a rien à enregistrer : le média
            // découle du fichier, que l'annulation du geste ramènera à son tour. L'ancien rendu est
            // resté visible jusqu'ici.
            existingBin->setProducerProperty(kClipHash, hash);
            // Le nom : celui du rendu s'il est encore automatique, sinon celui que la personne ou un
            // agent a choisi (organiser_rushs, renommer_media), gardé tel quel.
            const bool automatic = automaticName(existingBin, {shown, element.displayName(), element.id}, project);
            const QString keptName = automatic ? shown : existingBin->name();
            existingBin->setName(keptName);
            QMap<QString, QString> props;
            props.insert(QStringLiteral("resource"), path);
            props.insert(QStringLiteral("kdenlive:originalurl"), path);
            props.insert(QStringLiteral("kdenlive:clipname"), keptName);
            if (automatic) {
                props.insert(QString(kClipAutoName), shown);
            }
            props.insert(QStringLiteral("_fullreload"), QStringLiteral("1"));
            existingBin->setProperties(props);
            existingBin->updateTimelineClips({TimelineModel::NameRole});
            scheduleThumbnail(existing);
        }
        if (clipFor(project, elementId) >= 0) {
            // Un élément seul rallongé attendait son nouveau rendu pour que son clip prenne toute sa durée.
            if (entry.sync->composition().isElement()) {
                QTimer::singleShot(600, this, [this, project]() { applyFromFile(project, false); });
            }
            Q_EMIT elementsChanged(project);
            return; // déjà posé
        }
        if (!timelineFor(project)) {
            // Sa séquence n'est pas ouverte : le média est à jour dans le chutier, le clip sera posé à la
            // réouverture de la séquence (setTimeline), jamais dans la séquence qui se trouve devant.
            Q_EMIT elementsChanged(project);
            return;
        }
    }
    auto undo = std::make_shared<Fun>([]() { return true; });
    auto redo = std::make_shared<Fun>([]() { return true; });
    const QString folder = ensureFolder(*undo, *redo);
    const QString name = project;
    auto place = [this, name, element, hash, shown, retired, undo, redo](const QString &binId) {
        if (!m_entries.contains(name)) {
            return;
        }
        auto bin = pCore->projectItemModel()->getClipByBinID(binId);
        if (!timeline() || !bin) {
            return;
        }
        // La timeline de la séquence de l'élément, relue ici : la séquence active a pu changer pendant le
        // rendu ou pendant le chargement du clip (défaut du 1er octobre 2026). Nulle (onglet fermé) : le
        // clip du chutier est prêt, la pose attend la réouverture.
        auto tl = timelineFor(name);
        if (tl) {
            watchModel(tl);
        }
        bin->setProducerProperty(kClipProject, name);
        bin->setProducerProperty(kClipElement, element.id);
        bin->setProducerProperty(kClipHash, hash);
        const int in = entryPoint(element, fps());
        bin->setProducerProperty(kClipIn, QString::number(in));
        bin->setProducerProperty(QStringLiteral("kdenlive:clipname"), shown);
        bin->setProducerProperty(kClipAutoName, shown);
        bin->setName(shown);
        bin->updateTimelineClips({TimelineModel::NameRole});
        m_applying = true;
        const int length = element.durationKnown ? frames(element.duration, fps()) : int(bin->frameDuration()) - in;
        const bool ok = tl && !retired && clipFor(name, element.id) < 0 && insertClip(m_entries[name], element, binId, in, length, *undo, *redo);
        m_applying = false;
        if (ok) {
            m_entries[name].placed.insert(element.id);
            scheduleThumbnail(binId);
            // Un élément seul (lot 5) : l'étape est son ajout, que la personne défait d'un Annuler.
            pCore->pushUndo(*undo, *redo, m_entries[name].sync->composition().isElement() ? i18nc("@action undo text", "Motion: add %1", shown)
                                                                                        : i18nc("@action undo text", "Motion: render %1", shown));
        }
        Q_EMIT elementsChanged(name);
    };
    if (existingBin) {
        // Le média vient d'être échangé : on pose le clip dès que le chutier l'a rechargé.
        QTimer::singleShot(400, this, [place, existing]() { place(existing); });
        return;
    }
    ClipCreator::createClipFromFile(path, folder, pCore->projectItemModel(), *undo, *redo, place);
}

void KoraCutMotionMirror::onRenderFailed(const QString &project, const QString &elementId, const QString &error)
{
    if (!m_entries.contains(project)) {
        return;
    }
    m_entries[project].failed.insert(elementId);
    m_entries[project].progress.remove(elementId);
    Q_EMIT message(i18n("Motion: the render of %1 failed. %2", elementId, error));
    Q_EMIT elementsChanged(project);
}

// ---------------------------------------------------------------------------------------------
// timeline → HTML

void KoraCutMotionMirror::scheduleReconcile(const QString &sequence)
{
    if (m_applying || m_entries.isEmpty()) {
        return;
    }
    m_dirty.insert(sequence);
    m_reconcile->start();
}

void KoraCutMotionMirror::reconcileTimeline()
{
    if (m_applying) {
        return;
    }
    const QSet<QString> sequences = m_dirty;
    m_dirty.clear();
    for (const QString &sequence : sequences) {
        reconcileSequence(sequence);
    }
}

void KoraCutMotionMirror::reconcileSequence(const QString &sequence)
{
    using namespace KoraCutMotion;
    if (m_applying) {
        return;
    }
    m_dirty.remove(sequence);
    auto tl = timelineOf(sequence);
    if (!tl) {
        return;
    }
    const double projectFps = fps();
    QStringList rewritten = reconcileRemovals(tl, sequence);
    for (const QString &name : retimeTransitions(tl, sequence)) {
        if (!rewritten.contains(name)) {
            rewritten << name;
        }
    }
    for (bool audio : {false, true}) {
        const QList<int> tracks = tl->getTracksIds(audio);
        for (int tid : tracks) {
            const auto items = tl->getItemsInRange(tid, 0, -1, false);
            for (int cid : items) {
                const ClipInfo info = clipInfoIn(tl, cid);
                if (info.project.isEmpty() || !m_entries.contains(info.project) || resolved(m_entries.value(info.project).sync->composition().sequence) != sequence) {
                    continue;
                }
                Entry &entry = m_entries[info.project];
                const Element *e = entry.sync->composition().element(info.elementId);
                if (!e || !e->startResolved) {
                    continue;
                }
                QMap<QString, QString> attributes;
                const int position = tl->getClipPosition(cid);
                const int length = tl->getClipPlaytime(cid);
                const bool single = entry.sync->composition().isElement();
                if (position != frames(e->start, projectFps)) {
                    // Une composition entière garde data-start="0" pour HyperFrames : sa place dans la
                    // timeline est un attribut de KoraCut, que l'empreinte ignore.
                    attributes.insert(e->kind == Kind::Whole ? QStringLiteral("data-koracut-start") : QStringLiteral("data-start"), secondsText(position / projectFps));
                }
                if ((e->kind != Kind::Whole || single) && (!e->durationKnown || length != frames(e->duration, projectFps))) {
                    // Un élément seul rallongé ou raccourci : sa durée change, son animation est rendue à nouveau.
                    attributes.insert(QStringLiteral("data-duration"), secondsText(length / projectFps));
                }
                if (e->kind == Kind::Audio) {
                    const int in = tl->getClipIn(cid);
                    if (in != frames(e->mediaStart, projectFps)) {
                        attributes.insert(QStringLiteral("data-media-start"), secondsText(in / projectFps));
                    }
                } else if (e->kind == Kind::Block || e->kind == Kind::Image || e->kind == Kind::Composition) {
                    // Rogné par la gauche : le point d'entrée dans le rendu, attribut propre à KoraCut.
                    const int in = tl->getClipIn(cid);
                    if (in != frames(e->mediaStart, projectFps)) {
                        attributes.insert(QStringLiteral("data-koracut-in"), in > 0 ? secondsText(in / projectFps) : QString());
                    }
                }
                if (!audio && e->kind != Kind::Whole) {
                    const int trackIndex = tl->getTrackProperty(tid, kTrackIndex).toInt();
                    if (tl->getTrackProperty(tid, kTrackProject).toString() == info.project && trackIndex != e->trackIndex) {
                        attributes.insert(QStringLiteral("data-track-index"), QString::number(trackIndex));
                    }
                }
                if (attributes.isEmpty()) {
                    continue;
                }
                if (entry.sync->writeElement(info.elementId, attributes)) {
                    if (!rewritten.contains(info.project)) {
                        rewritten << info.project;
                    }
                }
            }
        }
    }
    // Ce qu'on vient d'écrire peut déplacer d'autres éléments (débuts relatifs) : la timeline suit.
    for (const QString &name : std::as_const(rewritten)) {
        applyFromFile(name, false);
    }
}

QStringList KoraCutMotionMirror::reconcileRemovals(const std::shared_ptr<TimelineItemModel> &tl, const QString &sequence)
{
    using namespace KoraCutMotion;
    QStringList rewritten;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry &entry = it.value();
        const QString name = it.key();
        const Composition &single = entry.sync->composition();
        if (resolved(single.sequence) != sequence) {
            continue;
        }
        if (single.isElement() && !single.elements.isEmpty()) {
            // Une composition d'un seul élément : son clip supprimé la marque « retirée » (le fichier reste, rien
            // ne la repose seule) ; son clip revenu (Annuler) la remet. Une seule étape, celle de Kdenlive.
            const QString id = single.elements.first().id;
            const bool hasClip = clipFor(name, id) >= 0;
            QString wanted;
            if (hasClip && single.retired) {
                wanted = QStringLiteral("");
            } else if (!hasClip && !single.retired && entry.placed.contains(id)) {
                wanted = QStringLiteral("1");
                entry.placed.remove(id);
            }
            if (!wanted.isNull() && entry.sync->writeElement(id, {{QStringLiteral("data-koracut-retire"), wanted}})) {
                if (hasClip) {
                    entry.placed.insert(id);
                }
                rewritten << name;
            }
            continue;
        }
        // 1. Un clip revenu (Annuler d'une suppression) dont l'élément avait quitté le HTML : on le remet.
        for (bool audio : {false, true}) {
            for (int tid : tl->getTracksIds(audio)) {
                for (int cid : tl->getItemsInRange(tid, 0, -1, false)) {
                    const ClipInfo info = clipInfoIn(tl, cid);
                    if (info.project != name || !entry.removed.contains(info.elementId) || entry.sync->composition().element(info.elementId)) {
                        continue;
                    }
                    const RemovedElement removed = entry.removed.take(info.elementId);
                    if (entry.sync->writeAllSilently(insertFragment(entry.sync->html(), removed.offset, removed.fragment))) {
                        entry.placed.insert(info.elementId);
                        if (!rewritten.contains(name)) {
                            rewritten << name;
                        }
                    }
                }
            }
        }
        // 2. Un élément posé dont le clip a disparu à la main : il quitte le HTML (une composition entière
        //    reste : son clip reviendra à la prochaine relecture).
        bool again = true;
        while (again) {
            again = false;
            const Composition comp = entry.sync->composition();
            for (const Element &e : comp.elements) {
                if (e.kind == Kind::Whole || !entry.placed.contains(e.id) || clipFor(name, e.id) >= 0) {
                    continue;
                }
                RemovedElement removed;
                removed.fragment = entry.sync->html().mid(e.outerStart, e.outerEnd - e.outerStart) + QLatin1Char('\n');
                const QString html = removeElement(entry.sync->html(), e, &removed.offset);
                entry.placed.remove(e.id);
                if (html != entry.sync->html() && entry.sync->writeAllSilently(html)) {
                    entry.removed.insert(e.id, removed);
                    if (!rewritten.contains(name)) {
                        rewritten << name;
                    }
                    again = true;
                    break;
                }
            }
        }
    }
    return rewritten;
}

// ---------------------------------------------------------------------------------------------
// Les gestes de KoraCut

bool KoraCutMotionMirror::setElementText(const QString &project, const QString &id, const QString &text)
{
    if (!m_entries.contains(project)) {
        return false;
    }
    Entry &entry = m_entries[project];
    const KoraCutMotion::Element *e = entry.sync->composition().element(id);
    if (!e) {
        return false;
    }
    const QString before = e->text;
    if (before == text) {
        return true;
    }
    if (!entry.sync->writeElement(id, {}, &text)) {
        return false;
    }
    applyFromFile(project, false);
    KoraCutMotionSync *sync = entry.sync;
    Fun undo = [this, sync, project, id, before]() {
        if (sync->writeElement(id, {}, &before)) {
            applyFromFile(project, false);
        }
        return true;
    };
    Fun redo = [this, sync, project, id, text]() {
        if (sync->writeElement(id, {}, &text)) {
            applyFromFile(project, false);
        }
        return true;
    };
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Motion: change text of %1", id));
    return true;
}

bool KoraCutMotionMirror::setElementAttribute(const QString &project, const QString &id, const QString &attribute, const QString &value)
{
    if (!m_entries.contains(project)) {
        return false;
    }
    Entry &entry = m_entries[project];
    const KoraCutMotion::Element *e = entry.sync->composition().element(id);
    if (!e) {
        return false;
    }
    const QList<KoraCutMotion::Tag> tags = KoraCutMotion::tokenize(entry.sync->html());
    const QString before = e->tagIndex >= 0 && e->tagIndex < tags.size() ? tags.at(e->tagIndex).attr(attribute) : QString();
    if (before == value) {
        return true;
    }
    if (!entry.sync->writeElement(id, {{attribute, value}})) {
        return false;
    }
    applyFromFile(project, false);
    KoraCutMotionSync *sync = entry.sync;
    Fun undo = [this, sync, project, id, attribute, before]() {
        if (sync->writeElement(id, {{attribute, before}})) {
            applyFromFile(project, false);
        }
        return true;
    };
    Fun redo = [this, sync, project, id, attribute, value]() {
        if (sync->writeElement(id, {{attribute, value}})) {
            applyFromFile(project, false);
        }
        return true;
    };
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Motion: change %1 of %2", attribute, id));
    return true;
}

bool KoraCutMotionMirror::moveElement(const QString &project, const QString &id, double seconds)
{
    auto tl = timelineFor(project);
    const int cid = clipFor(project, id);
    if (!tl || cid < 0) {
        return false;
    }
    // Le vrai geste : le clip bouge dans la timeline (une étape d'annulation), le fichier suit.
    const bool ok = tl->requestClipMove(cid, tl->getClipTrackId(cid), frames(seconds, fps()), true, true, true);
    if (ok) {
        reconcileSequence(sequenceOf(project));
    }
    return ok;
}

bool KoraCutMotionMirror::replaceHtml(const QString &project, const QString &html, const QString &undoText)
{
    if (!m_entries.contains(project)) {
        return false;
    }
    Entry &entry = m_entries[project];
    const QString before = entry.sync->html();
    if (before == html) {
        return true;
    }
    if (!entry.sync->writeAllSilently(html)) {
        return false;
    }
    entry.removed.clear();
    applyFromFile(project, false);
    KoraCutMotionSync *sync = entry.sync;
    Fun undo = [this, sync, project, before]() {
        if (sync->writeAllSilently(before)) {
            applyFromFile(project, false);
        }
        return true;
    };
    Fun redo = [this, sync, project, html]() {
        if (sync->writeAllSilently(html)) {
            applyFromFile(project, false);
        }
        return true;
    };
    pCore->pushUndo(undo, redo, undoText.isEmpty() ? i18nc("@action undo text", "Motion: rewrite %1", project) : undoText);
    return true;
}

bool KoraCutMotionMirror::setElement(const QString &project, const QString &id, const QMap<QString, QString> &attributes, const QString *text,
                                     const QString &undoText)
{
    if (!m_entries.contains(project)) {
        return false;
    }
    Entry &entry = m_entries[project];
    const KoraCutMotion::Element *e = entry.sync->composition().element(id);
    if (!e) {
        return false;
    }
    const QList<KoraCutMotion::Tag> tags = KoraCutMotion::tokenize(entry.sync->html());
    QMap<QString, QString> before;
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        before.insert(it.key(), e->tagIndex >= 0 && e->tagIndex < tags.size() ? tags.at(e->tagIndex).attr(it.key()) : QString());
    }
    const bool withText = text != nullptr;
    const QString beforeText = e->text;
    const QString afterText = withText ? *text : QString();
    if (before == attributes && (!withText || beforeText == afterText)) {
        return true;
    }
    if (!entry.sync->writeElement(id, attributes, withText ? &afterText : nullptr)) {
        return false;
    }
    applyFromFile(project, false);
    KoraCutMotionSync *sync = entry.sync;
    Fun undo = [this, sync, project, id, before, withText, beforeText]() {
        if (sync->writeElement(id, before, withText ? &beforeText : nullptr)) {
            applyFromFile(project, false);
        }
        return true;
    };
    Fun redo = [this, sync, project, id, attributes, withText, afterText]() {
        if (sync->writeElement(id, attributes, withText ? &afterText : nullptr)) {
            applyFromFile(project, false);
        }
        return true;
    };
    pCore->pushUndo(undo, redo, undoText.isEmpty() ? i18nc("@action undo text", "Motion: change %1", id) : undoText);
    return true;
}

bool KoraCutMotionMirror::place(const QString &project, int trackId, double atSeconds, QString *error)
{
    using namespace KoraCutMotion;
    rescan();
    if (!timeline() || !m_entries.contains(project)) {
        if (error) {
            *error = QStringLiteral("projet_inconnu");
        }
        return false;
    }
    auto tl = timelineFor(project);
    if (!tl) {
        if (error) {
            *error = QStringLiteral("sequence_fermee");
        }
        return false;
    }
    Entry &entry = m_entries[project];
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool changed = false;
    const bool single = entry.sync->composition().isElement();
    if (single && trackId >= 0) {
        // Un élément seul : sa piste devient celle demandée, son clip y passe s'il est posé ailleurs.
        if (!tl->isTrack(trackId) || tl->isAudioTrack(trackId) || tl->trackIsLocked(trackId)) {
            if (error) {
                *error = QStringLiteral("mauvais_type_de_piste");
            }
            return false;
        }
        entry.homeTrack = trackId;
        const KoraCutMotion::Composition &comp = entry.sync->composition();
        const int cid = comp.elements.isEmpty() ? -1 : clipFor(project, comp.elements.first().id);
        if (cid >= 0 && tl->getClipTrackId(cid) != trackId) {
            m_applying = true;
            const bool moved = tl->requestClipMove(cid, trackId, tl->getClipPosition(cid), true, true, true, true, undo, redo) == TimelineModel::MoveSuccess;
            m_applying = false;
            if (!moved) {
                if (error) {
                    *error = QStringLiteral("place_occupee");
                }
                return false;
            }
            changed = true;
        }
        trackId = -1;
    }
    if (trackId >= 0) {
        if (!tl->isTrack(trackId) || tl->isAudioTrack(trackId) || tl->trackIsLocked(trackId)) {
            if (error) {
                *error = QStringLiteral("mauvais_type_de_piste");
            }
            return false;
        }
        // La piste demandée devient la première piste Motion du projet, si elle n'en est pas déjà une.
        int lowest = 0;
        bool first = true;
        for (const Element &e : entry.sync->composition().elements) {
            if (e.kind != Kind::Audio && (first || e.trackIndex < lowest)) {
                lowest = e.trackIndex;
                first = false;
            }
        }
        const QString current = tl->getTrackProperty(trackId, kTrackProject).toString();
        if (current.isEmpty()) {
            const QString beforeName = tl->getTrackProperty(trackId, QStringLiteral("kdenlive:track_name")).toString();
            const QString name = project;
            Fun mark = [tl, trackId, name, lowest]() {
                if (tl->isTrack(trackId)) {
                    tl->setTrackProperty(trackId, kTrackProject, name);
                    tl->setTrackProperty(trackId, kTrackIndex, QString::number(lowest));
                }
                return true;
            };
            Fun unmark = [tl, trackId]() {
                if (tl->isTrack(trackId)) {
                    tl->setTrackProperty(trackId, kTrackProject, QString());
                    tl->setTrackProperty(trackId, kTrackIndex, QString());
                }
                return true;
            };
            mark();
            merge(undo, redo, unmark, mark);
            changed = true;
        } else if (current != project) {
            if (error) {
                *error = QStringLiteral("place_occupee");
            }
            return false;
        }
    }
    changed = applyFromFile(project, false, QString(), &undo, &redo) || changed;
    if (atSeconds >= 0) {
        // Les éléments à début absolu bougent d'un même pas ; les débuts relatifs suivent par le fichier.
        const double projectFps = fps();
        QList<std::pair<int, int>> moves; // clip, nouvelle position
        int earliest = -1;
        for (const Element &e : entry.sync->composition().elements) {
            bool numeric = false;
            e.startExpr.trimmed().toDouble(&numeric);
            const int cid = clipFor(project, e.id);
            if (!numeric || cid < 0) {
                continue;
            }
            const int pos = tl->getClipPosition(cid);
            earliest = earliest < 0 ? pos : std::min(earliest, pos);
            moves << std::make_pair(cid, pos);
        }
        const int delta = earliest < 0 ? 0 : frames(atSeconds, projectFps) - earliest;
        if (delta != 0) {
            std::sort(moves.begin(), moves.end(), [delta](const auto &a, const auto &b) { return delta > 0 ? a.second > b.second : a.second < b.second; });
            m_applying = true;
            for (const auto &[cid, pos] : std::as_const(moves)) {
                if (tl->requestClipMove(cid, tl->getClipTrackId(cid), pos + delta, true, true, true, true, undo, redo) == TimelineModel::MoveSuccess) {
                    changed = true;
                }
            }
            m_applying = false;
            reconcileSequence(sequenceOf(project));
        }
    }
    if (changed) {
        pCore->pushUndo(undo, redo, i18nc("@action undo text", "Motion: place %1", project));
    }
    Q_EMIT elementsChanged(project);
    return changed;
}

// ---------------------------------------------------------------------------------------------
// Lot 5 : un élément = une composition, posée comme un seul clip ; les transitions suivent leur coupe

void KoraCutMotionMirror::setElementPlacement(const QString &project, int trackId, int clipBefore, int clipAfter)
{
    if (!m_entries.contains(project)) {
        return;
    }
    Entry &entry = m_entries[project];
    entry.homeTrack = trackId;
    entry.cutBefore = clipBefore;
    entry.cutAfter = clipAfter;
}

int KoraCutMotionMirror::waitForClip(const QString &project, const QString &elementId, int ms)
{
    QElapsedTimer timer;
    timer.start();
    int cid = clipFor(project, elementId);
    while (cid < 0 && timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        cid = clipFor(project, elementId);
    }
    return cid;
}

int KoraCutMotionMirror::animationsTrack() const
{
    auto tl = m_timeline.lock();
    if (!tl) {
        return -1;
    }
    for (int tid : tl->getTracksIds(false)) {
        if (tl->getTrackProperty(tid, kTrackProject).toString() == kSharedAnimations) {
            return tid;
        }
    }
    return -1;
}

void KoraCutMotionMirror::announceOpened(const QString &project, const QString &elementId)
{
    m_lastOpened = project;
    Q_EMIT elementOpened(project, elementId);
}

int KoraCutMotionMirror::elementTrack(const QString &project, int position, int length, Fun &undo, Fun &redo)
{
    auto tl = timelineFor(project);
    if (!tl || !m_entries.contains(project)) {
        return -1;
    }
    Entry &entry = m_entries[project];
    auto isFree = [tl, position, length](int tid) {
        if (!tl->isTrack(tid) || tl->isAudioTrack(tid) || tl->trackIsLocked(tid)) {
            return false;
        }
        for (int id : tl->getItemsInRange(tid, position, position + std::max(1, length) - 1, false)) {
            if (tl->isClip(id)) {
                return false;
            }
        }
        return true;
    };
    if (entry.homeTrack >= 0 && isFree(entry.homeTrack)) {
        return entry.homeTrack;
    }
    // Les pistes partagées : « Transitions » tout en haut, « Animations » juste en dessous, pour qu'un
    // élément qui doit s'accorder à une transition soit sous elle (demande d'Amadou Ba, 24 septembre 2026).
    const bool transition = entry.sync->composition().isTransition();
    const QString purpose = transition ? QString(kSharedTransitions) : QString(kSharedAnimations);
    int count = 0;
    int lowestTransition = -1;
    const QList<int> videoTracks = tl->getTracksIds(false);
    for (int tid : videoTracks) {
        const QString mark = tl->getTrackProperty(tid, kTrackProject).toString();
        if (mark == kSharedTransitions && (lowestTransition < 0 || tl->getTrackPosition(tid) < tl->getTrackPosition(lowestTransition))) {
            lowestTransition = tid;
        }
        if (mark != purpose) {
            continue;
        }
        ++count;
        if (isFree(tid)) {
            entry.homeTrack = tid;
            return tid;
        }
    }
    QString name = transition ? i18nc("@item track name, shared track of Motion transitions", "Transitions")
                              : i18nc("@item track name, shared track of Motion animated elements", "Animations");
    if (count > 0) {
        name = i18nc("@item track name, second shared Motion track and more: name number", "%1 %2", name, count + 1);
    }
    // Un projet a déjà ses pistes (dix pour tout projet, document 17) : une piste vidéo vide et sans nom,
    // juste au-dessus des plans (et sous les transitions pour les animations), est adoptée plutôt que d'en
    // ajouter une tout en haut, loin des plans (vu à l'essai grandeur nature du 24 septembre 2026).
    // La piste vidéo la plus basse (V1, les plans) n'est jamais adoptée, même vide.
    int contentTop = -1;
    for (int tid : videoTracks) {
        contentTop = contentTop < 0 ? tl->getTrackPosition(tid) : std::min(contentTop, tl->getTrackPosition(tid));
    }
    int animationsTop = -1;
    for (int tid : videoTracks) {
        const QString mark = tl->getTrackProperty(tid, kTrackProject).toString();
        const int pos = tl->getTrackPosition(tid);
        if (mark == kSharedAnimations) {
            animationsTop = std::max(animationsTop, pos);
        } else if (mark.isEmpty()) {
            for (int id : tl->getItemsInRange(tid, 0, -1, false)) {
                if (tl->isClip(id)) {
                    contentTop = std::max(contentTop, pos);
                    break;
                }
            }
        }
    }
    const int floorPosition = transition ? std::max(contentTop, animationsTop) : contentTop;
    const int ceilingPosition = (!transition && lowestTransition >= 0) ? tl->getTrackPosition(lowestTransition) : std::numeric_limits<int>::max();
    int adopted = -1;
    // Une piste que la personne (ou le plan du projet) a déjà nommée « Animations » ou « Transitions », libre à
    // cet endroit, est prise telle quelle, où qu'elle soit : c'est sa place voulue (montage Tek Tech, 24 septembre 2026,
    // où `projet.nouveau` avait créé ces pistes par leur nom et où l'élément partait sur une piste en plus).
    const QString englishName = transition ? QStringLiteral("Transitions") : QStringLiteral("Animations");
    for (int tid : videoTracks) {
        const QString trackName = tl->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString().trimmed();
        if (!tl->getTrackProperty(tid, kTrackProject).toString().isEmpty() || !isFree(tid)) {
            continue;
        }
        if (trackName.compare(name, Qt::CaseInsensitive) == 0 || trackName.compare(englishName, Qt::CaseInsensitive) == 0) {
            if (adopted < 0 || tl->getTrackPosition(tid) < tl->getTrackPosition(adopted)) {
                adopted = tid;
            }
        }
    }
    for (int tid : videoTracks) {
        if (adopted >= 0) {
            break;
        }
        const int pos = tl->getTrackPosition(tid);
        if (pos <= floorPosition || pos >= ceilingPosition || tl->trackIsLocked(tid) || !tl->getTrackProperty(tid, kTrackProject).toString().isEmpty() ||
            !tl->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString().isEmpty() || !tl->getItemsInRange(tid, 0, -1, true).empty()) {
            continue;
        }
        if (adopted < 0 || pos < tl->getTrackPosition(adopted)) {
            adopted = tid;
        }
    }
    if (adopted >= 0) {
        Fun adopt = [tl, adopted, purpose, name]() {
            if (tl->isTrack(adopted)) {
                tl->setTrackProperty(adopted, kTrackProject, purpose);
                tl->setTrackProperty(adopted, QStringLiteral("kdenlive:track_name"), name);
            }
            return true;
        };
        const QString previousName = tl->getTrackProperty(adopted, QStringLiteral("kdenlive:track_name")).toString();
        Fun release = [tl, adopted, previousName]() {
            if (tl->isTrack(adopted)) {
                tl->setTrackProperty(adopted, kTrackProject, QString());
                tl->setTrackProperty(adopted, QStringLiteral("kdenlive:track_name"), previousName);
            }
            return true;
        };
        adopt();
        merge(undo, redo, release, adopt);
        entry.homeTrack = adopted;
        return adopted;
    }
    const int insertAt = (!transition && lowestTransition >= 0) ? tl->getTrackPosition(lowestTransition) : -1;
    int tid = -1;
    if (!tl->requestTrackInsertion(insertAt, tid, name, false, undo, redo)) {
        return -1;
    }
    Fun mark = [tl, tid, purpose]() {
        if (tl->isTrack(tid)) {
            tl->setTrackProperty(tid, kTrackProject, purpose);
        }
        return true;
    };
    mark();
    Fun previousRedo = redo;
    redo = [previousRedo, mark]() { return previousRedo() && mark(); };
    entry.homeTrack = tid;
    return tid;
}

bool KoraCutMotionMirror::findCutNear(const std::shared_ptr<TimelineItemModel> &tl, int aboveTrack, int center, int radius, int &before, int &after, int &cut) const
{
    int best = -1;
    const int limit = aboveTrack >= 0 && tl->isTrack(aboveTrack) ? tl->getTrackPosition(aboveTrack) : std::numeric_limits<int>::max();
    for (int tid : tl->getTracksIds(false)) {
        if (tid == aboveTrack || tl->getTrackPosition(tid) >= limit) {
            continue;
        }
        // Les pistes Motion (et les pistes partagées) ne portent pas de coupe à suivre.
        if (!tl->getTrackProperty(tid, kTrackProject).toString().isEmpty()) {
            continue;
        }
        for (int cid : tl->getItemsInRange(tid, std::max(0, center - radius - 1), center + radius + 1, false)) {
            if (!tl->isClip(cid)) {
                continue;
            }
            const int end = tl->getClipPosition(cid) + tl->getClipPlaytime(cid);
            if (std::abs(end - center) > radius) {
                continue;
            }
            const int next = tl->getClipByPosition(tid, end);
            if (next < 0 || next == cid || tl->getClipPosition(next) != end) {
                continue;
            }
            if (best < 0 || std::abs(end - center) < std::abs(cut - center)) {
                best = end;
                before = cid;
                after = next;
                cut = end;
            }
        }
    }
    return best >= 0;
}

QList<int> KoraCutMotionMirror::followersOf(const std::shared_ptr<TimelineItemModel> &tl, const QString &transition, int transitionClip) const
{
    QList<int> out;
    if (transitionClip < 0 || !tl->isClip(transitionClip)) {
        return out;
    }
    const int tid = tl->getClipTrackId(transitionClip);
    const int start = tl->getClipPosition(transitionClip);
    const int end = start + tl->getClipPlaytime(transitionClip);
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        const KoraCutMotion::Composition &comp = it.value().sync->composition();
        if (it.key() == transition || !comp.isElement() || comp.isTransition() || comp.retired || comp.elements.isEmpty() ||
            sequenceOf(it.key()) != sequenceOf(transition)) {
            continue;
        }
        const int cid = clipFor(it.key(), comp.elements.first().id);
        if (cid < 0 || !tl->isClip(cid) || tl->getClipTrackId(cid) == tid || tl->isAudioTrack(tl->getClipTrackId(cid))) {
            continue;
        }
        // Sous la transition : une piste plus basse, et le clip croise sa durée.
        if (tl->getTrackPosition(tl->getClipTrackId(cid)) >= tl->getTrackPosition(tid)) {
            continue;
        }
        const int from = tl->getClipPosition(cid);
        if (from < end && from + tl->getClipPlaytime(cid) > start) {
            out << cid;
        }
    }
    return out;
}

QString KoraCutMotionMirror::followedTransition(const QString &name) const
{
    auto tl = timelineFor(name);
    if (!tl || !m_entries.contains(name)) {
        return QString();
    }
    const KoraCutMotion::Composition &comp = m_entries.value(name).sync->composition();
    if (!comp.isElement() || comp.isTransition() || comp.elements.isEmpty()) {
        return QString();
    }
    const int cid = clipFor(name, comp.elements.first().id);
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        const KoraCutMotion::Composition &other = it.value().sync->composition();
        if (!other.isTransition() || other.cut < 0 || other.elements.isEmpty() || sequenceOf(it.key()) != sequenceOf(name)) {
            continue;
        }
        if (followersOf(tl, it.key(), clipFor(it.key(), other.elements.first().id)).contains(cid)) {
            return it.key();
        }
    }
    return QString();
}

QStringList KoraCutMotionMirror::retimeTransitions(const std::shared_ptr<TimelineItemModel> &tl, const QString &sequence)
{
    using namespace KoraCutMotion;
    QStringList rewritten;
    const double projectFps = fps();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry &entry = it.value();
        const Composition &comp = entry.sync->composition();
        if (resolved(comp.sequence) != sequence || !comp.isTransition() || comp.cut < 0 || comp.elements.isEmpty()) {
            continue;
        }
        const QString elementId = comp.elements.first().id;
        const int cid = clipFor(it.key(), elementId);
        if (cid < 0) {
            continue;
        }
        const int tid = tl->getClipTrackId(cid);
        const int position = tl->getClipPosition(cid);
        const int half = tl->getClipPlaytime(cid) / 2;
        const int storedCut = frames(comp.cut, projectFps);
        auto valid = [tl, tid](int clip) { return clip >= 0 && tl->isClip(clip) && tl->getClipTrackId(clip) != tid && !tl->isAudioTrack(tl->getClipTrackId(clip)); };
        if (!valid(entry.cutBefore) && !valid(entry.cutAfter)) {
            // À la réouverture du projet, ou après une annulation qui a changé les ids : la coupe écrite.
            int before = -1, after = -1, cut = -1;
            if (findCutNear(tl, tid, storedCut, 0, before, after, cut)) {
                entry.cutBefore = before;
                entry.cutAfter = after;
            }
        }
        // Les deux plans restent collés l'un à l'autre si possible : le plan qui finit là où le suivant commence.
        if (valid(entry.cutAfter)) {
            const int start = tl->getClipPosition(entry.cutAfter);
            const int left = tl->getClipByPosition(tl->getClipTrackId(entry.cutAfter), start - 1);
            if (left >= 0 && left != entry.cutAfter && tl->getClipPosition(left) + tl->getClipPlaytime(left) == start) {
                entry.cutBefore = left;
            }
        } else if (valid(entry.cutBefore)) {
            const int end = tl->getClipPosition(entry.cutBefore) + tl->getClipPlaytime(entry.cutBefore);
            const int right = tl->getClipByPosition(tl->getClipTrackId(entry.cutBefore), end);
            if (right >= 0 && right != entry.cutBefore && tl->getClipPosition(right) == end) {
                entry.cutAfter = right;
            }
        }
        int cut = -1;
        if (valid(entry.cutAfter)) {
            cut = tl->getClipPosition(entry.cutAfter);
        } else if (valid(entry.cutBefore)) {
            cut = tl->getClipPosition(entry.cutBefore) + tl->getClipPlaytime(entry.cutBefore);
        }
        int target = -1;
        bool cutMoved = false;
        if (cut >= 0 && cut != storedCut) {
            // La coupe a bougé (un rognage, un déplacement de plan) : la transition se recentre sur elle.
            target = cut - half;
            cutMoved = true;
        } else if (position != storedCut - half) {
            // La transition a été déplacée ou rognée à la main : elle s'accroche à la coupe la plus proche de
            // son milieu, sinon elle se détache et devient un élément libre.
            int before = -1, after = -1, nearest = -1;
            if (findCutNear(tl, tid, position + half, std::max(half, 1), before, after, nearest)) {
                entry.cutBefore = before;
                entry.cutAfter = after;
                cut = nearest;
                target = nearest - half;
            } else {
                entry.cutBefore = -1;
                entry.cutAfter = -1;
                if (entry.sync->writeElement(comp.id, {{QStringLiteral("data-koracut-cut"), QString()}})) {
                    rewritten << it.key();
                }
                continue;
            }
        }
        if (target < 0 || cut < 0) {
            continue;
        }
        int placed = position;
        if (target != position) {
            // Les éléments posés sous la transition (lot 5, suite) : relevés avant qu'elle bouge.
            const QList<int> followers = cutMoved ? followersOf(tl, it.key(), cid) : QList<int>();
            Fun undo = []() { return true; };
            Fun redo = []() { return true; };
            m_applying = true;
            // Pas d'étape d'annulation : la place de la transition découle de la coupe, qu'Annuler remet.
            const bool moved = tl->requestClipMove(cid, tid, target, true, true, true, true, undo, redo) == TimelineModel::MoveSuccess;
            if (moved) {
                // Ils suivent du même écart, sans étape à eux non plus. Une place prise : celui-là reste.
                const int delta = target - position;
                for (int follower : followers) {
                    const int start = tl->getClipPosition(follower);
                    if (start + delta >= 0) {
                        tl->requestClipMove(follower, tl->getClipTrackId(follower), start + delta, true, true, true, true, undo, redo);
                    }
                }
            }
            m_applying = false;
            if (!moved) {
                // La place est prise sur la piste des transitions : on n'écrit rien, on réessaiera au prochain geste.
                continue;
            }
            placed = target;
        }
        const QString id = comp.id;
        if (entry.sync->writeElement(id, {{QStringLiteral("data-koracut-cut"), secondsText(cut / projectFps)},
                                          {QStringLiteral("data-koracut-start"), secondsText(placed / projectFps)}})) {
            rewritten << it.key();
        }
    }
    return rewritten;
}

void KoraCutMotionMirror::scheduleThumbnail(const QString &binId, int attempt)
{
    // La vignette d'un rendu transparent prise à sa première image est vide : on la prend au milieu, une fois
    // le clip chargé (jamais pendant un chargement, voir le piège de discardJobs dans CLAUDE.md).
    if (!pCore->window()) {
        return; // sans fenêtre (les tests), pas de panneau à illustrer
    }
    QTimer::singleShot(700, this, [this, binId, attempt]() {
        auto clip = pCore->projectItemModel() ? pCore->projectItemModel()->getClipByBinID(binId) : nullptr;
        if (!clip) {
            return;
        }
        const bool loading = !clip->statusReady() || pCore->taskManager.hasPendingJob(ObjectId(KdenliveObjectType::BinClip, binId.toInt(), QUuid()), AbstractTask::LOADJOB);
        if (loading) {
            if (attempt < 10) {
                scheduleThumbnail(binId, attempt + 1);
            }
            return;
        }
        const int middle = int(clip->frameDuration()) / 2;
        if (middle > 0 && clip->getThumbFrame() != middle) {
            clip->setThumbFrame(middle);
        }
    });
}

QString KoraCutMotionMirror::stateDump() const
{
    QString out;
    for (const QString &name : m_entries.keys()) {
        const auto list = elements(name);
        auto tl = timelineFor(name);
        for (const ElementState &e : list) {
            QString render = QStringLiteral("aucun");
            if (e.pending) {
                render = QStringLiteral("en-cours");
            } else if (e.failed) {
                render = QStringLiteral("echec");
            } else if (!e.renderPath.isEmpty()) {
                render = e.renderPath;
            }
            out += QStringLiteral("KORACUT-MOTION %1 %2 %3 start=%4 dur=%5 track=%6 clip=%7 pos=%8 len=%9 in=%10 rendu=%11 texte=%12\n")
                       .arg(name, e.id, e.kind, KoraCutMotion::secondsText(e.start), KoraCutMotion::secondsText(e.duration))
                       .arg(e.trackIndex)
                       .arg(e.clipId)
                       .arg(tl && e.clipId >= 0 ? tl->getClipPosition(e.clipId) : -1)
                       .arg(tl && e.clipId >= 0 ? tl->getClipPlaytime(e.clipId) : -1)
                       .arg(tl && e.clipId >= 0 ? tl->getClipIn(e.clipId) : -1)
                       .arg(render, e.text);
        }
    }
    return out;
}
