/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracuthwdecode.h"

#include "kdenlivesettings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <atomic>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

namespace {

const char kEnvName[] = "MLT_AVFORMAT_HWACCEL";
// Au-delà, on repart d'un fichier de verdicts vide : il ne doit pas grossir sans fin.
const int kMaxVerdicts = 4000;

enum class Outcome { Decodes, Fails, Unknown };

struct Entry
{
    QString name;
    QString path;
    QString codec;
    QString pixFmt;
    bool legacy{false};
    KoraCutHwDecode::State state{KoraCutHwDecode::State::Ok};
    // Le décodeur pour lequel l'état vaut (vide : aucun essai demandé).
    QString hw;
};

struct Shared
{
    QMutex mutex;
    QHash<const void *, Entry> entries;
    // « décodeur|chemin|taille|date » → décodable par la puce.
    QHash<QString, bool> verdicts;
    bool verdictsLoaded{false};
    // Vide : le fichier du cache de l'application. Les tests en posent un autre.
    QString verdictsPath;
    QSet<QString> running;
    KoraCutHwDecode::Trial trial;
    QString applied;
    bool appliedOnce{false};
    bool loggedSoftware{false};
    QThreadPool pool;
    std::atomic<bool> quitting{false};
};

// Jamais détruit : le fil des essais ne doit pas retenir la fermeture de l'application.
Shared &shared()
{
    static Shared *s = []() {
        auto *created = new Shared;
        created->pool.setMaxThreadCount(1);
        created->pool.setThreadPriority(QThread::LowPriority);
        return created;
    }();
    return *s;
}

// Verrou tenu.
QString verdictsFile(const Shared &s)
{
    return s.verdictsPath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/koracut-decodage.json")
                                    : s.verdictsPath;
}

QString verdictKey(const QString &hw, const QString &path)
{
    const QFileInfo info(path);
    return QStringLiteral("%1|%2|%3|%4").arg(hw, info.absoluteFilePath()).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
}

// Verrou tenu.
void loadVerdicts(Shared &s)
{
    if (s.verdictsLoaded) {
        return;
    }
    s.verdictsLoaded = true;
    QFile file(verdictsFile(s));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject list = root.value(QStringLiteral("verdicts")).toObject();
    for (auto it = list.constBegin(); it != list.constEnd(); ++it) {
        s.verdicts.insert(it.key(), it.value().toBool());
    }
}

// Verrou tenu.
void saveVerdicts(Shared &s)
{
    if (s.verdicts.size() > kMaxVerdicts) {
        s.verdicts.clear();
    }
    QJsonObject list;
    for (auto it = s.verdicts.constBegin(); it != s.verdicts.constEnd(); ++it) {
        list.insert(it.key(), it.value());
    }
    const QString path = verdictsFile(s);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("verdicts"), list}}).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

bool trialsAllowed(const Shared &s)
{
    // Dans les tests, pas de ffmpeg lancé dans le dos : seul un essai posé par le test tourne.
    return bool(s.trial) || !qEnvironmentVariableIsSet("MLT_TESTS");
}

Outcome runFfmpeg(const QString &ffmpeg, const QString &hw, const QString &path)
{
    if (ffmpeg.isEmpty() || !QFileInfo(ffmpeg).isFile() || !QFileInfo::exists(path)) {
        return Outcome::Unknown;
    }
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
#if defined(Q_OS_UNIX)
    proc.setChildProcessModifier([]() { ::setpriority(PRIO_PROCESS, 0, 10); });
#endif
    // Douze images depuis le début : l'image clé passe toujours, ce sont les suivantes qui échouent.
    proc.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-v"),       QStringLiteral("error"), QStringLiteral("-hwaccel"),
                        hw == QLatin1String("nvdec") ? QStringLiteral("cuda") : hw, QStringLiteral("-i"),    path,
                        QStringLiteral("-map"),         QStringLiteral("0:v:0"),    QStringLiteral("-frames:v"), QStringLiteral("12"),
                        QStringLiteral("-an"),          QStringLiteral("-sn"),      QStringLiteral("-dn"),   QStringLiteral("-f"),
                        QStringLiteral("null"),         QStringLiteral("-")});
    if (!proc.waitForStarted(5000)) {
        return Outcome::Unknown;
    }
    if (!proc.waitForFinished(20000)) {
        proc.kill();
        proc.waitForFinished(2000);
        return Outcome::Unknown;
    }
    if (proc.exitStatus() != QProcess::NormalExit) {
        return Outcome::Unknown;
    }
    // Le code de sortie ne dit rien : ffmpeg échoue sur un AV1 sans puce alors que MLT le lit très bien.
    return KoraCutHwDecode::outputSaysRefused(QString::fromUtf8(proc.readAllStandardError())) ? Outcome::Fails : Outcome::Decodes;
}

