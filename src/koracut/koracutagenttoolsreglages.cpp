/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// 25 septembre 2026 : le thème et les réglages de l'application, pour les agents et la porte de
// commande. Contrat : Agents in-app/outils/reglages.json. Rien ici ne touche au projet : pas
// d'étape d'annulation, mais chaque changement est réversible (thème précédent, valeur « avant »).

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "koracutthemes.h"
#include "koracutvisible.h"

#include "bin/bin.h"
#include "core.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "project/projectmanager.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QAction>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>

namespace KoraCutAgentToolsPrivate {

namespace {

QJsonObject paletteJson(const QMap<QString, QColor> &tokens)
{
    QJsonObject out;
    for (const QString &name : KoraCutThemes::tokenNames()) {
        if (tokens.contains(name)) {
            out.insert(name, KoraCutThemes::colorText(tokens.value(name)));
        }
    }
    return out;
}

QString baseName(const QString &base)
{
    return base == QLatin1String("light") ? QStringLiteral("clair") : QStringLiteral("sombre");
}

QJsonObject activeJson()
{
    const QString id = KoraCutThemes::self()->currentId();
    // Le thème précédent, sous le nom qu'un agent peut repasser à theme_appliquer.
    QString previous = KoraCutThemes::self()->previous();
    static const QHash<QString, QString> modes = {
        {QStringLiteral("dark"), QStringLiteral("sombre")}, {QStringLiteral("light"), QStringLiteral("clair")}, {QStringLiteral("system"), QStringLiteral("systeme")}};
    QJsonObject out{{QStringLiteral("actif"), id}, {QStringLiteral("mode"), KoraCutThemes::self()->mode()}};
    for (const KoraCutThemes::Theme &t : KoraCutThemes::self()->list()) {
        if (t.id == id) {
            out.insert(QStringLiteral("actif"), t.slug);
            out.insert(QStringLiteral("titre"), t.name);
        }
        if (t.id == previous) {
            previous = t.slug;
        }
    }
    out.insert(QStringLiteral("precedent"), modes.value(previous, previous));
    return out;
}

QJsonObject checksJson(const QList<KoraCutThemes::Check> &checks)
{
    int passed = 0;
    QJsonArray failed;
    for (const KoraCutThemes::Check &c : checks) {
        if (c.ok) {
            ++passed;
        } else {
            failed.append(QJsonObject{{QStringLiteral("texte"), c.fg},
                                      {QStringLiteral("fond"), c.bg},
                                      {QStringLiteral("rapport"), qRound(c.ratio * 100) / 100.0},
                                      {QStringLiteral("minimum"), c.minimum},
                                      {QStringLiteral("ecart_charte"), c.accepted}});
        }
    }
    QJsonObject out{{QStringLiteral("passes"), passed}, {QStringLiteral("total"), int(checks.size())}};
    if (!failed.isEmpty()) {
        out.insert(QStringLiteral("en_dessous"), failed);
    }
    return out;
}

QJsonObject themeLister(const Timeline &, const Args &args)
{
    QJsonArray themes;
    for (const KoraCutThemes::Theme &t : KoraCutThemes::self()->list()) {
        QJsonObject apercu;
        for (auto it = t.preview.constBegin(); it != t.preview.constEnd(); ++it) {
            apercu.insert(it.key(), KoraCutThemes::colorText(it.value()));
        }
        QJsonObject o{{QStringLiteral("nom"), t.slug},  {QStringLiteral("id"), t.id},           {QStringLiteral("titre"), t.name},
                      {QStringLiteral("base"), baseName(t.base)}, {QStringLiteral("livre"), t.builtin}, {QStringLiteral("cree"), t.created},
                      {QStringLiteral("actif"), t.active},  {QStringLiteral("apercu"), apercu}};
        if (!t.inspiration.isEmpty()) {
            o.insert(QStringLiteral("inspiration"), t.inspiration);
        }
        themes.append(o);
    }
    QJsonArray jetons;
    for (const QString &name : KoraCutThemes::tokenNames()) {
        jetons.append(QJsonObject{{QStringLiteral("nom"), name}, {QStringLiteral("role"), KoraCutThemes::tokenLabel(name)}});
    }
    QJsonObject out = activeJson();
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("themes"), themes);
    out.insert(QStringLiteral("jetons"), jetons);
    if (args.value(QStringLiteral("avec_palette")).toBool()) {
        const QString id = KoraCutThemes::self()->currentId();
        out.insert(QStringLiteral("palette"), paletteJson(KoraCutThemes::self()->tokens(id)));
        out.insert(QStringLiteral("controles"), checksJson(KoraCutThemes::self()->checks(id)));
        const QString police = KoraCutThemes::self()->police(id);
        if (!police.isEmpty()) {
            out.insert(QStringLiteral("police"), police);
        }
        if (KoraCutThemes::self()->rayon(id) >= 0) {
            out.insert(QStringLiteral("rayon"), KoraCutThemes::self()->rayon(id));
        }
    }
    return out;
}

QJsonObject themeAppliquer(const Timeline &, const Args &args)
{
    const QString name = args.value(QStringLiteral("nom")).toString();
    if (name.trimmed().isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("nom est vide."), QStringLiteral("clair, sombre, systeme, precedent, ou un nom de theme_lister."));
    }
    if (KoraCutThemes::simplify(name) == QLatin1String("precedent") && KoraCutThemes::self()->previous().isEmpty()) {
        return error(QStringLiteral("rien_a_annuler"), QStringLiteral("Aucun thème précédent n'est connu."), QStringLiteral("theme_lister donne le thème actif."));
    }
    QString failure;
    if (!KoraCutThemes::self()->apply(name, &failure)) {
        return error(QStringLiteral("theme_inconnu"), failure, QStringLiteral("theme_lister donne les noms ; theme_creer en fait un nouveau."));
    }
    QJsonObject out = activeJson();
    out.insert(QStringLiteral("ok"), true);
    return out;
}

