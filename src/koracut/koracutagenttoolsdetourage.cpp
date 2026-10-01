/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// 1er octobre 2026 : detourer_sujet (Agents in-app/outils/montage.json). Le sujet d'un plan (une
// personne, ou ce qui est devant : une main qui tient un téléphone) est détouré par le programme
// koracut-detourer (Sources/koracut-detourage/, framework Vision de macOS) dans un processus à part,
// puis posé au-dessus du plan, sur la piste « Sujets détourés » : un élément Motion glissé entre les
// deux passe derrière le sujet. L'appel attend dans une boucle d'événements, comme voix_off_generer :
// l'interface et la lecture continuent pendant le calcul.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "koracutvisible.h"

#include "bin/bin.h"
#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "effects/effectsrepository.hpp"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "macros.hpp"
#include "profiles/profilemodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "xml/xml.hpp"

#include <KLocalizedString>
#include <cstdlib>
#include <mlt++/MltAnimation.h>
#include <mlt++/MltProperties.h>
#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

using namespace KoraCutAgentToolsPrivate;

namespace {

const QLatin1String kMotionTrack("kdenlive:koracut_motion");
const QLatin1String kMotionAnimations("*animations");
const QLatin1String kMotionTransitions("*transitions");
// Au-delà, le calcul dépasse la patience d'un appel et le fichier pèse trop (5 à 10 Mo par seconde).
const int kMaxSeconds = 30;

// Le programme : celui des essais (KORACUT_DETOURER), celui livré à côté de l'application, celui du PATH.
QString detourerProgram()
{
    const QString forced = qEnvironmentVariable("KORACUT_DETOURER");
    if (!forced.isEmpty()) {
        return QFileInfo(forced).isExecutable() ? forced : QString();
    }
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/koracut-detourer");
    if (QFileInfo(beside).isExecutable()) {
        return beside;
    }
    return QStandardPaths::findExecutable(QStringLiteral("koracut-detourer"));
}

// Le dossier « detourages » du projet ; vide tant que le projet n'est pas sur le disque.
QString cutoutFolder()
{
    const QString forced = qEnvironmentVariable("KORACUT_DETOURAGE_DOSSIER");
    if (!forced.isEmpty()) {
        return forced; // essais
    }
    KdenliveDoc *doc = pCore->currentDoc();
    if (!doc || doc->url().isEmpty() || !QFileInfo::exists(doc->url().toLocalFile())) {
        return QString();
    }
    return QFileInfo(doc->url().toLocalFile()).absolutePath() + QStringLiteral("/detourages");
}

bool rangeFree(const Timeline &timeline, int trackId, int position, int length)
{
    for (int id : timeline->getItemsInRange(trackId, position, position + std::max(1, length) - 1, false)) {
        if (timeline->isClip(id)) {
            return false;
        }
    }
    return true;
}

QString trackName(const Timeline &timeline, int trackId)
{
    const QString name = timeline->getTrackProperty(trackId, QStringLiteral("kdenlive:track_name")).toString();
    return name.isEmpty() ? trackLabel(timeline, trackId) : name;
}

// La piste « Sujets détourés » : au-dessus du plan et des pistes « Animations », sous les « Transitions ».
// Une piste déjà marquée (ou déjà nommée ainsi) et libre à cet endroit est reprise, sinon elle est créée.
int subjectTrack(const Timeline &timeline, int clipTrack, int position, int length, Fun &undo, Fun &redo, bool &created)
{
    created = false;
    const QString property = KoraCutAgentTools::subjectTrackProperty();
    const QString baseName = i18nc("@item track name, track of cut-out subjects placed above their shot", "Cut-out subjects");
    int floor = timeline->getTrackPosition(clipTrack);
    int lowestTransition = -1;
    int count = 0;
    const QList<int> videoTracks = timeline->getTracksIds(false);
    for (int tid : videoTracks) {
        const QString mark = timeline->getTrackProperty(tid, kMotionTrack).toString();
        if (mark == kMotionAnimations) {
            floor = std::max(floor, timeline->getTrackPosition(tid));
        } else if (mark == kMotionTransitions && (lowestTransition < 0 || timeline->getTrackPosition(tid) < timeline->getTrackPosition(lowestTransition))) {
            lowestTransition = tid;
        }
    }
    int found = -1;
    int adopted = -1;
    for (int tid : videoTracks) {
        const bool marked = !timeline->getTrackProperty(tid, property).toString().isEmpty();
        if (marked) {
            ++count;
        }
        if (timeline->getTrackPosition(tid) <= floor || timeline->trackIsLocked(tid) || !rangeFree(timeline, tid, position, length)) {
            continue;
        }
        if (marked) {
            if (found < 0 || timeline->getTrackPosition(tid) < timeline->getTrackPosition(found)) {
                found = tid;
            }
            continue;
        }
        // Une piste que la personne (ou le plan du projet) a déjà nommée ainsi est prise telle quelle.
        const QString name = timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString().trimmed();
        if (timeline->getTrackProperty(tid, kMotionTrack).toString().isEmpty() &&
            (name.compare(baseName, Qt::CaseInsensitive) == 0 || name.compare(QStringLiteral("Cut-out subjects"), Qt::CaseInsensitive) == 0) &&
            (adopted < 0 || timeline->getTrackPosition(tid) < timeline->getTrackPosition(adopted))) {
            adopted = tid;
        }
    }
    if (found >= 0) {
        return found;
    }
    if (adopted >= 0) {
        Fun mark = [timeline, adopted, property]() {
            if (timeline->isTrack(adopted)) {
                timeline->setTrackProperty(adopted, property, QStringLiteral("1"));
            }
            return true;
        };
        Fun unmark = [timeline, adopted, property]() {
            if (timeline->isTrack(adopted)) {
                timeline->setTrackProperty(adopted, property, QString());
            }
            return true;
        };
        mark();
        UPDATE_UNDO_REDO_NOLOCK(mark, unmark, undo, redo);
        return adopted;
    }
    const QString name = count > 0 ? i18nc("@item track name, second track of cut-out subjects and more: name number", "%1 %2", baseName, count + 1) : baseName;
    // Sous la piste « Transitions » quand elle est au-dessus du plan, tout en haut sinon.
    const int insertAt = (lowestTransition >= 0 && timeline->getTrackPosition(lowestTransition) > floor) ? timeline->getTrackPosition(lowestTransition) : -1;
    int tid = -1;
    if (!timeline->requestTrackInsertion(insertAt, tid, name, false, undo, redo)) {
        return -1;
    }
    Fun mark = [timeline, tid, property]() {
        if (timeline->isTrack(tid)) {
            timeline->setTrackProperty(tid, property, QStringLiteral("1"));
        }
        return true;
    };
    mark();
    const Fun previousRedo = redo;
    redo = [previousRedo, mark]() { return previousRedo() && mark(); };
    created = true;
    return tid;
}

// Une animation du plan (« 50=… ;80=… », en images de la source) ramenée au début du sujet détouré, dont
// l'entrée est zéro : MLT la recoupe sur [in, out] et pose une image clé à chaque bord. Le décalage de
// EffectStackModel::fromXml ne sert pas ici : il perd les images clés quand l'une d'elles précède l'entrée.
QString animationFromZero(const std::shared_ptr<EffectItemModel> &effect, const QString &value, ParamType type, int in, int length)
{
    if (!value.contains(QLatin1Char('=')) || type == ParamType::Roto_spline) {
        return value;
    }
    Mlt::Properties props;
    effect->passProperties(props);
    props.set("key", value.toUtf8().constData());
    // Une lecture pour que MLT analyse l'animation.
    if (type == ParamType::AnimatedRect || type == ParamType::AnimatedFakeRect || type == ParamType::AnimatedPoint || type == ParamType::AnimatedFakePoint) {
        (void)props.anim_get_rect("key", in);
    } else {
        (void)props.anim_get_double("key", in);
    }
    Mlt::Animation animation = props.get_animation("key");
    if (!animation.is_valid() || animation.key_count() == 0) {
        return value;
    }
    char *cut = animation.serialize_cut(mlt_time_frames, in, in + std::max(1, length) - 1);
    if (!cut) {
        return value;
    }
    const QString result = QString::fromUtf8(cut);
    free(cut);
    return result.isEmpty() ? value : result;
}

// Recopie l'étalonnage et le Transformer du plan sur son sujet détouré : les deux images se
// superposent alors au pixel près, images clés comprises.
void copyLook(const Timeline &timeline, int from, int to, Fun &undo, Fun &redo, QJsonArray &copied, QJsonArray &left)
{
    const std::shared_ptr<EffectStackModel> source = timeline->getClipEffectStackModel(from);
    const std::shared_ptr<EffectStackModel> target = timeline->getClipEffectStackModel(to);
    if (!source || !target) {
        return;
    }
    const int in = timeline->getClipIn(from);
    const int length = timeline->getClipPlaytime(from);
    QDomDocument doc;
    QDomElement xml = source->toXml(doc);
    // Les animations sont ramenées à zéro ici : fromXml n'a plus rien à décaler.
    xml.setAttribute(QStringLiteral("parentIn"), timeline->getClipIn(to));
    const QDomNodeList nodes = xml.elementsByTagName(QStringLiteral("effect"));
    QList<QDomElement> drop;
    for (int i = 0; i < nodes.count(); ++i) {
        QDomElement node = nodes.item(i).toElement();
        // toXml écrit les effets dans l'ordre de la pile : le nœud i est l'effet de la ligne i.
        const auto effect = i < source->rowCount() ? std::static_pointer_cast<EffectItemModel>(source->getEffectStackRow(i)) : nullptr;
        const QString id = node.attribute(QStringLiteral("id"));
        const bool off = Xml::getXmlProperty(node, QStringLiteral("disable")).toInt() == 1;
        const bool wanted = id == QLatin1String("qtblend") || isColourEffect(id);
        if (off || !wanted) {
            drop << node;
            // Un effet éteint ne change rien à l'image, un fondu ou un son n'ont rien à faire ici.
            if (!off && !id.startsWith(QLatin1String("fade")) && !EffectsRepository::get()->isAudioEffect(id)) {
                left.append(id);
            }
            continue;
        }
        // Un effet intégré se colle comme un effet ordinaire : c'est ce chemin qui décale les images clés.
        Xml::removeXmlProperty(node, QStringLiteral("kdenlive:builtin"));
        Xml::removeXmlProperty(node, QStringLiteral("kdenlive:hiddenbuiltin"));
        if (effect && effect->getAssetId() == id) {
            const QMap<QString, std::pair<ParamType, bool>> animated = effect->getKeyframableParameters();
            const QDomNodeList properties = node.elementsByTagName(QStringLiteral("property"));
            for (int j = 0; j < properties.count(); ++j) {
                QDomElement property = properties.item(j).toElement();
                const QString name = property.attribute(QStringLiteral("name"));
                if (animated.contains(name)) {
                    const QString moved = animationFromZero(effect, property.text(), animated.value(name).first, in, length);
                    Xml::setXmlProperty(node, name, moved);
                }
            }
        }
        copied.append(id);
    }
    for (QDomElement &node : drop) {
        xml.removeChild(node);
    }
    if (!copied.isEmpty() && !target->fromXml(xml, undo, redo)) {
        // Rien n'a été posé : on le dit plutôt que de laisser croire à une superposition exacte.
        for (const QJsonValue &v : std::as_const(copied)) {
            left.append(v);
        }
        copied = QJsonArray();
    }
}

} // namespace

