/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutmotioncatalog.h"
#include "koracutmotionsync.h"

#include <KLocalizedString>

#include <QColor>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace KoraCutMotionCatalog {

namespace {

// Les gabarits portent des « {{CLEF}} » : un remplacement simple, pour que les « % » du CSS ne
// gênent pas (QString::arg les prendrait pour des numéros).
QString fill(QString text, const QMap<QString, QString> &values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        text.replace(QStringLiteral("{{") + it.key() + QStringLiteral("}}"), it.value());
    }
    return text;
}

QString px(double value)
{
    return QString::number(qRound(value));
}

QString rgba(const QString &hex, double alpha)
{
    const QColor c(hex);
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(QString::number(alpha, 'g', 3));
}

QString validColor(const QString &value, const QString &fallback)
{
    return QColor::isValidColorName(value) && value.startsWith(QLatin1Char('#')) ? value : fallback;
}

// La tête et la racine communes : fond transparent, police du système, GSAP embarqué dans assets/.
const char *kHead = R"KC(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width={{W}}, height={{H}}">
<script src="assets/gsap.min.js"></script>
<style>
  html, body { margin: 0; background: transparent; }
  #root { position: relative; width: 100%; height: 100%; overflow: hidden; font-family: system-ui, sans-serif; }
{{STYLE}}</style>
</head>
<body>
<div id="root" data-composition-id="{{ID}}" data-start="0" data-width="{{W}}" data-height="{{H}}" data-fps="{{FPS}}" data-duration="{{D}}" data-koracut-element="{{EL}}" data-koracut-style="{{ST}}" data-koracut-start="{{S}}"{{CUT}}{{SEQ}}>
{{BODY}}</div>
<script>
  window.__timelines = window.__timelines || {};
  const tl = gsap.timeline({ paused: true });
  const root = document.getElementById("root");
  const D = {{D}};
  const W = {{W}};
  const H = {{H}};
{{SCRIPT}}
  window.__timelines["{{ID}}"] = tl;
</script>
</body>
</html>
)KC";

QMap<QString, QString> baseValues(const Spec &spec)
{
    QMap<QString, QString> v;
    v.insert(QStringLiteral("ID"), spec.id);
    v.insert(QStringLiteral("W"), QString::number(spec.width));
    v.insert(QStringLiteral("H"), QString::number(spec.height));
    v.insert(QStringLiteral("FPS"), KoraCutMotion::secondsText(spec.fps));
    v.insert(QStringLiteral("D"), KoraCutMotion::secondsText(spec.duration));
    v.insert(QStringLiteral("EL"), spec.element);
    v.insert(QStringLiteral("S"), KoraCutMotion::secondsText(std::max(0., spec.start)));
    v.insert(QStringLiteral("SEQ"), spec.sequence.isEmpty() ? QString() : QStringLiteral(" data-koracut-sequence=\"%1\"").arg(spec.sequence));
    v.insert(QStringLiteral("CUT"), spec.cut >= 0 ? QStringLiteral(" data-koracut-cut=\"%1\"").arg(KoraCutMotion::secondsText(spec.cut)) : QString());
    const double u = std::min(spec.width, spec.height) / 1080.;
    v.insert(QStringLiteral("ACC"), validColor(spec.accent, QStringLiteral("#7C5CFF")));
    v.insert(QStringLiteral("TXT"), validColor(spec.textColor, QStringLiteral("#FFFFFF")));
    v.insert(QStringLiteral("BG"), validColor(spec.background, QStringLiteral("#2C2C2C")));
    v.insert(QStringLiteral("BGA"), rgba(validColor(spec.background, QStringLiteral("#2C2C2C")), 0.85));
    v.insert(QStringLiteral("U"), QString::number(u, 'g', 4));
    return v;
}

QString assemble(const Spec &spec, const QString &style, const QString &body, const QString &script, const QMap<QString, QString> &extra)
{
    QMap<QString, QString> v = baseValues(spec);
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        v.insert(it.key(), it.value());
    }
    // Les morceaux d'abord (ils portent eux-mêmes des clefs), puis les valeurs.
    QString html = QString::fromUtf8(kHead);
    html.replace(QStringLiteral("{{STYLE}}"), style);
    html.replace(QStringLiteral("{{BODY}}"), body);
    html.replace(QStringLiteral("{{SCRIPT}}"), script);
    return fill(html, v);
}

bool vertical(const Spec &spec)
{
    return spec.height > spec.width;
}

// ---- Les éléments -------------------------------------------------------------------------------

