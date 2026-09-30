/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Chantier 3, lot A (24 septembre 2026) : les outils de montage qui manquaient. Titre, fondu
// enchaîné, effets (pose, réglage, retrait), images clés, transformation. Contrat : montage.json
// et le banc (Agents in-app/banc/src/outils/montage.ts). Règle : un appel = une étape d'annulation.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"

#include "assets/keyframes/model/keyframemodel.hpp"
#include "assets/keyframes/model/keyframemodellist.hpp"
#include "bin/model/subtitlemodel.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "effects/effectsrepository.hpp"
#include "mainwindow.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "profiles/profilemodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "xml/xml.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>

#include <algorithm>
#include <cmath>

using namespace KoraCutAgentToolsPrivate;

namespace KoraCutAgentToolsPrivate {

namespace {

bool isFadeEffect(const QString &id)
{
    return id == QLatin1String("fadein") || id == QLatin1String("fadeout") || id == QLatin1String("fade_from_black") || id == QLatin1String("fade_to_black");
}

// Les 14 interpolations de KeyframeType, nommées comme dans montage.json.
const QList<QPair<QString, KeyframeType::KeyframeEnum>> &interpolations()
{
    static const QList<QPair<QString, KeyframeType::KeyframeEnum>> list = {
        {QStringLiteral("lineaire"), KeyframeType::Linear},
        {QStringLiteral("discrete"), KeyframeType::Discrete},
        {QStringLiteral("courbe"), KeyframeType::Curve},
        {QStringLiteral("courbe_douce"), KeyframeType::CurveSmooth},
        {QStringLiteral("rebond_entree"), KeyframeType::BounceIn},
        {QStringLiteral("rebond_sortie"), KeyframeType::BounceOut},
        {QStringLiteral("cubique_entree"), KeyframeType::CubicIn},
        {QStringLiteral("cubique_sortie"), KeyframeType::CubicOut},
        {QStringLiteral("exponentielle_entree"), KeyframeType::ExponentialIn},
        {QStringLiteral("exponentielle_sortie"), KeyframeType::ExponentialOut},
        {QStringLiteral("circulaire_entree"), KeyframeType::CircularIn},
        {QStringLiteral("circulaire_sortie"), KeyframeType::CircularOut},
        {QStringLiteral("elastique_entree"), KeyframeType::ElasticIn},
        {QStringLiteral("elastique_sortie"), KeyframeType::ElasticOut},
    };
    return list;
}

QString interpolationName(KeyframeType::KeyframeEnum type)
{
    for (const auto &pair : interpolations()) {
        if (pair.second == type) {
            return pair.first;
        }
    }
    return QStringLiteral("lineaire");
}

// Toutes les images clés d'un paramètre, dans l'ordre (l'accès à la liste est réservé au modèle).
QList<Keyframe> keyframesOf(const KeyframeModel &model)
{
    QList<Keyframe> list;
    GenTime cursor(-1);
    bool ok = true;
    while (ok) {
        const Keyframe next = model.getNextKeyframe(cursor, &ok);
        if (!ok) {
            break;
        }
        list << next;
        cursor = next.first;
    }
    return list;
}

} // namespace

QString variantText(const QVariant &value)
{
    if (value.typeId() == QMetaType::Double) {
        return QString::number(value.toDouble(), 'g', 10);
    }
    return value.toString();
}

std::vector<std::shared_ptr<EffectItemModel>> visibleEffects(const std::shared_ptr<EffectStackModel> &stack)
{
    std::vector<std::shared_ptr<EffectItemModel>> effects;
    if (!stack) {
        return effects;
    }
    for (int row = 0; row < stack->rowCount(); ++row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (!effect || isFadeEffect(effect->getAssetId())) {
            continue;
        }
        // Un effet intégré désactivé n'existe pas aux yeux de l'agent.
        if (effect->isBuiltIn() && !effect->isAssetEnabled()) {
            continue;
        }
        effects.push_back(effect);
    }
    return effects;
}

std::shared_ptr<EffectStackModel> stackFor(const Timeline &timeline, const Args &args, QJsonObject &failure, bool *isClip, int *itemId)
{
    const bool hasClip = args.contains(QStringLiteral("clip_id"));
    const bool hasTrack = args.contains(QStringLiteral("piste_id"));
    if (hasClip == hasTrack) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("Il faut donner clip_id ou piste_id, et un seul des deux."),
                        QStringLiteral("Pour un effet sur un clip : clip_id. Pour toute une piste : piste_id. Pour le master : piste_id -1."));
        return nullptr;
    }
    if (isClip) {
        *isClip = hasClip;
    }
    if (hasClip) {
        const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
        if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
            failure = e;
            return nullptr;
        }
        if (itemId) {
            *itemId = cid;
        }
        return timeline->getClipEffectStackModel(cid);
    }
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-2);
    if (itemId) {
        *itemId = tid;
    }
    if (tid == -1) {
        return timeline->getMasterEffectStackModel();
    }
    if (QJsonObject e = trackIdCheck(timeline, tid); !e.isEmpty()) {
        failure = e;
        return nullptr;
    }
    return timeline->getTrackEffectStackModel(tid);
}

QJsonObject parametersJson(const std::shared_ptr<EffectItemModel> &effect)
{
    QJsonObject params;
    const QVector<QPair<QString, QVariant>> all = effect->getAllParameters();
    for (const auto &pair : all) {
        params.insert(pair.first, variantText(pair.second));
    }
    return params;
}

QJsonArray effectsJson(const std::shared_ptr<EffectStackModel> &stack, bool complete, int itemIn)
{
    QJsonArray list;
    const auto effects = visibleEffects(stack);
    const double fps = projectFps();
    for (size_t i = 0; i < effects.size(); ++i) {
        const auto &effect = effects.at(i);
        QJsonObject o{{QStringLiteral("i"), int(i)}, {QStringLiteral("id"), effect->getAssetId()}};
        if (!effect->isAssetEnabled()) {
            o.insert(QStringLiteral("off"), true);
        }
        if (complete) {
            o.insert(QStringLiteral("p"), parametersJson(effect));
            QJsonObject kf;
            if (const std::shared_ptr<KeyframeModelList> models = effect->getKeyframeModel()) {
                for (const auto &param : models->getAllParameters()) {
                    const QString name = effect->data(param.first, AssetParameterModel::NameRole).toString();
                    // Un paramètre sans « = » dans sa valeur n'est pas animé : pas d'images clés à montrer.
                    if (!effect->data(param.first, AssetParameterModel::ValueRole).toString().contains(QLatin1Char('='))) {
                        continue;
                    }
                    QJsonArray keys;
                    for (const Keyframe &key : keyframesOf(*param.second)) {
                        keys.append(QJsonArray{key.first.frames(fps) - itemIn, variantText(param.second->getInterpolatedValue(key.first)), interpolationName(key.second)});
                    }
                    if (!keys.isEmpty()) {
                        kf.insert(name, keys);
                    }
                }
            }
            if (!kf.isEmpty()) {
                o.insert(QStringLiteral("kf"), kf);
            }
        }
        list.append(o);
    }
    return list;
}

