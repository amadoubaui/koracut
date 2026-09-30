/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "koracut/motion/koracutmotionproject.h"
#include "koracut/motion/koracutmotionsync.h"

#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryDir>

using namespace KoraCutMotion;

namespace {

QString readAll(const QString &path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    return QString::fromUtf8(file.readAll());
}

void writeAll(const QString &path, const QString &content)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    file.write(content.toUtf8());
}

const QString datasetIndex = sourcesPath + QStringLiteral("/dataset/koracut-motion/index.html");

/** Laisse tourner la boucle d'événements (la surveillance de fichier a besoin d'elle). */
void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/** Compte les changedOutside d'un synchroniseur. */
struct Counter
{
    explicit Counter(KoraCutMotionSync &sync)
    {
        QObject::connect(&sync, &KoraCutMotionSync::changedOutside, [this]() { ++count; });
    }
    int count = 0;
    bool wait(int ms)
    {
        for (int i = 0; i < ms / 50 && count == 0; ++i) {
            waitMs(50);
        }
        return count > 0;
    }
};

// Le cas StockFinity : une seule timeline GSAP, pas d'élément .clip.
const QString wholeHtml = QStringLiteral(R"(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<link rel="stylesheet" href="shared/stockfinity.css">
<script src="assets/gsap.min.js"></script>
<style>#mot { font-size: 168px; }</style>
</head>
<body>
<div id="root" data-composition-id="main" data-start="0" data-duration="3.4" data-fps="50" data-width="1080" data-height="1920">
  <div id="bloc"><div class="mot" id="mot">Aucun</div></div>
</div>
<script>
  window.__timelines = window.__timelines || {};
  const tl = gsap.timeline({ paused: true });
  window.__timelines["main"] = tl;
</script>
</body>
</html>
)");

} // namespace

TEST_CASE("Motion : lecture d'une composition à trois éléments", "[KoraCutMotion]")
{
    const QString html = readAll(datasetIndex);
    const Composition comp = analyse(html);
    REQUIRE(comp.hasRoot);
    CHECK(comp.id == QStringLiteral("essai"));
    CHECK(comp.width == 1080);
    CHECK(comp.height == 1920);
    CHECK(comp.fps == 25.);
    CHECK(comp.duration == 6.);
    CHECK(comp.errors.isEmpty());
    REQUIRE(comp.elements.size() == 3);

    const Element &titre = comp.elements.at(0);
    CHECK(titre.id == QStringLiteral("titre-1"));
    CHECK(titre.kind == Kind::Block);
    CHECK(titre.start == 0.5);
    CHECK(titre.duration == 3.);
    CHECK(titre.trackIndex == 1);
    CHECK(titre.text == QStringLiteral("Bonjour KoraCut"));
    CHECK(titre.displayName() == QStringLiteral("Bonjour KoraCut"));

    const Element &logo = comp.elements.at(1);
    CHECK(logo.kind == Kind::Image);
    CHECK(logo.src == QStringLiteral("assets/logo.png"));
    // « titre-1 + 0.5 » : la fin du titre (0.5 + 3) plus une demi-seconde.
    CHECK(logo.startExpr == QStringLiteral("titre-1 + 0.5"));
    CHECK(logo.start == 4.);
    CHECK(logo.duration == 2.);
    CHECK(logo.trackIndex == 2);
    CHECK(logo.displayName() == QStringLiteral("logo.png"));

    const Element &son = comp.elements.at(2);
    CHECK(son.kind == Kind::Audio);
    CHECK(son.start == 1.);
    CHECK(son.duration == 2.);
    CHECK(son.mediaStart == 0.5);
    CHECK(comp.end() == 6.);
    CHECK(comp.element(QStringLiteral("logo")) == &logo);
    CHECK(comp.element(QStringLiteral("absent")) == nullptr);
}