QJsonArray correctionsJson(const QList<KoraCutThemes::Correction> &corrections)
{
    QJsonArray out;
    for (const KoraCutThemes::Correction &c : corrections) {
        out.append(QJsonObject{{QStringLiteral("jeton"), c.token},
                               {QStringLiteral("avant"), KoraCutThemes::colorText(c.before)},
                               {QStringLiteral("apres"), KoraCutThemes::colorText(c.after)},
                               {QStringLiteral("raison"), c.reason}});
    }
    return out;
}

bool readColors(const Args &args, QMap<QString, QString> &colors, QJsonObject &failure)
{
    const QJsonValue v = args.value(QStringLiteral("couleurs"));
    if (!v.isUndefined() && !v.isObject()) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("couleurs doit être un objet { jeton: \"#RRGGBB\" }."));
        return false;
    }
    const QJsonObject o = v.toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (!it.value().isString()) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("La couleur de %1 doit être un texte « #RRGGBB ».").arg(it.key()));
            return false;
        }
        colors.insert(it.key(), it.value().toString());
    }
    return true;
}

QJsonObject themeResult(const QString &id, const QList<KoraCutThemes::Correction> &corrections)
{
    QJsonObject out = activeJson();
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("id"), id);
    for (const KoraCutThemes::Theme &t : KoraCutThemes::self()->list()) {
        if (t.id == id) {
            out.insert(QStringLiteral("nom"), t.slug);
            out.insert(QStringLiteral("titre"), t.name);
        }
    }
    out.insert(QStringLiteral("corrections"), correctionsJson(corrections));
    out.insert(QStringLiteral("controles"), checksJson(KoraCutThemes::self()->checks(id)));
    out.insert(QStringLiteral("palette"), paletteJson(KoraCutThemes::self()->tokens(id)));
    if (!corrections.isEmpty()) {
        out.insert(QStringLiteral("remarque"), QStringLiteral("%1 couleur(s) ajustée(s) pour rester lisibles : dis-le à la personne.").arg(corrections.size()));
    }
    return out;
}

