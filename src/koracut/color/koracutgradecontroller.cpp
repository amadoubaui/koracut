/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutgradecontroller.h"
#include "koracutnodegraph.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "project/projectmanager.h"
#include "doc/kdenlivedoc.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "mainwindow.h"
#include "timeline2/model/clipmodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"
#include "utils/thumbnailcache.hpp"

#include "kcgrade.h"
#include "macros.hpp"
#include "undohelper.hpp"
#include "koracut/koracutagenttools_p.h"

#include <KLocalizedString>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace {
const QString kGradeEffect = QStringLiteral("koracut_grade");
const QString kParam = QStringLiteral("graphe");
const QString kStillsProperty = QStringLiteral("koracut:etal_stills");

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
} // namespace

KoraCutGradeController *KoraCutGradeController::self()
{
    static QPointer<KoraCutGradeController> instance;
    if (!instance) {
        instance = new KoraCutGradeController(qApp);
    }
    return instance;
}

KoraCutGradeController::KoraCutGradeController(QObject *parent)
    : QObject(parent)
{
    // Les LUT nommées sans chemin dans un graphe (« Rec709 Kodak 2383 D65.cube ») sont cherchées ici.
    std::vector<std::string> paths;
    for (const QString &f : lutFolders()) {
        paths.push_back(f.toStdString());
    }
    kcgrade::setLutSearchPaths(paths);
    // La séquence active peut changer (onglets, ouverture d'un projet) : on se rebranche à chaque fois.
    auto *poll = new QTimer(this);
    poll->setInterval(700);
    connect(poll, &QTimer::timeout, this, &KoraCutGradeController::attach);
    poll->start();
    QTimer::singleShot(0, this, &KoraCutGradeController::attach);
}

QStringList KoraCutGradeController::lutFolders()
{
    QStringList folders;
    for (const QString &dir : QStandardPaths::locateAll(QStandardPaths::AppLocalDataLocation, QStringLiteral("luts"), QStandardPaths::LocateDirectory)) {
        folders << dir;
    }
    if (pCore && pCore->currentDoc()) {
        const QString projectLuts = pCore->currentDoc()->projectDataFolder() + QStringLiteral("/luts");
        if (QFileInfo::exists(projectLuts)) {
            folders << projectLuts;
        }
    }
    return folders;
}

void KoraCutGradeController::attach()
{
    if (!pCore || !pCore->window()) {
        return;
    }
    TimelineWidget *w = pCore->window()->getCurrentTimeline();
    TimelineController *c = w ? w->controller() : nullptr;
    if (c == m_attached) {
        return;
    }
    if (m_attached) {
        disconnect(m_attached, nullptr, this, nullptr);
    }
    m_attached = c;
    m_clip = -1;
    m_thumbCache.clear();
    if (c) {
        connect(c, &TimelineController::selectionChanged, this, &KoraCutGradeController::followSelection);
        connect(c, &TimelineController::seeked, this, &KoraCutGradeController::followPosition);
        if (auto model = timeline()) {
            connect(model.get(), &QAbstractItemModel::rowsInserted, this, &KoraCutGradeController::clipsChanged);
            connect(model.get(), &QAbstractItemModel::rowsRemoved, this, &KoraCutGradeController::clipsChanged);
            connect(model.get(), &QAbstractItemModel::dataChanged, this, [this]() { Q_EMIT clipsChanged(); });
        }
        followPosition(pCore->getMonitorPosition());
    }
    Q_EMIT clipsChanged();
    Q_EMIT clipChanged(m_clip);
    Q_EMIT graphChanged();
    Q_EMIT stillsChanged();
}

std::shared_ptr<TimelineItemModel> KoraCutGradeController::timeline() const
{
    if (!pCore) {
        return nullptr;
    }
    if (m_override) {
        return m_override;
    }
    if (pCore->window()) {
        if (TimelineWidget *w = pCore->window()->getCurrentTimeline()) {
            return w->model();
        }
    }
    // Sans fenêtre (les tests, un rendu sans écran) : la séquence active du gestionnaire de projet.
    return pCore->projectManager() ? pCore->projectManager()->getTimeline() : nullptr;
}

QList<int> KoraCutGradeController::videoClips() const
{
    QList<int> clips;
    auto t = timeline();
    if (!t) {
        return clips;
    }
    struct Item
    {
        int pos;
        int track;
        int cid;
    };
    std::vector<Item> items;
    int trackRank = 0;
    // Pistes de haut en bas (l'ordre de l'écran) : à position égale, la piste du haut d'abord.
    for (int tid : KoraCutAgentToolsPrivate::tracksOnScreen(t)) {
        ++trackRank;
        if (t->isAudioTrack(tid)) {
            continue;
        }
        for (int cid : KoraCutAgentToolsPrivate::clipsOnTrack(t, tid)) {
            items.push_back({t->getClipPosition(cid), trackRank, cid});
        }
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.pos != b.pos ? a.pos < b.pos : a.track < b.track; });
    for (const Item &i : items) {
        clips << i.cid;
    }
    return clips;
}

