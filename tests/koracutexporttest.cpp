/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Le module Export comme la page Deliver de DaVinci Resolve (document 23, 28 septembre 2026) : les modèles,
// les réglages qu'ils donnent, les paramètres du consumer, les noms de fichiers, les réglages gardés dans le
// projet, le modèle par type de vidéo, le ménage des playlists, et un vrai rendu par l'outil modeles_export.

#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "koracut/export/koracutexport.h"
#include "koracut/export/koracutexportqueue.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutvideotypes.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace KoraCutExport;

TEST_CASE("Module Export : les modèles et leurs réglages", "[KoraCutExport]")
{
    REQUIRE(models().size() >= 25);
    for (const QString &id : {QStringLiteral("youtube_1080p"), QStringLiteral("youtube_shorts"), QStringLiteral("tiktok"), QStringLiteral("instagram_reels"),
                              QStringLiteral("facebook"), QStringLiteral("x"), QStringLiteral("linkedin"), QStringLiteral("snapchat"), QStringLiteral("vimeo"),
                              QStringLiteral("whatsapp"), QStringLiteral("twitch"), QStringLiteral("master_h264"), QStringLiteral("master_h265"),
                              QStringLiteral("prores_hq"), QStringLiteral("prores_4444"), QStringLiteral("audio_wav"), QStringLiteral("gif"), QStringLiteral("image_png")}) {
        INFO(id.toStdString());
        const Model *m = model(id);
        REQUIRE(m);
        // Une plateforme a son vrai logo embarqué, un modèle technique son badge.
        if (m->platform) {
            CHECK(QFile::exists(QStringLiteral(":/koracut/plateformes/%1.svg").arg(m->logo)));
        } else {
            CHECK_FALSE(m->badge.isEmpty());
        }
    }
    // Les anciens identifiants des outils d'agent mènent aux nouveaux modèles.
    CHECK(model(QStringLiteral("reels_tiktok_9x16"))->id == QStringLiteral("tiktok"));
    CHECK(model(QStringLiteral("master_4k_prores"))->id == QStringLiteral("prores_hq"));
    CHECK(model(QStringLiteral("nulle_part")) == nullptr);

    const QSize hd(1920, 1080);
    const QSize vertical(1080, 1920);

    SECTION("Un modèle de plateforme : la taille suit la séquence, jamais déformée")
    {
        Settings s = Settings::forModel(*model(QStringLiteral("tiktok")), hd, 25.);
        CHECK(s.width == 0); // 9:16 sur une séquence 16:9 : le fichier garde la séquence
        CHECK(s.bitrateKbps == 12000);
        CHECK(s.normalize);
        CHECK(s.lufs == -14);
        s = Settings::forModel(*model(QStringLiteral("whatsapp")), vertical, 25.);
        CHECK(s.width == 720); // même rapport : la taille du modèle
        CHECK(s.height == 1280);
        s = Settings::forModel(*model(QStringLiteral("youtube_1080p")), hd, 50.);
        CHECK(s.bitrateKbps == 12000); // 48 à 60 i/s : le débit de la fiche YouTube
        s = Settings::forModel(*model(QStringLiteral("gif")), hd, 25.);
        CHECK(s.width == 720);
        CHECK(s.height == 404);
        CHECK_FALSE(s.audio);
        CHECK(modelFits(*model(QStringLiteral("youtube_shorts")), vertical));
        CHECK_FALSE(modelFits(*model(QStringLiteral("youtube_shorts")), hd));
    }

    SECTION("Les paramètres du consumer")
    {
        Settings s = Settings::forModel(*model(QStringLiteral("youtube_1080p")), hd, 25.);
        s.hardware = false;
        RenderPresetParams p = s.params(hd, 25.);
        CHECK(p.value(QStringLiteral("f")) == QStringLiteral("mp4"));
        CHECK(p.value(QStringLiteral("vcodec")) == QStringLiteral("libx264"));
        CHECK(p.value(QStringLiteral("maxrate")) == QStringLiteral("8000k"));
        CHECK(p.value(QStringLiteral("vprofile")) == QStringLiteral("high"));
        CHECK_FALSE(p.contains(QStringLiteral("profile"))); // irait aussi à l'encodeur AAC
        CHECK(p.value(QStringLiteral("acodec")) == QStringLiteral("aac"));
        CHECK(p.value(QStringLiteral("movflags")) == QStringLiteral("+faststart"));
        CHECK_FALSE(p.contains(QStringLiteral("width")));
        // La signature « Monté avec KoraCut » : dans le commentaire du fichier par défaut, retirable d'un réglage.
        CHECK(p.value(QStringLiteral("meta.attr.comment.markup")).contains(QStringLiteral("KoraCut")));
        CHECK_FALSE(p.value(QStringLiteral("meta.attr.comment.markup")).contains(QLatin1Char('"')));
        setSignatureEnabled(false);
        CHECK_FALSE(s.params(hd, 25.).contains(QStringLiteral("meta.attr.comment.markup")));
        setSignatureEnabled(true);
        CHECK(shareText().contains(shareLink()));
        CHECK(shareLink().startsWith(QStringLiteral("https://koracut.org/?utm_source=app")));
        s.width = 1280;
        s.height = 720;
        p = s.params(hd, 25.);
        CHECK(p.value(QStringLiteral("width")) == QStringLiteral("1280"));
        p = Settings::forModel(*model(QStringLiteral("audio_wav")), hd, 25.).params(hd, 25.);
        CHECK(p.value(QStringLiteral("vn")) == QStringLiteral("1"));
        CHECK(p.value(QStringLiteral("acodec")) == QStringLiteral("pcm_s24le"));
        p = Settings::forModel(*model(QStringLiteral("prores_4444")), hd, 25.).params(hd, 25.);
        CHECK(p.hasAlpha());
        s = Settings::forModel(*model(QStringLiteral("master_h265")), hd, 25.);
        s.hardware = false;
        p = s.params(hd, 25.);
        CHECK(p.value(QStringLiteral("vcodec")) == QStringLiteral("libx265"));
        CHECK(p.value(QStringLiteral("vtag")) == QStringLiteral("hvc1"));
        // Une estimation qui a du sens : 1 minute de YouTube 1080p, environ 60 Mo.
        const double mb = Settings::forModel(*model(QStringLiteral("youtube_1080p")), hd, 25.).estimatedKbps(hd, 25.) * 60. / 8. / 1000.;
        CHECK(mb > 40.);
        CHECK(mb < 80.);
    }

    SECTION("Les réglages en JSON, les noms de fichiers")
    {
        Settings s = Settings::forModel(*model(QStringLiteral("instagram_reels")), vertical, 25.);
        s.name = QStringLiteral("{projet} - {sequence} - {modele} {version}");
        s.subtitles = QStringLiteral("fichier");
        const Settings back = Settings::fromJson(s.toJson());
        CHECK(back.toJson() == s.toJson());
        CHECK(expandName(s.name, s, QStringLiteral("Recette"), QStringLiteral("Séquence 1"), 2) == QStringLiteral("Recette - Séquence 1 - Instagram Reels v2"));
        CHECK(expandName(QStringLiteral("a/b:c"), s, QString(), QString(), 1) == QStringLiteral("a-b-c"));
        CHECK(expandName(QStringLiteral("{projet}"), s, QStringLiteral("P"), QString(), 3) == QStringLiteral("P v3"));
        // Jamais par-dessus un fichier : v2 quand le nom est pris.
        QTemporaryDir dir;
        s.folder = dir.path();
        s.name = QStringLiteral("{projet}");
        const QString first = outputPath(s, QStringLiteral("Essai"), QString());
        CHECK(QFileInfo(first).fileName() == QStringLiteral("Essai.mp4"));
        QFile f(first);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.close();
        CHECK(QFileInfo(outputPath(s, QStringLiteral("Essai"), QString())).fileName() == QStringLiteral("Essai v2.mp4"));
    }

    SECTION("Le modèle par type de vidéo, dans les deux sens")
    {
        // Chaque type du guide pointe vers un modèle qui existe.
        for (const KoraCutVideoTypes::VideoType &t : KoraCutVideoTypes::all()) {
            INFO(t.id.toStdString());
            CHECK(model(t.exportModel));
            CHECK(model(t.exportModelOther));
        }
        const KoraCutVideoTypes::VideoType *vertical9 = nullptr;
        for (const KoraCutVideoTypes::VideoType &t : KoraCutVideoTypes::all()) {
            if (t.format == QLatin1String("vertical") && t.exportModel == QLatin1String("tiktok")) {
                vertical9 = &t;
                break;
            }
        }
        REQUIRE(vertical9);
        CHECK(modelForVideoType(vertical9->id, vertical) == QStringLiteral("tiktok"));
        // La même vidéo dans une séquence 16:9 : le modèle de l'autre sens.
        CHECK(modelForVideoType(vertical9->id, hd) == vertical9->exportModelOther);
        CHECK(modelForSize(hd) == QStringLiteral("youtube_1080p"));
        CHECK(modelForSize(vertical) == QStringLiteral("tiktok"));
        CHECK(modelForSize(QSize(1080, 1080)) == QStringLiteral("instagram_carre"));
        CHECK(modelForSize(QSize(3840, 2160)) == QStringLiteral("youtube_4k"));
    }

    SECTION("Le ménage des playlists de rendu d'avant")
    {
        const QString old = QDir::temp().absoluteFilePath(QStringLiteral("koracut-testvieux.mlt"));
        const QString fresh = QDir::temp().absoluteFilePath(QStringLiteral("koracut-testneuf.mlt"));
        for (const QString &path : {old, fresh}) {
            QFile f(path);
            REQUIRE(f.open(QIODevice::WriteOnly));
            f.close();
        }
        QFile oldFile(old);
        REQUIRE(oldFile.open(QIODevice::ReadWrite));
        REQUIRE(oldFile.setFileTime(QDateTime::currentDateTime().addDays(-2), QFileDevice::FileModificationTime));
        oldFile.close();
        CHECK(cleanTemporaryFiles() >= 1);
        CHECK_FALSE(QFile::exists(old));
        CHECK(QFile::exists(fresh)); // un rendu du jour n'est jamais touché
        QFile::remove(fresh);
    }
}