TEST_CASE("Motion : expressions relatives", "[KoraCutMotion]")
{
    const Composition comp = analyse(readAll(datasetIndex));
    double seconds = -1.;
    SECTION("Un nombre, un id seul, plus et moins")
    {
        CHECK(resolveStart(QStringLiteral(" 1.25 "), comp, seconds));
        CHECK(seconds == 1.25);
        CHECK(resolveStart(QStringLiteral("titre-1"), comp, seconds));
        CHECK(seconds == 3.5);
        CHECK(resolveStart(QStringLiteral("titre-1 - 0.25"), comp, seconds));
        CHECK(seconds == 3.25);
        CHECK(resolveStart(QStringLiteral("logo+1"), comp, seconds));
        CHECK(seconds == 7.);
    }
    SECTION("Un id inconnu ou une expression étrange ne se résout pas")
    {
        CHECK_FALSE(resolveStart(QStringLiteral("nulle-part + 1"), comp, seconds));
        CHECK_FALSE(resolveStart(QStringLiteral("1 + titre-1"), comp, seconds));
        CHECK_FALSE(resolveStart(QString(), comp, seconds));
    }
    SECTION("Une boucle est refusée et signalée")
    {
        const QString html = QStringLiteral("<div id=\"root\" data-composition-id=\"c\" data-start=\"0\" data-duration=\"5\" data-width=\"10\" data-height=\"10\">"
                                            "<div id=\"a\" class=\"clip\" data-start=\"b\" data-duration=\"1\"></div>"
                                            "<div id=\"b\" class=\"clip\" data-start=\"a + 1\" data-duration=\"1\"></div>"
                                            "<div id=\"c1\" class=\"clip\" data-start=\"0\" data-duration=\"1\"></div></div>");
        const Composition loop = analyse(html);
        REQUIRE(loop.elements.size() == 3);
        CHECK_FALSE(loop.elements.at(0).startResolved);
        CHECK_FALSE(loop.elements.at(1).startResolved);
        CHECK(loop.elements.at(2).startResolved);
        CHECK(loop.errors.size() == 2);
    }
    SECTION("Une image sans durée dure 3 secondes, un bloc sans durée est signalé")
    {
        const QString html = QStringLiteral("<div id=\"root\" data-composition-id=\"c\" data-start=\"0\" data-width=\"10\" data-height=\"10\">"
                                            "<img id=\"i\" src=\"a.png\" data-start=\"1\">"
                                            "<section id=\"s\" class=\"clip\" data-start=\"i\"></section></div>");
        const Composition c = analyse(html);
        REQUIRE(c.elements.size() == 2);
        CHECK(c.elements.at(0).duration == 3.);
        CHECK(c.elements.at(0).durationKnown);
        CHECK(c.elements.at(1).start == 4.);
        CHECK_FALSE(c.elements.at(1).durationKnown);
        CHECK(c.errors.size() == 1);
    }
}

TEST_CASE("Motion : écriture minimale", "[KoraCutMotion]")
{
    const QString html = readAll(datasetIndex);
    const Composition comp = analyse(html);
    const QList<Tag> tags = tokenize(html);
    const Element &titre = comp.elements.at(0);
    SECTION("Changer la valeur d'un attribut ne touche à rien d'autre")
    {
        const QString after = setAttribute(html, tags.at(titre.tagIndex), QStringLiteral("data-start"), QStringLiteral("1.2"));
        QString expected = html;
        expected.replace(QStringLiteral("data-start=\"0.5\" data-duration=\"3\""), QStringLiteral("data-start=\"1.2\" data-duration=\"3\""));
        CHECK(after == expected);
        CHECK(after.size() == html.size());
        // Le logo, relatif au titre, suit sans qu'on l'écrive.
        CHECK(analyse(after).elements.at(1).start == 4.7);
    }
    SECTION("Ajouter puis retirer un attribut")
    {
        const QString added = setAttribute(html, tags.at(titre.tagIndex), QStringLiteral("data-volume"), QStringLiteral("0.5"));
        CHECK(added.contains(QStringLiteral("data-track-index=\"1\" data-volume=\"0.5\">")));
        CHECK(added.size() == html.size() + QStringLiteral(" data-volume=\"0.5\"").size());
        const QList<Tag> tags2 = tokenize(added);
        const QString removed = setAttribute(added, tags2.at(titre.tagIndex), QStringLiteral("data-volume"), QString());
        CHECK(removed == html);
    }
    SECTION("Un attribut sur une balise auto-fermée")
    {
        const QString html2 = QStringLiteral("<div id=\"root\" data-composition-id=\"c\" data-start=\"0\" data-width=\"10\" data-height=\"10\">"
                                             "<img id=\"i\" src=\"a.png\" data-start='1' /></div>");
        const Composition c = analyse(html2);
        const QList<Tag> t = tokenize(html2);
        const QString after = setAttribute(html2, t.at(c.elements.at(0).tagIndex), QStringLiteral("data-duration"), QStringLiteral("2"));
        CHECK(after.contains(QStringLiteral("data-start='1' data-duration=\"2\" />")));
    }
    SECTION("Changer le texte d'un bloc garde ses balises et sa mise en forme")
    {
        const QString after = setText(html, titre, QStringLiteral("Salut & bienvenue"));
        QString expected = html;
        expected.replace(QStringLiteral("<h1 id=\"titre\">Bonjour KoraCut</h1>"), QStringLiteral("<h1 id=\"titre\">Salut &amp; bienvenue</h1>"));
        CHECK(after == expected);
        CHECK(analyse(after).elements.at(0).text == QStringLiteral("Salut & bienvenue"));
    }
    SECTION("Un bloc à plusieurs nœuds de texte : le premier prend le texte, les autres se vident")
    {
        const QString html2 = QStringLiteral("<div id=\"root\" data-composition-id=\"c\" data-start=\"0\" data-duration=\"1\" data-width=\"1\" data-height=\"1\">"
                                             "<section id=\"s\" class=\"clip\" data-start=\"0\" data-duration=\"1\">\n  <b>Un</b> <i>deux</i>\n</section></div>");
        const Composition c = analyse(html2);
        CHECK(c.elements.at(0).text == QStringLiteral("Un deux"));
        const QString after = setText(html2, c.elements.at(0), QStringLiteral("Trois"));
        CHECK(after.contains(QStringLiteral("<b>Trois</b> <i></i>")));
        CHECK(analyse(after).elements.at(0).text == QStringLiteral("Trois"));
    }
    SECTION("Le texte visible replie les blancs et décode les entités")
    {
        CHECK(visibleText(QStringLiteral("  <b>Tom</b> &amp; <i>J&eacute;rry</i>\n <script>var x = '<'</script> ")) == QStringLiteral("Tom & J&eacute;rry"));
        CHECK(visibleText(QStringLiteral("a&#233;b &lt;c&gt; &#x41;")) == QStringLiteral("aéb <c> A"));
    }
    SECTION("Les secondes s'écrivent court")
    {
        CHECK(secondsText(1.) == QStringLiteral("1"));
        CHECK(secondsText(0.5) == QStringLiteral("0.5"));
        CHECK(secondsText(1.0 / 3.) == QStringLiteral("0.333"));
        CHECK(secondsText(2.10) == QStringLiteral("2.1"));
    }
}

