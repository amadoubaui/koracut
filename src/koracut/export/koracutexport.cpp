/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutexport.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutvideotypes.h"
#include "koracutexportqueue.h"
#include "monitor/monitor.h"
#include "render/renderrequest.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "renderpresets/renderpresetrepository.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace KoraCutExport {

namespace {

bool french()
{
    return KLocalizedString::languages().value(0).startsWith(QLatin1String("fr"));
}

QList<Model> load()
{
    QList<Model> list;
    QFile file(QStringLiteral(":/data/koracut-export-models.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return list;
    }
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("modeles")).toArray();
    const bool fr = french();
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        Model m;
        m.id = o.value(QStringLiteral("id")).toString();
        m.name = o.value(fr ? QStringLiteral("nom") : QStringLiteral("nom_en")).toString();
        m.detail = o.value(fr ? QStringLiteral("detail") : QStringLiteral("detail_en")).toString();
        m.platform = o.value(QStringLiteral("famille")).toString() == QLatin1String("plateforme");
        m.logo = o.value(QStringLiteral("logo")).toString();
        m.logoColor = QColor(o.value(QStringLiteral("logo_couleur")).toString());
        m.logoBackground = QColor(o.value(QStringLiteral("logo_fond")).toString());
        m.badge = o.value(QStringLiteral("badge")).toString();
        m.width = o.value(QStringLiteral("largeur")).toInt();
        m.height = o.value(QStringLiteral("hauteur")).toInt();
        m.orientation = o.value(QStringLiteral("orientation")).toString();
        m.maxFps = o.value(QStringLiteral("cadence_max")).toInt();
        m.container = o.value(QStringLiteral("conteneur")).toString();
        m.video = o.value(QStringLiteral("video")).toString();
        m.profile = o.value(QStringLiteral("profil")).toString();
        m.bitrateKbps = o.value(QStringLiteral("debit_kbps")).toInt();
        m.bitrateKbps60 = o.value(QStringLiteral("debit_kbps_60")).toInt();
        m.audio = o.value(QStringLiteral("audio")).toString();
        m.audioKbps = o.value(QStringLiteral("audio_kbps")).toInt(192);
        m.sampleRate = o.value(QStringLiteral("frequence")).toInt(48000);
        m.lufs = o.value(QStringLiteral("lufs")).toInt();
        m.peak = o.value(QStringLiteral("crete")).toDouble(-1.);
        m.maxSeconds = o.value(QStringLiteral("duree_max_s")).toInt();
        m.maxMegabytes = o.value(QStringLiteral("taille_max_mo")).toInt();
        m.guides = o.value(QStringLiteral("reperes")).toString();
        for (const QJsonValue &a : o.value(QStringLiteral("anciens")).toArray()) {
            m.legacyIds << a.toString();
        }
        if (m.isValid()) {
            list << m;
        }
    }
    return list;
}

int even(double v)
{
    const int i = int(std::lround(v));
    return std::max(2, i - (i % 2));
}

QString sanitize(QString name)
{
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|\\n\\r\\t]"));
    name.replace(bad, QStringLiteral("-"));
    return name.simplified();
}

QString orientationOf(const QSize &size)
{
    if (size.width() <= 0 || size.height() <= 0) {
        return QString();
    }
    const double r = double(size.width()) / size.height();
    if (std::abs(r - 1.) < 0.02) {
        return QStringLiteral("c");
    }
    if (r > 1.) {
        return QStringLiteral("h");
    }
    return std::abs(r - 0.8) < 0.03 ? QStringLiteral("p") : QStringLiteral("v");
}

} // namespace

const QList<Model> &models()
{
    static const QList<Model> list = load();
    return list;
}

const Model *model(const QString &id)
{
    for (const Model &m : models()) {
        if (m.id == id) {
            return &m;
        }
    }
    for (const Model &m : models()) {
        if (m.legacyIds.contains(id)) {
            return &m;
        }
    }
    return nullptr;
}

bool modelFits(const Model &m, const QSize &sequence)
{
    if (m.width <= 0 || m.height <= 0 || sequence.isEmpty()) {
        return true;
    }
    const double a = double(m.width) / m.height;
    const double b = double(sequence.width()) / sequence.height();
    return std::abs(a - b) < 0.02;
}

