/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracuteffectpreviews.h"

#include "assets/model/assetparametermodel.hpp"
#include "core.h"
#include "effects/effectsrepository.hpp"

#include <mlt++/MltFilter.h>
#include <mlt++/MltFrame.h>
#include <mlt++/MltProducer.h>
#include <mlt++/MltProfile.h>

#include <QDir>
#include <QDomElement>
#include <QDomNodeList>
#include <QFile>
#include <QFutureWatcher>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace {
// Monter ce numéro quand l'image d'exemple ou le rendu change : les vignettes sur le disque sont
// alors recalculées.
constexpr int PreviewVersion = 1;
constexpr int RenderWidth = 320;
constexpr int RenderHeight = 180;

// Préparation des vignettes livrées (Sources/koracut-effect-previews/generer.sh, 27 septembre 2026) :
// KORACUT_APERCUS_SORTIE=« 288x162 » rend des vignettes plus grandes que celles de l'application
// (une grille agrandie avec + reste nette), KORACUT_APERCUS_EXEMPLE=<image 16:9> remplace l'image
// d'exemple dessinée par une photo (une par famille d'effets). Sans elles, rien ne change.
QSize outputSize()
{
    const QString asked = qEnvironmentVariable("KORACUT_APERCUS_SORTIE");
    const int w = asked.section(QLatin1Char('x'), 0, 0).toInt();
    const int h = asked.section(QLatin1Char('x'), 1, 1).toInt();
    if (w >= 32 && h >= 18) {
        return {w, h};
    }
    return {RenderWidth / 2, RenderHeight / 2};
}

QSize renderSize()
{
    const QSize out = outputSize();
    return {std::max(RenderWidth, out.width()), std::max(RenderHeight, out.height())};
}

// Quelques effets ne montrent rien avec leurs valeurs par défaut (luminosité à 100 %, saturation à
// 1…). Pour la vignette seulement, on leur donne un réglage parlant. Rien de ceci ne touche l'effet
// posé sur un clip, qui garde ses valeurs par défaut.
QHash<QString, QHash<QString, QString>> demoSettings()
{
    // Noms et unités lus dans les XML de data/effects (en unités MLT, donc après le « factor »).
    const auto s = [](const char *k, const char *v) { return std::make_pair(QString::fromLatin1(k), QString::fromLatin1(v)); };
    return {
        {QStringLiteral("brightness"), {s("level", "1.45")}},
        {QStringLiteral("gamma"), {s("gamma", "2.2")}},
        {QStringLiteral("frei0r.brightness"), {s("Brightness", "0.75")}},
        {QStringLiteral("frei0r.contrast0r"), {s("Contrast", "0.8")}},
        {QStringLiteral("frei0r.saturat0r"), {s("Saturation", "0.45")}},
        {QStringLiteral("avfilter.eq"), {s("av.saturation", "2.2"), s("av.contrast", "1.3")}},
        {QStringLiteral("avfilter.huesaturation"), {s("av.colors", "a"), s("av.sat", "0.7"), s("av.hue", "35"), s("av.strength", "1")}},
        {QStringLiteral("avfilter.colorcontrast"), {s("av.rc", "0.7"), s("av.rcw", "1"), s("av.by", "-0.6"), s("av.byw", "1")}},
        {QStringLiteral("avfilter.cas"), {s("av.strength", "1")}},
        {QStringLiteral("lift_gamma_gain"), {s("gain_r", "1.35"), s("gain_g", "1.05"), s("gain_b", "0.75"), s("lift_b", "0.08")}},
        {QStringLiteral("box_blur"), {s("hradius", "10"), s("vradius", "10")}},
        {QStringLiteral("avfilter.boxblur"), {s("hori", "6"), s("vert", "6"), s("blur", "6")}},
        {QStringLiteral("frei0r.IIRblur"), {s("Amount", "0.4")}},
        {QStringLiteral("frei0r.pixeliz0r"), {s("Block width", "0.08"), s("Block height", "0.08")}},
        {QStringLiteral("crop"), {s("top", "22"), s("bottom", "22"), s("left", "40"), s("right", "40"), s("use_profile", "0")}},
        {QStringLiteral("avfilter.drawbox"), {s("av.x", "70"), s("av.y", "40"), s("av.w", "180"), s("av.h", "90"), s("av.color", "0xFF5A1Fff"), s("av.t", "5")}},
        {QStringLiteral("avfilter.drawgrid"), {s("av.w", "40"), s("av.h", "40"), s("av.t", "2"), s("av.color", "0xFFFFFFB0")}},
        {QStringLiteral("chroma"), {s("key", "0x1F6F8Bff"), s("variance", "0.3")}},
    };
}