void notify(const QStringList &refused, const QList<bool> &legacy)
{
    KoraCutHwDecode *self = KoraCutHwDecode::instance();
    QMetaObject::invokeMethod(
        self,
        [self, refused, legacy]() {
            for (int i = 0; i < refused.size(); ++i) {
                Q_EMIT self->clipRefused(refused.at(i), legacy.at(i));
            }
            Q_EMIT self->changed();
        },
        Qt::QueuedConnection);
}

void finishTrial(const QString &key, const QString &hw, const QString &path, Outcome outcome)
{
    Shared &s = shared();
    QStringList refused;
    QList<bool> legacy;
    {
        QMutexLocker lock(&s.mutex);
        s.running.remove(key);
        if (outcome != Outcome::Unknown) {
            loadVerdicts(s);
            s.verdicts.insert(key, outcome == Outcome::Decodes);
            saveVerdicts(s);
        }
        bool wasLegacy = false;
        bool touched = false;
        for (auto it = s.entries.begin(); it != s.entries.end(); ++it) {
            if (it->path == path && it->hw == hw && it->state == KoraCutHwDecode::State::Pending) {
                // Essai impossible (pas de ffmpeg, délai) : dans le doute, le processeur.
                it->state = outcome == Outcome::Decodes ? KoraCutHwDecode::State::Ok : KoraCutHwDecode::State::Refused;
                wasLegacy = wasLegacy || it->legacy;
                touched = true;
            }
        }
        if (touched && outcome != Outcome::Decodes) {
            refused << path;
            legacy << wasLegacy;
        }
    }
    KoraCutHwDecode::apply();
    notify(refused, legacy);
}

// Verrou tenu. Rend vrai si le clip vient d'être refusé (verdict déjà connu).
bool evaluate(Shared &s, Entry &entry, const QString &wantedHw)
{
    const QString hw = !wantedHw.isEmpty() ? wantedHw : (entry.legacy ? KoraCutHwDecode::platformDecoder() : QString());
    entry.hw = hw;
    if (hw.isEmpty() || !trialsAllowed(s) || !KoraCutHwDecode::needsTrial(hw, entry.codec)) {
        entry.state = KoraCutHwDecode::State::Ok;
        return false;
    }
    loadVerdicts(s);
    const QString key = verdictKey(hw, entry.path);
    const auto known = s.verdicts.constFind(key);
    if (known != s.verdicts.constEnd()) {
        entry.state = known.value() ? KoraCutHwDecode::State::Ok : KoraCutHwDecode::State::Refused;
        return !known.value();
    }
    entry.state = KoraCutHwDecode::State::Pending;
    if (!s.running.contains(key)) {
        s.running.insert(key);
        const QString path = entry.path;
        const QString ffmpeg = KdenliveSettings::ffmpegpath();
        const KoraCutHwDecode::Trial trial = s.trial;
        s.pool.start([key, hw, path, ffmpeg, trial]() {
            Shared &inner = shared();
            if (inner.quitting.load()) {
                return;
            }
            Outcome outcome;
            if (trial) {
                outcome = trial(hw, path) ? Outcome::Decodes : Outcome::Fails;
            } else {
                outcome = runFfmpeg(ffmpeg, hw, path);
            }
            finishTrial(key, hw, path, outcome);
        });
    }
    return false;
}

} // namespace

KoraCutHwDecode *KoraCutHwDecode::instance()
{
    static KoraCutHwDecode *self = []() {
        auto *created = new KoraCutHwDecode;
        if (QCoreApplication *app = QCoreApplication::instance()) {
            // Créé parfois depuis un fil de chargement : les signaux partent du fil principal.
            created->moveToThread(app->thread());
            QObject::connect(app, &QCoreApplication::aboutToQuit, created, []() {
                Shared &s = shared();
                s.quitting.store(true);
                s.pool.clear();
            });
        }
        return created;
    }();
    return self;
}

QString KoraCutHwDecode::wanted()
{
    return KdenliveSettings::hwDecoding();
}

QString KoraCutHwDecode::platformDecoder()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("videotoolbox");
#else
    return QString();
#endif
}