namespace KoraCutAgentTools {

QString subjectTrackProperty()
{
    return QStringLiteral("kdenlive:koracut_detourage");
}

} // namespace KoraCutAgentTools

namespace KoraCutAgentToolsPrivate {

QJsonObject placeSubjectCutout(const Timeline &timeline, int clipId, const QString &file, int trackId)
{
    auto model = pCore ? pCore->projectItemModel() : nullptr;
    if (!model || !pCore->currentDoc() || !timeline) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    if (!timeline->isClip(clipId)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(clipId),
                     QStringLiteral("Relis etat_timeline : les identifiants changent après une coupe ou une annulation."));
    }
    const int clipTrack = timeline->getClipTrackId(clipId);
    const int position = timeline->getClipPosition(clipId);
    const int length = timeline->getClipPlaytime(clipId);
    if (trackId >= 0) {
        if (QJsonObject e = trackIdCheck(timeline, trackId); !e.isEmpty()) {
            return e;
        }
        if (timeline->isAudioTrack(trackId)) {
            return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("La piste %1 est une piste audio : un sujet détouré se pose sur une piste vidéo.").arg(trackLabel(timeline, trackId)),
                         QStringLiteral("Laisse piste_id vide : le sujet va sur la piste « Sujets détourés », créée si besoin."));
        }
        if (timeline->getTrackPosition(trackId) <= timeline->getTrackPosition(clipTrack)) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("La piste %1 n'est pas au-dessus du plan : le sujet détouré serait caché.").arg(trackLabel(timeline, trackId)),
                         QStringLiteral("Donne une piste vidéo au-dessus du plan, ou laisse piste_id vide."));
        }
        if (!rangeFree(timeline, trackId, position, length)) {
            return error(QStringLiteral("place_occupee"), QStringLiteral("La piste %1 est déjà occupée au-dessus du plan.").arg(trackLabel(timeline, trackId)),
                         QStringLiteral("Laisse piste_id vide : une piste libre est trouvée ou créée."));
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    // Le dossier « Détourages » du chutier, créé dans la même étape s'il n'existe pas.
    QString folderId = model->getRootFolder()->clipId();
    const QString folderName = i18nc("@item bin folder for cut-out subject videos", "Cutouts");
    const QString existingFolder = model->getFolderIdByName(folderName);
    if (!existingFolder.isEmpty() && model->getFolderByBinId(existingFolder)) {
        folderId = existingFolder;
    } else {
        QString newId;
        if (model->requestAddFolder(newId, folderName, folderId, undo, redo)) {
            folderId = newId;
        }
    }
    // Un détourage déjà dans le chutier (même fichier) est repris, pas importé une seconde fois.
    QString binId;
    const QStringList known = model->getClipByUrl(QFileInfo(file));
    if (!known.isEmpty()) {
        binId = known.first();
        // Un fichier refait sur le disque est rechargé par le chutier : on lui laisse le temps. S'il ne
        // revient pas, le fichier entre une seconde fois plutôt que de faire échouer la pose.
        const auto existing = model->getClipByBinID(binId);
        QElapsedTimer waiting;
        waiting.start();
        while (existing && !existing->statusReady() && waiting.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        if (!existing || !existing->statusReady()) {
            binId.clear();
        }
    }
    if (binId.isEmpty()) {
        bool ready = false;
        binId = ClipCreator::createClipFromFile(file, folderId, model, undo, redo, [&ready](const QString &) { ready = true; });
        if (binId.isEmpty() || binId == QLatin1String("-1")) {
            undo();
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le fichier %1 n'a pas pu être importé.").arg(QFileInfo(file).fileName()));
        }
        if (pCore->bin()) {
            pCore->bin()->shouldCheckProfile = false;
        }
        QElapsedTimer timer;
        timer.start();
        while (!ready && timer.elapsed() < 30000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    }
    const auto binClip = model->getClipByBinID(binId);
    if (!binClip || !binClip->statusReady()) {
        undo();
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le détourage n'a pas pu être lu : %1").arg(file));
    }
    // Le plan a pu bouger pendant l'import.
    if (!timeline->isClip(clipId) || timeline->getClipPosition(clipId) != position || timeline->getClipPlaytime(clipId) != length) {
        undo();
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le plan a changé pendant le détourage ; le fichier est gardé : %1").arg(file),
                     QStringLiteral("Rappelle detourer_sujet : un détourage déjà calculé est repris tel quel."));
    }
    int track = trackId;
    bool trackCreated = false;
    if (track < 0) {
        track = subjectTrack(timeline, clipTrack, position, length, undo, redo, trackCreated);
        if (track < 0) {
            undo();
            return error(QStringLiteral("non_disponible"), QStringLiteral("La piste « Sujets détourés » n'a pas pu être créée."));
        }
    }
    int newClip = -1;
    if (!timeline->requestClipInsertion(binId, track, position, newClip, false, true, false, undo, redo) || newClip < 0) {
        undo();
        return error(QStringLiteral("place_occupee"), QStringLiteral("Pas de place au-dessus du plan sur la piste %1.").arg(trackName(timeline, track)),
                     QStringLiteral("Libère la piste à cet endroit, ou donne une autre piste vidéo avec piste_id."));
    }
    // Même durée que le plan, à l'image près.
    if (timeline->getClipPlaytime(newClip) > length) {
        int size = length;
        timeline->requestItemResize(newClip, size, true, false, undo, redo);
    }
    QJsonArray copied;
    QJsonArray left;
    copyLook(timeline, clipId, newClip, undo, redo, copied, left);
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Cut out subject"));
    QJsonObject out{{QStringLiteral("ok"), true},
                    {QStringLiteral("clip_id"), newClip},
                    {QStringLiteral("clip_source_id"), clipId},
                    {QStringLiteral("media_id"), binId},
                    {QStringLiteral("piste_id"), track},
                    {QStringLiteral("piste"), trackName(timeline, track)},
                    {QStringLiteral("piste_creee"), trackCreated},
                    {QStringLiteral("position"), time(position)},
                    {QStringLiteral("duree"), time(timeline->getClipPlaytime(newClip))},
                    {QStringLiteral("fichier"), file},
                    {QStringLiteral("effets_copies"), copied}};
    if (!left.isEmpty()) {
        out.insert(QStringLiteral("effets_non_copies"), left);
        out.insert(QStringLiteral("note"), QStringLiteral("Seuls l'étalonnage et le Transformer sont recopiés. Les autres effets du plan qui déplacent ou déforment l'image "
                                                          "sont à poser aussi sur le sujet (ajouter_effet), sinon il ne se superpose plus."));
    }
    if (timeline->getClipPlaytime(newClip) < length) {
        out.insert(QStringLiteral("avertissement"), QStringLiteral("Le détourage est plus court que le plan de %1 image(s).").arg(length - timeline->getClipPlaytime(newClip)));
    }
    return out;
}

} // namespace KoraCutAgentToolsPrivate

