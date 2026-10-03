/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Chantier 3, lot B (24 septembre 2026) : les dix outils d'étalonnage du Coloriste. Contrat :
// etalonnage.json et le banc (Agents in-app/banc/src/outils/etalonnage.ts). La mesure d'image se
// fait en C++ sur une image tirée par MLT (les scopes de src/scopes/ calculent pour dessiner, dans
// des threads de widgets : on ne les appelle pas). Règle : un appel = une étape d'annulation.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "koracut/koracutperformance.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "doc/kthumb.h"
#include "effects/effectsrepository.hpp"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "profiles/profilemodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

using namespace KoraCutAgentToolsPrivate;

namespace KoraCutAgentToolsPrivate {

bool isColourEffect(const QString &effectId)
{
    static const QSet<QString> ids = {
        // Ce que posent les outils du Coloriste.
        QStringLiteral("koracut_grade"), QStringLiteral("lift_gamma_gain"), QStringLiteral("avfilter.colortemperature"), QStringLiteral("avfilter.colorbalance"), QStringLiteral("avfilter.exposure"),
        QStringLiteral("avfilter.eq"), QStringLiteral("avfilter.curves"), QStringLiteral("avfilter.lut3d"),
        // Ce que la personne pose à la main dans la catégorie Couleur.
        QStringLiteral("avfilter.colorlevels"), QStringLiteral("avfilter.colorchannelmixer"), QStringLiteral("avfilter.colorcontrast"),
        QStringLiteral("avfilter.colorcorrect"), QStringLiteral("avfilter.histeq"), QStringLiteral("avfilter.normalize"), QStringLiteral("avfilter.vibrance"),
        QStringLiteral("avfilter.hue"), QStringLiteral("lumaliftgaingamma"), QStringLiteral("frei0r.balanc0r"), QStringLiteral("frei0r.bezier_curves"),
        QStringLiteral("frei0r.brightness"), QStringLiteral("frei0r.contrast0r"), QStringLiteral("frei0r.saturat0r"), QStringLiteral("frei0r.coloradj_RGB"),
        QStringLiteral("frei0r.colgate"), QStringLiteral("frei0r.curves"), QStringLiteral("frei0r.levels"), QStringLiteral("frei0r.hueshift0r"),
        QStringLiteral("frei0r.sopsat"), QStringLiteral("frei0r.three_point_balance"), QStringLiteral("frei0r.gamma"), QStringLiteral("frei0r.equaliz0r"),
        QStringLiteral("frei0r.primaries"), QStringLiteral("frei0r.sigmoidaltransfer"), QStringLiteral("gamma"), QStringLiteral("brightness"),
    };
    return ids.contains(effectId);
}

} // namespace KoraCutAgentToolsPrivate

namespace {

// ---------------------------------------------------------------------------------------------
// Les aides

// Un clip qui a une image, sur une piste vidéo. @p readOnly : une piste verrouillée ne gêne pas.
QJsonObject imageClipCheck(const Timeline &timeline, int cid, bool readOnly = false)
{
    if (readOnly) {
        if (!timeline->isClip(cid)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(cid),
                         QStringLiteral("Relis etat_timeline : les identifiants changent après une coupe ou une annulation."));
        }
    } else if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    if (timeline->isAudioTrack(timeline->getClipTrackId(cid))) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Le clip %1 est un son : il n'a pas d'image à étalonner.").arg(cid),
                     QStringLiteral("Donne l'id du clip vidéo du plan (champ k = « v » dans etat_timeline)."));
    }
    return {};
}

QString numberText(double v)
{
    return QString::number(v, 'f', 4).remove(QRegularExpression(QStringLiteral("\\.?0+$")));
}

double round1(double v)
{
    return std::round(v * 10.) / 10.;
}

// Les effets de couleur visibles d'une pile, dans l'ordre.
std::vector<std::shared_ptr<EffectItemModel>> colourEffects(const std::shared_ptr<EffectStackModel> &stack)
{
    std::vector<std::shared_ptr<EffectItemModel>> list;
    for (const auto &effect : visibleEffects(stack)) {
        if (isColourEffect(effect->getAssetId())) {
            list.push_back(effect);
        }
    }
    return list;
}

// Pose ou règle l'effet @p effectId (un seul par pile) avec @p params, dans undo/redo.
std::shared_ptr<EffectItemModel> uniqueEffect(const std::shared_ptr<EffectStackModel> &stack, const QString &effectId, const QMap<QString, QString> &params, Fun &undo,
                                              Fun &redo)
{
    if (std::shared_ptr<EffectItemModel> existing = findVisibleEffect(stack, effectId)) {
        if (!params.isEmpty()) {
            setParametersWithUndo(existing, params, undo, redo);
        }
        return existing;
    }
    return appendEffect(stack, effectId, params, undo, redo);
}

// Un nombre lu dans un paramètre d'effet, @p fallback s'il ne se lit pas.
double paramNumber(const std::shared_ptr<EffectItemModel> &effect, const QString &name, double fallback)
{
    bool ok = false;
    const double v = effect->getParamFromName(name).toString().toDouble(&ok);
    return ok ? v : fallback;
}

// ---------------------------------------------------------------------------------------------
// La mesure d'image

struct ImageMeasure
{
    bool ok = false;
    int lumaMin = 0;
    int lumaMax = 0;
    double lumaMean = 0.;
    double crushed = 0.;   // % de pixels écrasés (Y <= 2)
    double burned = 0.;    // % de pixels brûlés (Y >= 253)
    double saturation = 0.;
    double domR = 0.;
    double domG = 0.;
    double domB = 0.;
    bool skin = false;
    double skinHue = 0.;   // angle sur le vecteurscope, 0° à droite, sens antihoraire
    double skinDelta = 0.; // écart à la ligne de peau (123°), en valeur absolue
};

