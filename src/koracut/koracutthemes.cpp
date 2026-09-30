/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutthemes.h"
#include "koracutfonts.h"

#include "kdenlivesettings.h"
#include "utils/uiutils.h"

#include <KColorSchemeManager>
#include <KColorSchemeModel>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QJsonArray>
#include <QLocale>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>
#include <utility>

namespace {

// Le thème à l'écran, gardé pour les lectures de peinture (token(), trackColor()).
QHash<QString, QColor> g_current;
QHash<QString, QColor> g_tracks;
QString g_currentId;
// La police d'interface d'avant tout thème (celle du système), pour y revenir.
QFont g_defaultFont;
bool g_hasDefaultFont = false;

// Les couleurs de la charte (« 2 - Charte graphique KoraCut.md ») : ce qu'un thème ne dit pas.
const QMap<QString, QColor> &charte()
{
    static const QMap<QString, QColor> c = {
        {QStringLiteral("accent"), QColor(0x0D99FF)},      {QStringLiteral("rouge"), QColor(0xFF7262)},
        {QStringLiteral("vert"), QColor(0x4ADE80)},        {QStringLiteral("jaune"), QColor(0xFFD166)},
        {QStringLiteral("piste_video"), QColor(0x407499)}, {QStringLiteral("piste_titre"), QColor(0x7C5CFF)},
        {QStringLiteral("piste_voix"), QColor(0x14A37F)},  {QStringLiteral("piste_musique"), QColor(0xE07B39)},
    };
    return c;
}

double channel(double v)
{
    v /= 255.0;
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &c)
{
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

QColor over(const QColor &fg, const QColor &bg)
{
    const double a = fg.alphaF();
    return QColor(qRound(fg.red() * a + bg.red() * (1 - a)), qRound(fg.green() * a + bg.green() * (1 - a)), qRound(fg.blue() * a + bg.blue() * (1 - a)));
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor(qRound(a.red() + (b.red() - a.red()) * t), qRound(a.green() + (b.green() - a.green()) * t), qRound(a.blue() + (b.blue() - a.blue()) * t));
}

QColor withAlpha(QColor c, int alpha)
{
    c.setAlpha(alpha);
    return c;
}

QString rgbTriplet(const QColor &c)
{
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

// Les paires contrôlées : premier plan, fond, minimum. Texte 4,5:1 (WCAG 1.4.3), contours et
// repères 3:1 (WCAG 1.4.11).
struct Pair
{
    const char *fg;
    const char *bg;
    double min;
};
const Pair kPairs[] = {
    {"texte", "fond", 4.5},
    {"texte", "panneau", 4.5},
    {"texte", "contenu", 4.5},
    {"texte", "surface", 4.5},
    {"texte", "champ", 4.5},
    {"texte", "saisie", 4.5},
    {"texte_secondaire", "surface", 4.5},
    {"texte_secondaire", "panneau", 4.5},
    {"rouge", "surface", 4.5},
    {"rouge", "panneau", 4.5},
    {"texte_sur_accent", "accent", 4.5},
    {"texte_sur_accent", "selection", 4.5},
    {"bordure", "surface", 3.0},
    {"bordure", "panneau", 3.0},
    {"accent", "surface", 3.0},
    {"accent", "fond", 3.0},
    {"piste_video", "contenu", 3.0},
    {"piste_titre", "contenu", 3.0},
    {"piste_voix", "contenu", 3.0},
    {"piste_musique", "contenu", 3.0},
};

QString baseOf(const QMap<QString, QColor> &tokens)
{
    return KoraCutThemes::isDark(tokens) ? QStringLiteral("dark") : QStringLiteral("light");
}

QMap<QString, QColor> parseTokens(const QString &text)
{
    QMap<QString, QColor> out;
    const QStringList pairs = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &p : pairs) {
        const qsizetype sep = p.indexOf(QLatin1Char(':'));
        if (sep <= 0) {
            continue;
        }
        const QString key = p.left(sep).trimmed();
        const QColor c = KoraCutThemes::parseColor(p.mid(sep + 1).trimmed());
        if (c.isValid() && KoraCutThemes::tokenNames().contains(key)) {
            out.insert(key, c);
        }
    }
    return out;
}

QString tokensText(const QMap<QString, QColor> &tokens)
{
    QStringList parts;
    for (const QString &name : KoraCutThemes::tokenNames()) {
        if (tokens.contains(name)) {
            parts << name + QLatin1Char(':') + KoraCutThemes::colorText(tokens.value(name));
        }
    }
    return parts.join(QLatin1Char(','));
}

QString readTemplate()
{
    QFile f(QStringLiteral(":/koracut.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

QString camel(const QString &name)
{
    QString out;
    bool up = true;
    const QString decomposed = name.normalized(QString::NormalizationForm_D);
    for (const QChar ch : decomposed) {
        if (ch.isMark()) {
            continue;
        }
        if (ch.isLetterOrNumber() && ch.unicode() < 128) {
            out += up ? ch.toUpper() : ch;
            up = false;
        } else {
            up = true;
        }
    }
    return out.isEmpty() ? QStringLiteral("Theme") : out;
}

KSharedConfigPtr freshConfig(const QString &path)
{
    // Le fichier a pu être réécrit pendant la séance (theme_modifier) : KSharedConfig garde sa lecture.
    KSharedConfigPtr config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
    config->reparseConfiguration();
    return config;
}

QString fixedFamily()
{
    return KoraCutFonts::active() ? KoraCutFonts::codeFamily() : QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

} // namespace

KoraCutThemes *KoraCutThemes::self()
{
    static KoraCutThemes *instance = new KoraCutThemes(qApp);
    return instance;
}

KoraCutThemes::KoraCutThemes(QObject *parent)
    : QObject(parent)
{
    loadBuiltins();
}

QStringList KoraCutThemes::tokenNames()
{
    static const QStringList names = {
        QStringLiteral("fond"),          QStringLiteral("panneau"),        QStringLiteral("contenu"),          QStringLiteral("surface"),
        QStringLiteral("champ"),         QStringLiteral("saisie"),         QStringLiteral("champ_survol"),     QStringLiteral("poignee"),
        QStringLiteral("poignee_survol"), QStringLiteral("bordure"),       QStringLiteral("bordure_survol"),   QStringLiteral("voile"),
        QStringLiteral("texte"),         QStringLiteral("encre"),          QStringLiteral("texte_secondaire"), QStringLiteral("texte_desactive"),
        QStringLiteral("texte_sur_accent"), QStringLiteral("accent"),      QStringLiteral("accent_survol"),    QStringLiteral("accent_vif"),
        QStringLiteral("accent_presse"), QStringLiteral("accent_doux"),    QStringLiteral("selection"),        QStringLiteral("rouge"),
        QStringLiteral("vert"),          QStringLiteral("jaune"),          QStringLiteral("piste_video"),      QStringLiteral("piste_titre"),
        QStringLiteral("piste_voix"),    QStringLiteral("piste_musique")};
    return names;
}

QString KoraCutThemes::tokenLabel(const QString &name)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("fond"), QStringLiteral("fond de la fenêtre, entre les panneaux")},
        {QStringLiteral("panneau"), QStringLiteral("fond des panneaux Agents, Script, Console, Motion")},
        {QStringLiteral("contenu"), QStringLiteral("fond des listes et de la timeline")},
        {QStringLiteral("surface"), QStringLiteral("barres, menus, en-têtes, têtes de pistes")},
        {QStringLiteral("champ"), QStringLiteral("boutons, onglet actif, contrôles")},
        {QStringLiteral("saisie"), QStringLiteral("champs où l'on écrit")},
        {QStringLiteral("champ_survol"), QStringLiteral("bouton survolé")},
        {QStringLiteral("poignee"), QStringLiteral("barres de défilement")},
        {QStringLiteral("poignee_survol"), QStringLiteral("barre de défilement survolée")},
        {QStringLiteral("bordure"), QStringLiteral("contour des champs (3:1 contre la surface)")},
        {QStringLiteral("bordure_survol"), QStringLiteral("contour d'un champ survolé")},
        {QStringLiteral("voile"), QStringLiteral("teinte des survols et des filets translucides")},
        {QStringLiteral("texte"), QStringLiteral("texte principal (4,5:1 contre tous les fonds)")},
        {QStringLiteral("encre"), QStringLiteral("teinte des textes translucides")},
        {QStringLiteral("texte_secondaire"), QStringLiteral("textes secondaires, étiquettes")},
        {QStringLiteral("texte_desactive"), QStringLiteral("texte d'un contrôle grisé")},
        {QStringLiteral("texte_sur_accent"), QStringLiteral("texte posé sur l'accent et la sélection")},
        {QStringLiteral("accent"), QStringLiteral("couleur d'action : bouton principal, focus, outil actif")},
        {QStringLiteral("accent_survol"), QStringLiteral("accent survolé")},
        {QStringLiteral("accent_vif"), QStringLiteral("accent survolé des panneaux Agents et Console")},
        {QStringLiteral("accent_presse"), QStringLiteral("accent enfoncé")},
        {QStringLiteral("accent_doux"), QStringLiteral("bulle de la personne dans le panneau Agents")},
        {QStringLiteral("selection"), QStringLiteral("fond d'un texte ou d'une ligne sélectionnés")},
        {QStringLiteral("rouge"), QStringLiteral("erreurs, annuler")},
        {QStringLiteral("vert"), QStringLiteral("prêt, réussi")},
        {QStringLiteral("jaune"), QStringLiteral("attention")},
        {QStringLiteral("piste_video"), QStringLiteral("clips vidéo de la timeline")},
        {QStringLiteral("piste_titre"), QStringLiteral("clips de titre et Motion")},
        {QStringLiteral("piste_voix"), QStringLiteral("clips audio (voix)")},
        {QStringLiteral("piste_musique"), QStringLiteral("clips des pistes de musique")},
    };
    return labels.value(name);
}

QString KoraCutThemes::simplify(const QString &text)
{
    QString out;
    for (const QChar ch : text.normalized(QString::NormalizationForm_D)) {
        if (ch.isLetterOrNumber()) {
            out += ch.toLower();
        }
    }
    return out;
}

QColor KoraCutThemes::parseColor(const QString &text)
{
    const QString t = text.trimmed();
    static const QRegularExpression hex8(QStringLiteral("^#([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})$"));
    const QRegularExpressionMatch m = hex8.match(t);
    if (m.hasMatch()) {
        QColor c(QLatin1Char('#') + m.captured(1));
        c.setAlpha(m.captured(2).toInt(nullptr, 16));
        return c;
    }
    QColor c = QColor::fromString(t);
    if (!c.isValid() && !t.startsWith(QLatin1Char('#')) && t.size() == 6) {
        c = QColor::fromString(QLatin1Char('#') + t);
    }
    return c;
}

QString KoraCutThemes::colorText(const QColor &color)
{
    QString s = color.name(QColor::HexRgb).toUpper();
    if (color.alpha() != 255) {
        s += QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0')).toUpper();
    }
    return s;
}

double KoraCutThemes::contrast(const QColor &fg, const QColor &bg)
{
    const QColor f = fg.alpha() < 255 ? over(fg, bg) : fg;
    double a = luminance(f);
    double b = luminance(bg);
    if (a < b) {
        std::swap(a, b);
    }
    return (a + 0.05) / (b + 0.05);
}

bool KoraCutThemes::isDark(const QMap<QString, QColor> &tokens)
{
    return luminance(tokens.value(QStringLiteral("fond"), QColor(0x1E1E1E))) < 0.18;
}

QMap<QString, QColor> KoraCutThemes::derive(const QMap<QString, QColor> &given)
{
    // Les règles viennent de la charte : appliquées à Sombre, elles redonnent ses marches.
    QMap<QString, QColor> t = given;
    const auto def = [&t](const char *key, const QColor &value) {
        const QString k = QString::fromLatin1(key);
        if (!t.contains(k)) {
            t.insert(k, value);
        }
    };
    def("fond", QColor(0x1E1E1E));
    const bool dark = isDark(t);
    def("texte", dark ? QColor(0xFFFFFF) : QColor(0x1E1E1E));
    const QColor fond = t.value(QStringLiteral("fond"));
    const QColor texte = t.value(QStringLiteral("texte"));
    def("surface", dark ? mix(fond, texte, 0.06) : mix(fond, QColor(0xFFFFFF), 0.85));
    const QColor surface = t.value(QStringLiteral("surface"));
    def("panneau", fond);
    def("contenu", mix(fond, surface, 0.5));
    def("champ", mix(surface, texte, dark ? 0.06 : 0.04));
    def("saisie", dark ? t.value(QStringLiteral("champ")) : surface);
    def("champ_survol", mix(t.value(QStringLiteral("champ")), texte, 0.05));
    def("poignee", mix(surface, texte, dark ? 0.14 : 0.26));
    def("poignee_survol", mix(surface, texte, dark ? 0.22 : 0.37));
    def("bordure", mix(surface, texte, dark ? 0.36 : 0.39));
    def("bordure_survol", mix(surface, texte, dark ? 0.46 : 0.52));
    def("encre", texte);
    def("voile", texte);
    def("texte_secondaire", withAlpha(t.value(QStringLiteral("encre")), 158));
    def("texte_desactive", mix(surface, texte, 0.35));
    def("accent", charte().value(QStringLiteral("accent")));
    const QColor accent = t.value(QStringLiteral("accent"));
    def("accent_survol", mix(accent, QColor(0xFFFFFF), 0.12));
    def("accent_vif", mix(accent, QColor(0xFFFFFF), 0.18));
    def("accent_presse", mix(accent, QColor(Qt::black), 0.15));
    def("accent_doux", mix(surface, accent, dark ? 0.3 : 0.18));
    def("selection", accent);
    if (!t.contains(QStringLiteral("texte_sur_accent"))) {
        const QColor blanc(0xFFFFFF);
        const QColor noir(Qt::black);
        t.insert(QStringLiteral("texte_sur_accent"), contrast(blanc, accent) >= contrast(noir, accent) ? blanc : noir);
    }
    for (auto it = charte().constBegin(); it != charte().constEnd(); ++it) {
        if (!t.contains(it.key())) {
            t.insert(it.key(), it.value());
        }
    }
    return t;
}

QList<KoraCutThemes::Check> KoraCutThemes::contrastChecks(const QMap<QString, QColor> &tokens, const QStringList &explicitKeys)
{
    QList<Check> out;
    for (const Pair &p : kPairs) {
        const QString fg = QString::fromLatin1(p.fg);
        if (fg.startsWith(QLatin1String("piste_")) && (!explicitKeys.contains(fg) || tokens.value(fg) == charte().value(fg))) {
            continue;
        }
        Check c;
        c.fg = fg;
        c.bg = QString::fromLatin1(p.bg);
        c.minimum = p.min;
        c.ratio = contrast(tokens.value(c.fg), tokens.value(c.bg));
        c.ok = c.ratio + 1e-9 >= c.minimum;
        out << c;
    }
    return out;
}

QList<KoraCutThemes::Correction> KoraCutThemes::correct(QMap<QString, QColor> &explicitTokens, const QStringList &ignore)
{
    QList<Correction> done;
    const auto note = [&done](const QString &token, const QColor &before, const QColor &after, const Check &c) {
        for (Correction &d : done) {
            if (d.token == token) {
                d.after = after;
                return;
            }
        }
        done << Correction{token, before, after,
                           QStringLiteral("contraste %1:1 contre %2, il faut au moins %3:1")
                               .arg(QString::number(c.ratio, 'f', 2).replace(QLatin1Char('.'), QLatin1Char(',')), c.bg,
                                    QString::number(c.minimum, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')))};
    };
    // Éloigne @p fg de @p bg (vers le blanc sur un fond sombre, vers le noir sur un fond clair) jusqu'au minimum.
    const auto push = [](QColor fg, const QColor &bg, double min, bool *ok) -> QColor {
        const QColor cible = luminance(bg) < 0.18 ? QColor(0xFFFFFF) : QColor(Qt::black);
        while (fg.alpha() < 255 && contrast(fg, bg) < min) {
            fg.setAlpha(std::min(255, fg.alpha() + 12));
        }
        const QColor depart = fg;
        for (int step = 0; step <= 25 && contrast(fg, bg) < min; ++step) {
            fg = withAlpha(mix(depart, cible, step / 25.0), fg.alpha());
        }
        *ok = contrast(fg, bg) >= min;
        return fg;
    };
    bool frozen = false;
    for (int pass = 0; pass < 6; ++pass) {
        QMap<QString, QColor> full = derive(explicitTokens);
        const QList<Check> checks = contrastChecks(full, explicitTokens.keys());
        if (!frozen) {
            bool anyFailure = false;
            for (const Check &c : checks) {
                anyFailure = anyFailure || (!c.ok && !ignore.contains(c.fg + QLatin1Char('/') + c.bg));
            }
            if (anyFailure) {
                // Une palette qu'on corrige devient entièrement écrite : sinon les surfaces, dérivées
                // du texte, bougeraient avec lui et la correction courrait après sa propre cible.
                // Ce qui suit l'accent ou le texte reste dérivé : la sélection doit suivre un accent
                // corrigé, le texte secondaire un texte corrigé.
                static const QStringList followers = {QStringLiteral("selection"),     QStringLiteral("accent_survol"), QStringLiteral("accent_vif"),
                                                      QStringLiteral("accent_presse"), QStringLiteral("accent_doux"),   QStringLiteral("encre"),
                                                      QStringLiteral("voile"),         QStringLiteral("texte_secondaire")};
                for (auto it = full.constBegin(); it != full.constEnd(); ++it) {
                    if (!explicitTokens.contains(it.key()) && !followers.contains(it.key())) {
                        explicitTokens.insert(it.key(), it.value());
                    }
                }
                frozen = true;
            }
        }
        bool changed = false;
        for (const Check &c : checks) {
            if (c.ok || ignore.contains(c.fg + QLatin1Char('/') + c.bg)) {
                continue;
            }
            full = derive(explicitTokens);
            if (contrast(full.value(c.fg), full.value(c.bg)) >= c.minimum) {
                continue;
            }
            const QColor before = full.value(c.fg);
            if (c.fg == QLatin1String("texte_sur_accent")) {
                // Blanc ou noir d'abord ; si aucun ne suffit, c'est l'accent qui bouge.
                const QColor blanc(0xFFFFFF);
                const QColor noir(Qt::black);
                const QColor meilleur = contrast(blanc, full.value(c.bg)) >= contrast(noir, full.value(c.bg)) ? blanc : noir;
                if (contrast(meilleur, full.value(c.bg)) >= c.minimum) {
                    explicitTokens.insert(c.fg, meilleur);
                    note(c.fg, before, meilleur, c);
                } else {
                    bool ok = false;
                    const QColor bgBefore = full.value(c.bg);
                    const QColor bg = push(bgBefore, meilleur, c.minimum, &ok);
                    explicitTokens.insert(c.bg, bg);
                    explicitTokens.insert(c.fg, meilleur);
                    note(c.bg, bgBefore, bg, c);
                }
                changed = true;
                continue;
            }
            bool ok = false;
            const QColor after = push(before, full.value(c.bg), c.minimum, &ok);
            explicitTokens.insert(c.fg, after);
            note(c.fg, before, after, c);
            changed = true;
            if (!ok) {
                // Même le blanc ou le noir ne suffit pas : le fond est à mi-chemin, on le déplace.
                const QColor bgBefore = full.value(c.bg);
                bool bgOk = false;
                const QColor bg = push(bgBefore, after, c.minimum, &bgOk);
                explicitTokens.insert(c.bg, bg);
                note(c.bg, bgBefore, bg, c);
            }
        }
        if (!changed) {
            break;
        }
    }
    return done;
}

QString KoraCutThemes::renderStyleSheet(const QString &tmpl, const QMap<QString, QColor> &tokens, int rayon)
{
    const bool dark = isDark(tokens);
    static const QRegularExpression placeholder(QStringLiteral("\\{([a-z_]+)(?:@(\\d+)(?:/(\\d+))?)?\\}"));
    QString out;
    out.reserve(tmpl.size());
    qsizetype pos = 0;
    auto it = placeholder.globalMatch(tmpl);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(tmpl).mid(pos, m.capturedStart() - pos);
        pos = m.capturedEnd();
        const QString name = m.captured(1);
        if (name == QLatin1String("chevron")) {
            out += dark ? QStringLiteral("sombre") : QStringLiteral("clair");
            continue;
        }
        if (!tokens.contains(name)) {
            out += m.captured(0);
            continue;
        }
        QColor c = tokens.value(name);
        if (!m.captured(2).isEmpty()) {
            c.setAlpha((!dark && !m.captured(3).isEmpty()) ? m.captured(3).toInt() : m.captured(2).toInt());
        }
        if (c.alpha() == 255) {
            out += c.name(QColor::HexRgb).toUpper();
        } else {
            out += QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
        }
    }
    out += QStringView(tmpl).mid(pos);
    if (rayon >= 0 && rayon != 4) {
        // Les rayons de la charte sont pensés pour 4 px : on les met à l'échelle, à l'unité.
        static const QRegularExpression radius(QStringLiteral("(radius\\s*:)([^;}]*)"));
        static const QRegularExpression px(QStringLiteral("(\\d+)px"));
        QString scaled;
        qsizetype p = 0;
        auto rit = radius.globalMatch(out);
        while (rit.hasNext()) {
            const QRegularExpressionMatch m = rit.next();
            scaled += QStringView(out).mid(p, m.capturedStart(2) - p);
            QString value = m.captured(2);
            QString v2;
            qsizetype q = 0;
            auto pit = px.globalMatch(value);
            while (pit.hasNext()) {
                const QRegularExpressionMatch n = pit.next();
                v2 += QStringView(value).mid(q, n.capturedStart() - q);
                v2 += QStringLiteral("%1px").arg(qRound(n.captured(1).toInt() * rayon / 4.0));
                q = n.capturedEnd();
            }
            v2 += QStringView(value).mid(q);
            scaled += v2;
            p = m.capturedEnd(2);
        }
        scaled += QStringView(out).mid(p);
        out = scaled;
    }
    return out;
}

QString KoraCutThemes::schemeText(const QString &id, const QString &name, const QString &nameFr, const QMap<QString, QColor> &explicitTokens, const QString &police,
                                  int rayon, const QString &origine, const QString &inspiration)
{
    const QMap<QString, QColor> t = derive(explicitTokens);
    const bool dark = isDark(t);
    const auto c = [&t](const char *k) -> QString { return rgbTriplet(t.value(QString::fromLatin1(k))); };
    const QColor secondaire = over(t.value(QStringLiteral("texte_secondaire")), t.value(QStringLiteral("surface")));
    const QColor surAccent = t.value(QStringLiteral("texte_sur_accent"));
    const auto clair = [&surAccent](const QColor &x) -> QString { return rgbTriplet(mix(x, surAccent, 0.6)); };
    const auto group = [&](const QString &bg, const QString &alt, const QString &normal, const QString &inactive) -> QString {
        return QStringLiteral("BackgroundAlternate=%1\nBackgroundNormal=%2\nDecorationFocus=%3\nDecorationHover=%3\nForegroundActive=%3\nForegroundInactive=%4\n"
                              "ForegroundLink=%3\nForegroundNegative=%5\nForegroundNeutral=%6\nForegroundNormal=%7\nForegroundPositive=%8\nForegroundVisited=%9\n")
            .arg(alt, bg, c("accent"), inactive, c("rouge"), c("jaune"), normal, c("vert"), c("piste_titre"));
    };
    const QString sec = rgbTriplet(secondaire);
    QString s;
    s += QStringLiteral("# SPDX-License-Identifier: LGPL-2.0-or-later\n");
    s += QStringLiteral("# %1 : thème KoraCut écrit par le moteur de thèmes (src/koracut/koracutthemes.cpp).\n").arg(nameFr.isEmpty() ? name : nameFr);
    s += QStringLiteral("# La section [KoraCut] fait foi : la palette ci-dessous en est tirée.\n\n");
    s += QStringLiteral("[ColorEffects:Disabled]\nColor=%1\nColorAmount=0\nColorEffect=0\nContrastAmount=0.65\nContrastEffect=1\nIntensityAmount=0.1\nIntensityEffect=2\n\n")
             .arg(c("champ"));
    s += QStringLiteral("[ColorEffects:Inactive]\nChangeSelectionColor=true\nColor=112,111,110\nColorAmount=0.025\nColorEffect=2\nContrastAmount=0.1\nContrastEffect=2\n"
                        "Enable=false\nIntensityAmount=0\nIntensityEffect=0\n\n");
    s += QStringLiteral("[Colors:Button]\n") + group(c("champ"), rgbTriplet(mix(t.value(QStringLiteral("surface")), t.value(QStringLiteral("champ")), 0.33)), c("texte"), sec) +
        QLatin1Char('\n');
    s += QStringLiteral("[Colors:Complementary]\n") + group(c("fond"), c("surface"), c("texte"), sec) + QLatin1Char('\n');
    s += QStringLiteral("[Colors:Header]\n") + group(c("surface"), c("fond"), c("texte"), sec) + QLatin1Char('\n');
    s += QStringLiteral("[Colors:Header][Inactive]\n") + group(c("surface"), c("fond"), sec, c("texte_desactive")) + QLatin1Char('\n');
    s += QStringLiteral("[Colors:Selection]\nBackgroundAlternate=%1\nBackgroundNormal=%2\nDecorationFocus=%3\nDecorationHover=%3\nForegroundActive=%4\n"
                        "ForegroundInactive=%5\nForegroundLink=%5\nForegroundNegative=%6\nForegroundNeutral=%7\nForegroundNormal=%4\nForegroundPositive=%8\n"
                        "ForegroundVisited=%9\n\n")
             .arg(rgbTriplet(mix(t.value(QStringLiteral("surface")), t.value(QStringLiteral("selection")), 0.2)), c("selection"), c("accent"), rgbTriplet(surAccent),
                  clair(t.value(QStringLiteral("selection"))), clair(t.value(QStringLiteral("rouge"))), clair(t.value(QStringLiteral("jaune"))),
                  clair(t.value(QStringLiteral("vert"))), clair(t.value(QStringLiteral("piste_titre"))));
    s += QStringLiteral("[Colors:Tooltip]\n") + group(c("fond"), c("surface"), c("texte"), sec) + QLatin1Char('\n');
    s += QStringLiteral("[Colors:View]\n") + group(c("contenu"), c("surface"), c("texte"), sec) + QLatin1Char('\n');
    s += QStringLiteral("[Colors:Window]\n") + group(c("surface"), c("fond"), c("texte"), sec) + QLatin1Char('\n');
    s += QStringLiteral("[General]\nColorScheme=%1\nName=%2\n").arg(id, name);
    if (!nameFr.isEmpty() && nameFr != name) {
        s += QStringLiteral("Name[fr]=%1\n").arg(nameFr);
    }
    s += QStringLiteral("shadeSortColumn=true\nTitlebarIsAccentColored=false\naccentActiveTitlebar=false\naccentInactiveTitlebar=false\n\n");
    s += QStringLiteral("[KDE]\ncontrast=4\n\n");
    s += QStringLiteral("[WM]\nactiveBackground=%1\nactiveBlend=%1\nactiveForeground=%2\ninactiveBackground=%1\ninactiveBlend=%1\ninactiveForeground=%3\n\n")
             .arg(c("surface"), c("texte"), sec);
    s += QStringLiteral("[KoraCut]\nBase=%1\nJetons=%2\n").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"), tokensText(explicitTokens));
    if (!police.isEmpty()) {
        s += QStringLiteral("Police=%1\n").arg(police);
    }
    if (rayon >= 0) {
        s += QStringLiteral("Rayon=%1\n").arg(rayon);
    }
    s += QStringLiteral("Origine=%1\n").arg(origine);
    if (!inspiration.isEmpty()) {
        QString line = inspiration;
        line.replace(QLatin1Char('\n'), QLatin1Char(' '));
        s += QStringLiteral("Inspiration=%1\n").arg(line);
    }
    return s;
}

void KoraCutThemes::loadBuiltins()
{
    QFile f(QStringLiteral(":/koracut-themes.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "KoraCut: koracut-themes.json not found in resources";
        return;
    }
    const QJsonArray themes = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("themes")).toArray();
    for (const QJsonValue &v : themes) {
        const QJsonObject o = v.toObject();
        Builtin b;
        b.slug = o.value(QStringLiteral("id")).toString();
        b.id = o.value(QStringLiteral("schema")).toString();
        b.name = o.value(QStringLiteral("nom")).toString();
        b.nameFr = o.value(QStringLiteral("nom_fr")).toString();
        b.base = o.value(QStringLiteral("base")).toString() == QLatin1String("clair") ? QStringLiteral("light") : QStringLiteral("dark");
        b.police = o.value(QStringLiteral("police")).toString();
        b.rayon = o.value(QStringLiteral("rayon")).toInt(-1);
        b.inspiration = o.value(QStringLiteral("inspiration")).toString();
        const QJsonObject jetons = o.value(QStringLiteral("jetons")).toObject();
        for (auto it = jetons.constBegin(); it != jetons.constEnd(); ++it) {
            b.tokens.insert(it.key(), parseColor(it.value().toString()));
        }
        for (const QJsonValue &e : o.value(QStringLiteral("ecarts_charte")).toArray()) {
            b.accepted << e.toString();
        }
        m_builtins << b;
    }
}

const KoraCutThemes::Builtin *KoraCutThemes::builtin(const QString &id) const
{
    for (const Builtin &b : m_builtins) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

void KoraCutThemes::installBuiltins()
{
    const QString folder = UiUtils::userThemesFolder();
    QDir().mkpath(folder);
    for (const Builtin &b : m_builtins) {
        // Sombre et Clair ont leur .colors écrit à la main, copié par main.cpp : on n'y touche pas.
        if (b.id == QLatin1String("KoraCutDark") || b.id == QLatin1String("KoraCutLight")) {
            continue;
        }
        const QString text = schemeText(b.id, b.name, b.nameFr, b.tokens, b.police, b.rayon, QStringLiteral("fourni"), b.inspiration);
        const QString path = folder + QLatin1Char('/') + b.id + QStringLiteral(".colors");
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            if (QString::fromUtf8(existing.readAll()) == text) {
                continue;
            }
            existing.close();
        }
        QSaveFile out(path);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(text.toUtf8());
            out.commit();
        }
    }
}

QString KoraCutThemes::fileFor(const QString &id) const
{
    return UiUtils::themeFile(id);
}

QString KoraCutThemes::police(const QString &id) const
{
    if (const Builtin *b = builtin(id)) {
        return b->police;
    }
    const QString path = fileFor(id);
    return path.isEmpty() ? QString() : KConfigGroup(freshConfig(path), QStringLiteral("KoraCut")).readEntry("Police", QString());
}

int KoraCutThemes::rayon(const QString &id) const
{
    if (const Builtin *b = builtin(id)) {
        return b->rayon;
    }
    const QString path = fileFor(id);
    return path.isEmpty() ? -1 : KConfigGroup(freshConfig(path), QStringLiteral("KoraCut")).readEntry("Rayon", -1);
}

QMap<QString, QColor> KoraCutThemes::explicitTokens(const QString &id) const
{
    if (const Builtin *b = builtin(id)) {
        return b->tokens;
    }
    const QString path = fileFor(id);
    if (path.isEmpty()) {
        return {};
    }
    KSharedConfigPtr config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
    config->reparseConfiguration();
    const KConfigGroup g(config, QStringLiteral("KoraCut"));
    if (g.hasKey("Jetons")) {
        return parseTokens(g.readEntry("Jetons", QString()));
    }
    // Ancien format : la palette de base et ses remplacements de couleurs.
    const Builtin *base = builtin(g.readEntry("Base", QString()) == QLatin1String("light") ? QStringLiteral("KoraCutLight") : QStringLiteral("KoraCutDark"));
    QMap<QString, QColor> t = base ? base->tokens : QMap<QString, QColor>();
    const QStringList pairs = g.readEntry("Remplacements", QString()).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &p : pairs) {
        const QStringList two = p.split(QLatin1Char(':'));
        if (two.size() != 2) {
            continue;
        }
        const QColor from = parseColor(two.at(0));
        const QColor to = parseColor(two.at(1));
        for (auto it = t.begin(); it != t.end(); ++it) {
            if (it.value().rgb() == from.rgb()) {
                it.value() = withAlpha(to, it.value().alpha());
            }
        }
    }
    return t;
}

QMap<QString, QColor> KoraCutThemes::tokens(const QString &id) const
{
    return derive(explicitTokens(id));
}

QList<KoraCutThemes::Check> KoraCutThemes::checks(const QString &id) const
{
    const QMap<QString, QColor> given = explicitTokens(id);
    QList<Check> out = contrastChecks(derive(given), given.keys());
    if (const Builtin *b = builtin(id)) {
        for (Check &c : out) {
            c.accepted = !c.ok && b->accepted.contains(c.fg + QLatin1Char('/') + c.bg);
        }
    }
    return out;
}

void KoraCutThemes::applyScheme(const QString &path, const QString &id)
{
    if (!g_hasDefaultFont && qApp) {
        g_defaultFont = QApplication::font();
        // La police de l'interface est la police embarquée (koracutfonts.h), même si ce premier appel
        // arrive avant que main() ne l'ait posée : revenir d'un thème à chasse fixe doit la retrouver.
        if (KoraCutFonts::active()) {
            g_defaultFont = QFont(KoraCutFonts::uiFamily());
            g_defaultFont.setStyleHint(QFont::SansSerif);
        }
        g_hasDefaultFont = true;
    }
    QString base;
    QMap<QString, QColor> given;
    QString policeName;
    int rayonValue = -1;
    QString remplacements;
    if (path.isEmpty()) {
        base = QStringLiteral("dark");
        if (const Builtin *b = builtin(QStringLiteral("KoraCutDark"))) {
            given = b->tokens;
        }
    } else {
        KSharedConfigPtr config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
        config->reparseConfiguration();
        const KConfigGroup g(config, QStringLiteral("KoraCut"));
        base = g.readEntry("Base", QString());
        if (const Builtin *b = builtin(id); b && !g.hasKey("Jetons")) {
            given = b->tokens;
            policeName = b->police;
            rayonValue = b->rayon;
        } else if (g.hasKey("Jetons")) {
            given = parseTokens(g.readEntry("Jetons", QString()));
            policeName = g.readEntry("Police", QString());
            rayonValue = g.readEntry("Rayon", -1);
        } else if (!base.isEmpty()) {
            const Builtin *b0 = builtin(base == QLatin1String("light") ? QStringLiteral("KoraCutLight") : QStringLiteral("KoraCutDark"));
            given = b0 ? b0->tokens : QMap<QString, QColor>();
            remplacements = g.readEntry("Remplacements", QString());
        }
    }
    g_current.clear();
    g_tracks.clear();
    g_currentId = id;
    QString sheet;
    if (!base.isEmpty()) {
        const QMap<QString, QColor> full = derive(given);
        for (auto it = full.constBegin(); it != full.constEnd(); ++it) {
            g_current.insert(it.key(), it.value());
        }
        for (const QString &k : {QStringLiteral("piste_video"), QStringLiteral("piste_titre"), QStringLiteral("piste_voix"), QStringLiteral("piste_musique")}) {
            if (given.contains(k) && given.value(k) != charte().value(k)) {
                g_tracks.insert(k, given.value(k));
            }
        }
        sheet = renderStyleSheet(readTemplate(), full, rayonValue);
        if (!remplacements.isEmpty()) {
            // Ancien format : les remplacements s'appliquent sur la feuille rendue, en une passe.
            QHash<QString, QString> table;
            for (const QString &p : remplacements.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const QStringList two = p.trimmed().split(QLatin1Char(':'));
                if (two.size() == 2) {
                    table.insert(two.at(0).trimmed().toUpper(), two.at(1).trimmed());
                }
            }
            static const QRegularExpression hexa(QStringLiteral("#[0-9A-Fa-f]{6}\\b"));
            QString out;
            qsizetype debut = 0;
            auto it = hexa.globalMatch(sheet);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                out += QStringView(sheet).mid(debut, m.capturedStart() - debut);
                out += table.value(m.captured().toUpper(), m.captured());
                debut = m.capturedEnd();
            }
            out += QStringView(sheet).mid(debut);
            sheet = out;
        }
    }
    if (qApp) {
        // La police avant la feuille : les règles de la feuille qui fixent une taille figent la
        // famille en cours au moment où la feuille est posée (vu le 25 septembre 2026 : Spider-Man
        // gardait la chasse fixe de Matrix).
        if (!qEnvironmentVariableIsSet("KORACUT_NO_FONT")) {
            // On part d'un QFont complet, pas d'un nom de famille : la police du système sur macOS
            // (« .AppleSystemUIFont ») ne se retrouve pas par son nom. La taille en cours est gardée.
            const QFont current = QApplication::font();
            QFont f = g_defaultFont;
            if (policeName == QLatin1String("mono")) {
                f = KoraCutFonts::code();
            } else if (!policeName.isEmpty() && QFontDatabase::hasFamily(policeName)) {
                f = g_defaultFont;
                f.setFamilies({policeName});
            }
            if (current.pixelSize() > 0) {
                f.setPixelSize(current.pixelSize());
            } else {
                f.setPointSizeF(current.pointSizeF());
            }
            if (!(f == current)) {
                QApplication::setFont(f);
            }
        }
        qApp->setStyleSheet(sheet);
    }
    Q_EMIT themeChanged(id);
}

QColor KoraCutThemes::token(const QString &name, int alpha)
{
    QColor c = g_current.value(name);
    if (!c.isValid()) {
        c = derive({}).value(name);
    }
    if (alpha >= 0) {
        c.setAlpha(alpha);
    }
    return c;
}

QColor KoraCutThemes::trackColor(const QString &name, const QColor &fallback)
{
    return g_tracks.value(name, fallback);
}

QString KoraCutThemes::mode() const
{
    return KdenliveSettings::koracut_theme_mode();
}

QString KoraCutThemes::currentId() const
{
    if (!g_currentId.isEmpty()) {
        return g_currentId;
    }
    return KColorSchemeManager::instance()->activeSchemeId();
}

QString KoraCutThemes::previous() const
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("themePrecedent", QString());
}

