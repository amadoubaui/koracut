/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Chantier 5 (29 septembre 2026) : la voix off de synthèse pour les agents, voix_off_generer,
// voix_off_voix et voix_off_installer (Agents in-app/outils/voix.json). Le travail lourd tourne
// dans un processus à part (KoraCutVoiceOver) ; l'appel attend sa fin dans une boucle d'événements,
// comme transcrire_media : l'interface et la lecture continuent pendant ce temps.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "koracutvisible.h"
#include "voix/koracutvoiceover.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>

using namespace KoraCutAgentToolsPrivate;

namespace {

QString shortText(const QString &text)
{
    const QString simple = text.simplified();
    return simple.size() > 40 ? simple.left(39) + QChar(0x2026) : simple;
}

// voix_off_generer ---------------------------------------------------------------------------------

QJsonObject voixOffGenerer(const Timeline &timeline, const Args &args)
{
    const QString text = args.value(QStringLiteral("texte")).toString().simplified();
    if (text.isEmpty() || text.size() > 1000) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("texte doit faire entre 1 et 1000 caractères."),
                     QStringLiteral("Une phrase par appel : un fichier par plan."));
    }
    QString engine = args.value(QStringLiteral("moteur")).toString(QStringLiteral("auto"));
    if (engine == QLatin1String("auto")) {
        engine = KoraCutVoiceOver::autoEngine();
    }
    if (!KoraCutVoiceOver::engines().contains(engine)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("moteur vaut auto, local, elevenlabs ou openai."));
    }
    const QJsonObject state = KoraCutVoiceOver::engineState(engine);
    if (!state.value(QStringLiteral("pret")).toBool()) {
        const bool missing = engine == QLatin1String("local") && KoraCutVoiceOver::localSupported();
        QJsonObject e = error(missing ? QStringLiteral("moteur_absent") : QStringLiteral("non_disponible"), state.value(QStringLiteral("raison")).toString(),
                              state.value(QStringLiteral("conseil")).toString());
        e.insert(QStringLiteral("moteur"), state);
        return e;
    }
    KdenliveDoc *doc = pCore->currentDoc();
    if (!doc) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    // La place, vérifiée avant de générer : une erreur de piste ne doit pas coûter une minute de calcul.
    const QJsonObject place = args.value(QStringLiteral("poser")).toObject();
    const bool posing = args.contains(QStringLiteral("poser"));
    int position = -1;
    int trackId = -1;
    if (posing) {
        if (place.contains(QStringLiteral("position"))) {
            position = KoraCutAgentTools::parseTime(place.value(QStringLiteral("position")), projectFps());
            if (position < 0) {
                return error(QStringLiteral("entree_invalide"), QStringLiteral("poser.position ne se lit pas (images ou HH:MM:SS:II)."));
            }
        } else {
            position = pCore->getMonitorPosition(Kdenlive::ProjectMonitor);
        }
        if (place.contains(QStringLiteral("piste_id"))) {
            trackId = place.value(QStringLiteral("piste_id")).toInt(-1);
            if (QJsonObject e = trackIdCheck(timeline, trackId); !e.isEmpty()) {
                return e;
            }
            if (!timeline->isAudioTrack(trackId)) {
                return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("La piste %1 est une piste vidéo : une voix se pose sur une piste audio.").arg(trackLabel(timeline, trackId)),
                             QStringLiteral("Laisse piste_id vide : la voix va sur la piste « Voix off », créée si besoin."));
            }
        }
    }
    const QString folder = KoraCutVoiceOver::projectVoiceFolder();
    if (!QDir().mkpath(folder)) {
        return error(QStringLiteral("ecriture_impossible"), QStringLiteral("Le dossier %1 n'a pas pu être créé.").arg(folder));
    }
    const QString output = folder + QLatin1Char('/') + KoraCutVoiceOver::fileNameFor(folder, text, args.value(QStringLiteral("nom")).toString());

    KoraCutVoiceRequest request;
    request.engine = engine;
    request.voice = args.value(QStringLiteral("voix")).toString();
    request.tone = args.value(QStringLiteral("ton")).toString(QStringLiteral("pose"));
    request.speed = std::clamp(args.value(QStringLiteral("vitesse")).toDouble(1.0), 0.8, 1.25);
    request.takes = std::clamp(args.value(QStringLiteral("prises")).toInt(3), 1, 5);
    request.phrases << KoraCutVoicePhrase{QStringLiteral("p1"), text, args.value(QStringLiteral("dire")).toString().simplified(), output};

    // La génération tourne dans un autre processus ; on attend ici sans bloquer l'interface.
    KoraCutVoiceOver job;
    QEventLoop loop;
    QJsonObject phrase;
    QString failCode;
    QString failMessage;
    QString failAdvice;
    bool done = false;
    const QString line = shortText(text);
    QObject::connect(&job, &KoraCutVoiceOver::progress, &loop, [line](const QString &step, int n, int total) {
        const QString counter = total > 0 ? QStringLiteral(" (%1/%2)").arg(n).arg(total) : QString();
        KoraCutVisible::self()->progressLine(QString(), i18nc("@info:status voice-over progress, %1 step, %2 counter, %3 text", "Voice-over: %1%2 · %3", step, counter, line));
    });
    QObject::connect(&job, &KoraCutVoiceOver::phraseReady, &loop, [&phrase](const QJsonObject &p) { phrase = p; });
    QObject::connect(&job, &KoraCutVoiceOver::finished, &loop, [&](const QJsonObject &) {
        done = true;
        loop.quit();
    });
    QObject::connect(&job, &KoraCutVoiceOver::failed, &loop, [&](const QString &code, const QString &message, const QString &advice) {
        failCode = code;
        failMessage = message;
        failAdvice = advice;
        loop.quit();
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10 * 60 * 1000);
    const QPointer<KdenliveDoc> guard(doc);
    job.generate(request);
    loop.exec();
    job.cancel();
    if (!failCode.isEmpty()) {
        return error(failCode, failMessage, failAdvice);
    }
    if (!done) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La voix n'a pas été générée en dix minutes."), QStringLiteral("Essaie une phrase plus courte."));
    }
    if (!guard || pCore->currentDoc() != doc) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le projet a été fermé pendant la génération ; le fichier est gardé : %1").arg(output));
    }
    if (phrase.contains(QStringLiteral("erreur")) || !QFileInfo::exists(output)) {
        return error(QStringLiteral("non_disponible"), phrase.value(QStringLiteral("erreur")).toString(QStringLiteral("Aucune prise n'a été produite.")),
                     QStringLiteral("Réessaie ; si ça recommence, essaie un autre moteur."));
    }

    // Le chutier, et la timeline si on le demande : une seule étape d'annulation.
    // Sans position donnée, la tête de lecture ; si la place est prise, juste après ce qui l'occupe.
    const bool flexible = posing && !place.contains(QStringLiteral("position"));
    const QJsonObject placed = KoraCutVoiceOver::importAndPlace(timeline, {output}, {posing ? position : -1}, trackId,
                                                                i18nc("@action undo text, %1 start of the text", "Voice-over: %1", line), {flexible});
    if (!placed.value(QStringLiteral("ok")).toBool()) {
        return placed;
    }
    const QJsonObject media = placed.value(QStringLiteral("medias")).toArray().first().toObject();
    const QJsonValue fidelity = phrase.value(QStringLiteral("fidelite"));
    const QJsonArray doubtful = phrase.value(QStringLiteral("douteux")).toArray();
    const bool verified = !fidelity.isNull() && !fidelity.isUndefined();
    QJsonObject out{{QStringLiteral("ok"), true},
                    {QStringLiteral("media_id"), media.value(QStringLiteral("id"))},
                    {QStringLiteral("fichier"), output},
                    {QStringLiteral("duree_s"), phrase.value(QStringLiteral("duree"))},
                    {QStringLiteral("moteur"), engine},
                    {QStringLiteral("voix"), request.voice.isEmpty() ? KoraCutVoiceOver::defaultVoice(engine) : request.voice},
                    {QStringLiteral("verifiee"), verified},
                    {QStringLiteral("fidelite"), fidelity},
                    {QStringLiteral("entendu"), phrase.value(QStringLiteral("entendu"))},
                    {QStringLiteral("mots_douteux"), doubtful},
                    {QStringLiteral("a_reecouter"), verified && (fidelity.toDouble() < 0.95 || !doubtful.isEmpty())},
                    {QStringLiteral("prises"), phrase.value(QStringLiteral("prises"))},
                    {QStringLiteral("lufs"), phrase.value(QStringLiteral("lufs"))},
                    {QStringLiteral("crete_dbfs"), phrase.value(QStringLiteral("crete"))}};
    if (!verified) {
        out.insert(QStringLiteral("note"), QStringLiteral("Voix non vérifiée : la reconnaissance vocale n'est pas installée (Préférences > Reconnaissance vocale)."));
    }
    if (engine == QLatin1String("local")) {
        out.insert(QStringLiteral("credit"), state.value(QStringLiteral("credit")));
    }
    if (state.contains(QStringLiteral("avertissement"))) {
        out.insert(QStringLiteral("avertissement"), state.value(QStringLiteral("avertissement")));
    }
    if (media.contains(QStringLiteral("clip_id"))) {
        out.insert(QStringLiteral("clip_id"), media.value(QStringLiteral("clip_id")));
        out.insert(QStringLiteral("position"), media.value(QStringLiteral("position")));
        out.insert(QStringLiteral("duree"), media.value(QStringLiteral("duree")));
    }
    if (placed.contains(QStringLiteral("piste_id"))) {
        out.insert(QStringLiteral("piste_id"), placed.value(QStringLiteral("piste_id")));
        out.insert(QStringLiteral("piste_creee"), placed.value(QStringLiteral("piste_creee")));
    }
    if (media.value(QStringLiteral("decalee")).toBool()) {
        out.insert(QStringLiteral("decalee"), true);
    }
    if (media.contains(QStringLiteral("pose_refusee"))) {
        out.insert(QStringLiteral("pose_refusee"), media.value(QStringLiteral("pose_refusee")));
    }
    return out;
}