QString titleHtml(const Spec &spec, const QString &style)
{
    const double u = std::min(spec.width, spec.height) / 1080.;
    const int words = spec.text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    // La taille suit le petit côté et la longueur du texte : une ligne de cinq mots tient dans 80 % de la largeur.
    double size = (vertical(spec) ? 104. : 112.) * u;
    if (spec.text.size() > 18) {
        size *= std::max(0.55, 18. / spec.text.size() * 1.25);
    }
    QMap<QString, QString> v;
    v.insert(QStringLiteral("FS"), px(size));
    v.insert(QStringLiteral("MAXW"), px(spec.width * 0.84));
    v.insert(QStringLiteral("RISE"), px(0.55 * size));
    v.insert(QStringLiteral("GAP"), px(22 * u));
    v.insert(QStringLiteral("BW"), px(std::min(spec.width * 0.3, 260 * u)));
    v.insert(QStringLiteral("BH"), px(std::max(4., 8 * u)));
    v.insert(QStringLiteral("TOP"), vertical(spec) ? QStringLiteral("42%") : QStringLiteral("50%"));
    v.insert(QStringLiteral("TW"), px(std::hypot(spec.width, spec.height) * 0.5));
    v.insert(QStringLiteral("TH"), px(std::max(3., 6 * u)));
    const QString text = KoraCutMotion::escapeText(spec.text);
    Q_UNUSED(words)
    QString css = QStringLiteral(R"KC(  #scene { position: absolute; left: 0; right: 0; top: {{TOP}}; display: flex; flex-direction: column; align-items: center; gap: {{GAP}}px; }
  #bloc { position: relative; margin-top: -0.6em; font-size: {{FS}}px; }
  #texte { display: block; margin: 0; color: {{TXT}}; font-size: {{FS}}px; font-weight: 800; line-height: 1.05; letter-spacing: -0.02em; text-align: center; max-width: {{MAXW}}px; }
  #texte .mot { display: inline-block; }
  #barre { width: {{BW}}px; height: {{BH}}px; background: {{ACC}}; border-radius: {{BH}}px; transform-origin: left center; }
)KC");
    QString body;
    QString js;
    if (style == QLatin1String("machine")) {
        css += QStringLiteral(R"KC(  #texte .car { opacity: 0; }
  #curseur { display: inline-block; width: {{BH}}px; height: 0.85em; margin-left: 0.08em; background: {{ACC}}; vertical-align: -0.08em; }
)KC");
        body = QStringLiteral("  <div id=\"scene\"><div id=\"bloc\"><h1 id=\"texte\"><span id=\"source\" data-koracut-texte=\"1\">%1</span><span id=\"curseur\"></span></h1></div></div>\n").arg(text);
        js = QStringLiteral(R"KC(  // La machine à écrire : une lettre après l'autre, puis le curseur clignote (un nombre fini de fois).
  const source = document.getElementById("source");
  const lettres = Array.from(source.textContent.trim());
  source.textContent = "";
  lettres.forEach((l) => { const s = document.createElement("span"); s.className = "car"; s.textContent = l; source.appendChild(s); });
  const pas = Math.min(0.07, (D * 0.45) / Math.max(1, lettres.length));
  tl.to(source.querySelectorAll(".car"), { opacity: 1, duration: 0.01, stagger: pas, ease: "none" }, 0.15);
  const fin = 0.15 + pas * lettres.length;
  const clignes = Math.max(1, Math.floor((D - 0.5 - fin) / 0.5));
  tl.fromTo("#curseur", { opacity: 1 }, { opacity: 0, duration: 0.25, ease: "steps(1)", repeat: clignes * 2 - 1, yoyo: true }, fin);
  tl.to("#texte", { opacity: 0, duration: 0.3, ease: "power2.in" }, Math.max(fin + 0.2, D - 0.35));
)KC");
    } else if (style == QLatin1String("eclat")) {
        css += QStringLiteral(R"KC(  #coupe { position: absolute; left: 50%; top: 50%; width: 0; height: 0; transform: rotate(-32deg); }
  #trait { position: absolute; left: -{{TW}}px; top: -{{TH}}px; width: calc({{TW}}px * 2); height: {{TH}}px; background: {{ACC}}; transform-origin: left center; }
)KC");
        body = QStringLiteral("  <div id=\"scene\"><div id=\"bloc\"><h1 id=\"texte\" data-koracut-texte=\"1\">%1</h1><div id=\"coupe\"><div id=\"trait\"></div></div></div><div id=\"barre\"></div></div>\n").arg(text);
        js = QStringLiteral(R"KC(  // L'éclat : une coupe à 32° traverse, le texte se découvre derrière elle.
  tl.fromTo("#trait", { scaleX: 0 }, { scaleX: 1, duration: 0.32, ease: "power4.out" }, 0.05);
  tl.fromTo("#texte", { clipPath: "inset(0% 100% 0% 0%)", scale: 1.06 }, { clipPath: "inset(0% 0% 0% 0%)", scale: 1, duration: 0.6, ease: "power3.out" }, 0.18);
  tl.to("#trait", { opacity: 0, duration: 0.35, ease: "power2.out" }, 0.45);
  tl.fromTo("#barre", { scaleX: 0 }, { scaleX: 1, duration: 0.45, ease: "power3.out" }, 0.45);
  tl.to("#texte", { clipPath: "inset(0% 0% 0% 100%)", duration: 0.4, ease: "power2.in" }, Math.max(1, D - 0.45));
  tl.to("#barre", { scaleX: 0, duration: 0.3, ease: "power2.in" }, Math.max(1, D - 0.4));
)KC");
    } else {
        body = QStringLiteral("  <div id=\"scene\"><div id=\"bloc\"><h1 id=\"texte\" data-koracut-texte=\"1\">%1</h1></div><div id=\"barre\"></div></div>\n").arg(text);
        js = QStringLiteral(R"KC(  // Les mots montent un par un, la barre d'accent se trace dessous.
  const texte = document.getElementById("texte");
  const mots = texte.textContent.trim().split(/\s+/);
  texte.textContent = "";
  mots.forEach((m, i) => {
    const s = document.createElement("span");
    s.className = "mot";
    s.textContent = m;
    texte.appendChild(s);
    if (i < mots.length - 1) texte.appendChild(document.createTextNode(" "));
  });
  const spans = texte.querySelectorAll(".mot");
  tl.fromTo(spans, { y: {{RISE}}, opacity: 0 }, { y: 0, opacity: 1, duration: 0.55, ease: "power3.out", stagger: 0.07 }, 0.1);
  tl.fromTo("#barre", { scaleX: 0 }, { scaleX: 1, duration: 0.5, ease: "power3.out" }, 0.3);
  tl.to(spans, { y: -{{RISE}} * 0.4, opacity: 0, duration: 0.35, ease: "power2.in", stagger: 0.03 }, Math.max(0.9, D - 0.5));
  tl.to("#barre", { scaleX: 0, duration: 0.3, ease: "power2.in" }, Math.max(0.9, D - 0.45));
)KC");
    }
    // Le bloc garde sa hauteur : la scène est centrée sur le haut choisi, pas sur sa propre boîte.
    css += QStringLiteral("  #scene { transform-origin: center; }\n");
    return assemble(spec, css, body, js, v);
}

