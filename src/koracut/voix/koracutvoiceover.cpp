/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <config-kdenlive.h>

#include "koracutvoiceover.h"

#include "koracut/koracutagenttools.h"
#include "koracut/koracutagenttools_p.h"
#include "koracut/koracuttranscriber.h"
#if KORACUT_AGENTS
#include "koracut/ai/koracutaicatalog.h"
#include "koracut/ai/koracutaiconnections.h"
#include "koracut/ai/koracutaisecrets.h"
#endif

#include "bin/bin.h"
#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "pythoninterfaces/speechtotextwhisper.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

namespace {

// La taille du modèle Kyutai une fois téléchargé (poids 8 bits non compris : ils sont calculés au chargement).
constexpr qint64 kModelBytes = 3'900'000'000LL;

QString plainText(const QString &text)
{
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out.append(c.toLower());
        }
    }
    return out.simplified();
}

QString hubDir()
{
    QString hub = qEnvironmentVariable("HF_HUB_CACHE");
    if (hub.isEmpty()) {
        const QString home = qEnvironmentVariable("HF_HOME");
        hub = (home.isEmpty() ? QDir::homePath() + QStringLiteral("/.cache/huggingface") : home) + QStringLiteral("/hub");
    }
    return hub;
}

qint64 folderSize(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

bool modelPresent()
{
    const QDir snapshots(KoraCutVoiceOver::modelDir() + QStringLiteral("/snapshots"));
    const QStringList revisions = snapshots.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &rev : revisions) {
        const QDir d(snapshots.filePath(rev));
        if (!d.entryList({QStringLiteral("dsm_tts_*.safetensors")}, QDir::Files).isEmpty() &&
            !d.entryList({QStringLiteral("tokenizer-*.safetensors")}, QDir::Files).isEmpty() &&
            !d.entryList({QStringLiteral("tokenizer_spm_*.model")}, QDir::Files).isEmpty()) {
            return true;
        }
    }
    return false;
}

QString kyutaiPython()
{
    // Pour les essais (comme KORACUT_VOIX_SCRIPT) : l'interpréteur à la place de celui de l'environnement.
    // Un lien vers python ne se pose pas de la même façon sur les trois systèmes, une variable si.
    const QString forced = qEnvironmentVariable("KORACUT_VOIX_PYTHON");
    if (!forced.isEmpty()) {
        return forced;
    }
#ifdef Q_OS_WIN
    return KoraCutVoiceOver::cacheDir() + QStringLiteral("/kyutai/Scripts/python.exe");
#else
    return KoraCutVoiceOver::cacheDir() + QStringLiteral("/kyutai/bin/python");
#endif
}

// Le même choix que moteur_local() dans koracut_voix.py : MLX sur Mac à puce Apple, PyTorch partout
// ailleurs (Windows, Linux, Mac Intel), avec le même modèle Kyutai et les mêmes voix.
bool usesMlx()
{
    const QString forced = qEnvironmentVariable("KORACUT_VOIX_MOTEUR");
    if (!forced.isEmpty()) {
        return forced == QLatin1String("mlx");
    }
#if defined(Q_OS_MACOS)
    return QSysInfo::currentCpuArchitecture() == QLatin1String("arm64");
#else
    return false;
#endif
}

bool engineInstalled()
{
    if (!QFileInfo::exists(kyutaiPython())) {
        return false;
    }
    // Le moteur posé dans l'environnement : moshi_mlx sur Mac à puce Apple, moshi (PyTorch) ailleurs
    // (lib/python3.X/site-packages sur Mac et Linux, Lib/site-packages sous Windows).
    const QString module = usesMlx() ? QStringLiteral("moshi_mlx") : QStringLiteral("moshi");
    for (const QString &lib : {QStringLiteral("/kyutai/lib"), QStringLiteral("/kyutai/Lib")}) {
        QDirIterator it(KoraCutVoiceOver::cacheDir() + lib, {module}, QDir::Dirs, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            return true;
        }
    }
    return false;
}

QString engineName(const QString &engine)
{
    if (engine == QLatin1String("local")) {
        return i18nc("@item voice-over engine", "On this computer (Kyutai)");
    }
    if (engine == QLatin1String("elevenlabs")) {
        return QStringLiteral("ElevenLabs");
    }
    return QStringLiteral("OpenAI");
}

} // namespace

KoraCutVoiceOver::KoraCutVoiceOver(QObject *parent)
    : QObject(parent)
{
}

KoraCutVoiceOver::~KoraCutVoiceOver()
{
    cancel();
}

// ---------------------------------------------------------------------------------------------
// Ce qui est disponible