constexpr double kSkinLine = 123.; // la ligne I du vecteurscope, où tombent les peaux

// Les chiffres d'une image : luma Rec.709 sur 0 à 255, saturation HSV, dominante par canal, peau
// cherchée autour de la ligne I du vecteurscope (plan U = 0,493 (B - Y), V = 0,877 (R - Y)).
ImageMeasure measureImage(const QImage &source)
{
    ImageMeasure m;
    if (source.isNull()) {
        return m;
    }
    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    const int w = image.width();
    const int h = image.height();
    const double total = double(w) * h;
    if (total <= 0) {
        return m;
    }
    int histogram[256] = {0};
    double sumY = 0.;
    double sumR = 0.;
    double sumG = 0.;
    double sumB = 0.;
    double sumSat = 0.;
    qint64 crushed = 0;
    qint64 burned = 0;
    qint64 skinCount = 0;
    double skinSin = 0.;
    double skinCos = 0.;
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int r = qRed(line[x]);
            const int g = qGreen(line[x]);
            const int b = qBlue(line[x]);
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const int bin = std::clamp(int(std::lround(luma)), 0, 255);
            ++histogram[bin];
            sumY += luma;
            sumR += r;
            sumG += g;
            sumB += b;
            if (bin <= 2) {
                ++crushed;
            }
            if (bin >= 253) {
                ++burned;
            }
            const int maxC = std::max({r, g, b});
            const int minC = std::min({r, g, b});
            const double sat = maxC > 0 ? double(maxC - minC) / maxC : 0.;
            sumSat += sat;
            // La peau : une teinte proche de la ligne I, une chroma modérée, ni noire ni blanche.
            const double y601 = 0.299 * r + 0.587 * g + 0.114 * b;
            const double u = 0.493 * (b - y601);
            const double v = 0.877 * (r - y601);
            const double chroma = std::sqrt(u * u + v * v) / 255.;
            if (chroma >= 0.04 && chroma <= 0.35 && y601 >= 30 && y601 <= 240) {
                const double angle = std::atan2(v, u) * 180. / M_PI;
                double delta = angle - kSkinLine;
                while (delta > 180.) {
                    delta -= 360.;
                }
                while (delta < -180.) {
                    delta += 360.;
                }
                if (std::abs(delta) <= 30.) {
                    ++skinCount;
                    skinSin += std::sin(angle * M_PI / 180.);
                    skinCos += std::cos(angle * M_PI / 180.);
                }
            }
        }
    }
    // min et max : les percentiles 1 et 99, pour qu'un pixel isolé ne compte pas.
    qint64 seen = 0;
    m.lumaMin = 0;
    for (int i = 0; i < 256; ++i) {
        seen += histogram[i];
        if (seen >= total * 0.01) {
            m.lumaMin = i;
            break;
        }
    }
    seen = 0;
    m.lumaMax = 255;
    for (int i = 255; i >= 0; --i) {
        seen += histogram[i];
        if (seen >= total * 0.01) {
            m.lumaMax = i;
            break;
        }
    }
    m.lumaMean = sumY / total;
    m.crushed = 100. * crushed / total;
    m.burned = 100. * burned / total;
    m.saturation = sumSat / total;
    const double meanR = sumR / total;
    const double meanG = sumG / total;
    const double meanB = sumB / total;
    const double gray = (meanR + meanG + meanB) / 3.;
    m.domR = meanR - gray;
    m.domG = meanG - gray;
    m.domB = meanB - gray;
    m.skin = skinCount >= total * 0.03;
    if (m.skin) {
        double hue = std::atan2(skinSin, skinCos) * 180. / M_PI;
        if (hue < 0) {
            hue += 360.;
        }
        m.skinHue = hue;
        double delta = hue - kSkinLine;
        while (delta > 180.) {
            delta -= 360.;
        }
        while (delta < -180.) {
            delta += 360.;
        }
        m.skinDelta = std::abs(delta);
    }
    m.ok = true;
    return m;
}

QJsonObject measureJson(const ImageMeasure &m)
{
    return QJsonObject{{QStringLiteral("luma"), QJsonObject{{QStringLiteral("min"), m.lumaMin},
                                                            {QStringLiteral("max"), m.lumaMax},
                                                            {QStringLiteral("moyenne"), round1(m.lumaMean)},
                                                            {QStringLiteral("noirs_ecrases_pourcent"), round1(m.crushed)},
                                                            {QStringLiteral("blancs_brules_pourcent"), round1(m.burned)}}},
                       {QStringLiteral("saturation_moyenne"), std::round(m.saturation * 100.) / 100.},
                       {QStringLiteral("dominante"), QJsonObject{{QStringLiteral("r"), round1(m.domR)}, {QStringLiteral("v"), round1(m.domG)}, {QStringLiteral("b"), round1(m.domB)}}},
                       {QStringLiteral("peau"), QJsonObject{{QStringLiteral("detectee"), m.skin},
                                                            {QStringLiteral("teinte_degres"), round1(m.skinHue)},
                                                            {QStringLiteral("ecart_ligne_peau_degres"), round1(m.skinDelta)}}}};
}

constexpr int kMeasureWidth = 480;