TEST_CASE("Motion : empreintes stables", "[KoraCutMotion]")
{
    const QString html = readAll(datasetIndex);
    const Composition comp = analyse(html);
    const QList<Tag> tags = tokenize(html);
    const QString titreHash = comp.elements.at(0).hash;
    const QString logoHash = comp.elements.at(1).hash;
    REQUIRE(titreHash.size() == 12);
    CHECK(titreHash != logoHash);

    SECTION("Déplacer, rogner ou changer de piste ne change pas l'empreinte")
    {
        QString moved = setAttribute(html, tags.at(comp.elements.at(0).tagIndex), QStringLiteral("data-start"), QStringLiteral("2"));
        moved = setAttribute(moved, tokenize(moved).at(comp.elements.at(0).tagIndex), QStringLiteral("data-duration"), QStringLiteral("1.5"));
        moved = setAttribute(moved, tokenize(moved).at(comp.elements.at(0).tagIndex), QStringLiteral("data-track-index"), QStringLiteral("3"));
        const Composition c = analyse(moved);
        CHECK(c.elements.at(0).hash == titreHash);
        CHECK(c.elements.at(1).hash == logoHash);
    }
    SECTION("Changer le texte change l'empreinte de ce seul élément")
    {
        const Composition c = analyse(setText(html, comp.elements.at(0), QStringLiteral("Autre titre")));
        CHECK(c.elements.at(0).hash != titreHash);
        CHECK(c.elements.at(1).hash == logoHash);
    }
    SECTION("Changer un style ou le script de la page change toutes les empreintes")
    {
        QString styled = html;
        styled.replace(QStringLiteral("font: 700 120px"), QStringLiteral("font: 700 140px"));
        const Composition c = analyse(styled);
        CHECK(c.elements.at(0).hash != titreHash);
        CHECK(c.elements.at(1).hash != logoHash);
        QString scripted = html;
        scripted.replace(QStringLiteral("duration: 0.6"), QStringLiteral("duration: 0.9"));
        CHECK(analyse(scripted).elements.at(0).hash != titreHash);
    }
    SECTION("La même composition relue donne les mêmes empreintes")
    {
        CHECK(analyse(html).elements.at(0).hash == titreHash);
    }
}