QJsonObject Settings::toJson() const
{
    return QJsonObject{{QStringLiteral("modele"), model},
                       {QStringLiteral("video"), video},
                       {QStringLiteral("audio"), audio},
                       {QStringLiteral("conteneur"), container},
                       {QStringLiteral("codec"), videoCodec},
                       {QStringLiteral("materiel"), hardware},
                       {QStringLiteral("profil"), profile},
                       {QStringLiteral("largeur"), width},
                       {QStringLiteral("hauteur"), height},
                       {QStringLiteral("debit_kbps"), bitrateKbps},
                       {QStringLiteral("images_cles"), keyframes},
                       {QStringLiteral("reorganisation"), reorder},
                       {QStringLiteral("codec_audio"), audioCodec},
                       {QStringLiteral("audio_kbps"), audioKbps},
                       {QStringLiteral("frequence"), sampleRate},
                       {QStringLiteral("canaux"), channels},
                       {QStringLiteral("normaliser"), normalize},
                       {QStringLiteral("lufs"), lufs},
                       {QStringLiteral("crete"), peak},
                       {QStringLiteral("sous_titres"), subtitles},
                       {QStringLiteral("dossier"), folder},
                       {QStringLiteral("nom"), name},
                       {QStringLiteral("entree_sortie"), inOut},
                       {QStringLiteral("modifie"), customized}};
}

Settings Settings::fromJson(const QJsonObject &o)
{
    Settings s;
    s.model = o.value(QStringLiteral("modele")).toString();
    s.video = o.value(QStringLiteral("video")).toBool(true);
    s.audio = o.value(QStringLiteral("audio")).toBool(true);
    s.container = o.value(QStringLiteral("conteneur")).toString();
    s.videoCodec = o.value(QStringLiteral("codec")).toString();
    s.hardware = o.value(QStringLiteral("materiel")).toBool(false);
    s.profile = o.value(QStringLiteral("profil")).toString();
    s.width = o.value(QStringLiteral("largeur")).toInt();
    s.height = o.value(QStringLiteral("hauteur")).toInt();
    s.bitrateKbps = o.value(QStringLiteral("debit_kbps")).toInt();
    s.keyframes = o.value(QStringLiteral("images_cles")).toInt();
    s.reorder = o.value(QStringLiteral("reorganisation")).toBool(true);
    s.audioCodec = o.value(QStringLiteral("codec_audio")).toString();
    s.audioKbps = o.value(QStringLiteral("audio_kbps")).toInt(192);
    s.sampleRate = o.value(QStringLiteral("frequence")).toInt(48000);
    s.channels = o.value(QStringLiteral("canaux")).toInt(2);
    s.normalize = o.value(QStringLiteral("normaliser")).toBool(false);
    s.lufs = o.value(QStringLiteral("lufs")).toInt(-14);
    s.peak = o.value(QStringLiteral("crete")).toDouble(-1.);
    s.subtitles = o.value(QStringLiteral("sous_titres")).toString(QStringLiteral("incruster"));
    s.folder = o.value(QStringLiteral("dossier")).toString();
    s.name = o.value(QStringLiteral("nom")).toString(QStringLiteral("{projet} - {modele}"));
    s.inOut = o.value(QStringLiteral("entree_sortie")).toBool(false);
    s.customized = o.value(QStringLiteral("modifie")).toBool(false);
    return s;
}

Settings Settings::forModel(const Model &m, const QSize &sequence, double fps)
{
    Settings s;
    s.model = m.id;
    s.video = m.hasVideo();
    s.audio = m.hasAudio();
    s.container = m.container;
    s.videoCodec = m.video;
    s.profile = m.profile;
    // La taille visée seulement si la séquence a le même rapport d'image : comme Resolve, on ne
    // déforme jamais une séquence 16:9 pour en faire un 9:16 (il faut une séquence verticale).
    if (m.width > 0 && m.height > 0 && modelFits(m, sequence) && QSize(m.width, m.height) != sequence) {
        s.width = m.width;
        s.height = m.height;
    } else if (m.width > 0 && m.height <= 0 && !sequence.isEmpty()) {
        s.width = even(m.width);
        s.height = even(double(m.width) * sequence.height() / sequence.width());
    }
    s.bitrateKbps = (fps >= 47. && m.bitrateKbps60 > 0) ? m.bitrateKbps60 : m.bitrateKbps;
    s.hardware = (m.video == QLatin1String("h264") || m.video == QLatin1String("h265")) && hardwareAvailable(m.video);
    s.audioCodec = m.audio;
    s.audioKbps = m.audioKbps;
    s.sampleRate = m.sampleRate;
    s.normalize = m.lufs != 0;
    s.lufs = m.lufs != 0 ? m.lufs : -14;
    s.peak = m.peak < 0 ? m.peak : -1.;
    s.subtitles = QStringLiteral("incruster");
    return s;
}