QString strapHtml(const Spec &spec)
{
    const double u = std::min(spec.width, spec.height) / 1080.;
    QStringList parts = spec.text.split(QRegularExpression(QStringLiteral("\\s*(\\||\\n)\\s*")), Qt::SkipEmptyParts);
    const QString name = parts.value(0).trimmed();
    const QString role = parts.mid(1).join(QLatin1Char(' ')).trimmed();
    QMap<QString, QString> v;
    const bool vert = vertical(spec);
    v.insert(QStringLiteral("BX"), px(vert ? spec.width * 0.08 : spec.width * 0.05));
    v.insert(QStringLiteral("VERT"), vert ? QStringLiteral("top: 62%") : QStringLiteral("bottom: %1px").arg(px(spec.height * 0.07)));
    v.insert(QStringLiteral("AW"), px(std::max(4., 8 * u)));
    v.insert(QStringLiteral("PY"), px(18 * u));
    v.insert(QStringLiteral("PX"), px(30 * u));
    v.insert(QStringLiteral("G"), px(6 * u));
    v.insert(QStringLiteral("FN"), px((vert ? 58 : 46) * u));
    v.insert(QStringLiteral("FF"), px((vert ? 36 : 28) * u));
    v.insert(QStringLiteral("SL"), px(60 * u));
    v.insert(QStringLiteral("SL2"), px(36 * u));
    v.insert(QStringLiteral("NY"), px(12 * u));
    const QString css = QStringLiteral(R"KC(  #bandeau { position: absolute; left: {{BX}}px; {{VERT}}; display: flex; align-items: stretch; }
  #accent { width: {{AW}}px; background: {{ACC}}; transform-origin: center bottom; }
  #boite { background: {{BGA}}; padding: {{PY}}px {{PX}}px; display: flex; flex-direction: column; gap: {{G}}px; }
  #nom { display: block; margin: 0; color: {{TXT}}; font-size: {{FN}}px; font-weight: 700; line-height: 1.1; }
  #fonction { display: block; margin: 0; color: {{TXT}}; opacity: 0.8; font-size: {{FF}}px; font-weight: 400; line-height: 1.2; }
)KC");
    QString body = QStringLiteral("  <div id=\"bandeau\"><div id=\"accent\"></div><div id=\"boite\"><p id=\"nom\" data-koracut-texte=\"1\">%1</p>").arg(KoraCutMotion::escapeText(name));
    if (!role.isEmpty()) {
        body += QStringLiteral("<p id=\"fonction\">%1</p>").arg(KoraCutMotion::escapeText(role));
    }
    body += QStringLiteral("</div></div>\n");
    const QString js = QStringLiteral(R"KC(  // Le bandeau glisse depuis le bord, la barre d'accent pousse, le nom puis la fonction arrivent.
  tl.fromTo("#bandeau", { x: -{{SL}}, opacity: 0 }, { x: 0, opacity: 1, duration: 0.5, ease: "power3.out" }, 0.1);
  tl.fromTo("#accent", { scaleY: 0 }, { scaleY: 1, duration: 0.45, ease: "power3.out" }, 0.1);
  tl.fromTo("#nom", { y: {{NY}}, opacity: 0 }, { y: 0, opacity: 1, duration: 0.45, ease: "power3.out" }, 0.2);
  if (document.getElementById("fonction")) {
    tl.fromTo("#fonction", { y: {{NY}}, opacity: 0 }, { y: 0, opacity: 0.8, duration: 0.45, ease: "power3.out" }, 0.35);
  }
  tl.to("#bandeau", { x: -{{SL2}}, opacity: 0, duration: 0.35, ease: "power2.in" }, Math.max(1, D - 0.4));
)KC");
    return assemble(spec, css, body, js, v);
}