// voix_off_voix ------------------------------------------------------------------------------------

QJsonObject voixOffVoix(const Timeline &, const Args &args)
{
    const QString wanted = args.value(QStringLiteral("moteur")).toString();
    if (!wanted.isEmpty() && !KoraCutVoiceOver::engines().contains(wanted)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("moteur vaut local, elevenlabs ou openai."));
    }
    QJsonArray engines;
    QJsonArray voices;
    for (const QString &engine : KoraCutVoiceOver::engines()) {
        if (!wanted.isEmpty() && engine != wanted) {
            continue;
        }
        const QJsonObject state = KoraCutVoiceOver::engineState(engine);
        engines.append(state);
        if (engine == QLatin1String("local")) {
            if (KoraCutVoiceOver::localSupported()) {
                for (const QJsonValue &v : KoraCutVoiceOver::localVoices()) {
                    voices.append(v);
                }
            }
            continue;
        }
        if (!state.value(QStringLiteral("pret")).toBool()) {
            continue;
        }
        // Les voix d'un service en ligne : une requête, dans un autre processus.
        KoraCutVoiceOver job;
        QEventLoop loop;
        QJsonArray remote;
        QObject::connect(&job, &KoraCutVoiceOver::remoteVoices, &loop, [&](const QJsonArray &list) {
            remote = list;
            loop.quit();
        });
        QObject::connect(&job, &KoraCutVoiceOver::failed, &loop, [&](const QString &, const QString &message, const QString &) {
            remote = QJsonArray{QJsonObject{{QStringLiteral("erreur"), message}}};
            loop.quit();
        });
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        job.listRemoteVoices(engine);
        loop.exec();
        job.cancel();
        const QString byDefault = KoraCutVoiceOver::defaultVoice(engine);
        for (const QJsonValue &v : std::as_const(remote)) {
            QJsonObject o = v.toObject();
            if (o.contains(QStringLiteral("erreur"))) {
                QJsonObject withError = state;
                withError.insert(QStringLiteral("erreur_liste"), o.value(QStringLiteral("erreur")));
                engines.replace(engines.size() - 1, withError);
                continue;
            }
            o.insert(QStringLiteral("moteur"), engine);
            o.insert(QStringLiteral("defaut"), o.value(QStringLiteral("id")).toString() == byDefault);
            voices.append(o);
        }
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("moteur_auto"), KoraCutVoiceOver::autoEngine()},
                       {QStringLiteral("moteurs"), engines},
                       {QStringLiteral("voix"), voices}};
}