QList<KoraCutThemes::Theme> KoraCutThemes::list() const
{
    QList<Theme> out;
    QSet<QString> seen;
    const QString current = currentId();
    const auto fill = [](Theme &t, const QMap<QString, QColor> &full) {
        for (const QString &k : {QStringLiteral("fond"), QStringLiteral("surface"), QStringLiteral("texte"), QStringLiteral("accent")}) {
            t.preview.insert(k, full.value(k));
        }
    };
    const bool francais = KLocalizedString::languages().value(0).startsWith(QLatin1String("fr"));
    for (const Builtin &b : m_builtins) {
        Theme t;
        t.id = b.id;
        t.slug = b.slug;
        t.name = francais ? b.nameFr : b.name;
        t.base = b.base;
        t.builtin = true;
        t.file = UiUtils::themeFile(b.id);
        t.inspiration = b.inspiration;
        t.active = b.id == current;
        fill(t, derive(b.tokens));
        out << t;
        seen.insert(b.id);
    }
    // Les thèmes du dossier de l'utilisateur (créés, installés) et ceux que le gestionnaire connaît.
    QStringList files;
    const QDir folder(UiUtils::userThemesFolder());
    for (const QString &f : folder.entryList({QStringLiteral("*.colors")}, QDir::Files, QDir::Name)) {
        files << folder.absoluteFilePath(f);
    }
    QAbstractItemModel *model = KColorSchemeManager::instance()->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QString p = model->index(row, 0).data(KColorSchemeModel::PathRole).toString();
        if (!p.isEmpty() && !files.contains(p)) {
            files << p;
        }
    }
    for (const QString &p : std::as_const(files)) {
        const QString id = QFileInfo(p).completeBaseName();
        if (seen.contains(id)) {
            continue;
        }
        const KConfigGroup g(freshConfig(p), QStringLiteral("KoraCut"));
        if (!g.hasKey("Base")) {
            continue;
        }
        seen.insert(id);
        Theme t;
        t.id = id;
        t.name = UiUtils::themeName(p, id);
        t.slug = simplify(t.name);
        t.base = g.readEntry("Base", QString());
        t.created = g.readEntry("Origine", QString()) == QLatin1String("cree");
        t.file = p;
        t.inspiration = g.readEntry("Inspiration", QString());
        t.active = id == current;
        fill(t, tokens(id));
        out << t;
    }
    return out;
}

