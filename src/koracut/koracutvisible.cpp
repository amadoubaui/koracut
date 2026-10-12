/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutvisible.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutagenttools_p.h"
#include "mainwindow.h"
#include "monitor/monitormanager.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QActionGroup>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QQuickItem>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <memory>
#include <unordered_set>

namespace {

const char *kConfigKey = "montageVisible";

KoraCutVisible::Speed speedFromKey(const QString &key)
{
    if (key == QLatin1String("rapide")) {
        return KoraCutVisible::Speed::Fast;
    }
    if (key == QLatin1String("desactive")) {
        return KoraCutVisible::Speed::Off;
    }
    return KoraCutVisible::Speed::Normal;
}

QString speedLabel(KoraCutVisible::Speed speed)
{
    switch (speed) {
    case KoraCutVisible::Speed::Off:
        return i18nc("@item visible editing speed", "Off");
    case KoraCutVisible::Speed::Fast:
        return i18nc("@item visible editing speed", "Fast");
    case KoraCutVisible::Speed::Normal:
        break;
    }
    return i18nc("@item visible editing speed", "Normal");
}

TimelineWidget *currentTimelineWidget()
{
    return pCore && pCore->window() ? pCore->window()->getCurrentTimeline() : nullptr;
}

std::shared_ptr<TimelineItemModel> currentModel()
{
    TimelineWidget *tw = currentTimelineWidget();
    return tw ? tw->model() : nullptr;
}

QString mediaName(const QString &binId)
{
    if (!pCore || !pCore->projectItemModel()) {
        return binId;
    }
    auto clip = pCore->projectItemModel()->getClipByBinID(binId);
    return clip ? clip->clipName() : binId;
}

QString tc(const QJsonValue &value)
{
    const int frames = KoraCutAgentTools::parseTime(value, KoraCutAgentToolsPrivate::projectFps());
    return frames < 0 ? QString() : KoraCutAgentTools::timecode(frames, KoraCutAgentToolsPrivate::projectFps());
}

// Le premier média visé par un appel : media_id, media_ids, ou la première ligne d'un plan.
QString firstMedia(const QJsonObject &args)
{
    if (args.contains(QStringLiteral("media_id"))) {
        return args.value(QStringLiteral("media_id")).toVariant().toString();
    }
    const QJsonArray ids = args.value(QStringLiteral("media_ids")).toArray();
    if (!ids.isEmpty()) {
        return ids.first().toVariant().toString();
    }
    const QJsonArray plan = args.value(QStringLiteral("plan")).toArray();
    if (!plan.isEmpty()) {
        return plan.first().toObject().value(QStringLiteral("media_id")).toVariant().toString();
    }
    return QString();
}

} // namespace

KoraCutVisible *KoraCutVisible::self()
{
    static QPointer<KoraCutVisible> instance;
    if (!instance) {
        instance = new KoraCutVisible(qApp);
    }
    return instance;
}

KoraCutVisible::KoraCutVisible(QObject *parent)
    : QObject(parent)
{
    m_speed = speedFromKey(KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry(kConfigKey, QStringLiteral("normal")));
    m_idle = new QTimer(this);
    m_idle->setSingleShot(true);
    m_idle->setInterval(2500);
    // La série de gestes est finie : la ligne s'efface, « Accélérer » retombe.
    connect(m_idle, &QTimer::timeout, this, [this]() {
        m_hurry = false;
        m_line.clear();
        refreshWidget();
    });
}

QString KoraCutVisible::speedKey(Speed speed)
{
    switch (speed) {
    case Speed::Off:
        return QStringLiteral("desactive");
    case Speed::Fast:
        return QStringLiteral("rapide");
    case Speed::Normal:
        break;
    }
    return QStringLiteral("normal");
}

void KoraCutVisible::setSpeed(Speed speed)
{
    if (speed == m_speed) {
        return;
    }
    m_speed = speed;
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
    group.writeEntry(kConfigKey, speedKey(speed));
    group.sync();
    refreshWidget();
    Q_EMIT speedChanged();
}