// Coupe tous les filtres d'un service MLT (la copie ne sert qu'à la mesure).
void disableFilters(Mlt::Service &service)
{
    for (int f = 0; f < service.filter_count(); ++f) {
        std::unique_ptr<Mlt::Filter> filter(service.filter(f));
        if (filter && filter->is_valid()) {
            filter->set("disable", 1);
        }
    }
}

// Coupe les filtres du clip qui passe par @p position dans une piste. Une piste de Kdenlive est un
// tracteur de deux listes de lecture (pour les fondus enchaînés) : on descend jusqu'aux listes, et
// dans chacune on coupe les filtres de l'entrée et de son producteur parent.
void disableClipFilters(Mlt::Service &service, int position)
{
    const mlt_service_type type = service.type();
    if (type == mlt_service_tractor_type || type == mlt_service_multitrack_type) {
        Mlt::Tractor sub(service);
        if (!sub.is_valid()) {
            return;
        }
        for (int j = 0; j < sub.count(); ++j) {
            std::unique_ptr<Mlt::Producer> inner(sub.track(j));
            if (inner && inner->is_valid()) {
                Mlt::Service innerService(inner->get_service());
                disableFilters(innerService);
                disableClipFilters(innerService, position);
            }
        }
        return;
    }
    if (type != mlt_service_playlist_type) {
        return;
    }
    Mlt::Playlist playlist(service);
    if (!playlist.is_valid()) {
        return;
    }
    const int index = playlist.get_clip_index_at(position);
    if (index < 0 || index >= playlist.count() || playlist.is_blank(index)) {
        return;
    }
    std::unique_ptr<Mlt::Producer> entry(playlist.get_clip(index));
    if (!entry || !entry->is_valid()) {
        return;
    }
    Mlt::Service cut(entry->get_service());
    disableFilters(cut);
    Mlt::Producer parent = entry->parent();
    if (parent.is_valid()) {
        Mlt::Service parentService(parent.get_service());
        disableFilters(parentService);
    }
}

// L'image d'un clip à @p positionInClip, tirée d'une copie XML de la timeline où seules la piste du
// clip et la piste de fond restent visibles : effets compris, ou tous les filtres coupés (clip, piste
// et sortie générale) pour l'image source. Le même chemin dans les deux cas, pour que les deux
// mesures se comparent.
QImage clipFrame(const Timeline &timeline, int cid, int positionInClip, bool withEffects)
{
    int projectWidth = 1920;
    int projectHeight = 1080;
    if (const auto &profile = pCore->getCurrentProfile()) {
        projectWidth = profile->width();
        projectHeight = profile->height();
    }
    const int height = std::max(1, kMeasureWidth * projectHeight / std::max(1, projectWidth));
    const QString xml = timeline->sceneList(QString());
    if (xml.isEmpty()) {
        return {};
    }
    // La copie ne doit pas évincer les décodeurs du moniteur (plantages du 2 octobre 2026).
    const KoraCutPerformance::DecoderCacheRoom room(xml);
    // La mesure porte sur l'image étalonnée, même si l'étalonnage est en pause dans l'aperçu.
    const KoraCutPerformance::GradeBypass graded;
    Mlt::Producer producer(pCore->getProjectProfile(), "xml-string", xml.toUtf8().constData());
    if (!producer.is_valid()) {
        return {};
    }
    const int position = timeline->getClipPosition(cid) + positionInClip;
    Mlt::Service service(producer.get_service());
    Mlt::Tractor tractor(service);
    if (tractor.is_valid()) {
        const int keep = timeline->getTrackMltIndex(timeline->getClipTrackId(cid));
        const int count = tractor.count();
        for (int i = 1; i < count; ++i) {
            std::unique_ptr<Mlt::Producer> track(tractor.track(i));
            if (!track || !track->is_valid()) {
                continue;
            }
            if (i != keep) {
                track->set("hide", 3);
                continue;
            }
            if (withEffects) {
                continue;
            }
            Mlt::Service trackService(track->get_service());
            disableFilters(trackService);
            disableClipFilters(trackService, position);
        }
        if (!withEffects) {
            disableFilters(service);
        }
    }
    return KThumb::getFrame(producer, position, kMeasureWidth, height);
}

// La mesure d'un clip à son milieu (ou à @p positionInClip), ou une erreur.
ImageMeasure measureClip(const Timeline &timeline, int cid, int positionInClip, bool withEffects, QJsonObject &failure)
{
    const int length = timeline->getClipPlaytime(cid);
    if (positionInClip < 0) {
        positionInClip = length / 2;
    }
    if (positionInClip > length - 1) {
        failure = error(QStringLiteral("hors_clip"), QStringLiteral("position_dans_clip (%1) dépasse la dernière image du clip (%2).").arg(positionInClip).arg(length - 1),
                        QStringLiteral("La position se compte depuis le début du clip, pas depuis le début de la timeline."));
        return {};
    }
    const ImageMeasure m = measureImage(clipFrame(timeline, cid, positionInClip, withEffects));
    if (!m.ok) {
        failure = error(QStringLiteral("non_disponible"), QStringLiteral("Aucune image n'a pu être tirée du clip %1.").arg(cid));
    }
    return m;
}

// ---------------------------------------------------------------------------------------------
// mesurer_image

QJsonObject mesurerImage(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, cid, true); !e.isEmpty()) {
        return e;
    }
    const int position = args.contains(QStringLiteral("position_dans_clip")) ? args.value(QStringLiteral("position_dans_clip")).toInt(-1) : -1;
    if (args.contains(QStringLiteral("position_dans_clip")) && position < 0) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("position_dans_clip est un nombre d'images depuis le début du clip."));
    }
    QJsonObject failure;
    const ImageMeasure m = measureClip(timeline, cid, position, !args.value(QStringLiteral("sans_effets")).toBool(false), failure);
    if (!m.ok) {
        return failure;
    }
    const int measured = position < 0 ? timeline->getClipPlaytime(cid) / 2 : position;
    QJsonObject result = measureJson(m);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("position"), time(timeline->getClipPosition(cid) + measured));
    result.insert(QStringLiteral("position_dans_clip"), measured);
    return result;
}