void KoraCutGradeController::followSelection()
{
    auto t = timeline();
    if (!t) {
        return;
    }
    const auto sel = t->getCurrentSelection();
    if (sel.size() == 1) {
        const int id = *sel.begin();
        if (t->isClip(id) && !t->isAudioTrack(t->getClipTrackId(id)) && id != m_clip) {
            setCurrentClip(id);
        }
    }
}

void KoraCutGradeController::followPosition(int position)
{
    auto t = timeline();
    if (!t || m_scope == Scope::Timeline) {
        return;
    }
    // Sans sélection : le plan visible sous la tête de lecture (la piste vidéo la plus haute).
    if (!t->getCurrentSelection().empty()) {
        return;
    }
    for (int tid : KoraCutAgentToolsPrivate::tracksOnScreen(t)) {
        if (t->isAudioTrack(tid)) {
            continue;
        }
        const int cid = t->getClipByPosition(tid, position);
        if (cid >= 0) {
            if (cid != m_clip) {
                setCurrentClip(cid);
            }
            return;
        }
    }
}

void KoraCutGradeController::setCurrentClip(int cid, bool seek)
{
    if (m_compare) {
        setCompare(false);
        m_compare = true; // on la remet sur le nouveau plan
    }
    m_clip = cid;
    m_node = 1;
    if (seek && cid >= 0) {
        if (auto t = timeline()) {
            pCore->seekMonitor(Kdenlive::ProjectMonitor, t->getClipPosition(cid));
        }
    }
    const QJsonObject g = graph();
    const QList<int> ids = KoraCutNodeGraph::nodeIds(g);
    m_node = ids.isEmpty() ? 1 : ids.first();
    if (m_compare) {
        applyCompare();
    }
    Q_EMIT clipChanged(cid);
    Q_EMIT graphChanged();
    Q_EMIT nodeChanged(m_node);
}

void KoraCutGradeController::setScope(Scope scope)
{
    if (scope == m_scope) {
        return;
    }
    m_scope = scope;
    const QList<int> ids = KoraCutNodeGraph::nodeIds(graph());
    m_node = ids.isEmpty() ? 1 : ids.first();
    Q_EMIT clipChanged(m_clip);
    Q_EMIT graphChanged();
    Q_EMIT nodeChanged(m_node);
}

int KoraCutGradeController::targetId() const
{
    return m_scope == Scope::Timeline ? -1 : m_clip;
}

std::shared_ptr<EffectStackModel> KoraCutGradeController::stack(int cid) const
{
    auto t = timeline();
    if (!t) {
        return nullptr;
    }
    if (cid < 0) {
        return t->getMasterEffectStackModel();
    }
    if (!t->isClip(cid)) {
        return nullptr;
    }
    return t->getClipEffectStackModel(cid);
}

std::shared_ptr<EffectItemModel> KoraCutGradeController::gradeEffect(const std::shared_ptr<EffectStackModel> &s) const
{
    if (!s) {
        return nullptr;
    }
    for (int row = 0; row < s->rowCount(); ++row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(s->getEffectStackRow(row));
        if (effect && effect->getAssetId() == kGradeEffect) {
            return effect;
        }
    }
    return nullptr;
}

QJsonObject KoraCutGradeController::graphFor(int cid) const
{
    auto effect = gradeEffect(stack(cid));
    if (!effect) {
        return KoraCutNodeGraph::defaultGraph();
    }
    const QString text = effect->getParamFromName(kParam).toString();
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    return doc.isObject() ? KoraCutNodeGraph::normalized(doc.object()) : KoraCutNodeGraph::defaultGraph();
}

QJsonObject KoraCutGradeController::graph() const
{
    if (m_scope == Scope::Clip && m_clip < 0) {
        return KoraCutNodeGraph::defaultGraph();
    }
    return graphFor(targetId());
}

bool KoraCutGradeController::hasGrade(int cid) const
{
    auto effect = gradeEffect(stack(cid));
    if (!effect) {
        return false;
    }
    std::string error;
    auto g = kcgrade::Grade::compile(effect->getParamFromName(kParam).toString().toStdString(), &error);
    return g && !g->isIdentity();
}