bool KoraCutVisible::active() const
{
    return m_speed != Speed::Off && pCore && pCore->window() != nullptr;
}

int KoraCutVisible::pauseMs() const
{
    if (!active() || m_hurry) {
        return 0;
    }
    return m_speed == Speed::Fast ? 120 : 350;
}

bool KoraCutVisible::isReadOnly(const QString &tool)
{
    static const QStringList prefixes{QStringLiteral("etat_"),   QStringLiteral("lister_"),  QStringLiteral("mesurer_"),
                                      QStringLiteral("voir_"),   QStringLiteral("verifier_"), QStringLiteral("estimer_"),
                                      // Le thème et les réglages de l'application : rien à montrer dans la timeline.
                                      QStringLiteral("theme_"),  QStringLiteral("reglages_")};
    for (const QString &p : prefixes) {
        if (tool.startsWith(p)) {
            return true;
        }
    }
    // Ce qui ne change pas la timeline, ou qui prend du temps de lui-même (rendus, transcription).
    static const QStringList names{QStringLiteral("script_lire"),       QStringLiteral("motion_lire"),       QStringLiteral("motion_lister"),
                                   QStringLiteral("motion_instantane"), QStringLiteral("motion_verifier"),   QStringLiteral("motion_rendre"),
                                   QStringLiteral("transcrire_media"),  QStringLiteral("detecter_silences"), QStringLiteral("temps_musique"),     QStringLiteral("exporter"),
                                   QStringLiteral("exporter_serie"),    QStringLiteral("exporter_image"),    QStringLiteral("annuler_export"),
                                   QStringLiteral("rendre_apercu"),     QStringLiteral("proposer_plan"),     QStringLiteral("deleguer"),
                                   QStringLiteral("voix_off_voix"),     QStringLiteral("voix_off_installer")};
    return names.contains(tool);
}

void KoraCutVisible::later(int ms, QObject *guard, const std::function<void()> &fn)
{
    if (ms <= 0) {
        fn();
        return;
    }
    QTimer::singleShot(ms, guard ? guard : this, fn);
}

void KoraCutVisible::run(const QString &who, const QString &tool, const QJsonObject &args, QObject *guard, const std::function<QJsonObject()> &gesture,
                         const std::function<void(const QJsonObject &)> &done, bool enabled)
{
    if (!enabled || !active() || isReadOnly(tool)) {
        done(gesture());
        return;
    }
    before(who, tool, args);
    const int pause = pauseMs();
    // Un peu moins de la moitié avant (on voit l'endroit), le reste après (on voit le résultat).
    const int pre = pause * 2 / 5;
    later(pre, guard, [this, tool, args, guard, gesture, done, pause, pre]() {
        const QJsonObject result = gesture();
        const bool ok = result.value(QStringLiteral("ok")).toBool(false);
        const int extra = ok ? after(tool, args, result) : 0;
        touchActivity();
        // « Accélérer » pressé entre-temps : on ne fait plus attendre.
        const int rest = m_hurry ? 0 : (ok ? pause - pre + extra : 0);
        later(rest, guard, [done, result]() { done(result); });
    });
}

void KoraCutVisible::touchActivity()
{
    m_idle->start();
    refreshWidget();
}

void KoraCutVisible::setLine(const QString &text)
{
    m_line = text;
    refreshWidget();
    Q_EMIT lineChanged(text);
}

void KoraCutVisible::progressLine(const QString &who, const QString &text)
{
    if (!active()) {
        return;
    }
    setLine(who.isEmpty() ? text : QStringLiteral("%1 · %2").arg(who, text));
    touchActivity();
}

void KoraCutVisible::hurry()
{
    m_hurry = true;
    refreshWidget();
}

