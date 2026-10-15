/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/clipcreator.hpp"
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

#include <algorithm>
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

            // Plus long que son rendu (4 s) : le bloc est rendu à nouveau à sa nouvelle durée, le clip la prend,
            // et le miroir ne remet pas l'ancienne dans le fichier en attendant (15 octobre 2026).
            const int before5 = undoStack->index();
            r = call(timeline, QStringLiteral("motion_element"),
                     {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}, {QStringLiteral("id"), QStringLiteral("titre-1")}, {QStringLiteral("duree"), 6.0}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("allongement_en_attente")).toBool());
            CHECK(undoStack->index() == before5 + 1);
            mirror.renderer()->waitForIdle(20000);
            CHECK(waitFor([&]() { return timeline->getClipPlaytime(mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"))) == int(std::lround(6 * fps)); }, 6000));
            waitMs(900);
            CHECK(timeline->getClipPlaytime(mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"))) == int(std::lround(6 * fps)));
            CHECK(timeline->getClipPosition(mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"))) == int(std::lround(fps)));
            CHECK(readAll(indexPath).contains(QLatin1String("data-duration=\"6\"")));
            CHECK(undoStack->index() == before5 + 1);
            // Annuler : 3 s, au clip et au fichier.
            undoStack->undo();
            CHECK(waitFor([&]() { return timeline->getClipPlaytime(mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"))) == int(std::lround(3 * fps)); }, 6000));
            waitMs(900);
            CHECK(readAll(indexPath).contains(QLatin1String("data-duration=\"3\"")));
            // Refaire, puis motion_rendre : c'est le rendu long qui revient (pas le premier, plus court), et
            // l'outil attend que le clip ait pris sa durée.
            undoStack->redo();
            r = call(timeline, QStringLiteral("motion_rendre"), {{QStringLiteral("nom"), QStringLiteral("bandeau-essai")}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(elementOf(r, QStringLiteral("titre-1")).value(QStringLiteral("len")).toInt() == int(std::lround(6 * fps)));
            CHECK_FALSE(r.contains(QStringLiteral("non_allonges")));
            waitMs(900);
            CHECK(timeline->getClipPlaytime(mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"))) == int(std::lround(6 * fps)));
            CHECK(readAll(indexPath).contains(QLatin1String("data-duration=\"6\"")));
            undoStack->undo();
            CHECK(waitFor([&]() { return timeline->getClipPlaytime(mirror.clipFor(QStringLiteral("bandeau-essai"), QStringLiteral("titre-1"))) == int(std::lround(3 * fps)); }, 6000));
            waitMs(900);
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

        SECTION("motion_poser dit quel élément n'a pas pu être posé : deux cartons de la même piste qui se chevauchent d'une image")
        {
            // Deux cartons de 2 s sur la même piste ; le second commence une image avant la fin du premier.
            const QString name = QStringLiteral("bandeau-essai");
            const int length = int(std::lround(2 * fps));
            const double secondStart = (length - 1) / fps;
            const QString html = QStringLiteral(R"(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<script src="assets/gsap.min.js"></script>
<style>
  body { margin: 0; background: transparent; }
  #root { position: relative; width: 1080px; height: 1920px; overflow: hidden; }
  .clip { position: absolute; inset: 0; }
</style>
</head>
<body>
<div id="root" data-composition-id="bandeau-essai" data-start="0" data-duration="6" data-fps="25" data-width="1080" data-height="1920">
  <section id="carton-1" class="clip" data-start="0" data-duration="2" data-track-index="1"><h1>Un</h1></section>
  <section id="carton-2" class="clip" data-start="%1" data-duration="2" data-track-index="1"><h1>Deux</h1></section>
</div>
<script>
  window.__timelines = window.__timelines || {};
  window.__timelines["bandeau-essai"] = gsap.timeline({ paused: true });
</script>
</body>
</html>
)").arg(secondStart);
            r = call(timeline, QStringLiteral("motion_ecrire"), {{QStringLiteral("nom"), name}, {QStringLiteral("html"), html}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(r.value(QStringLiteral("elements")).toArray().size() == 2);
            // motion_rendre attend la fin des rendus ET la pose : sa réponse dit déjà lequel n'a pas pu l'être,
            // sans autre attente ; motion_lister le dit aussi, dans la fiche de la composition.
            r = call(timeline, QStringLiteral("motion_rendre"), {{QStringLiteral("nom"), name}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("termine")).toBool());
            REQUIRE(r.value(QStringLiteral("non_poses")).toArray().size() == 1);
            CHECK(r.value(QStringLiteral("non_poses")).toArray().first().toObject().value(QStringLiteral("raison")).toString() == QStringLiteral("place_occupee"));
            CHECK_FALSE(r.contains(QStringLiteral("non_deplaces")));
            {
                const QJsonObject listed = firstComposition(call(timeline, QStringLiteral("motion_lister"), {}));
                INFO(QJsonDocument(listed).toJson(QJsonDocument::Compact).toStdString());
                CHECK(listed.value(QStringLiteral("non_poses")).toArray() == r.value(QStringLiteral("non_poses")).toArray());
            }
            // Les deux rendus sont finis et chargés dans le chutier : le premier arrivé a pris la place.
            REQUIRE(waitFor(
                [&]() {
                    const auto states = mirror.elements(name);
                    return states.size() == 2 && std::all_of(states.cbegin(), states.cend(), [](const KoraCutMotionMirror::ElementState &e) {
                               return !e.pending && !e.renderPath.isEmpty() && !e.binId.isEmpty();
                           });
                },
                20000));
            mirror.renderer()->waitForIdle(20000);
            waitMs(800);

            r = call(timeline, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), name}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            const int first = mirror.clipFor(name, QStringLiteral("carton-1"));
            const int second = mirror.clipFor(name, QStringLiteral("carton-2"));
            // Un seul des deux est dans la timeline, et le compte le dit.
            REQUIRE((first >= 0) != (second >= 0));
            CHECK(r.value(QStringLiteral("poses")).toInt() == 1);
            // L'autre est nommé, avec sa piste, sa place en images, la raison et le clip qui tient la place.
            // Ce n'est pas un rendu en attente : sans_clip ne le compte pas.
            const QJsonArray refused = r.value(QStringLiteral("non_poses")).toArray();
            REQUIRE(refused.size() == 1);
            const QJsonObject lost = refused.first().toObject();
            const int placedClip = first >= 0 ? first : second;
            CHECK(lost.value(QStringLiteral("element")).toString() == (first >= 0 ? QStringLiteral("carton-2") : QStringLiteral("carton-1")));
            CHECK(lost.value(QStringLiteral("raison")).toString() == QStringLiteral("place_occupee"));
            CHECK(lost.value(QStringLiteral("position")).toInt() == (first >= 0 ? length - 1 : 0));
            CHECK(lost.value(QStringLiteral("duree")).toInt() == length);
            CHECK(lost.value(QStringLiteral("piste_id")).toInt(-1) == timeline->getClipTrackId(placedClip));
            CHECK(!lost.value(QStringLiteral("piste")).toString().isEmpty());
            CHECK(lost.value(QStringLiteral("occupe_par")).toObject().value(QStringLiteral("clip_id")).toInt(-1) == placedClip);
            CHECK(r.value(QStringLiteral("sans_clip")).toInt() == 0);
            CHECK(r.value(QStringLiteral("remarque")).toString().contains(lost.value(QStringLiteral("element")).toString()));

            // Le chevauchement levé (le second carton commence quand le premier finit) : tout est posé,
            // et la réponse ne garde rien de l'ancien refus.
            r = call(timeline, QStringLiteral("motion_element"), {{QStringLiteral("nom"), name}, {QStringLiteral("id"), QStringLiteral("carton-2")}, {QStringLiteral("debut"), 2.0}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            mirror.renderer()->waitForIdle(20000);
            waitMs(800);
            r = call(timeline, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), name}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("poses")).toInt() == 2);
            CHECK_FALSE(r.contains(QStringLiteral("non_poses")));
            CHECK(mirror.clipFor(name, QStringLiteral("carton-1")) >= 0);
            CHECK(mirror.clipFor(name, QStringLiteral("carton-2")) >= 0);
            CHECK_FALSE(firstComposition(call(timeline, QStringLiteral("motion_lister"), {})).contains(QStringLiteral("non_poses")));
        }

        SECTION("Un clip déjà posé qui ne peut pas être déplacé : non_deplaces dit où il est resté (position de motion_poser, début du HTML)")
        {
            const QString name = QStringLiteral("bandeau-essai");
            const int cid = mirror.clipFor(name, QStringLiteral("titre-1"));
            REQUIRE(cid >= 0);
            const int tid = timeline->getClipTrackId(cid);
            const int length = timeline->getClipPlaytime(cid);
            REQUIRE(timeline->getClipPosition(cid) == 0);
            // Un autre clip sur la piste de la composition, là où le titre devrait arriver.
            const QString red = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 20, true);
            int obstacle = -1;
            REQUIRE(timeline->requestClipInsertion(red, tid, length + 50, obstacle));
            waitMs(500);

            // 1. motion_poser avec position : le décalage n'a pas lieu, et l'outil le dit.
            r = call(timeline, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), name}, {QStringLiteral("position"), length}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(timeline->getClipPosition(cid) == 0);
            CHECK(r.value(QStringLiteral("poses")).toInt() == 1);
            CHECK_FALSE(r.value(QStringLiteral("change")).toBool(true));
            REQUIRE(r.contains(QStringLiteral("position_appliquee")));
            CHECK_FALSE(r.value(QStringLiteral("position_appliquee")).toBool(true));
            CHECK_FALSE(r.contains(QStringLiteral("non_poses")));
            QJsonArray stuck = r.value(QStringLiteral("non_deplaces")).toArray();
            REQUIRE(stuck.size() == 1);
            QJsonObject kept = stuck.first().toObject();
            CHECK(kept.value(QStringLiteral("element")).toString() == QStringLiteral("titre-1"));
            CHECK(kept.value(QStringLiteral("clip_id")).toInt(-1) == cid);
            CHECK(kept.value(QStringLiteral("position")).toInt(-1) == length);
            CHECK(kept.value(QStringLiteral("duree")).toInt() == length);
            CHECK(kept.value(QStringLiteral("piste_id")).toInt(-1) == tid);
            CHECK(kept.value(QStringLiteral("raison")).toString() == QStringLiteral("place_occupee"));
            CHECK(kept.value(QStringLiteral("occupe_par")).toObject().value(QStringLiteral("clip_id")).toInt(-1) == obstacle);
            CHECK(kept.value(QStringLiteral("reste_a")).toObject().value(QStringLiteral("pos")).toInt(-1) == 0);
            CHECK(kept.value(QStringLiteral("reste_a")).toObject().value(QStringLiteral("piste_id")).toInt(-1) == tid);
            CHECK(r.value(QStringLiteral("remarque")).toString().contains(QStringLiteral("titre-1")));
            // motion_lister le dit aussi.
            CHECK(firstComposition(call(timeline, QStringLiteral("motion_lister"), {})).value(QStringLiteral("non_deplaces")).toArray() == stuck);
            // Sans position, rien n'est demandé : rien à signaler.
            r = call(timeline, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), name}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK_FALSE(r.contains(QStringLiteral("non_deplaces")));
            CHECK_FALSE(r.contains(QStringLiteral("position_appliquee")));

            // 2. Le début écrit dans le HTML mène à la même place prise : le clip reste, la fiche le dit.
            r = call(timeline, QStringLiteral("motion_element"), {{QStringLiteral("nom"), name}, {QStringLiteral("id"), QStringLiteral("titre-1")}, {QStringLiteral("debut"), length / fps}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(timeline->getClipPosition(cid) == 0);
            // motion_element le dit dans sa propre réponse, pour cet élément.
            REQUIRE(r.value(QStringLiteral("non_deplaces")).toArray().size() == 1);
            CHECK(r.value(QStringLiteral("non_deplaces")).toArray().first().toObject().value(QStringLiteral("element")).toString() == QStringLiteral("titre-1"));
            CHECK(r.value(QStringLiteral("remarque")).toString().contains(QStringLiteral("titre-1")));
            const QJsonObject listed = firstComposition(call(timeline, QStringLiteral("motion_lister"), {}));
            INFO(QJsonDocument(listed).toJson(QJsonDocument::Compact).toStdString());
            stuck = listed.value(QStringLiteral("non_deplaces")).toArray();
            REQUIRE(stuck.size() == 1);
            kept = stuck.first().toObject();
            CHECK(kept.value(QStringLiteral("element")).toString() == QStringLiteral("titre-1"));
            CHECK(kept.value(QStringLiteral("position")).toInt(-1) == length);
            CHECK(kept.value(QStringLiteral("raison")).toString() == QStringLiteral("place_occupee"));
            CHECK(kept.value(QStringLiteral("reste_a")).toObject().value(QStringLiteral("pos")).toInt(-1) == 0);
            r = call(timeline, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), name}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("non_deplaces")).toArray() == stuck);
            CHECK(r.value(QStringLiteral("remarque")).toString().contains(QStringLiteral("titre-1")));

            // La place libérée : la timeline a changé, le fichier reprend la place réelle du clip (début 0),
            // l'ancienne demande est oubliée et plus rien n'est signalé. Le début redonné, le clip y va.
            REQUIRE(timeline->requestItemDeletion(obstacle, false));
            r = call(timeline, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), name}});
            INFO(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK_FALSE(r.contains(QStringLiteral("non_deplaces")));
            CHECK(timeline->getClipPosition(cid) == 0);
            CHECK(elementOf(r, QStringLiteral("titre-1")).value(QStringLiteral("debut")).toDouble() == Approx(0.0));
            r = call(timeline, QStringLiteral("motion_element"), {{QStringLiteral("nom"), name}, {QStringLiteral("id"), QStringLiteral("titre-1")}, {QStringLiteral("debut"), length / fps}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(timeline->getClipPosition(cid) == length);
            waitMs(500);
            CHECK_FALSE(firstComposition(call(timeline, QStringLiteral("motion_lister"), {})).contains(QStringLiteral("non_deplaces")));
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

namespace {

/** Le nombre de clips posés dans une séquence, toutes pistes confondues. */
int clipsIn(const std::shared_ptr<TimelineItemModel> &tl)
{
    int n = 0;
    for (bool audio : {false, true}) {
        for (int tid : tl->getTracksIds(audio)) {
            for (int id : tl->getItemsInRange(tid, 0, -1, false)) {
                if (tl->isClip(id)) {
                    ++n;
                }
            }
        }
    }
    return n;
}

QJsonObject compositionOf(const QJsonObject &list, const QString &name)
{
    for (const QJsonValue &v : list.value(QStringLiteral("compositions")).toArray()) {
        if (v.toObject().value(QStringLiteral("nom")).toString() == name) {
            return v.toObject();
        }
    }
    return {};
}

std::string dumpJson(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();
}

} // namespace

// 1er octobre 2026, quatre publicités dans un projet : un rendu fini pendant qu'une autre séquence est
// devant posait son clip dans cette autre séquence. Chaque élément reste dans SA séquence (pose, nouveau
// rendu, retime, retrait, annulation, séquence fermée). Et dans la foulée : le nom d'une composition dont
// la création est annulée redevient libre, et les outils savent apporter des polices et des images.
TEST_CASE("Outils Motion : deux séquences, noms libérés, fichiers fournis", "[KoraCutMotionTools]")
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

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    document.setUrl(QUrl::fromLocalFile(tmp.path() + QStringLiteral("/pubs.koracut")));
    {
        QFile marker(tmp.path() + QStringLiteral("/pubs.koracut"));
        REQUIRE(marker.open(QIODevice::WriteOnly));
    }
    const bool haveFfmpeg = !ffmpegPath().isEmpty();
    KoraCutMotionRenderer::setSimulator([](const KoraCutMotionRenderRequest &r) {
        // La durée : celle de l'enveloppe, sinon celle de la racine de l'index.html (élément seul).
        double seconds = 3.;
        const QRegularExpression duration(QStringLiteral("data-composition-id=\"[^\"]*\"[^>]*data-duration=\"([0-9.]+)\""));
        QRegularExpressionMatch m = duration.match(r.envelopeHtml);
        QFile index(r.projectDir + QStringLiteral("/index.html"));
        if (!m.hasMatch() && index.open(QIODevice::ReadOnly)) {
            m = duration.match(QString::fromUtf8(index.readAll()));
        }
        if (m.hasMatch()) {
            seconds = m.captured(1).toDouble();
        }
        return fakeRender(r, seconds);
    });

    // La deuxième séquence du projet.
    const QString seqBin = ClipCreator::createPlaylistClip(QStringLiteral("Publicité 2"), {2, 2}, QStringLiteral("-1"), binModel);
    REQUIRE(seqBin != QLatin1String("-1"));
    QUuid uuid2;
    const QMap<QUuid, QString> sequences = binModel->getAllSequenceClips();
    for (auto it = sequences.constBegin(); it != sequences.constEnd(); ++it) {
        if (it.value() == seqBin) {
            uuid2 = it.key();
        }
    }
    REQUIRE_FALSE(uuid2.isNull());
    auto timeline2 = document.getTimeline(uuid2);
    REQUIRE(timeline2);
    const QString seq1 = document.uuid().toString(QUuid::WithoutBraces);
    const QString seq2 = uuid2.toString(QUuid::WithoutBraces);

    KoraCutMotionMirror mirror;
    mirror.setDocument(&document, timeline);
    auto activate = [&](const std::shared_ptr<TimelineItemModel> &tl) {
        pCore->projectManager()->testSetActiveTimeline(tl);
        mirror.setTimeline(tl);
    };
    // Les deux onglets sont ouverts ; la séquence 1 est devant.
    activate(timeline2);
    activate(timeline);
    const QString motionRoot = tmp.path() + QStringLiteral("/motion");

    if (haveFfmpeg) {
        SECTION("Un rendu qui finit pendant qu'une autre séquence est devant pose son clip dans SA séquence")
        {
            const int tracks2 = timeline2->getTracksCount();
            // L'élément est demandé dans la séquence 1, sans attendre ; la séquence 2 passe devant avant la fin du rendu.
            QJsonObject r = call(timeline, QStringLiteral("motion_element_seul"),
                                 {{QStringLiteral("nom"), QStringLiteral("titre-s1")}, {QStringLiteral("sorte"), QStringLiteral("titre")}, {QStringLiteral("texte"), QStringLiteral("Pub un")},
                                  {QStringLiteral("position"), 10}, {QStringLiteral("duree"), 50}, {QStringLiteral("attendre"), false}});
            INFO(dumpJson(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("sequence")).toString() == seq1);
            activate(timeline2);
            REQUIRE(waitFor([&]() { return mirror.clipFor(QStringLiteral("titre-s1"), QStringLiteral("titre-s1")) >= 0; }, 20000));
            const int cid1 = mirror.clipFor(QStringLiteral("titre-s1"), QStringLiteral("titre-s1"));
            CHECK(timeline->isClip(cid1));
            CHECK_FALSE(timeline2->isClip(cid1));
            CHECK(timeline->getClipPosition(cid1) == 10);
            CHECK(clipsIn(timeline) == 1);
            CHECK(clipsIn(timeline2) == 0);
            CHECK(timeline2->getTracksCount() == tracks2); // pas de piste « Animations » créée pour l'occasion
            const QString index1 = motionRoot + QStringLiteral("/titre-s1/index.html");
            CHECK(readAll(index1).contains(QStringLiteral("data-koracut-sequence=\"%1\"").arg(seq1)));

            // motion_lister dit la séquence de chaque composition, et le clip dans cette séquence.
            QJsonObject list = call(timeline2, QStringLiteral("motion_lister"), {});
            QJsonObject comp = compositionOf(list, QStringLiteral("titre-s1"));
            INFO(dumpJson(comp));
            CHECK(comp.value(QStringLiteral("sequence")).toString() == seq1);
            CHECK_FALSE(comp.value(QStringLiteral("sequence_active")).toBool());
            CHECK(elementOf(comp, QStringLiteral("titre-s1")).value(QStringLiteral("clip_id")).toInt(-1) == cid1);
            CHECK(elementOf(comp, QStringLiteral("titre-s1")).value(QStringLiteral("pos")).toInt(-1) == 10);
            CHECK(call(timeline2, QStringLiteral("motion_lister"), {{QStringLiteral("sequence"), QStringLiteral("active")}}).value(QStringLiteral("compositions")).toArray().isEmpty());

            // Nouveau rendu (le texte change) pendant que la séquence 2 est devant : même clip, même séquence.
            r = call(timeline2, QStringLiteral("motion_element"),
                     {{QStringLiteral("nom"), QStringLiteral("titre-s1")}, {QStringLiteral("id"), QStringLiteral("titre-s1")}, {QStringLiteral("texte"), QStringLiteral("Pub une")}});
            INFO(dumpJson(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("rendu_relance")).toBool());
            mirror.renderer()->waitForIdle(20000);
            waitMs(1200);
            CHECK(mirror.clipFor(QStringLiteral("titre-s1"), QStringLiteral("titre-s1")) == cid1);
            CHECK(clipsIn(timeline) == 1);
            CHECK(clipsIn(timeline2) == 0);
            CHECK(timeline2->getTracksCount() == tracks2);

            // Retime depuis l'autre séquence : c'est le clip de la séquence 1 qui bouge.
            r = call(timeline2, QStringLiteral("motion_element"),
                     {{QStringLiteral("nom"), QStringLiteral("titre-s1")}, {QStringLiteral("id"), QStringLiteral("titre-s1")}, {QStringLiteral("debut"), 30 / fps}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(timeline->getClipPosition(cid1) == 30);
            CHECK(clipsIn(timeline2) == 0);

            // Un élément créé pendant que la séquence 2 est devant vit dans la séquence 2.
            r = call(timeline2, QStringLiteral("motion_element_seul"),
                     {{QStringLiteral("nom"), QStringLiteral("titre-s2")}, {QStringLiteral("sorte"), QStringLiteral("titre")}, {QStringLiteral("texte"), QStringLiteral("Pub deux")},
                      {QStringLiteral("position"), 5}, {QStringLiteral("duree"), 40}});
            INFO(dumpJson(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(r.contains(QStringLiteral("clip_id")));
            const int cid2 = r.value(QStringLiteral("clip_id")).toInt();
            CHECK(timeline2->isClip(cid2));
            CHECK(r.value(QStringLiteral("sequence")).toString() == seq2);
            CHECK(clipsIn(timeline) == 1);
            CHECK(clipsIn(timeline2) == 1);
            comp = compositionOf(call(timeline2, QStringLiteral("motion_lister"), {}), QStringLiteral("titre-s2"));
            CHECK(comp.value(QStringLiteral("sequence_nom")).toString() == QStringLiteral("Publicité 2"));
            CHECK(comp.value(QStringLiteral("sequence_active")).toBool());
            // Une composition à plusieurs éléments créée là reçoit la séquence devant, elle aussi.
            r = call(timeline2, QStringLiteral("motion_creer"), {{QStringLiteral("nom"), QStringLiteral("gabarit-s2")}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("sequence")).toString() == seq2);
            REQUIRE(waitFor([&]() { return mirror.clipFor(QStringLiteral("gabarit-s2"), QStringLiteral("titre-1")) >= 0; }, 20000));
            CHECK(timeline2->isClip(mirror.clipFor(QStringLiteral("gabarit-s2"), QStringLiteral("titre-1"))));
            CHECK(clipsIn(timeline) == 1);

            // motion_ecrire avec un HTML qui a perdu data-koracut-sequence : la composition garde sa séquence.
            QString html = call(timeline2, QStringLiteral("motion_lire"), {{QStringLiteral("nom"), QStringLiteral("titre-s1")}}).value(QStringLiteral("html")).toString();
            REQUIRE(html.contains(QStringLiteral(" data-koracut-sequence=\"%1\"").arg(seq1)));
            html.remove(QStringLiteral(" data-koracut-sequence=\"%1\"").arg(seq1));
            // Et, pour un élément seul, sa sorte, sa place et son style (12 octobre 2026) : sans eux il n'était plus
            // reconnu, et son clip repartait à l'image 0 sur une piste neuve.
            const int placeBefore = timeline->getClipPosition(cid1);
            const int trackBefore = timeline->getClipTrackId(cid1);
            QStringList lost;
            for (const char *name : {"data-koracut-element", "data-koracut-start", "data-koracut-style"}) {
                const QRegularExpression attribute(QStringLiteral(" %1=\"[^\"]*\"").arg(QLatin1String(name)));
                if (attribute.match(html).hasMatch()) {
                    lost << QLatin1String(name);
                    html.remove(attribute);
                }
            }
            REQUIRE(lost.contains(QStringLiteral("data-koracut-element")));
            html.replace(QStringLiteral("Pub une"), QStringLiteral("Pub réécrite"));
            r = call(timeline2, QStringLiteral("motion_ecrire"), {{QStringLiteral("nom"), QStringLiteral("titre-s1")}, {QStringLiteral("html"), html}});
            INFO(dumpJson(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("sequence")).toString() == seq1);
            mirror.renderer()->waitForIdle(20000);
            waitMs(1200);
            CHECK(mirror.clipFor(QStringLiteral("titre-s1"), QStringLiteral("titre-s1")) == cid1);
            CHECK(clipsIn(timeline) == 1);
            CHECK(clipsIn(timeline2) == 2);
            {
                const QString written = call(timeline2, QStringLiteral("motion_lire"), {{QStringLiteral("nom"), QStringLiteral("titre-s1")}}).value(QStringLiteral("html")).toString();
                for (const QString &name : std::as_const(lost)) {
                    INFO(name.toStdString());
                    CHECK(written.contains(QLatin1Char(' ') + name + QLatin1String("=\"")));
                }
                CHECK(written.contains(QStringLiteral("Pub réécrite")));
                CHECK(timeline->getClipPosition(cid1) == placeBefore);
                CHECK(timeline->getClipTrackId(cid1) == trackBefore);
            }

            // piste_id est une piste de la séquence devant : refusée pour une composition d'une autre séquence.
            CHECK(errorCode(call(timeline2, QStringLiteral("motion_poser"),
                                 {{QStringLiteral("nom"), QStringLiteral("titre-s1")}, {QStringLiteral("piste_id"), timeline2->getTracksIds(false).first()}})) ==
                  QStringLiteral("autre_sequence"));

            // Retrait depuis l'autre séquence, puis Annuler : la composition est retirée, puis revient, dans la séquence 1.
            REQUIRE(timeline->requestItemDeletion(cid1, true));
            REQUIRE(waitFor([&]() { return readAll(index1).contains(QLatin1String("data-koracut-retire=\"1\"")); }, 4000));
            list = call(timeline2, QStringLiteral("motion_lister"), {});
            CHECK(compositionOf(list, QStringLiteral("titre-s1")).isEmpty());
            CHECK(list.value(QStringLiteral("retirees_masquees")).toInt() == 1);
            CHECK(compositionOf(call(timeline2, QStringLiteral("motion_lister"), {{QStringLiteral("retirees"), true}}), QStringLiteral("titre-s1")).value(QStringLiteral("retiree")).toBool());
            undoStack->undo();
            REQUIRE(waitFor([&]() { return !readAll(index1).contains(QLatin1String("data-koracut-retire")); }, 4000));
            CHECK(clipsIn(timeline) == 1);
            CHECK(clipsIn(timeline2) == 2);

            // Sa séquence n'est plus ouverte dans un onglet quand son rendu se termine : rien n'est posé
            // ailleurs, et tout est en place à la réouverture.
            r = call(timeline2, QStringLiteral("motion_element"),
                     {{QStringLiteral("nom"), QStringLiteral("titre-s1")}, {QStringLiteral("id"), QStringLiteral("titre-s1")}, {QStringLiteral("texte"), QStringLiteral("Pub fermée")}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            const int tracks2Before = timeline2->getTracksCount();
            timeline->isClosed = true;
            mirror.renderer()->waitForIdle(20000);
            waitMs(1200);
            CHECK(clipsIn(timeline2) == 2);
            const int tracks2After = timeline2->getTracksCount();
            CHECK(tracks2After == tracks2Before); // pas de piste créée pour l'occasion
            CHECK(errorCode(call(timeline2, QStringLiteral("motion_poser"), {{QStringLiteral("nom"), QStringLiteral("titre-s1")}})) == QStringLiteral("sequence_fermee"));
            CHECK(compositionOf(call(timeline2, QStringLiteral("motion_lister"), {}), QStringLiteral("titre-s1")).value(QStringLiteral("sequence_ouverte")).toBool(true) == false);
            activate(timeline); // testSetActiveTimeline la rouvre
            mirror.renderer()->waitForIdle(20000);
            waitMs(1200);
            CHECK(clipsIn(timeline) == 1);
            CHECK(clipsIn(timeline2) == 2);
            CHECK(timeline2->getTracksCount() == tracks2After);
            CHECK(elementOf(compositionOf(call(timeline, QStringLiteral("motion_lister"), {}), QStringLiteral("titre-s1")), QStringLiteral("titre-s1")).value(QStringLiteral("rendu")).toString() ==
                  QStringLiteral("pret"));
        }

        SECTION("Annuler une création libère le nom ; un dossier retiré à la main aussi")
        {
            QJsonObject r = call(timeline, QStringLiteral("motion_element_seul"),
                                 {{QStringLiteral("nom"), QStringLiteral("titre-b")}, {QStringLiteral("sorte"), QStringLiteral("titre")}, {QStringLiteral("texte"), QStringLiteral("Premier jet")},
                                  {QStringLiteral("position"), 0}, {QStringLiteral("duree"), 40}});
            INFO(dumpJson(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(r.contains(QStringLiteral("clip_id")));
            const QString dir = motionRoot + QStringLiteral("/titre-b");
            CHECK(errorCode(call(timeline, QStringLiteral("motion_element_seul"),
                                 {{QStringLiteral("nom"), QStringLiteral("titre-b")}, {QStringLiteral("texte"), QStringLiteral("x")}, {QStringLiteral("position"), 100}, {QStringLiteral("duree"), 40}})) ==
                  QStringLiteral("existe_deja"));
            // Annuler : le clip part ; la composition n'est plus dans la liste, son dossier reste (Rétablir la remet).
            undoStack->undo();
            CHECK(clipsIn(timeline) == 0);
            QJsonObject list = call(timeline, QStringLiteral("motion_lister"), {});
            CHECK(compositionOf(list, QStringLiteral("titre-b")).isEmpty());
            CHECK(list.value(QStringLiteral("retirees_masquees")).toInt() == 1);
            CHECK(QFile::exists(dir + QStringLiteral("/index.html")));
            undoStack->redo();
            REQUIRE(waitFor([&]() { return !readAll(dir + QStringLiteral("/index.html")).contains(QLatin1String("data-koracut-retire")); }, 4000));
            CHECK(clipsIn(timeline) == 1);
            CHECK_FALSE(compositionOf(call(timeline, QStringLiteral("motion_lister"), {}), QStringLiteral("titre-b")).isEmpty());
            undoStack->undo();
            // Le nom est libre : l'ancien dossier part dans la corbeille du projet, rien n'est supprimé.
            r = call(timeline, QStringLiteral("motion_element_seul"),
                     {{QStringLiteral("nom"), QStringLiteral("titre-b")}, {QStringLiteral("sorte"), QStringLiteral("titre")}, {QStringLiteral("texte"), QStringLiteral("Second jet")},
                      {QStringLiteral("position"), 0}, {QStringLiteral("duree"), 40}});
            INFO(dumpJson(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(r.contains(QStringLiteral("clip_id")));
            const QString trashed = r.value(QStringLiteral("ancienne_retiree")).toString();
            CHECK(trashed.startsWith(motionRoot + QStringLiteral("/.corbeille/titre-b-")));
            CHECK(readAll(trashed + QStringLiteral("/index.html")).contains(QLatin1String("Premier jet")));
            CHECK(readAll(dir + QStringLiteral("/index.html")).contains(QLatin1String("Second jet")));
            list = call(timeline, QStringLiteral("motion_lister"), {{QStringLiteral("retirees"), true}});
            CHECK(list.value(QStringLiteral("compositions")).toArray().size() == 1); // la corbeille n'est pas une composition
            CHECK_FALSE(compositionOf(list, QStringLiteral("titre-b")).value(QStringLiteral("retiree")).toBool());
            mirror.renderer()->waitForIdle(20000);
            waitMs(900);

            // Un dossier supprimé à la main : le nom est libre tout de suite, sans relancer l'application.
            r = call(timeline, QStringLiteral("motion_creer"), {{QStringLiteral("nom"), QStringLiteral("a-la-main")}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            mirror.renderer()->waitForIdle(20000);
            waitMs(900);
            CHECK(errorCode(call(timeline, QStringLiteral("motion_creer"), {{QStringLiteral("nom"), QStringLiteral("a-la-main")}})) == QStringLiteral("existe_deja"));
            REQUIRE(QDir(motionRoot + QStringLiteral("/a-la-main")).removeRecursively());
            CHECK(compositionOf(call(timeline, QStringLiteral("motion_lister"), {}), QStringLiteral("a-la-main")).isEmpty());
            r = call(timeline, QStringLiteral("motion_creer"), {{QStringLiteral("nom"), QStringLiteral("a-la-main")}});
            INFO(dumpJson(r));
            CHECK(r.value(QStringLiteral("ok")).toBool());
        }

        SECTION("Un élément seul dont la dernière image tombe sur le début d'un autre va sur une autre piste")
        {
            // « debut » tient les images 39 à 78 de la piste « Animations ».
            QJsonObject r = call(timeline, QStringLiteral("motion_element_seul"),
                                 {{QStringLiteral("nom"), QStringLiteral("debut")}, {QStringLiteral("sorte"), QStringLiteral("titre")}, {QStringLiteral("texte"), QStringLiteral("Un")},
                                  {QStringLiteral("position"), 39}, {QStringLiteral("duree"), 40}});
            INFO(dumpJson(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(r.contains(QStringLiteral("clip_id")));
            const int firstTrack = r.value(QStringLiteral("piste_id")).toInt(-1);
            // « avant » demande les images 0 à 39 : sa dernière image est la première de « debut ». La piste
            // n'est pas libre : il lui faut une autre piste, et son clip est posé.
            r = call(timeline, QStringLiteral("motion_element_seul"),
                     {{QStringLiteral("nom"), QStringLiteral("avant")}, {QStringLiteral("sorte"), QStringLiteral("titre")}, {QStringLiteral("texte"), QStringLiteral("Deux")},
                      {QStringLiteral("position"), 0}, {QStringLiteral("duree"), 40}});
            INFO(dumpJson(r));
            mirror.renderer()->waitForIdle(20000);
            waitMs(900);
            const int second = mirror.clipFor(QStringLiteral("avant"), QStringLiteral("avant"));
            INFO(dumpJson(compositionOf(call(timeline, QStringLiteral("motion_lister"), {}), QStringLiteral("avant"))));
            REQUIRE(second >= 0);
            CHECK(timeline->getClipPosition(second) == 0);
            CHECK(timeline->getClipPlaytime(second) == 40);
            CHECK(timeline->getClipTrackId(second) != firstTrack);
            CHECK(mirror.placementRefusals(QStringLiteral("avant")).isEmpty());
        }
    } else {
        WARN("ffmpeg introuvable : les séquences et l'annulation ne sont pas vérifiées (elles demandent un rendu).");
    }

    SECTION("fichiers : des polices et des images copiées dans assets/ avant le rendu")
    {
        QDir().mkpath(tmp.path() + QStringLiteral("/Charte"));
        const QString font = tmp.path() + QStringLiteral("/Charte/Outfit.ttf");
        const QString logo = tmp.path() + QStringLiteral("/logo.png");
        for (const QString &path : {font, logo}) {
            QFile f(path);
            REQUIRE(f.open(QIODevice::WriteOnly));
            f.write(path.toUtf8());
        }
        const QString brief = QStringLiteral("<style>@font-face{font-family:\"Outfit\";src:url(\"assets/Outfit.ttf\")}#t{font-family:\"Outfit\"}</style>"
                                             "<h1 id=\"t\" data-koracut-texte=\"1\">Univers</h1><img src=\"assets/logo.png\">"
                                             "<script>tl.fromTo(\"#t\",{opacity:0},{opacity:1,duration:0.4},0);</script>");
        // Un fichier absent : rien n'est créé.
        QJsonObject r = call(timeline, QStringLiteral("motion_element_seul"),
                             {{QStringLiteral("nom"), QStringLiteral("carte-absente")}, {QStringLiteral("sorte"), QStringLiteral("autre")}, {QStringLiteral("brief_html"), brief},
                              {QStringLiteral("fichiers"), QJsonArray{font, QStringLiteral("absent.ttf")}}, {QStringLiteral("position"), 100}, {QStringLiteral("duree"), 30},
                              {QStringLiteral("attendre"), false}});
        CHECK(errorCode(r) == QStringLiteral("fichier_introuvable"));
        CHECK_FALSE(QFile::exists(motionRoot + QStringLiteral("/carte-absente")));
        // Deux fichiers du même nom iraient au même endroit.
        QDir().mkpath(tmp.path() + QStringLiteral("/Autre"));
        REQUIRE(QFile::copy(font, tmp.path() + QStringLiteral("/Autre/Outfit.ttf")));
        CHECK(errorCode(call(timeline, QStringLiteral("motion_element_seul"),
                             {{QStringLiteral("nom"), QStringLiteral("carte-double")}, {QStringLiteral("sorte"), QStringLiteral("autre")}, {QStringLiteral("brief_html"), brief},
                              {QStringLiteral("fichiers"), QJsonArray{font, QStringLiteral("Autre/Outfit.ttf")}}, {QStringLiteral("position"), 100}, {QStringLiteral("duree"), 30}})) ==
              QStringLiteral("entree_invalide"));
        // Absolu et relatif au dossier du projet.
        r = call(timeline, QStringLiteral("motion_element_seul"),
                 {{QStringLiteral("nom"), QStringLiteral("carte")}, {QStringLiteral("sorte"), QStringLiteral("autre")}, {QStringLiteral("brief_html"), brief},
                  {QStringLiteral("fichiers"), QJsonArray{font, QStringLiteral("logo.png")}}, {QStringLiteral("position"), 100}, {QStringLiteral("duree"), 30},
                  {QStringLiteral("attendre"), false}});
        INFO(dumpJson(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("fichiers_copies")).toArray() == QJsonArray{QStringLiteral("assets/Outfit.ttf"), QStringLiteral("assets/logo.png")});
        CHECK(readAll(motionRoot + QStringLiteral("/carte/assets/Outfit.ttf")) == font);
        CHECK(QFile::exists(motionRoot + QStringLiteral("/carte/assets/logo.png")));
        CHECK(QFile::exists(motionRoot + QStringLiteral("/carte/assets/gsap.min.js")));
        // motion_creer et motion_ecrire prennent la même liste.
        r = call(timeline, QStringLiteral("motion_creer"), {{QStringLiteral("nom"), QStringLiteral("habillage")}, {QStringLiteral("fichiers"), QJsonArray{QStringLiteral("Charte/Outfit.ttf")}}});
        INFO(dumpJson(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(QFile::exists(motionRoot + QStringLiteral("/habillage/assets/Outfit.ttf")));
        const QString html = call(timeline, QStringLiteral("motion_lire"), {{QStringLiteral("nom"), QStringLiteral("habillage")}}).value(QStringLiteral("html")).toString();
        r = call(timeline, QStringLiteral("motion_ecrire"),
                 {{QStringLiteral("nom"), QStringLiteral("habillage")}, {QStringLiteral("html"), QString(html).replace(QStringLiteral("Nouveau titre"), QStringLiteral("Avec logo"))},
                  {QStringLiteral("fichiers"), QJsonArray{logo}}});
        INFO(dumpJson(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("fichiers_copies")).toArray() == QJsonArray{QStringLiteral("assets/logo.png")});
        CHECK(QFile::exists(motionRoot + QStringLiteral("/habillage/assets/logo.png")));
        CHECK(errorCode(call(timeline, QStringLiteral("motion_ecrire"),
                             {{QStringLiteral("nom"), QStringLiteral("habillage")}, {QStringLiteral("html"), html}, {QStringLiteral("fichiers"), QJsonArray{QStringLiteral("~/koracut-absent-xyz.png")}}})) ==
              QStringLiteral("fichier_introuvable"));
    }

    mirror.renderer()->waitForIdle(20000);
    waitMs(800);
    KoraCutMotionRenderer::setSimulator({});
    mirror.clear();
    KoraCutAgentTools::setMotionMirror(nullptr);
    pCore->projectManager()->closeCurrentDocument(false, false);
}