// ---------------------------------------------------------------------------------------------
// balance_blancs

QJsonObject balanceBlancs(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    if (!args.value(QStringLiteral("temperature_k")).isDouble()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("temperature_k manque ou n'est pas un nombre."), QStringLiteral("6500 ne change rien, plus haut refroidit, plus bas réchauffe."));
    }
    const double kelvin = args.value(QStringLiteral("temperature_k")).toDouble();
    if (kelvin < 1000 || kelvin > 40000) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("temperature_k va de 1000 à 40000 kelvins."));
    }
    const double tint = std::clamp(args.value(QStringLiteral("teinte")).toDouble(0.), -1., 1.);
    const bool hasMix = args.contains(QStringLiteral("intensite"));
    const double mix = std::clamp(args.value(QStringLiteral("intensite")).toDouble(1.), 0., 1.);
    for (const char *id : {"avfilter.colortemperature", "avfilter.colorbalance"}) {
        if (!EffectsRepository::get()->exists(QLatin1String(id))) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet %1 n'est pas dans la bibliothèque de cette machine.").arg(QLatin1String(id)));
        }
    }
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QMap<QString, QString> params{{QStringLiteral("av.temperature"), numberText(kelvin)}};
    if (hasMix) {
        params.insert(QStringLiteral("av.mix"), numberText(mix));
    }
    const std::shared_ptr<EffectItemModel> temperature = uniqueEffect(stack, QStringLiteral("avfilter.colortemperature"), params, undo, redo);
    if (!temperature) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet de température n'a pas pu être posé sur le clip %1.").arg(cid));
    }
    // La teinte vert-magenta vit dans l'effet Balance des couleurs (av.gm), posé seulement s'il sert.
    std::shared_ptr<EffectItemModel> balance = findVisibleEffect(stack, QStringLiteral("avfilter.colorbalance"));
    if (args.contains(QStringLiteral("teinte")) && (!qFuzzyIsNull(tint) || balance)) {
        balance = uniqueEffect(stack, QStringLiteral("avfilter.colorbalance"), {{QStringLiteral("av.gm"), numberText(tint)}}, undo, redo);
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "White balance"));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("effet_index"), effectRank(stack, temperature)},
                       {QStringLiteral("temperature_k"), paramNumber(temperature, QStringLiteral("av.temperature"), kelvin)},
                       {QStringLiteral("teinte"), balance ? paramNumber(balance, QStringLiteral("av.gm"), 0.) : 0.},
                       {QStringLiteral("intensite"), paramNumber(temperature, QStringLiteral("av.mix"), 1.)}};
}

// ---------------------------------------------------------------------------------------------
// regler_exposition

QJsonObject reglerExposition(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    const bool hasExposure = args.contains(QStringLiteral("exposition_ev"));
    const bool hasBlack = args.contains(QStringLiteral("niveau_noir"));
    const bool hasContrast = args.contains(QStringLiteral("contraste"));
    const bool hasSaturation = args.contains(QStringLiteral("saturation"));
    if (!hasExposure && !hasBlack && !hasContrast && !hasSaturation) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Aucun réglage donné."), QStringLiteral("Donne au moins un champ parmi exposition_ev, niveau_noir, contraste, saturation."));
    }
    const double ev = std::clamp(args.value(QStringLiteral("exposition_ev")).toDouble(0.), -3., 3.);
    const double black = std::clamp(args.value(QStringLiteral("niveau_noir")).toDouble(0.), -1., 1.);
    const double contrast = std::clamp(args.value(QStringLiteral("contraste")).toDouble(1.), 0., 3.);
    const double saturation = std::clamp(args.value(QStringLiteral("saturation")).toDouble(1.), 0., 3.);
    for (const char *id : {"avfilter.exposure", "avfilter.eq"}) {
        if (!EffectsRepository::get()->exists(QLatin1String(id))) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet %1 n'est pas dans la bibliothèque de cette machine.").arg(QLatin1String(id)));
        }
    }
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (hasExposure || hasBlack) {
        QMap<QString, QString> params;
        if (hasExposure) {
            params.insert(QStringLiteral("av.exposure"), numberText(ev));
        }
        if (hasBlack) {
            params.insert(QStringLiteral("av.black"), numberText(black));
        }
        if (!uniqueEffect(stack, QStringLiteral("avfilter.exposure"), params, undo, redo)) {
            undo();
            return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet d'exposition n'a pas pu être posé sur le clip %1.").arg(cid));
        }
    }
    if (hasContrast || hasSaturation) {
        QMap<QString, QString> params;
        if (hasContrast) {
            params.insert(QStringLiteral("av.contrast"), numberText(contrast));
        }
        if (hasSaturation) {
            params.insert(QStringLiteral("av.saturation"), numberText(saturation));
        }
        if (!uniqueEffect(stack, QStringLiteral("avfilter.eq"), params, undo, redo)) {
            undo();
            return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet de contraste n'a pas pu être posé sur le clip %1.").arg(cid));
        }
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Adjust exposure"));
    QJsonArray posed;
    const auto exposure = findVisibleEffect(stack, QStringLiteral("avfilter.exposure"));
    const auto eq = findVisibleEffect(stack, QStringLiteral("avfilter.eq"));
    for (const auto &effect : {exposure, eq}) {
        if (effect) {
            posed.append(QJsonObject{{QStringLiteral("effet_index"), effectRank(stack, effect)}, {QStringLiteral("effet_id"), effect->getAssetId()}});
        }
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("effets"), posed},
                       {QStringLiteral("exposition_ev"), exposure ? paramNumber(exposure, QStringLiteral("av.exposure"), 0.) : 0.},
                       {QStringLiteral("niveau_noir"), exposure ? paramNumber(exposure, QStringLiteral("av.black"), 0.) : 0.},
                       {QStringLiteral("contraste"), eq ? paramNumber(eq, QStringLiteral("av.contrast"), 1.) : 1.},
                       {QStringLiteral("saturation"), eq ? paramNumber(eq, QStringLiteral("av.saturation"), 1.) : 1.}};
}

