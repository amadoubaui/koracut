/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagenttools.h"
#include "koracut/motion/koracutmotionmirror.h"
#include "koracut/motion/koracutmotionrenderer.h"
#include "koracut/motion/koracutmotionsync.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>

// Les outils motion_* des agents (document 19, lot 3), sur un miroir réel et un rendu simulé : sans
// Node, ffmpeg fabrique une petite vidéo transparente à la place de HyperFrames. Sans ffmpeg, les
// rendus échouent proprement et les vérifications qui ont besoin d'un clip sont sautées.

using KoraCutAgentTools::call;

namespace {

QString errorCode(const QJsonObject &r)
{
    return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString();
}

QString readAll(const QString &path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    return QString::fromUtf8(file.readAll());
}

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/** Attend qu'une condition devienne vraie, en laissant tourner la boucle d'événements. */
template <typename F> bool waitFor(F condition, int ms = 8000)
{
    for (int i = 0; i < ms / 50; ++i) {
        if (condition()) {
            return true;
        }
        waitMs(50);
    }
    return condition();
}

QString ffmpegPath()
{
    // Le ffmpeg de Homebrew d'abord : celui de Craft ne se lance pas hors de l'application (libvpx).
    QString found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"), {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin")});
    if (found.isEmpty()) {
        found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    }
    return found;
}

/** Le rendu simulé : une vidéo ProRes 4444 rouge, de la durée de l'élément, à la place du rendu HyperFrames. */
bool fakeRender(const KoraCutMotionRenderRequest &request, double durationSeconds)
{
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(request.output).absolutePath());
    QProcess p;
    p.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                     QStringLiteral("color=c=red@0.5:s=64x64:r=%1:d=%2").arg(request.fps).arg(durationSeconds + 0.2), QStringLiteral("-c:v"), QStringLiteral("prores_ks"),
                     QStringLiteral("-profile:v"), QStringLiteral("4444"), QStringLiteral("-pix_fmt"), QStringLiteral("yuva444p10le"), request.output});
    return p.waitForFinished(30000) && p.exitCode() == 0;
}

QJsonObject firstComposition(const QJsonObject &list)
{
    return list.value(QStringLiteral("compositions")).toArray().first().toObject();
}

QJsonObject elementOf(const QJsonObject &composition, const QString &id)
{
    for (const QJsonValue &v : composition.value(QStringLiteral("elements")).toArray()) {
        if (v.toObject().value(QStringLiteral("id")).toString() == id) {
            return v.toObject();
        }
    }
    return {};
}

} // namespace