// Le rang visible d'un effet dans sa pile, -1 s'il n'y est plus.
int effectRank(const std::shared_ptr<EffectStackModel> &stack, const std::shared_ptr<EffectItemModel> &effect)
{
    const auto effects = visibleEffects(stack);
    for (size_t i = 0; i < effects.size(); ++i) {
        if (effects.at(i) == effect) {
            return int(i);
        }
    }
    return -1;
}

std::shared_ptr<EffectItemModel> findVisibleEffect(const std::shared_ptr<EffectStackModel> &stack, const QString &effectId)
{
    for (const auto &effect : visibleEffects(stack)) {
        if (effect->getAssetId() == effectId) {
            return effect;
        }
    }
    return nullptr;
}

QStringList parameterNames(const std::shared_ptr<EffectItemModel> &effect)
{
    QStringList names;
    const QVector<QPair<QString, QVariant>> all = effect->getAllParameters();
    for (const auto &pair : all) {
        names << pair.first;
    }
    return names;
}

// Pose l'effet @p effectId sur la pile avec ses paramètres, dans undo/redo. Si un effet intégré caché
// du même identifiant existe déjà (Kdenlive refuse alors le doublon), c'est lui qui est activé.
std::shared_ptr<EffectItemModel> appendEffect(const std::shared_ptr<EffectStackModel> &stack, const QString &effectId, const QMap<QString, QString> &params, Fun &undo,
                                              Fun &redo)
{
    for (int row = 0; row < stack->rowCount(); ++row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect && effect->isBuiltIn() && !effect->isAssetEnabled() && effect->getAssetId() == effectId) {
            effect->markEnabled(true, undo, redo);
            QVector<QPair<QString, QVariant>> before, after;
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                before.append({it.key(), effect->getParamFromName(it.key())});
                after.append({it.key(), it.value()});
            }
            Fun local_redo = [effect, after]() {
                effect->setParameters(after);
                return true;
            };
            Fun local_undo = [effect, before]() {
                effect->setParameters(before);
                return true;
            };
            local_redo();
            UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
            return effect;
        }
    }
    const int rowsBefore = stack->rowCount();
    if (!stack->appendEffectWithParams(effectId, params, undo, redo) || stack->rowCount() <= rowsBefore) {
        return nullptr;
    }
    // Le nouvel effet est le dernier rang qui porte cet identifiant.
    std::shared_ptr<EffectItemModel> added;
    for (int row = stack->rowCount() - 1; row >= 0; --row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect && effect->getAssetId() == effectId) {
            added = effect;
            break;
        }
    }
    if (added) {
        // Les paramètres ont été posés sur le filtre MLT ; le modèle doit les connaître aussi (ils
        // restent sur l'objet, que l'annulation retire et remet tel quel).
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            added->setParameter(it.key(), it.value());
        }
    }
    return added;
}

// Change des paramètres dans undo/redo (valeurs précédentes relues sur le modèle).
void setParametersWithUndo(const std::shared_ptr<EffectItemModel> &effect, const QMap<QString, QString> &params, Fun &undo, Fun &redo)
{
    QVector<QPair<QString, QVariant>> before, after;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        before.append({it.key(), effect->getParamFromName(it.key())});
        after.append({it.key(), it.value()});
    }
    std::weak_ptr<EffectItemModel> weak = effect;
    Fun local_redo = [weak, after]() {
        if (auto e = weak.lock()) {
            e->setParameters(after);
        }
        return true;
    };
    Fun local_undo = [weak, before]() {
        if (auto e = weak.lock()) {
            e->setParameters(before);
        }
        return true;
    };
    local_redo();
    UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
}

} // namespace KoraCutAgentToolsPrivate