// ---------------------------------------------------------------------------------------------
// lift_gamma_gain

// Une roue { r, v, b } lue dans les arguments ; false si elle est absente, une erreur si elle est mal formée.
bool readWheel(const Args &args, const QString &key, QMap<QString, QString> &params, const QString &prefix, QJsonObject &failure)
{
    if (!args.contains(key)) {
        return false;
    }
    const QJsonObject wheel = args.value(key).toObject();
    const QStringList channels{QStringLiteral("r"), QStringLiteral("v"), QStringLiteral("b")};
    const QStringList suffixes{QStringLiteral("r"), QStringLiteral("g"), QStringLiteral("b")};
    for (int i = 0; i < 3; ++i) {
        const QJsonValue v = wheel.value(channels.at(i));
        if (!v.isDouble() || v.toDouble() < 0 || v.toDouble() > 5) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("%1 attend trois nombres r, v, b entre 0 et 5.").arg(key));
            return false;
        }
        params.insert(QStringLiteral("%1_%2").arg(prefix, suffixes.at(i)), numberText(v.toDouble()));
    }
    return true;
}

QJsonObject wheelJson(const std::shared_ptr<EffectItemModel> &effect, const QString &prefix, double neutral)
{
    return QJsonObject{{QStringLiteral("r"), paramNumber(effect, prefix + QStringLiteral("_r"), neutral)},
                       {QStringLiteral("v"), paramNumber(effect, prefix + QStringLiteral("_g"), neutral)},
                       {QStringLiteral("b"), paramNumber(effect, prefix + QStringLiteral("_b"), neutral)}};
}

QJsonObject liftGammaGain(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    QJsonObject failure;
    QMap<QString, QString> params;
    const bool lift = readWheel(args, QStringLiteral("ombres"), params, QStringLiteral("lift"), failure);
    if (!failure.isEmpty()) {
        return failure;
    }
    const bool gamma = readWheel(args, QStringLiteral("tons_moyens"), params, QStringLiteral("gamma"), failure);
    if (!failure.isEmpty()) {
        return failure;
    }
    const bool gain = readWheel(args, QStringLiteral("hautes_lumieres"), params, QStringLiteral("gain"), failure);
    if (!failure.isEmpty()) {
        return failure;
    }
    if (!lift && !gamma && !gain) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Aucune roue donnée."), QStringLiteral("Donne ombres, tons_moyens ou hautes_lumieres."));
    }
    if (!EffectsRepository::get()->exists(QStringLiteral("lift_gamma_gain"))) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet lift_gamma_gain n'est pas dans la bibliothèque de cette machine."));
    }
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const std::shared_ptr<EffectItemModel> effect = uniqueEffect(stack, QStringLiteral("lift_gamma_gain"), params, undo, redo);
    if (!effect) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Les roues n'ont pas pu être posées sur le clip %1.").arg(cid));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Color wheels"));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("effet_index"), effectRank(stack, effect)},
                       {QStringLiteral("ombres"), wheelJson(effect, QStringLiteral("lift"), 0.)},
                       {QStringLiteral("tons_moyens"), wheelJson(effect, QStringLiteral("gamma"), 1.)},
                       {QStringLiteral("hautes_lumieres"), wheelJson(effect, QStringLiteral("gain"), 1.)}};
}

// ---------------------------------------------------------------------------------------------
// regler_courbes

QJsonObject reglerCourbes(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    static const QMap<QString, QString> channels{{QStringLiteral("luma"), QStringLiteral("av.master")},
                                                 {QStringLiteral("rouge"), QStringLiteral("av.r")},
                                                 {QStringLiteral("vert"), QStringLiteral("av.g")},
                                                 {QStringLiteral("bleu"), QStringLiteral("av.b")}};
    const QString channel = args.value(QStringLiteral("canal")).toString();
    if (!channels.contains(channel)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("canal vaut luma, rouge, vert ou bleu."));
    }
    const QJsonArray points = args.value(QStringLiteral("points")).toArray();
    if (points.size() < 2 || points.size() > 8) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("points compte de 2 à 8 points { entree, sortie }."));
    }
    QStringList pairs;
    QJsonArray echo;
    double last = -1.;
    for (const QJsonValue &v : points) {
        const QJsonObject p = v.toObject();
        const QJsonValue in = p.value(QStringLiteral("entree"));
        const QJsonValue out = p.value(QStringLiteral("sortie"));
        if (!in.isDouble() || !out.isDouble() || in.toDouble() < 0 || in.toDouble() > 1 || out.toDouble() < 0 || out.toDouble() > 1) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Chaque point donne entree et sortie entre 0 et 1."));
        }
        if (in.toDouble() <= last) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Les points de la courbe doivent être triés par entrée strictement croissante."),
                         QStringLiteral("Trie les points, sans doublon."));
        }
        last = in.toDouble();
        pairs << QStringLiteral("%1/%2").arg(numberText(in.toDouble()), numberText(out.toDouble()));
        echo.append(QJsonObject{{QStringLiteral("entree"), in.toDouble()}, {QStringLiteral("sortie"), out.toDouble()}});
    }
    if (!EffectsRepository::get()->exists(QStringLiteral("avfilter.curves"))) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet avfilter.curves n'est pas dans la bibliothèque de cette machine."));
    }
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    // Le modèle garde les points séparés par « ; » et les donne au filtre séparés par des espaces.
    const std::shared_ptr<EffectItemModel> effect = uniqueEffect(stack, QStringLiteral("avfilter.curves"), {{channels.value(channel), pairs.join(QLatin1Char(';'))}}, undo, redo);
    if (!effect) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Les courbes n'ont pas pu être posées sur le clip %1.").arg(cid));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Adjust curves"));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("effet_index"), effectRank(stack, effect)}, {QStringLiteral("canal"), channel}, {QStringLiteral("points"), echo}};
}