void KoraCutGradeController::setCurrentNode(int id)
{
    if (id == m_node) {
        return;
    }
    m_node = id;
    Q_EMIT nodeChanged(id);
}

std::shared_ptr<EffectItemModel> KoraCutGradeController::ensureGradeEffect(int cid, const QString &json, bool *created)
{
    *created = false;
    auto s = stack(cid);
    if (!s) {
        return nullptr;
    }
    if (auto e = gradeEffect(s)) {
        return e;
    }
    // Le grade se pose en tête de pile : il agit sur l'image du plan avant les autres effets
    // (recadrage, transformation), comme le grade de Resolve avant le sizing de sortie. La pose fait
    // partie du geste en cours : elle entre dans la même étape d'annulation (m_createUndo).
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const int before = s->rowCount();
    if (!s->appendEffectWithParams(kGradeEffect, {{kParam, json}}, undo, redo) || s->rowCount() <= before) {
        return nullptr;
    }
    auto e = gradeEffect(s);
    if (e && e->row() > 0) {
        s->moveEffectWithUndo(0, e, undo, redo);
    }
    if (e) {
        e->setParameter(kParam, json, true);
    }
    *created = true;
    m_createUndo = undo;
    m_createRedo = redo;
    return e;
}

void KoraCutGradeController::edit(const QJsonObject &newGraph, const QString &gesture, bool final)
{
    const int target = targetId();
    if (m_scope == Scope::Clip && m_clip < 0) {
        return;
    }
    const QString json = compact(newGraph);
    bool created = false;
    auto effect = gradeEffect(stack(target));
    const QString before = effect ? effect->getParamFromName(kParam).toString() : QString();
    if (m_gestureTarget != target) {
        m_gestureStart.clear();
    }
    if (m_gestureStart.isEmpty()) {
        m_gestureStart = before.isEmpty() ? compact(KoraCutNodeGraph::defaultGraph()) : before;
        m_gestureTarget = target;
    }
    if (!effect) {
        effect = ensureGradeEffect(target, json, &created);
        if (!effect) {
            return;
        }
    }
    if (!final) {
        // Pendant le glisser : l'image suit, rien n'entre dans l'historique.
        effect->setParameter(kParam, json, true);
        Q_EMIT graphChanged();
        return;
    }
    const QString start = m_gestureStart;
    m_gestureStart.clear();
    m_gestureTarget = -2;
    if (start == json && !created && !m_createUndo) {
        effect->setParameter(kParam, json, true);
        Q_EMIT graphChanged();
        return;
    }
    std::weak_ptr<EffectItemModel> weak = effect;
    // Si l'effet vient d'être posé pendant ce geste, sa pose et son réglage font une seule étape.
    Fun createUndo = m_createUndo ? m_createUndo : Fun([]() { return true; });
    Fun createRedo = m_createRedo ? m_createRedo : Fun([]() { return true; });
    const bool wasCreated = bool(m_createUndo);
    m_createUndo = nullptr;
    m_createRedo = nullptr;
    Fun redo = [weak, json]() {
        if (auto e = weak.lock()) {
            e->setParameter(kParam, json, true);
        }
        return true;
    };
    Fun undo = [weak, start, this]() {
        if (auto e = weak.lock()) {
            e->setParameter(kParam, start, true);
        }
        QTimer::singleShot(0, this, [this]() { Q_EMIT graphChanged(); });
        return true;
    };
    Fun redoNotify = [redo, createRedo, wasCreated, this]() {
        if (wasCreated) {
            createRedo();
        }
        redo();
        QTimer::singleShot(0, this, [this]() { Q_EMIT graphChanged(); });
        return true;
    };
    Fun undoAll = [undo, createUndo, wasCreated]() {
        undo();
        if (wasCreated) {
            createUndo();
        }
        return true;
    };
    redo();
    pCore->pushUndo(undoAll, redoNotify, i18nc("@action undo text %1 is what was changed", "Color: %1", gesture));
    Q_EMIT graphChanged();
}

bool KoraCutGradeController::applyGraph(int cid, const QString &json, Fun &undo, Fun &redo)
{
    auto s = stack(cid);
    if (!s) {
        return false;
    }
    auto e = gradeEffect(s);
    if (!e) {
        const int before = s->rowCount();
        if (!s->appendEffectWithParams(kGradeEffect, {{kParam, json}}, undo, redo) || s->rowCount() <= before) {
            return false;
        }
        e = gradeEffect(s);
        if (!e) {
            return false;
        }
        if (e->row() > 0) {
            s->moveEffectWithUndo(0, e, undo, redo);
        }
        e->setParameter(kParam, json, true);
        return true;
    }
    const QString before = e->getParamFromName(kParam).toString();
    std::weak_ptr<EffectItemModel> weak = e;
    Fun local_redo = [weak, json]() {
        if (auto x = weak.lock()) {
            x->setParameter(kParam, json, true);
        }
        return true;
    };
    Fun local_undo = [weak, before]() {
        if (auto x = weak.lock()) {
            x->setParameter(kParam, before, true);
        }
        return true;
    };
    local_redo();
    UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
    return true;
}