bool KoraCutVoiceOver::localSupported()
{
    if (qEnvironmentVariableIsSet("KORACUT_VOIX_LOCAL_OK")) {
        return true; // essais
    }
    // Depuis le 30 septembre 2026, sur les trois systèmes : MLX sur Mac à puce Apple, PyTorch ailleurs
    // (carte NVIDIA si elle répond, sinon le processeur, plus lent : 3 à 4 s de calcul par seconde de voix).
    return true;
}

QString KoraCutVoiceOver::cacheDir()
{
    const QString forced = qEnvironmentVariable("KORACUT_VOIX_CACHE");
    return forced.isEmpty() ? QDir::homePath() + QStringLiteral("/.cache/koracut-voix") : forced;
}

QString KoraCutVoiceOver::modelDir()
{
    return hubDir() + QStringLiteral("/models--kyutai--tts-1.6b-en_fr");
}

int KoraCutVoiceOver::installSizeMb()
{
    return 4100;
}

QString KoraCutVoiceOver::scriptPath()
{
    const QString forced = qEnvironmentVariable("KORACUT_VOIX_SCRIPT");
    if (!forced.isEmpty()) {
        return forced;
    }
    return QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("scripts/voix/koracut_voix.py"));
}

QString KoraCutVoiceOver::systemPython()
{
    static QString cached;
    static bool searched = false;
    if (searched) {
        return cached;
    }
    searched = true;
    QStringList candidates;
    for (const QString &name : {QStringLiteral("python3.12"), QStringLiteral("python3.11"), QStringLiteral("python3.13"), QStringLiteral("python3.10"),
                                QStringLiteral("python3"), QStringLiteral("python")}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) {
            candidates << found;
        }
    }
    candidates << QDir::homePath() + QStringLiteral("/.local/bin/python3.11") << QStringLiteral("/opt/homebrew/bin/python3.12")
               << QStringLiteral("/opt/homebrew/bin/python3.11");
#ifdef Q_OS_WIN
    // L'installeur de python.org pose Python dans %LOCALAPPDATA%\Programs\Python\Python31x, souvent hors
    // du PATH ; le « python.exe » du Microsoft Store n'est qu'un raccourci vers la boutique (il échoue au test).
    const QString programs = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs/Python");
    for (const QString &version : {QStringLiteral("312"), QStringLiteral("311"), QStringLiteral("313"), QStringLiteral("310")}) {
        candidates << programs + QStringLiteral("/Python") + version + QStringLiteral("/python.exe")
                   << QStringLiteral("C:/Program Files/Python") + version + QStringLiteral("/python.exe");
    }
#endif
    for (const QString &python : std::as_const(candidates)) {
        if (!QFileInfo::exists(python)) {
            continue;
        }
        QProcess p;
        p.start(python, {QStringLiteral("-c"), QStringLiteral("import sys; print(sys.version_info[0] * 100 + sys.version_info[1])")});
        // PyTorch n'a pas encore de version pour Python 3.14 : le moteur PyTorch veut 3.10 à 3.13.
        const int version = p.waitForFinished(4000) && p.exitCode() == 0 ? p.readAllStandardOutput().trimmed().toInt() : 0;
        if (version >= 310 && (usesMlx() || version <= 313)) {
            cached = python;
            break;
        }
    }
    return cached;
}

QStringList KoraCutVoiceOver::engines()
{
    return {QStringLiteral("local"), QStringLiteral("elevenlabs"), QStringLiteral("openai")};
}

bool KoraCutVoiceOver::remoteCredentials(const QString &engine, QString *key, QString *baseUrl, QString *voice, QString *connection)
{
#if KORACUT_AGENTS
    const QString provider = engine == QLatin1String("elevenlabs") ? QStringLiteral("elevenlabs") : QStringLiteral("openai");
    for (const KoraCutAiConnection &c : KoraCutAiConnections::instance()->list()) {
        if (c.provider != provider) {
            continue;
        }
        bool found = false;
        const QString secret = KoraCutAiSecrets::read(c.id, &found);
        if (!found || secret.isEmpty()) {
            continue;
        }
        if (key) {
            *key = secret;
        }
        if (baseUrl) {
            *baseUrl = KoraCutAiConnections::effectiveBaseUrl(c, KoraCutAiCatalog::provider(c.provider));
        }
        if (voice) {
            // Pour ElevenLabs, le « modèle » choisi dans la page est la voix par défaut.
            *voice = provider == QLatin1String("elevenlabs") ? c.model : QString();
        }
        if (connection) {
            *connection = c.id;
        }
        return true;
    }
#else
    Q_UNUSED(engine)
    Q_UNUSED(key)
    Q_UNUSED(baseUrl)
    Q_UNUSED(voice)
    Q_UNUSED(connection)
#endif
    return false;
}