// ---------------------------------------------------------------------------------------------
// appliquer_lut

// Les dossiers où chercher une LUT donnée par son seul nom : ceux de l'application, puis « luts »
// à côté du projet.
QStringList lutFolders()
{
    QStringList folders = QStandardPaths::locateAll(QStandardPaths::AppLocalDataLocation, QStringLiteral("luts"), QStandardPaths::LocateDirectory);
    if (pCore->currentDoc() && !pCore->currentDoc()->url().isEmpty()) {
        folders << QFileInfo(pCore->currentDoc()->url().toLocalFile()).absolutePath() + QStringLiteral("/luts");
    }
    return folders;
}

QJsonObject appliquerLut(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    QString file = args.value(QStringLiteral("fichier")).toString().trimmed();
    if (file.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("fichier est vide."));
    }
    if (file.startsWith(QLatin1String("~/"))) {
        file = QDir::homePath() + file.mid(1);
    }
    static const QRegularExpression extension(QStringLiteral("\\.(cube|3dl|dat|m3d)$"), QRegularExpression::CaseInsensitiveOption);
    if (!extension.match(file).hasMatch()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Une LUT 3D est un fichier .cube, .3dl, .dat ou .m3d."), QStringLiteral("Vérifie le nom du fichier."));
    }
    if (!QDir::isAbsolutePath(file)) {
        // Un simple nom : cherché dans les dossiers de LUT (ceux livrés avec KoraCut, puis le projet).
        QString found;
        for (const QString &folder : lutFolders()) {
            const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files);
            for (const QFileInfo &entry : entries) {
                if (entry.fileName().compare(file, Qt::CaseInsensitive) == 0) {
                    found = entry.absoluteFilePath();
                    break;
                }
            }
            if (!found.isEmpty()) {
                break;
            }
        }
        if (found.isEmpty()) {
            return error(QStringLiteral("fichier_introuvable"), QStringLiteral("Aucune LUT « %1 » dans les dossiers de LUT (%2).").arg(file, lutFolders().join(QLatin1String(", "))),
                         QStringLiteral("Donne le chemin absolu du fichier. L'outil ne télécharge rien : demande à la personne où est sa LUT."));
        }
        file = found;
    }
    if (!QFileInfo::exists(file)) {
        return error(QStringLiteral("fichier_introuvable"), QStringLiteral("Le fichier %1 n'existe pas sur le disque.").arg(file),
                     QStringLiteral("N'invente pas de chemin : l'outil ne télécharge rien. Demande à la personne où est sa LUT."));
    }
    static const QMap<QString, QString> interpolations{{QStringLiteral("tetraedrique"), QStringLiteral("tetrahedral")},
                                                       {QStringLiteral("trilineaire"), QStringLiteral("trilinear")},
                                                       {QStringLiteral("plus_proche"), QStringLiteral("nearest")}};
    const QString interpolation = args.value(QStringLiteral("interpolation")).toString(QStringLiteral("tetraedrique"));
    if (!interpolations.contains(interpolation)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("interpolation vaut tetraedrique, trilineaire ou plus_proche."));
    }
    const QString place = args.value(QStringLiteral("place")).toString(QStringLiteral("fin_de_pile"));
    if (place != QLatin1String("fin_de_pile") && place != QLatin1String("debut_de_pile")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("place vaut debut_de_pile ou fin_de_pile."));
    }
    if (!EffectsRepository::get()->exists(QStringLiteral("avfilter.lut3d"))) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet avfilter.lut3d n'est pas dans la bibliothèque de cette machine."));
    }
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    // Une LUT technique et une LUT créative peuvent coexister : on en pose toujours une nouvelle.
    const std::shared_ptr<EffectItemModel> effect =
        appendEffect(stack, QStringLiteral("avfilter.lut3d"), {{QStringLiteral("av.file"), file}, {QStringLiteral("av.interp"), interpolations.value(interpolation)}}, undo, redo);
    if (!effect) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La LUT n'a pas pu être posée sur le clip %1.").arg(cid));
    }
    if (place == QLatin1String("debut_de_pile") && effect->row() > 0) {
        stack->moveEffectWithUndo(0, effect, undo, redo);
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Apply LUT %1", QFileInfo(file).fileName()));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("effet_index"), effectRank(stack, effect)},
                       {QStringLiteral("fichier"), file},
                       {QStringLiteral("interpolation"), interpolation},
                       {QStringLiteral("place"), place}};
}

