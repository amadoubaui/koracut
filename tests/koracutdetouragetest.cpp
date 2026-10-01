/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// 1er octobre 2026 : detourer_sujet. La pose du sujet détouré au-dessus de son plan (même place, même
// durée, piste « Sujets détourés » au-dessus des « Animations » et sous les « Transitions », Transformer
// recopié avec ses images clés, une seule étape d'annulation), puis l'outil entier avec un faux
// programme de détourage. Le vrai (Vision, macOS) n'est pas lancé : le faux détourage est une vidéo
// ProRes 4444 avec alpha fabriquée par ffmpeg.

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "assets/keyframes/model/keyframemodel.hpp"
#include "assets/keyframes/model/keyframemodellist.hpp"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutagenttools_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
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

QJsonObject clipInState(const QJsonObject &state, int clipId)
{
    for (const QJsonValue &p : state.value(QStringLiteral("pistes")).toArray()) {
        for (const QJsonValue &c : p.toObject().value(QStringLiteral("clips")).toArray()) {
            if (c.toObject().value(QStringLiteral("id")).toInt() == clipId) {
                return c.toObject();
            }
        }
    }
    return {};
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

bool ffmpeg(const QString &program, const QStringList &args)
{
    QProcess p;
    p.start(program, QStringList{QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y")} + args);
    return p.waitForFinished(60000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// La place (rect) donnée par le Transformer d'un clip à cette image du clip, images clés comprises.
QString rectAt(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, int frame)
{
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(clipId);
    for (int row = 0; stack && row < stack->rowCount(); ++row) {
        const auto effect = std::static_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect->getAssetId() != QLatin1String("qtblend") || !effect->isAssetEnabled()) {
            continue;
        }
        const QModelIndex index = effect->getParamIndexFromName(QStringLiteral("rect"));
        const std::shared_ptr<KeyframeModelList> models = effect->getKeyframeModel();
        KeyframeModel *keys = models ? models->getKeyModel(QPersistentModelIndex(index)) : nullptr;
        if (!keys) {
            return effect->getParamFromName(QStringLiteral("rect")).toString();
        }
        // Arrondi au pixel : les deux clips n'écrivent pas forcément les mêmes décimales.
        QStringList rounded;
        const QString value = keys->getInterpolatedValue(GenTime(timeline->getClipIn(clipId) + frame, pCore->getCurrentFps())).toString();
        for (const QString &part : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            rounded << QString::number(qRound(part.toDouble()));
        }
        return rounded.join(QLatin1Char(' '));
    }
    return QString();
}

// Le Transformer du sujet donne la même place que celui du plan, du début à la fin.
bool sameTransform(const std::shared_ptr<TimelineItemModel> &timeline, int clip, int subject)
{
    for (int frame : {0, 10, 15, 30, 45}) {
        const QString wanted = rectAt(timeline, clip, frame);
        if (wanted.isEmpty() || rectAt(timeline, subject, frame) != wanted) {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("Détourage : le sujet posé au-dessus du plan", "[KoraCutDetourage]")
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
    const QString rate = QString::number(pCore->getCurrentFps(), 'f', 3);

    // Le plan (4 secondes) et le faux détourage (2 secondes, à moitié transparent).
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString plan = dir.filePath(QStringLiteral("plan.mov"));
    const QString cutout = dir.filePath(QStringLiteral("sujet.mov"));
    const bool made = ffmpeg(program, {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=s=320x240:r=%1:d=4").arg(rate),
                                       QStringLiteral("-c:v"), QStringLiteral("prores_ks"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv422p10le"), plan}) &&
                      ffmpeg(program, {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=c=red@0.5:s=320x240:r=%1:d=2,format=yuva444p10le").arg(rate),
                                       QStringLiteral("-c:v"), QStringLiteral("prores_ks"), QStringLiteral("-profile:v"), QStringLiteral("4444"), QStringLiteral("-pix_fmt"),
                                       QStringLiteral("yuva444p10le"), cutout});
    if (!made) {
        WARN("ffmpeg sans prores_ks : test sauté");
        pCore->projectManager()->closeCurrentDocument(false, false);
        return;
    }
    const int length = qRound(2 * pCore->getCurrentFps());

    // Le plan dans le chutier, puis sur la piste vidéo du bas, rogné au début : son entrée n'est pas zéro.
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
    REQUIRE(videoTracks.size() >= 2);
    const int v1 = videoTracks.first();
    int clip = -1;
    REQUIRE(timeline->requestClipInsertion(binId, v1, 40, clip));
    REQUIRE(timeline->requestItemResize(clip, length, false) == length);
    const int in = timeline->getClipIn(clip);
    const int position = timeline->getClipPosition(clip);
    REQUIRE(in > 0);
    REQUIRE(timeline->getClipPlaytime(clip) == length);

    // Un Transformer avec deux images clés : un zoom lent.
    QJsonObject r = call(timeline, QStringLiteral("transformer_clip"),
                         {{QStringLiteral("clip_id"), clip}, {QStringLiteral("x_px"), 0}, {QStringLiteral("y_px"), 0}, {QStringLiteral("largeur_px"), 1920}, {QStringLiteral("hauteur_px"), 1080}});
    INFO(dump(r));
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    for (const auto &key : {std::pair<int, QString>{0, QStringLiteral("0 0 1920 1080 1")}, std::pair<int, QString>{30, QStringLiteral("-96 -54 2112 1188 1")}}) {
        r = call(timeline, QStringLiteral("poser_keyframe"),
                 {{QStringLiteral("clip_id"), clip}, {QStringLiteral("effet_index"), 0}, {QStringLiteral("parametre"), QStringLiteral("rect")}, {QStringLiteral("position_dans_clip"), key.first}, {QStringLiteral("valeur"), key.second}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
    }
    // Le zoom est bien là : la place change entre le début et l'image 30.
    REQUIRE(rectAt(timeline, clip, 0) == QStringLiteral("0 0 1920 1080 1"));
    REQUIRE(rectAt(timeline, clip, 30) == QStringLiteral("-96 -54 2112 1188 1"));
    REQUIRE(rectAt(timeline, clip, 15) != rectAt(timeline, clip, 0));
    const int tracksBefore = timeline->getTracksCount();
    const QString mark = KoraCutAgentTools::subjectTrackProperty();

    SECTION("La pose : même place, même durée, piste créée, Transformer recopié, une étape")
    {
        const int steps = undoStack->index();
        r = KoraCutAgentToolsPrivate::placeSubjectCutout(timeline, clip, cutout);
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int subject = r.value(QStringLiteral("clip_id")).toInt(-1);
        const int track = r.value(QStringLiteral("piste_id")).toInt(-1);
        REQUIRE(timeline->isClip(subject));
        CHECK(undoStack->index() == steps + 1);
        CHECK(timeline->getClipPosition(subject) == position);
        CHECK(timeline->getClipPlaytime(subject) == length);
        CHECK(timeline->getClipIn(subject) == 0);
        CHECK(timeline->getClipTrackId(subject) == track);
        CHECK(r.value(QStringLiteral("piste_creee")).toBool());
        CHECK(timeline->getTracksCount() == tracksBefore + 1);
        CHECK(timeline->getTrackPosition(track) > timeline->getTrackPosition(v1));
        CHECK_FALSE(timeline->getTrackProperty(track, mark).toString().isEmpty());
        CHECK_FALSE(timeline->getTrackProperty(track, QStringLiteral("kdenlive:track_name")).toString().isEmpty());
        // Le chutier : le détourage est rangé dans son dossier.
        const QString mediaId = r.value(QStringLiteral("media_id")).toString();
        REQUIRE(binModel->getClipByBinID(mediaId));
        CHECK(binModel->getClipByBinID(mediaId)->url() == cutout);
        CHECK_FALSE(KoraCutAgentTools::binFolderPath(mediaId).isEmpty());
        // Le Transformer suit, ses images clés ramenées au début du clip détouré (entrée zéro).
        CHECK(r.value(QStringLiteral("effets_copies")).toArray().contains(QStringLiteral("qtblend")));
        INFO(rectAt(timeline, subject, 0).toStdString() << " / " << rectAt(timeline, subject, 15).toStdString() << " / " << rectAt(timeline, subject, 30).toStdString());
        CHECK(sameTransform(timeline, clip, subject));
        CHECK(rectAt(timeline, subject, 30) == QStringLiteral("-96 -54 2112 1188 1"));

        // Annuler retire le sujet et sa piste, rétablir les remet.
        undoStack->undo();
        CHECK(timeline->getTracksCount() == tracksBefore);
        CHECK(timeline->getItemsInRange(v1, 0, -1, false).size() == 1);
        undoStack->redo();
        CHECK(timeline->getTracksCount() == tracksBefore + 1);
        const int again = [&]() {
            for (int id : timeline->getItemsInRange(track, 0, -1, false)) {
                return id;
            }
            return -1;
        }();
        REQUIRE(timeline->isClip(again));
        CHECK(timeline->getClipPosition(again) == position);
        CHECK(sameTransform(timeline, clip, again));

        // Une seconde pose sur le même plan : la place est prise, une piste de plus, le même média.
        const QJsonObject second = KoraCutAgentToolsPrivate::placeSubjectCutout(timeline, clip, cutout);
        INFO(dump(second));
        REQUIRE(second.value(QStringLiteral("ok")).toBool());
        CHECK(second.value(QStringLiteral("piste_id")).toInt() != track);
        CHECK(second.value(QStringLiteral("piste_creee")).toBool());
        CHECK(second.value(QStringLiteral("media_id")).toString() == mediaId);
        // Une piste donnée : sous le plan, refusée ; prise, refusée.
        CHECK(errorCode(KoraCutAgentToolsPrivate::placeSubjectCutout(timeline, clip, cutout, v1)) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(KoraCutAgentToolsPrivate::placeSubjectCutout(timeline, clip, cutout, track)) == QStringLiteral("place_occupee"));
        CHECK(errorCode(KoraCutAgentToolsPrivate::placeSubjectCutout(timeline, 99999, cutout)) == QStringLiteral("id_inconnu"));
    }

    SECTION("L'ordre des pistes : au-dessus des « Animations », sous les « Transitions »")
    {
        int animations = -1;
        int transitions = -1;
        REQUIRE(timeline->requestTrackInsertion(-1, animations, QStringLiteral("Animations")));
        REQUIRE(timeline->requestTrackInsertion(-1, transitions, QStringLiteral("Transitions")));
        timeline->setTrackProperty(animations, QStringLiteral("kdenlive:koracut_motion"), QStringLiteral("*animations"));
        timeline->setTrackProperty(transitions, QStringLiteral("kdenlive:koracut_motion"), QStringLiteral("*transitions"));
        REQUIRE(timeline->getTrackPosition(transitions) > timeline->getTrackPosition(animations));
        r = KoraCutAgentToolsPrivate::placeSubjectCutout(timeline, clip, cutout);
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int track = r.value(QStringLiteral("piste_id")).toInt(-1);
        CHECK(timeline->getTrackPosition(track) > timeline->getTrackPosition(animations));
        CHECK(timeline->getTrackPosition(track) < timeline->getTrackPosition(transitions));
        CHECK(timeline->getTrackPosition(animations) > timeline->getTrackPosition(v1));
    }

#ifndef Q_OS_WIN
    SECTION("L'outil : le programme lancé sur l'extrait du plan, le fichier repris, les refus")
    {
        // Le faux programme : il recopie le détourage tout fait à l'endroit demandé.
        const QString fake = dir.filePath(QStringLiteral("faux-detourer.sh"));
        {
            QFile f(fake);
            REQUIRE(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\n"
                    "echo \"$@\" > \"$KORACUT_DETOURER_FAUX_ARGS\"\n"
                    "while [ $# -gt 0 ]; do case \"$1\" in --sortie) out=\"$2\"; shift;; esac; shift; done\n"
                    "if [ -n \"$KORACUT_DETOURER_FAUX_VIDE\" ]; then echo 'sujet_introuvable' >&2; exit 3; fi\n"
                    "echo 'koracut-detourer : 50/50 images' >&2\n"
                    "cp \"$KORACUT_DETOURER_FAUX\" \"$out\" && echo '{\"ok\":true}'\n");
        }
        REQUIRE(QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString folder = dir.filePath(QStringLiteral("detourages"));
        const QString argsFile = dir.filePath(QStringLiteral("args.txt"));
        qputenv("KORACUT_DETOURER", fake.toUtf8());
        qputenv("KORACUT_DETOURER_FAUX", cutout.toUtf8());
        qputenv("KORACUT_DETOURER_FAUX_ARGS", argsFile.toUtf8());

        // Un projet jamais enregistré n'a pas de dossier où ranger le détourage.
        qunsetenv("KORACUT_DETOURAGE_DOSSIER");
        CHECK(errorCode(call(timeline, QStringLiteral("detourer_sujet"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("sujet"), QStringLiteral("personne")}})) ==
              QStringLiteral("projet_absent"));
        qputenv("KORACUT_DETOURAGE_DOSSIER", folder.toUtf8());

        const int steps = undoStack->index();
        r = call(timeline, QStringLiteral("detourer_sujet"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("sujet"), QStringLiteral("personne")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == steps + 1);
        const QString file = r.value(QStringLiteral("fichier")).toString();
        CHECK(file.startsWith(folder + QLatin1Char('/')));
        CHECK(QFileInfo::exists(file));
        CHECK(QFileInfo(file).fileName().startsWith(QStringLiteral("plan - personne %1-%2 ").arg(in).arg(in + length)));
        CHECK_FALSE(r.value(QStringLiteral("deja_calcule")).toBool());
        const int subject = r.value(QStringLiteral("clip_id")).toInt(-1);
        REQUIRE(timeline->isClip(subject));
        CHECK(timeline->getClipPosition(subject) == position);
        CHECK(timeline->getClipPlaytime(subject) == length);
        // Le programme a reçu l'entrée et la durée du clip, en secondes.
        QFile seen(argsFile);
        REQUIRE(seen.open(QIODevice::ReadOnly));
        const QString line = QString::fromUtf8(seen.readAll());
        INFO(line.toStdString());
        CHECK(line.contains(QStringLiteral("--debut %1").arg(QString::number(in / pCore->getCurrentFps(), 'f', 6))));
        CHECK(line.contains(QStringLiteral("--duree %1").arg(QString::number(length / pCore->getCurrentFps(), 'f', 6))));
        CHECK(line.contains(QStringLiteral("--sujet personne")));

        // Annulé puis redemandé : le fichier est repris, rien n'est recalculé.
        undoStack->undo();
        CHECK_FALSE(timeline->isClip(subject));
        QFile::remove(argsFile);
        r = call(timeline, QStringLiteral("detourer_sujet"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("sujet"), QStringLiteral("personne")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("deja_calcule")).toBool());
        CHECK_FALSE(QFileInfo::exists(argsFile));

        // Rien à détourer : le programme sort avec le code 3.
        qputenv("KORACUT_DETOURER_FAUX_VIDE", "1");
        CHECK(errorCode(call(timeline, QStringLiteral("detourer_sujet"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("sujet"), QStringLiteral("premier_plan")}})) ==
              QStringLiteral("sujet_introuvable"));
        qunsetenv("KORACUT_DETOURER_FAUX_VIDE");

        // Les entrées refusées, puis le programme absent.
        CHECK(errorCode(call(timeline, QStringLiteral("detourer_sujet"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("sujet"), QStringLiteral("visage")}})) ==
              QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("detourer_sujet"), {{QStringLiteral("clip_id"), 99999}, {QStringLiteral("sujet"), QStringLiteral("personne")}})) ==
              QStringLiteral("id_inconnu"));
        qputenv("KORACUT_DETOURER", dir.filePath(QStringLiteral("absent")).toUtf8());
        CHECK(errorCode(call(timeline, QStringLiteral("detourer_sujet"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("sujet"), QStringLiteral("premier_plan")}})) ==
              QStringLiteral("non_disponible"));
        qunsetenv("KORACUT_DETOURER");
        qunsetenv("KORACUT_DETOURER_FAUX");
        qunsetenv("KORACUT_DETOURER_FAUX_ARGS");
        qunsetenv("KORACUT_DETOURAGE_DOSSIER");
    }
#endif

    pCore->projectManager()->closeCurrentDocument(false, false);
}
