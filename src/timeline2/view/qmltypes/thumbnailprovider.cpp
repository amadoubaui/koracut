/*
    SPDX-FileCopyrightText: 2013-2016 Meltytech LLC
    SPDX-FileCopyrightText: 2013-2016 Dan Dennedy <dan@dennedy.org>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "thumbnailprovider.h"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kthumb.h"
#include "utils/thumbnailcache.hpp"

#include <QCryptographicHash>
#include <QDebug>
#include <mlt++/MltFilter.h>

namespace {
/* KoraCut, 25 septembre 2026 (SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK,
   GPL-3.0-or-later) : une vignette de timeline est rendue à la forme du projet, avec des bandes
   quand le média n'a pas cette forme (une vidéo 16:9 dans un projet 9:16 n'occupait qu'une
   tranche au milieu). On rend ici l'image seule, sans ses bandes : la timeline la recadre
   ensuite pour remplir sa case. Le cache garde l'image entière, le recadrage ne coûte qu'une
   copie d'une petite image. */
QImage koraCutWithoutBars(const QImage &image, const std::shared_ptr<ProjectClip> &clip)
{
    if (image.isNull()) {
        return image;
    }
    switch (clip->clipType()) {
    case ClipType::Video:
    case ClipType::AV:
    case ClipType::Image:
        break;
    default:
        return image;
    }
    if (clip->getProducerIntProperty(QStringLiteral("rotate")) != 0) {
        return image;
    }
    const QSize source = clip->getFrameSize();
    if (source.width() <= 0 || source.height() <= 0) {
        return image;
    }
    const double sourceRatio = double(source.width()) / source.height();
    const double imageRatio = double(image.width()) / image.height();
    if (qAbs(sourceRatio - imageRatio) < 0.02) {
        return image;
    }
    QRect content;
    if (sourceRatio > imageRatio) {
        // Plus large que le projet : bandes en haut et en bas.
        const int h = qMax(1, qRound(image.width() / sourceRatio));
        content = QRect(0, (image.height() - h) / 2, image.width(), h);
    } else {
        // Plus haut que le projet : bandes à gauche et à droite.
        const int w = qMax(1, qRound(image.height() * sourceRatio));
        content = QRect((image.width() - w) / 2, 0, w, image.height());
    }
    return image.copy(content);
}
} // namespace

ThumbnailProvider::ThumbnailProvider()
    : QQuickImageProvider(QQmlImageProviderBase::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
{
}

ThumbnailProvider::~ThumbnailProvider() = default;

QImage ThumbnailProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage result;
    // id is binID/#frameNumber
    QString binId = id.section('/', 0, 0);
    bool ok;
    int frameNumber = id.section('#', -1).toInt(&ok);
    if (ok) {
        std::shared_ptr<ProjectClip> binClip = pCore->projectItemModel()->getClipByBinID(binId);
        if (binClip) {
            int duration = binClip->frameDuration();
            if (frameNumber > duration && duration > 0) {
                // for endless loopable clips, we rewrite the position
                frameNumber = frameNumber - ((frameNumber / duration) * duration);
            }
            result = ThumbnailCache::get()->getThumbnail(binClip->hashForThumbs(), binId, frameNumber);
            if (!result.isNull()) {
                result = koraCutWithoutBars(result, binClip);
                *size = result.size();
                return result;
            }
            std::unique_ptr<Mlt::Producer> prod = binClip->getThumbProducer();
            if (prod && prod->is_valid()) {
                if (binClip->clipType() != ClipType::Timeline && binClip->clipType() != ClipType::Playlist) {
                    Mlt::Profile *prodProfile = &pCore->thumbProfile();
                    Mlt::Filter scaler(*prodProfile, "swscale");
                    Mlt::Filter padder(*prodProfile, "resize");
                    Mlt::Filter converter(*prodProfile, "avcolor_space");
                    prod->attach(scaler);
                    prod->attach(padder);
                    prod->attach(converter);
                }
                result = makeThumbnail(std::move(prod), frameNumber, requestedSize);
                ThumbnailCache::get()->storeThumbnail(binId, frameNumber, result, false);
                result = koraCutWithoutBars(result, binClip);
            }
        }
    }
    if (size) *size = result.size();
    return result;
}

QImage ThumbnailProvider::makeThumbnail(std::unique_ptr<Mlt::Producer> producer, int frameNumber, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize)
    producer->seek(frameNumber);
    std::unique_ptr<Mlt::Frame> frame(producer->get_frame());
    if (frame == nullptr || !frame->is_valid()) {
        return QImage();
    }
    // TODO: cache these values ?
    frame->set("consumer.deinterlacer", "onefield");
    frame->set("consumer.top_field_first", -1);
    frame->set("consumer.rescale", "nearest");
    int imageHeight = pCore->thumbProfile().height();
    int imageWidth = pCore->thumbProfile().width();
    int fullWidth = qRound(imageHeight * pCore->getCurrentDar());
    return KThumb::getFrame(frame.get(), imageWidth, imageHeight, fullWidth);
}