bool KoraCutGradeController::setGraphsFor(const QList<QPair<int, QJsonObject>> &graphs, const QString &gesture)
{
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool all = true;
    for (const auto &[cid, g] : graphs) {
        all = applyGraph(cid, compact(g), undo, redo) && all;
    }
    Fun undoNotify = [undo, this]() {
        undo();
        QTimer::singleShot(0, this, [this]() {
            Q_EMIT graphChanged();
            Q_EMIT clipsChanged();
        });
        return true;
    };
    Fun redoNotify = [redo, this]() {
        redo();
        QTimer::singleShot(0, this, [this]() {
            Q_EMIT graphChanged();
            Q_EMIT clipsChanged();
        });
        return true;
    };
    pCore->pushUndo(undoNotify, redoNotify, i18nc("@action undo text %1 is what was changed", "Color: %1", gesture));
    m_thumbCache.clear();
    Q_EMIT graphChanged();
    Q_EMIT clipsChanged();
    return all;
}

bool KoraCutGradeController::setGraphFor(int cid, const QJsonObject &g, const QString &gesture)
{
    return setGraphsFor({{cid, g}}, gesture);
}

int KoraCutGradeController::copyGrade(int from, const QList<int> &to, bool append)
{
    const QJsonObject source = graphFor(from);
    QList<QPair<int, QJsonObject>> list;
    for (int cid : to) {
        if (cid == from || !stack(cid)) {
            continue;
        }
        QJsonObject target = append ? graphFor(cid) : source;
        if (append) {
            KoraCutNodeGraph::appendGraph(target, source);
        }
        list.append({cid, target});
    }
    if (list.isEmpty()) {
        return 0;
    }
    setGraphsFor(list, append ? i18nc("@action undo text", "append grade") : i18nc("@action undo text", "copy grade"));
    return int(list.size());
}


void KoraCutGradeController::resetGrade(bool keepNodes)
{
    QJsonObject g = graph();
    KoraCutNodeGraph::resetAll(g, keepNodes);
    edit(g, keepNodes ? i18nc("@action undo text", "reset grades, keep nodes") : i18nc("@action undo text", "reset grade"));
    const QList<int> ids = KoraCutNodeGraph::nodeIds(graph());
    setCurrentNode(ids.isEmpty() ? 1 : ids.first());
}

bool KoraCutGradeController::bypassed() const
{
    return !graph().value(QStringLiteral("actif")).toBool(true);
}

void KoraCutGradeController::toggleBypass()
{
    QJsonObject g = graph();
    g.insert(QStringLiteral("actif"), bypassed());
    edit(g, bypassed() ? i18nc("@action undo text", "enable grade") : i18nc("@action undo text", "bypass grade"));
}

void KoraCutGradeController::setCompare(bool split, double position)
{
    m_compare = split;
    m_comparePos = std::clamp(position, 0.0, 1.0);
    applyCompare();
}

void KoraCutGradeController::applyCompare()
{
    // La propriété commence par « _ » : MLT ne l'écrit pas dans le projet.
    auto t = timeline();
    if (!t) {
        return;
    }
    for (int cid : videoClips()) {
        if (auto e = gradeEffect(stack(cid))) {
            const bool on = m_compare && cid == m_clip;
            e->filter().set("_koracut_comparer", on ? "split" : "");
            e->filter().set("_koracut_position", m_comparePos);
        }
    }
    pCore->refreshProjectMonitorOnce();
}