// ---------------------------------------------------------------------------------------------
// copier_etalonnage

QJsonObject copierEtalonnage(const Timeline &timeline, const Args &args)
{
    const int source = args.value(QStringLiteral("clip_source_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, source, true); !e.isEmpty()) {
        return e;
    }
    const auto sourceEffects = colourEffects(timeline->getClipEffectStackModel(source));
    if (sourceEffects.empty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le clip de référence ne porte aucun effet de couleur."), QStringLiteral("Étalonne d'abord le clip de référence."));
    }
    const QJsonArray ids = args.value(QStringLiteral("clip_cible_ids")).toArray();
    if (ids.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("clip_cible_ids est vide."));
    }
    // Tout vérifier avant de toucher à quoi que ce soit.
    QList<int> targets;
    for (const QJsonValue &v : ids) {
        const int cid = v.toInt(-1);
        if (cid == source) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Le clip de référence figure parmi les cibles."), QStringLiteral("Retire-le de clip_cible_ids."));
        }
        if (QJsonObject e = imageClipCheck(timeline, cid); !e.isEmpty()) {
            return e;
        }
        if (!targets.contains(cid)) {
            targets << cid;
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QJsonArray done;
    for (int cid : std::as_const(targets)) {
        const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
        // Les effets de couleur déjà là partent, les autres (transformer, fondus, son) restent.
        // Dans l'ordre de la pile : ses annulations se rejouent dans l'ordre chronologique
        // (PUSH_LAMBDA), et c'est ainsi que chaque effet retrouve son rang.
        for (const auto &effect : colourEffects(stack)) {
            if (effect->isBuiltIn()) {
                effect->markEnabled(false, undo, redo);
            } else {
                QString name;
                stack->removeEffectWithUndo(effect, name, undo, redo);
            }
        }
        int copied = 0;
        const PlaylistState::ClipState state = timeline->getClipState(cid).first;
        // Les effets déjà là, pour reconnaître ensuite les copies et les remettre dans l'ordre de la
        // source (la pile insère près de l'effet actif, pas toujours à la fin).
        std::vector<std::shared_ptr<EffectItemModel>> before = visibleEffects(stack);
        QStringList wanted;
        for (const auto &effect : sourceEffects) {
            if (stack->copyEffectWithUndo(effect, state, undo, redo)) {
                ++copied;
                wanted << effect->getAssetId();
            }
        }
        std::vector<std::shared_ptr<EffectItemModel>> added;
        for (const auto &effect : visibleEffects(stack)) {
            if (std::find(before.begin(), before.end(), effect) == before.end()) {
                added.push_back(effect);
            }
        }
        if (int(added.size()) == wanted.size()) {
            for (int k = 0; k < wanted.size(); ++k) {
                for (auto it = added.begin() + k; it != added.end(); ++it) {
                    if ((*it)->getAssetId() == wanted.at(k)) {
                        std::rotate(added.begin() + k, it, it + 1);
                        break;
                    }
                }
            }
            const int firstRow = stack->rowCount() - int(added.size());
            for (size_t k = 0; k < added.size(); ++k) {
                const int destRow = firstRow + int(k);
                if (added.at(k)->row() != destRow) {
                    stack->moveEffectWithUndo(destRow, added.at(k), undo, redo);
                }
            }
        }
        done.append(QJsonObject{{QStringLiteral("clip_id"), cid}, {QStringLiteral("effets_copies"), copied}});
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Copy color grade"));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("source_id"), source}, {QStringLiteral("cibles"), done}};
}

// ---------------------------------------------------------------------------------------------
// matcher_plans

QJsonObject matcherPlans(const Timeline &timeline, const Args &args)
{
    const int reference = args.value(QStringLiteral("clip_reference_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, reference, true); !e.isEmpty()) {
        return e;
    }
    const int target = args.value(QStringLiteral("clip_cible_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, target); !e.isEmpty()) {
        return e;
    }
    if (target == reference) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le clip cible est le clip de référence."));
    }
    const double force = std::clamp(args.value(QStringLiteral("force")).toDouble(0.8), 0., 1.);
    for (const char *id : {"avfilter.exposure", "avfilter.colorbalance"}) {
        if (!EffectsRepository::get()->exists(QLatin1String(id))) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet %1 n'est pas dans la bibliothèque de cette machine.").arg(QLatin1String(id)));
        }
    }
    QJsonObject failure;
    const ImageMeasure ref = measureClip(timeline, reference, -1, true, failure);
    if (!ref.ok) {
        return failure;
    }
    const ImageMeasure before = measureClip(timeline, target, -1, true, failure);
    if (!before.ok) {
        return failure;
    }
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(target);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QJsonArray posed;
    // La luma moyenne : un décalage d'exposition en diaphragmes, dosé par la force.
    const double ev = std::clamp(force * std::log2(std::max(1., ref.lumaMean) / std::max(1., before.lumaMean)), -2., 2.);
    if (std::abs(ev) >= 0.02) {
        const auto exposure = findVisibleEffect(stack, QStringLiteral("avfilter.exposure"));
        const double current = exposure ? paramNumber(exposure, QStringLiteral("av.exposure"), 0.) : 0.;
        const double value = std::clamp(current + ev, -3., 3.);
        if (uniqueEffect(stack, QStringLiteral("avfilter.exposure"), {{QStringLiteral("av.exposure"), numberText(value)}}, undo, redo)) {
            posed.append(QJsonObject{{QStringLiteral("effet_id"), QStringLiteral("avfilter.exposure")}, {QStringLiteral("parametres"), QJsonObject{{QStringLiteral("av.exposure"), value}}}});
        }
    }
    // La dominante : les tons moyens de la balance des couleurs, un point sur 255 vaut à peu près
    // 1/255 × 1,3 sur le curseur (le filtre pèse moins les extrêmes), luminosité préservée.
    const double dr = force * (ref.domR - before.domR);
    const double dg = force * (ref.domG - before.domG);
    const double db = force * (ref.domB - before.domB);
    if (std::abs(dr) >= 0.5 || std::abs(dg) >= 0.5 || std::abs(db) >= 0.5) {
        const auto balance = findVisibleEffect(stack, QStringLiteral("avfilter.colorbalance"));
        auto value = [&](const QString &name, double delta) {
            const double current = balance ? paramNumber(balance, name, 0.) : 0.;
            return std::clamp(current + delta / 255. * 1.3, -1., 1.);
        };
        const double rm = value(QStringLiteral("av.rm"), dr);
        const double gm = value(QStringLiteral("av.gm"), dg);
        const double bm = value(QStringLiteral("av.bm"), db);
        const QMap<QString, QString> params{{QStringLiteral("av.rm"), numberText(rm)},
                                            {QStringLiteral("av.gm"), numberText(gm)},
                                            {QStringLiteral("av.bm"), numberText(bm)},
                                            {QStringLiteral("av.pl"), QStringLiteral("1")}};
        if (uniqueEffect(stack, QStringLiteral("avfilter.colorbalance"), params, undo, redo)) {
            posed.append(QJsonObject{{QStringLiteral("effet_id"), QStringLiteral("avfilter.colorbalance")},
                                     {QStringLiteral("parametres"), QJsonObject{{QStringLiteral("av.rm"), rm}, {QStringLiteral("av.gm"), gm}, {QStringLiteral("av.bm"), bm}}}});
        }
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("reference"), measureJson(ref)},
                       {QStringLiteral("avant"), measureJson(before)},
                       {QStringLiteral("reglages_poses"), posed},
                       {QStringLiteral("force"), force}};
    if (posed.isEmpty()) {
        result.insert(QStringLiteral("apres"), measureJson(before));
        result.insert(QStringLiteral("remarque"), QStringLiteral("Les deux plans se ressemblent déjà : rien n'a été posé."));
        return result;
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Match shots"));
    const ImageMeasure after = measureClip(timeline, target, -1, true, failure);
    result.insert(QStringLiteral("apres"), after.ok ? measureJson(after) : measureJson(before));
    result.insert(QStringLiteral("conseil"), QStringLiteral("C'est un point de départ : vérifie les peaux avec mesurer_image et regarde l'image avec voir_image."));
    return result;
}

// ---------------------------------------------------------------------------------------------
// activer_etalonnage

QJsonObject activerEtalonnage(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    if (!args.value(QStringLiteral("actif")).isBool()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("actif vaut true ou false."));
    }
    const bool enabled = args.value(QStringLiteral("actif")).toBool();
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    if (!stack) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le clip %1 n'a pas de pile d'effets.").arg(cid));
    }
    const bool before = stack->isStackEnabled();
    if (before != enabled) {
        std::weak_ptr<EffectStackModel> weak = stack;
        Fun redo = [weak, enabled]() {
            if (auto s = weak.lock()) {
                s->setEffectStackEnabled(enabled);
            }
            return true;
        };
        Fun undo = [weak, before]() {
            if (auto s = weak.lock()) {
                s->setEffectStackEnabled(before);
            }
            return true;
        };
        redo();
        pCore->pushUndo(undo, redo, enabled ? i18nc("@action undo text", "Enable effects") : i18nc("@action undo text", "Disable effects"));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), cid}, {QStringLiteral("actif"), stack->isStackEnabled()}};
}