QJsonObject themeCreer(const Timeline &, const Args &args)
{
    QMap<QString, QString> colors;
    QJsonObject failure;
    if (!readColors(args, colors, failure)) {
        return failure;
    }
    QList<KoraCutThemes::Correction> corrections;
    QString message;
    const QString id = KoraCutThemes::self()->create(args.value(QStringLiteral("nom")).toString(), colors, args.value(QStringLiteral("inspiration")).toString(),
                                                     args.value(QStringLiteral("police")).toString(), args.value(QStringLiteral("rayon")).toInt(-1), &corrections, &message);
    if (id.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), message, QStringLiteral("Au moins fond ou accent, en « #RRGGBB » ; theme_lister donne les jetons."));
    }
    return themeResult(id, corrections);
}

QJsonObject themeModifier(const Timeline &, const Args &args)
{
    QMap<QString, QString> changes;
    QJsonObject failure;
    if (!readColors(args, changes, failure)) {
        return failure;
    }
    if (args.contains(QStringLiteral("police"))) {
        changes.insert(QStringLiteral("police"), args.value(QStringLiteral("police")).toString());
    }
    if (args.contains(QStringLiteral("rayon"))) {
        changes.insert(QStringLiteral("rayon"), QString::number(args.value(QStringLiteral("rayon")).toInt()));
    }
    if (changes.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Rien à changer."), QStringLiteral("Donne couleurs, police ou rayon."));
    }
    const QString name = args.value(QStringLiteral("nom")).toString().isEmpty() ? KoraCutThemes::self()->currentId() : args.value(QStringLiteral("nom")).toString();
    QList<KoraCutThemes::Correction> corrections;
    QString message;
    const QString id = KoraCutThemes::self()->modify(name, changes, &corrections, &message);
    if (id.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), message, QStringLiteral("theme_lister donne les thèmes et les jetons."));
    }
    return themeResult(id, corrections);
}

QJsonObject themeSupprimer(const Timeline &, const Args &args)
{
    const QString name = args.value(QStringLiteral("nom")).toString();
    const QString id = KoraCutThemes::self()->resolve(name);
    QString message;
    if (!KoraCutThemes::self()->remove(name, &message)) {
        return error(id.isEmpty() ? QStringLiteral("theme_inconnu") : QStringLiteral("fichier_protege"), message,
                     QStringLiteral("theme_lister : seuls les thèmes marqués cree se suppriment."));
    }
    QJsonObject out = activeJson();
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("supprime"), id);
    return out;
}

// ---- Les réglages ---------------------------------------------------------------------------

enum class Kind { Bool, Int, Duration, Choice };

struct Setting
{
    const char *key;
    const char *section;
    const char *label;
    Kind kind;
    int min;
    int max;
    /** @brief koracutrc [KoraCut] au lieu de KdenliveSettings. */
    bool koracut;
    const char *applies;
    const char *choices;
    const char *defaultValue;
};