QString KoraCutThemes::resolve(const QString &name) const
{
    const QString s = simplify(name);
    if (s.isEmpty()) {
        return {};
    }
    const QList<Theme> all = list();
    for (const Theme &t : all) {
        if (simplify(t.id) == s || simplify(t.slug) == s || simplify(t.name) == s) {
            return t.id;
        }
    }
    for (const Builtin &b : m_builtins) {
        if (simplify(b.name) == s || simplify(b.nameFr) == s) {
            return b.id;
        }
    }
    // « theme matrix », « le thème Spider-Man » : on essaie sans les mots d'appel.
    for (const Theme &t : all) {
        const QString n = simplify(t.name);
        if (n.size() >= 4 && (s.endsWith(n) || s.endsWith(simplify(t.slug)))) {
            return t.id;
        }
    }
    return {};
}

bool KoraCutThemes::setMode(const QString &mode, QString *error)
{
    Q_UNUSED(error)
    const QString before = KdenliveSettings::koracut_theme_mode();
    if (before != mode) {
        KConfigGroup g = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
        g.writeEntry("themePrecedent", before);
        g.sync();
    }
    KdenliveSettings::setKoracut_theme_mode(mode);
    KdenliveSettings::self()->save();
    UiUtils::applyThemeMode();
    return true;
}