QString counterHtml(const Spec &spec)
{
    const double u = std::min(spec.width, spec.height) / 1080.;
    QMap<QString, QString> v;
    v.insert(QStringLiteral("FC"), px((vertical(spec) ? 200 : 180) * u));
    v.insert(QStringLiteral("FL"), px(40 * u));
    v.insert(QStringLiteral("GAP"), px(10 * u));
    v.insert(QStringLiteral("TOP"), vertical(spec) ? QStringLiteral("40%") : QStringLiteral("44%"));
    const QString css = QStringLiteral(R"KC(  #source { display: none; }
  #scene { position: absolute; left: 0; right: 0; top: {{TOP}}; display: flex; flex-direction: column; align-items: center; gap: {{GAP}}px; }
  #ligne { display: flex; align-items: baseline; justify-content: center; }
  #chiffre { display: inline-block; color: {{TXT}}; font-size: {{FC}}px; font-weight: 800; font-variant-numeric: tabular-nums; line-height: 1; transform-origin: center; }
  #avant, #apres { display: inline-block; color: {{ACC}}; font-size: calc({{FC}}px * 0.55); font-weight: 800; }
  #libelle { display: block; color: {{TXT}}; opacity: 0.85; font-size: {{FL}}px; font-weight: 500; margin: 0; }
)KC");
    const QString body = QStringLiteral(R"KC(  <span id="source" data-koracut-texte="1">%1</span>
  <div id="scene"><div id="ligne"><span id="avant"></span><span id="chiffre">0</span><span id="apres"></span></div><p id="libelle"></p></div>
)KC")
                             .arg(KoraCutMotion::escapeText(spec.text));
    const QString js = QStringLiteral(R"KC(  // Le chiffre compte de 0 à sa valeur en grandissant, puis son unité arrive et le libellé.
  const brut = document.getElementById("source").textContent.trim();
  const m = brut.match(/^(\D*?)(\d[\d\s .,]*\d|\d)(.*)$/s);
  const avant = m ? m[1].trim() : "";
  const nombre = m ? parseFloat(m[2].replace(/[\s ]/g, "").replace(",", ".")) : 0;
  const decimales = m && /[.,]\d+$/.test(m[2].trim()) ? m[2].trim().split(/[.,]/).pop().length : 0;
  const reste = m ? m[3].trim() : brut;
  const unite = reste.match(/^(%|\+|×|x|k|M)\s*/);
  document.getElementById("avant").textContent = avant;
  document.getElementById("apres").textContent = unite ? unite[1] : "";
  document.getElementById("libelle").textContent = unite ? reste.slice(unite[0].length) : reste;
  const chiffre = document.getElementById("chiffre");
  const ecrire = (x) => {
    const t = x.toFixed(decimales).split(".");
    t[0] = t[0].replace(/\B(?=(\d{3})+(?!\d))/g, " ");
    chiffre.textContent = t.join(",");
  };
  ecrire(0);
  const etat = { v: 0 };
  const duree = Math.min(1.6, D * 0.5);
  tl.to(etat, { v: nombre, duration: duree, ease: "power2.out", onUpdate: () => ecrire(etat.v) }, 0.15);
  tl.fromTo(chiffre, { scale: 0.82, opacity: 0 }, { scale: 1, opacity: 1, duration: duree, ease: "power2.out" }, 0.15);
  tl.fromTo("#avant, #apres", { opacity: 0, y: 20 }, { opacity: 1, y: 0, duration: 0.35, ease: "back.out(1.8)" }, 0.15 + duree);
  tl.fromTo("#libelle", { opacity: 0, y: 14 }, { opacity: 0.85, y: 0, duration: 0.45, ease: "power2.out" }, 0.3);
  tl.to("#scene", { opacity: 0, duration: 0.35, ease: "power2.in" }, Math.max(0.15 + duree + 0.3, D - 0.4));
)KC");
    return assemble(spec, css, body, js, v);
}

QString logoHtml(const Spec &spec)
{
    const double side = std::min(spec.width, spec.height) * 0.36;
    QMap<QString, QString> v;
    v.insert(QStringLiteral("LS"), px(side));
    v.insert(QStringLiteral("TOP"), vertical(spec) ? QStringLiteral("40%") : QStringLiteral("50%"));
    const QString css = QStringLiteral(R"KC(  #scene { position: absolute; left: 0; right: 0; top: {{TOP}}; display: flex; justify-content: center; }
  #logo { display: block; width: {{LS}}px; height: {{LS}}px; margin-top: calc({{LS}}px / -2); object-fit: contain; transform-origin: center; }
)KC");
    const QString body = QStringLiteral("  <div id=\"scene\"><img id=\"logo\" src=\"%1\" alt=\"\"></div>\n").arg(QString(spec.file).replace(QLatin1Char('"'), QLatin1String("&quot;")));
    const QString js = QStringLiteral(R"KC(  // Le logo arrive avec un rebond, se pose, repart doucement.
  tl.fromTo("#logo", { scale: 0.4, opacity: 0, rotation: -8 }, { scale: 1, opacity: 1, rotation: 0, duration: 0.65, ease: "back.out(1.8)" }, 0.1);
  tl.to("#logo", { scale: 0.92, opacity: 0, duration: 0.35, ease: "power2.in" }, Math.max(1, D - 0.4));
)KC");
    return assemble(spec, css, body, js, v);
}

QString captionHtml(const Spec &spec)
{
    const double u = std::min(spec.width, spec.height) / 1080.;
    const bool vert = vertical(spec);
    QMap<QString, QString> v;
    v.insert(QStringLiteral("BX"), px(vert ? spec.width * 0.08 : spec.width * 0.05));
    v.insert(QStringLiteral("VERT"), vert ? QStringLiteral("top: 18%") : QStringLiteral("top: %1px").arg(px(spec.height * 0.08)));
    v.insert(QStringLiteral("LW"), px(80 * u));
    v.insert(QStringLiteral("LH"), px(std::max(3., 5 * u)));
    v.insert(QStringLiteral("F"), px((vert ? 44 : 36) * u));
    v.insert(QStringLiteral("GAP"), px(18 * u));
    v.insert(QStringLiteral("SL"), px(24 * u));
    const QString css = QStringLiteral(R"KC(  #legende { position: absolute; left: {{BX}}px; {{VERT}}; display: flex; align-items: center; gap: {{GAP}}px; }
  #ligne { width: {{LW}}px; height: {{LH}}px; background: {{ACC}}; transform-origin: left center; }
  #texte { display: block; margin: 0; color: {{TXT}}; font-size: {{F}}px; font-weight: 600; letter-spacing: 0.01em; }
)KC");
    const QString body = QStringLiteral("  <div id=\"legende\"><div id=\"ligne\"></div><p id=\"texte\" data-koracut-texte=\"1\">%1</p></div>\n").arg(KoraCutMotion::escapeText(spec.text));
    const QString js = QStringLiteral(R"KC(  // Un trait se tire, la légende glisse derrière lui.
  tl.fromTo("#ligne", { scaleX: 0 }, { scaleX: 1, duration: 0.4, ease: "power3.out" }, 0.1);
  tl.fromTo("#texte", { x: -{{SL}}, opacity: 0 }, { x: 0, opacity: 1, duration: 0.45, ease: "power3.out" }, 0.25);
  tl.to("#legende", { opacity: 0, duration: 0.3, ease: "power2.in" }, Math.max(1, D - 0.35));
)KC");
    return assemble(spec, css, body, js, v);
}

// ---- Les transitions --------------------------------------------------------------------------