QString Settings::extension() const
{
    return container.isEmpty() ? QStringLiteral("mp4") : container;
}

QSize Settings::outputSize(const QSize &sequence) const
{
    if (width > 0 && height > 0) {
        return QSize(width, height);
    }
    return sequence;
}

double Settings::estimatedKbps(const QSize &sequence, double fps) const
{
    double kbps = 0.;
    const QSize out = outputSize(sequence);
    const double pixels = double(out.width()) * out.height() * std::max(1., fps);
    if (video) {
        if (videoCodec == QLatin1String("prores_hq")) {
            kbps += 4.2 * pixels / 1000.;
        } else if (videoCodec == QLatin1String("prores_4444")) {
            kbps += 6.3 * pixels / 1000.;
        } else if (videoCodec == QLatin1String("gif")) {
            kbps += 1.2 * pixels / 1000.;
        } else if (bitrateKbps > 0) {
            // Un plafond est rarement atteint sur toute la durée : un peu moins que le plafond.
            kbps += bitrateKbps * 0.9;
        } else {
            kbps += (videoCodec == QLatin1String("h265") ? 0.08 : 0.14) * pixels / 1000.;
        }
    }
    if (audio) {
        if (audioCodec.startsWith(QLatin1String("pcm"))) {
            kbps += sampleRate * channels * (audioCodec == QLatin1String("pcm24") ? 24. : 16.) / 1000.;
        } else {
            kbps += audioKbps;
        }
    }
    return kbps;
}

bool signatureEnabled()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("exportSignature", true);
}

void setSignatureEnabled(bool on)
{
    KConfigGroup g = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
    g.writeEntry("exportSignature", on);
    g.sync();
}

QString signatureText()
{
    return i18nc("@info written in the metadata of every exported file; no quotes nor apostrophes",
                 "Made with KoraCut, the free and open source editing app. koracut.org");
}

QString shareLink()
{
    return QStringLiteral("https://koracut.org/?utm_source=app&utm_medium=partage&utm_campaign=export");
}

QString shareText()
{
    return i18nc("@info text ready to paste on a social network after an export, %1 is the link",
                 "I edited this video with KoraCut, a free and open source editing app. You can plug your own AI model or agent into it, "
                 "or edit by hand. %1 #KoraCut",
                 shareLink());
}