// La liste blanche : ce qu'un agent a le droit de changer. Pas de chemins, pas de programmes
// externes, pas la porte de commande ni le terminal, rien qui supprime.
const Setting kSettings[] = {
    {"autosave_time", "sauvegarde", "Sauvegarde automatique : toutes les N secondes", Kind::Int, 30, 3600, false, "tout_de_suite", "", ""},
    {"autosave_ops", "sauvegarde", "Sauvegarde automatique : après N gestes annulables", Kind::Int, 5, 500, false, "tout_de_suite", "", ""},
    {"openlastproject", "sauvegarde", "Rouvrir le dernier projet au lancement", Kind::Bool, 0, 0, false, "prochain_lancement", "", ""},
    {"snaptopoints", "timeline", "Magnétisme : les clips s'aimantent aux clips, repères et marqueurs", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"autoscroll", "timeline", "La timeline défile pendant la lecture", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"centeredplayhead", "timeline", "Tête de lecture au centre pendant la lecture", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"pauseonseek", "timeline", "Mettre en pause quand on déplace la tête de lecture", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"scrollvertically", "timeline", "La molette fait défiler verticalement (Maj pour horizontal)", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"videothumbnails", "timeline", "Vignettes vidéo dans les clips", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"audiothumbnails", "timeline", "Formes d'onde dans les clips", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"displayallchannels", "timeline", "Formes d'onde : tous les canaux séparés", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"seekonaddeffect", "timeline", "Aller au clip quand on lui ajoute un effet", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"image_duration", "durees", "Durée d'une image fixe posée", Kind::Duration, 0, 0, false, "nouveaux_clips", "", ""},
    {"title_duration", "durees", "Durée d'un titre", Kind::Duration, 0, 0, false, "nouveaux_clips", "", ""},
    {"color_duration", "durees", "Durée d'un clip de couleur", Kind::Duration, 0, 0, false, "nouveaux_clips", "", ""},
    {"transition_duration", "durees", "Durée d'une transition", Kind::Duration, 0, 0, false, "nouveaux_clips", "", ""},
    {"mix_duration", "durees", "Durée d'un fondu enchaîné entre deux clips", Kind::Duration, 0, 0, false, "nouveaux_clips", "", ""},
    {"fade_duration", "durees", "Durée d'un fondu d'entrée ou de sortie", Kind::Duration, 0, 0, false, "nouveaux_clips", "", ""},
    {"volume", "lecture", "Volume de lecture des moniteurs (0 à 100)", Kind::Int, 0, 100, false, "prochaine_lecture", "", ""},
    {"hoverPreview", "lecture", "Aperçu animé au survol d'une vignette du chutier", Kind::Bool, 0, 0, false, "tout_de_suite", "", ""},
    {"enableproxy", "proxy", "Fichiers d'aperçu légers (proxys) pour monter vite sur une machine modeste", Kind::Bool, 0, 0, false, "nouveau_projet", "", ""},
    {"generateproxy", "proxy", "Créer un proxy pour chaque vidéo importée", Kind::Bool, 0, 0, false, "nouveau_projet", "", ""},
    {"proxyminsize", "proxy", "Largeur minimale d'une vidéo pour lui faire un proxy (pixels)", Kind::Int, 100, 10000, false, "nouveau_projet", "", ""},
    {"generateimageproxy", "proxy", "Créer un proxy pour les grandes images", Kind::Bool, 0, 0, false, "nouveau_projet", "", ""},
    {"videotracks", "nouveau_projet", "Pistes vidéo d'un projet neuf sans type de vidéo", Kind::Int, 0, 10, false, "nouveau_projet", "", ""},
    {"audiotracks", "nouveau_projet", "Pistes audio d'un projet neuf sans type de vidéo", Kind::Int, 0, 10, false, "nouveau_projet", "", ""},
    {"montageVisible", "agents", "Montage visible : l'écran suit les gestes des agents", Kind::Choice, 0, 0, true, "tout_de_suite", "normal,rapide,desactive", "normal"},
    {"apercuPlafondMo", "apercu", "Plafond du cache de rendu d'un projet en Mo (0 = automatique)", Kind::Int, 0, 200000, true, "tout_de_suite", "", "0"},
    {"apercuNettoyageJours", "apercu", "Âge (jours) au-delà duquel les rendus d'un projet fermé partent", Kind::Int, 1, 365, true, "tout_de_suite", "", "30"},
};

const Setting *findSetting(const QString &key)
{
    for (const Setting &s : kSettings) {
        if (QString::fromLatin1(s.key) == key) {
            return &s;
        }
    }
    return nullptr;
}

QString kindName(Kind k)
{
    switch (k) {
    case Kind::Bool:
        return QStringLiteral("booleen");
    case Kind::Int:
        return QStringLiteral("entier");
    case Kind::Duration:
        return QStringLiteral("duree");
    case Kind::Choice:
        return QStringLiteral("choix");
    }
    return {};
}

KConfigGroup koraCutGroup()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
}

QJsonValue readValue(const Setting &s)
{
    const QString key = QString::fromLatin1(s.key);
    if (s.koracut) {
        const QString v = koraCutGroup().readEntry(s.key, QString::fromUtf8(s.defaultValue));
        return s.kind == Kind::Int ? QJsonValue(v.toInt()) : QJsonValue(v);
    }
    KConfigSkeletonItem *item = KdenliveSettings::self()->findItem(key);
    if (!item) {
        return QJsonValue();
    }
    return QJsonValue::fromVariant(item->property());
}