QString KoraCutVisible::describe(const QString &tool, const QJsonObject &args) const
{
    auto tl = currentModel();
    const QJsonValue position = args.value(QStringLiteral("position"));
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    const QString clipName = tl && cid >= 0 && tl->isClip(cid) ? tl->getClipName(cid) : QString();
    const QString name = args.value(QStringLiteral("nom")).toString();
    const QString media = args.contains(QStringLiteral("media_id")) ? mediaName(args.value(QStringLiteral("media_id")).toVariant().toString()) : QString();
    if (tool == QLatin1String("couper_clip")) {
        return i18nc("@info:status visible editing, %1 timecode", "Cut at %1", tc(position));
    }
    if (tool == QLatin1String("couper_toutes_pistes")) {
        return i18nc("@info:status visible editing, %1 timecode", "Cut all tracks at %1", tc(position));
    }
    if (tool == QLatin1String("ajouter_clip")) {
        const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
        const QString track = tl && tl->isTrack(tid) ? KoraCutAgentToolsPrivate::trackLabel(tl, tid) : QString();
        return i18nc("@info:status visible editing, %1 media, %2 track, %3 timecode", "Place %1 on %2 at %3", media, track, tc(position));
    }
    if (tool == QLatin1String("deplacer_clip")) {
        return i18nc("@info:status visible editing, %1 clip, %2 timecode", "Move %1 to %2", clipName, tc(position));
    }
    if (tool == QLatin1String("supprimer_clip")) {
        return i18nc("@info:status visible editing", "Remove %1", clipName.isEmpty() ? i18nc("@item", "clips") : clipName);
    }
    if (tool == QLatin1String("rogner_clip")) {
        return i18nc("@info:status visible editing, %1 clip", "Trim %1", clipName);
    }
    if (tool == QLatin1String("supprimer_espace")) {
        return i18nc("@info:status visible editing, %1 timecode", "Close the gap at %1", tc(position));
    }
    if (tool == QLatin1String("ajouter_titre")) {
        return i18nc("@info:status visible editing, %1 title text", "Title: %1", args.value(QStringLiteral("texte")).toString().left(40));
    }
    if (tool == QLatin1String("renommer_media")) {
        return i18nc("@info:status visible editing, %1 new name", "Rename %1", name);
    }
    if (tool == QLatin1String("renommer_fichier_media")) {
        return i18nc("@info:status visible editing, %1 new file name", "Rename the file %1", name);
    }
    if (tool == QLatin1String("ranger_media")) {
        const int n = int(args.value(QStringLiteral("media_ids")).toArray().size());
        return i18ncp("@info:status visible editing, %2 folder", "Move %1 media to %2", "Move %1 media to %2", n, args.value(QStringLiteral("dossier")).toString());
    }
    if (tool == QLatin1String("organiser_rushs")) {
        const int n = int(args.value(QStringLiteral("plan")).toArray().size());
        return i18ncp("@info:status visible editing", "Organize %1 media", "Organize %1 media", n);
    }
    if (tool == QLatin1String("creer_dossier_chutier")) {
        return i18nc("@info:status visible editing, %1 folder", "Create the folder %1", name);
    }
    if (tool == QLatin1String("renommer_dossier_chutier")) {
        return i18nc("@info:status visible editing, %1 folder", "Rename the folder %1", name);
    }
    if (tool == QLatin1String("creer_sequence")) {
        return i18nc("@info:status visible editing, %1 sequence", "Create the sequence %1", name);
    }
    if (tool == QLatin1String("renommer_sequence")) {
        return i18nc("@info:status visible editing, %1 sequence", "Rename the sequence %1", name);
    }
    if (tool == QLatin1String("activer_sequence")) {
        return i18nc("@info:status visible editing, %1 sequence", "Open the sequence %1", args.value(QStringLiteral("sequence")).toString());
    }
    if (tool == QLatin1String("dupliquer_sequence")) {
        return i18nc("@info:status visible editing", "Duplicate the sequence");
    }
    if (tool == QLatin1String("renommer_piste")) {
        return i18nc("@info:status visible editing, %1 track name", "Rename the track %1", name);
    }
    if (tool == QLatin1String("renommer_clip")) {
        return i18nc("@info:status visible editing, %1 clip name", "Name the clip %1", name);
    }
    if (tool == QLatin1String("voix_off_generer")) {
        return i18nc("@info:status visible editing, %1 start of the spoken text", "Voice-over: %1", args.value(QStringLiteral("texte")).toString().left(40));
    }
    if (tool == QLatin1String("importer_media")) {
        const int n = int(args.value(QStringLiteral("chemins")).toArray().size());
        return i18ncp("@info:status visible editing", "Import %1 file", "Import %1 files", n);
    }
    if (tool.contains(QLatin1String("effet")) || tool.contains(QLatin1String("etalonnage")) || tool.contains(QLatin1String("fondu")) || tool == QLatin1String("transformer_clip")) {
        return clipName.isEmpty() ? i18nc("@info:status visible editing", "Adjust the effects") : i18nc("@info:status visible editing, %1 clip", "Adjust %1", clipName);
    }
    // Le nom de l'outil, lisible : « regler_piste » devient « Regler piste ».
    QString readable = tool;
    readable.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!readable.isEmpty()) {
        readable[0] = readable.at(0).toUpper();
    }
    return clipName.isEmpty() ? readable : QStringLiteral("%1 · %2").arg(readable, clipName);
}

