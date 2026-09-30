/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutvideotypes.h"

#include <KLocalizedString>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace KoraCutVideoTypes {

namespace {
struct Catalogue
{
    QList<Family> families;
    QList<VideoType> types;
};

const Catalogue &catalogue()
{
    static const Catalogue data = [] {
        Catalogue c;
        QFile file(QStringLiteral(":data/koracut-video-types.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return c;
        }
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const auto strings = [](const QJsonValue &v) {
            QStringList out;
            for (const QJsonValue &s : v.toArray()) {
                out << s.toString();
            }
            return out;
        };
        for (const QJsonValue &f : root.value(QStringLiteral("familles")).toArray()) {
            const QJsonObject o = f.toObject();
            c.families << Family{o.value(QStringLiteral("code")).toString(), o.value(QStringLiteral("nom")).toString(),
                                 o.value(QStringLiteral("description")).toString()};
        }
        for (const QJsonValue &t : root.value(QStringLiteral("types")).toArray()) {
            const QJsonObject o = t.toObject();
            VideoType v;
            v.id = o.value(QStringLiteral("id")).toString();
            v.family = o.value(QStringLiteral("famille")).toString();
            v.name = o.value(QStringLiteral("nom")).toString();
            v.description = o.value(QStringLiteral("description")).toString();
            v.platforms = o.value(QStringLiteral("plateformes")).toString();
            v.format = o.value(QStringLiteral("format")).toString();
            v.fps = o.value(QStringLiteral("ips")).toInt(25);
            v.duration = o.value(QStringLiteral("duree")).toString();
            v.difficulty = o.value(QStringLiteral("difficulte")).toInt(1);
            v.videoTracks = strings(o.value(QStringLiteral("pistesVideo")));
            v.audioTracks = strings(o.value(QStringLiteral("pistesAudio")));
            v.exportLufs = o.value(QStringLiteral("exportLufs")).toInt(-14);
            v.exportCodec = o.value(QStringLiteral("exportCodec")).toString();
            v.exportModel = o.value(QStringLiteral("exportModele")).toString();
            v.exportModelOther = o.value(QStringLiteral("exportModeleAutre")).toString();
            v.tips = strings(o.value(QStringLiteral("conseils")));
            c.types << v;
        }
        return c;
    }();
    return data;
}
} // namespace

const QList<Family> &families()
{
    return catalogue().families;
}

const QList<VideoType> &all()
{
    return catalogue().types;
}

const VideoType *byId(const QString &id)
{
    for (const VideoType &t : all()) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

QSize frameSize(const QString &format)
{
    if (format == QLatin1String("vertical")) {
        return {1080, 1920};
    }
    if (format == QLatin1String("carre")) {
        return {1080, 1080};
    }
    if (format == QLatin1String("portrait")) {
        return {1080, 1350};
    }
    return {1920, 1080};
}

QString formatLabel(const QString &format)
{
    if (format == QLatin1String("vertical")) {
        return i18nc("@item video format", "Vertical 9:16");
    }
    if (format == QLatin1String("carre")) {
        return i18nc("@item video format", "Square 1:1");
    }
    if (format == QLatin1String("portrait")) {
        return i18nc("@item video format", "Portrait 4:5");
    }
    return i18nc("@item video format", "Horizontal 16:9");
}

QString searchable(const QString &text)
{
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out.append(c.toLower());
        }
    }
    return out;
}

} // namespace KoraCutVideoTypes