RenderPresetParams Settings::params(const QSize &sequence, double fps) const
{
    RenderPresetParams p;
    const QString ext = extension();
    static const QHash<QString, QString> formats = {{QStringLiteral("mp4"), QStringLiteral("mp4")}, {QStringLiteral("mov"), QStringLiteral("mov")},
                                                    {QStringLiteral("wav"), QStringLiteral("wav")}, {QStringLiteral("m4a"), QStringLiteral("ipod")},
                                                    {QStringLiteral("mp3"), QStringLiteral("mp3")}, {QStringLiteral("gif"), QStringLiteral("gif")}};
    p.insert(QStringLiteral("f"), formats.value(ext, ext));
    if (ext == QLatin1String("mp4") || ext == QLatin1String("mov") || ext == QLatin1String("m4a")) {
        // Le fichier se lit avant d'être téléchargé en entier (« Optimisation du réseau » de Resolve).
        p.insert(QStringLiteral("movflags"), QStringLiteral("+faststart"));
    }
    if (signatureEnabled()) {
        // Une ligne dans les informations du fichier, jamais sur l'image : c'est ainsi que KoraCut se fait connaître.
        p.insert(QStringLiteral("meta.attr.comment.markup"), signatureText());
    }
    const bool withVideo = video && !videoCodec.isEmpty() && ext != QLatin1String("wav") && ext != QLatin1String("m4a") && ext != QLatin1String("mp3");
    if (!withVideo) {
        p.insert(QStringLiteral("vn"), QStringLiteral("1"));
        p.insert(QStringLiteral("video_off"), QStringLiteral("1"));
    } else {
        const QSize out = outputSize(sequence);
        if (out != sequence && out.isValid()) {
            p.insert(QStringLiteral("width"), QString::number(out.width()));
            p.insert(QStringLiteral("height"), QString::number(out.height()));
        }
        const double pixels = double(out.width()) * out.height() * std::max(1., fps);
        const int autoHardwareKbps = std::clamp(int((videoCodec == QLatin1String("h265") ? 0.1 : 0.16) * pixels / 1000.), 2000, 150000);
        if (videoCodec == QLatin1String("h264") || videoCodec == QLatin1String("h265")) {
            const bool h265 = videoCodec == QLatin1String("h265");
            const QString encoder = hardware ? hardwareEncoder(videoCodec) : QString();
            if (!encoder.isEmpty()) {
                p.insert(QStringLiteral("vcodec"), encoder);
                if (encoder.endsWith(QLatin1String("_vaapi"))) {
                    // VAAPI encode depuis la carte : l'image y monte en NV12 (comme le préréglage VAAPI d'origine).
                    p.insert(QStringLiteral("vaapi_device"), QStringLiteral("/dev/dri/renderD128"));
                    p.insert(QStringLiteral("vf"), QStringLiteral("format=nv12,hwupload"));
                } else {
                    p.insert(QStringLiteral("pix_fmt"), QStringLiteral("nv12"));
                }
                p.insert(QStringLiteral("vb"), QStringLiteral("%1k").arg(bitrateKbps > 0 ? bitrateKbps : autoHardwareKbps));
                if (bitrateKbps > 0) {
                    p.insert(QStringLiteral("maxrate"), QStringLiteral("%1k").arg(bitrateKbps));
                    p.insert(QStringLiteral("bufsize"), QStringLiteral("%1k").arg(bitrateKbps * 2));
                }
                if (encoder.endsWith(QLatin1String("_videotoolbox"))) {
                    p.insert(QStringLiteral("allow_sw"), QStringLiteral("1"));
                    p.insert(QStringLiteral("realtime"), QStringLiteral("0"));
                }
            } else if (h265) {
                p.insert(QStringLiteral("vcodec"), QStringLiteral("libx265"));
                p.insert(QStringLiteral("preset"), QStringLiteral("medium"));
                p.insert(QStringLiteral("pix_fmt"), QStringLiteral("yuv420p"));
                QString x265 = QStringLiteral("crf=22");
                if (bitrateKbps > 0) {
                    x265 += QStringLiteral(":vbv-maxrate=%1:vbv-bufsize=%2").arg(bitrateKbps).arg(bitrateKbps * 2);
                }
                if (keyframes > 0) {
                    x265 += QStringLiteral(":keyint=%1").arg(keyframes);
                }
                if (!reorder) {
                    x265 += QStringLiteral(":bframes=0");
                }
                p.insert(QStringLiteral("x265-params"), x265);
            } else {
                p.insert(QStringLiteral("vcodec"), QStringLiteral("libx264"));
                p.insert(QStringLiteral("preset"), QStringLiteral("medium"));
                p.insert(QStringLiteral("pix_fmt"), QStringLiteral("yuv420p"));
                p.insert(QStringLiteral("crf"), QStringLiteral("18"));
                if (bitrateKbps > 0) {
                    p.insert(QStringLiteral("maxrate"), QStringLiteral("%1k").arg(bitrateKbps));
                    p.insert(QStringLiteral("bufsize"), QStringLiteral("%1k").arg(bitrateKbps * 2));
                }
            }
            if (h265) {
                // Lu par les lecteurs d'Apple (QuickTime, iPhone).
                p.insert(QStringLiteral("vtag"), QStringLiteral("hvc1"));
            }
            if (!profile.isEmpty() && profile != QLatin1String("auto") && !(h265 && !hardware)) {
                // « vprofile » et pas « profile » : ce dernier irait aussi à l'encodeur AAC, qui le refuse.
                p.insert(QStringLiteral("vprofile"), profile);
            }
            if (keyframes > 0 && !(h265 && !hardware)) {
                p.insert(QStringLiteral("g"), QString::number(keyframes));
            }
            if (!reorder && !(h265 && !hardware)) {
                p.insert(QStringLiteral("bf"), QStringLiteral("0"));
            }
        } else if (videoCodec == QLatin1String("prores_hq") || videoCodec == QLatin1String("prores_4444")) {
            const bool alpha = videoCodec == QLatin1String("prores_4444");
            p.insert(QStringLiteral("vcodec"), QStringLiteral("prores_ks"));
            p.insert(QStringLiteral("profile"), alpha ? QStringLiteral("4") : QStringLiteral("3"));
            p.insert(QStringLiteral("vendor"), QStringLiteral("apl0"));
            p.insert(QStringLiteral("pix_fmt"), alpha ? QStringLiteral("yuva444p10le") : QStringLiteral("yuv422p10le"));
            p.insert(QStringLiteral("qscale"), QStringLiteral("9"));
        } else if (videoCodec == QLatin1String("gif")) {
            p.insert(QStringLiteral("vcodec"), QStringLiteral("gif"));
            p.insert(QStringLiteral("g"), QStringLiteral("1"));
            p.insert(QStringLiteral("bf"), QStringLiteral("0"));
        }
    }
    const bool withAudio = audio && !audioCodec.isEmpty() && ext != QLatin1String("gif");
    if (!withAudio) {
        p.insert(QStringLiteral("an"), QStringLiteral("1"));
        p.insert(QStringLiteral("audio_off"), QStringLiteral("1"));
    } else {
        if (audioCodec == QLatin1String("pcm24")) {
            p.insert(QStringLiteral("acodec"), QStringLiteral("pcm_s24le"));
        } else if (audioCodec == QLatin1String("pcm16")) {
            p.insert(QStringLiteral("acodec"), QStringLiteral("pcm_s16le"));
        } else if (audioCodec == QLatin1String("mp3")) {
            p.insert(QStringLiteral("acodec"), QStringLiteral("libmp3lame"));
            p.insert(QStringLiteral("ab"), QStringLiteral("%1k").arg(audioKbps));
        } else {
            p.insert(QStringLiteral("acodec"), QStringLiteral("aac"));
            p.insert(QStringLiteral("ab"), QStringLiteral("%1k").arg(audioKbps));
        }
        p.insert(QStringLiteral("ar"), QString::number(sampleRate));
        p.insert(QStringLiteral("channels"), QString::number(channels));
    }
    return p;
}