QJsonObject KoraCutVoiceOver::engineState(const QString &engine)
{
    QJsonObject o{{QStringLiteral("moteur"), engine}, {QStringLiteral("nom"), engineName(engine)}};
    if (engine == QLatin1String("local")) {
        const bool supported = localSupported();
        const bool installed = engineInstalled();
        const bool model = modelPresent();
        o.insert(QStringLiteral("installe"), installed);
        o.insert(QStringLiteral("modele_present"), model);
        o.insert(QStringLiteral("taille_mo"), installSizeMb());
        o.insert(QStringLiteral("dossiers"), QJsonArray{cacheDir(), modelDir()});
        o.insert(QStringLiteral("gratuit"), true);
        o.insert(QStringLiteral("credit"), QStringLiteral("Voix : Kyutai TTS 1.6B (CC-BY 4.0), voix du corpus CML-TTS (CC-BY 4.0)"));
        KoraCutVoiceOver *inst = installer();
        if (inst->isInstalling()) {
            o.insert(QStringLiteral("installation"), QJsonObject{{QStringLiteral("en_cours"), true},
                                                                 {QStringLiteral("pourcent"), inst->installPercent()},
                                                                 {QStringLiteral("etape"), inst->installStep()}});
        }
        if (!supported) {
            o.insert(QStringLiteral("pret"), false);
            o.insert(QStringLiteral("raison"), QStringLiteral("Le moteur de voix local tourne pour l'instant sur les Mac à puce Apple seulement."));
            o.insert(QStringLiteral("conseil"), QStringLiteral("Branche ElevenLabs (ou OpenAI) dans Préférences > Connexions IA pour générer une voix ici."));
        } else if (!installed || !model) {
            o.insert(QStringLiteral("pret"), false);
            o.insert(QStringLiteral("raison"),
                     QStringLiteral("Le moteur de voix local n'est pas installé : environ %1 Go à télécharger une seule fois, rangés dans %2 et %3.")
                         .arg(QLocale().toString(installSizeMb() / 1000., 'f', 1), cacheDir(), modelDir()));
            if (systemPython().isEmpty() && !installed) {
                o.insert(QStringLiteral("conseil"), QStringLiteral("Il faut d'abord Python 3.10 ou plus récent (python.org). Ensuite, demande l'accord de la personne et appelle voix_off_installer."));
            } else {
                o.insert(QStringLiteral("conseil"), QStringLiteral("Demande l'accord de la personne (taille et dossier), puis appelle voix_off_installer."));
            }
        } else if (scriptPath().isEmpty()) {
            o.insert(QStringLiteral("pret"), false);
            o.insert(QStringLiteral("raison"), QStringLiteral("Le script de la voix off manque dans l'installation de KoraCut."));
        } else {
            o.insert(QStringLiteral("pret"), true);
        }
        return o;
    }
    QString connection;
    const bool connected = remoteCredentials(engine, nullptr, nullptr, nullptr, &connection);
    o.insert(QStringLiteral("gratuit"), false);
    o.insert(QStringLiteral("pret"), connected && !runnerPython(engine).isEmpty());
    if (connected) {
        o.insert(QStringLiteral("connexion"), connection);
    } else {
        o.insert(QStringLiteral("raison"), QStringLiteral("Aucune connexion %1 avec sa clé dans Préférences > Connexions IA.").arg(engineName(engine)));
    }
    if (engine == QLatin1String("openai")) {
        o.insert(QStringLiteral("avertissement"), QStringLiteral("OpenAI demande d'annoncer que la voix est générée par une IA."));
    }
    if (connected && runnerPython(engine).isEmpty()) {
        o.insert(QStringLiteral("raison"), QStringLiteral("Python 3.10 ou plus récent est nécessaire sur cette machine (python.org)."));
    }
    return o;
}

QString KoraCutVoiceOver::autoEngine()
{
    for (const QString &engine : engines()) {
        if (engineState(engine).value(QStringLiteral("pret")).toBool()) {
            return engine;
        }
    }
    return QStringLiteral("local");
}

