/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// 8 octobre 2026 : flouter_zone et flous_lister. Un plan d'essai fabriqué par ffmpeg (un carré de mire
// qui glisse sur un fond gris), posé avec une entrée non nulle. Le flou suivi : l'effet, ses images
// clés comptées depuis le début du média, la zone qui arrive où le carré arrive, une seule étape
// d'annulation. Le rectangle fixe : rangé avant le Transformer du plan. Le suivi passe par melt ; sans
// melt, cette partie est sautée et dite.

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "effects/effectsrepository.hpp"
#include "koracut/koracutagenttools.h"
#include "profiles/profilemodel.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

using KoraCutAgentTools::call;

namespace {
std::string dump(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)).toStdString();
}

QString errorCode(const QJsonObject &r)
{
    return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString();
}

// Le ffmpeg de Homebrew d'abord : celui de Craft ne se lance pas hors de l'application.
QString ffmpegProgram()
{
    for (const QString &candidate : {QStringLiteral("/opt/homebrew/bin/ffmpeg"), QStringLiteral("/usr/local/bin/ffmpeg")}) {
        if (QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

// Le rang de pile du premier effet de cet identifiant, -1 s'il n'y est pas.
int rowOf(const std::shared_ptr<EffectStackModel> &stack, const QString &effectId)
{
    for (int row = 0; stack && row < stack->rowCount(); ++row) {
        const auto effect = std::static_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect->getAssetId() == effectId) {
            return row;
        }
    }
    return -1;
}

std::shared_ptr<EffectItemModel> effectOf(const std::shared_ptr<EffectStackModel> &stack, const QString &effectId)
{
    const int row = rowOf(stack, effectId);
    return row < 0 ? nullptr : std::static_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
}

QJsonObject zone(double x, double y, double w, double h)
{
    return QJsonObject{{QStringLiteral("x_px"), x}, {QStringLiteral("y_px"), y}, {QStringLiteral("largeur_px"), w}, {QStringLiteral("hauteur_px"), h}};
}
} // namespace

TEST_CASE("Flouter une zone : le flou suivi, le rectangle fixe, l'annulation", "[KoraCutFlou]")
{
    const QString program = ffmpegProgram();
    if (program.isEmpty()) {
        WARN("ffmpeg absent : test sauté");
        return;
    }
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    auto binModel = pCore->projectItemModel();
    const double fps = pCore->getCurrentFps();
    const QString rate = QString::number(fps, 'f', 3);
    const QSize profile = pCore->getCurrentFrameSize();

    // Le plan : 4 secondes, 640 pixels de large et les proportions du projet, un carré de mire de 80 × 60
    // qui part de (100, 80) et glisse de 50 pixels par seconde vers la droite, 25 vers le bas. Tout en
    // images entières (ProRes).
    const double shape = profile.width() * pCore->getCurrentProfile()->sar() / profile.height();
    const int sourceHeight = 2 * qRound(640 / shape / 2);
    REQUIRE(sourceHeight >= 300);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString plan = dir.filePath(QStringLiteral("plan.mov"));
    QProcess make;
    make.start(program, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                         QStringLiteral("color=c=0x808080:s=640x%2:r=%1:d=4").arg(rate).arg(sourceHeight), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                         QStringLiteral("smptebars=s=80x60:r=%1:d=4").arg(rate), QStringLiteral("-filter_complex"), QStringLiteral("[0][1]overlay=x='100+50*t':y='80+25*t'"),
                         QStringLiteral("-c:v"), QStringLiteral("prores_ks"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv422p10le"), plan});
    if (!make.waitForFinished(60000) || make.exitStatus() != QProcess::NormalExit || make.exitCode() != 0 || !QFileInfo::exists(plan)) {
        WARN("ffmpeg sans prores_ks ou sans lavfi : test sauté");
        pCore->projectManager()->closeCurrentDocument(false, false);
        return;
    }
    bool ready = false;
    QString binId;
    {
        // Ces deux fonctions gardent le clip en vie : elles doivent mourir avant la fermeture du document.
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        binId = ClipCreator::createClipFromFile(plan, binModel->getRootFolder()->clipId(), binModel, undo, redo, [&ready](const QString &) { ready = true; });
    }
    REQUIRE_FALSE(binId.isEmpty());
    QElapsedTimer timer;
    timer.start();
    while (!ready && timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    REQUIRE(ready);
    QList<int> videoTracks = timeline->getTracksIds(false);
    std::sort(videoTracks.begin(), videoTracks.end(), [&timeline](int a, int b) { return timeline->getTrackPosition(a) < timeline->getTrackPosition(b); });
    REQUIRE_FALSE(videoTracks.isEmpty());
    // Deux secondes, rognées au début : l'entrée du plan n'est pas zéro.
    const int length = qRound(2 * fps);
    int clip = -1;
    REQUIRE(timeline->requestClipInsertion(binId, videoTracks.first(), 40, clip));
    REQUIRE(timeline->requestItemResize(clip, length, false) == length);
    const int in = timeline->getClipIn(clip);
    REQUIRE(in > 0);
    const auto stack = timeline->getClipEffectStackModel(clip);
    REQUIRE(stack);
    const QString tracker = QStringLiteral("opencv.tracker");
    // Où est le carré, en pixels de la source, à une image du plan.
    const auto squareAt = [&](int frameInClip) -> QPointF {
        const double t = (in + frameInClip) / fps;
        return QPointF(100 + 50 * t, 80 + 25 * t);
    };
    const QPointF first = squareAt(0);
    const QPointF last = squareAt(length - 1);
    REQUIRE(last.x() - first.x() > 80);

    // Sans le module OpenCV de MLT (le MLT de MSYS2 sous Windows ne l'a pas), l'outil doit le dire et ne rien
    // poser : c'est tout ce qu'on peut vérifier sur une telle machine.
    if (!EffectsRepository::get()->exists(tracker)) {
        const int steps = undoStack->index();
        CHECK(errorCode(call(timeline, QStringLiteral("flouter_zone"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("zone"), zone(10, 10, 50, 50)}, {QStringLiteral("suivre"), false}})) ==
              QStringLiteral("non_disponible"));
        CHECK(undoStack->index() == steps);
        WARN("module de suivi OpenCV absent de MLT : seul le refus a été joué");
        pCore->projectManager()->closeCurrentDocument(false, false);
        return;
    }

    SECTION("Les refus")
    {
        const int steps = undoStack->index();
        CHECK(errorCode(call(timeline, QStringLiteral("flouter_zone"), {{QStringLiteral("clip_id"), 99999}, {QStringLiteral("zone"), zone(10, 10, 50, 50)}})) == QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("flouter_zone"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("zone"), zone(5000, 10, 50, 50)}, {QStringLiteral("suivre"), false}})) ==
              QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("flouter_zone"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("zone"), zone(10, 10, 2, 50)}, {QStringLiteral("suivre"), false}})) ==
              QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("flouter_zone"),
                             {{QStringLiteral("clip_id"), clip}, {QStringLiteral("zone"), zone(10, 10, 50, 50)}, {QStringLiteral("position_dans_clip"), length + 5}, {QStringLiteral("suivre"), false}})) ==
              QStringLiteral("hors_clip"));
        CHECK(errorCode(call(timeline, QStringLiteral("flouter_zone"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("zone"), zone(10, 10, 50, 50)}, {QStringLiteral("type"), QStringLiteral("mosaique")}})) ==
              QStringLiteral("entree_invalide"));
        CHECK(undoStack->index() == steps);
        CHECK(rowOf(stack, tracker) == -1);
    }

    SECTION("Le rectangle fixe : avant le Transformer, deux images clés, une étape, listé puis retiré")
    {
        QJsonObject r = call(timeline, QStringLiteral("transformer_clip"),
                             {{QStringLiteral("clip_id"), clip}, {QStringLiteral("x_px"), -96}, {QStringLiteral("y_px"), -54}, {QStringLiteral("largeur_px"), qRound(profile.width() * 1.1)},
                              {QStringLiteral("hauteur_px"), qRound(profile.height() * 1.1)}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const auto transform = effectOf(stack, QStringLiteral("qtblend"));
        REQUIRE(transform);
        const int steps = undoStack->index();
        r = call(timeline, QStringLiteral("flouter_zone"),
                 {{QStringLiteral("clip_id"), clip}, {QStringLiteral("zone"), zone(first.x(), first.y(), 80, 60)}, {QStringLiteral("suivre"), false}, {QStringLiteral("type"), QStringLiteral("plein")},
                  {QStringLiteral("force"), 80}, {QStringLiteral("etiquette"), QStringLiteral("numéro de téléphone")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == steps + 1);
        CHECK_FALSE(r.value(QStringLiteral("suivi")).toBool());
        CHECK(r.value(QStringLiteral("images_cles")).toInt() == 2);
        CHECK(r.value(QStringLiteral("zone_debut")).toObject() == r.value(QStringLiteral("zone_fin")).toObject());
        CHECK(r.value(QStringLiteral("duree_calcul_ms")).toInt() == 0);
        const auto blur = effectOf(stack, tracker);
        REQUIRE(blur);
        CHECK(blur->getParamFromName(QStringLiteral("shape_width")).toInt() == 0);
        CHECK(blur->getParamFromName(QStringLiteral("blur")).toInt() == 80);
        CHECK(blur->getParamFromName(QStringLiteral("blur_type")).toInt() == 3);
        // Un Transformer ordinaire et une source aux proportions du projet : le flou passe avant lui et
        // reste dans le repère de la source (ramenée à la taille du projet).
        if (!transform->isBuiltIn()) {
            CHECK(r.value(QStringLiteral("place")).toString() == QStringLiteral("avant_transformer"));
            CHECK(rowOf(stack, tracker) < rowOf(stack, QStringLiteral("qtblend")));
            const double scale = profile.width() / 640.;
            const QString results = blur->getParamFromName(QStringLiteral("results")).toString();
            INFO(results.toStdString());
            const QStringList keys = results.split(QLatin1Char(';'));
            REQUIRE(keys.size() == 2);
            CHECK(keys.first().section(QLatin1Char('='), 0, 0).toInt() == in);
            CHECK(keys.last().section(QLatin1Char('='), 0, 0).toInt() == in + length - 1);
            const QStringList rect = keys.first().section(QLatin1Char('='), 1).split(QLatin1Char(' '));
            REQUIRE(rect.size() >= 4);
            // La zone, plus sa petite marge (12 pixels au plus).
            CHECK(std::abs(rect.at(0).toDouble() - first.x() * scale) <= 13);
            CHECK(std::abs(rect.at(2).toDouble() - 80 * scale) <= 25);
        } else {
            CHECK(r.value(QStringLiteral("place")).toString() == QStringLiteral("apres_transformer"));
        }
        // La zone rendue est dans le repère demandé (la source), marge comprise.
        CHECK(std::abs(r.value(QStringLiteral("zone_debut")).toObject().value(QStringLiteral("x_px")).toDouble() - first.x()) <= 6);
        CHECK(std::abs(r.value(QStringLiteral("zone_debut")).toObject().value(QStringLiteral("largeur_px")).toDouble() - 80) <= 10);

        // flous_lister le retrouve, supprimer_effet le retire.
        QJsonObject list = call(timeline, QStringLiteral("flous_lister"), {});
        INFO(dump(list));
        REQUIRE(list.value(QStringLiteral("nombre")).toInt() == 1);
        const QJsonObject found = list.value(QStringLiteral("flous")).toArray().first().toObject();
        CHECK(found.value(QStringLiteral("clip_id")).toInt() == clip);
        CHECK(found.value(QStringLiteral("etiquette")).toString() == QStringLiteral("numéro de téléphone"));
        CHECK(found.value(QStringLiteral("type")).toString() == QStringLiteral("plein"));
        CHECK(found.value(QStringLiteral("effet_index")).toInt() == r.value(QStringLiteral("effet_index")).toInt());
        CHECK_FALSE(found.value(QStringLiteral("suivi")).toBool());

        // Annuler retire le flou et laisse le Transformer ; rétablir le remet à sa place.
        undoStack->undo();
        CHECK(rowOf(stack, tracker) == -1);
        CHECK(rowOf(stack, QStringLiteral("qtblend")) >= 0);
        CHECK(call(timeline, QStringLiteral("flous_lister"), {{QStringLiteral("clip_id"), clip}}).value(QStringLiteral("nombre")).toInt() == 0);
        undoStack->redo();
        REQUIRE(rowOf(stack, tracker) >= 0);
        if (!transform->isBuiltIn()) {
            CHECK(rowOf(stack, tracker) < rowOf(stack, QStringLiteral("qtblend")));
        }
        r = call(timeline, QStringLiteral("supprimer_effet"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("effet_index"), found.value(QStringLiteral("effet_index")).toInt()}});
        INFO(dump(r));
        CHECK(r.value(QStringLiteral("ok")).toBool());
        CHECK(rowOf(stack, tracker) == -1);

        // Le repère « image » : la zone donnée dans l'image du projet revient au même endroit.
        const QJsonObject asked = zone(400, 300, 200, 100);
        r = call(timeline, QStringLiteral("flouter_zone"),
                 {{QStringLiteral("clip_id"), clip}, {QStringLiteral("zone"), asked}, {QStringLiteral("repere"), QStringLiteral("image")}, {QStringLiteral("suivre"), false}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const QJsonObject back = r.value(QStringLiteral("zone_debut")).toObject();
        CHECK(std::abs(back.value(QStringLiteral("x_px")).toDouble() - 400) <= 14);
        CHECK(std::abs(back.value(QStringLiteral("y_px")).toDouble() - 300) <= 14);
        CHECK(std::abs(back.value(QStringLiteral("largeur_px")).toDouble() - 200) <= 28);
    }

    SECTION("Le flou suivi : la zone arrive où le carré arrive, images clés comptées depuis le début du média")
    {
        const int steps = undoStack->index();
        QJsonObject r = call(timeline, QStringLiteral("flouter_zone"),
                             {{QStringLiteral("clip_id"), clip}, {QStringLiteral("zone"), zone(first.x(), first.y(), 80, 60)}, {QStringLiteral("etiquette"), QStringLiteral("nom WhatsApp")}});
        INFO(dump(r));
        if (errorCode(r) == QStringLiteral("non_disponible")) {
            WARN("melt ou le module de suivi OpenCV est introuvable : le suivi réel n'a pas été joué");
            CHECK(undoStack->index() == steps);
        } else {
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(undoStack->index() == steps + 1);
            CHECK(r.value(QStringLiteral("suivi")).toBool());
            CHECK_FALSE(r.contains(QStringLiteral("suivi_perdu")));
            CHECK(r.value(QStringLiteral("images_cles")).toInt() >= 2);
            CHECK(r.value(QStringLiteral("place")).toString() == QStringLiteral("sans_transformer"));
            const auto blur = effectOf(stack, tracker);
            REQUIRE(blur);
            CHECK(blur->getParamFromName(QStringLiteral("shape_width")).toInt() == 0);
            CHECK(blur->getParamFromName(QStringLiteral("blur")).toInt() == 40);
            CHECK(blur->getParamFromName(QStringLiteral("blur_type")).toInt() == 1);
            const QString results = blur->getParamFromName(QStringLiteral("results")).toString();
            INFO(results.toStdString());
            const QStringList keys = results.split(QLatin1Char(';'));
            REQUIRE(keys.size() >= 2);
            CHECK(keys.first().section(QLatin1Char('='), 0, 0).toInt() == in);
            CHECK(keys.last().section(QLatin1Char('='), 0, 0).toInt() == in + length - 1);
            // La zone a suivi le carré, à quelques pixels près (marge comprise), en pixels de la source.
            const QJsonObject begin = r.value(QStringLiteral("zone_debut")).toObject();
            const QJsonObject end = r.value(QStringLiteral("zone_fin")).toObject();
            CHECK(std::abs(begin.value(QStringLiteral("x_px")).toDouble() - first.x()) <= 8);
            CHECK(std::abs(begin.value(QStringLiteral("y_px")).toDouble() - first.y()) <= 8);
            CHECK(std::abs(end.value(QStringLiteral("x_px")).toDouble() - last.x()) <= 16);
            CHECK(std::abs(end.value(QStringLiteral("y_px")).toDouble() - last.y()) <= 16);
            // L'état de la timeline montre l'effet et ses images clés, ramenées au début du clip.
            const QJsonObject list = call(timeline, QStringLiteral("flous_lister"), {});
            INFO(dump(list));
            REQUIRE(list.value(QStringLiteral("nombre")).toInt() == 1);
            CHECK(list.value(QStringLiteral("flous")).toArray().first().toObject().value(QStringLiteral("suivi")).toBool());
            CHECK(list.value(QStringLiteral("flous")).toArray().first().toObject().value(QStringLiteral("debut_dans_clip")).toInt() == 0);
            // Annuler retire l'effet en une fois.
            undoStack->undo();
            CHECK(undoStack->index() == steps);
            CHECK(rowOf(stack, tracker) == -1);
            undoStack->redo();
            CHECK(rowOf(stack, tracker) >= 0);
            CHECK(effectOf(stack, tracker)->getParamFromName(QStringLiteral("results")).toString() == results);
        }
    }

    pCore->projectManager()->closeCurrentDocument(false, false);
}