namespace {

// L'effet au rang visible @p index, ou une erreur `id_inconnu`.
std::shared_ptr<EffectItemModel> effectAt(const std::shared_ptr<EffectStackModel> &stack, int index, QJsonObject &failure)
{
    const auto effects = visibleEffects(stack);
    if (index < 0 || size_t(index) >= effects.size()) {
        failure = error(QStringLiteral("id_inconnu"), QStringLiteral("Il n'y a pas d'effet au rang %1 (la pile en compte %2).").arg(index).arg(effects.size()),
                        QStringLiteral("Relis etat_timeline : les rangs baissent quand un effet est retiré."));
        return nullptr;
    }
    return effects.at(size_t(index));
}

// ---------------------------------------------------------------------------------------------
// ajouter_titre

struct TitleStyle
{
    int size;          // hauteur du texte, en pixels d'un projet de 1080 de haut
    double x;          // position horizontale, -1 = centré
    double y;          // position verticale, fraction de la hauteur du projet
    int alignment;     // Qt::AlignLeft (1) ou Qt::AlignHCenter (4)
    bool background;   // fond plein
};

TitleStyle titleStyle(const QString &name)
{
    if (name == QLatin1String("bas_ecran")) {
        return {48, 0.06, 0.78, 1, false};
    }
    if (name == QLatin1String("carton")) {
        return {64, -1, -1, 4, true};
    }
    if (name == QLatin1String("sous_titre_simple")) {
        return {40, -1, 0.86, 4, false};
    }
    return {96, -1, -1, 4, false}; // ouverture
}

QString colorText(const QString &hex, int alpha)
{
    const QColor c(hex);
    return QStringLiteral("%1,%2,%3,%4").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

QJsonObject ajouterTitre(const Timeline &timeline, const Args &args)
{
    const QString text = args.value(QStringLiteral("texte")).toString().replace(QLatin1String("\\n"), QLatin1String("\n"));
    if (text.trimmed().isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("texte est vide."));
    }
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
    if (QJsonObject e = trackIdCheck(timeline, tid); !e.isEmpty()) {
        return e;
    }
    if (timeline->isAudioTrack(tid)) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("%1 est une piste audio : un titre va sur une piste vidéo.").arg(trackLabel(timeline, tid)),
                     QStringLiteral("Choisis une piste vidéo située au-dessus de l'image à titrer."));
    }
    QJsonObject failure;
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    const int duration = requiredTime(args, QStringLiteral("duree"), failure);
    if (duration < 1) {
        return failure.isEmpty() ? error(QStringLiteral("entree_invalide"), QStringLiteral("La durée doit faire au moins une image.")) : failure;
    }
    const QString styleName = args.value(QStringLiteral("style")).toString(QStringLiteral("ouverture"));
    if (styleName != QLatin1String("ouverture") && styleName != QLatin1String("bas_ecran") && styleName != QLatin1String("carton") &&
        styleName != QLatin1String("sous_titre_simple")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("style vaut ouverture, bas_ecran, carton ou sous_titre_simple."));
    }
    const QString color = args.value(QStringLiteral("couleur")).toString(QStringLiteral("#FFFFFF"));
    if (!QColor(color).isValid()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("couleur s'écrit en hexadécimal, par exemple #FFFFFF."));
    }
    // La place doit être libre sur la piste avant de créer quoi que ce soit dans le chutier.
    for (int cid : clipsOnTrack(timeline, tid)) {
        const int start = timeline->getClipPosition(cid);
        const int end = start + timeline->getClipPlaytime(cid);
        if (start < position + duration && end > position) {
            return error(QStringLiteral("place_occupee"), QStringLiteral("La piste %1 est occupée de %2 à %3.").arg(trackLabel(timeline, tid)).arg(start).arg(end - 1),
                         QStringLiteral("Choisis une autre piste vidéo au-dessus de l'image, ou crée-en une avec ajouter_piste."));
        }
    }

    // Le XML du titre, au format du titreur de Kdenlive (data/titles/simple.koracuttitle).
    int width = 1920;
    int height = 1080;
    if (const auto &profile = pCore->getCurrentProfile()) {
        width = profile->width();
        height = profile->height();
    }
    const TitleStyle style = titleStyle(styleName);
    // La taille suit le petit côté du projet (un 9:16 n'a que 1080 de large), et le texte ne sort
    // jamais de la zone sûre : sans taille imposée, il rétrécit jusqu'à tenir dans 90 % de la largeur.
    const bool fixedSize = args.contains(QStringLiteral("taille_px"));
    int size = fixedSize ? std::clamp(args.value(QStringLiteral("taille_px")).toInt(), 8, 600) : qRound(style.size * std::min(width, height) / 1080.);
    QString family = args.value(QStringLiteral("police")).toString().trimmed();
    if (family.isEmpty()) {
        family = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    }
    QFont font(family);
    font.setWeight(styleName == QLatin1String("ouverture") ? QFont::Bold : QFont::Normal);
    const QStringList lines = text.split(QLatin1Char('\n'));
    int textWidth = 0;
    int textHeight = 0;
    for (;;) {
        font.setPixelSize(size);
        const QFontMetrics metrics(font);
        textWidth = 0;
        for (const QString &line : lines) {
            textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
        }
        textHeight = metrics.lineSpacing() * lines.size();
        if (fixedSize || textWidth <= width * 0.9 || size <= 12) {
            break;
        }
        size -= std::max(1, size / 20);
    }
    int x = 0;
    int y = 0;
    if (args.contains(QStringLiteral("x_px"))) {
        x = args.value(QStringLiteral("x_px")).toInt();
    } else {
        x = style.x < 0 ? (width - textWidth) / 2 : qRound(style.x * width);
    }
    if (args.contains(QStringLiteral("y_px"))) {
        y = args.value(QStringLiteral("y_px")).toInt();
    } else {
        y = style.y < 0 ? (height - textHeight) / 2 : qRound(style.y * height) - textHeight / 2;
    }
    QDomDocument title;
    QDomElement root = title.createElement(QStringLiteral("kdenlivetitle"));
    root.setAttribute(QStringLiteral("width"), width);
    root.setAttribute(QStringLiteral("height"), height);
    root.setAttribute(QStringLiteral("out"), duration - 1);
    root.setAttribute(QStringLiteral("LC_NUMERIC"), QStringLiteral("C"));
    title.appendChild(root);
    QDomElement item = title.createElement(QStringLiteral("item"));
    item.setAttribute(QStringLiteral("z-index"), 0);
    item.setAttribute(QStringLiteral("type"), QStringLiteral("QGraphicsTextItem"));
    QDomElement pos = title.createElement(QStringLiteral("position"));
    pos.setAttribute(QStringLiteral("x"), x);
    pos.setAttribute(QStringLiteral("y"), y);
    QDomElement transform = title.createElement(QStringLiteral("transform"));
    transform.appendChild(title.createTextNode(QStringLiteral("1,0,0,0,1,0,0,0,1")));
    pos.appendChild(transform);
    item.appendChild(pos);
    QDomElement content = title.createElement(QStringLiteral("content"));
    content.setAttribute(QStringLiteral("font"), family);
    content.setAttribute(QStringLiteral("font-pixel-size"), size);
    content.setAttribute(QStringLiteral("font-weight"), int(font.weight()));
    content.setAttribute(QStringLiteral("font-italic"), 0);
    content.setAttribute(QStringLiteral("font-underline"), 0);
    content.setAttribute(QStringLiteral("font-color"), colorText(color, 255));
    content.setAttribute(QStringLiteral("font-outline"), 0);
    content.setAttribute(QStringLiteral("font-outline-color"), QStringLiteral("0,0,0,255"));
    content.setAttribute(QStringLiteral("alignment"), style.alignment);
    content.appendChild(title.createTextNode(text));
    item.appendChild(content);
    root.appendChild(item);
    QDomElement startViewport = title.createElement(QStringLiteral("startviewport"));
    startViewport.setAttribute(QStringLiteral("rect"), QStringLiteral("0,0,%1,%2").arg(width).arg(height));
    root.appendChild(startViewport);
    QDomElement endViewport = title.createElement(QStringLiteral("endviewport"));
    endViewport.setAttribute(QStringLiteral("rect"), QStringLiteral("0,0,%1,%2").arg(width).arg(height));
    root.appendChild(endViewport);
    QDomElement background = title.createElement(QStringLiteral("background"));
    background.setAttribute(QStringLiteral("color"), style.background ? QStringLiteral("0,0,0,255") : QStringLiteral("0,0,0,0"));
    root.appendChild(background);

    // Le producteur du chutier (ce que ClipCreator::createTitleClip construit), dans notre étape.
    const QString firstLine = lines.first().trimmed();
    const QString name = QStringLiteral("Titre %1").arg(firstLine.left(30));
    QDomDocument xml;
    QDomElement prod = xml.createElement(QStringLiteral("producer"));
    xml.appendChild(prod);
    prod.setAttribute(QStringLiteral("type"), int(ClipType::Text));
    prod.setAttribute(QStringLiteral("in"), QStringLiteral("0"));
    prod.setAttribute(QStringLiteral("length"), duration);
    std::unordered_map<QString, QString> properties;
    properties[QStringLiteral("kdenlive:clipname")] = name;
    properties[QStringLiteral("mlt_service")] = QStringLiteral("kdenlivetitle");
    properties[QStringLiteral("xmldata")] = title.toString();
    properties[QStringLiteral("kdenlive:koracut_titre_texte")] = text;
    properties[QStringLiteral("kdenlive:koracut_titre_style")] = styleName;
    Xml::addXmlProperties(prod, properties);

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QString binId;
    auto model = pCore->projectItemModel();
    if (!model->requestAddBinClip(binId, xml.documentElement(), model->getRootFolder()->clipId(), undo, redo)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le clip de titre n'a pas pu être créé dans le chutier."));
    }
    // Le titre se rend en tâche de fond : on attend qu'il soit prêt (quelques dizaines de ms).
    QElapsedTimer timer;
    timer.start();
    std::shared_ptr<ProjectClip> clip = model->getClipByBinID(binId);
    while (clip && !clip->statusReady() && timer.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        clip = model->getClipByBinID(binId);
    }
    if (!clip || !clip->statusReady()) {
        undo();
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le titre n'a pas pu être rendu."), QStringLiteral("Vérifie la police demandée."));
    }
    int newId = -1;
    if (!timeline->requestClipInsertion(binId, tid, position, newId, true, true, false, undo, redo) || newId == -1) {
        undo();
        return error(QStringLiteral("place_occupee"), QStringLiteral("Le titre n'a pas pu être posé sur la piste %1 à %2.").arg(trackLabel(timeline, tid)).arg(position));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Add title %1", firstLine.left(30)));
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), newId},
                       {QStringLiteral("media_id"), binId},
                       {QStringLiteral("piste_id"), tid},
                       {QStringLiteral("position"), time(timeline->getClipPosition(newId))},
                       {QStringLiteral("duree"), time(timeline->getClipPlaytime(newId))},
                       {QStringLiteral("style"), styleName}};
    int tooLong = 0;
    for (const QString &line : lines) {
        if (line.size() > 40) {
            ++tooLong;
        }
    }
    if (tooLong > 0) {
        result.insert(QStringLiteral("avertissement"), QStringLiteral("%1 ligne(s) de plus de 40 caractères : le titre risque de sortir de la zone sûre.").arg(tooLong));
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// ajouter_transition

// La matière en réserve d'un clip du côté demandé, en images de timeline. Un titre, une couleur ou
// une image n'a pas de limite.
int reserveAfter(const Timeline &timeline, int cid)
{
    const auto binClip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid));
    if (!binClip || !binClip->hasLimitedDuration()) {
        return std::numeric_limits<int>::max();
    }
    const std::pair<int, int> inOut = timeline->getClipInOut(cid);
    return std::max(0, int(binClip->frameDuration()) - inOut.second - 1);
}