// Un montage qui tient dans un coin de la timeline ne se regarde pas : quand tout le contenu occupe
// moins de 40 % de la largeur visible, on ajuste le zoom à la séquence (comme « Ajuster le zoom »).
// Seulement pour zoomer plus près, jamais pour éloigner : un long montage garde le zoom de la personne.
static void fitIfTiny(TimelineWidget *tw)
{
    auto tl = tw ? tw->model() : nullptr;
    QQuickItem *root = tw ? tw->rootObject() : nullptr;
    MainWindow *w = pCore ? pCore->window() : nullptr;
    if (!tl || !root || !w) {
        return;
    }
    const double scale = root->property("timeScale").toDouble();
    const double width = tw->width() - root->property("headerWidth").toDouble();
    const int duration = tl->duration();
    if (scale <= 0 || width <= 0 || duration <= 0) {
        return;
    }
    if (duration * scale < 0.4 * width) {
        QMetaObject::invokeMethod(w, "slotFitZoom", Qt::DirectConnection);
    }
}

void KoraCutVisible::seek(int position)
{
    TimelineWidget *tw = currentTimelineWidget();
    if (!tw || position < 0 || !pCore->monitorManager()) {
        return;
    }
    fitIfTiny(tw);
    const int current = pCore->getMonitorPosition(Kdenlive::ProjectMonitor);
    pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
    if (current != position) {
        // La timeline défile d'elle-même quand la tête sort de la vue (Logic.scrollIfNeeded).
        tw->controller()->setPosition(position);
        return;
    }
    // Même place : rien ne défile tout seul ; si la personne a fait défiler ailleurs, on recentre.
    if (QQuickItem *root = tw->rootObject()) {
        const double scale = root->property("timeScale").toDouble();
        if (scale > 0) {
            const int first = root->property("scrollMin").toInt();
            const int visible = int((tw->width() - root->property("headerWidth").toDouble()) / scale);
            if (position < first || position > first + visible) {
                tw->slotCenterView();
            }
        }
    }
}

void KoraCutVisible::revealClip(int clipId, int position)
{
    TimelineWidget *tw = currentTimelineWidget();
    auto tl = tw ? tw->model() : nullptr;
    if (!tl || !tl->isClip(clipId)) {
        return;
    }
    tl->requestSetSelection({clipId});
    fitIfTiny(tw);
    // Un clip de quelques pixels ne se voit pas : on zoome un peu, jamais au-delà du raisonnable.
    if (QQuickItem *root = tw->rootObject()) {
        const double scale = root->property("timeScale").toDouble();
        MainWindow *w = pCore->window();
        if (w && scale > 0 && tl->getClipPlaytime(clipId) * scale < 12 && w->koraCutZoomLevel() > 3) {
            w->koraCutSetZoom(w->koraCutZoomLevel() - 2, false);
        }
    }
    seek(position >= 0 ? position : tl->getClipPosition(clipId));
}