TEST_CASE("Motion : composition sans clip", "[KoraCutMotion]")
{
    const Composition comp = analyse(wholeHtml);
    REQUIRE(comp.hasRoot);
    CHECK(comp.id == QStringLiteral("main"));
    CHECK(comp.fps == 50.);
    REQUIRE(comp.elements.size() == 1);
    const Element &whole = comp.elements.at(0);
    CHECK(whole.kind == Kind::Whole);
    CHECK(whole.id == QStringLiteral("main"));
    CHECK(whole.start == 0.);
    CHECK(whole.duration == 3.4);
    CHECK(whole.durationKnown);
    CHECK(comp.end() == 3.4);
    CHECK(comp.errors.isEmpty());
    // L'empreinte suit tout le fichier.
    QString other = wholeHtml;
    other.replace(QStringLiteral("Aucun"), QStringLiteral("Tout"));
    CHECK(analyse(other).elements.at(0).hash != whole.hash);
    // Sans racine : rien, et une erreur en clair.
    const Composition none = analyse(QStringLiteral("<html><body><p>rien</p></body></html>"));
    CHECK_FALSE(none.hasRoot);
    CHECK(none.elements.isEmpty());
    CHECK(none.errors.size() == 1);
}

TEST_CASE("Motion : le synchroniseur écrit, surveille et refuse d'écraser", "[KoraCutMotion]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString index = dir.path() + QStringLiteral("/index.html");
    writeAll(index, readAll(datasetIndex));
    KoraCutMotionSync sync(index);
    sync.setDebounce(50);
    REQUIRE(sync.composition().elements.size() == 3);

    SECTION("writeElement fait une seule écriture minimale et met la composition à jour")
    {
        const QString before = sync.html();
        const QString text = QStringLiteral("Nouveau texte");
        REQUIRE(sync.writeElement(QStringLiteral("titre-1"), {{QStringLiteral("data-start"), QStringLiteral("2")}, {QStringLiteral("data-duration"), QStringLiteral("1.5")}}, &text));
        const QString onDisk = readAll(index);
        CHECK(onDisk == sync.html());
        CHECK(sync.composition().element(QStringLiteral("titre-1"))->start == 2.);
        CHECK(sync.composition().element(QStringLiteral("titre-1"))->duration == 1.5);
        CHECK(sync.composition().element(QStringLiteral("titre-1"))->text == text);
        CHECK(sync.composition().element(QStringLiteral("logo"))->start == 4.);
        // Rien d'autre n'a bougé.
        QString expected = before;
        expected.replace(QStringLiteral("data-start=\"0.5\" data-duration=\"3\""), QStringLiteral("data-start=\"2\" data-duration=\"1.5\""));
        expected.replace(QStringLiteral(">Bonjour KoraCut<"), QStringLiteral(">Nouveau texte<"));
        CHECK(onDisk == expected);
        CHECK_FALSE(sync.changedOnDisk());
        CHECK_FALSE(sync.reload());
        // Sa propre écriture ne revient pas comme un changement extérieur.
        Counter spy(sync);
        waitMs(200);
        CHECK(spy.count == 0);
    }
    SECTION("Un élément inconnu n'est pas écrit")
    {
        CHECK_FALSE(sync.writeElement(QStringLiteral("absent"), {{QStringLiteral("data-start"), QStringLiteral("2")}}));
    }
    SECTION("Une écriture extérieure est relue et bloque l'écriture tant qu'elle n'est pas relue")
    {
        Counter spy(sync);
        QString external = readAll(index);
        external.replace(QStringLiteral("Bonjour KoraCut"), QStringLiteral("Écrit par un agent"));
        writeAll(index, external);
        CHECK(sync.changedOnDisk());
        // Refus : on n'écrase pas ce qu'un autre vient d'écrire.
        CHECK_FALSE(sync.writeElement(QStringLiteral("titre-1"), {{QStringLiteral("data-start"), QStringLiteral("3")}}));
        CHECK(readAll(index) == external);
        REQUIRE(spy.wait(2000));
        CHECK(spy.count == 1);
        CHECK(sync.composition().element(QStringLiteral("titre-1"))->text == QStringLiteral("Écrit par un agent"));
        CHECK_FALSE(sync.changedOnDisk());
        // Relue : l'écriture passe.
        CHECK(sync.writeElement(QStringLiteral("titre-1"), {{QStringLiteral("data-start"), QStringLiteral("3")}}));
        CHECK(readAll(index).contains(QStringLiteral("data-start=\"3\"")));
    }
    SECTION("writeAll (panneau Code) passe pour un changement extérieur")
    {
        Counter spy(sync);
        QString content = sync.html();
        content.replace(QStringLiteral("data-track-index=\"2\""), QStringLiteral("data-track-index=\"4\""));
        CHECK(sync.writeAll(content));
        CHECK(spy.count == 1);
        CHECK(sync.composition().element(QStringLiteral("logo"))->trackIndex == 4);
        CHECK(readAll(index) == content);
    }
}