// voix_off_installer -------------------------------------------------------------------------------

QJsonObject voixOffInstaller(const Timeline &, const Args &)
{
    const QJsonObject state = KoraCutVoiceOver::engineState(QStringLiteral("local"));
    if (state.value(QStringLiteral("pret")).toBool()) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deja_installe"), true}};
    }
    if (!KoraCutVoiceOver::localSupported()) {
        return error(QStringLiteral("non_disponible"), state.value(QStringLiteral("raison")).toString(), state.value(QStringLiteral("conseil")).toString());
    }
    KoraCutVoiceOver *installer = KoraCutVoiceOver::installer();
    if (!installer->isInstalling()) {
        if (KoraCutVoiceOver::systemPython().isEmpty() && !state.value(QStringLiteral("installe")).toBool()) {
            return error(QStringLiteral("python_absent"), QStringLiteral("Python 3.10 ou plus récent est nécessaire pour installer le moteur de voix."),
                         QStringLiteral("Dis à la personne d'installer Python depuis python.org, puis de réessayer."));
        }
        installer->install();
    }
    // L'installation continue en tâche de fond : voix_off_voix dit où elle en est.
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("en_cours"), true},
                       {QStringLiteral("taille_mo"), KoraCutVoiceOver::installSizeMb()},
                       {QStringLiteral("dossiers"), state.value(QStringLiteral("dossiers"))},
                       {QStringLiteral("conseil"), QStringLiteral("Le téléchargement prend quelques minutes. Rappelle voix_off_voix pour savoir où il en est, puis génère la voix.")}};
}

} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> voixTools()
{
    return {
        {QStringLiteral("voix_off_generer"), voixOffGenerer},
        {QStringLiteral("voix_off_voix"), voixOffVoix},
        {QStringLiteral("voix_off_installer"), voixOffInstaller},
    };
}

} // namespace KoraCutAgentToolsPrivate
