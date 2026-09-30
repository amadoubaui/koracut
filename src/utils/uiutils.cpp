/*
    SPDX-FileCopyrightText: 2025 Julius Künzel <julius.kuenzel@kde.org>
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "uiutils.h"
#include "koracut/koracutfonts.h"
#include "kdenlivesettings.h"
#include "koracut/koracutmetrics.h"
#include "koracut/koracutthemes.h"

#include <KColorScheme>
#include <KColorSchemeManager>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QHash>
#include <QDir>
#include <KSharedConfig>
#include <KLocalizedString>
#include <KConfigGroup>
#include <KColorSchemeModel>
#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QPixmap>

const QStringList UiUtils::getProxyForbiddenParams()
{
    static QStringList forbiddenParams = {QStringLiteral("attach"), QStringLiteral("metadata"), QStringLiteral("null"),   QStringLiteral("dump"),
                                          QStringLiteral("concat"), QStringLiteral("safe"),     QStringLiteral("ladspa"), QStringLiteral("protocol_whitelist"),
                                          QStringLiteral("-ante "), QStringLiteral("-post ")};
    return forbiddenParams;
}

const QStringList UiUtils::getRenderForbiddenParams()
{
    static QStringList forbiddenParams = {QStringLiteral(" ante="), QStringLiteral(" post=")};
    return forbiddenParams;
}

UiUtils *UiUtils::instance()
{
    static UiUtils *instance;
    if (!instance) instance = new UiUtils;

    return instance;
}

UiUtils *UiUtils::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QFont UiUtils::getFixedFont()
{
    // KoraCut : DM Mono embarquée, 10 px sur les trois systèmes (koracutfonts.h). La police fixe du
    // système mesurait 10 px sur Mac, 12 sous Windows, 13 sous Linux, et la timeline en tire ses hauteurs.
    return KoraCutFonts::numbers();
}

QFont UiUtils::getSmallestReadableFont()
{
    // KoraCut : le cran « dense » de la charte, 11 px, et non la police que le système juge la
    // plus petite lisible (elle varie d'un système à l'autre et d'un écran à l'autre, ce qui
    // donnait des tailles différentes d'un panneau QML à l'autre). Voir « 11 - Refonte UI-UX », 3.2.
    QFont dense = QApplication::font();
    dense.setPixelSize(KoraCutMetrics::FontDense);
    return dense;
}

QFont UiUtils::getLabelFont()
{
    // KoraCut : le cran « étiquette » de la charte, 10 px en 600. Le nom d'un clip s'écrivait
    // au cran dense, en travers du haut du clip et sans limite de largeur : il occupait presque
    // un tiers de la hauteur et débordait. Il tient maintenant dans un bandeau de 16 px.
    QFont etiquette = QApplication::font();
    etiquette.setPixelSize(KoraCutMetrics::FontLabel);
    etiquette.setWeight(QFont::DemiBold);
    return etiquette;
}

qreal UiUtils::getBaseSizeMedium()
{
    QFontInfo fi(getFixedFont());
    return fi.pixelSize();
}

QStringList UiUtils::checkUnknownProxyParams(QString proxyData)
{
    // Next check if all our params are in the safe list
    if (!proxyData.startsWith(QLatin1Char(' '))) {
        proxyData.prepend(QLatin1Char(' '));
    }
    const QStringList params = proxyData.split(QLatin1String(" -"), Qt::SkipEmptyParts);
    QStringList detectedParams;
    for (auto &p : params) {
        QString paramName = p.section(QLatin1Char(' '), 0, 0);
        if (paramName.contains(QLatin1Char('='))) {
            paramName = p.section(QLatin1Char('='), 0, 0);
        }
        detectedParams << paramName;
    }
    detectedParams.removeDuplicates();
    QStringList unknownParams;
    for (auto &d : detectedParams) {
        if (!KdenliveSettings::safeFFmpegParams().contains(d)) {
            unknownParams << d;
        }
    }
    return unknownParams;
}

void UiUtils::addSafeParameters(QStringList unknownParams)
{
    unknownParams << KdenliveSettings::safeFFmpegParams();
    unknownParams.removeDuplicates();
    KdenliveSettings::setSafeFFmpegParams(unknownParams);
}

QIcon UiUtils::rotatedIcon(const QString &iconName, const QSize iconSize, qreal rotation)
{
    QIcon icon = QIcon::fromTheme(iconName);
    QPixmap pix = icon.pixmap(iconSize);
    QTransform trans;
    trans.rotate(rotation);
    pix = pix.transformed(trans);
    return QIcon(pix);
}

QString UiUtils::getSaveFileName(QWidget *parent, const QString &caption, const QString &dir, const QString &filter, const QString &extension)
{
    QString selectedFile = QFileDialog::getSaveFileName(parent, caption, dir, filter);
    if (!selectedFile.isEmpty() && QFileInfo(selectedFile).suffix().isEmpty()) {
        selectedFile.append(extension);
    }
    return selectedFile;
}

// Le schéma dont la feuille de style est en place (« - » : aucune encore).
static QString s_currentScheme = QStringLiteral("-");
// Vrai quand le thème à l'écran a été posé depuis son fichier, hors du gestionnaire de schémas.
static bool s_themeFromFile = false;

void UiUtils::applyKoraCutStyleSheet()
{
    // KoraCut : une seule fonction pour le QSS, sinon le thème choisi et la feuille de style se désaccordent
    // (constaté à la première compilation, le 20/09/2026 : l'écran de démarrage activait BreezeDark après le chargement du QSS).
    if (qEnvironmentVariableIsSet("KORACUT_NO_QSS")) {
        return;
    }
    const QString activeScheme = KColorSchemeManager::instance()->activeSchemeId();
    // On ne repose pas une feuille déjà en place : setStyleSheet relance un changement de palette, donc cette fonction.
    if (activeScheme == s_currentScheme) {
        return;
    }
    s_currentScheme = activeScheme;
    // KoraCut, 25 septembre 2026 : la feuille est rendue depuis le gabarit et les jetons du thème
    // (src/koracut/koracutthemes.cpp). Un schéma KDE ordinaire, sans section [KoraCut], n'a pas de
    // feuille de style, sinon nos gris resteraient plaqués sur ses couleurs.
    const QString path = activeScheme.isEmpty() ? QString() : themeFile(activeScheme);
    KoraCutThemes::self()->applyScheme(path, activeScheme.isEmpty() ? QStringLiteral("KoraCutDark") : activeScheme);
}

void UiUtils::setKoraCutStyleSheet(const QString &base, const QString &remplacements)
{
    // Gardée pour les anciens appels : la feuille de Sombre ou de Clair, avec des remplacements de couleurs.
    const QString id = base == QLatin1String("light") ? QStringLiteral("KoraCutLight") : QStringLiteral("KoraCutDark");
    if (base.isEmpty()) {
        qApp->setStyleSheet(QString());
        return;
    }
    KoraCutThemes::self()->applyScheme(themeFile(id), id);
    if (!remplacements.isEmpty()) {
        QString sheet = qApp->styleSheet();
        for (const QString &paire : remplacements.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QStringList deux = paire.trimmed().split(QLatin1Char(':'));
            if (deux.size() == 2) {
                sheet.replace(deux.at(0).trimmed(), deux.at(1).trimmed(), Qt::CaseInsensitive);
            }
        }
        qApp->setStyleSheet(sheet);
    }
}

void UiUtils::applyThemeMode()
{
    if (qEnvironmentVariableIsSet("KORACUT_NO_THEME")) {
        return;
    }
    const QString mode = KdenliveSettings::koracut_theme_mode();
    bool dark = true;
    if (mode == QLatin1String("light")) {
        dark = false;
    } else if (mode == QLatin1String("dark")) {
        dark = true;
    } else {
        // « system » : on demande au système. Qt renvoie Unknown quand il ne sait pas
        // (certains environnements de bureau Linux) : dans ce cas on garde le sombre,
        // qui est le thème de travail de KoraCut.
        dark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    }
    auto manager = KColorSchemeManager::instance();
    QString id = dark ? QStringLiteral("KoraCutDark") : QStringLiteral("KoraCutLight");
    // KoraCut, 23 septembre 2026 : en suivant le système, on prend le thème choisi pour chaque cas
    // (Affichage > Thème > « Quand le système est sombre / clair »), Sombre et Clair par défaut.
    if (mode == QLatin1String("system")) {
        const QString choisi = dark ? KdenliveSettings::koracut_theme_system_dark() : KdenliveSettings::koracut_theme_system_light();
        if (!choisi.isEmpty() && (manager->indexForSchemeId(choisi).isValid() || !themeFile(choisi).isEmpty())) {
            id = choisi;
        }
    } else if (mode != QLatin1String("light") && mode != QLatin1String("dark")) {
        // Un thème choisi par son identifiant (Très sombre, Gris, un thème installé…) : on le prend tel quel.
        id = mode;
    }
    if (!manager->indexForSchemeId(id).isValid()) {
        // Un thème installé pendant la séance : le gestionnaire de KF6 ne relit pas le dossier des
        // schémas et ne le connaît donc pas avant le prochain lancement. On l'applique depuis son fichier.
        const QString path = themeFile(id);
        if (!path.isEmpty()) {
            applyThemeFile(path);
            return;
        }
        qWarning() << "KoraCut: color scheme" << id << "not found in share/color-schemes";
        id = dark ? QStringLiteral("KoraCutDark") : QStringLiteral("KoraCutLight");
        if (!manager->indexForSchemeId(id).isValid()) {
            applyKoraCutStyleSheet();
            return;
        }
    }
    // KColorScheme sans configuration (timeline, accent du style) lit ce fichier : un thème posé
    // depuis son fichier l'avait détourné (25 septembre 2026).
    qApp->setProperty("KDE_COLOR_SCHEME_PATH", themeFile(id));
    if (manager->activeSchemeId() != id) {
        manager->activateSchemeId(id);
    } else if (s_themeFromFile) {
        // Retour au schéma que le gestionnaire croit encore actif : il ne ferait rien, alors que
        // l'écran montre le thème posé depuis son fichier. On repose sa palette nous-mêmes.
        qApp->setPalette(KColorScheme::createApplicationPalette(KSharedConfig::openConfig(themeFile(id), KConfig::SimpleConfig)));
    }
    if (s_themeFromFile) {
        s_themeFromFile = false;
        s_currentScheme = QStringLiteral("-");
    }
    applyKoraCutStyleSheet();
}

QString UiUtils::themeName(const QString &path, const QString &fallback)
{
    return KConfigGroup(KSharedConfig::openConfig(path, KConfig::SimpleConfig), QStringLiteral("General")).readEntry("Name", fallback);
}

QString UiUtils::themeBase(const QString &path)
{
    return KConfigGroup(KSharedConfig::openConfig(path, KConfig::SimpleConfig), QStringLiteral("KoraCut")).readEntry("Base", QString());
}

QString UiUtils::themeFile(const QString &id)
{
    if (id.isEmpty()) {
        return {};
    }
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("color-schemes/%1.colors").arg(id));
}

void UiUtils::applyThemeFile(const QString &path)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
    // Le fichier a pu être réécrit pendant la séance (thème modifié par un agent).
    config->reparseConfiguration();
    // La feuille d'abord, et marquée comme posée pour ce fichier : le changement de palette qui suit
    // rappelle applyKoraCutStyleSheet(), qui ne doit pas la remplacer par celle de l'ancien schéma.
    s_currentScheme = KColorSchemeManager::instance()->activeSchemeId();
    s_themeFromFile = true;
    // KColorScheme sans configuration (couleurs de la timeline, accent du style) lit ce fichier-là.
    qApp->setProperty("KDE_COLOR_SCHEME_PATH", path);
    KoraCutThemes::self()->applyScheme(path, QFileInfo(path).completeBaseName());
    qApp->setPalette(KColorScheme::createApplicationPalette(config));
}

QList<QPair<QString, QString>> UiUtils::koraCutThemesFor(const QString &base)
{
    QList<QPair<QString, QString>> themes;
    QAbstractItemModel *model = KColorSchemeManager::instance()->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        const QString path = index.data(KColorSchemeModel::PathRole).toString();
        if (path.isEmpty()) {
            continue;
        }
        const KConfigGroup group(KSharedConfig::openConfig(path, KConfig::SimpleConfig), QStringLiteral("KoraCut"));
        if (group.readEntry("Base", QString()) == base) {
            themes << qMakePair(index.data(KColorSchemeModel::IdRole).toString(), index.data(KColorSchemeModel::NameRole).toString());
        }
    }
    return themes;
}

QList<QPair<QString, QString>> UiUtils::koraCutThemes()
{
    QList<QPair<QString, QString>> themes;
    QAbstractItemModel *model = KColorSchemeManager::instance()->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        const QString path = index.data(KColorSchemeModel::PathRole).toString();
        const QString id = index.data(KColorSchemeModel::IdRole).toString();
        if (path.isEmpty() || id == QLatin1String("KoraCutDark") || id == QLatin1String("KoraCutLight")) {
            continue;
        }
        const KConfigGroup group(KSharedConfig::openConfig(path, KConfig::SimpleConfig), QStringLiteral("KoraCut"));
        if (group.hasKey("Base")) {
            themes << qMakePair(id, index.data(KColorSchemeModel::NameRole).toString());
        }
    }
    return themes;
}

QString UiUtils::userThemesFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/color-schemes");
}

QString UiUtils::installTheme(const QString &path, QString &error)
{
    const KConfigGroup general(KSharedConfig::openConfig(path, KConfig::SimpleConfig), QStringLiteral("General"));
    if (!general.hasKey("Name")) {
        error = i18nc("@info", "This file is not a theme: it lacks the [General] section and its name.");
        return {};
    }
    QDir().mkpath(userThemesFolder());
    const QString target = userThemesFolder() + QLatin1Char('/') + QFileInfo(path).fileName();
    QFile::remove(target);
    if (!QFile::copy(path, target)) {
        error = i18nc("@info", "Could not copy the theme to %1.", userThemesFolder());
        return {};
    }
    return QFileInfo(target).completeBaseName();
}

void UiUtils::followSystemColorScheme()
{
    static bool branche = false;
    if (branche || qEnvironmentVariableIsSet("KORACUT_NO_THEME")) {
        return;
    }
    branche = true;
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [](Qt::ColorScheme) {
        // On ne suit le système que si l'utilisateur ne s'est pas prononcé.
        if (KdenliveSettings::koracut_theme_mode() == QLatin1String("system")) {
            applyThemeMode();
        }
    });
}