bool KoraCutHwDecode::needsTrial(const QString &hw, const QString &codec)
{
    // Vu sur VideoToolbox seulement : la session s'ouvre, puis chaque image hors image clé échoue.
    // Les autres décodeurs (D3D11VA, VAAPI, NVDEC) refusent à l'ouverture et MLT retombe proprement.
    if (hw != QLatin1String("videotoolbox")) {
        return false;
    }
    if (codec.isEmpty()) {
        return true;
    }
    static const QSet<QString> candidates{QStringLiteral("h264"),       QStringLiteral("hevc"),  QStringLiteral("prores"),
                                          QStringLiteral("mpeg1video"), QStringLiteral("mpeg2video"), QStringLiteral("mpeg4"),
                                          QStringLiteral("h263"),       QStringLiteral("vp9"),   QStringLiteral("av1")};
    return candidates.contains(codec);
}

bool KoraCutHwDecode::outputSaysRefused(const QString &ffmpegStderr)
{
    return ffmpegStderr.contains(QLatin1String("hardware accelerator failed to decode picture"), Qt::CaseInsensitive);
}

QString KoraCutHwDecode::describeFormat(const QString &codec, const QString &pixFmt)
{
    static const QHash<QString, QString> names{{QStringLiteral("h264"), QStringLiteral("H.264")},
                                               {QStringLiteral("hevc"), QStringLiteral("HEVC")},
                                               {QStringLiteral("prores"), QStringLiteral("ProRes")},
                                               {QStringLiteral("mpeg2video"), QStringLiteral("MPEG-2")},
                                               {QStringLiteral("mpeg4"), QStringLiteral("MPEG-4")},
                                               {QStringLiteral("vp9"), QStringLiteral("VP9")},
                                               {QStringLiteral("av1"), QStringLiteral("AV1")}};
    QString text = names.value(codec, codec.toUpper());
    static const QRegularExpression re(QStringLiteral("^yuv[aj]?(4)(\\d)(\\d)p(\\d+)?"));
    const QRegularExpressionMatch m = re.match(pixFmt);
    if (m.hasMatch()) {
        const QString bits = m.captured(4).isEmpty() ? QStringLiteral("8") : m.captured(4);
        const QString chroma = QStringLiteral("%1:%2:%3").arg(m.captured(1), m.captured(2), m.captured(3));
        text = text.isEmpty() ? QStringLiteral("%1 %2 bits").arg(chroma, bits) : QStringLiteral("%1 %2 %3 bits").arg(text, chroma, bits);
    }
    return text;
}

QString KoraCutHwDecode::effective()
{
    const QString hw = wanted();
    if (hw.isEmpty()) {
        return QString();
    }
    Shared &s = shared();
    QMutexLocker lock(&s.mutex);
    for (auto it = s.entries.constBegin(); it != s.entries.constEnd(); ++it) {
        if (it->state != State::Ok && it->hw == hw) {
            return QString();
        }
    }
    return hw;
}

void KoraCutHwDecode::apply()
{
    const QString hw = wanted();
    Shared &s = shared();
    QStringList refused;
    QList<bool> legacy;
    QString now = hw;
    QStringList culprits;
    bool changedNow = false;
    {
        QMutexLocker lock(&s.mutex);
        for (auto it = s.entries.begin(); it != s.entries.end(); ++it) {
            // Le réglage a changé depuis l'essai de ce clip : on le reprend.
            const QString expected = !hw.isEmpty() ? hw : (it->legacy ? platformDecoder() : QString());
            if (it->hw != expected && evaluate(s, *it, hw)) {
                refused << it->path;
                legacy << it->legacy;
            }
            if (!hw.isEmpty() && it->hw == hw && it->state != State::Ok) {
                now.clear();
                if (it->state == State::Refused) {
                    const QString format = describeFormat(it->codec, it->pixFmt);
                    const QString line = format.isEmpty() ? it->name : QStringLiteral("%1 (%2)").arg(it->name, format);
                    if (!culprits.contains(line)) {
                        culprits << line;
                    }
                }
            }
        }
        if (!s.appliedOnce || s.applied != now) {
            changedNow = s.appliedOnce;
            s.appliedOnce = true;
            s.applied = now;
            // Rien demandé et rien de posé : on ne touche pas à l'environnement.
            if (!now.isEmpty() || qEnvironmentVariableIsSet(kEnvName)) {
                qputenv(kEnvName, now.toUtf8());
            }
        }
        if (now.isEmpty() && !culprits.isEmpty() && !s.loggedSoftware) {
            s.loggedSoftware = true;
            const int more = int(culprits.size()) - 5;
            qInfo().noquote() << "KORACUT-PERF décodage logiciel pour ce projet :" << hw << "ne décode pas" << culprits.mid(0, 5).join(QStringLiteral(", "))
                              << (more > 0 ? QStringLiteral("et %1 autres").arg(more) : QString());
        } else if (!now.isEmpty() && s.loggedSoftware) {
            s.loggedSoftware = false;
            qInfo().noquote() << "KORACUT-PERF décodage" << now << ": tous les clips du projet passent par la puce";
        }
    }
    if (changedNow || !refused.isEmpty()) {
        notify(refused, legacy);
    }
}

