/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutupdatecheck.h"

#include "core.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KSharedConfig>

#include <QAction>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

namespace {

KConfigGroup group()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
}

void announce(const QJsonObject &published, const QString &installed)
{
    const QString date = published.value(QStringLiteral("date")).toString();
    const QString page = published.value(QStringLiteral("page")).toString(QStringLiteral("https://koracut.org/telecharger"));
    // La phrase de la version et les dates dans la langue de l'interface (celle des traductions, pas la
    // région du système : un Mac en anglais réglé sur le Sénégal a une interface en français).
    const QStringList languages = KLocalizedString::languages();
    const bool french = !languages.isEmpty() && languages.constFirst().startsWith(QLatin1String("fr"));
    const QLocale words(french ? QLocale::French : QLocale::English);
    const QString notes = published.value(french ? QStringLiteral("notes_fr") : QStringLiteral("notes_en")).toString();
    auto *download = new QAction(i18nc("@action:button", "Download"), pCore.get());
    QObject::connect(download, &QAction::triggered, download, [page]() { QDesktopServices::openUrl(QUrl(page)); });
    const auto spoken = [words](const QString &d) -> QString {
        const QDate day = QDate::fromString(d, QStringLiteral("yyyy.MM.dd"));
        return day.isValid() ? words.toString(day, QStringLiteral("d MMMM yyyy")) : d;
    };
    QString text = i18nc("@info %1 date of the new version, %2 date of the installed one", "A new version of KoraCut is available (%1, you have %2).",
                         spoken(date), spoken(installed));
    if (!notes.isEmpty()) {
        text += QLatin1Char(' ') + notes;
    }
    pCore->displayBinMessage(text, KMessageWidget::Information, {download}, true);
    group().writeEntry("majVersionAnnoncee", date);
    group().sync();
}

void check(QObject *parent)
{
    const QJsonObject local = KoraCutUpdateCheck::packageInfo();
    const QString installed = local.value(QStringLiteral("date")).toString();
    auto *network = new QNetworkAccessManager(parent);
    QNetworkRequest request{QUrl(KoraCutUpdateCheck::feedUrl())};
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("KoraCut/%1 (%2)").arg(local.value(QStringLiteral("version")).toString(), QSysInfo::productType()));
    QNetworkReply *reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, network, [reply, network, installed]() {
        reply->deleteLater();
        network->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return; // hors ligne : on réessaiera au prochain lancement
        }
        group().writeEntry("majDerniereVerification", QDateTime::currentDateTime().toString(Qt::ISODate));
        group().sync();
        const QJsonObject published = QJsonDocument::fromJson(reply->readAll()).object();
        if (KoraCutUpdateCheck::isNewer(published.value(QStringLiteral("date")).toString(), installed)) {
            announce(published, installed);
        }
    });
}

} // namespace

QJsonObject KoraCutUpdateCheck::packageInfo()
{
    QJsonObject info;
    const QString path = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("koracut-paquet.json"));
    if (!path.isEmpty()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            info = QJsonDocument::fromJson(f.readAll()).object();
        }
    }
    const QString forced = qEnvironmentVariable("KORACUT_PAQUET_DATE");
    if (!forced.isEmpty()) {
        info.insert(QStringLiteral("date"), forced);
    }
    return info;
}

bool KoraCutUpdateCheck::isNewer(const QString &published, const QString &installed)
{
    // « 2026.10.21 » : l'ordre des chaînes est celui des dates, à forme égale.
    static const QRegularExpression form(QStringLiteral("^\\d{4}\\.\\d{2}\\.\\d{2}$"));
    if (!form.match(published).hasMatch() || !form.match(installed).hasMatch()) {
        return false;
    }
    return published > installed;
}

QString KoraCutUpdateCheck::feedUrl()
{
    const QString forced = qEnvironmentVariable("KORACUT_MAJ_URL");
    return forced.isEmpty() ? QStringLiteral("https://koracut.org/version.json") : forced;
}

void KoraCutUpdateCheck::schedule(QObject *parent, bool force)
{
    if (packageInfo().value(QStringLiteral("date")).toString().isEmpty() || !group().readEntry("verifierMisesAJour", true)) {
        return;
    }
    const QDateTime last = QDateTime::fromString(group().readEntry("majDerniereVerification", QString()), Qt::ISODate);
    if (!force && last.isValid() && last.secsTo(QDateTime::currentDateTime()) < 24 * 3600) {
        return;
    }
    // Vingt secondes après le lancement : le projet s'ouvre d'abord, rien ne passe avant lui.
    QTimer::singleShot(force ? 2000 : 20000, parent, [parent]() { check(parent); });
}