TEST_CASE("Motion : projets, gabarit et import", "[KoraCutMotion]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/motion");
    CHECK(KoraCutMotionProject::sanitizeName(QStringLiteral(" Bandeau nom / fonction ")) == QStringLiteral("Bandeau-nom-fonction"));
    CHECK(KoraCutMotionProject::sanitizeName(QString()) == QStringLiteral("composition"));
    CHECK(KoraCutMotionProject::list(root).isEmpty());

    SECTION("Le gabarit vide est une composition valide aux dimensions demandées")
    {
        const QString html = KoraCutMotionProject::templateHtml(QStringLiteral("bandeau"), 1920, 1080, 25);
        const Composition c = analyse(html);
        CHECK(c.id == QStringLiteral("bandeau"));
        CHECK(c.width == 1920);
        CHECK(c.height == 1080);
        CHECK(c.fps == 25.);
        REQUIRE(c.elements.size() == 1);
        CHECK(c.elements.at(0).kind == Kind::Block);
        CHECK(c.elements.at(0).text == QStringLiteral("Nouveau titre"));
        CHECK(c.errors.isEmpty());
    }
    SECTION("Importer un .html seul copie ce qu'il cite, sans les « ../ »")
    {
        // Une composition à la StockFinity : dans compositions/, avec shared/ et assets/ à côté.
        const QString src = dir.path() + QStringLiteral("/source");
        QDir().mkpath(src + QStringLiteral("/compositions"));
        QDir().mkpath(src + QStringLiteral("/shared"));
        QDir().mkpath(src + QStringLiteral("/assets/fonts"));
        writeAll(src + QStringLiteral("/assets/gsap.min.js"), QStringLiteral("// gsap"));
        writeAll(src + QStringLiteral("/assets/fonts/Inter.woff2"), QStringLiteral("woff"));
        writeAll(src + QStringLiteral("/shared/style.css"), QStringLiteral("@font-face { src: url(\"../assets/fonts/Inter.woff2\"); }"));
        QString html = wholeHtml;
        html.replace(QStringLiteral("href=\"shared/stockfinity.css\""), QStringLiteral("href=\"../shared/style.css\""));
        html.replace(QStringLiteral("src=\"assets/gsap.min.js\""), QStringLiteral("src=\"../assets/gsap.min.js\""));
        writeAll(src + QStringLiteral("/compositions/01-accroche.html"), html);
        QString error;
        const QString index = KoraCutMotionProject::import(root, src + QStringLiteral("/compositions/01-accroche.html"), QStringLiteral("01 accroche"), &error);
        REQUIRE_FALSE(index.isEmpty());
        CHECK(index == root + QStringLiteral("/01-accroche/index.html"));
        const QString imported = readAll(index);
        CHECK(imported.contains(QStringLiteral("href=\"shared/style.css\"")));
        CHECK(imported.contains(QStringLiteral("src=\"assets/gsap.min.js\"")));
        CHECK(QFile::exists(root + QStringLiteral("/01-accroche/shared/style.css")));
        CHECK(QFile::exists(root + QStringLiteral("/01-accroche/assets/gsap.min.js")));
        CHECK(QFile::exists(root + QStringLiteral("/01-accroche/assets/fonts/Inter.woff2")));
        REQUIRE(KoraCutMotionProject::list(root).size() == 1);
        CHECK(KoraCutMotionProject::list(root).first().name == QStringLiteral("01-accroche"));
        // Deux fois le même nom : refusé, expliqué.
        CHECK(KoraCutMotionProject::import(root, src + QStringLiteral("/compositions/01-accroche.html"), QStringLiteral("01 accroche"), &error).isEmpty());
        CHECK_FALSE(error.isEmpty());
    }
    SECTION("Importer un dossier exige un index.html")
    {
        const QString folder = dir.path() + QStringLiteral("/dossier");
        QDir().mkpath(folder + QStringLiteral("/assets"));
        QString error;
        CHECK(KoraCutMotionProject::import(root, folder, QStringLiteral("d"), &error).isEmpty());
        CHECK(error.contains(QStringLiteral("index.html")));
        writeAll(folder + QStringLiteral("/index.html"), wholeHtml);
        writeAll(folder + QStringLiteral("/assets/a.txt"), QStringLiteral("a"));
        const QString index = KoraCutMotionProject::import(root, folder, QStringLiteral("d"), &error);
        CHECK(index == root + QStringLiteral("/d/index.html"));
        CHECK(QFile::exists(root + QStringLiteral("/d/assets/a.txt")));
    }
}