void KoraCutHwDecode::applyTo(QProcessEnvironment &env)
{
    const QString hw = effective();
    if (hw.isEmpty()) {
        env.remove(QLatin1String(kEnvName));
    } else {
        env.insert(QLatin1String(kEnvName), hw);
    }
}

void KoraCutHwDecode::noteClip(const void *owner, const QString &name, const QString &path, const QString &codec, const QString &pixFmt, bool legacy)
{
    if (owner == nullptr || path.isEmpty()) {
        return;
    }
    instance();
    Shared &s = shared();
    bool refusedNow = false;
    {
        QMutexLocker lock(&s.mutex);
        Entry entry;
        entry.name = name;
        entry.path = path;
        entry.codec = codec;
        entry.pixFmt = pixFmt;
        entry.legacy = legacy;
        refusedNow = evaluate(s, entry, wanted());
        s.entries.insert(owner, entry);
    }
    // Tout de suite, dans ce fil : le fichier ne doit pas s'ouvrir avec la puce avant le verdict.
    apply();
    if (refusedNow) {
        notify({path}, {legacy});
    }
}

void KoraCutHwDecode::forgetClip(const void *owner)
{
    Shared &s = shared();
    {
        QMutexLocker lock(&s.mutex);
        if (s.entries.remove(owner) == 0) {
            return;
        }
    }
    apply();
}

QList<KoraCutHwDecode::Clip> KoraCutHwDecode::softwareClips()
{
    Shared &s = shared();
    QList<Clip> clips;
    QSet<QString> seen;
    QMutexLocker lock(&s.mutex);
    for (auto it = s.entries.constBegin(); it != s.entries.constEnd(); ++it) {
        if (it->state == State::Ok || seen.contains(it->path)) {
            continue;
        }
        seen.insert(it->path);
        clips << Clip{it->name, it->path, describeFormat(it->codec, it->pixFmt), it->state};
    }
    std::sort(clips.begin(), clips.end(), [](const Clip &a, const Clip &b) { return a.name < b.name; });
    return clips;
}

QJsonObject KoraCutHwDecode::stateJson()
{
    const QString hw = wanted();
    const QString now = effective();
    QJsonArray list;
    const QList<Clip> clips = softwareClips();
    for (const Clip &clip : clips) {
        list << QJsonObject{{QStringLiteral("nom"), clip.name},
                            {QStringLiteral("format"), clip.format},
                            {QStringLiteral("etat"), clip.state == State::Refused ? QStringLiteral("refuse") : QStringLiteral("essai")}};
    }
    return QJsonObject{{QStringLiteral("demande"), hw.isEmpty() ? QStringLiteral("logiciel") : hw},
                       {QStringLiteral("effectif"), now.isEmpty() ? QStringLiteral("logiciel") : now},
                       {QStringLiteral("clips_logiciel"), list}};
}

bool KoraCutHwDecode::waitForTrials(int msecs)
{
    Shared &s = shared();
    QElapsedTimer clock;
    clock.start();
    while (true) {
        {
            QMutexLocker lock(&s.mutex);
            if (s.running.isEmpty()) {
                return true;
            }
        }
        if (clock.elapsed() >= msecs) {
            return false;
        }
        QThread::msleep(20);
    }
}

void KoraCutHwDecode::setTrial(const Trial &trial)
{
    Shared &s = shared();
    QMutexLocker lock(&s.mutex);
    s.trial = trial;
}

bool KoraCutHwDecode::ffmpegTrial(const QString &ffmpeg, const QString &hw, const QString &path)
{
    return runFfmpeg(ffmpeg, hw, path) == Outcome::Decodes;
}

void KoraCutHwDecode::resetForTests(const QString &verdictsPath)
{
    waitForTrials(30000);
    Shared &s = shared();
    QMutexLocker lock(&s.mutex);
    s.entries.clear();
    s.verdicts.clear();
    // Jamais le fichier de la personne : un test travaille sur le sien, puis rend la main.
    s.verdictsPath = verdictsPath;
    s.verdictsLoaded = false;
    if (!verdictsPath.isEmpty()) {
        QFile::remove(verdictsPath);
    }
    s.trial = nullptr;
    s.loggedSoftware = false;
    s.appliedOnce = false;
    s.applied.clear();
}

KoraCutHwDecode::Hold::Hold(const QString &path)
{
    KoraCutHwDecode::noteClip(this, QFileInfo(path).fileName(), path, QString(), QString(), false);
}

KoraCutHwDecode::Hold::~Hold()
{
    KoraCutHwDecode::forgetClip(this);
}