QJsonArray KoraCutVoiceOver::localVoices()
{
    // Les voix permises pour un usage commercial (Recherche/Voix off de synthese.md) : CML-TTS en
    // CC-BY 4.0, les voix données à Kyutai en CC0. Expresso et EARS sont non commerciales : absentes.
    auto voice = [](const QString &id, const QString &name, const QString &description, bool isDefault) {
        return QJsonObject{{QStringLiteral("id"), id},
                           {QStringLiteral("nom"), name},
                           {QStringLiteral("description"), description},
                           {QStringLiteral("moteur"), QStringLiteral("local")},
                           {QStringLiteral("defaut"), isDefault}};
    };
    return QJsonArray{
        voice(QStringLiteral("cml-tts/fr/4482_3103_000063-0001_enhanced.wav"), QStringLiteral("Grave et posée"),
              i18nc("@item voice description", "Man, deep and calm voice. The most faithful in our tests."), true),
        voice(QStringLiteral("cml-tts/fr/6318_7016_000027-0002_enhanced.wav"), QStringLiteral("Claire"), i18nc("@item voice description", "Woman, clear voice."), false),
        voice(QStringLiteral("unmute-prod-website/developpeuse-3.wav"), QStringLiteral("Proche"), i18nc("@item voice description", "Woman, close and natural voice."), false),
        voice(QStringLiteral("unmute-prod-website/fabieng-enhanced-v2.wav"), QStringLiteral("Vive"), i18nc("@item voice description", "Man, lively voice."), false),
    };
}

QString KoraCutVoiceOver::defaultVoice(const QString &engine)
{
    if (engine == QLatin1String("local")) {
        return QStringLiteral("cml-tts/fr/4482_3103_000063-0001_enhanced.wav");
    }
    if (engine == QLatin1String("openai")) {
        return QStringLiteral("coral");
    }
    QString voice;
    remoteCredentials(engine, nullptr, nullptr, &voice);
    return voice;
}

QString KoraCutVoiceOver::runnerPython(const QString &engine)
{
    if (QFileInfo::exists(kyutaiPython())) {
        return kyutaiPython();
    }
    if (engine == QLatin1String("local")) {
        return QString();
    }
    SpeechToTextWhisper stt;
    const QString whisper = stt.venvPythonExecs().python;
    if (!whisper.isEmpty() && QFileInfo::exists(whisper)) {
        return whisper;
    }
    return systemPython();
}

// ---------------------------------------------------------------------------------------------
// Les fichiers

QString KoraCutVoiceOver::projectVoiceFolder()
{
    const QString forced = qEnvironmentVariable("KORACUT_VOIX_DOSSIER");
    if (!forced.isEmpty()) {
        return forced; // essais
    }
    KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    QString base;
    if (doc && !doc->url().isEmpty() && QFileInfo::exists(doc->url().toLocalFile())) {
        base = QFileInfo(doc->url().toLocalFile()).absolutePath();
    } else if (doc) {
        base = doc->projectDataFolder();
    }
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/KoraCut");
    }
    return base + QStringLiteral("/Voix off");
}

QString KoraCutVoiceOver::fileNameFor(const QString &folder, const QString &text, const QString &name, int skip)
{
    // Le premier numéro libre du dossier (« 01 - … », « 02 - … »).
    int next = 1;
    const QStringList existing = QDir(folder).entryList({QStringLiteral("*.wav")}, QDir::Files);
    static const QRegularExpression numbered(QStringLiteral("^(\\d{2,3}) - "));
    for (const QString &f : existing) {
        const QRegularExpressionMatch m = numbered.match(f);
        if (m.hasMatch()) {
            next = std::max(next, m.captured(1).toInt() + 1);
        }
    }
    QString description = name.trimmed();
    if (description.isEmpty()) {
        const QStringList words = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        description = words.mid(0, 6).join(QLatin1Char(' '));
    }
    static const QRegularExpression forbidden(QStringLiteral("[/\\\\:*?\"<>|\\x{2026}.,;!]"));
    description.replace(forbidden, QString());
    description = description.simplified().left(48).trimmed();
    if (description.isEmpty()) {
        description = QStringLiteral("Voix off");
    }
    description[0] = description.at(0).toUpper();
    return QStringLiteral("%1 - %2.wav").arg(next + std::max(0, skip), 2, 10, QLatin1Char('0')).arg(description);
}

int KoraCutVoiceOver::voiceTrack(const std::shared_ptr<TimelineItemModel> &timeline)
{
    if (!timeline) {
        return -1;
    }
    for (int tid : KoraCutAgentToolsPrivate::tracksOnScreen(timeline)) {
        if (!timeline->isAudioTrack(tid)) {
            continue;
        }
        const QString name = plainText(timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString());
        if (name.startsWith(QLatin1String("voix off")) || name.startsWith(QLatin1String("voice over")) || name.startsWith(QLatin1String("voice-over"))) {
            return tid;
        }
    }
    return -1;
}