// Effets qu'on ne rend pas en vignette : des outils d'analyse (ils dessinent un graphique au lieu de
// transformer l'image) et ceux qui font planter MLT dans ces conditions. Trouvés en rendant tous les
// effets vidéo, le 23 septembre 2026 (test koracuteffectpreviewstest, KORACUT_APERCUS_DOSSIER).
bool skipped(const QString &effectId)
{
    static const QSet<QString> list = {
        QStringLiteral("avfilter.waveform"),
    };
    return list.contains(effectId);
}

// Vignette « audio » : une onde, dans la couleur des pistes de voix de la charte.
QPixmap drawAudio(const QSize &size)
{
    QPixmap pix(size);
    pix.fill(QColor(0x1E, 0x1E, 0x1E));
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor wave(0x14, 0xA3, 0x7F);
    p.setPen(Qt::NoPen);
    p.setBrush(wave);
    const int bars = 23;
    const qreal barWidth = size.width() / qreal(bars * 2);
    for (int i = 0; i < bars; ++i) {
        const qreal t = i / qreal(bars - 1);
        const qreal h = size.height() * (0.18 + 0.55 * std::abs(std::sin(t * 9.0)) * (0.55 + 0.45 * std::sin(t * 3.1)));
        p.drawRoundedRect(QRectF(barWidth * (2 * i + 0.5), (size.height() - h) / 2, barWidth, h), barWidth / 2, barWidth / 2);
    }
    return pix;
}
}

KoraCutEffectPreviews *KoraCutEffectPreviews::instance()
{
    static auto *self = new KoraCutEffectPreviews(qApp);
    return self;
}

KoraCutEffectPreviews::KoraCutEffectPreviews(QObject *parent)
    : QObject(parent)
{
}

KoraCutEffectPreviews::~KoraCutEffectPreviews()
{
    delete m_profile;
}

QSize KoraCutEffectPreviews::size()
{
    return {RenderWidth / 2, RenderHeight / 2};
}

QString KoraCutEffectPreviews::cacheFolder() const
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/apercus-effets/v%1").arg(PreviewVersion);
    QDir().mkpath(folder);
    return folder;
}