TEST_CASE("Outils Motion : composition, éléments, rendu simulé, pose", "[KoraCutMotionTools]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    const double fps = pCore->getCurrentFps();

    // Sans miroir : les outils le disent.
    KoraCutAgentTools::setMotionMirror(nullptr);
    CHECK(errorCode(call(timeline, QStringLiteral("motion_lister"), {})) == QStringLiteral("non_disponible"));

    // Le projet vit dans un dossier temporaire : motion/ et cache/ à côté.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    document.setUrl(QUrl::fromLocalFile(tmp.path() + QStringLiteral("/essai.koracut")));
    // Le fichier existe : depuis le lot 3 de la porte (24 septembre 2026), un projet dont le fichier
    // n'est pas encore sur le disque n'a pas de dossier motion/ à côté (KoraCutMotionProject::motionRoot).
    {
        QFile marker(tmp.path() + QStringLiteral("/essai.koracut"));
        REQUIRE(marker.open(QIODevice::WriteOnly));
    }
    const bool haveFfmpeg = !ffmpegPath().isEmpty();
    KoraCutMotionRenderer::setSimulator([](const KoraCutMotionRenderRequest &r) {
        // La durée de l'élément est dans l'enveloppe (data-duration de la racine) ou vaut 4 s (gabarit).
        double seconds = 4.;
        const QRegularExpression duration(QStringLiteral("data-composition-id=\"[^\"]*\"[^>]*data-duration=\"([0-9.]+)\""));
        const QRegularExpressionMatch m = duration.match(r.envelopeHtml);
        if (m.hasMatch()) {
            seconds = m.captured(1).toDouble();
        }
        return fakeRender(r, seconds);
    });
    KoraCutMotionMirror mirror; // s'inscrit auprès des outils
    mirror.setDocument(&document, timeline);
    REQUIRE(KoraCutAgentTools::motionMirror() == &mirror);
    REQUIRE(KoraCutAgentTools::implementedTools().contains(QStringLiteral("motion_poser")));

    // Rien au départ ; la charte du client (tokens.json à côté du projet) arrive telle quelle.
    {
        QFile tokens(tmp.path() + QStringLiteral("/tokens.json"));
        REQUIRE(tokens.open(QIODevice::WriteOnly));
        tokens.write("{\"couleurs\": {\"bleu\": \"#1C4E9A\"}}");
    }
    QJsonObject list = call(timeline, QStringLiteral("motion_lister"), {});
    REQUIRE(list.value(QStringLiteral("ok")).toBool());
    CHECK(list.value(QStringLiteral("compositions")).toArray().isEmpty());
    CHECK(list.value(QStringLiteral("charte")).toObject().value(QStringLiteral("couleurs")).toObject().value(QStringLiteral("bleu")).toString() == QStringLiteral("#1C4E9A"));
    CHECK(!list.value(QStringLiteral("hyperframes")).toObject().value(QStringLiteral("etat")).toString().isEmpty());
    CHECK(errorCode(call(timeline, QStringLiteral("motion_lire"), {{QStringLiteral("nom"), QStringLiteral("rien")}})) == QStringLiteral("projet_inconnu"));

    // Créer depuis le gabarit : un titre de 4 s, aux dimensions du projet. La piste Motion naît tout de
    // suite, le clip arrive avec le rendu.
    const int undoBefore = undoStack->index();
    const int tracksBefore = timeline->getTracksCount();
    QJsonObject r = call(timeline, QStringLiteral("motion_creer"), {{QStringLiteral("nom"), QStringLiteral("bandeau essai")}});
    INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    CHECK(r.value(QStringLiteral("nom")).toString() == QStringLiteral("bandeau-essai"));
    const QString indexPath = r.value(QStringLiteral("fichier")).toString();
    CHECK(QFile::exists(indexPath));
    CHECK(QFile::exists(QFileInfo(indexPath).absolutePath() + QStringLiteral("/assets/gsap.min.js")));
    CHECK(r.value(QStringLiteral("elements")).toArray().size() == 1);
    CHECK(elementOf(r, QStringLiteral("titre-1")).value(QStringLiteral("sorte")).toString() == QStringLiteral("bloc"));
    CHECK(errorCode(call(timeline, QStringLiteral("motion_creer"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}})) == QStringLiteral("existe_deja"));
    CHECK(errorCode(call(timeline, QStringLiteral("motion_creer"), {{QStringLiteral("nom"), QStringLiteral("x")}, {QStringLiteral("html"), QStringLiteral("<html></html>")}})) ==
          QStringLiteral("entree_invalide"));

    // Lire.
    r = call(timeline, QStringLiteral("motion_lire"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    CHECK(r.value(QStringLiteral("html")).toString().contains(QLatin1String("data-composition-id=\"bandeau-essai\"")));

    // Le rendu simulé pose le clip du titre sur une piste Motion.
    CHECK(timeline->getTracksCount() == tracksBefore + 1);
    if (haveFfmpeg) {
        REQUIRE(waitFor([&]() { return mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1")) >= 0; }, 20000));
        list = call(timeline, QStringLiteral("motion_lister"), {});
        const QJsonObject titre = elementOf(firstComposition(list), QStringLiteral("titre-1"));
        CHECK(titre.value(QStringLiteral("rendu")).toString() == QStringLiteral("pret"));
        CHECK(titre.value(QStringLiteral("clip_id")).toInt(-1) >= 0);
        CHECK(titre.value(QStringLiteral("pos")).toInt() == 0);
        CHECK(titre.value(QStringLiteral("len")).toInt() == int(std::lround(4 * fps)));
    } else {
        WARN("ffmpeg introuvable : le rendu simulé est sauté, pas de clip à vérifier.");
    }

    SECTION("motion_element : texte, début, durée, attribut, une étape chacun")
    {
        const int before = undoStack->index();
        r = call(timeline, QStringLiteral("motion_element"),
                 {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("id"), QStringLiteral("titre-1")}, {QStringLiteral("texte"), QStringLiteral("Awa Ndiaye")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("rendu_relance")).toBool());
        CHECK(readAll(indexPath).contains(QLatin1String(">Awa Ndiaye<")));
        CHECK(undoStack->index() == before + 1);
        undoStack->undo();
        CHECK(readAll(indexPath).contains(QLatin1String(">Nouveau titre<")));
        undoStack->redo();
        CHECK(readAll(indexPath).contains(QLatin1String(">Awa Ndiaye<")));

        r = call(timeline, QStringLiteral("motion_element"),
                 {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("id"), QStringLiteral("titre-1")}, {QStringLiteral("debut"), 1.0}, {QStringLiteral("duree"), 3.0}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK_FALSE(r.value(QStringLiteral("rendu_relance")).toBool());
        CHECK(readAll(indexPath).contains(QLatin1String("data-start=\"1\"")));
        CHECK(readAll(indexPath).contains(QLatin1String("data-duration=\"3\"")));
        if (haveFfmpeg) {
            REQUIRE(waitFor([&]() { return mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1")) >= 0; }, 20000));
            const int cid = mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"));
            CHECK(timeline->getClipPosition(cid) == int(std::lround(fps)));
            CHECK(timeline->getClipPlaytime(cid) == int(std::lround(3 * fps)));
        }

        r = call(timeline, QStringLiteral("motion_element"),
                 {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")},
                  {QStringLiteral("id"), QStringLiteral("titre-1")},
                  {QStringLiteral("attributs"), QJsonObject{{QStringLiteral("data-track-index"), 2}, {QStringLiteral("data-role"), QStringLiteral("nom")}}}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(readAll(indexPath).contains(QLatin1String("data-track-index=\"2\"")));
        CHECK(readAll(indexPath).contains(QLatin1String("data-role=\"nom\"")));
        r = call(timeline, QStringLiteral("motion_element"),
                 {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("id"), QStringLiteral("titre-1")}, {QStringLiteral("attributs"), QJsonObject{{QStringLiteral("data-role"), QJsonValue::Null}}}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK_FALSE(readAll(indexPath).contains(QLatin1String("data-role")));

        CHECK(errorCode(call(timeline, QStringLiteral("motion_element"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("id"), QStringLiteral("absent")}, {QStringLiteral("texte"), QStringLiteral("x")}})) ==
              QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("motion_element"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("id"), QStringLiteral("titre-1")}})) ==
              QStringLiteral("entree_invalide"));
    }

    SECTION("motion_ecrire : la composition d'essai à trois éléments, diff et annulation")
    {
        // Les assets de la composition d'essai (logo, son) viennent avec.
        const QString dataset = sourcesPath + QStringLiteral("/dataset/koracut-motion");
        const QString dir = QFileInfo(indexPath).absolutePath();
        for (const QString &asset : {QStringLiteral("logo.png"), QStringLiteral("son.wav")}) {
            REQUIRE(QFile::copy(dataset + QStringLiteral("/assets/") + asset, dir + QStringLiteral("/assets/") + asset));
        }
        const QString html = readAll(dataset + QStringLiteral("/index.html"));
        const QString before = readAll(indexPath);
        const int undoIndex = undoStack->index();
        r = call(timeline, QStringLiteral("motion_ecrire"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("html"), html}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("elements")).toArray().size() == 3);
        QStringList added;
        for (const QJsonValue &v : r.value(QStringLiteral("ajoutes")).toArray()) {
            added << v.toString();
        }
        CHECK(added.contains(QStringLiteral("logo")));
        CHECK(added.contains(QStringLiteral("son")));
        CHECK(r.value(QStringLiteral("retires")).toArray().isEmpty());
        CHECK(r.value(QStringLiteral("rendus_relances")).toArray().contains(QStringLiteral("titre-1")));
        CHECK(readAll(indexPath) == html);
        CHECK(undoStack->index() == undoIndex + 1);
        CHECK(elementOf(r, QStringLiteral("son")).value(QStringLiteral("sorte")).toString() == QStringLiteral("audio"));
        CHECK(elementOf(r, QStringLiteral("logo")).value(QStringLiteral("debut")).toDouble() == Approx(4.0)); // titre-1 + 0.5 : la fin du titre (0.5 + 3) + 0.5

        if (haveFfmpeg) {
            REQUIRE(waitFor([&]() { return mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("logo")) >= 0; }, 20000));
            REQUIRE(waitFor([&]() { return mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("son")) >= 0; }, 20000));
            const int logo = mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("logo"));
            CHECK(timeline->getClipPosition(logo) == int(std::lround(4 * fps)));
            const int son = mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("son"));
            CHECK(timeline->isAudioTrack(timeline->getClipTrackId(son)));
            CHECK(timeline->getClipIn(son) == int(std::lround(0.5 * fps)));
        }

        // Annuler remet l'ancien HTML et retire les clips des éléments disparus. Les rendus arrivés
        // entre-temps ont posé leurs clips dans des étapes à eux (« Motion : rendu ») : on remonte jusqu'à
        // l'étape de la réécriture.
        while (undoStack->index() > undoIndex) {
            undoStack->undo();
        }
        CHECK(readAll(indexPath) == before);
        CHECK(mirror.elements(QStringLiteral("bandeau-essai")).size() == 1);
        CHECK(mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("logo")) < 0);
        undoStack->redo();
        CHECK(readAll(indexPath) == html);
        CHECK(mirror.elements(QStringLiteral("bandeau-essai")).size() == 3);
        waitMs(600);

        CHECK(errorCode(call(timeline, QStringLiteral("motion_ecrire"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("html"), QStringLiteral("<p>rien</p>")}})) ==
              QStringLiteral("entree_invalide"));
    }

    SECTION("motion_verifier et motion_rendre")
    {
        // Le bandeau du scénario 16 (Agents in-app) : l'analyse de KoraCut n'y trouve rien à redire.
        const QString bandeau = readAll(sourcesPath + QStringLiteral("/dataset/koracut-motion/bandeau-awa.html"));
        r = call(timeline, QStringLiteral("motion_creer"), {{QStringLiteral("nom"), QStringLiteral("bandeau-awa")}, {QStringLiteral("html"), bandeau}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("elements")).toArray().size() == 1);
        r = call(timeline, QStringLiteral("motion_verifier"), {{QStringLiteral("nom"), QStringLiteral("bandeau-awa")}});
        INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("erreurs")).toInt() == 0);
        if (haveFfmpeg) {
            // Le rendu simulé du bandeau doit être posé avant la fin du test : fermer le projet pendant
            // qu'un clip charge encore bloque Kdenlive (slotCancelJobs attend la tâche).
            REQUIRE(waitFor([&]() { return mirror.clipFor(QStringLiteral("bandeau-awa"), QStringLiteral("bandeau")) >= 0; }, 20000));
        }

        r = call(timeline, QStringLiteral("motion_verifier"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("erreurs")).toInt() == 0);
        CHECK(r.contains(QStringLiteral("lint")));
        CHECK(r.value(QStringLiteral("messages")).isArray());

        r = call(timeline, QStringLiteral("motion_rendre"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("forcer"), true}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("demandes")).toInt() == 1);
        CHECK(r.value(QStringLiteral("termine")).toBool());
        if (haveFfmpeg) {
            CHECK(r.value(QStringLiteral("rendus")).toInt() == 1);
        } else {
            CHECK(r.value(QStringLiteral("echecs")).toInt() == 1);
        }
        CHECK(errorCode(call(timeline, QStringLiteral("motion_rendre"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("ids"), QJsonArray{QStringLiteral("absent")}}})) ==
              QStringLiteral("id_inconnu"));

        // Un instantané demande la vraie CLI : sans elle, l'outil le dit.
        r = call(timeline, QStringLiteral("motion_instantane"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("temps"), 1.0}});
        if (KoraCutMotionHost::state() == KoraCutMotionHost::State::Ready) {
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(QFileInfo(r.value(QStringLiteral("chemin")).toString()).size() > 0);
        } else {
            CHECK(errorCode(r) == QStringLiteral("non_disponible"));
        }
    }

    if (haveFfmpeg) {
        SECTION("Supprimer un clip Motion retire l'élément du HTML, Annuler remet les deux")
        {
            const int cid = mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"));
            REQUIRE(cid >= 0);
            REQUIRE(timeline->requestItemDeletion(cid, true));
            REQUIRE(waitFor([&]() { return !readAll(indexPath).contains(QLatin1String("id=\"titre-1\"")); }, 5000));
            CHECK(mirror.elements(QStringLiteral("bandeau-essai")).isEmpty());
            undoStack->undo();
            REQUIRE(waitFor([&]() { return readAll(indexPath).contains(QLatin1String("id=\"titre-1\"")); }, 5000));
            REQUIRE(waitFor([&]() { return mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1")) >= 0; }, 5000));
            CHECK(readAll(indexPath).contains(QLatin1String(">Nouveau titre<")));
        }

        SECTION("Le nom du clip du chutier : automatique il suit, choisi il reste, brut il se répare")
        {
            // Le défaut des vidéos AFEBABA (27 septembre 2026) : le miroir remettait « TA BOUTIQUE · 9:16 »
            // sur un clip qu'organiser_rushs venait de renommer ; et un rendu du projet Tek Tech gardait
            // le nom brut de son fichier (« h-compteur-300m-02072e7ff640.mov »).
            const auto binOf = [&]() {
                const int cid = mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"));
                return cid >= 0 ? binModel->getClipByBinID(timeline->getClipBinId(cid)) : nullptr;
            };
            REQUIRE(binOf());
            CHECK(binOf()->name() == QStringLiteral("Nouveau titre"));
            CHECK(binOf()->getProducerProperty(QStringLiteral("kdenlive:koracut_motion_nom_auto")) == QStringLiteral("Nouveau titre"));
            const auto renderedHash = [&]() { return binOf() ? binOf()->getProducerProperty(QStringLiteral("kdenlive:koracut_motion_hash")) : QString(); };

            // Un nom automatique suit le texte au rendu suivant.
            QString hash = renderedHash();
            r = call(timeline, QStringLiteral("motion_element"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("id"), QStringLiteral("titre-1")}, {QStringLiteral("texte"), QStringLiteral("Awa Ndiaye")}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(waitFor([&]() { return renderedHash() != hash && binOf() && binOf()->clipStatus() == FileStatus::StatusReady; }, 20000));
            REQUIRE(waitFor([&]() { return binOf()->name() == QStringLiteral("Awa Ndiaye"); }, 5000));

            // Un nom choisi (renommer_media, comme organiser_rushs) : le rendu suivant le garde.
            const QString binId = binOf()->clipId();
            r = call(timeline, QStringLiteral("renommer_media"), {{QStringLiteral("media_id"), binId}, {QStringLiteral("nom"), QStringLiteral("01 - Bandeau Awa")}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            hash = renderedHash();
            r = call(timeline, QStringLiteral("motion_element"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("id"), QStringLiteral("titre-1")}, {QStringLiteral("texte"), QStringLiteral("Awa Ndiaye, directrice")}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(waitFor([&]() { return renderedHash() != hash && binOf() && binOf()->clipStatus() == FileStatus::StatusReady; }, 20000));
            waitMs(800);
            CHECK(binOf()->name() == QStringLiteral("01 - Bandeau Awa"));
            CHECK(binOf()->clipName() == QStringLiteral("01 - Bandeau Awa"));

            // Un clip resté au nom brut de son rendu, sans trace du nom donné (projet d'avant) : réparé à la
            // relecture de sa composition (ici un déplacement, à l'ouverture pareil).
            binOf()->setProperties({{QStringLiteral("kdenlive:clipname"), QFileInfo(binOf()->url()).fileName()}, {QStringLiteral("kdenlive:koracut_motion_nom_auto"), QString()}});
            REQUIRE(binOf()->name() == QFileInfo(binOf()->url()).fileName());
            r = call(timeline, QStringLiteral("motion_element"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("id"), QStringLiteral("titre-1")}, {QStringLiteral("debut"), 0.5}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(waitFor([&]() { return binOf() && binOf()->name() == QStringLiteral("Awa Ndiaye, directrice"); }, 5000));
            CHECK(binOf()->getProducerProperty(QStringLiteral("kdenlive:koracut_motion_nom_auto")) == QStringLiteral("Awa Ndiaye, directrice"));
        }

        SECTION("Rogner par la gauche écrit data-koracut-in ; motion_poser déplace et marque une piste")
        {
            const int cid = mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"));
            REQUIRE(cid >= 0);
            const int length = timeline->getClipPlaytime(cid);
            REQUIRE(timeline->requestItemResize(cid, length - 5, false, true) == length - 5);
            REQUIRE(waitFor([&]() { return readAll(indexPath).contains(QLatin1String("data-koracut-in=\"0.2\"")); }, 5000));
            CHECK(readAll(indexPath).contains(QLatin1String("data-start=\"0.2\"")));
            // Le point d'entrée ne change pas l'empreinte : pas de nouveau rendu.
            CHECK_FALSE(mirror.renderer()->isPending(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1")));

            // Poser à 2 s : le clip bouge, le fichier suit.
            const int undoIndex = undoStack->index();
            r = call(timeline, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("position"), QStringLiteral("00:00:02:00")}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("change")).toBool());
            CHECK(r.value(QStringLiteral("poses")).toInt() == 1);
            CHECK(timeline->getClipPosition(cid) == int(std::lround(2 * fps)));
            CHECK(readAll(indexPath).contains(QLatin1String("data-start=\"2\"")));
            CHECK(undoStack->index() == undoIndex + 1);

            // Une piste vidéo ordinaire devient la piste Motion de la composition.
            const QString v1 = timeline->getTrackTagById(timeline->getTracksIds(false).first());
            int ordinary = -1;
            for (int tid : timeline->getTracksIds(false)) {
                if (timeline->getTrackProperty(tid, QStringLiteral("kdenlive:koracut_motion")).toString().isEmpty()) {
                    ordinary = tid;
                    break;
                }
            }
            REQUIRE(ordinary >= 0);
            Q_UNUSED(v1);
            // Le clip est déjà posé sur sa piste Motion : la marque n'y change rien de plus, l'outil le dit.
            r = call(timeline, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("piste_id"), ordinary}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(timeline->getTrackProperty(ordinary, QStringLiteral("kdenlive:koracut_motion")).toString() == QStringLiteral("bandeau-essai"));
            CHECK(errorCode(call(timeline, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("piste_id"), timeline->getTracksIds(true).first()}})) ==
                  QStringLiteral("mauvais_type_de_piste"));
        }
    }

    // Rien ne doit être en train de charger quand le projet se ferme.
    mirror.renderer()->waitForIdle(20000);
    waitMs(800);
    KoraCutMotionRenderer::setSimulator({});
    mirror.clear();
    KoraCutAgentTools::setMotionMirror(nullptr);
    Q_UNUSED(undoBefore);
    pCore->projectManager()->closeCurrentDocument(false, false);
}