bool KoraCutThemes::apply(const QString &name, QString *error)
{
    const QString s = simplify(name);
    static const QHash<QString, QString> modes = {
        {QStringLiteral("sombre"), QStringLiteral("dark")},      {QStringLiteral("dark"), QStringLiteral("dark")},
        {QStringLiteral("modesombre"), QStringLiteral("dark")},  {QStringLiteral("clair"), QStringLiteral("light")},
        {QStringLiteral("light"), QStringLiteral("light")},      {QStringLiteral("modeclair"), QStringLiteral("light")},
        {QStringLiteral("systeme"), QStringLiteral("system")},   {QStringLiteral("system"), QStringLiteral("system")},
        {QStringLiteral("auto"), QStringLiteral("system")},      {QStringLiteral("suivrelesysteme"), QStringLiteral("system")},
    };
    if (s == QLatin1String("precedent") || s == QLatin1String("previous") || s == QLatin1String("avant")) {
        return revert(error);
    }
    if (modes.contains(s)) {
        return setMode(modes.value(s), error);
    }
    const QString id = resolve(name);
    if (id.isEmpty()) {
        if (error) {
            QStringList names;
            for (const Theme &t : list()) {
                names << t.slug;
            }
            *error = QStringLiteral("Aucun thème ne s'appelle « %1 ». Thèmes : sombre, clair, systeme, %2.").arg(name, names.join(QStringLiteral(", ")));
        }
        return false;
    }
    // Sombre et Clair passent par le mode, comme le menu Affichage > Thème.
    if (id == QLatin1String("KoraCutDark")) {
        return setMode(QStringLiteral("dark"), error);
    }
    if (id == QLatin1String("KoraCutLight")) {
        return setMode(QStringLiteral("light"), error);
    }
    return setMode(id, error);
}