QImage KoraCutEffectPreviews::sampleImage()
{
    // L'image d'exemple : un paysage dessiné, pas une photo. Il a ce qu'il faut pour voir ce que
    // fait un effet : un dégradé de ciel (couleur, contraste), un soleil (hautes lumières, flou),
    // une mer et des collines (tons moyens), des bords nets (netteté, contours), et une rangée de
    // couleurs pures en bas (teinte, saturation, inversion).
    QImage img(RenderWidth, RenderHeight, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, RenderHeight * 0.62);
    sky.setColorAt(0, QColor(0x2E, 0x5C, 0xB8));
    sky.setColorAt(0.65, QColor(0xF2, 0x9E, 0x6B));
    sky.setColorAt(1, QColor(0xFF, 0xD1, 0x8C));
    p.fillRect(QRectF(0, 0, RenderWidth, RenderHeight * 0.62), sky);
    QRadialGradient sun(QPointF(RenderWidth * 0.72, RenderHeight * 0.47), RenderHeight * 0.16);
    sun.setColorAt(0, QColor(0xFF, 0xF6, 0xD8));
    sun.setColorAt(0.55, QColor(0xFF, 0xE0, 0x8A));
    sun.setColorAt(1, QColor(0xFF, 0xB0, 0x60, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(sun);
    p.drawEllipse(QPointF(RenderWidth * 0.72, RenderHeight * 0.47), RenderHeight * 0.16, RenderHeight * 0.16);
    QLinearGradient sea(0, RenderHeight * 0.62, 0, RenderHeight * 0.84);
    sea.setColorAt(0, QColor(0x1F, 0x6F, 0x8B));
    sea.setColorAt(1, QColor(0x0B, 0x3C, 0x5D));
    p.fillRect(QRectF(0, RenderHeight * 0.62, RenderWidth, RenderHeight * 0.22), sea);
    QPainterPath hills;
    hills.moveTo(0, RenderHeight * 0.66);
    hills.cubicTo(RenderWidth * 0.18, RenderHeight * 0.44, RenderWidth * 0.30, RenderHeight * 0.52, RenderWidth * 0.46, RenderHeight * 0.70);
    hills.lineTo(RenderWidth * 0.46, RenderHeight * 0.84);
    hills.lineTo(0, RenderHeight * 0.84);
    hills.closeSubpath();
    p.setBrush(QColor(0x2F, 0x6B, 0x3A));
    p.drawPath(hills);
    p.setPen(QPen(QColor(255, 255, 255, 170), 1.2));
    for (int i = 0; i < 5; ++i) {
        const qreal y = RenderHeight * (0.68 + i * 0.03);
        p.drawLine(QPointF(RenderWidth * (0.52 + i * 0.02), y), QPointF(RenderWidth * (0.66 + i * 0.03), y));
    }
    const QList<QColor> swatches = {Qt::white, QColor(0xE5, 0x3E, 0x3E), QColor(0xF2, 0xC1, 0x2E), QColor(0x3E, 0xB4, 0x5E),
                                    QColor(0x2E, 0xA8, 0xE5), QColor(0x5B, 0x4B, 0xD6), QColor(0xD6, 0x4B, 0xB4), Qt::black};
    const qreal w = RenderWidth / qreal(swatches.size());
    for (int i = 0; i < swatches.size(); ++i) {
        p.fillRect(QRectF(i * w, RenderHeight * 0.84, w + 1, RenderHeight * 0.16), swatches.at(i));
    }
    p.end();
    return img;
}

QString KoraCutEffectPreviews::samplePath()
{
    const QString photo = qEnvironmentVariable("KORACUT_APERCUS_EXEMPLE");
    if (!photo.isEmpty() && QFile::exists(photo)) {
        return photo;
    }
    const QString path = cacheFolder() + QStringLiteral("/exemple.png");
    if (!QFile::exists(path)) {
        sampleImage().save(path);
    }
    return path;
}

QPixmap KoraCutEffectPreviews::placeholder(const QString &effectId, bool audio) const
{
    if (audio) {
        return drawAudio(size());
    }
    // En attendant : l'image d'exemple, éteinte, pour que la grille ne saute pas quand les
    // vignettes arrivent.
    Q_UNUSED(effectId)
    static QPixmap waiting = [] {
        QPixmap pix = QPixmap::fromImage(sampleImage().scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        QPainter p(&pix);
        p.fillRect(pix.rect(), QColor(0x1E, 0x1E, 0x1E, 200));
        return pix;
    }();
    return waiting;
}

bool KoraCutEffectPreviews::isNeutral(const QString &effectId)
{
    static const QSet<QString> neutral = [] {
        QSet<QString> list;
        QFile file(QStringLiteral(":/effect-previews/neutres.txt"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                list.insert(line.trimmed());
            }
        }
        return list;
    }();
    return neutral.contains(effectId);
}

void KoraCutEffectPreviews::keep(const QString &effectId, const QPixmap &pix)
{
    m_ready.insert(effectId, new QPixmap(pix), std::max<qsizetype>(1, qsizetype(pix.width()) * pix.height() * 4 / 1024));
}

bool KoraCutEffectPreviews::isReady(const QString &effectId)
{
    return m_ready.contains(effectId) || QFile::exists(QStringLiteral(":/effect-previews/%1.jpg").arg(effectId));
}

QPixmap KoraCutEffectPreviews::preview(const QString &effectId, bool audio)
{
    if (audio) {
        static QPixmap audioPix = drawAudio(size());
        return audioPix;
    }
    if (const QPixmap *kept = m_ready.object(effectId)) {
        return *kept;
    }
    // Les vignettes livrées avec l'application (préparées par Sources/koracut-effect-previews).
    const QString bundled = QStringLiteral(":/effect-previews/%1.jpg").arg(effectId);
    if (QFile::exists(bundled)) {
        QPixmap pix(bundled);
        if (!pix.isNull()) {
            keep(effectId, pix);
            return pix;
        }
    }
    // Pas de rendu en direct par défaut : rendre un effet inconnu peut planter MLT, et ce serait
    // perdre le montage en cours. KORACUT_APERCUS_DIRECT=1 le permet, pour les essais.
    if (qEnvironmentVariableIsSet("KORACUT_APERCUS_DIRECT")) {
        const QString file = cacheFolder() + QLatin1Char('/') + effectId + QStringLiteral(".png");
        if (QFile::exists(file)) {
            QPixmap pix(file);
            if (!pix.isNull()) {
                keep(effectId, pix);
                return pix;
            }
        }
        if (!m_failed.contains(effectId) && !m_queue.contains(effectId)) {
            m_queue << effectId;
            if (!m_busy) {
                QTimer::singleShot(0, this, &KoraCutEffectPreviews::next);
            }
        }
    }
    return placeholder(effectId, false);
}

namespace {
struct Candidate
{
    QByteArray name;
    QByteArray value;
};

struct Job
{
    std::shared_ptr<Mlt::Producer> producer;
    std::shared_ptr<Mlt::Filter> filter;
    // Réglages à essayer si l'effet ne change rien avec ses valeurs par défaut.
    QList<Candidate> candidates;
    // L'image d'exemple à la taille de sortie, pour mesurer ce que l'effet change.
    QImage reference;
};

// Écart moyen entre deux images de même taille, de 0 (identiques) à 255.
double imageDifference(const QImage &a, const QImage &b)
{
    if (a.size() != b.size()) {
        return 0;
    }
    const QImage x = a.convertToFormat(QImage::Format_RGB32);
    const QImage y = b.convertToFormat(QImage::Format_RGB32);
    double total = 0;
    for (int j = 0; j < x.height(); ++j) {
        const auto *p = reinterpret_cast<const QRgb *>(x.constScanLine(j));
        const auto *q = reinterpret_cast<const QRgb *>(y.constScanLine(j));
        for (int i = 0; i < x.width(); ++i) {
            total += std::abs(qRed(p[i]) - qRed(q[i])) + std::abs(qGreen(p[i]) - qGreen(q[i])) + std::abs(qBlue(p[i]) - qBlue(q[i]));
        }
    }
    return total / (x.width() * x.height() * 3.);
}

// Écart type de la luminance : une image presque uniforme (tout noir, tout blanc) ne montre rien.
double contrast(const QImage &a)
{
    const QImage x = a.convertToFormat(QImage::Format_Grayscale8);
    double sum = 0;
    double sum2 = 0;
    const int n = x.width() * x.height();
    for (int j = 0; j < x.height(); ++j) {
        const uchar *p = x.constScanLine(j);
        for (int i = 0; i < x.width(); ++i) {
            sum += p[i];
            sum2 += double(p[i]) * p[i];
        }
    }
    const double mean = sum / n;
    return std::sqrt(std::max(0., sum2 / n - mean * mean));
}

// Les paramètres numériques de l'effet, chacun à 25 % et 75 % de sa plage, en unités MLT.
QList<Candidate> candidatesFor(const QString &effectId)
{
    QList<Candidate> out;
    const QDomNodeList params = EffectsRepository::get()->getXml(effectId).elementsByTagName(QStringLiteral("parameter"));
    for (int i = 0; i < params.count(); ++i) {
        const QDomElement e = params.item(i).toElement();
        static const QStringList numeric = {QStringLiteral("animated"), QStringLiteral("constant"), QStringLiteral("double"), QStringLiteral("simplekeyframe")};
        if (!numeric.contains(e.attribute(QStringLiteral("type")))) {
            continue;
        }
        bool okMin = false;
        bool okMax = false;
        const double min = e.attribute(QStringLiteral("min")).toDouble(&okMin);
        const double max = e.attribute(QStringLiteral("max")).toDouble(&okMax);
        if (!okMin || !okMax || max <= min) {
            continue;
        }
        const double factor = e.attribute(QStringLiteral("factor"), QStringLiteral("1")).toDouble();
        for (double t : {0.25, 0.75}) {
            const double display = min + (max - min) * t;
            out << Candidate{e.attribute(QStringLiteral("name")).toUtf8(), QByteArray::number(factor != 0 ? display / factor : display)};
        }
    }
    return out;
}

// Préparation (fil principal) : le producteur d'image, le filtre et ses valeurs par défaut.
bool prepare(Mlt::Profile &profile, const QString &sample, const QString &effectId, Job &job)
{
    if (skipped(effectId) || !EffectsRepository::get()->exists(effectId)) {
        return false;
    }
    job.producer = std::make_shared<Mlt::Producer>(profile, nullptr, sample.toUtf8().constData());
    if (!job.producer->is_valid()) {
        return false;
    }
    std::unique_ptr<Mlt::Filter> filter = EffectsRepository::get()->getEffect(effectId);
    if (!filter || !filter->is_valid()) {
        return false;
    }
    job.filter = std::shared_ptr<Mlt::Filter>(filter.release());
    // Les valeurs par défaut, lues dans la description XML de l'effet, exactement comme quand on
    // pose l'effet sur un clip. Le modèle partage le filtre (compteur de références de MLT).
    {
        AssetParameterModel model(std::make_unique<Mlt::Filter>(*job.filter), EffectsRepository::get()->getXml(effectId), effectId, ObjectId());
    }
    const auto demo = demoSettings().value(effectId);
    for (auto it = demo.cbegin(); it != demo.cend(); ++it) {
        job.filter->set(it.key().toUtf8().constData(), it.value().toUtf8().constData());
    }
    if (demo.isEmpty()) {
        job.candidates = candidatesFor(effectId);
    }
    job.reference = QImage(sample).convertToFormat(QImage::Format_RGB32).scaled(outputSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    job.filter->set("in", 0);
    job.filter->set("out", 100);
    job.producer->attach(*job.filter);
    return true;
}

// Rendu (fil à part) : une image, en RGBA, à la taille des vignettes.
QImage renderFrame(const Job &job)
{
    // Image 12 et non 0 : les effets qui jouent dans le temps (stroboscope, gel, danse…) n'ont
    // encore rien fait à la première image.
    job.producer->seek(12);
    std::unique_ptr<Mlt::Frame> frame(job.producer->get_frame());
    if (!frame || !frame->is_valid()) {
        return {};
    }
    frame->set("consumer.rescale", "bilinear");
    mlt_image_format format = mlt_image_rgba;
    int width = renderSize().width();
    int height = renderSize().height();
    const uchar *data = frame->get_image(format, width, height);
    if (!data || width <= 0 || height <= 0) {
        return {};
    }
    const QImage image = QImage(data, width, height, width * 4, QImage::Format_RGBA8888).copy();
    // Sur un damier, comme dans les logiciels de montage : un effet qui rend une partie de l'image
    // transparente (incrustation, masque, transparence) se voit, au lieu de disparaître dans le noir.
    QImage canvas(outputSize(), QImage::Format_RGB32);
    QPainter p(&canvas);
    const int cell = 8;
    for (int y = 0; y < canvas.height(); y += cell) {
        for (int x = 0; x < canvas.width(); x += cell) {
            p.fillRect(x, y, cell, cell, ((x / cell + y / cell) % 2) ? QColor(0x3A, 0x3A, 0x3A) : QColor(0x55, 0x55, 0x55));
        }
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(canvas.rect(), image);
    p.end();
    return canvas;
}

// Le rendu avec les valeurs par défaut ; s'il ne change presque rien, chaque réglage candidat est
// essayé et le plus visible est gardé, pourvu que l'image ne devienne pas uniforme. La référence est
// l'image d'exemple elle-même, rendue sans effet par le même chemin (damier compris).
QImage renderJob(const Job &job)
{
    QImage best = renderFrame(job);
    if (best.isNull() || job.candidates.isEmpty()) {
        return best;
    }
    const QImage &reference = job.reference;
    double bestScore = imageDifference(best, reference);
    if (bestScore >= 6) {
        return best;
    }
    for (const Candidate &c : job.candidates) {
        const QByteArray before = job.filter->get(c.name.constData());
        job.filter->set(c.name.constData(), c.value.constData());
        const QImage image = renderFrame(job);
        const double score = image.isNull() ? 0 : imageDifference(image, reference);
        if (score > bestScore && contrast(image) > 12) {
            best = image;
            bestScore = score;
        } else {
            job.filter->set(c.name.constData(), before.constData());
        }
    }
    return best;
}
}

void KoraCutEffectPreviews::ensureProfile()
{
    if (m_profile) {
        return;
    }
    m_profile = new Mlt::Profile();
    m_profile->set_width(renderSize().width());
    m_profile->set_height(renderSize().height());
    m_profile->set_frame_rate(25, 1);
    m_profile->set_sample_aspect(1, 1);
    m_profile->set_display_aspect(16, 9);
    m_profile->set_progressive(1);
    m_profile->set_explicit(1);
}

QImage KoraCutEffectPreviews::renderNow(const QString &effectId)
{
    ensureProfile();
    Job job;
    if (!prepare(*m_profile, samplePath(), effectId, job)) {
        return {};
    }
    return renderJob(job);
}

void KoraCutEffectPreviews::next()
{
    if (m_busy || m_queue.isEmpty()) {
        return;
    }
    const QString effectId = m_queue.takeFirst();
    ensureProfile();
    Job job;
    if (!prepare(*m_profile, samplePath(), effectId, job)) {
        m_failed.insert(effectId);
        QTimer::singleShot(0, this, &KoraCutEffectPreviews::next);
        return;
    }
    m_busy = true;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, effectId, job]() {
        const QImage image = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        if (image.isNull()) {
            m_failed.insert(effectId);
        } else {
            image.save(cacheFolder() + QLatin1Char('/') + effectId + QStringLiteral(".png"));
            keep(effectId, QPixmap::fromImage(image));
            Q_EMIT previewReady(effectId);
        }
        Q_UNUSED(job)
        next();
    });
    watcher->setFuture(QtConcurrent::run([job]() { return renderJob(job); }));
}