void KoraCutVisible::selectMedia(const QString &binId)
{
    if (binId.isEmpty() || !pCore || !pCore->window() || !pCore->projectItemModel() || !pCore->projectItemModel()->hasClip(binId)) {
        return;
    }
    // Le chutier devant, s'il est ouvert dans la disposition ; le dossier du média s'ouvre.
    if (auto *dock = KDDockWidgets::DockRegistry::self()->dockByName(QStringLiteral("project_bin")); dock && dock->isOpen()) {
        dock->setAsCurrentTab();
    }
    pCore->selectBinClip(binId, false);
}

void KoraCutVisible::before(const QString &who, const QString &tool, const QJsonObject &args)
{
    if (!active()) {
        return;
    }
    ++m_gestures;
    const QString text = describe(tool, args);
    setLine(who.isEmpty() ? text : QStringLiteral("%1 · %2").arg(who, text));
    touchActivity();
    auto tl = currentModel();
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    const int position = args.contains(QStringLiteral("position")) ? KoraCutAgentTools::parseTime(args.value(QStringLiteral("position")), KoraCutAgentToolsPrivate::projectFps()) : -1;
    // Le média d'abord (le chutier s'ouvre dessus), la timeline ensuite (le moniteur Programme devant).
    const QString media = firstMedia(args);
    if (!media.isEmpty()) {
        selectMedia(media);
    }
    if (tl && cid >= 0 && tl->isClip(cid)) {
        const int start = tl->getClipPosition(cid);
        const int end = start + tl->getClipPlaytime(cid);
        // Une coupe à l'intérieur du clip : la tête va à la coupe ; sinon au début du clip.
        revealClip(cid, position >= start && position < end ? position : start);
        return;
    }
    const QJsonArray ids = args.value(QStringLiteral("clip_ids")).toArray();
    if (tl && !ids.isEmpty()) {
        std::unordered_set<int> valid;
        int first = -1;
        for (const QJsonValue &v : ids) {
            if (tl->isClip(v.toInt(-1))) {
                valid.insert(v.toInt());
                const int p = tl->getClipPosition(v.toInt());
                first = first < 0 ? p : std::min(first, p);
            }
        }
        if (!valid.empty()) {
            tl->requestSetSelection(valid);
            seek(first);
            return;
        }
    }
    // Une position sur la timeline (ajouter_clip, supprimer_espace…) ; celle d'un marqueur de média non.
    if (tl && position >= 0 && (media.isEmpty() || args.contains(QStringLiteral("piste_id")))) {
        seek(position);
    }
}

int KoraCutVisible::after(const QString &tool, const QJsonObject &args, const QJsonObject &result)
{
    if (!active()) {
        return 0;
    }
    Q_UNUSED(args)
    auto tl = currentModel();
    // Un plan posé, un titre, un clip déplacé : il est sélectionné et la tête va sur lui.
    const int cid = result.value(QStringLiteral("clip_id")).toInt(-1);
    if (tl && cid >= 0 && tl->isClip(cid)) {
        const bool placed = tool == QLatin1String("ajouter_clip") || tool == QLatin1String("ajouter_titre") || tool == QLatin1String("deplacer_clip");
        if (placed) {
            revealClip(cid, tl->getClipPosition(cid));
        } else {
            tl->requestSetSelection({cid});
        }
    }
    // Un lot rangé : le chutier passe sur chaque média, vite, pour qu'on voie où ils sont allés.
    QStringList medias;
    for (const QJsonValue &v : result.value(QStringLiteral("ranges")).toArray()) {
        medias << (v.isObject() ? v.toObject().value(QStringLiteral("media_id")).toVariant().toString() : v.toVariant().toString());
    }
    for (const QJsonValue &v : result.value(QStringLiteral("medias")).toArray()) {
        medias << v.toObject().value(QStringLiteral("id")).toVariant().toString();
    }
    if (result.contains(QStringLiteral("media_id"))) {
        medias.prepend(result.value(QStringLiteral("media_id")).toVariant().toString());
    }
    medias.removeAll(QString());
    if (medias.isEmpty()) {
        return 0;
    }
    if (medias.size() == 1 || m_hurry) {
        selectMedia(medias.first());
        return 0;
    }
    // Au plus douze arrêts, pris régulièrement dans la liste.
    QStringList stops;
    const int count = std::min<int>(12, int(medias.size()));
    for (int i = 0; i < count; ++i) {
        stops << medias.at(int(qint64(i) * (medias.size() - 1) / std::max(1, count - 1)));
    }
    const int step = std::clamp(pauseMs() / 2, 60, 160);
    for (int i = 0; i < stops.size(); ++i) {
        const QString id = stops.at(i);
        QTimer::singleShot(i * step, this, [this, id]() { selectMedia(id); });
    }
    return int(stops.size()) * step;
}