QString expandName(const QString &pattern, const Settings &s, const QString &project, const QString &sequence, int version)
{
    const Model *m = model(s.model);
    QString name = pattern.isEmpty() ? QStringLiteral("{projet} - {modele}") : pattern;
    const QDateTime now = QDateTime::currentDateTime();
    QString format;
    if (s.video && s.width > 0 && s.height > 0) {
        format = QStringLiteral("%1x%2").arg(s.width).arg(s.height);
    } else if (s.video && pCore && pCore->getCurrentProfile()) {
        const QSize size = pCore->getCurrentFrameSize();
        format = QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
    } else {
        format = s.extension().toUpper();
    }
    name.replace(QStringLiteral("{projet}"), project.isEmpty() ? i18nc("@item nom de fichier d'un projet sans nom", "Untitled") : project);
    name.replace(QStringLiteral("{sequence}"), sequence);
    name.replace(QStringLiteral("{date}"), now.date().toString(QStringLiteral("yyyy-MM-dd")));
    name.replace(QStringLiteral("{heure}"), now.time().toString(QStringLiteral("HH'h'mm")));
    name.replace(QStringLiteral("{format}"), format);
    name.replace(QStringLiteral("{modele}"), m ? m->name : s.model);
    const bool hasVersion = name.contains(QLatin1String("{version}"));
    name.replace(QStringLiteral("{version}"), QStringLiteral("v%1").arg(version));
    name = sanitize(name);
    if (name.isEmpty()) {
        name = QStringLiteral("export");
    }
    if (!hasVersion && version > 1) {
        name += QStringLiteral(" v%1").arg(version);
    }
    return name;
}