QJsonObject KoraCutVoiceOver::importAndPlace(const std::shared_ptr<TimelineItemModel> &timeline, const QStringList &files, const QList<int> &positions, int trackId,
                                             const QString &undoText, const QList<bool> &flexible, const QString &binFolder)
{
    using KoraCutAgentToolsPrivate::error;
    auto model = pCore ? pCore->projectItemModel() : nullptr;
    if (!model || !pCore->currentDoc()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    // Le dossier « Voix off » du chutier, créé dans la même étape s'il n'existe pas.
    QString folderId = model->getRootFolder()->clipId();
    const QString folderName = i18nc("@item bin folder and track name for generated voices", "Voice-over");
    const QString existingFolder = model->getFolderIdByName(folderName);
    if (!binFolder.trimmed().isEmpty()) {
        // Un dossier demandé par son chemin : chaque étage est pris s'il existe, créé sinon, dans la même étape.
        const QStringList parts = binFolder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString wanted = part.trimmed();
            auto parent = model->getFolderByBinId(folderId);
            QString found;
            for (int i = 0; parent && i < parent->childCount(); ++i) {
                auto child = std::static_pointer_cast<AbstractProjectItem>(parent->child(i));
                if (child->itemType() == AbstractProjectItem::FolderItem && child->name().compare(wanted, Qt::CaseInsensitive) == 0) {
                    found = child->clipId();
                    break;
                }
            }
            if (found.isEmpty() && !model->requestAddFolder(found, wanted, folderId, undo, redo)) {
                undo();
                return error(QStringLiteral("non_disponible"), QStringLiteral("Le dossier « %1 » du chutier n'a pas pu être créé.").arg(binFolder));
            }
            folderId = found;
        }
    } else if (!existingFolder.isEmpty() && model->getFolderByBinId(existingFolder)) {
        folderId = existingFolder;
    } else {
        QString newId;
        if (model->requestAddFolder(newId, folderName, folderId, undo, redo)) {
            folderId = newId;
        }
    }
    QSet<QString> ready;
    auto onReady = [&ready](const QString &binId) { ready.insert(binId); };
    QStringList ids;
    for (const QString &file : files) {
        const QString id = ClipCreator::createClipFromFile(file, folderId, model, undo, redo, onReady);
        if (id.isEmpty() || id == QLatin1String("-1")) {
            undo();
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le fichier %1 n'a pas pu être importé.").arg(QFileInfo(file).fileName()));
        }
        ids << id;
    }
    if (pCore->bin()) {
        pCore->bin()->shouldCheckProfile = false;
    }
    // Le clip doit être prêt avant d'être posé (quelques centaines de millisecondes pour un WAV).
    QElapsedTimer timer;
    timer.start();
    while (ready.size() < ids.size() && timer.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QJsonArray medias;
    int track = trackId;
    bool trackCreated = false;
    for (int i = 0; i < ids.size(); ++i) {
        QJsonObject m{{QStringLiteral("id"), ids.at(i)}, {QStringLiteral("fichier"), files.at(i)}};
        const int position = i < positions.size() ? positions.at(i) : -1;
        if (position >= 0 && timeline) {
            if (track < 0) {
                track = voiceTrack(timeline);
            }
            if (track < 0) {
                // Une piste audio « Voix off » sous les autres, dans la même étape.
                int newTrack = -1;
                if (timeline->requestTrackInsertion(0, newTrack, folderName, true, undo, redo)) {
                    track = newTrack;
                    trackCreated = true;
                }
            }
            const auto clip = model->getClipByBinID(ids.at(i));
            if (track >= 0 && clip && clip->statusReady()) {
                int clipId = -1;
                int at = position;
                if (i < flexible.size() && flexible.at(i)) {
                    // Place souple (tête de lecture, ou « à la suite ») : si elle est prise, la première
                    // place libre assez longue après elle, sur la même piste.
                    const int length = int(clip->frameDuration());
                    bool moved = true;
                    while (moved) {
                        moved = false;
                        for (int other : KoraCutAgentToolsPrivate::clipsOnTrack(timeline, track)) {
                            const int start = timeline->getClipPosition(other);
                            const int end = start + timeline->getClipPlaytime(other);
                            if (start < at + length && end > at) {
                                at = end;
                                moved = true;
                            }
                        }
                    }
                }
                if (timeline->requestClipInsertion(ids.at(i), track, at, clipId, false, true, false, undo, redo) && clipId >= 0) {
                    m.insert(QStringLiteral("clip_id"), clipId);
                    m.insert(QStringLiteral("position"), KoraCutAgentToolsPrivate::time(at));
                    if (at != position) {
                        m.insert(QStringLiteral("decalee"), true);
                    }
                    m.insert(QStringLiteral("duree"), KoraCutAgentToolsPrivate::time(timeline->getClipPlaytime(clipId)));
                } else {
                    const QString name = timeline->getTrackProperty(track, QStringLiteral("kdenlive:track_name")).toString();
                    m.insert(QStringLiteral("pose_refusee"), QStringLiteral("Pas de place à %1 sur la piste %2 : le son est dans le chutier.")
                                                                .arg(KoraCutAgentTools::timecode(position, KoraCutAgentToolsPrivate::projectFps()),
                                                                     name.isEmpty() ? KoraCutAgentToolsPrivate::trackLabel(timeline, track) : name));
                }
            } else if (clip && !clip->statusReady()) {
                m.insert(QStringLiteral("pose_refusee"), QStringLiteral("Le son n'était pas encore lu : il est dans le chutier, pose-le avec ajouter_clip."));
            }
        }
        medias.append(m);
    }
    pCore->pushUndo(undo, redo, undoText);
    QJsonObject out{{QStringLiteral("ok"), true}, {QStringLiteral("medias"), medias}};
    if (track >= 0) {
        out.insert(QStringLiteral("piste_id"), track);
        out.insert(QStringLiteral("piste_creee"), trackCreated);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// La génération

bool KoraCutVoiceOver::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void KoraCutVoiceOver::cancel()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process->deleteLater();
    }
    m_process.clear();
    if (m_sizeTimer) {
        m_sizeTimer->stop();
    }
}

void KoraCutVoiceOver::generate(const KoraCutVoiceRequest &request)
{
    const QString engine = request.engine.isEmpty() ? QStringLiteral("local") : request.engine;
    const QJsonObject state = engineState(engine);
    if (!state.value(QStringLiteral("pret")).toBool()) {
        const QString code = engine == QLatin1String("local") && localSupported() ? QStringLiteral("moteur_absent") : QStringLiteral("non_disponible");
        QTimer::singleShot(0, this, [this, code, state]() {
            Q_EMIT failed(code, state.value(QStringLiteral("raison")).toString(), state.value(QStringLiteral("conseil")).toString());
        });
        return;
    }
    QJsonArray phrases;
    for (const KoraCutVoicePhrase &p : request.phrases) {
        QJsonObject o{{QStringLiteral("id"), p.id}, {QStringLiteral("texte"), p.text}, {QStringLiteral("sortie"), p.output}};
        if (!p.say.isEmpty()) {
            o.insert(QStringLiteral("dire"), p.say);
        }
        phrases.append(o);
    }
    QJsonObject job{{QStringLiteral("mode"), QStringLiteral("generer")},
                    {QStringLiteral("moteur"), engine},
                    {QStringLiteral("phrases"), phrases},
                    {QStringLiteral("voix"), request.voice.isEmpty() ? defaultVoice(engine) : request.voice},
                    {QStringLiteral("ton"), request.tone},
                    {QStringLiteral("vitesse"), request.speed},
                    {QStringLiteral("prises"), request.takes}};
    if (engine != QLatin1String("local")) {
        QString key;
        QString base;
        remoteCredentials(engine, &key, &base, nullptr);
        job.insert(QStringLiteral("cle"), key);
        if (engine == QLatin1String("elevenlabs") || !base.isEmpty()) {
            job.insert(QStringLiteral("url_base"), base);
        }
    }
    // L'écoute : le Whisper de la coupe automatique, s'il est installé ; sinon la voix n'est pas vérifiée.
    SpeechToTextWhisper stt;
    const QString whisperPython = stt.venvPythonExecs().python;
    const QString whisperModel = KoraCutTranscriber::chooseModel();
    const QString listenScript = QFileInfo(scriptPath()).absolutePath() + QStringLiteral("/koracut_voix_ecoute.py");
    if (!whisperPython.isEmpty() && QFileInfo::exists(whisperPython) && !whisperModel.isEmpty() && QFileInfo::exists(listenScript) &&
        !qEnvironmentVariableIsSet("KORACUT_VOIX_SANS_ECOUTE")) {
        QJsonObject listen{{QStringLiteral("python"), whisperPython},
                           {QStringLiteral("script"), listenScript},
                           {QStringLiteral("modele"), whisperModel},
                           {QStringLiteral("langue"), QStringLiteral("fr")}};
        if (!KdenliveSettings::ffmpegpath().isEmpty()) {
            listen.insert(QStringLiteral("ffmpeg_dossier"), QFileInfo(KdenliveSettings::ffmpegpath()).absolutePath());
        }
        job.insert(QStringLiteral("ecoute"), listen);
    }
    m_mode = QStringLiteral("generer");
    start(runnerPython(engine), job);
}

void KoraCutVoiceOver::listRemoteVoices(const QString &engine)
{
    QString key;
    QString base;
    if (!remoteCredentials(engine, &key, &base, nullptr)) {
        QTimer::singleShot(0, this, [this, engine]() {
            Q_EMIT failed(QStringLiteral("non_disponible"), QStringLiteral("Aucune connexion %1 avec sa clé.").arg(engineName(engine)),
                          QStringLiteral("Préférences > Connexions IA."));
        });
        return;
    }
    QJsonObject job{{QStringLiteral("mode"), QStringLiteral("voix_distantes")}, {QStringLiteral("moteur"), engine}, {QStringLiteral("cle"), key}};
    if (!base.isEmpty()) {
        job.insert(QStringLiteral("url_base"), base);
    }
    m_mode = QStringLiteral("voix_distantes");
    start(runnerPython(engine), job);
}

KoraCutVoiceOver *KoraCutVoiceOver::installer()
{
    static QPointer<KoraCutVoiceOver> shared;
    if (!shared) {
        shared = new KoraCutVoiceOver(qApp);
    }
    return shared;
}

void KoraCutVoiceOver::install()
{
    if (m_installing) {
        return;
    }
    if (!localSupported()) {
        QTimer::singleShot(0, this, [this]() { Q_EMIT installFinished(QStringLiteral("Le moteur de voix local tourne pour l'instant sur les Mac à puce Apple seulement.")); });
        return;
    }
    const QString python = QFileInfo::exists(kyutaiPython()) ? kyutaiPython() : systemPython();
    if (python.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            Q_EMIT installFinished(i18nc("@info", "Python 3.10 or later is needed to install the voice engine. Install it from python.org, then try again."));
        });
        return;
    }
    m_installing = true;
    m_installPercent = 0;
    m_installStep = i18nc("@info voice engine install step", "Preparing");
    m_mode = QStringLiteral("installer");
    m_sizeAtStart = folderSize(modelDir());
    if (!m_sizeTimer) {
        // Le téléchargement du modèle ne dit rien : on regarde le dossier grossir, toutes les 2 s, et
        // seulement pendant l'installation.
        m_sizeTimer = new QTimer(this);
        m_sizeTimer->setInterval(2000);
        connect(m_sizeTimer, &QTimer::timeout, this, &KoraCutVoiceOver::pollInstallSize);
    }
    m_sizeTimer->start();
    start(python, QJsonObject{{QStringLiteral("mode"), QStringLiteral("installer")}, {QStringLiteral("python_systeme"), systemPython()}});
}