void KoraCutVisible::refreshWidget()
{
    if (!m_widget) {
        return;
    }
    const bool busy = !m_line.isEmpty() && m_idle->isActive();
    if (m_label) {
        m_label->setText(m_label->fontMetrics().elidedText(m_line, Qt::ElideRight, 420));
        m_label->setToolTip(m_line);
    }
    if (m_hurryButton) {
        m_hurryButton->setEnabled(!m_hurry && m_speed != Speed::Off);
    }
    if (m_speedButton) {
        m_speedButton->setText(speedLabel(m_speed));
    }
    m_widget->setVisible(busy);
}

QWidget *KoraCutVisible::createStatusWidget(QWidget *parent)
{
    auto *box = new QWidget(parent);
    box->setObjectName(QStringLiteral("koracutVisibleEditing"));
    auto *layout = new QHBoxLayout(box);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(6);
    m_label = new QLabel(box);
    m_label->setTextFormat(Qt::PlainText);
    layout->addWidget(m_label);
    m_hurryButton = new QToolButton(box);
    m_hurryButton->setAutoRaise(true);
    m_hurryButton->setText(i18nc("@action:button visible editing: skip the pauses", "Faster"));
    m_hurryButton->setToolTip(i18nc("@info:tooltip", "Skip the pauses until the agent stops"));
    connect(m_hurryButton, &QToolButton::clicked, this, &KoraCutVisible::hurry);
    layout->addWidget(m_hurryButton);
    m_speedButton = new QToolButton(box);
    m_speedButton->setAutoRaise(true);
    m_speedButton->setPopupMode(QToolButton::InstantPopup);
    m_speedButton->setToolTip(i18nc("@info:tooltip", "Visible editing: how fast the agents' gestures follow each other"));
    auto *menu = new QMenu(m_speedButton);
    auto *group = new QActionGroup(menu);
    KActionCollection *collection = nullptr;
    if (auto *window = qobject_cast<MainWindow *>(parent)) {
        collection = window->actionCollection();
    }
    const auto addSpeed = [this, menu, group, collection](Speed speed, const QString &name, const QString &text) {
        QAction *a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(m_speed == speed);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, speed]() { setSpeed(speed); });
        connect(this, &KoraCutVisible::speedChanged, a, [this, a, speed]() { a->setChecked(m_speed == speed); });
        if (collection) {
            collection->addAction(name, a);
        }
    };
    addSpeed(Speed::Normal, QStringLiteral("koracut_visible_normal"), i18nc("@action:inmenu visible editing speed", "Visible Editing: Normal"));
    addSpeed(Speed::Fast, QStringLiteral("koracut_visible_rapide"), i18nc("@action:inmenu visible editing speed", "Visible Editing: Fast"));
    addSpeed(Speed::Off, QStringLiteral("koracut_visible_desactive"), i18nc("@action:inmenu visible editing speed", "Visible Editing: Off"));
    m_speedButton->setMenu(menu);
    layout->addWidget(m_speedButton);
    m_widget = box;
    box->setVisible(false);
    refreshWidget();
    return box;
}

QString KoraCutVisible::debugState() const
{
    return QStringLiteral("vitesse=%1 pause=%2 gestes=%3 ligne=%4").arg(speedKey(m_speed)).arg(pauseMs()).arg(m_gestures).arg(m_line);
}