QJsonValue defaultValue(const Setting &s)
{
    if (s.koracut) {
        const QString v = QString::fromUtf8(s.defaultValue);
        return s.kind == Kind::Int ? QJsonValue(v.toInt()) : QJsonValue(v);
    }
    KConfigSkeletonItem *item = KdenliveSettings::self()->findItem(QString::fromLatin1(s.key));
    return item ? QJsonValue::fromVariant(item->getDefault()) : QJsonValue();
}

QJsonObject settingJson(const Setting &s)
{
    QJsonObject o{{QStringLiteral("cle"), QString::fromLatin1(s.key)}, {QStringLiteral("section"), QString::fromLatin1(s.section)},
                  {QStringLiteral("libelle"), QString::fromUtf8(s.label)}, {QStringLiteral("type"), kindName(s.kind)},
                  {QStringLiteral("valeur"), readValue(s)},             {QStringLiteral("defaut"), defaultValue(s)},
                  {QStringLiteral("applique"), QString::fromLatin1(s.applies)}};
    if (s.kind == Kind::Int) {
        o.insert(QStringLiteral("min"), s.min);
        o.insert(QStringLiteral("max"), s.max);
    }
    if (s.kind == Kind::Choice) {
        o.insert(QStringLiteral("choix"), QJsonArray::fromStringList(QString::fromLatin1(s.choices).split(QLatin1Char(','))));
    }
    if (s.kind == Kind::Duration) {
        o.insert(QStringLiteral("format"), QStringLiteral("HH:MM:SS:II"));
    }
    return o;
}

QJsonObject unknownSetting(const QString &key)
{
    return error(QStringLiteral("reglage_inconnu"), QStringLiteral("« %1 » n'est pas un réglage accessible aux agents.").arg(key),
                 QStringLiteral("reglages_lister donne la liste ; le thème se change avec theme_appliquer."));
}