void KoraCutVoiceOver::pollInstallSize()
{
    // 0 à 10 % pour Python et le moteur, 10 à 99 % pour le modèle (d'après la taille du dossier).
    const qint64 grown = folderSize(modelDir()) - m_sizeAtStart;
    if (grown <= 0) {
        return;
    }
    const qint64 remaining = std::max<qint64>(1, kModelBytes - m_sizeAtStart);
    const int percent = std::max(m_installPercent, int(std::clamp<qint64>(10 + grown * 89 / remaining, 10, 99)));
    if (percent != m_installPercent) {
        m_installPercent = percent;
        Q_EMIT installProgress(m_installPercent, m_installStep);
    }
}

void KoraCutVoiceOver::start(const QString &python, const QJsonObject &job)
{
    cancel();
    m_buffer.clear();
    m_phrases = QJsonArray();
    m_failed = false;
    const QString script = scriptPath();
    if (python.isEmpty() || script.isEmpty()) {
        const bool install = m_mode == QLatin1String("installer");
        QTimer::singleShot(0, this, [this, install, python]() {
            const QString message = python.isEmpty() ? QStringLiteral("Python 3.10 ou plus récent est introuvable sur cette machine.")
                                                     : QStringLiteral("Le script de la voix off manque dans l'installation de KoraCut.");
            if (install) {
                m_installing = false;
                Q_EMIT installFinished(message);
            } else {
                Q_EMIT failed(QStringLiteral("non_disponible"), message, QStringLiteral("Installe Python depuis python.org, puis réessaie."));
            }
        });
        return;
    }
    QJsonObject full = job;
    if (!KdenliveSettings::ffmpegpath().isEmpty()) {
        full.insert(QStringLiteral("ffmpeg"), KdenliveSettings::ffmpegpath());
    }
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    // Le cache du modèle toujours au même endroit, celui qu'on annonce à la personne : sans HF_HOME,
    // Hugging Face suivrait XDG_CACHE_HOME quand il est posé (Craft, certains Linux) et ne trouverait
    // pas le modèle déjà téléchargé.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!env.contains(QStringLiteral("HF_HUB_CACHE")) && !env.contains(QStringLiteral("HF_HOME"))) {
        env.insert(QStringLiteral("HF_HOME"), QDir::homePath() + QStringLiteral("/.cache/huggingface"));
    }
    // Python parle UTF-8 dans les deux sens, quel que soit le système : sous Windows, son entrée et sa sortie
    // suivent sinon la page de code (cp1252), et tout texte accentué arrive abîmé ou fait échouer le script
    // (« Aucune prise n'a été produite », vu sur la compilation Windows du 11 octobre 2026).
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_buffer += m_process->readAllStandardOutput();
        int nl;
        while ((nl = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(nl).trimmed();
            m_buffer.remove(0, nl + 1);
            onLine(line);
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_buffer += m_process->readAllStandardOutput();
        if (!m_buffer.trimmed().isEmpty()) {
            onLine(m_buffer.trimmed());
            m_buffer.clear();
        }
        const QByteArray errors = m_process->readAllStandardError();
        m_process->deleteLater();
        m_process.clear();
        const bool install = m_mode == QLatin1String("installer");
        if (install) {
            m_installing = false;
            if (m_sizeTimer) {
                m_sizeTimer->stop();
            }
        }
        if (m_failed) {
            return; // déjà dit par KORACUT-VOIX-ERREUR
        }
        if (status != QProcess::NormalExit || code != 0) {
            qWarning() << "KoraCut: voix off, le script s'est arrêté" << code << errors.right(1500);
            const QString message = i18nc("@info", "The voice-over script stopped unexpectedly. The details are in the application log.");
            if (install) {
                Q_EMIT installFinished(message);
            } else {
                Q_EMIT failed(QStringLiteral("non_disponible"), message, QString());
            }
            return;
        }
        if (install) {
            m_installPercent = 100;
            Q_EMIT installProgress(100, m_installStep);
            Q_EMIT installFinished(QString());
        } else if (m_mode == QLatin1String("generer")) {
            Q_EMIT finished(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("phrases"), m_phrases}});
        }
    });
    m_process->start(python, {script});
    m_process->write(QJsonDocument(full).toJson(QJsonDocument::Compact));
    m_process->closeWriteChannel();
}