int reserveBefore(const Timeline &timeline, int cid)
{
    const auto binClip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid));
    if (!binClip || !binClip->hasLimitedDuration()) {
        return std::numeric_limits<int>::max();
    }
    return timeline->getClipInOut(cid).first;
}

// Le clip qui touche la fin de @p cid sur sa piste, -1 sinon.
int rightNeighbour(const Timeline &timeline, int cid)
{
    const int end = timeline->getClipPosition(cid) + timeline->getClipPlaytime(cid);
    const int next = timeline->getClipByPosition(timeline->getClipTrackId(cid), end);
    return (next != -1 && next != cid && timeline->getClipPosition(next) == end) ? next : -1;
}

QJsonObject ajouterTransition(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    QJsonObject failure;
    const int duration = requiredTime(args, QStringLiteral("duree"), failure);
    if (duration < 1) {
        return failure.isEmpty() ? error(QStringLiteral("entree_invalide"), QStringLiteral("La durée doit faire au moins une image.")) : failure;
    }
    const QString type = args.value(QStringLiteral("type")).toString(QStringLiteral("fondu"));
    QString mixId;
    if (type == QLatin1String("fondu")) {
        mixId = QStringLiteral("luma");
    } else if (type == QLatin1String("volet")) {
        mixId = QStringLiteral("wipe");
    } else if (type == QLatin1String("glissement")) {
        mixId = QStringLiteral("slide");
    } else {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("type vaut fondu, volet ou glissement."));
    }
    const int next = rightNeighbour(timeline, cid);
    if (next == -1) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Aucun clip ne touche la fin de celui-ci sur la même piste."),
                     QStringLiteral("Un fondu enchaîné se fait entre deux clips collés. Referme le vide d'abord, ou utilise ajouter_fondu."));
    }
    if (timeline->getMixDuration(next) > 0) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Il y a déjà un fondu enchaîné entre les clips %1 et %2.").arg(cid).arg(next),
                     QStringLiteral("Demande à la personne de le retirer, ou garde-le."));
    }
    if (duration >= timeline->getClipPlaytime(cid) || duration >= timeline->getClipPlaytime(next)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le fondu enchaîné est plus long que l'un des deux clips."),
                     QStringLiteral("Raccourcis la durée : 12 images est une bonne base."));
    }
    // Ce que mixClip exige : le clip de gauche s'allonge de sa moitié vers la droite (matière après sa
    // sortie), le clip de droite recule de l'autre moitié (matière avant son entrée).
    const int rightHalf = (duration + 1) / 2;
    const int leftHalf = duration - rightHalf;
    const int reserveLeft = reserveAfter(timeline, cid);
    const int reserveRight = reserveBefore(timeline, next);
    if (reserveLeft < leftHalf || reserveRight < rightHalf) {
        const QString lack = reserveLeft < leftHalf ? QStringLiteral("le clip de gauche n'a que %1 images après sa sortie").arg(reserveLeft)
                                                    : QStringLiteral("le clip de droite n'a que %1 images avant son entrée").arg(reserveRight);
        return error(QStringLiteral("pas_de_matiere"), QStringLiteral("Pas assez de matière en réserve pour %1 images de fondu enchaîné : %2.").arg(duration).arg(lack),
                     QStringLiteral("Raccourcis le fondu, ou rogne d'abord les deux clips de quelques images pour créer de la réserve."));
    }
    // Les paires à mélanger : le clip et, s'il en a un, son partenaire audio avec son propre voisin.
    QList<std::pair<int, int>> pairs{{cid, next}};
    if (const int partner = timeline->getClipSplitPartner(cid); partner > -1 && timeline->isClip(partner)) {
        const int partnerNext = rightNeighbour(timeline, partner);
        if (partnerNext != -1 && timeline->getMixDuration(partnerNext) == 0 && reserveAfter(timeline, partner) >= leftHalf && reserveBefore(timeline, partnerNext) >= rightHalf &&
            duration < timeline->getClipPlaytime(partner) && duration < timeline->getClipPlaytime(partnerNext)) {
            pairs << std::make_pair(partner, partnerNext);
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    for (const auto &[left, right] : std::as_const(pairs)) {
        const int tid = timeline->getClipTrackId(left);
        const int cut = timeline->getClipPosition(left) + timeline->getClipPlaytime(left);
        if (!timeline->requestClipMix(mixId, {left, right}, {leftHalf, rightHalf}, tid, cut, true, true, true, undo, redo, false)) {
            undo();
            return error(QStringLiteral("pas_de_matiere"), QStringLiteral("Le fondu enchaîné entre %1 et %2 a été refusé par la timeline.").arg(left).arg(right),
                         QStringLiteral("Vérifie la réserve des deux clips et qu'aucun autre fondu enchaîné ne les touche."));
        }
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Add mix"));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_gauche_id"), cid},
                       {QStringLiteral("clip_droit_id"), next},
                       {QStringLiteral("duree"), time(timeline->getMixDuration(next))},
                       {QStringLiteral("type"), type}};
}