QMap<QString, QString> transitionValues(const Spec &spec)
{
    const double diag = std::hypot(spec.width, spec.height);
    const double u = std::min(spec.width, spec.height) / 1080.;
    QMap<QString, QString> v;
    v.insert(QStringLiteral("DIAG"), px(diag));
    v.insert(QStringLiteral("PW"), px(diag * 3));
    v.insert(QStringLiteral("PL"), px(diag * 1.5));
    v.insert(QStringLiteral("PH"), px(diag * 1.2));
    v.insert(QStringLiteral("SW"), px(std::max(3., 7 * u)));
    v.insert(QStringLiteral("TH"), px(std::max(3., 8 * u)));
    v.insert(QStringLiteral("TH2"), px(std::max(1.5, 4 * u)));
    v.insert(QStringLiteral("BLUR"), px(60 * u));
    return v;
}

QString stringsHtml(const Spec &spec)
{
    // Cordes coupées (l'idée d'Amadou Ba, le signe de la marque) : quatre cordes se tracent, deux volets
    // se referment sur une coupe à 32°, un éclair la tranche, les volets s'écartent et emportent chacun
    // la moitié des cordes. Au milieu, l'image est entièrement couverte : la coupe se fait derrière.
    const QString css = QStringLiteral(R"KC(  #coupe { position: absolute; left: 50%; top: 50%; width: 0; height: 0; transform: rotate(-32deg); }
  .volet { position: absolute; left: -{{PL}}px; width: {{PW}}px; height: {{PH}}px; background: {{BG}}; }
  #volet-haut { top: -{{PH}}px; }
  #volet-bas { top: -2px; }
  #trait { position: absolute; left: -{{PL}}px; top: -{{TH2}}px; width: {{PW}}px; height: {{TH}}px; background: {{ACC}}; transform-origin: center; }
  #cordes { position: absolute; inset: 0; }
  .corde { position: absolute; width: {{SW}}px; background: {{TXT}}; border-radius: {{SW}}px; }
  #eclair { position: absolute; inset: 0; background: #FFFFFF; opacity: 0; }
)KC");
    const QString body = QStringLiteral(R"KC(  <div id="coupe"><div id="volet-haut" class="volet"></div><div id="volet-bas" class="volet"></div><div id="trait"></div></div>
  <div id="cordes"></div>
  <div id="eclair"></div>
)KC");
    const QString js = QStringLiteral(R"KC(  // La coupe passe par le centre, inclinée à 32° : chaque corde est tranchée là où la coupe la croise.
  const M = D / 2;
  const pente = Math.tan(32 * Math.PI / 180);
  const cordes = document.getElementById("cordes");
  const hauts = [];
  const bas = [];
  [0.38, 0.46, 0.54, 0.62].forEach((f) => {
    const x = W * f;
    const y = H / 2 - (x - W / 2) * pente;
    const haut = document.createElement("div");
    haut.className = "corde";
    haut.style.left = x + "px";
    haut.style.top = "0px";
    haut.style.height = y + "px";
    haut.style.transformOrigin = "center top";
    const dessous = document.createElement("div");
    dessous.className = "corde";
    dessous.style.left = x + "px";
    dessous.style.top = y + "px";
    dessous.style.height = (H - y) + "px";
    dessous.style.transformOrigin = "center bottom";
    cordes.appendChild(haut);
    cordes.appendChild(dessous);
    hauts.push(haut);
    bas.push(dessous);
  });
  // La direction qui écarte les deux moitiés : perpendiculaire à la coupe.
  const nx = -Math.sin(32 * Math.PI / 180);
  const ny = -Math.cos(32 * Math.PI / 180);
  const loin = {{DIAG}} * 0.7;
  tl.fromTo(hauts, { scaleY: 0 }, { scaleY: 1, duration: M * 0.45, ease: "power2.out", stagger: M * 0.05 }, 0);
  tl.fromTo(bas, { scaleY: 0 }, { scaleY: 1, duration: M * 0.45, ease: "power2.out", stagger: M * 0.05 }, 0);
  tl.fromTo("#volet-haut", { yPercent: -100 }, { yPercent: 0, duration: M * 0.75, ease: "power3.in" }, M * 0.15);
  tl.fromTo("#volet-bas", { yPercent: 100 }, { yPercent: 0, duration: M * 0.75, ease: "power3.in" }, M * 0.15);
  tl.fromTo("#trait", { scaleX: 0 }, { scaleX: 1, duration: M * 0.12, ease: "power4.out" }, M * 0.88);
  tl.fromTo("#eclair", { opacity: 0 }, { opacity: 0.85, duration: M * 0.1, ease: "power2.in" }, M * 0.9);
  tl.to("#eclair", { opacity: 0, duration: M * 0.5, ease: "power2.out" }, M);
  tl.to("#volet-haut", { yPercent: -100, duration: M, ease: "power3.out" }, M);
  tl.to("#volet-bas", { yPercent: 100, duration: M, ease: "power3.out" }, M);
  tl.to(hauts, { x: nx * loin, y: ny * loin, duration: M, ease: "power3.out" }, M);
  tl.to(bas, { x: -nx * loin, y: -ny * loin, duration: M, ease: "power3.out" }, M);
  tl.to("#trait", { opacity: 0, duration: M * 0.4, ease: "power2.out" }, M * 1.05);
)KC");
    return assemble(spec, css, body, js, transitionValues(spec));
}