bool KoraCutThemes::revert(QString *error)
{
    const QString before = previous();
    if (before.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Aucun thème précédent n'est connu.");
        }
        return false;
    }
    return setMode(before, error);
}

QString KoraCutThemes::create(const QString &name, const QMap<QString, QString> &tokens, const QString &inspiration, const QString &police, int rayon,
                              QList<Correction> *corrections, QString *error, const QStringList &ignore)
{
    const auto fail = [error](const QString &message) -> QString {
        if (error) {
            *error = message;
        }
        return QString();
    };
    if (simplify(name).isEmpty()) {
        return fail(QStringLiteral("Le thème a besoin d'un nom."));
    }
    QMap<QString, QColor> given;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        if (!tokenNames().contains(it.key())) {
            return fail(QStringLiteral("Le jeton « %1 » n'existe pas. Jetons : %2.").arg(it.key(), tokenNames().join(QStringLiteral(", "))));
        }
        const QColor c = parseColor(it.value());
        if (!c.isValid()) {
            return fail(QStringLiteral("La couleur « %1 » (%2) ne se lit pas : « #RRGGBB » attendu.").arg(it.value(), it.key()));
        }
        given.insert(it.key(), c);
    }
    if (!given.contains(QStringLiteral("fond")) && !given.contains(QStringLiteral("accent"))) {
        return fail(QStringLiteral("Donne au moins le fond ou l'accent."));
    }
    QString policeName = police.trimmed();
    if (!policeName.isEmpty() && policeName != QLatin1String("mono") && !QFontDatabase::hasFamily(policeName)) {
        return fail(QStringLiteral("La police « %1 » n'est pas installée. Mets « mono » pour la police à chasse fixe du système, ou laisse vide.").arg(policeName));
    }
    if (rayon > 12) {
        rayon = 12;
    }
    // Un nom déjà pris par un thème livré : le thème créé porte « perso » en plus.
    QString finalName = name.trimmed();
    for (const Builtin &b : m_builtins) {
        if (simplify(b.name) == simplify(finalName) || simplify(b.nameFr) == simplify(finalName) || simplify(b.slug) == simplify(finalName)) {
            finalName += QStringLiteral(" perso");
            break;
        }
    }
    const QList<Correction> done = correct(given, ignore);
    if (corrections) {
        *corrections = done;
    }
    const QString id = QStringLiteral("KoraCutPerso") + camel(finalName);
    const QString folder = UiUtils::userThemesFolder();
    QDir().mkpath(folder);
    QSaveFile out(folder + QLatin1Char('/') + id + QStringLiteral(".colors"));
    if (!out.open(QIODevice::WriteOnly)) {
        return fail(QStringLiteral("Impossible d'écrire dans %1.").arg(folder));
    }
    out.write(schemeText(id, finalName, QString(), given, policeName, rayon, QStringLiteral("cree"), inspiration).toUtf8());
    if (!out.commit()) {
        return fail(QStringLiteral("Impossible d'écrire dans %1.").arg(folder));
    }
    Q_EMIT listChanged();
    // Un thème créé n'est pas connu du gestionnaire de schémas : applyThemeMode le pose depuis son
    // fichier, même s'il était déjà à l'écran (cas d'une modification).
    setMode(id, error);
    if (KColorSchemeManager::instance()->indexForSchemeId(id).isValid() && mode() == id) {
        // Connu du gestionnaire (créé lors d'une séance d'avant) : il ne reposerait rien.
        UiUtils::applyThemeFile(UiUtils::themeFile(id));
    }
    return id;
}