namespace {

QJsonObject detourerSujet(const Timeline &timeline, const Args &args)
{
    const int clipId = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (!timeline->isClip(clipId)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(clipId),
                     QStringLiteral("Relis etat_timeline : les identifiants changent après une coupe ou une annulation."));
    }
    const QString subject = args.value(QStringLiteral("sujet")).toString();
    if (subject != QLatin1String("personne") && subject != QLatin1String("premier_plan")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("sujet vaut « personne » ou « premier_plan »."),
                     QStringLiteral("personne : une ou plusieurs personnes. premier_plan : ce qui est devant (une main qui tient un téléphone, un produit)."));
    }
    const double feather = std::clamp(args.value(QStringLiteral("adoucir")).toDouble(2.0), 0.0, 20.0);
    const int clipTrack = timeline->getClipTrackId(clipId);
    if (timeline->isAudioTrack(clipTrack)) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Le clip %1 est un son : il n'a pas d'image à détourer.").arg(clipId),
                     QStringLiteral("Donne l'id du clip vidéo du plan (champ k = « v » dans etat_timeline)."));
    }
    const auto binClip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(clipId));
    const QString source = binClip ? binClip->url() : QString();
    if (!binClip || (binClip->clipType() != ClipType::AV && binClip->clipType() != ClipType::Video) || !QFileInfo::exists(source)) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Le clip %1 n'est pas tiré d'un fichier vidéo.").arg(clipId),
                     QStringLiteral("Le détourage marche sur un plan filmé, pas sur une image, un titre, une couleur ou une séquence."));
    }
    if (!qFuzzyCompare(timeline->getClipSpeed(clipId), 1.0)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Remets la vitesse de ce plan à 100 % avant de le détourer."),
                     QStringLiteral("changer_vitesse_clip avec vitesse_pourcent 100, puis detourer_sujet."));
    }
    const int trackId = args.contains(QStringLiteral("piste_id")) ? args.value(QStringLiteral("piste_id")).toInt(-1) : -1;
    if (args.contains(QStringLiteral("piste_id"))) {
        if (QJsonObject e = trackIdCheck(timeline, trackId); !e.isEmpty()) {
            return e;
        }
    }
    const double fps = projectFps();
    const int in = timeline->getClipIn(clipId);
    const int position = timeline->getClipPosition(clipId);
    const int length = timeline->getClipPlaytime(clipId);
    if (length > kMaxSeconds * fps) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Ce plan dure plus de %1 secondes : le détourage serait long à calculer et lourd sur le disque.").arg(kMaxSeconds),
                     QStringLiteral("Coupe le plan autour du moment où un élément passe derrière le sujet (couper_clip), puis détoure ce morceau seul."));
    }
    const QString program = detourerProgram();
    if (program.isEmpty()) {
#ifdef Q_OS_MACOS
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le programme koracut-detourer est introuvable."),
                     QStringLiteral("Il se compile depuis Sources/koracut-detourage (swiftc -O) et se range à côté de l'application."));