QString sweepHtml(const Spec &spec)
{
    // Balayage à 32° : une bande de la couleur d'accent traverse l'image en biais, un liseré clair la
    // précède ; au milieu elle couvre tout.
    const QString css = QStringLiteral(R"KC(  #coupe { position: absolute; left: 50%; top: 50%; width: 0; height: 0; transform: rotate(-32deg); }
  #bande { position: absolute; left: -{{PL}}px; top: -{{PH}}px; width: {{PW}}px; height: calc({{PH}}px * 2); background: {{ACC}}; }
  #lisere { position: absolute; right: 0; top: 0; bottom: 0; width: calc({{DIAG}}px * 0.06); background: {{TXT}}; }
)KC");
    const QString body = QStringLiteral(R"KC(  <div id="coupe"><div id="bande"><div id="lisere"></div></div></div>
)KC");
    const QString js = QStringLiteral(R"KC(  const M = D / 2;
  tl.fromTo("#bande", { x: -{{PW}} }, { x: 0, duration: M * 0.9, ease: "power3.in" }, 0);
  tl.to("#bande", { x: {{PW}}, duration: M, ease: "power3.out" }, M);
)KC");
    return assemble(spec, css, body, js, transitionValues(spec));
}

QString shakeHtml(const Spec &spec)
{
    // Secousse : des bandes décalées tremblent, un voile couvre l'image au milieu, un éclair, puis les
    // bandes tremblent encore en s'effaçant. Hasard à graine fixe : le rendu est toujours le même.
    const QString css = QStringLiteral(R"KC(  #tout { position: absolute; inset: 0; }
  .bande { position: absolute; left: -5%; width: 110%; }
  #voile { position: absolute; inset: 0; background: {{BG}}; opacity: 0; }
  #eclair { position: absolute; inset: 0; background: #FFFFFF; opacity: 0; }
)KC");
    const QString body = QStringLiteral(R"KC(  <div id="tout"><div id="voile"></div><div id="bandes"></div><div id="eclair"></div></div>
)KC");
    const QString js = QStringLiteral(R"KC(  const M = D / 2;
  let graine = 20260924;
  const hasard = () => { graine = (graine * 16807) % 2147483647; return (graine - 1) / 2147483646; };
  const couleurs = ["{{ACC}}", "{{TXT}}", "{{BG}}"];
  const bandes = document.getElementById("bandes");
  const n = 9;
  const liste = [];
  for (let i = 0; i < n; i++) {
    const b = document.createElement("div");
    b.className = "bande";
    const h = H / n * (0.35 + hasard() * 0.9);
    b.style.top = (H / n * i + hasard() * H / n * 0.4) + "px";
    b.style.height = h + "px";
    b.style.background = couleurs[i % couleurs.length];
    b.style.opacity = "0";
    bandes.appendChild(b);
    liste.push(b);
  }
  const image = 1 / {{FPS}};
  const trembler = (debut, fin, force) => {
    for (let t = debut; t < fin; t += image * 2) {
      liste.forEach((b) => tl.set(b, { x: (hasard() - 0.5) * W * force, opacity: 0.35 + hasard() * 0.55 }, t));
      tl.set("#tout", { x: (hasard() - 0.5) * W * force * 0.3, y: (hasard() - 0.5) * H * force * 0.2 }, t);
    }
  };
  trembler(M * 0.35, M * 0.8, 0.08);
  tl.fromTo("#voile", { opacity: 0 }, { opacity: 1, duration: M * 0.18, ease: "power2.in" }, M * 0.72);
  tl.fromTo("#eclair", { opacity: 0 }, { opacity: 0.9, duration: M * 0.08, ease: "none" }, M * 0.92);
  tl.to("#eclair", { opacity: 0, duration: M * 0.35, ease: "power2.out" }, M);
  tl.to("#voile", { opacity: 0, duration: M * 0.45, ease: "power2.out" }, M);
  trembler(M, M * 1.5, 0.06);
  liste.forEach((b) => tl.set(b, { opacity: 0, x: 0 }, M * 1.55));
  tl.set("#tout", { x: 0, y: 0 }, M * 1.55);
)KC");
    return assemble(spec, css, body, js, transitionValues(spec));
}

QString veilHtml(const Spec &spec)
{
    // Voile de brume : des nappes floues montent, se rejoignent en un voile clair au milieu, puis se
    // dissipent en dérivant.
    const QString css = QStringLiteral(R"KC(  #nappe { position: absolute; inset: 0; background: {{TXT}}; opacity: 0; }
  .brume { position: absolute; border-radius: 50%; background: radial-gradient(circle, {{TXT}} 0%, rgba(255, 255, 255, 0) 70%); filter: blur({{BLUR}}px); opacity: 0; }
)KC");
    const QString body = QStringLiteral(R"KC(  <div id="brumes"></div>
  <div id="nappe"></div>
)KC");
    const QString js = QStringLiteral(R"KC(  const M = D / 2;
  const places = [[0.15, 0.3], [0.55, 0.2], [0.85, 0.45], [0.3, 0.75], [0.7, 0.8], [0.5, 0.5]];
  const brumes = document.getElementById("brumes");
  const liste = places.map(([fx, fy], i) => {
    const b = document.createElement("div");
    b.className = "brume";
    const r = {{DIAG}} * (0.35 + (i % 3) * 0.08);
    b.style.width = r + "px";
    b.style.height = r + "px";
    b.style.left = (W * fx - r / 2) + "px";
    b.style.top = (H * fy - r / 2) + "px";
    brumes.appendChild(b);
    return b;
  });
  tl.fromTo(liste, { scale: 0.5, opacity: 0 }, { scale: 1.3, opacity: 0.95, duration: M * 0.9, ease: "sine.inOut", stagger: M * 0.03 }, 0);
  tl.fromTo("#nappe", { opacity: 0 }, { opacity: 1, duration: M * 0.5, ease: "sine.in" }, M * 0.42);
  tl.to("#nappe", { opacity: 0, duration: M * 0.8, ease: "sine.out" }, M);
  tl.to(liste, { scale: 1.8, opacity: 0, x: W * 0.06, duration: M, ease: "sine.out", stagger: M * 0.02 }, M);
)KC");
    return assemble(spec, css, body, js, transitionValues(spec));
}

