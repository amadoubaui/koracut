/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutaicatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSvgRenderer>

namespace {

QString resourceFile()
{
    return QStringLiteral(":/koracut/fournisseurs/koracut-fournisseurs-ia.json");
}

QStringList toStrings(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &v : value.toArray()) {
        if (!v.toString().isEmpty()) {
            list << v.toString();
        }
    }
    return list;
}

} // namespace

QList<KoraCutAiProvider> KoraCutAiCatalog::parse(const QByteArray &json, QString *error)
{
    QList<KoraCutAiProvider> list;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        if (error) {
            *error = parseError.errorString();
        }
        return list;
    }
    for (const QJsonValue &v : doc.object().value(QStringLiteral("fournisseurs")).toArray()) {
        const QJsonObject o = v.toObject();
        KoraCutAiProvider p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.name = o.value(QStringLiteral("nom")).toString(p.id);
        p.logo = o.value(QStringLiteral("logo")).toString();
        p.description = o.value(QStringLiteral("description")).toString();
        p.docUrl = o.value(QStringLiteral("url_doc")).toString();
        p.signupUrl = o.value(QStringLiteral("url_inscription")).toString();
        p.keyUrl = o.value(QStringLiteral("url_cle")).toString();
        p.baseUrl = o.value(QStringLiteral("url_base")).toString();
        p.baseUrlEditable = o.value(QStringLiteral("url_base_modifiable")).toBool(false);
        p.baseUrlHint = o.value(QStringLiteral("url_base_aide")).toString();
        p.format = o.value(QStringLiteral("format")).toString(QStringLiteral("openai"));
        p.auth = o.value(QStringLiteral("auth")).toString(QStringLiteral("bearer"));
        p.modelsPath = o.value(QStringLiteral("chemin_modeles")).toString(QStringLiteral("/models"));
        p.verifyPath = o.value(QStringLiteral("chemin_verification")).toString();
        p.keyRequired = o.value(QStringLiteral("cle_requise")).toBool(true);
        p.local = o.value(QStringLiteral("local")).toBool(false);
        p.free = o.value(QStringLiteral("gratuit")).toBool(false);
        p.freeNote = o.value(QStringLiteral("gratuit_note")).toString();
        p.recommendedModels = toStrings(o.value(QStringLiteral("modeles_conseilles")));
        p.note = o.value(QStringLiteral("note")).toString();
        p.extraHeaders = o.value(QStringLiteral("entetes")).toObject();
        if (p.isValid()) {
            list << p;
        }
    }
    if (list.isEmpty() && error) {
        *error = QStringLiteral("aucun fournisseur");
    }
    return list;
}

QList<KoraCutAiProvider> KoraCutAiCatalog::providers()
{
    static QList<KoraCutAiProvider> cache;
    static bool loaded = false;
    if (loaded) {
        return cache;
    }
    loaded = true;
    // La copie de l'utilisateur passe devant celle de l'application, si elle se lit.
    const QString user = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("koracut-fournisseurs-ia.json"));
    for (const QString &path : {user, resourceFile()}) {
        if (path.isEmpty()) {
            continue;
        }
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            cache = parse(file.readAll());
            if (!cache.isEmpty()) {
                break;
            }
        }
    }
    return cache;
}

KoraCutAiProvider KoraCutAiCatalog::provider(const QString &id)
{
    const QList<KoraCutAiProvider> list = providers();
    for (const KoraCutAiProvider &p : list) {
        if (p.id == id) {
            return p;
        }
    }
    return {};
}

bool KoraCutAiCatalog::hasPlaceholder(const QString &url)
{
    static const QRegularExpression re(QStringLiteral("\\{[^}]+\\}"));
    return re.match(url).hasMatch();
}

QIcon KoraCutAiCatalog::icon(const KoraCutAiProvider &provider, const QColor &ink, int size)
{
    QPixmap pixmap(size * 2, size * 2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    bool drawn = false;
    if (!provider.logo.isEmpty()) {
        QFile file(QStringLiteral(":/koracut/fournisseurs/") + provider.logo);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray svg = file.readAll();
            // Les logos d'une seule couleur suivent le thème : currentColor devient la couleur du texte.
            svg.replace("currentColor", ink.name().toLatin1());
            QSvgRenderer renderer(svg);
            if (renderer.isValid()) {
                renderer.render(&painter, QRectF(0, 0, size * 2, size * 2));
                drawn = true;
            }
        }
    }
    if (!drawn) {
        // Pas de logo sûr : une pastille avec l'initiale, jamais un logo inventé.
        painter.setPen(Qt::NoPen);
        QColor disc = ink;
        disc.setAlphaF(0.18);
        painter.setBrush(disc);
        painter.drawEllipse(QRectF(1, 1, size * 2 - 2, size * 2 - 2));
        QFont font = painter.font();
        font.setPixelSize(int(size * 1.1));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(ink);
        painter.drawText(QRectF(0, 0, size * 2, size * 2), Qt::AlignCenter, provider.name.left(1).toUpper());
    }
    painter.end();
    pixmap.setDevicePixelRatio(2.0);
    return QIcon(pixmap);
}