// ---------------------------------------------------------------------------------------------
// ajouter_effet, regler_effet, supprimer_effet

QMap<QString, QString> parametersArg(const Args &args)
{
    QMap<QString, QString> params;
    const QJsonObject o = args.value(QStringLiteral("parametres")).toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        params.insert(it.key(), it.value().isString() ? it.value().toString() : variantText(it.value().toVariant()));
    }
    return params;
}

QJsonObject ajouterEffet(const Timeline &timeline, const Args &args)
{
    const QString effectId = args.value(QStringLiteral("effet_id")).toString();
    if (effectId.isEmpty() || !EffectsRepository::get()->exists(effectId)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("L'effet « %1 » n'est pas dans la bibliothèque.").arg(effectId),
                     QStringLiteral("Donne l'identifiant technique (qtblend, avfilter.gblur, vignette, volume...)."));
    }
    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = stackFor(timeline, args, failure);
    if (!stack) {
        return failure;
    }
    const QMap<QString, QString> params = parametersArg(args);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const std::shared_ptr<EffectItemModel> effect = appendEffect(stack, effectId, {}, undo, redo);
    if (!effect) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("L'effet %1 ne peut pas se poser ici (mauvais type de clip, ou effet réservé au master).").arg(effectId),
                     QStringLiteral("Un effet d'image va sur un clip ou une piste vidéo, un effet de son sur de l'audio."));
    }
    const QStringList names = parameterNames(effect);
    QStringList unknown;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (!names.contains(it.key())) {
            unknown << it.key();
        }
    }
    if (!unknown.isEmpty()) {
        undo();
        return error(QStringLiteral("entree_invalide"), QStringLiteral("L'effet %1 n'a pas de paramètre %2.").arg(effectId, unknown.join(QLatin1String(", "))),
                     QStringLiteral("Paramètres de cet effet : %1.").arg(names.join(QLatin1String(", "))));
    }
    if (!params.isEmpty()) {
        setParametersWithUndo(effect, params, undo, redo);
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Add effect %1", EffectsRepository::get()->getName(effectId)));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("effet_index"), effectRank(stack, effect)},
                       {QStringLiteral("effet_id"), effectId},
                       {QStringLiteral("parametres"), parametersJson(effect)}};
}

QJsonObject reglerEffet(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = stackFor(timeline, args, failure);
    if (!stack) {
        return failure;
    }
    const int index = args.value(QStringLiteral("effet_index")).toInt(-1);
    const std::shared_ptr<EffectItemModel> effect = effectAt(stack, index, failure);
    if (!effect) {
        return failure;
    }
    const QMap<QString, QString> params = parametersArg(args);
    const QStringList names = parameterNames(effect);
    QStringList unknown;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (!names.contains(it.key())) {
            unknown << it.key();
        }
    }
    if (!unknown.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("L'effet %1 n'a pas de paramètre %2.").arg(effect->getAssetId(), unknown.join(QLatin1String(", "))),
                     QStringLiteral("Paramètres de cet effet : %1.").arg(names.join(QLatin1String(", "))));
    }
    if (params.isEmpty() && !args.contains(QStringLiteral("actif"))) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Rien à changer : donne parametres ou actif."));
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!params.isEmpty()) {
        setParametersWithUndo(effect, params, undo, redo);
    }
    if (args.contains(QStringLiteral("actif"))) {
        const bool enabled = args.value(QStringLiteral("actif")).toBool();
        if (enabled != effect->isAssetEnabled()) {
            effect->markEnabled(enabled, undo, redo);
        }
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Adjust effect %1", EffectsRepository::get()->getName(effect->getAssetId())));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("effet_index"), effectRank(stack, effect)},
                       {QStringLiteral("effet_id"), effect->getAssetId()},
                       {QStringLiteral("actif"), effect->isAssetEnabled()},
                       {QStringLiteral("parametres"), parametersJson(effect)}};
}

QJsonObject supprimerEffet(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const std::shared_ptr<EffectStackModel> stack = stackFor(timeline, args, failure);
    if (!stack) {
        return failure;
    }
    const int index = args.value(QStringLiteral("effet_index")).toInt(-1);
    const std::shared_ptr<EffectItemModel> effect = effectAt(stack, index, failure);
    if (!effect) {
        return failure;
    }
    const QString removedId = effect->getAssetId();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (effect->isBuiltIn()) {
        // Un effet intégré ne se retire pas : on le désactive, il disparaît de la liste des agents.
        effect->markEnabled(false, undo, redo);
    } else {
        QString name;
        stack->removeEffectWithUndo(effect, name, undo, redo);
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Remove effect %1", EffectsRepository::get()->getName(removedId)));
    QJsonArray remaining;
    for (const auto &e : visibleEffects(stack)) {
        remaining.append(e->getAssetId());
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("effet_id_retire"), removedId}, {QStringLiteral("effets_restants"), remaining}};
}

// ---------------------------------------------------------------------------------------------
// poser_keyframe

