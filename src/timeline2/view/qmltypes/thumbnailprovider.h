/*
    SPDX-FileCopyrightText: 2013-2016 Meltytech LLC
    SPDX-FileCopyrightText: Dan Dennedy <dan@dennedy.org>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <KImageCache>
#include <QCache>
#include <QQuickImageProvider>
#include <memory>
#include <mlt++/MltProducer.h>
#include <mlt++/MltProfile.h>

class ThumbnailProvider : public QQuickImageProvider
{
public:
    /** @brief KoraCut, mesure de la fluidité (KC-011) : vignettes demandées et vignettes décodées (pas en cache), depuis le lancement. */
    static int koraCutRequests();
    static int koraCutDecodes();
    explicit ThumbnailProvider();
    ~ThumbnailProvider() override;
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    Mlt::Profile m_profile;
    QImage makeThumbnail(std::unique_ptr<Mlt::Producer> producer, int frameNumber, const QSize &requestedSize);
};