TEST_CASE("Module Export : réglages gardés dans le projet et rendu par modeles_export", "[KoraCutExport]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    const QSize size = pCore->getCurrentFrameSize();
    const QString uuid = pCore->currentTimelineId().toString();

    // Sans type : le modèle du format ; avec un type, celui du guide et sa cible sonore.
    Settings s = documentSettings(uuid, size, pCore->getCurrentFps());
    CHECK(s.model == modelForSize(size));
    document.setDocumentProperty(QStringLiteral("koracut:videotype"), KoraCutVideoTypes::all().first().id);
    document.setDocumentProperty(QStringLiteral("koracut:export_lufs"), QStringLiteral("-23"));
    s = documentSettings(uuid, size, pCore->getCurrentFps());
    CHECK(s.model == modelForVideoType(KoraCutVideoTypes::all().first().id, size));
    CHECK(s.lufs == -23);
    // Le choix de la personne passe devant, et reste dans le projet.
    Settings mine = Settings::forModel(*model(QStringLiteral("vimeo")), size, 25.);
    mine.bitrateKbps = 15000;
    saveDocumentSettings(uuid, mine);
    s = documentSettings(uuid, size, pCore->getCurrentFps());
    CHECK(s.model == QStringLiteral("vimeo"));
    CHECK(s.bitrateKbps == 15000);

    QJsonObject r = KoraCutAgentTools::call(timeline, QStringLiteral("modeles_export"), {{QStringLiteral("action"), QStringLiteral("lister")}});
    INFO(QJsonDocument(r).toJson().toStdString());
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    CHECK(r.value(QStringLiteral("modeles")).toArray().size() == models().size());
    CHECK(r.value(QStringLiteral("reglages")).toObject().value(QStringLiteral("modele")).toString() == QStringLiteral("vimeo"));
    r = KoraCutAgentTools::call(timeline, QStringLiteral("modeles_export"), {{QStringLiteral("action"), QStringLiteral("appliquer")}, {QStringLiteral("modele"), QStringLiteral("dvd")}});
    CHECK(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("id_inconnu"));
    r = KoraCutAgentTools::call(timeline, QStringLiteral("modeles_export"),
                                {{QStringLiteral("action"), QStringLiteral("appliquer")}, {QStringLiteral("modele"), QStringLiteral("master_h264")}, {QStringLiteral("reglages"), QJsonObject{{QStringLiteral("couleur"), 1}}}});
    CHECK(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("entree_invalide"));
    // Une timeline vide : rien à exporter.
    r = KoraCutAgentTools::call(timeline, QStringLiteral("modeles_export"), {{QStringLiteral("action"), QStringLiteral("ajouter")}, {QStringLiteral("modele"), QStringLiteral("youtube_1080p")}});
    CHECK(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("entree_invalide"));

    // Deux secondes de bleu, un vrai rendu H.264 logiciel (le rendu matériel dépend de la machine).
    const int fps = qRound(pCore->getCurrentFps());
    const QString blue = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 2 * fps, true);
    const int v1 = timeline->getTracksIds(false).first();
    r = KoraCutAgentTools::call(timeline, QStringLiteral("ajouter_clip"),
                                {{QStringLiteral("media_id"), blue}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    QTemporaryDir dir;
    const auto emptyPlaylists = []() {
        int n = 0;
        for (const QFileInfo &info : QDir::temp().entryInfoList({QStringLiteral("koracut-*.mlt")}, QDir::Files)) {
            n += info.size() == 0 ? 1 : 0;
        }
        return n;
    };
    const int emptyBefore = emptyPlaylists();
    const QJsonObject settings{{QStringLiteral("dossier"), dir.path()}, {QStringLiteral("materiel"), false}, {QStringLiteral("normaliser"), false}};
    r = KoraCutAgentTools::call(timeline, QStringLiteral("modeles_export"),
                                {{QStringLiteral("action"), QStringLiteral("ajouter")}, {QStringLiteral("modele"), QStringLiteral("youtube_1080p")}, {QStringLiteral("reglages"), settings}});
    INFO(QJsonDocument(r).toJson().toStdString());
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    const QString path = r.value(QStringLiteral("chemin")).toString();
    CHECK(path.startsWith(dir.path()));
    CHECK(r.value(QStringLiteral("etat")).toString() == QStringLiteral("en_attente")); // retenu, rien ne part
    // Le fichier .mlt vide réservé par RenderRequest ne traîne plus dans le dossier temporaire.
    CHECK(emptyPlaylists() <= emptyBefore);
    const bool canRender = QFileInfo::exists(KdenliveSettings::meltpath()) && !KoraCutExportQueue::rendererPath().isEmpty();
    if (canRender) {
        r = KoraCutAgentTools::call(timeline, QStringLiteral("modeles_export"), {{QStringLiteral("action"), QStringLiteral("lancer_file")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        QElapsedTimer timer;
        timer.start();
        KoraCutExportQueue::JobPtr job;
        for (const auto &j : KoraCutExportQueue::self().jobs()) {
            if (j->path == path) {
                job = j;
            }
        }
        REQUIRE(job);
        while (timer.elapsed() < 90000 && (job->state == QLatin1String("en_cours") || job->state == QLatin1String("en_attente"))) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
        INFO(job->errorText.toStdString());
        CHECK(job->state == QStringLiteral("termine"));
        CHECK(QFileInfo(path).size() > 0);
    }
    // Le document est une variable du test : on le referme avant qu'il disparaisse, sinon le nettoyage du
    // chutier à la sortie du programme le relit détruit (plantage vu sous Linux le 30 septembre 2026).
    pCore->projectManager()->closeCurrentDocument(false, false);
}