QImage KoraCutGradeController::clipImage(int cid, int width, bool graded, const QJsonObject &g, int upToNode) const
{
    auto t = timeline();
    if (!t || !t->isClip(cid)) {
        return QImage();
    }
    const QString binId = t->getClipBinId(cid);
    const int in = t->getClipIn(cid);
    const QString key = QStringLiteral("%1:%2:%3").arg(binId).arg(in).arg(width);
    QImage base = m_thumbCache.value(key);
    if (base.isNull()) {
        QImage img = ThumbnailCache::get()->getThumbnail(binId, in);
        if (img.isNull()) {
            if (auto clip = pCore->projectItemModel()->getClipByBinID(binId)) {
                img = clip->thumbnail(width, int(width * 9. / 16.)).toImage();
            }
        }
        if (img.isNull()) {
            return QImage();
        }
        base = img.scaledToWidth(width, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
        m_thumbCache.insert(key, base);
    }
    if (!graded) {
        return base;
    }
    QJsonObject graphToUse = g.isEmpty() ? graphFor(cid) : g;
    if (upToNode >= 0) {
        // La vignette d'un nœud : le grade jusqu'à ce nœud inclus.
        graphToUse.insert(QStringLiteral("sortie"), upToNode);
    }
    std::string error;
    auto grade = kcgrade::Grade::compile(compact(graphToUse).toStdString(), &error);
    if (!grade || grade->isIdentity()) {
        return base;
    }
    QImage out = base.copy();
    grade->processRgba8(out.bits(), out.width(), out.height(), int(out.bytesPerLine()), kcgrade::threadRunner(1));
    return out;
}

QList<KoraCutGradeController::Still> KoraCutGradeController::stills() const
{
    QList<Still> list;
    if (!pCore || !pCore->currentDoc()) {
        return list;
    }
    const QJsonArray arr = QJsonDocument::fromJson(pCore->currentDoc()->getDocumentProperty(kStillsProperty).toUtf8()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Still s;
        s.id = o.value(QStringLiteral("id")).toString();
        s.label = o.value(QStringLiteral("etiquette")).toString();
        s.graph = o.value(QStringLiteral("graphe")).toObject();
        s.image.loadFromData(QByteArray::fromBase64(o.value(QStringLiteral("image")).toString().toLatin1()), "JPG");
        list << s;
    }
    return list;
}

void KoraCutGradeController::grabStill()
{
    if (!pCore || !pCore->currentDoc() || m_clip < 0) {
        return;
    }
    QJsonArray arr = QJsonDocument::fromJson(pCore->currentDoc()->getDocumentProperty(kStillsProperty).toUtf8()).array();
    const QImage img = clipImage(m_clip, 256, true);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.convertToFormat(QImage::Format_RGB888).save(&buf, "JPG", 82);
    // Étiquette à la manière de Resolve : « 1.3 » (numéro du plan, numéro du still).
    const int clipNumber = videoClips().indexOf(m_clip) + 1;
    QJsonObject o{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                  {QStringLiteral("etiquette"), QStringLiteral("%1.%2").arg(clipNumber).arg(arr.size() + 1)},
                  {QStringLiteral("graphe"), graph()},
                  {QStringLiteral("date"), QDateTime::currentDateTime().toString(Qt::ISODate)},
                  {QStringLiteral("image"), QString::fromLatin1(bytes.toBase64())}};
    arr.append(o);
    const QString before = pCore->currentDoc()->getDocumentProperty(kStillsProperty);
    const QString after = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    Fun redo = [after, this]() {
        pCore->currentDoc()->setDocumentProperty(kStillsProperty, after);
        Q_EMIT stillsChanged();
        return true;
    };
    Fun undo = [before, this]() {
        pCore->currentDoc()->setDocumentProperty(kStillsProperty, before);
        Q_EMIT stillsChanged();
        return true;
    };
    redo();
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Color: grab still"));
}

void KoraCutGradeController::applyStill(int index, bool append)
{
    const QList<Still> list = stills();
    if (index < 0 || index >= list.size()) {
        return;
    }
    QJsonObject g = append ? graph() : KoraCutNodeGraph::normalized(list.at(index).graph);
    if (append) {
        KoraCutNodeGraph::appendGraph(g, list.at(index).graph);
    }
    edit(g, append ? i18nc("@action undo text", "append still grade") : i18nc("@action undo text", "apply still grade"));
}

void KoraCutGradeController::removeStill(int index)
{
    if (!pCore || !pCore->currentDoc()) {
        return;
    }
    QJsonArray arr = QJsonDocument::fromJson(pCore->currentDoc()->getDocumentProperty(kStillsProperty).toUtf8()).array();
    if (index < 0 || index >= arr.size()) {
        return;
    }
    const QString before = pCore->currentDoc()->getDocumentProperty(kStillsProperty);
    arr.removeAt(index);
    const QString after = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    Fun redo = [after, this]() {
        pCore->currentDoc()->setDocumentProperty(kStillsProperty, after);
        Q_EMIT stillsChanged();
        return true;
    };
    Fun undo = [before, this]() {
        pCore->currentDoc()->setDocumentProperty(kStillsProperty, before);
        Q_EMIT stillsChanged();
        return true;
    };
    redo();
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Color: delete still"));
}
