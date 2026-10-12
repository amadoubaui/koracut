/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Lot 5 du document 19 (24 septembre 2026) : les animations élément par élément. Une composition par
// élément (catalogue, pose sur la piste partagée, retrait et retour), une transition HyperFrames qui suit
// sa coupe, et les gestes de l'application seule (entrée, sortie, texte, transitions). Chaque geste : une
// étape d'annulation. Le rendu est simulé par ffmpeg, comme dans koracutmotiontoolstest.

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/projectclip.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagenttools.h"
#include "koracut/motion/koracutmotioncatalog.h"
#include "koracut/motion/koracutmotionmirror.h"
#include "koracut/koracutanimatemenu.h"
#include "koracut/motion/koracutmotionproject.h"
#include "koracut/motion/koracutmotionrenderer.h"
#include "koracut/motion/koracutmotionsync.h"

#include <QAction>
#include <QDir>
#include <QMenu>
#include <QWidget>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>

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
    QString found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"), {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin")});
    return found.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("ffmpeg")) : found;
}

bool fakeRender(const KoraCutMotionRenderRequest &request)
{
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty()) {
        return false;
    }
    // La durée : data-duration de la racine du index.html (une composition d'un seul élément n'a pas d'enveloppe).
    double seconds = 3.;
    QFile index(request.projectDir + QStringLiteral("/index.html"));
    if (index.open(QIODevice::ReadOnly)) {
        const QRegularExpression duration(QStringLiteral("data-composition-id=\"[^\"]*\"[^>]*data-duration=\"([0-9.]+)\""));
        const QRegularExpressionMatch m = duration.match(QString::fromUtf8(index.readAll()));
        if (m.hasMatch()) {
            seconds = m.captured(1).toDouble();
        }
    }
    QDir().mkpath(QFileInfo(request.output).absolutePath());
    QProcess p;
    p.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                     QStringLiteral("color=c=red@0.5:s=64x64:r=%1:d=%2").arg(request.fps).arg(seconds + 0.2), QStringLiteral("-c:v"), QStringLiteral("prores_ks"),
                     QStringLiteral("-profile:v"), QStringLiteral("4444"), QStringLiteral("-pix_fmt"), QStringLiteral("yuva444p10le"), request.output});
    return p.waitForFinished(30000) && p.exitCode() == 0;
}

} // namespace