QString KoraCutThemes::modify(const QString &name, const QMap<QString, QString> &changes, QList<Correction> *corrections, QString *error)
{
    const QString id = resolve(name);
    if (id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Aucun thème ne s'appelle « %1 ».").arg(name);
        }
        return {};
    }
    QMap<QString, QColor> given = explicitTokens(id);
    QString policeName = police(id);
    int rayonValue = rayon(id);
    QMap<QString, QString> colors;
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        if (it.key() == QLatin1String("police")) {
            policeName = it.value();
        } else if (it.key() == QLatin1String("rayon")) {
            rayonValue = it.value().isEmpty() ? -1 : it.value().toInt();
        } else {
            colors.insert(it.key(), it.value());
        }
    }
    QMap<QString, QString> all;
    for (auto it = given.constBegin(); it != given.constEnd(); ++it) {
        all.insert(it.key(), colorText(it.value()));
    }
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
        all.insert(it.key(), it.value());
    }
    QString themeName;
    QString inspiration;
    QStringList ignore;
    if (const Builtin *b = builtin(id)) {
        themeName = KLocalizedString::languages().value(0).startsWith(QLatin1String("fr")) ? b->nameFr : b->name;
        inspiration = b->inspiration;
        // Les écarts connus de la charte restent tels quels sur la copie, sauf si le changement les touche.
        for (const QString &pair : b->accepted) {
            const QStringList two = pair.split(QLatin1Char('/'));
            if (!colors.contains(two.value(0)) && !colors.contains(two.value(1))) {
                ignore << pair;
            }
        }
    } else {
        const QString path = fileFor(id);
        themeName = UiUtils::themeName(path, id);
        inspiration = KConfigGroup(freshConfig(path), QStringLiteral("KoraCut")).readEntry("Inspiration", QString());
        const bool createdTheme = KConfigGroup(freshConfig(path), QStringLiteral("KoraCut")).readEntry("Origine", QString()) ==
            QLatin1String("cree");
        if (!createdTheme) {
            themeName += QStringLiteral(" perso");
        }
    }
    return create(themeName, all, inspiration, policeName, rayonValue, corrections, error, ignore);
}

bool KoraCutThemes::remove(const QString &name, QString *error)
{
    const QString id = resolve(name);
    const QString path = id.isEmpty() ? QString() : fileFor(id);
    if (id.isEmpty() || path.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Aucun thème ne s'appelle « %1 ».").arg(name);
        }
        return false;
    }
    const bool createdTheme =
        !builtin(id) && KConfigGroup(freshConfig(path), QStringLiteral("KoraCut")).readEntry("Origine", QString()) == QLatin1String("cree");
    if (!createdTheme) {
        if (error) {
            *error = QStringLiteral("« %1 » est un thème livré ou installé : seuls les thèmes créés se suppriment.").arg(name);
        }
        return false;
    }
    const bool wasActive = currentId() == id || mode() == id;
    if (!QFile::remove(path)) {
        if (error) {
            *error = QStringLiteral("Le fichier %1 n'a pas pu être supprimé.").arg(path);
        }
        return false;
    }
    if (wasActive) {
        const QString before = previous();
        setMode(before.isEmpty() || before == id ? QStringLiteral("dark") : before, error);
    }
    Q_EMIT listChanged();
    return true;
}