QString flashHtml(const Spec &spec)
{
    const QString css = QStringLiteral(R"KC(  #eclair { position: absolute; inset: 0; background: #FFFFFF; opacity: 0; }
)KC");
    const QString body = QStringLiteral("  <div id=\"eclair\"></div>\n");
    const QString js = QStringLiteral(R"KC(  const M = D / 2;
  tl.fromTo("#eclair", { opacity: 0 }, { opacity: 1, duration: M * 0.45, ease: "power2.in" }, M * 0.5);
  tl.to("#eclair", { opacity: 0, duration: M, ease: "power2.out" }, M);
)KC");
    return assemble(spec, css, body, js, transitionValues(spec));
}

} // namespace

QStringList elementKinds()
{
    return {QStringLiteral("titre"), QStringLiteral("bandeau"), QStringLiteral("compteur"), QStringLiteral("logo"), QStringLiteral("legende")};
}

QStringList elementStyles(const QString &element)
{
    if (element == QLatin1String("titre")) {
        return {QStringLiteral("monte"), QStringLiteral("machine"), QStringLiteral("eclat")};
    }
    if (element == QLatin1String("bandeau")) {
        return {QStringLiteral("glisse")};
    }
    if (element == QLatin1String("compteur")) {
        return {QStringLiteral("compte")};
    }
    if (element == QLatin1String("logo")) {
        return {QStringLiteral("rebond")};
    }
    if (element == QLatin1String("legende")) {
        return {QStringLiteral("trait")};
    }
    return {};
}

QStringList transitionStyles()
{
    return {QStringLiteral("cordes"), QStringLiteral("balayage"), QStringLiteral("secousse"), QStringLiteral("voile"), QStringLiteral("eclair")};
}

double transitionDefaultDuration(const QString &style)
{
    if (style == QLatin1String("voile")) {
        return 1.2;
    }
    if (style == QLatin1String("secousse") || style == QLatin1String("eclair")) {
        return 0.6;
    }
    return 0.9; // cordes, balayage
}

QString elementHtml(const Spec &spec, QString *error)
{
    const QStringList styles = elementStyles(spec.element);
    if (styles.isEmpty()) {
        if (error) {
            *error = QStringLiteral("sorte vaut %1.").arg(elementKinds().join(QLatin1String(", ")));
        }
        return QString();
    }
    const QString style = spec.style.isEmpty() ? styles.first() : spec.style;
    if (!styles.contains(style)) {
        if (error) {
            *error = QStringLiteral("Pour un %1, style vaut %2.").arg(spec.element, styles.join(QLatin1String(", ")));
        }
        return QString();
    }
    if (spec.element == QLatin1String("logo")) {
        if (spec.file.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Un logo a besoin de son fichier (fichier).");
            }
            return QString();
        }
    } else if (spec.text.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("texte est vide.");
        }
        return QString();
    }
    Spec s = spec;
    s.style = style;
    QString html;
    if (spec.element == QLatin1String("titre")) {
        html = titleHtml(s, style);
    } else if (spec.element == QLatin1String("bandeau")) {
        html = strapHtml(s);
    } else if (spec.element == QLatin1String("compteur")) {
        html = counterHtml(s);
    } else if (spec.element == QLatin1String("logo")) {
        html = logoHtml(s);
    } else {
        html = captionHtml(s);
    }
    return html.replace(QStringLiteral("{{ST}}"), style);
}

QString transitionHtml(const Spec &spec, QString *error)
{
    const QString style = spec.style.isEmpty() ? transitionStyles().first() : spec.style;
    Spec s = spec;
    s.element = QStringLiteral("transition");
    s.style = style;
    QString html;
    if (style == QLatin1String("cordes")) {
        // Les volets sont sombres, les cordes du texte (blanc) : la couleur de fond par défaut suit l'application.
        html = stringsHtml(s);
    } else if (style == QLatin1String("balayage")) {
        html = sweepHtml(s);
    } else if (style == QLatin1String("secousse")) {
        html = shakeHtml(s);
    } else if (style == QLatin1String("voile")) {
        html = veilHtml(s);
    } else if (style == QLatin1String("eclair")) {
        html = flashHtml(s);
    } else {
        if (error) {
            *error = QStringLiteral("style vaut %1.").arg(transitionStyles().join(QLatin1String(", ")));
        }
        return QString();
    }
    return html.replace(QStringLiteral("{{ST}}"), style);
}