TEST_CASE("Motion, lot 5 : la composition d'un seul élément (analyse et catalogue)", "[KoraCutAnimation]")
{
    using namespace KoraCutMotion;
    KoraCutMotionCatalog::Spec spec;
    spec.id = QStringLiteral("titre-essai");
    spec.element = QStringLiteral("titre");
    spec.text = QStringLiteral("Big Buck Bunny");
    spec.duration = 3.;
    spec.start = 2.;
    QString why;

    SECTION("Chaque sorte et chaque style donnent une composition d'un seul élément, placée et nommée")
    {
        for (const QString &kind : KoraCutMotionCatalog::elementKinds()) {
            for (const QString &style : KoraCutMotionCatalog::elementStyles(kind)) {
                KoraCutMotionCatalog::Spec s = spec;
                s.element = kind;
                s.style = style;
                s.text = kind == QLatin1String("bandeau") ? QStringLiteral("Awa Ndiaye | directrice") : (kind == QLatin1String("compteur") ? QStringLiteral("1250 clients") : spec.text);
                s.file = QStringLiteral("assets/logo.png");
                const QString html = KoraCutMotionCatalog::elementHtml(s, &why);
                INFO(kind.toStdString() + " " + style.toStdString() + " " + why.toStdString());
                REQUIRE_FALSE(html.isEmpty());
                CHECK_FALSE(html.contains(QLatin1String("{{")));
                const Composition comp = analyse(html);
                CHECK(comp.errors.isEmpty());
                REQUIRE(comp.elements.size() == 1);
                CHECK(comp.elements.first().kind == Kind::Whole);
                CHECK(comp.isElement());
                CHECK(comp.elementKind == kind);
                CHECK(comp.style == style);
                CHECK(comp.elements.first().start == Approx(2.));
                CHECK(comp.elements.first().duration == Approx(3.));
                if (kind == QLatin1String("bandeau")) {
                    CHECK(comp.elements.first().text == QStringLiteral("Awa Ndiaye"));
                } else if (kind != QLatin1String("logo")) {
                    CHECK(comp.elements.first().text == s.text);
                }
                CHECK(html.contains(QLatin1String("window.__timelines[\"titre-essai\"]")));
                CHECK(html.contains(QLatin1String("assets/gsap.min.js")));
            }
        }
        CHECK(KoraCutMotionCatalog::elementHtml([&]() { auto s = spec; s.element = QStringLiteral("rien"); return s; }(), &why).isEmpty());
        CHECK(KoraCutMotionCatalog::elementHtml([&]() { auto s = spec; s.text.clear(); return s; }(), &why).isEmpty());
        CHECK(KoraCutMotionCatalog::elementHtml([&]() { auto s = spec; s.style = QStringLiteral("rien"); return s; }(), &why).isEmpty());
    }

    SECTION("La place, la coupe et le retrait ne changent pas l'empreinte ; la durée et le texte oui")
    {
        const QString html = KoraCutMotionCatalog::elementHtml(spec, &why);
        const Composition comp = analyse(html);
        const Element &e = comp.elements.first();
        const QList<Tag> tags = tokenize(html);
        QString moved = setAttribute(html, tags.at(e.tagIndex), QStringLiteral("data-koracut-start"), QStringLiteral("7.5"));
        moved = setAttribute(moved, tokenize(moved).at(e.tagIndex), QStringLiteral("data-koracut-retire"), QStringLiteral("1"));
        const Composition movedComp = analyse(moved);
        CHECK(movedComp.elements.first().start == Approx(7.5));
        CHECK(movedComp.retired);
        CHECK(movedComp.elements.first().hash == e.hash);
        const QString longer = setAttribute(html, tags.at(e.tagIndex), QStringLiteral("data-duration"), QStringLiteral("4"));
        CHECK(analyse(longer).elements.first().hash != e.hash);
        const QString renamed = setText(html, e, QStringLiteral("Sintel"));
        CHECK(analyse(renamed).elements.first().text == QStringLiteral("Sintel"));
        CHECK(analyse(renamed).elements.first().hash != e.hash);
        CHECK(KoraCutMotionCatalog::readableName(analyse(renamed), QStringLiteral("titre-essai")) == QStringLiteral("Title · Sintel"));
        // Dans le chutier : un élément de sorte générique sans texte porte le nom de sa composition, pas son style
        // (quarante médias écrits par un agent s'appelaient tous « agent · 16:9 », 6 octobre 2026).
        {
            Composition generic;
            generic.elementKind = QStringLiteral("element");
            generic.style = QStringLiteral("agent");
            generic.width = 1920;
            generic.height = 1080;
            CHECK(KoraCutMotionCatalog::binClipName(generic, QStringLiteral("scene-03")) == QStringLiteral("scene-03 · 16:9"));
            CHECK(KoraCutMotionCatalog::binClipName(generic, QString()) == QStringLiteral("agent · 16:9"));
        }
    }

    SECTION("Les transitions du catalogue : une composition, sa coupe, son nom")
    {
        for (const QString &style : KoraCutMotionCatalog::transitionStyles()) {
            KoraCutMotionCatalog::Spec s = spec;
            s.style = style;
            s.cut = 2.45;
            s.duration = KoraCutMotionCatalog::transitionDefaultDuration(style);
            const QString html = KoraCutMotionCatalog::transitionHtml(s, &why);
            INFO(style.toStdString());
            REQUIRE_FALSE(html.isEmpty());
            CHECK_FALSE(html.contains(QLatin1String("{{")));
            const Composition comp = analyse(html);
            CHECK(comp.isTransition());
            CHECK(comp.cut == Approx(2.45));
            REQUIRE(comp.elements.size() == 1);
            CHECK(KoraCutMotionCatalog::readableName(comp, QStringLiteral("x")) == QStringLiteral("Transition · %1").arg(style));
        }
        CHECK(KoraCutMotionCatalog::transitionHtml([&]() { auto s = spec; s.style = QStringLiteral("rien"); return s; }(), &why).isEmpty());
    }

    SECTION("Un fragment d'agent est enveloppé ; deux éléments temporels sont refusés")
    {
        const QString fragment = QStringLiteral("<style>#t{color:#fff}</style><h1 id=\"t\" data-koracut-texte=\"1\">Coucou</h1><script>tl.fromTo(\"#t\",{opacity:0},{opacity:1,duration:0.4},0);</script>");
        const QString html = KoraCutMotionCatalog::wrapBrief(fragment, spec, &why);
        INFO(why.toStdString());
        REQUIRE_FALSE(html.isEmpty());
        const Composition comp = analyse(html);
        CHECK(comp.elements.first().text == QStringLiteral("Coucou"));
        CHECK(html.contains(QLatin1String("tl.fromTo(\"#t\"")));
        CHECK(html.indexOf(QLatin1String("#t{color:#fff}")) < html.indexOf(QLatin1String("</head>")));
        CHECK(KoraCutMotionCatalog::wrapBrief(QStringLiteral("<div id=\"a\" data-start=\"0\" data-duration=\"1\">a</div><div id=\"b\" data-start=\"1\" data-duration=\"1\">b</div>"), spec, &why).isEmpty());
        CHECK(KoraCutMotionCatalog::wrapBrief(QStringLiteral("<script>const tl2 = gsap.timeline({paused:true});</script>"), spec, &why).isEmpty());
        // Une composition complète garde son contenu, sa racine reçoit la place et la durée.
        const QString full = KoraCutMotionCatalog::elementHtml(spec, &why);
        KoraCutMotionCatalog::Spec other = spec;
        other.start = 9.;
        other.duration = 5.;
        const Composition wrapped = analyse(KoraCutMotionCatalog::wrapBrief(full, other, &why));
        CHECK(wrapped.elements.first().start == Approx(9.));
        CHECK(wrapped.elements.first().duration == Approx(5.));
    }
}