QJsonObject reglagesLister(const Timeline &, const Args &args)
{
    const QString section = args.value(QStringLiteral("section")).toString();
    QJsonArray list;
    QStringList sections;
    for (const Setting &s : kSettings) {
        if (!sections.contains(QString::fromLatin1(s.section))) {
            sections << QString::fromLatin1(s.section);
        }
        if (section.isEmpty() || section == QLatin1String(s.section)) {
            list.append(settingJson(s));
        }
    }
    if (!section.isEmpty() && list.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La section « %1 » n'existe pas.").arg(section), sections.join(QStringLiteral(", ")));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("sections"), QJsonArray::fromStringList(sections)}, {QStringLiteral("reglages"), list}};
}

QJsonObject reglagesLire(const Timeline &, const Args &args)
{
    QStringList keys;
    if (args.contains(QStringLiteral("cle"))) {
        keys << args.value(QStringLiteral("cle")).toString();
    }
    for (const QJsonValue &v : args.value(QStringLiteral("cles")).toArray()) {
        keys << v.toString();
    }
    if (keys.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Donne cle ou cles."), QStringLiteral("reglages_lister donne les clés."));
    }
    QJsonArray out;
    for (const QString &key : keys) {
        const Setting *s = findSetting(key);
        if (!s) {
            return unknownSetting(key);
        }
        out.append(QJsonObject{{QStringLiteral("cle"), key},
                               {QStringLiteral("libelle"), QString::fromUtf8(s->label)},
                               {QStringLiteral("valeur"), readValue(*s)},
                               {QStringLiteral("defaut"), defaultValue(*s)}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("reglages"), out}};
}

/** @brief Ce que l'interface doit refléter tout de suite après un changement. */
void reflect(const QString &key)
{
    if (!pCore || !pCore->window()) {
        return;
    }
    MainWindow *window = pCore->window();
    if (key == QLatin1String("autosave_time") && pCore->projectManager()) {
        pCore->projectManager()->updateAutoSaveTimer();
    } else if (key == QLatin1String("audiothumbnails") && pCore->bin()) {
        pCore->bin()->checkAudioThumbs();
    } else if (key == QLatin1String("snaptopoints")) {
        if (QAction *a = window->actionCollection()->action(QStringLiteral("snap"))) {
            a->setChecked(KdenliveSettings::snaptopoints());
        }
    }
    // Les boutons de la timeline (vignettes, formes d'onde, marqueurs) suivent le réglage.
    QMetaObject::invokeMethod(window, "updateConfiguration", Qt::QueuedConnection);
}

QJsonObject reglagesEcrire(const Timeline &, const Args &args)
{
    const QString key = args.value(QStringLiteral("cle")).toString();
    const Setting *s = findSetting(key);
    if (!s) {
        return unknownSetting(key);
    }
    const QJsonValue value = args.value(QStringLiteral("valeur"));
    const QJsonValue before = readValue(*s);
    QVariant v;
    switch (s->kind) {
    case Kind::Bool: {
        if (value.isBool()) {
            v = value.toBool();
        } else if (value.isString() && QStringList{QStringLiteral("true"), QStringLiteral("oui"), QStringLiteral("vrai")}.contains(value.toString().toLower())) {
            v = true;
        } else if (value.isString() && QStringList{QStringLiteral("false"), QStringLiteral("non"), QStringLiteral("faux")}.contains(value.toString().toLower())) {
            v = false;
        } else {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("%1 attend vrai ou faux.").arg(key));
        }
        break;
    }
    case Kind::Int: {
        bool ok = value.isDouble();
        int n = value.toInt();
        if (value.isString()) {
            n = value.toString().toInt(&ok);
        }
        if (!ok || n < s->min || n > s->max) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("%1 attend un entier entre %2 et %3.").arg(key).arg(s->min).arg(s->max));
        }
        v = n;
        break;
    }
    case Kind::Duration: {
        static const QRegularExpression tc(QStringLiteral("^\\d{2}:[0-5]\\d:[0-5]\\d:\\d{2}$"));
        if (!value.isString() || !tc.match(value.toString()).hasMatch()) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("%1 attend une durée « HH:MM:SS:II », par exemple 00:00:05:00.").arg(key));
        }
        v = value.toString();
        break;
    }
    case Kind::Choice: {
        const QStringList choices = QString::fromLatin1(s->choices).split(QLatin1Char(','));
        if (!value.isString() || !choices.contains(value.toString())) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("%1 attend un de ces choix : %2.").arg(key, choices.join(QStringLiteral(", "))));
        }
        v = value.toString();
        break;
    }
    }
    if (s->koracut) {
        if (key == QLatin1String("montageVisible")) {
            const QString c = v.toString();
            KoraCutVisible::self()->setSpeed(c == QLatin1String("rapide")      ? KoraCutVisible::Speed::Fast
                                             : c == QLatin1String("desactive") ? KoraCutVisible::Speed::Off
                                                                               : KoraCutVisible::Speed::Normal);
        }
        KConfigGroup g = koraCutGroup();
        g.writeEntry(s->key, v.toString());
        g.sync();
    } else {
        KConfigSkeletonItem *item = KdenliveSettings::self()->findItem(key);
        if (!item) {
            return unknownSetting(key);
        }
        item->setProperty(v);
        KdenliveSettings::self()->save();
        reflect(key);
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("cle"), key},
                       {QStringLiteral("avant"), before},
                       {QStringLiteral("apres"), readValue(*s)},
                       {QStringLiteral("applique"), QString::fromLatin1(s->applies)}};
}

} // namespace

QMap<QString, Tool> reglagesTools()
{
    return {{QStringLiteral("theme_lister"), themeLister},       {QStringLiteral("theme_appliquer"), themeAppliquer},
            {QStringLiteral("theme_creer"), themeCreer},         {QStringLiteral("theme_modifier"), themeModifier},
            {QStringLiteral("theme_supprimer"), themeSupprimer}, {QStringLiteral("reglages_lister"), reglagesLister},
            {QStringLiteral("reglages_lire"), reglagesLire},     {QStringLiteral("reglages_ecrire"), reglagesEcrire}};
}

QStringList reglagesToolNames()
{
    return reglagesTools().keys();
}

} // namespace KoraCutAgentToolsPrivate