QString defaultFolder()
{
    const KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    if (doc && !doc->url().isEmpty()) {
        const QFileInfo info(doc->url().toLocalFile());
        // Un projet neuf porte une URL sans fichier (CLAUDE.md, pièges) : on ne la suit que si le fichier existe.
        if (info.exists()) {
            return info.absoluteDir().absoluteFilePath(QStringLiteral("Exports"));
        }
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).absoluteFilePath(QStringLiteral("KoraCut/Exports"));
}

QString outputPath(const Settings &s, const QString &project, const QString &sequence)
{
    const QString folder = s.folder.isEmpty() ? defaultFolder() : s.folder;
    const QString ext = s.extension();
    // Jamais par-dessus un fichier existant : la version suivante (Resolve écrase, KoraCut non).
    for (int v = 1; v < 1000; ++v) {
        const QString path = QDir(folder).absoluteFilePath(expandName(s.name, s, project, sequence, v) + QLatin1Char('.') + ext);
        if (!QFileInfo::exists(path) && !KoraCutExportQueue::self().holds(path)) {
            return path;
        }
    }
    return QDir(folder).absoluteFilePath(expandName(s.name, s, project, sequence, 1) + QLatin1Char('.') + ext);
}

QString hardwareEncoder(const QString &videoCodec)
{
    if (videoCodec != QLatin1String("h264") && videoCodec != QLatin1String("h265")) {
        return {};
    }
    const QString prefix = videoCodec == QLatin1String("h265") ? QStringLiteral("hevc_") : QStringLiteral("h264_");
#ifdef Q_OS_MACOS
    const QStringList codecs = RenderPresetRepository::vcodecs();
    const QString vt = prefix + QStringLiteral("videotoolbox");
    return codecs.isEmpty() || codecs.contains(vt) ? vt : QString();
#else
    // Sous Windows et Linux, seulement un encodeur qui a réussi l'essai du premier lancement
    // (Wizard::testHwEncoders encode une image avec chacun) : qu'ffmpeg le connaisse ne suffit pas,
    // il faut la carte qui va avec. Ordre : NVIDIA, Intel, AMD, puis VAAPI (Linux).
    const QStringList working = KdenliveSettings::supportedHWCodecs();
    for (const QString &family : {QStringLiteral("nvenc"), QStringLiteral("qsv"), QStringLiteral("amf"), QStringLiteral("vaapi")}) {
        if (working.contains(prefix + family)) {
            return prefix + family;
        }
    }
    return {};
#endif
}

bool hardwareAvailable(const QString &videoCodec)
{
    return !hardwareEncoder(videoCodec).isEmpty();
}

QString modelForSize(const QSize &sequence)
{
    const QString o = orientationOf(sequence);
    if (o == QLatin1String("v")) {
        return QStringLiteral("tiktok");
    }
    if (o == QLatin1String("c")) {
        return QStringLiteral("instagram_carre");
    }
    if (o == QLatin1String("p")) {
        return QStringLiteral("instagram_feed");
    }
    if (sequence.height() >= 2000) {
        return QStringLiteral("youtube_4k");
    }
    return QStringLiteral("youtube_1080p");
}

QString modelForVideoType(const QString &typeId, const QSize &sequence)
{
    const KoraCutVideoTypes::VideoType *type = KoraCutVideoTypes::byId(typeId);
    if (!type) {
        return modelForSize(sequence);
    }
    const QString o = orientationOf(sequence);
    // Les modèles indiqués par le guide (exportModele pour le format du type, exportModeleAutre pour
    // l'autre sens) : une séquence verticale d'un projet YouTube prend Shorts, et l'inverse.
    for (const QString &id : {type->exportModel, type->exportModelOther}) {
        const Model *m = model(id);
        if (m && (m->orientation.isEmpty() || m->orientation == o || (m->width <= 0))) {
            return m->id;
        }
    }
    return modelForSize(sequence);
}