TEST_CASE("Motion, lot 5 : éléments seuls, transitions qui suivent la coupe, gestes de l'application", "[KoraCutAnimation]")
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
    document.setUrl(QUrl::fromLocalFile(tmp.path() + QStringLiteral("/essai.koracut")));
    {
        QFile marker(tmp.path() + QStringLiteral("/essai.koracut"));
        REQUIRE(marker.open(QIODevice::WriteOnly));
    }
    const bool haveFfmpeg = !ffmpegPath().isEmpty();
    KoraCutMotionRenderer::setSimulator([](const KoraCutMotionRenderRequest &r) { return fakeRender(r); });
    KoraCutMotionMirror mirror;
    mirror.setDocument(&document, timeline);
    REQUIRE(KoraCutAgentTools::implementedTools().contains(QStringLiteral("motion_element_seul")));
    REQUIRE(KoraCutAgentTools::implementedTools().contains(QStringLiteral("transition_app")));

    // Trois plans collés sur V1 (des couleurs sans limite de durée : de la réserve pour les fondus enchaînés).
    const int v1 = timeline->getTracksIds(false).first();
    QList<int> shots;
    for (const char *color : {"#406080", "#804060", "#608040"}) {
        const QString media = KdenliveTests::createProducer(pCore->getProjectProfile(), color, binModel, 100, false);
        const QJsonObject r = call(timeline, QStringLiteral("ajouter_clip"),
                                   {{QStringLiteral("media_id"), media}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 100 * int(shots.size())},
                                    {QStringLiteral("entree"), 0}, {QStringLiteral("sortie"), 100}, {QStringLiteral("partie"), QStringLiteral("video")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        shots << r.value(QStringLiteral("clip_id")).toInt();
    }

    SECTION("Les gestes de l'application : entrée, sortie, une étape chacun")
    {
        const int before = undoStack->index();
        QJsonObject r = call(timeline, QStringLiteral("animer_entree"), {{QStringLiteral("clip_id"), shots.at(0)}, {QStringLiteral("style"), QStringLiteral("zoom_avant")}, {QStringLiteral("duree"), 12}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == before + 1);
        QJsonObject state = call(timeline, QStringLiteral("etat_timeline"), {{QStringLiteral("niveau"), QStringLiteral("complet")}});
        const QString text = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
        CHECK(text.contains(QLatin1String("qtblend")));
        // Aucune image clé sans valeur (« 0= ») : le plan garde sa place avant le geste.
        CHECK_FALSE(text.contains(QLatin1String("=;")));
        r = call(timeline, QStringLiteral("animer_sortie"), {{QStringLiteral("clip_id"), shots.at(0)}, {QStringLiteral("style"), QStringLiteral("glisse_gauche")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == before + 2);
        CHECK(errorCode(call(timeline, QStringLiteral("animer_entree"), {{QStringLiteral("clip_id"), shots.at(0)}, {QStringLiteral("style"), QStringLiteral("rien")}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("animer_entree"), {{QStringLiteral("clip_id"), shots.at(0)}, {QStringLiteral("duree"), 500}})) == QStringLiteral("entree_invalide"));
        CHECK(undoStack->index() == before + 2);
        undoStack->undo();
        undoStack->undo();
        CHECK(undoStack->index() == before);
        state = call(timeline, QStringLiteral("etat_timeline"), {{QStringLiteral("niveau"), QStringLiteral("complet")}});
        CHECK_FALSE(QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)).contains(QLatin1String("qtblend")));
    }

    SECTION("Les transitions de l'application : sans réserve (images clés) et fondu enchaîné, une étape")
    {
        const int before = undoStack->index();
        QJsonObject r = call(timeline, QStringLiteral("transition_app"), {{QStringLiteral("clip_avant"), shots.at(0)}, {QStringLiteral("style"), QStringLiteral("zoom")}, {QStringLiteral("duree"), 12}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("clip_apres")).toInt() == shots.at(1));
        CHECK(r.value(QStringLiteral("coupe")).toObject().value(QStringLiteral("i")).toInt() == 100);
        CHECK(undoStack->index() == before + 1);
        r = call(timeline, QStringLiteral("transition_app"), {{QStringLiteral("clip_avant"), shots.at(1)}, {QStringLiteral("style"), QStringLiteral("fondu_enchaine")}, {QStringLiteral("duree"), 10}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(timeline->getMixDuration(r.value(QStringLiteral("clip_droit_id")).toInt()) == 10);
        CHECK(undoStack->index() == before + 2);
        CHECK(errorCode(call(timeline, QStringLiteral("transition_app"), {{QStringLiteral("clip_avant"), shots.at(2)}})) == QStringLiteral("pas_de_coupe"));
        undoStack->undo();
        undoStack->undo();
        CHECK(undoStack->index() == before);
    }

    SECTION("animer_texte : la machine à écrire sur un titre de KoraCut")
    {
        int v2 = -1;
        REQUIRE(timeline->requestTrackInsertion(-1, v2, QStringLiteral("Titres"), false));
        QJsonObject r = call(timeline, QStringLiteral("ajouter_titre"),
                             {{QStringLiteral("texte"), QStringLiteral("Sintel")}, {QStringLiteral("piste_id"), v2}, {QStringLiteral("position"), 110}, {QStringLiteral("duree"), 50}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int title = r.value(QStringLiteral("clip_id")).toInt();
        const int before = undoStack->index();
        r = call(timeline, QStringLiteral("animer_texte"), {{QStringLiteral("clip_id"), title}, {QStringLiteral("duree"), 18}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("pas_images")).toInt() == 3);
        auto bin = binModel->getClipByBinID(timeline->getClipBinId(title));
        CHECK(bin->getProducerProperty(QStringLiteral("xmldata")).contains(QLatin1String("typewriter=\"1;3;1;0;0\"")));
        CHECK(undoStack->index() == before + 1);
        undoStack->undo();
        CHECK_FALSE(bin->getProducerProperty(QStringLiteral("xmldata")).contains(QLatin1String("typewriter=\"1;")));
        CHECK(errorCode(call(timeline, QStringLiteral("animer_texte"), {{QStringLiteral("clip_id"), shots.at(0)}})) == QStringLiteral("entree_invalide"));
        waitMs(500);
    }

    SECTION("Le clic droit sur un clip : Animer et Transition vers le plan suivant, les mêmes outils")
    {
        QWidget host;
        KoraCutAnimateMenu menu(&host);
        menu.prepare(timeline, shots.at(0));
        CHECK(menu.animateMenu()->menuAction()->isVisible());
        CHECK(menu.transitionMenu()->isEnabled());
        REQUIRE(menu.find(QStringLiteral("animer_texte"), QStringLiteral("machine_a_ecrire")));
        CHECK_FALSE(menu.find(QStringLiteral("animer_texte"), QStringLiteral("machine_a_ecrire"))->isVisible());
        for (const QString &style : {QStringLiteral("cordes"), QStringLiteral("balayage"), QStringLiteral("voile"), QStringLiteral("eclair")}) {
            CHECK(menu.find(QStringLiteral("motion_transition"), style));
        }
        // Le dernier plan n'a pas de plan suivant : le sous-menu des transitions est grisé.
        menu.prepare(timeline, shots.at(2));
        CHECK_FALSE(menu.transitionMenu()->isEnabled());
        // Un choix = l'outil des agents = une étape d'annulation.
        int before = undoStack->index();
        QJsonObject r = menu.apply(menu.find(QStringLiteral("animer_entree"), QStringLiteral("rebond")));
        INFO(dump(r));
        CHECK(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("clip_id")).toInt() == shots.at(2));
        CHECK(undoStack->index() == before + 1);
        r = menu.apply(menu.find(QStringLiteral("animer_sortie"), QStringLiteral("fondu")));
        CHECK(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == before + 2);
        menu.prepare(timeline, shots.at(0));
        before = undoStack->index();
        r = menu.apply(menu.find(QStringLiteral("transition_app"), QStringLiteral("zoom")));
        INFO(dump(r));
        CHECK(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("clip_apres")).toInt() == shots.at(1));
        CHECK(undoStack->index() == before + 1);
        waitMs(300);
    }

    if (haveFfmpeg) {
        SECTION("Un élément seul, une transition qui suit sa coupe, retrait et retour, ouverture")
        {
            // Le titre, posé seul sur « Animations ».
            int before = undoStack->index();
            QJsonObject r = call(timeline, QStringLiteral("motion_element_seul"),
                                 {{QStringLiteral("nom"), QStringLiteral("titre-bunny")}, {QStringLiteral("sorte"), QStringLiteral("titre")},
                                  {QStringLiteral("texte"), QStringLiteral("Big Buck Bunny")}, {QStringLiteral("position"), 12}, {QStringLiteral("duree"), 50}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(r.contains(QStringLiteral("clip_id")));
            const int titleClip = r.value(QStringLiteral("clip_id")).toInt();
            CHECK(r.value(QStringLiteral("affichage")).toString() == QStringLiteral("Title · Big Buck Bunny"));
            CHECK(r.value(QStringLiteral("position")).toObject().value(QStringLiteral("i")).toInt() == 12);
            CHECK(r.value(QStringLiteral("duree")).toObject().value(QStringLiteral("i")).toInt() == 50);
            const int animations = r.value(QStringLiteral("piste_id")).toInt();
            CHECK(timeline->getTrackProperty(animations, QStringLiteral("kdenlive:koracut_motion")).toString() == QStringLiteral("*animations"));
            CHECK(undoStack->index() == before + 1);
            const QString titleIndex = r.value(QStringLiteral("fichier")).toString();
            CHECK(readAll(titleIndex).contains(QStringLiteral("data-koracut-start=\"%1\"").arg(KoraCutMotion::secondsText(12 / fps))));

            // Déplacer le clip réécrit sa place sans nouveau rendu.
            REQUIRE(timeline->requestClipMove(titleClip, animations, 25, true, true, true));
            REQUIRE(waitFor([&]() { return readAll(titleIndex).contains(QStringLiteral("data-koracut-start=\"%1\"").arg(KoraCutMotion::secondsText(25 / fps))); }, 4000));
            CHECK_FALSE(mirror.renderer()->isPending(QStringLiteral("titre-bunny"), QStringLiteral("titre-bunny")));

            // Le texte change par motion_element : le fichier suit.
            r = call(timeline, QStringLiteral("motion_element"),
                     {{QStringLiteral("nom"), QStringLiteral("titre-bunny")}, {QStringLiteral("id"), QStringLiteral("titre-bunny")}, {QStringLiteral("texte"), QStringLiteral("Sintel")}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(readAll(titleIndex).contains(QLatin1String(">Sintel<")));
            CHECK(r.value(QStringLiteral("rendu_relance")).toBool());
            mirror.renderer()->waitForIdle(20000);
            waitMs(900);

            // La transition entre le premier et le deuxième plan, centrée sur la coupe (100).
            before = undoStack->index();
            r = call(timeline, QStringLiteral("motion_transition"), {{QStringLiteral("clip_avant"), shots.at(0)}, {QStringLiteral("style"), QStringLiteral("cordes")}, {QStringLiteral("duree"), 20}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(r.contains(QStringLiteral("clip_id")));
            const QString transition = r.value(QStringLiteral("nom")).toString();
            CHECK(transition == QStringLiteral("transition-cordes"));
            const int transitionClip = r.value(QStringLiteral("clip_id")).toInt();
            const int transitions = r.value(QStringLiteral("piste_id")).toInt();
            CHECK(timeline->getClipPosition(transitionClip) == 90);
            CHECK(timeline->getClipPlaytime(transitionClip) == 20);
            CHECK(timeline->getTrackProperty(transitions, QStringLiteral("kdenlive:koracut_motion")).toString() == QStringLiteral("*transitions"));
            CHECK(timeline->getTrackPosition(transitions) > timeline->getTrackPosition(animations));
            CHECK(r.value(QStringLiteral("piste_dessous_id")).toInt() == animations);
            CHECK(undoStack->index() == before + 1);

            // Une légende posée sous la transition (sur « Animations », son clip croise celui de la transition).
            r = call(timeline, QStringLiteral("motion_element_seul"),
                     {{QStringLiteral("nom"), QStringLiteral("legende-coupe")}, {QStringLiteral("sorte"), QStringLiteral("legende")},
                      {QStringLiteral("texte"), QStringLiteral("Sintel, au couchant")}, {QStringLiteral("position"), 94}, {QStringLiteral("duree"), 40}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(r.contains(QStringLiteral("clip_id")));
            const int legendClip = r.value(QStringLiteral("clip_id")).toInt();
            CHECK(timeline->getClipTrackId(legendClip) == animations);
            CHECK(r.value(QStringLiteral("suit_transition")).toString() == transition);
            CHECK(mirror.followedTransition(QStringLiteral("titre-bunny")).isEmpty());
            const QString legendIndex = mirror.projectState(QStringLiteral("legende-coupe")).indexPath;
            mirror.renderer()->waitForIdle(20000);
            waitMs(900);

            // La coupe bouge (le premier plan raccourci de 10, le deuxième avancé de 10) : la transition suit,
            // la légende aussi, du même écart ; le titre, loin d'elle, ne bouge pas.
            const int beforeMove = undoStack->index();
            const int titleBefore = timeline->getClipPosition(mirror.clipFor(QStringLiteral("titre-bunny"), QStringLiteral("titre-bunny")));
            REQUIRE(timeline->requestItemResize(shots.at(0), 90, true, true) == 90);
            REQUIRE(timeline->requestClipMove(shots.at(1), v1, 90, true, true, true));
            REQUIRE(waitFor([&]() { return timeline->getClipPosition(transitionClip) == 80; }, 4000));
            REQUIRE(waitFor([&]() { return timeline->getClipPosition(legendClip) == 84; }, 4000));
            CHECK(timeline->getClipPosition(mirror.clipFor(QStringLiteral("titre-bunny"), QStringLiteral("titre-bunny"))) == titleBefore);
            const QString transitionIndex = mirror.projectState(transition).indexPath;
            CHECK(readAll(transitionIndex).contains(QStringLiteral("data-koracut-cut=\"%1\"").arg(KoraCutMotion::secondsText(90 / fps))));
            REQUIRE(waitFor([&]() { return readAll(legendIndex).contains(QStringLiteral("data-koracut-start=\"%1\"").arg(KoraCutMotion::secondsText(84 / fps))); }, 4000));
            // Ni étape d'annulation à elles, ni nouveau rendu (la durée n'a pas changé).
            waitMs(600);
            CHECK(undoStack->index() == beforeMove + 2);
            CHECK_FALSE(mirror.renderer()->isPending(QStringLiteral("legende-coupe"), QStringLiteral("legende-coupe")));
            CHECK(timeline->getClipPlaytime(legendClip) == 40);
            // Annuler les deux gestes : la transition revient sur la coupe d'origine, la légende sous elle.
            while (undoStack->index() > beforeMove) {
                undoStack->undo();
            }
            REQUIRE(waitFor([&]() { return timeline->getClipPosition(transitionClip) == 90; }, 4000));
            REQUIRE(waitFor([&]() { return timeline->getClipPosition(legendClip) == 94; }, 4000));
            CHECK(mirror.followedTransition(QStringLiteral("legende-coupe")) == transition);
            CHECK(readAll(transitionIndex).contains(QStringLiteral("data-koracut-cut=\"%1\"").arg(KoraCutMotion::secondsText(100 / fps))));

            // Glissée à la main près de l'autre coupe (200), elle s'y accroche.
            REQUIRE(timeline->requestClipMove(transitionClip, transitions, 185, true, true, true));
            REQUIRE(waitFor([&]() { return timeline->getClipPosition(transitionClip) == 190; }, 4000));
            CHECK(readAll(transitionIndex).contains(QStringLiteral("data-koracut-cut=\"%1\"").arg(KoraCutMotion::secondsText(200 / fps))));
            undoStack->undo();
            REQUIRE(waitFor([&]() { return timeline->getClipPosition(transitionClip) == 90; }, 4000));

            // Ouvrir l'élément : lui seul.
            r = call(timeline, QStringLiteral("motion_ouvrir"), {{QStringLiteral("clip_id"), transitionClip}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("nom")).toString() == transition);
            CHECK(mirror.lastOpenedProject() == transition);
            CHECK(r.value(QStringLiteral("dossier_studio")).toString().endsWith(QLatin1String("/motion/transition-cordes")));

            // Supprimer le clip du titre le marque retiré ; Annuler le remet.
            const int titleNow = mirror.clipFor(QStringLiteral("titre-bunny"), QStringLiteral("titre-bunny"));
            REQUIRE(titleNow >= 0);
            REQUIRE(timeline->requestItemDeletion(titleNow, true));
            REQUIRE(waitFor([&]() { return readAll(titleIndex).contains(QLatin1String("data-koracut-retire=\"1\"")); }, 4000));
            CHECK(mirror.projectState(QStringLiteral("titre-bunny")).retired);
            undoStack->undo();
            REQUIRE(waitFor([&]() { return !readAll(titleIndex).contains(QLatin1String("data-koracut-retire")); }, 4000));
            CHECK(mirror.clipFor(QStringLiteral("titre-bunny"), QStringLiteral("titre-bunny")) >= 0);

            // Les erreurs.
            CHECK(errorCode(call(timeline, QStringLiteral("motion_element_seul"), {{QStringLiteral("nom"), QStringLiteral("titre-bunny")}, {QStringLiteral("position"), 0}, {QStringLiteral("duree"), 25}})) ==
                  QStringLiteral("existe_deja"));
            CHECK(errorCode(call(timeline, QStringLiteral("motion_element_seul"),
                                 {{QStringLiteral("nom"), QStringLiteral("x")}, {QStringLiteral("sorte"), QStringLiteral("autre")}, {QStringLiteral("position"), 0}, {QStringLiteral("duree"), 25}})) ==
                  QStringLiteral("entree_invalide"));
            CHECK(errorCode(call(timeline, QStringLiteral("motion_transition"), {{QStringLiteral("clip_avant"), shots.at(2)}})) == QStringLiteral("pas_de_coupe"));
            CHECK(errorCode(call(timeline, QStringLiteral("motion_ouvrir"), {{QStringLiteral("nom"), QStringLiteral("absent")}})) == QStringLiteral("projet_inconnu"));
        }
    } else {
        WARN("ffmpeg introuvable : les éléments rendus ne sont pas vérifiés.");
    }

    mirror.renderer()->waitForIdle(20000);
    waitMs(800);
    // Les rendus simulés vont dans le cache du document : on ne laisse rien derrière soi.
    for (const QString &name : mirror.projects()) {
        QDir(KoraCutMotionProject::cacheDir(&document, name)).removeRecursively();
    }
    KoraCutMotionRenderer::setSimulator({});
    mirror.clear();
    KoraCutAgentTools::setMotionMirror(nullptr);
    pCore->projectManager()->closeCurrentDocument(false, false);
}