// ---------------------------------------------------------------------------------------------
// reinitialiser_etalonnage

QJsonObject reinitialiserEtalonnage(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = imageClipCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    const auto existing = colourEffects(stack);
    QJsonArray removed;
    if (existing.empty()) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), cid}, {QStringLiteral("effets_retires"), removed},
                           {QStringLiteral("remarque"), QStringLiteral("Le clip ne portait aucun effet de couleur.")}};
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    // Dans l'ordre de la pile (les annulations se rejouent dans l'ordre chronologique, chaque effet
    // retrouve ainsi son rang).
    for (const auto &effect : existing) {
        removed.append(effect->getAssetId());
        if (effect->isBuiltIn()) {
            effect->markEnabled(false, undo, redo);
        } else {
            QString name;
            stack->removeEffectWithUndo(effect, name, undo, redo);
        }
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Reset color grade"));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), cid}, {QStringLiteral("effets_retires"), removed}};
}

} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> etalonnageTools()
{
    return {
        {QStringLiteral("mesurer_image"), mesurerImage},         {QStringLiteral("balance_blancs"), balanceBlancs},
        {QStringLiteral("regler_exposition"), reglerExposition}, {QStringLiteral("lift_gamma_gain"), liftGammaGain},
        {QStringLiteral("regler_courbes"), reglerCourbes},       {QStringLiteral("appliquer_lut"), appliquerLut},
        {QStringLiteral("copier_etalonnage"), copierEtalonnage}, {QStringLiteral("matcher_plans"), matcherPlans},
        {QStringLiteral("activer_etalonnage"), activerEtalonnage}, {QStringLiteral("reinitialiser_etalonnage"), reinitialiserEtalonnage},
    };
}

} // namespace KoraCutAgentToolsPrivate