QString wrapBrief(const QString &brief, const Spec &spec, QString *error)
{
    using namespace KoraCutMotion;
    auto fail = [error](const QString &why) {
        if (error) {
            *error = why;
        }
        return QString();
    };
    if (brief.trimmed().isEmpty()) {
        return fail(QStringLiteral("brief_html est vide."));
    }
    QString html;
    if (brief.contains(QLatin1String("data-composition-id"))) {
        // Une composition complète : on garde tout, la racine reçoit ce que KoraCut décide.
        html = brief;
        const QMap<QString, QString> attributes{{QStringLiteral("data-width"), QString::number(spec.width)},
                                                {QStringLiteral("data-height"), QString::number(spec.height)},
                                                {QStringLiteral("data-fps"), secondsText(spec.fps)},
                                                {QStringLiteral("data-duration"), secondsText(spec.duration)},
                                                {QStringLiteral("data-koracut-element"), spec.element},
                                                {QStringLiteral("data-koracut-style"), spec.style.isEmpty() ? QStringLiteral("agent") : spec.style},
                                                {QStringLiteral("data-koracut-start"), secondsText(std::max(0., spec.start))}};
        QMap<QString, QString> withSequence = attributes;
        if (!spec.sequence.isEmpty()) {
            withSequence.insert(QStringLiteral("data-koracut-sequence"), spec.sequence);
        }
        for (auto it = withSequence.constBegin(); it != withSequence.constEnd(); ++it) {
            const QList<Tag> tags = tokenize(html);
            for (const Tag &t : tags) {
                if (!t.closing && t.has(QStringLiteral("data-composition-id"))) {
                    html = setAttribute(html, t, it.key(), it.value());
                    break;
                }
            }
        }
        if (spec.cut >= 0) {
            const QList<Tag> tags = tokenize(html);
            for (const Tag &t : tags) {
                if (!t.closing && t.has(QStringLiteral("data-composition-id"))) {
                    html = setAttribute(html, t, QStringLiteral("data-koracut-cut"), secondsText(spec.cut));
                    break;
                }
            }
        }
    } else {
        // Un fragment : ses styles vont dans la tête, ses scripts dans la timeline déjà créée.
        QString rest = brief;
        QString styles;
        QString scripts;
        static const QRegularExpression styleRe(QStringLiteral("<style[^>]*>(.*?)</style>"), QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression scriptRe(QStringLiteral("<script[^>]*>(.*?)</script>"), QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
        for (auto it = styleRe.globalMatch(brief); it.hasNext();) {
            styles += QStringLiteral("  ") + it.next().captured(1).trimmed() + QLatin1Char('\n');
        }
        for (auto it = scriptRe.globalMatch(brief); it.hasNext();) {
            scripts += QStringLiteral("  ") + it.next().captured(1).trimmed() + QLatin1Char('\n');
        }
        rest.remove(styleRe);
        rest.remove(scriptRe);
        if (scripts.contains(QLatin1String("gsap.timeline("))) {
            return fail(QStringLiteral("Le fragment crée sa propre timeline : utilise tl, déjà créée et en pause."));
        }
        QString html0 = QString::fromUtf8(kHead);
        html0.replace(QStringLiteral("{{STYLE}}"), styles);
        html0.replace(QStringLiteral("{{BODY}}"), QStringLiteral("  ") + rest.trimmed() + QLatin1Char('\n'));
        html0.replace(QStringLiteral("{{SCRIPT}}"), scripts);
        html = fill(html0, baseValues(spec));
        html.replace(QStringLiteral("{{ST}}"), spec.style.isEmpty() ? QStringLiteral("agent") : spec.style);
    }
    const Composition comp = analyse(html);
    if (!comp.hasRoot) {
        return fail(QStringLiteral("Le HTML n'a pas de racine data-composition-id."));
    }
    if (comp.elements.size() != 1 || comp.elements.first().kind != Kind::Whole) {
        return fail(QStringLiteral("Une composition d'un seul élément n'a aucun élément temporel à l'intérieur (pas de data-start sous la racine) : "
                                   "un élément = une composition. Pour plusieurs éléments, un appel par élément."));
    }
    return html;
}

QString kindLabel(const QString &element)
{
    if (element == QLatin1String("titre")) {
        return i18nc("@item kind of a Motion element", "Title");
    }
    if (element == QLatin1String("bandeau")) {
        return i18nc("@item kind of a Motion element (name and role strap)", "Strap");
    }
    if (element == QLatin1String("compteur")) {
        return i18nc("@item kind of a Motion element", "Counter");
    }
    if (element == QLatin1String("logo")) {
        return i18nc("@item kind of a Motion element", "Logo");
    }
    if (element == QLatin1String("legende")) {
        return i18nc("@item kind of a Motion element", "Caption");
    }
    if (element == QLatin1String("transition")) {
        return i18nc("@item kind of a Motion element", "Transition");
    }
    return i18nc("@item kind of a Motion element", "Element");
}

namespace {
// Ce qui distingue un élément seul dans une liste : son texte, sinon son style (« Transition · cordes »).
// Pour un élément de sorte générique, le nom de sa composition passe avant le style (12 octobre 2026) : un
// agent écrit toutes ses compositions avec le même style (« agent »), et quarante médias s'appelaient alors
// « agent · 16:9 » dans le chutier.
QString elementDetail(const KoraCutMotion::Composition &composition, const QString &project, bool projectFirst)
{
    QString detail;
    if (!composition.elements.isEmpty()) {
        detail = composition.elements.first().text;
    }
    if (detail.isEmpty() && projectFirst) {
        detail = project;
    }
    if (detail.isEmpty()) {
        detail = composition.style;
    }
    if (detail.size() > 32) {
        detail = detail.left(31) + QChar(0x2026);
    }
    return detail;
}
} // namespace

QString readableName(const KoraCutMotion::Composition &composition, const QString &project)
{
    if (!composition.isElement()) {
        return project;
    }
    const QString detail = elementDetail(composition, project, false);
    return detail.isEmpty() ? kindLabel(composition.elementKind) : i18nc("@item Motion element name: kind · detail", "%1 · %2", kindLabel(composition.elementKind), detail);
}

QString formatLabel(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return QString();
    }
    const int g = std::gcd(width, height);
    const int a = width / g;
    const int b = height / g;
    if (a <= 32 && b <= 32) {
        return QStringLiteral("%1:%2").arg(a).arg(b);
    }
    return QStringLiteral("%1×%2").arg(width).arg(height);
}

QString binClipName(const KoraCutMotion::Composition &composition, const QString &project)
{
    QString base = readableName(composition, project);
    if (composition.isElement() && kindLabel(composition.elementKind) == kindLabel(QString())) {
        // La sorte générique « Élément » n'apprend rien : le détail seul suffit.
        const QString detail = elementDetail(composition, project, true);
        if (!detail.isEmpty()) {
            base = detail;
        }
    }
    const QString format = formatLabel(composition.width, composition.height);
    return format.isEmpty() ? base : i18nc("@item media name in the bin: name · picture format (16:9, 9:16)", "%1 · %2", base, format);
}

} // namespace KoraCutMotionCatalog