void KoraCutVoiceOver::onLine(const QByteArray &line)
{
    const int space = line.indexOf(' ');
    if (space < 0 || !line.startsWith("KORACUT-VOIX-")) {
        return;
    }
    const QByteArray tag = line.left(space);
    const QJsonDocument doc = QJsonDocument::fromJson(line.mid(space + 1));
    if (tag == "KORACUT-VOIX-ETAPE") {
        const QJsonObject o = doc.object();
        const QString text = o.value(QStringLiteral("texte")).toString();
        if (m_mode == QLatin1String("installer")) {
            m_installStep = text;
            const int done = o.value(QStringLiteral("fait")).toInt();
            // Python et le moteur : jusqu'à 10 % ; le modèle suit par la taille du dossier.
            m_installPercent = std::max(m_installPercent, done >= 3 ? 100 : done * 5);
            Q_EMIT installProgress(m_installPercent, text);
        } else {
            Q_EMIT progress(text, o.value(QStringLiteral("fait")).toInt(), o.value(QStringLiteral("total")).toInt());
        }
    } else if (tag == "KORACUT-VOIX-PHRASE") {
        m_phrases.append(doc.object());
        Q_EMIT phraseReady(doc.object());
    } else if (tag == "KORACUT-VOIX-LISTE") {
        Q_EMIT remoteVoices(doc.array());
    } else if (tag == "KORACUT-VOIX-ERREUR") {
        m_failed = true;
        const QJsonObject o = doc.object();
        const QString code = o.value(QStringLiteral("code")).toString();
        const QString message = o.value(QStringLiteral("message")).toString();
        if (m_mode == QLatin1String("installer")) {
            m_installing = false;
            if (m_sizeTimer) {
                m_sizeTimer->stop();
            }
            Q_EMIT installFinished(message);
            return;
        }
        QString advice;
        if (code == QLatin1String("cle_refusee") || code == QLatin1String("cle_absente")) {
            advice = QStringLiteral("Préférences > Connexions IA : vérifie la clé du service de voix.");
        } else if (code == QLatin1String("credit_epuise")) {
            advice = QStringLiteral("Essaie le moteur local (moteur: local), ou recharge le compte du service.");
        } else if (code == QLatin1String("moteur_absent")) {
            advice = QStringLiteral("Demande l'accord de la personne, puis appelle voix_off_installer.");
        }
        Q_EMIT failed(code, message, advice);
    }
}
