/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Les outils d'organisation (Agents in-app/outils/organisation.json, 25 septembre 2026) : dossiers et
// noms du chutier, fichiers dans le dossier du projet, séquences, pistes et clips. Chaque section
// vérifie le cas normal, un refus, et qu'un appel se défait d'un seul Annuler.

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutvisible.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using KoraCutAgentTools::call;

namespace {
QString errorCode(const QJsonObject &r)
{
    return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString();
}

std::string dump(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)).toStdString();
}

QString parentName(const QString &binId)
{
    auto clip = pCore->projectItemModel()->getClipByBinID(binId);
    auto parent = clip ? clip->parent() : nullptr;
    return parent && parent != pCore->projectItemModel()->getRootFolder() ? parent->name() : QString();
}

QString clipResource(const std::shared_ptr<TimelineItemModel> &timeline, int cid)
{
    return timeline->data(timeline->makeClipIndexFromID(cid), TimelineModel::ResourceRole).toString();
}
} // namespace

TEST_CASE("Outils d'organisation : chutier, fichiers, séquences, pistes, clips", "[KoraCutAgentTools]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    // Sans fenêtre, le montage visible ne fait rien et ne fait jamais attendre.
    CHECK_FALSE(KoraCutVisible::self()->active());
    CHECK(KoraCutVisible::self()->pauseMs() == 0);

    // Le projet est sur le disque, avec deux rushs dedans et un troisième ailleurs.
    QTemporaryDir project;
    QTemporaryDir elsewhere;
    REQUIRE(project.isValid());
    REQUIRE(elsewhere.isValid());
    const QString projectFile = project.path() + QStringLiteral("/essai.koracut");
    document.setUrl(QUrl::fromLocalFile(projectFile));
    {
        QFile marker(projectFile);
        REQUIRE(marker.open(QIODevice::WriteOnly));
    }
    const QString rushA = project.path() + QStringLiteral("/C0001.mkv");
    const QString rushB = project.path() + QStringLiteral("/C0002.mkv");
    const QString rushOut = elsewhere.path() + QStringLiteral("/C0003.mkv");
    REQUIRE(QFile::copy(sourcesPath + QStringLiteral("/small.mkv"), rushA));
    REQUIRE(QFile::copy(sourcesPath + QStringLiteral("/small.mkv"), rushB));
    REQUIRE(QFile::copy(sourcesPath + QStringLiteral("/small.mkv"), rushOut));
    const QString idA = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, rushA);
    const QString idB = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, rushB);
    const QString idOut = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, rushOut);
    const QString red = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 40, true);
    const int v1 = timeline->getTracksIds(false).first();
    undoStack->clear();

    SECTION("Dossiers du chutier : créer, lister, renommer, ranger, un Annuler par appel")
    {
        // Le document neuf a peut-être déjà ses dossiers (séquences) : on compte à partir d'eux.
        const int folders0 = int(call(timeline, QStringLiteral("lister_dossiers_chutier"), {}).value(QStringLiteral("dossiers")).toArray().size());
        QJsonObject r = call(timeline, QStringLiteral("creer_dossier_chutier"), {{QStringLiteral("nom"), QStringLiteral("01 Rushs")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK_FALSE(r.value(QStringLiteral("existait")).toBool());
        CHECK(undoStack->index() == 1);
        const QString rushs = r.value(QStringLiteral("dossier_id")).toString();
        // Le même nom (sans accent ni majuscule près) rend le dossier existant, sans étape.
        r = call(timeline, QStringLiteral("creer_dossier_chutier"), {{QStringLiteral("nom"), QStringLiteral("01 rushs")}});
        CHECK(r.value(QStringLiteral("existait")).toBool());
        CHECK(r.value(QStringLiteral("dossier_id")).toString() == rushs);
        CHECK(undoStack->index() == 1);
        // Un sous-dossier par chemin, puis la liste.
        r = call(timeline, QStringLiteral("creer_dossier_chutier"), {{QStringLiteral("nom"), QStringLiteral("Jour 1")}, {QStringLiteral("parent"), QStringLiteral("01 Rushs")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("chemin")).toString() == QStringLiteral("01 Rushs/Jour 1"));
        CHECK(errorCode(call(timeline, QStringLiteral("creer_dossier_chutier"), {{QStringLiteral("nom"), QStringLiteral("a/b")}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("creer_dossier_chutier"), {{QStringLiteral("nom"), QStringLiteral("x")}, {QStringLiteral("parent"), QStringLiteral("Nulle part")}})) ==
              QStringLiteral("id_inconnu"));
        r = call(timeline, QStringLiteral("lister_dossiers_chutier"), {});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("dossiers")).toArray().size() == folders0 + 2);
        CHECK(r.value(QStringLiteral("racine_medias")).toInt() == 4);
        CHECK(QFileInfo(r.value(QStringLiteral("dossier_projet")).toString()).canonicalFilePath() == QFileInfo(project.path()).canonicalFilePath());

        // Ranger deux médias : une étape, qu'un Annuler défait entièrement.
        const int before = undoStack->index();
        r = call(timeline, QStringLiteral("ranger_media"), {{QStringLiteral("media_ids"), QJsonArray{idA, idB}}, {QStringLiteral("dossier"), QStringLiteral("01 Rushs/Jour 1")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("ranges")).toArray().size() == 2);
        CHECK(parentName(idA) == QStringLiteral("Jour 1"));
        CHECK(undoStack->index() == before + 1);
        CHECK(QFile::exists(rushA)); // disque false : le fichier ne bouge pas
        r = call(timeline, QStringLiteral("ranger_media"), {{QStringLiteral("media_ids"), QJsonArray{idA}}, {QStringLiteral("dossier"), QStringLiteral("Jour 1")}});
        CHECK(r.value(QStringLiteral("deja_la")).toArray().size() == 1);
        CHECK(undoStack->index() == before + 1);
        undoStack->undo();
        CHECK(parentName(idA).isEmpty());
        undoStack->redo();
        CHECK(parentName(idB) == QStringLiteral("Jour 1"));
        // Un dossier qui n'existe pas se crée dans la même étape.
        r = call(timeline, QStringLiteral("ranger_media"), {{QStringLiteral("media_ids"), QJsonArray{red}}, {QStringLiteral("dossier"), QStringLiteral("04 Graphismes")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(parentName(red) == QStringLiteral("04 Graphismes"));
        undoStack->undo();
        CHECK(parentName(red).isEmpty());
        CHECK(call(timeline, QStringLiteral("lister_dossiers_chutier"), {}).value(QStringLiteral("dossiers")).toArray().size() == folders0 + 2);
        CHECK(errorCode(call(timeline, QStringLiteral("ranger_media"), {{QStringLiteral("media_ids"), QJsonArray{QStringLiteral("999")}}, {QStringLiteral("dossier"), QStringLiteral("x")}})) ==
              QStringLiteral("id_inconnu"));

        // Renommer un dossier : collision refusée, sinon une étape.
        call(timeline, QStringLiteral("creer_dossier_chutier"), {{QStringLiteral("nom"), QStringLiteral("02 Son")}});
        CHECK(errorCode(call(timeline, QStringLiteral("renommer_dossier_chutier"), {{QStringLiteral("dossier"), QStringLiteral("02 Son")}, {QStringLiteral("nom"), QStringLiteral("01 Rushs")}})) ==
              QStringLiteral("existe_deja"));
        const int beforeRename = undoStack->index();
        r = call(timeline, QStringLiteral("renommer_dossier_chutier"), {{QStringLiteral("dossier"), QStringLiteral("02 Son")}, {QStringLiteral("nom"), QStringLiteral("02 Sons")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("avant")).toString() == QStringLiteral("02 Son"));
        CHECK(undoStack->index() == beforeRename + 1);
        undoStack->undo();
        CHECK(binModel->getFolderByBinId(r.value(QStringLiteral("dossier_id")).toString())->name() == QStringLiteral("02 Son"));
    }

    SECTION("Noms : média du chutier, clip de la timeline, piste")
    {
        QJsonObject r = call(timeline, QStringLiteral("renommer_media"), {{QStringLiteral("media_id"), idA}, {QStringLiteral("nom"), QStringLiteral("01 - Accroche Awa.mkv")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(binModel->getClipByBinID(idA)->clipName() == QStringLiteral("01 - Accroche Awa.mkv"));
        CHECK(undoStack->index() == 1);
        CHECK(QFile::exists(rushA)); // le fichier ne change pas
        CHECK(errorCode(call(timeline, QStringLiteral("renommer_media"), {{QStringLiteral("media_id"), idB}, {QStringLiteral("nom"), QStringLiteral("01 - accroche awa.mkv")}})) ==
              QStringLiteral("existe_deja"));
        undoStack->undo();
        CHECK(binModel->getClipByBinID(idA)->clipName() == QStringLiteral("C0001.mkv"));

        // Le nom propre d'un clip : il ne touche pas au média, il suit les deux moitiés d'une coupe.
        int cid = -1;
        REQUIRE(timeline->requestClipInsertion(red, v1, 0, cid, true, true, false));
        undoStack->clear();
        r = call(timeline, QStringLiteral("renommer_clip"), {{QStringLiteral("clip_id"), cid}, {QStringLiteral("nom"), QStringLiteral("Accroche")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(timeline->getClipName(cid) == QStringLiteral("Accroche"));
        CHECK(binModel->getClipByBinID(red)->clipName() != QStringLiteral("Accroche"));
        CHECK(undoStack->index() == 1);
        REQUIRE(call(timeline, QStringLiteral("couper_clip"), {{QStringLiteral("clip_id"), cid}, {QStringLiteral("position"), 20}}).value(QStringLiteral("ok")).toBool());
        const int right = timeline->getClipByPosition(v1, 25);
        CHECK(right != cid);
        CHECK(timeline->getClipName(right) == QStringLiteral("Accroche"));
        undoStack->undo(); // la coupe
        undoStack->undo(); // le nom
        CHECK(timeline->getClipName(cid) == binModel->getClipByBinID(red)->clipName());
        // Vide : le clip reprend le nom de son média.
        call(timeline, QStringLiteral("renommer_clip"), {{QStringLiteral("clip_id"), cid}, {QStringLiteral("nom"), QStringLiteral("B-roll")}});
        r = call(timeline, QStringLiteral("renommer_clip"), {{QStringLiteral("clip_id"), cid}, {QStringLiteral("nom"), QString()}});
        CHECK(r.value(QStringLiteral("nom")).toString() == binModel->getClipByBinID(red)->clipName());
        CHECK(errorCode(call(timeline, QStringLiteral("renommer_clip"), {{QStringLiteral("clip_id"), 9999}, {QStringLiteral("nom"), QStringLiteral("x")}})) == QStringLiteral("id_inconnu"));

        // La piste : une étape, rien si le nom ne change pas.
        undoStack->clear();
        r = call(timeline, QStringLiteral("renommer_piste"), {{QStringLiteral("piste_id"), v1}, {QStringLiteral("nom"), QStringLiteral("Plans")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(timeline->getTrackProperty(v1, QStringLiteral("kdenlive:track_name")).toString() == QStringLiteral("Plans"));
        CHECK(undoStack->index() == 1);
        call(timeline, QStringLiteral("renommer_piste"), {{QStringLiteral("piste_id"), v1}, {QStringLiteral("nom"), QStringLiteral("Plans")}});
        CHECK(undoStack->index() == 1);
        undoStack->undo();
        CHECK(timeline->getTrackProperty(v1, QStringLiteral("kdenlive:track_name")).toString() != QStringLiteral("Plans"));
    }

    SECTION("Fichiers : renommer sur le disque, le chutier et la timeline suivent")
    {
        int cid = -1;
        REQUIRE(timeline->requestClipInsertion(idA, v1, 0, cid, true, true, false));
        undoStack->clear();
        CHECK(clipResource(timeline, cid).endsWith(QStringLiteral("C0001.mkv")));
        QJsonObject r = call(timeline, QStringLiteral("renommer_fichier_media"), {{QStringLiteral("media_id"), idA}, {QStringLiteral("nom"), QStringLiteral("01 - Accroche Awa")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const QString renamed = project.path() + QStringLiteral("/01 - Accroche Awa.mkv");
        CHECK(QFile::exists(renamed));
        CHECK_FALSE(QFile::exists(rushA));
        CHECK(r.value(QStringLiteral("apres")).toString() == QStringLiteral("01 - Accroche Awa.mkv"));
        CHECK(binModel->getClipByBinID(idA)->url() == renamed);
        CHECK(binModel->getClipByBinID(idA)->clipName() == QStringLiteral("01 - Accroche Awa.mkv"));
        // Le producteur de la piste, celui que le projet enregistre, suit aussi.
        CHECK(clipResource(timeline, cid) == renamed);
        CHECK(undoStack->index() == 1);
        undoStack->undo();
        CHECK(QFile::exists(rushA));
        CHECK_FALSE(QFile::exists(renamed));
        CHECK(binModel->getClipByBinID(idA)->url() == rushA);
        CHECK(clipResource(timeline, cid) == rushA);
        undoStack->redo();
        CHECK(QFile::exists(renamed));
        undoStack->undo();

        // Les refus : un nom pris, un fichier hors du projet, un média sans fichier.
        CHECK(errorCode(call(timeline, QStringLiteral("renommer_fichier_media"), {{QStringLiteral("media_id"), idA}, {QStringLiteral("nom"), QStringLiteral("C0002")}})) == QStringLiteral("fichier_existe"));
        CHECK(errorCode(call(timeline, QStringLiteral("renommer_fichier_media"), {{QStringLiteral("media_id"), idOut}, {QStringLiteral("nom"), QStringLiteral("x")}})) == QStringLiteral("hors_projet"));
        CHECK(errorCode(call(timeline, QStringLiteral("renommer_fichier_media"), {{QStringLiteral("media_id"), red}, {QStringLiteral("nom"), QStringLiteral("x")}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("renommer_fichier_media"), {{QStringLiteral("media_id"), idA}, {QStringLiteral("nom"), QStringLiteral("a/b")}})) == QStringLiteral("entree_invalide"));
        CHECK(QFile::exists(rushOut));
        CHECK(undoStack->index() == 0);

        // Ranger sur le disque : dans le sous-dossier du projet ; le rush d'ailleurs ne bouge pas.
        r = call(timeline, QStringLiteral("ranger_media"), {{QStringLiteral("media_ids"), QJsonArray{idA, idOut}}, {QStringLiteral("dossier"), QStringLiteral("01 Rushs")}, {QStringLiteral("disque"), true}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("fichiers_deplaces")).toInt() == 1);
        CHECK(r.value(QStringLiteral("hors_projet")).toArray().size() == 1);
        CHECK(QFile::exists(project.path() + QStringLiteral("/01 Rushs/C0001.mkv")));
        CHECK(QFile::exists(rushOut));
        CHECK(clipResource(timeline, cid) == project.path() + QStringLiteral("/01 Rushs/C0001.mkv"));
        CHECK(undoStack->index() == 1);
        undoStack->undo();
        CHECK(QFile::exists(rushA));
        CHECK(parentName(idA).isEmpty());
    }

    SECTION("organiser_rushs : un lot entier en une étape, les collisions refusées sans rien faire")
    {
        // Une collision : les deux finiraient « 01 - Plan.mkv » dans le même dossier.
        QJsonArray clash{QJsonObject{{QStringLiteral("media_id"), idA}, {QStringLiteral("nom"), QStringLiteral("Plan")}, {QStringLiteral("numero"), 1}},
                         QJsonObject{{QStringLiteral("media_id"), idB}, {QStringLiteral("nom"), QStringLiteral("Plan")}, {QStringLiteral("numero"), 1}}};
        QJsonObject r = call(timeline, QStringLiteral("organiser_rushs"), {{QStringLiteral("plan"), clash}, {QStringLiteral("disque"), QStringLiteral("ranger")}});
        CHECK(errorCode(r) == QStringLiteral("existe_deja"));
        CHECK_FALSE(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("conflits")).toArray().isEmpty());
        CHECK(QFile::exists(rushA));
        CHECK(binModel->getClipByBinID(idA)->clipName() == QStringLiteral("C0001.mkv"));
        CHECK(undoStack->index() == 0);
        // Un fichier déjà sur le disque à la place visée.
        QDir().mkpath(project.path() + QStringLiteral("/01 Rushs"));
        {
            QFile taken(project.path() + QStringLiteral("/01 Rushs/02 - Interview.mkv"));
            REQUIRE(taken.open(QIODevice::WriteOnly));
        }
        QJsonArray plan{QJsonObject{{QStringLiteral("media_id"), idA}, {QStringLiteral("nom"), QStringLiteral("Accroche Awa")}, {QStringLiteral("dossier"), QStringLiteral("01 Rushs")}},
                        QJsonObject{{QStringLiteral("media_id"), idB}, {QStringLiteral("nom"), QStringLiteral("Interview")}, {QStringLiteral("dossier"), QStringLiteral("01 Rushs")}},
                        QJsonObject{{QStringLiteral("media_id"), idOut}, {QStringLiteral("nom"), QStringLiteral("Drone")}, {QStringLiteral("dossier"), QStringLiteral("01 Rushs")}}};
        CHECK(errorCode(call(timeline, QStringLiteral("organiser_rushs"), {{QStringLiteral("plan"), plan}, {QStringLiteral("disque"), QStringLiteral("ranger")}})) == QStringLiteral("fichier_existe"));
        CHECK(QFile::exists(rushB));
        QFile::remove(project.path() + QStringLiteral("/01 Rushs/02 - Interview.mkv"));
        // Le vrai lot : trois médias numérotés, deux fichiers rangés, le troisième hors du projet.
        r = call(timeline, QStringLiteral("organiser_rushs"), {{QStringLiteral("plan"), plan}, {QStringLiteral("disque"), QStringLiteral("ranger")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == 1);
        CHECK(r.value(QStringLiteral("fichiers_renommes")).toInt() == 2);
        CHECK(r.value(QStringLiteral("hors_projet")).toArray().size() == 1);
        CHECK(binModel->getClipByBinID(idA)->clipName() == QStringLiteral("01 - Accroche Awa.mkv"));
        CHECK(binModel->getClipByBinID(idB)->clipName() == QStringLiteral("02 - Interview.mkv"));
        CHECK(binModel->getClipByBinID(idOut)->clipName() == QStringLiteral("03 - Drone.mkv"));
        CHECK(parentName(idOut) == QStringLiteral("01 Rushs"));
        CHECK(QFile::exists(project.path() + QStringLiteral("/01 Rushs/01 - Accroche Awa.mkv")));
        CHECK(QFile::exists(project.path() + QStringLiteral("/01 Rushs/02 - Interview.mkv")));
        CHECK(QFile::exists(rushOut));
        // Relancer le même plan ne renumérote pas deux fois (« 01 - 01 - … »).
        r = call(timeline, QStringLiteral("organiser_rushs"), {{QStringLiteral("plan"), QJsonArray{QJsonObject{{QStringLiteral("media_id"), idA}}}}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(binModel->getClipByBinID(idA)->clipName() == QStringLiteral("01 - Accroche Awa.mkv"));
        undoStack->undo();
        // Un Annuler défait tout le lot : noms, dossiers et fichiers.
        undoStack->undo();
        CHECK(QFile::exists(rushA));
        CHECK(QFile::exists(rushB));
        CHECK(binModel->getClipByBinID(idB)->clipName() == QStringLiteral("C0002.mkv"));
        CHECK(parentName(idOut).isEmpty());
        // Un préfixe de musique, sans toucher au disque.
        r = call(timeline, QStringLiteral("organiser_rushs"),
                 {{QStringLiteral("plan"), QJsonArray{QJsonObject{{QStringLiteral("media_id"), idB}, {QStringLiteral("nom"), QStringLiteral("Kora douce - Ablaye")}}}}, {QStringLiteral("prefixe"), QStringLiteral("M")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(binModel->getClipByBinID(idB)->clipName() == QStringLiteral("M01 - Kora douce - Ablaye.mkv"));
        CHECK(QFile::exists(rushB));
    }

    SECTION("Séquences : créer, renommer, collisions")
    {
        QJsonObject r = call(timeline, QStringLiteral("creer_sequence"), {{QStringLiteral("nom"), QStringLiteral("Boutique Awa - 9x16 - v1")}, {QStringLiteral("pistes_video"), 2}, {QStringLiteral("pistes_audio"), 2}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == 1);
        const QString seqId = r.value(QStringLiteral("sequence")).toObject().value(QStringLiteral("media_id")).toString();
        REQUIRE(binModel->getClipByBinID(seqId));
        CHECK(errorCode(call(timeline, QStringLiteral("creer_sequence"), {{QStringLiteral("nom"), QStringLiteral("boutique awa - 9x16 - v1")}})) == QStringLiteral("existe_deja"));
        r = call(timeline, QStringLiteral("renommer_sequence"), {{QStringLiteral("sequence"), seqId}, {QStringLiteral("nom"), QStringLiteral("Boutique Awa - 9x16 - v2")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("avant")).toString() == QStringLiteral("Boutique Awa - 9x16 - v1"));
        CHECK(binModel->getClipByBinID(seqId)->clipName() == QStringLiteral("Boutique Awa - 9x16 - v2"));
        CHECK(undoStack->index() == 2);
        CHECK(errorCode(call(timeline, QStringLiteral("renommer_sequence"), {{QStringLiteral("sequence"), QStringLiteral("Inconnue")}, {QStringLiteral("nom"), QStringLiteral("x")}})) ==
              QStringLiteral("id_inconnu"));
        undoStack->undo();
        CHECK(binModel->getClipByBinID(seqId)->clipName() == QStringLiteral("Boutique Awa - 9x16 - v1"));
        undoStack->undo();
        CHECK_FALSE(binModel->hasClip(seqId));
    }
    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