QJsonObject poserKeyframe(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    QJsonObject failure;
    const std::shared_ptr<EffectItemModel> effect = effectAt(stack, args.value(QStringLiteral("effet_index")).toInt(-1), failure);
    if (!effect) {
        return failure;
    }
    const QString param = args.value(QStringLiteral("parametre")).toString();
    const QModelIndex paramIndex = effect->getParamIndexFromName(param);
    if (param.isEmpty() || !paramIndex.isValid()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("L'effet %1 n'a pas de paramètre « %2 ».").arg(effect->getAssetId(), param),
                     QStringLiteral("Paramètres de cet effet : %1.").arg(parameterNames(effect).join(QLatin1String(", "))));
    }
    const std::shared_ptr<KeyframeModelList> models = effect->getKeyframeModel();
    KeyframeModel *keyframes = models ? models->getKeyModel(QPersistentModelIndex(paramIndex)) : nullptr;
    if (!keyframes) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le paramètre « %1 » de %2 ne s'anime pas.").arg(param, effect->getAssetId()),
                     QStringLiteral("Choisis un paramètre animable (level, rect...) ou règle-le en fixe avec regler_effet."));
    }
    const int position = args.value(QStringLiteral("position_dans_clip")).toInt(-1);
    const int length = timeline->getClipPlaytime(cid);
    if (position < 0 || position > length - 1) {
        return error(QStringLiteral("hors_clip"), QStringLiteral("position_dans_clip (%1) dépasse la dernière image du clip (%2).").arg(position).arg(length - 1),
                     QStringLiteral("La position se compte depuis le début du clip, pas depuis le début de la timeline."));
    }
    const double fps = projectFps();
    const int in = timeline->getClipIn(cid);
    const GenTime pos(in + position, fps);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (args.value(QStringLiteral("supprimer")).toBool(false)) {
        if (!keyframes->hasKeyframe(pos)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Pas d'image clé à l'image %1 sur %2.").arg(position).arg(param),
                         QStringLiteral("Relis etat_timeline au niveau complet pour voir les images clés."));
        }
        if (!models->removeKeyframeWithUndo(pos, undo, redo)) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("L'image clé n'a pas pu être retirée (la première ne se retire pas)."));
        }
        pCore->pushUndo(undo, redo, i18nc("@action undo text", "Delete keyframe"));
    } else {
        const QString valueText = args.value(QStringLiteral("valeur")).toString();
        if (valueText.isEmpty()) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("valeur est vide."));
        }
        const QString interpolation = args.value(QStringLiteral("interpolation")).toString(QStringLiteral("lineaire"));
        KeyframeType::KeyframeEnum type = KeyframeType::Linear;
        bool found = false;
        for (const auto &pair : interpolations()) {
            if (pair.first == interpolation) {
                type = pair.second;
                found = true;
            }
        }
        if (!found) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Interpolation « %1 » inconnue.").arg(interpolation));
        }
        // Un rectangle se garde en texte, un nombre en nombre.
        const int paramType = effect->data(paramIndex, AssetParameterModel::TypeRole).toInt();
        QVariant value;
        if (paramType == int(ParamType::AnimatedRect) || paramType == int(ParamType::AnimatedFakeRect) || paramType == int(ParamType::Color) ||
            paramType == int(ParamType::AnimatedPoint) || paramType == int(ParamType::AnimatedFakePoint)) {
            value = valueText;
        } else {
            bool ok = false;
            const double number = valueText.toDouble(&ok);
            if (!ok) {
                return error(QStringLiteral("entree_invalide"), QStringLiteral("« %1 » n'est pas un nombre pour le paramètre %2.").arg(valueText, param));
            }
            value = number;
        }
        // Le modèle n'ajoute pas d'image clé de l'extérieur : on duplique la plus proche à la position
        // voulue, puis on lui donne sa valeur et son interpolation, le tout dans la même étape.
        bool posed = true;
        if (!keyframes->hasKeyframe(pos)) {
            bool ok = false;
            const Keyframe closest = keyframes->getClosestKeyframe(pos, &ok);
            posed = ok && models->duplicateKeyframeWithUndo(closest.first, pos, undo, redo);
        }
        posed = posed && keyframes->updateKeyframe(pos, value, undo, redo) && keyframes->updateKeyframeType(pos, int(type), undo, redo);
        if (!posed) {
            undo();
            return error(QStringLiteral("entree_invalide"), QStringLiteral("L'image clé n'a pas pu être posée."));
        }
        pCore->pushUndo(undo, redo, i18nc("@action undo text", "Add keyframe"));
    }
    QJsonArray list;
    for (const Keyframe &key : keyframesOf(*keyframes)) {
        list.append(QJsonObject{{QStringLiteral("i"), key.first.frames(fps) - in},
                                {QStringLiteral("valeur"), variantText(keyframes->getInterpolatedValue(key.first))},
                                {QStringLiteral("interpolation"), interpolationName(key.second)}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("parametre"), param}, {QStringLiteral("keyframes"), list}};
}

// ---------------------------------------------------------------------------------------------
// transformer_clip

QJsonObject transformerClip(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    if (timeline->isAudioTrack(timeline->getClipTrackId(cid))) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Un clip audio n'a pas d'image à transformer."), QStringLiteral("Donne l'id du clip vidéo du plan."));
    }
    for (const char *key : {"x_px", "y_px", "largeur_px", "hauteur_px"}) {
        if (!args.contains(QLatin1String(key)) || !args.value(QLatin1String(key)).isDouble()) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("« %1 » manque ou n'est pas un nombre.").arg(QLatin1String(key)));
        }
    }
    const int x = args.value(QStringLiteral("x_px")).toInt();
    const int y = args.value(QStringLiteral("y_px")).toInt();
    const int w = args.value(QStringLiteral("largeur_px")).toInt();
    const int h = args.value(QStringLiteral("hauteur_px")).toInt();
    if (w < 1 || h < 1) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("largeur_px et hauteur_px font au moins 1 pixel."));
    }
    const double opacity = std::clamp(args.value(QStringLiteral("opacite")).toDouble(1.), 0., 1.);
    const double rotation = std::clamp(args.value(QStringLiteral("rotation_degres")).toDouble(0.), -360., 360.);
    const QString rect = QStringLiteral("%1 %2 %3 %4 %5").arg(x).arg(y).arg(w).arg(h).arg(opacity, 0, 'f', 3);
    const QMap<QString, QString> params{{QStringLiteral("rect"), rect}, {QStringLiteral("rotation"), QString::number(rotation, 'f', 2)}};
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    std::shared_ptr<EffectItemModel> effect;
    for (const auto &e : visibleEffects(stack)) {
        if (e->getAssetId() == QLatin1String("qtblend")) {
            effect = e;
            break;
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (effect) {
        setParametersWithUndo(effect, params, undo, redo);
    } else {
        effect = appendEffect(stack, QStringLiteral("qtblend"), params, undo, redo);
        if (!effect) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet Transformer (qtblend) n'a pas pu être posé sur ce clip."));
        }
        // Un Transformer tout juste posé garde une image clé de départ sans valeur (« 0= ») : la première
        // image clé posée ensuite (moniteur, agent) réécrirait l'animation depuis ce modèle et le plan
        // perdrait sa place. Même correction que dans les gestes d'animation (document 19, lot 5).
        const QModelIndex index = effect->getParamIndexFromName(QStringLiteral("rect"));
        const std::shared_ptr<KeyframeModelList> models = effect->getKeyframeModel();
        if (KeyframeModel *model = models ? models->getKeyModel(QPersistentModelIndex(index)) : nullptr) {
            GenTime cursor(-1);
            bool ok = true;
            QList<GenTime> empty;
            while (ok) {
                const Keyframe next = model->getNextKeyframe(cursor, &ok);
                if (!ok) {
                    break;
                }
                if (model->getInterpolatedValue(next.first).toString().trimmed().isEmpty()) {
                    empty << next.first;
                }
                cursor = next.first;
            }
            for (const GenTime &t : std::as_const(empty)) {
                model->updateKeyframe(t, QVariant(rect), undo, redo);
            }
        }
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Transform clip"));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("effet_index"), effectRank(stack, effect)},
                       {QStringLiteral("rect"), QJsonObject{{QStringLiteral("x_px"), x},
                                                           {QStringLiteral("y_px"), y},
                                                           {QStringLiteral("largeur_px"), w},
                                                           {QStringLiteral("hauteur_px"), h},
                                                           {QStringLiteral("opacite"), opacity}}},
                       {QStringLiteral("rotation_degres"), rotation}};
}