Settings documentSettings(const QString &sequenceUuid, const QSize &sequence, double fps)
{
    const KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    if (doc) {
        const QJsonObject all = QJsonDocument::fromJson(doc->getDocumentProperty(QStringLiteral("koracut:export_reglages")).toUtf8()).object();
        const QJsonObject mine = all.value(sequenceUuid).toObject();
        if (!mine.isEmpty() && model(mine.value(QStringLiteral("modele")).toString())) {
            return Settings::fromJson(mine);
        }
    }
    const QString typeId = doc ? doc->getDocumentProperty(QStringLiteral("koracut:videotype")) : QString();
    const Model *m = model(typeId.isEmpty() ? modelForSize(sequence) : modelForVideoType(typeId, sequence));
    if (!m) {
        return Settings();
    }
    Settings s = Settings::forModel(*m, sequence, fps);
    if (doc && !typeId.isEmpty()) {
        // La cible sonore du guide pour ce type (document 16) passe devant celle du modèle.
        const int typeLufs = doc->getDocumentProperty(QStringLiteral("koracut:export_lufs")).toInt();
        if (typeLufs != 0 && s.audio) {
            s.normalize = true;
            s.lufs = typeLufs;
        }
    }
    return s;
}

void saveDocumentSettings(const QString &sequenceUuid, const Settings &s)
{
    KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    if (!doc || sequenceUuid.isEmpty()) {
        return;
    }
    QJsonObject all = QJsonDocument::fromJson(doc->getDocumentProperty(QStringLiteral("koracut:export_reglages")).toUtf8()).object();
    const QJsonObject value = s.toJson();
    if (all.value(sequenceUuid).toObject() == value) {
        return;
    }
    all.insert(sequenceUuid, value);
    doc->setDocumentProperty(QStringLiteral("koracut:export_reglages"), QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
}

QString currentSequenceName()
{
    KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    if (!doc) {
        return QString();
    }
    const auto timeline = doc->getTimeline(pCore->currentTimelineId(), true);
    if (!timeline || !timeline->tractor()) {
        return QString();
    }
    return QString::fromUtf8(timeline->tractor()->get("kdenlive:clipname"));
}

QString currentProjectName()
{
    KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    if (!doc || doc->url().isEmpty()) {
        return QString();
    }
    return QFileInfo(doc->url().toLocalFile()).completeBaseName();
}

Enqueued enqueue(const Settings &s, bool held, const QString &wantedPath, int in, int out)
{
    Enqueued r;
    KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    const Model *m = model(s.model);
    if (!doc || !m) {
        r.code = QStringLiteral("non_disponible");
        r.message = doc ? QStringLiteral("Le modèle « %1 » n'existe pas.").arg(s.model) : QStringLiteral("Aucun projet ouvert.");
        return r;
    }
    const QUuid uuid = pCore->currentTimelineId();
    const auto timeline = doc->getTimeline(uuid, true);
    const int duration = pCore->projectDuration();
    if (!timeline || duration < 1) {
        r.code = QStringLiteral("entree_invalide");
        r.message = QStringLiteral("La timeline est vide : il n'y a rien à exporter.");
        return r;
    }
    // La plage : toute la timeline, ou la zone Entrée/Sortie du moniteur Programme.
    int first = 0;
    int last = duration - 1;
    if (in >= 0 || out >= 0) {
        first = std::max(0, in);
        last = out >= 0 ? std::min(out, duration - 1) : duration - 1;
    } else if (s.inOut) {
        Monitor *monitor = pCore->getMonitor(Kdenlive::ProjectMonitor);
        if (!monitor || monitor->getZoneEnd() <= monitor->getZoneStart()) {
            r.code = QStringLiteral("entree_invalide");
            r.message = QStringLiteral("Aucune zone Entrée/Sortie n'est posée sur la timeline.");
            return r;
        }
        first = monitor->getZoneStart();
        last = std::min(monitor->getZoneEnd() - 1, duration - 1);
    }
    if (last < first) {
        r.code = QStringLiteral("entree_invalide");
        r.message = QStringLiteral("La plage à exporter est vide.");
        return r;
    }
    const QString sequence = currentSequenceName();
    r.path = wantedPath.isEmpty() ? outputPath(s, currentProjectName(), sequence) : wantedPath;
    r.frames = last - first + 1;
    const QFileInfo info(r.path);
    if (!QDir().mkpath(info.absolutePath())) {
        r.code = QStringLiteral("fichier_introuvable");
        r.message = QStringLiteral("Le dossier %1 ne peut pas être créé.").arg(info.absolutePath());
        return r;
    }
    if (KoraCutExportQueue::self().holds(r.path) || info.exists()) {
        r.code = QStringLiteral("fichier_existe");
        r.message = QStringLiteral("%1 existe déjà ou est déjà dans la file.").arg(info.fileName());
        return r;
    }
    if (m->isStill() || s.videoCodec == QLatin1String("png")) {
        // Une image : écrite tout de suite, comme l'outil exporter_image.
        const QJsonObject done = KoraCutAgentTools::call(timeline, QStringLiteral("exporter_image"),
                                                         {{QStringLiteral("position"), pCore->getMonitorPosition()}, {QStringLiteral("chemin"), r.path}});
        r.ok = done.value(QStringLiteral("ok")).toBool();
        r.still = true;
        if (!r.ok) {
            const QJsonObject e = done.value(QStringLiteral("erreur")).toObject();
            r.code = e.value(QStringLiteral("code")).toString(QStringLiteral("non_disponible"));
            r.message = e.value(QStringLiteral("message")).toString();
        }
        return r;
    }
    RenderRequest request;
    request.setOutputFile(r.path);
    request.setPresetParams(s.params(pCore->getCurrentFrameSize(), pCore->getCurrentFps()));
    request.setProxyRendering(false);
    request.setDelayedRendering(false);
    request.setEmbedSubtitles(false);
    request.setBurnSubtitles(s.subtitles == QLatin1String("incruster"));
    request.setTwoPass(false);
    request.setAudioFilePerTrack(false);
    request.setLoudnessTarget(s.audio && s.normalize ? s.lufs : 0);
    request.setBounds(first, last);
    const std::vector<RenderRequest::RenderJob> jobs = request.process();
    if (!request.errorMessages().isEmpty() || jobs.empty()) {
        r.code = QStringLiteral("rendu_echoue");
        r.message = QStringLiteral("Le rendu n'a pas pu être préparé : %1").arg(request.errorMessages().join(QLatin1String(" ; ")));
        return r;
    }
    if (s.subtitles == QLatin1String("fichier") && doc->hasSubtitles()) {
        // Le texte à part, en .srt à côté de la vidéo (la piste de la timeline ne bouge pas).
        r.subtitleFile = QDir(info.absolutePath()).absoluteFilePath(info.completeBaseName() + QStringLiteral(".srt"));
        if (!QFileInfo::exists(r.subtitleFile)) {
            doc->generateRenderSubtitleFile(uuid, first, last, r.subtitleFile);
        }
    }
    for (const RenderRequest::RenderJob &job : jobs) {
        const auto added = KoraCutExportQueue::self().add(job, m->id, m->name, r.frames, s.audio && s.normalize ? s.lufs : 0, false, held, sequence);
        added->subtitleFile = r.subtitleFile;
        r.jobIds << added->id;
    }
    if (!held) {
        KoraCutExportQueue::self().startNext();
    }
    r.ok = true;
    return r;
}

int cleanTemporaryFiles()
{
    // Chaque rendu laissait sa playlist vide koracut-XXXXXX.mlt dans le dossier temporaire (122 fichiers
    // relevés le 28 septembre 2026). RenderRequest les retire maintenant ; ici, ce qui reste d'avant ou
    // d'une instance arrêtée brutalement, s'il a plus d'un jour (un rendu en cours n'est jamais touché).
    const QDir temp = QDir::temp();
    const QDateTime limit = QDateTime::currentDateTime().addDays(-1);
    int removed = 0;
    const QFileInfoList files = temp.entryInfoList({QStringLiteral("koracut-*.mlt"), QStringLiteral("koracut-*.srt"), QStringLiteral("koracut-estimation-*")}, QDir::Files);
    for (const QFileInfo &info : files) {
        if (info.lastModified() < limit && QFile::remove(info.absoluteFilePath())) {
            ++removed;
        }
    }
    return removed;
}

} // namespace KoraCutExport
