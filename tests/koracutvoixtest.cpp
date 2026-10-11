/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 5 (29 septembre 2026) : la voix off de synthèse. Les trois outils voix_off_generer,
// voix_off_voix et voix_off_installer, le nom des fichiers, la piste « Voix off » créée une seule
// fois, une étape d'annulation par appel, le moteur absent. Le vrai moteur (4 Go) n'est pas lancé :
// un faux script qui parle le même protocole écrit un son de synthèse (tests/dataset/koracut-voix-faux.py).

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagenttools.h"
#include "koracut/voix/koracutvoiceover.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
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

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("x");
}
} // namespace

TEST_CASE("Voix off : moteur, fichiers, outils et annulation", "[KoraCutVoix]")
{
    // Un faux moteur local « installé » dans un dossier temporaire : environnement, moteur, modèle.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString cache = dir.filePath(QStringLiteral("koracut-voix"));
    const QString hub = dir.filePath(QStringLiteral("hub"));
    const QString voices = dir.filePath(QStringLiteral("Voix off"));
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    REQUIRE_FALSE(python.isEmpty());
    // L'interpréteur du faux moteur : par la variable d'essai, la même sur les trois systèmes (un lien
    // « bin/python » ne valait que pour macOS et Linux, le test tombait sous Windows).
    qputenv("KORACUT_VOIX_PYTHON", python.toUtf8());
    // Les deux moteurs : moshi_mlx (Mac à puce Apple) et moshi (PyTorch, partout ailleurs).
    QDir().mkpath(cache + QStringLiteral("/kyutai/lib/python3.11/site-packages/moshi_mlx"));
    QDir().mkpath(cache + QStringLiteral("/kyutai/lib/python3.11/site-packages/moshi"));
    const QString snapshot = hub + QStringLiteral("/models--kyutai--tts-1.6b-en_fr/snapshots/abc");
    touch(snapshot + QStringLiteral("/dsm_tts_1e68beda@240.safetensors"));
    touch(snapshot + QStringLiteral("/tokenizer-e351c8d8-checkpoint125.safetensors"));
    touch(snapshot + QStringLiteral("/tokenizer_spm_8k_en_fr_audio.model"));
    qputenv("KORACUT_VOIX_CACHE", cache.toUtf8());
    qputenv("HF_HUB_CACHE", hub.toUtf8());
    qputenv("KORACUT_VOIX_DOSSIER", voices.toUtf8());
    qputenv("KORACUT_VOIX_LOCAL_OK", "1");
    qputenv("KORACUT_VOIX_SANS_ECOUTE", "1");
    qputenv("KORACUT_VOIX_SCRIPT", (sourcesPath + QStringLiteral("/dataset/koracut-voix-faux.py")).toUtf8());

    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    auto binModel = pCore->projectItemModel();
    const int tracksBefore = timeline->getTracksCount();

    SECTION("Le nom des fichiers : NN - premiers mots, numéro suivant")
    {
        QDir().mkpath(voices);
        CHECK(KoraCutVoiceOver::fileNameFor(voices, QStringLiteral("Sur AFEBABA Shop, tes clients paient par Wave.")) ==
              QStringLiteral("01 - Sur AFEBABA Shop tes clients paient.wav"));
        touch(voices + QStringLiteral("/07 - Ancienne.wav"));
        CHECK(KoraCutVoiceOver::fileNameFor(voices, QStringLiteral("afebaba.com : tout."), QString()) == QStringLiteral("08 - Afebabacom tout.wav"));
        CHECK(KoraCutVoiceOver::fileNameFor(voices, QStringLiteral("x"), QStringLiteral("Ouverture"), 2) == QStringLiteral("10 - Ouverture.wav"));
        QFile::remove(voices + QStringLiteral("/07 - Ancienne.wav"));
    }

    SECTION("voix_off_voix : le moteur local prêt, ses quatre voix")
    {
        const QJsonObject r = call(timeline, QStringLiteral("voix_off_voix"), {});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("moteur_auto")).toString() == QStringLiteral("local"));
        CHECK(r.value(QStringLiteral("moteurs")).toArray().first().toObject().value(QStringLiteral("pret")).toBool());
        CHECK(r.value(QStringLiteral("voix")).toArray().size() == 4);
        CHECK(call(timeline, QStringLiteral("voix_off_installer"), {}).value(QStringLiteral("deja_installe")).toBool());
    }

    SECTION("voix_off_generer : dans le chutier, puis posée sur la piste Voix off, une étape chacune")
    {
        const int steps = undoStack->index();
        const int medias = int(binModel->getAllClipIds().size());
        QJsonObject r = call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Bienvenue dans KoraCut.")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("fichier")).toString() == voices + QStringLiteral("/01 - Bienvenue dans KoraCut.wav"));
        CHECK(QFileInfo::exists(r.value(QStringLiteral("fichier")).toString()));
        CHECK(r.value(QStringLiteral("fidelite")).toDouble() == 1.0);
        CHECK_FALSE(r.value(QStringLiteral("a_reecouter")).toBool());
        CHECK(r.value(QStringLiteral("credit")).toString().contains(QStringLiteral("CC-BY")));
        CHECK_FALSE(r.contains(QStringLiteral("clip_id")));
        CHECK(int(binModel->getAllClipIds().size()) == medias + 1);
        CHECK(undoStack->index() == steps + 1);
        CHECK(timeline->getTracksCount() == tracksBefore);

        r = call(timeline, QStringLiteral("voix_off_generer"),
                 {{QStringLiteral("texte"), QStringLiteral("Livré en 24 à 48 heures, partout au Sénégal.")}, {QStringLiteral("poser"), QJsonObject{{QStringLiteral("position"), 50}}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        REQUIRE(r.contains(QStringLiteral("clip_id")));
        const int clip = r.value(QStringLiteral("clip_id")).toInt();
        const int track = r.value(QStringLiteral("piste_id")).toInt();
        CHECK(r.value(QStringLiteral("piste_creee")).toBool());
        CHECK(timeline->getClipPosition(clip) == 50);
        CHECK(timeline->isAudioTrack(track));
        CHECK(timeline->getTrackProperty(track, QStringLiteral("kdenlive:track_name")).toString() == QStringLiteral("Voice-over"));
        CHECK(KoraCutVoiceOver::voiceTrack(timeline) == track);
        CHECK(timeline->getTracksCount() == tracksBefore + 1);
        CHECK(undoStack->index() == steps + 2);

        // La même piste la fois suivante ; une place occupée laisse le son dans le chutier et le dit.
        r = call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Encore une.")}, {QStringLiteral("poser"), QJsonObject{{QStringLiteral("position"), 60}}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("piste_id")).toInt() == track);
        CHECK_FALSE(r.value(QStringLiteral("piste_creee")).toBool());
        CHECK(r.contains(QStringLiteral("pose_refusee")));
        CHECK(timeline->getTracksCount() == tracksBefore + 1);

        // Un Annuler retire le son posé, sa piste et son média d'un coup.
        undoStack->undo();
        undoStack->undo();
        CHECK_FALSE(timeline->isClip(clip));
        CHECK(timeline->getTracksCount() == tracksBefore);
        CHECK(int(binModel->getAllClipIds().size()) == medias + 1);
        undoStack->undo();
        CHECK(int(binModel->getAllClipIds().size()) == medias);
        // Le fichier reste sur le disque : annuler ne supprime jamais un fichier.
        CHECK(QFileInfo::exists(voices + QStringLiteral("/01 - Bienvenue dans KoraCut.wav")));
    }

    SECTION("dossier : la voix va dans un sous-dossier du projet, le chutier suit le même chemin")
    {
        // Relatif au projet : il faut un projet sur le disque.
        CHECK(errorCode(call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Bonjour.")}, {QStringLiteral("dossier"), QStringLiteral("Script 01/02 Son")}})) ==
              QStringLiteral("projet_absent"));
        const QString projectFile = dir.filePath(QStringLiteral("Projet/pubs.koracut"));
        QDir().mkpath(QFileInfo(projectFile).absolutePath());
        touch(projectFile);
        document.setUrl(QUrl::fromLocalFile(projectFile));
        CHECK(errorCode(call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Bonjour.")}, {QStringLiteral("dossier"), QStringLiteral("../Ailleurs")}})) ==
              QStringLiteral("hors_projet"));
        CHECK(errorCode(call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Bonjour.")}, {QStringLiteral("dossier"), dir.path()}})) ==
              QStringLiteral("hors_projet"));
        const int steps = undoStack->index();
        QJsonObject r = call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("La peau respire.")}, {QStringLiteral("dossier"), QStringLiteral("Script 01/02 Son")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const QString folder = dir.filePath(QStringLiteral("Projet/Script 01/02 Son"));
        CHECK(r.value(QStringLiteral("fichier")).toString() == folder + QStringLiteral("/01 - La peau respire.wav"));
        CHECK(QFileInfo::exists(r.value(QStringLiteral("fichier")).toString()));
        CHECK(r.value(QStringLiteral("dossier_chutier")).toString() == QStringLiteral("Script 01/02 Son"));
        CHECK(KoraCutAgentTools::binFolderPath(r.value(QStringLiteral("media_id")).toVariant().toString()) == QStringLiteral("Script 01/02 Son"));
        CHECK(undoStack->index() == steps + 1);
        CHECK_FALSE(QFileInfo::exists(voices)); // rien dans « Voix off »
        // La suivante retrouve les deux dossiers, sans les recréer, et prend le numéro suivant.
        r = call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Elle rayonne.")}, {QStringLiteral("dossier"), QStringLiteral("Script 01/02 Son")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("fichier")).toString() == folder + QStringLiteral("/02 - Elle rayonne.wav"));
        CHECK(r.value(QStringLiteral("dossier_chutier")).toString() == QStringLiteral("Script 01/02 Son"));
        CHECK(binModel->getFolderIdByName(QStringLiteral("02 Son")) == binModel->getClipByBinID(r.value(QStringLiteral("media_id")).toVariant().toString())->parent()->clipId());
        // Annuler retire le média ; le premier Annuler retire aussi les dossiers créés avec lui.
        undoStack->undo();
        undoStack->undo();
        CHECK(binModel->getFolderIdByName(QStringLiteral("02 Son")).isEmpty());
    }

    SECTION("Sans position : la tête de lecture, puis juste après ce qui l'occupe")
    {
        QJsonObject r = call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Première.")}, {QStringLiteral("poser"), QJsonObject{}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int first = r.value(QStringLiteral("clip_id")).toInt();
        CHECK(timeline->getClipPosition(first) == 0);
        CHECK_FALSE(r.value(QStringLiteral("decalee")).toBool());
        r = call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Seconde.")}, {QStringLiteral("poser"), QJsonObject{}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("decalee")).toBool());
        CHECK(timeline->getClipPosition(r.value(QStringLiteral("clip_id")).toInt()) == timeline->getClipPosition(first) + timeline->getClipPlaytime(first));
    }

    SECTION("Les refus : texte vide, piste vidéo, moteur en erreur, moteur absent")
    {
        const int steps = undoStack->index();
        CHECK(errorCode(call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("  ")}})) == QStringLiteral("entree_invalide"));
        const int video = timeline->getTracksIds(false).first();
        CHECK(errorCode(call(timeline, QStringLiteral("voix_off_generer"),
                             {{QStringLiteral("texte"), QStringLiteral("Non.")}, {QStringLiteral("poser"), QJsonObject{{QStringLiteral("piste_id"), video}}}})) ==
              QStringLiteral("mauvais_type_de_piste"));
        CHECK(errorCode(call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("ÉCHEC voulu.")}})) == QStringLiteral("moteur_en_erreur"));
        CHECK(errorCode(call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Bonjour.")}, {QStringLiteral("moteur"), QStringLiteral("elevenlabs")}})) ==
              QStringLiteral("non_disponible"));
        // Le moteur local sans son modèle : moteur_absent, avec la taille et le conseil de demander l'accord.
        QDir(hub).removeRecursively();
        const QJsonObject r = call(timeline, QStringLiteral("voix_off_generer"), {{QStringLiteral("texte"), QStringLiteral("Bonjour.")}, {QStringLiteral("moteur"), QStringLiteral("local")}});
        INFO(dump(r));
        CHECK(errorCode(r) == QStringLiteral("moteur_absent"));
        CHECK(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("conseil")).toString().contains(QStringLiteral("voix_off_installer")));
        CHECK(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("message")).toString().contains(QStringLiteral("Go")));
        CHECK_FALSE(KoraCutVoiceOver::engineState(QStringLiteral("local")).value(QStringLiteral("pret")).toBool());
        CHECK(undoStack->index() == steps);
    }

    pCore->projectManager()->closeCurrentDocument(false, false);
    for (const char *name : {"KORACUT_VOIX_CACHE", "HF_HUB_CACHE", "KORACUT_VOIX_DOSSIER", "KORACUT_VOIX_LOCAL_OK", "KORACUT_VOIX_SANS_ECOUTE", "KORACUT_VOIX_SCRIPT"}) {
        qunsetenv(name);
    }
}