// ---------------------------------------------------------------------------------------------
// sous_titres_poser (montage.json), 24 septembre 2026 : le montage Tek Tech a buté dessus. Les
// cartons de sous-titres vont sur la piste de sous-titres native de la séquence (libass, brûlés à
// l'export), avec des styles ASS nommés, un accent sur les mots entre *étoiles* et une entrée animée.

namespace {

QString assColour(const QString &hex, int alpha = 0)
{
    // ASS : &HAABBGGRR, alpha 00 = opaque.
    const QColor c(hex);
    return QStringLiteral("&H%1%2%3%4")
        .arg(alpha, 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString assTime(int frames, double fps)
{
    const qint64 cs = qRound64(frames * 100.0 / fps);
    return QStringLiteral("%1:%2:%3.%4")
        .arg(cs / 360000)
        .arg((cs / 6000) % 60, 2, 10, QLatin1Char('0'))
        .arg((cs / 100) % 60, 2, 10, QLatin1Char('0'))
        .arg(cs % 100, 2, 10, QLatin1Char('0'));
}

struct SubStyle
{
    QString name;
    QString font = QStringLiteral("Avenir Next");
    int size = 0;
    bool bold = true;
    QString colour = QStringLiteral("#FFFFFF");
    QString accent = QStringLiteral("#FFD166");
    QString outline = QStringLiteral("#000000");
    double outlineWidth = 0;
    double shadow = 0;
    QString box;
    int alignment = 2;
    int marginV = 0;
    int marginH = 0;
    QString entrance = QStringLiteral("pop");
    bool upper = false;
};

} // namespace

QJsonObject sousTitresPoser(const Timeline &timeline, const Args &args)
{
    const double fps = projectFps();
    const QSize frame = pCore->getCurrentFrameDisplaySize();
    const bool vertical = frame.height() > frame.width();
    if (!timeline) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucune séquence ouverte."));
    }
    // Les styles : « Default » toujours, plus ceux demandés.
    QMap<QString, SubStyle> styles;
    auto readStyle = [&](const QString &name, const QJsonObject &o) -> QJsonObject {
        SubStyle s;
        s.name = name;
        s.size = vertical ? qRound(frame.height() * 0.034) : qRound(frame.height() * 0.052);
        s.marginV = vertical ? qRound(frame.height() * 0.30) : qRound(frame.height() * 0.07);
        s.marginH = qRound(frame.width() * 0.08);
        s.outlineWidth = vertical ? 6 : 5;
        s.font = o.value(QStringLiteral("police")).toString(s.font);
        s.size = o.value(QStringLiteral("taille_px")).toInt(s.size);
        s.bold = o.value(QStringLiteral("gras")).toBool(s.bold);
        s.colour = o.value(QStringLiteral("couleur")).toString(s.colour);
        s.accent = o.value(QStringLiteral("accent")).toString(s.accent);
        s.outline = o.value(QStringLiteral("contour_couleur")).toString(s.outline);
        s.outlineWidth = o.value(QStringLiteral("contour_px")).toDouble(s.outlineWidth);
        s.shadow = o.value(QStringLiteral("ombre_px")).toDouble(s.shadow);
        s.box = o.value(QStringLiteral("fond_couleur")).toString();
        s.marginV = o.value(QStringLiteral("marge_v_px")).toInt(s.marginV);
        s.marginH = o.value(QStringLiteral("marge_h_px")).toInt(s.marginH);
        s.entrance = o.value(QStringLiteral("entree")).toString(s.entrance);
        s.upper = o.value(QStringLiteral("majuscules")).toBool(false);
        const QString pos = o.value(QStringLiteral("position")).toString(QStringLiteral("bas"));
        s.alignment = pos == QLatin1String("haut") ? 8 : (pos == QLatin1String("milieu") ? 5 : 2);
        for (const QString &c : {s.colour, s.accent, s.outline}) {
            if (!QColor::isValidColorName(c)) {
                return error(QStringLiteral("entree_invalide"), QStringLiteral("Couleur illisible : %1.").arg(c), QStringLiteral("Donne les couleurs en #RRGGBB."));
            }
        }
        if (!s.box.isEmpty() && !QColor::isValidColorName(s.box)) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Couleur illisible : %1.").arg(s.box), QStringLiteral("Donne les couleurs en #RRGGBB."));
        }
        styles.insert(name, s);
        return {};
    };
    const QJsonObject wanted = args.value(QStringLiteral("styles")).toObject();
    if (!wanted.contains(QStringLiteral("Default"))) {
        readStyle(QStringLiteral("Default"), {});
    }
    for (auto it = wanted.begin(); it != wanted.end(); ++it) {
        static const QRegularExpression okName(QStringLiteral("^[A-Za-z0-9_-]{1,30}$"));
        if (!okName.match(it.key()).hasMatch()) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Nom de style refusé : « %1 ».").arg(it.key()), QStringLiteral("Lettres, chiffres, - et _, 30 au plus."));
        }
        const QJsonObject failure = readStyle(it.key(), it.value().toObject());
        if (!failure.isEmpty()) {
            return failure;
        }
    }

    // Les cartons.
    const QJsonArray cards = args.value(QStringLiteral("cartons")).toArray();
    if (cards.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("cartons est vide."), QStringLiteral("Donne au moins un carton { debut, fin, texte }."));
    }
    QStringList events;
    int first = -1;
    int last = -1;
    int previousEnd = -1;
    static const QRegularExpression stars(QStringLiteral("\\*([^*]+)\\*"));
    for (int k = 0; k < cards.size(); ++k) {
        const QJsonObject c = cards.at(k).toObject();
        const int start = KoraCutAgentTools::parseTime(c.value(QStringLiteral("debut")), fps);
        const int end = KoraCutAgentTools::parseTime(c.value(QStringLiteral("fin")), fps);
        QString text = c.value(QStringLiteral("texte")).toString().trimmed();
        const QString styleName = c.value(QStringLiteral("style")).toString(QStringLiteral("Default"));
        if (start < 0 || end <= start) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Carton %1 : debut et fin illisibles, ou fin avant debut.").arg(k),
                         QStringLiteral("Temps en images ou en timecode HH:MM:SS:II, fin après debut."));
        }
        if (start < previousEnd) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Carton %1 : il commence avant la fin du précédent.").arg(k),
                         QStringLiteral("Donne les cartons dans l'ordre, sans chevauchement."));
        }
        if (text.isEmpty() || text.size() > 200) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Carton %1 : texte vide ou trop long (200 caractères au plus).").arg(k));
        }
        if (!styles.contains(styleName)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Carton %1 : style « %2 » inconnu.").arg(k).arg(styleName), QStringLiteral("Déclare-le dans styles."));
        }
        const SubStyle &s = styles.value(styleName);
        if (s.upper) {
            text = text.toUpper();
        }
        // Accolades : réservées aux balises ASS, on les retire du texte.
        text.remove(QLatin1Char('{')).remove(QLatin1Char('}'));
        text.replace(QLatin1Char('\n'), QLatin1String("\\N"));
        text.replace(stars, QStringLiteral("{\\c%1&}\\1{\\c%2&}").arg(assColour(s.accent).mid(0, 2) + assColour(s.accent).mid(4), assColour(s.colour).mid(0, 2) + assColour(s.colour).mid(4)));
        QString tags;
        if (s.entrance == QLatin1String("pop")) {
            tags = QStringLiteral("{\\fad(40,60)\\fscx86\\fscy86\\t(0,110,\\fscx104\\fscy104)\\t(110,190,\\fscx100\\fscy100)}");
        } else if (s.entrance == QLatin1String("fondu")) {
            tags = QStringLiteral("{\\fad(120,100)}");
        } else if (s.entrance == QLatin1String("monte")) {
            tags = QStringLiteral("{\\fad(80,60)\\frx40\\t(0,160,\\frx0)}");
        }
        events << QStringLiteral("Dialogue: 0,%1,%2,%3,,0,0,0,,%4%5").arg(assTime(start, fps), assTime(end, fps), styleName, tags, text);
        if (first < 0) {
            first = start;
        }
        last = end;
        previousEnd = end;
    }

    // Le fichier ASS, écrit dans le cache du projet puis importé : l'import de Kdenlive lit les
    // styles, la taille de l'image, et pousse lui-même son étape d'annulation.
    QString ass;
    QTextStream out(&ass);
    out << "[Script Info]\nScriptType: v4.00+\nPlayResX: " << frame.width() << "\nPlayResY: " << frame.height() << "\nLayoutResX: " << frame.width()
        << "\nLayoutResY: " << frame.height() << "\nWrapStyle: 0\nScaledBorderAndShadow: yes\nYCbCr Matrix: None\n\n";
    out << "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
           "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n";
    for (const SubStyle &s : std::as_const(styles)) {
        const bool box = !s.box.isEmpty();
        out << "Style: " << s.name << "," << s.font << "," << s.size << "," << assColour(s.colour) << "," << assColour(s.accent) << ","
            << (box ? assColour(s.box) : assColour(s.outline)) << "," << (box ? assColour(s.box) : assColour(s.outline, 0x60)) << "," << (s.bold ? -1 : 0)
            << ",0,0,0,100,100,0,0," << (box ? 3 : 1) << "," << (box ? qMax(8.0, s.outlineWidth) : s.outlineWidth) << "," << s.shadow << "," << s.alignment << ","
            << s.marginH << "," << s.marginH << "," << s.marginV << ",1\n";
    }
    out << "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (const QString &e : std::as_const(events)) {
        out << e << "\n";
    }
    out.flush();

    if (pCore->window() && timeline->uuid() == pCore->currentTimelineId()) {
        pCore->window()->showSubtitleTrack();
    }
    auto model = timeline->getSubtitleModel();
    if (!model) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La séquence n'a pas de piste de sous-titres."),
                     QStringLiteral("Active la séquence (projet.sequence faire=activer), puis recommence."));
    }
    if (model->isLocked()) {
        return error(QStringLiteral("piste_verrouillee"), QStringLiteral("La piste de sous-titres est verrouillée."));
    }
    const bool replace = args.value(QStringLiteral("remplacer")).toBool(true);
    int removed = 0;
    if (replace) {
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        const auto all = model->getAllSubtitles();
        for (int k = 0; k < int(all.size()); ++k) {
            const auto &sub = all.at(k);
            const int id = model->getIdForStartPos(sub.first.first, sub.first.second);
            if (id < 0) {
                continue;
            }
            const auto start = sub.first;
            const SubtitleEvent event = sub.second;
            const bool lastOne = k == int(all.size()) - 1;
            std::weak_ptr<SubtitleModel> weak = model;
            Fun local_redo = [weak, id, lastOne]() {
                auto m = weak.lock();
                return m && m->removeSubtitle(id, false, lastOne);
            };
            Fun local_undo = [weak, id, start, event, lastOne]() {
                auto m = weak.lock();
                return m && m->addSubtitle(id, start, event, false, lastOne);
            };
            if (local_redo()) {
                UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
                ++removed;
            }
        }
        if (removed > 0) {
            pCore->pushUndo(undo, redo, i18n("Delete subtitle"));
        }
    }
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/koracut-sous-titres");
    QDir().mkpath(folder);
    const QString path = folder + QStringLiteral("/%1.ass").arg(timeline->uuid().toString(QUuid::WithoutBraces));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Impossible d'écrire %1.").arg(path));
    }
    file.write(ass.toUtf8());
    file.close();
    // externalImport = vrai : les styles et la taille du fichier sont lus, et l'import pousse son étape d'annulation.
    model->importSubtitle(path, 0, true, float(fps), float(fps), "UTF-8");
    QJsonArray styleNames;
    for (const QString &n : styles.keys()) {
        styleNames.append(n);
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("cartons"), int(model->getAllSubtitles().size())},
                       {QStringLiteral("retires"), removed},
                       {QStringLiteral("styles"), styleNames},
                       {QStringLiteral("format"), QStringLiteral("%1x%2").arg(frame.width()).arg(frame.height())},
                       {QStringLiteral("premier"), time(first)},
                       {QStringLiteral("dernier"), time(last)},
                       {QStringLiteral("fichier"), path}};
}


} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> montageTools()
{
    return {
        {QStringLiteral("ajouter_titre"), ajouterTitre},   {QStringLiteral("ajouter_transition"), ajouterTransition},
        {QStringLiteral("ajouter_effet"), ajouterEffet},   {QStringLiteral("regler_effet"), reglerEffet},
        {QStringLiteral("supprimer_effet"), supprimerEffet}, {QStringLiteral("poser_keyframe"), poserKeyframe},
        {QStringLiteral("transformer_clip"), transformerClip}, {QStringLiteral("sous_titres_poser"), sousTitresPoser},
    };
}

} // namespace KoraCutAgentToolsPrivate