#else
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le détourage du sujet n'existe pour l'instant que sur macOS."),
                     QStringLiteral("Dis-le à la personne. À la place : un masque dessiné à la main, ou un fond vert au tournage."));
#endif
    }
    KdenliveDoc *doc = pCore->currentDoc();
    const QString folder = cutoutFolder();
    if (!doc || folder.isEmpty()) {
        return error(QStringLiteral("projet_absent"), QStringLiteral("Le projet n'est pas encore enregistré : le détourage n'a pas de dossier où aller."),
                     QStringLiteral("Enregistre le projet d'abord, puis rappelle detourer_sujet."));
    }
    if (!QDir().mkpath(folder)) {
        return error(QStringLiteral("ecriture_impossible"), QStringLiteral("Le dossier %1 n'a pas pu être créé.").arg(folder));
    }
    // Le nom dit tout ce qui fait le résultat : le même appel reprend le fichier au lieu de recalculer.
    QString base = QFileInfo(source).completeBaseName();
    base.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    const std::unique_ptr<ProfileModel> &profile = pCore->getCurrentProfile();
    const QString rate = profile->frame_rate_den() == 1 ? QString::number(profile->frame_rate_num())
                                                         : QStringLiteral("%1/%2").arg(profile->frame_rate_num()).arg(profile->frame_rate_den());
    const QString output = QStringLiteral("%1/%2 - %3 %4-%5 a%6 i%7.mov")
                               .arg(folder, base.left(60), subject == QLatin1String("personne") ? QStringLiteral("personne") : QStringLiteral("premier plan"))
                               .arg(in)
                               .arg(in + length)
                               .arg(qRound(feather * 10))
                               .arg(QString(rate).replace(QLatin1Char('/'), QLatin1Char('-')));
    double seconds = 0.;
    bool reused = QFileInfo::exists(output) && QFileInfo(output).size() > 0;
    if (!reused) {
        QProcess process;
        process.setProgram(program);
        process.setArguments({source, QStringLiteral("--debut"), QString::number(in / fps, 'f', 6), QStringLiteral("--duree"), QString::number(length / fps, 'f', 6),
                              QStringLiteral("--sujet"), subject, QStringLiteral("--ips"), rate, QStringLiteral("--adoucir"), QString::number(feather, 'f', 1),
                              QStringLiteral("--sortie"), output});
        QEventLoop loop;
        QByteArray errors;
        const QString label = QFileInfo(source).completeBaseName();
        QObject::connect(&process, &QProcess::readyReadStandardError, &loop, [&process, &errors, label]() {
            errors += process.readAllStandardError();
            // « koracut-detourer : 45/60 images » : la dernière ligne dit où en est le calcul.
            static const QRegularExpression counter(QStringLiteral("(\\d+)/(\\d+) images"));
            QRegularExpressionMatchIterator it = counter.globalMatch(QString::fromUtf8(errors.right(200)));
            QRegularExpressionMatch last;
            while (it.hasNext()) {
                last = it.next();
            }
            if (last.hasMatch() && last.captured(2).toInt() > 0) {
                const int percent = 100 * last.captured(1).toInt() / last.captured(2).toInt();
                KoraCutVisible::self()->progressLine(QString(), i18nc("@info:status subject cut-out progress, %1 percent, %2 clip name", "Cutting out the subject: %1% · %2", percent, label));
            }
        });
        QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
        QObject::connect(&process, &QProcess::errorOccurred, &loop, [&loop](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                loop.quit();
            }
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        // Dix fois la durée du plan, et au moins une minute : trois fois plus que le calcul mesuré.
        timeout.start(std::max(60, int(10 * length / fps)) * 1000);
        const QPointer<KdenliveDoc> guard(doc);
        QElapsedTimer timer;
        timer.start();
        KoraCutVisible::self()->progressLine(QString(), i18nc("@info:status subject cut-out progress, %1 percent, %2 clip name", "Cutting out the subject: %1% · %2", 0, label));
        process.start();
        loop.exec();
        seconds = timer.elapsed() / 1000.;
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(3000);
            QFile::remove(output);
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le détourage n'a pas abouti dans le temps prévu."),
                         QStringLiteral("Essaie sur un plan plus court."));
        }
        if (process.error() == QProcess::FailedToStart) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le programme koracut-detourer ne se lance pas : %1").arg(program));
        }
        const int code = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
        if (code == 3) {
            const QString what = subject == QLatin1String("personne") ? QStringLiteral("Aucune personne n'a été trouvée dans ce plan.")
                                                                         : QStringLiteral("Rien ne se détache du fond dans ce plan.");
            return error(QStringLiteral("sujet_introuvable"), what,
                         subject == QLatin1String("personne") ? QStringLiteral("Essaie sujet premier_plan si le sujet est une main ou un objet.")
                                                              : QStringLiteral("Essaie sujet personne, ou un autre moment du plan."));
        }
        if (code != 0 || !QFileInfo::exists(output)) {
            QFile::remove(output);
            const QString detail = QString::fromUtf8(errors).trimmed().section(QLatin1Char('\n'), -1).left(300);
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le détourage a échoué (code %1). %2").arg(code).arg(detail),
                         code == 4 ? QStringLiteral("ffmpeg est nécessaire (sur Mac : brew install ffmpeg).") : QString());
        }
        if (!guard || pCore->currentDoc() != doc) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le projet a été fermé pendant le détourage ; le fichier est gardé : %1").arg(output));
        }
        if (!timeline->isClip(clipId) || timeline->getClipIn(clipId) != in || timeline->getClipPlaytime(clipId) != length || timeline->getClipPosition(clipId) != position) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le plan a changé pendant le détourage ; le fichier est gardé : %1").arg(output),
                         QStringLiteral("Relis etat_timeline puis rappelle detourer_sujet sur le plan."));
        }
    }
    QJsonObject out = placeSubjectCutout(timeline, clipId, output, trackId);
    if (!out.value(QStringLiteral("ok")).toBool()) {
        return out;
    }
    out.insert(QStringLiteral("sujet"), subject);
    out.insert(QStringLiteral("duree_calcul_s"), std::round(seconds * 10.) / 10.);
    out.insert(QStringLiteral("deja_calcule"), reused);
    out.insert(QStringLiteral("poids_mo"), std::round(QFileInfo(output).size() / 1048576. * 10.) / 10.);
    return out;
}

} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> detourageTools()
{
    return {
        {QStringLiteral("detourer_sujet"), detourerSujet},
    };
}

} // namespace KoraCutAgentToolsPrivate
