 /*
    SPDX-FileCopyrightText: 2007 Marco Gittler <g.marco@freenet.de>
    SPDX-FileCopyrightText: 2008 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "core.h"
#include "mainwindow.h"
#ifdef CRASH_AUTO_TEST
#include "logger.hpp"
#endif
#include "definitions.h"
#include "dialogs/wizard.h"
#include "kdenlive_debug.h"
#include "kdenlivesettings.h"
#include "koracut/koracutmetrics.h"
#include "koracut/koracutfonts.h"
#include "koracut/koracutstyle.h"
// Required for MacOS definition of MLT_LC_NAME
#include "lib/localeHandling.h"
#include "render/renderrequest.h"
#include "utils/uiutils.h"
#include "koracut/koracutthemes.h"
#include <config-kdenlive.h>
#include <project/projectmanager.h>

#include <mlt++/Mlt.h>

#include <KAboutData>
#include <KColorSchemeManager>
#include <KConfigGroup>
#ifdef USE_DRMINGW
#include <exchndl.h>
#elif defined(KF5_USE_CRASH)
#include <KCrash>
#endif

#include <KIconLoader>
#include <KIconTheme>
#include <KNotification>
#include <KSandbox>
#include <KSharedConfig>

#ifndef NODBUS
#include <KDBusService>
#endif

#include <KStyleManager>
#include <kddockwidgets/DockWidget.h>

#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <cstdlib>
#include <QFile>
#include <QTimer>
#include <QFont>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQuickStyle>
#include <QStyleFactory>
#include <QQuickWindow>
#include <QResource>
#include <QStandardPaths>

#include <QUndoGroup>
#include <QUrl> //new
#include <QSocketNotifier>
#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
extern "C" {
// Inform the driver we could make use of the discrete gpu
// __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
// __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
static QString getShortPath(const QString &longPath)
{
    std::wstring longPathStd = QDir::toNativeSeparators(longPath).toStdWString();
    DWORD size = GetShortPathNameW(longPathStd.c_str(), nullptr, 0);
    if (size > 0) {
        std::vector<wchar_t> buffer(size);
        if (GetShortPathNameW(longPathStd.c_str(), buffer.data(), size) > 0) {
            return QString::fromWCharArray(buffer.data());
        }
    }
    return longPath;
}
#endif

static LinuxPackageType getPackageType()
{
    QString packageType;
    if (qEnvironmentVariableIsSet("PACKAGE_TYPE")) {
        packageType = qgetenv("PACKAGE_TYPE").toLower();
    }

    if (packageType == QStringLiteral("appimage")) {
        return LinuxPackageType::AppImage;
    }

    if (packageType == QStringLiteral("flatpak")) {
        return LinuxPackageType::Flatpak;
    }

    if (packageType == QStringLiteral("snap")) {
        return LinuxPackageType::Snap;
    }

    if (KSandbox::isFlatpak()) {
        return LinuxPackageType::Flatpak;
    }

    if (KSandbox::isSnap()) {
        return LinuxPackageType::Snap;
    }

    QString appPath = qApp->applicationDirPath();
    if (appPath.contains(QStringLiteral("/tmp/.mount_"))) {
        return LinuxPackageType::AppImage;
    }

    return LinuxPackageType::Unknown;
}

static void resetConfig()
{
    // Delete config file
    KSharedConfigPtr config = KSharedConfig::openConfig();
    if (config->name().contains(QLatin1String("koracut"))) {
        // Make sure we delete our config file
        QFile f(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, config->name(), QStandardPaths::LocateFile));
        if (f.exists()) {
            qDebug() << " = = = =\nGOT Deleted file: " << f.fileName();
            f.remove();
        }
    }

    // Delete xml ui rc file
    const QString configFile = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kxmlgui5/koracut/koracutui.rc"));

    if (configFile.isEmpty()) {
        return;
    }
    QFile f(configFile);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }

    bool shortcutFound = false;
    QDomDocument doc;
    doc.setContent(&f);
    f.close();
    if (!doc.documentElement().isNull()) {
        QDomElement shortcuts = doc.documentElement().firstChildElement(QStringLiteral("ActionProperties"));
        if (!shortcuts.isNull()) {
            qDebug() << "==== FOUND CUSTOM SHORTCUTS!!!";
            // Copy the original settings and append custom shortcuts
            QFile f2(QStringLiteral(":/kxmlgui5/koracut/koracutui.rc"));
            if (f2.exists() && f2.open(QIODevice::ReadOnly)) {
                QDomDocument doc2;
                doc2.setContent(&f2);
                f2.close();
                if (!doc2.documentElement().isNull()) {
                    doc2.documentElement().appendChild(doc2.importNode(shortcuts, true));
                    shortcutFound = true;
                    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        // overwrite local xml config
                        QTextStream out(&f);
                        out << doc2.toString();
                        f.close();
                    }
                }
            }
        }
    }
    if (!shortcutFound) {
        // No custom shortcuts found, simply delete the xmlui file
        f.remove();
    }
}

class Application : public QApplication
{
public:
    QUrl url;
    Application(int &argc, char **argv)
        : QApplication(argc, argv)
    {
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(event);
            url = QUrl::fromLocalFile(openEvent->file());
            return true;
        } else
            return QApplication::event(event);
    }
};

int main(int argc, char *argv[])
{
    int result = EXIT_SUCCESS;
#ifdef USE_DRMINGW
    ExcHndlInit();
#endif

#ifdef Q_OS_MAC
    // Launcher and Spotlight on macOS are not setting this environment
    // variable needed by setlocale() as used by MLT.
    if (QProcessEnvironment::systemEnvironment().value(MLT_LC_NAME).isEmpty()) {
        qputenv(MLT_LC_NAME, QLocale().name().toUtf8());

        QLocale localeByName(QLocale(QLocale().language(), QLocale().script(), QLocale().territory()));
        if (QLocale().decimalPoint() != localeByName.decimalPoint()) {
            // If region's numeric format does not match the language's, then we run
            // into problems because we told MLT and libc to use a different numeric
            // locale than actually in use by Qt because it is unable to give numeric
            // locale as a set of ISO-639 codes.
            QLocale::setDefault(localeByName);
            qputenv("LANG", QLocale().name().toUtf8());
        }
    }
#endif

    // Force QDomDocument to use a deterministic XML attribute order
    QHashSeed::setDeterministicGlobalSeed();

#ifdef CRASH_AUTO_TEST
    Logger::init();
#endif

#if defined(Q_OS_WIN)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#elif defined(Q_OS_MACOS)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#else
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL, true);
#endif

    // Block MLT Qt5 module to prevent crashes
    qputenv("MLT_REPOSITORY_DENY", "libmltqt:libmltglaxnimate");

    // KoraCut : un écran réglé à 125 % ou 150 % donne la même interface qu'un Mac à la même
    // échelle. Kdenlive arrondissait vers le bas sous Windows seulement (125 % devenait 100 %,
    // donc une interface plus petite que partout ailleurs). Chantier 12, 30 septembre 2026.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    // TODO: is it a good option ?
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);

    // trigger initialisation of proper icon theme
    KIconTheme::initTheme();

    Application app(argc, argv);

    // KoraCut : thème d'icônes en trait fin. Breeze reste le repli (Inherits= de index.theme).
    // On passe par KIconTheme et pas par QIcon::setThemeName : initTheme() a réglé le nom Qt
    // sur "KIconEngine" pour garder le moteur KDE, celui qui recolore les SVG.
    // Débrayable avec la variable d'environnement KORACUT_NO_ICONS.
    if (!qEnvironmentVariableIsSet("KORACUT_NO_ICONS")) {
        KIconTheme::forceThemeForTests(QStringLiteral("koracut"));
        KIconLoader::global()->reconfigure(QString());
    }

    // Default to org.kde.desktop style unless the user forces another style
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    // KoraCut : le même style de base sur les trois systèmes. KStyleManager suivait le bureau
    // (sous Plasma le style choisi par la personne, Oxygen, Kvantum ou autre ; ailleurs Breeze, ou le
    // dernier choix du menu Style), si bien qu'une même fenêtre changeait d'un ordinateur à l'autre.
    // Breeze est livré avec KoraCut sur les trois systèmes et c'est lui que la feuille de style
    // KoraCut a été réglée sur Mac ; Fusion (dans Qt) en repli. KORACUT_NATIVE_STYLE rend la main
    // au bureau, pour comparer. Chantier 12, 30 septembre 2026.
    if (qEnvironmentVariableIsSet("KORACUT_NATIVE_STYLE")) {
        KStyleManager::initStyle();
    } else {
        QStyle *base = QStyleFactory::create(QStringLiteral("Breeze"));
        if (!base) {
            base = QStyleFactory::create(QStringLiteral("Fusion"));
        }
        if (base) {
            app.setStyle(base);
        }
    }

    // KoraCut : le style KoraCut se pose par-dessus celui du système, et avant la feuille de style.
    // Il traite les trois choses que le CSS de Qt ne sait pas faire : l'anneau de focus
    // (« there is no way of getting rid of the focus rect through the style sheet », forum Qt),
    // la sélection arrondie d'une ligne de liste (le fond est peint par le style, pas par la boîte
    // CSS), et les guides d'arborescence (styliser QTreeView::branch fait disparaître les chevrons
    // avec les pointillés, essayé le 21/09/2026). Débrayable avec KORACUT_NO_STYLE.
    if (!qEnvironmentVariableIsSet("KORACUT_NO_STYLE")) {
        app.setStyle(new KoraCutStyle(app.style()));
    }

    // Try to detect package type
    LinuxPackageType packageType = getPackageType();

    // use a dedicated config file for sandbox packages,
    // however the next lines have no effect if the --config cmd option is used
    QString packageName;
    switch (packageType) {
    case LinuxPackageType::AppImage:
        packageName = QStringLiteral("appimage");
        break;
    case LinuxPackageType::Flatpak:
        packageName = QStringLiteral("flatpak");
        break;
    case LinuxPackageType::Snap:
        packageName = QStringLiteral("snap");
        break;
    default:
        break;
    }
    if (!packageName.isEmpty()) {
        KConfig::setMainConfigName(QStringLiteral("koracut-%1rc").arg(packageName));
    }

    KLocalizedString::setApplicationDomain("koracut");

    // Create KAboutData
    QString otherText = i18n("Report bugs at <a href=\"%1\">%2</a>", QStringLiteral("https://koracut.org/bogues"),
                             QStringLiteral("koracut.org/bogues"));

    // KoraCut : nom, description, site et copyright PIXEWORK.
    // La licence GPL v3 impose de garder la liste des détenteurs de droits d'auteur : elle reste
    // consultable dans l'onglet Auteurs, avec des rôles réécrits sans nom de projet ni de version.
    KAboutData aboutData(QByteArray("koracut"), i18n("KoraCut"), KDENLIVE_VERSION,
                         i18n("Multimedia creation application (editing, color, audio, VFX, photo, illustration) driven by AI agents."),
                         KAboutLicense::GPL_V3, i18n("Copyright © 2026 PIXEWORK"), otherText,
                         QStringLiteral("https://koracut.org"));
    // KoraCut
    aboutData.addAuthor(i18n("PIXEWORK (Amadou BA)"), i18n("Product direction and development"), QString(), QStringLiteral("https://pixework.pro"));
    // Détenteurs de droits d'auteur antérieurs, conservés au titre de la GPL v3.
    aboutData.addAuthor(i18n("Jean-Baptiste Mardelle"), i18n("Editing engine development"), QStringLiteral("jb@kdenlive.org"));
    aboutData.addAuthor(i18n("Julius Künzel"), i18n("Earlier work on the application core"), QStringLiteral("julius.kuenzel@kde.org"));
    aboutData.addAuthor(i18n("Vincent Pinon"), i18n("Earlier work on the application core"), QStringLiteral("vpinon@kde.org"));
    // other active developers (alphabetical)
     aboutData.addAuthor(i18n("Eric Jiang"), i18n("Bug fixing and testing"), QStringLiteral("erjiang@alumni.iu.edu"));
    aboutData.addAuthor(i18n("Simon A. Eugster"), i18n("Color scopes, bug fixing"), QStringLiteral("simon.eu@gmail.com"));
    aboutData.addAuthor(i18n("Jason Wood"), i18n("Earlier work on the application core"), QStringLiteral("jasonwood@blueyonder.co.uk"));
    // non developers (alphabetical)
    aboutData.addCredit(i18n("Farid Abdelnour"), i18n("Logo, promotion, testing"));
    aboutData.addCredit(i18n("balooii"),  i18n("Monitor, scopes and timeline"));
    aboutData.addCredit(i18n("Nicolas Carion"), i18n("Code and timeline architecture"));
    aboutData.addCredit(i18n("Dan Dennedy"), i18n("MLT maintainer, Bug fixing, etc."));
    aboutData.addCredit(i18n("Darby Johnston"), i18n("OTIO rewrite"));
    aboutData.addCredit(i18n("Bernd Jordan"), i18n("Documentation, effects interface, testing"));
    aboutData.addCredit(i18n("Eugen Mohr"), i18n("Documentation, bug triage, testing"));
    aboutData.addCredit(i18n("Nara Oliveira"), i18n("Logo"));
    aboutData.addCredit(i18n("Bruno Santos"), i18n("Testing"));
    aboutData.addCredit(i18n("Massimo Stella"), i18n("Advice and testing"));

    // Pas d'onglet Traduction : le bandeau que les bibliothèques y posent renvoie vers un autre
    // projet et n'est pas modifiable depuis ici. Les traducteurs restent crédités dans les
    // en-têtes des fichiers po/ et dans AUTHORS.
    aboutData.setTranslator(QString(), QString());
    aboutData.setOrganizationDomain(QByteArray("pixework.pro"));
    aboutData.setBugAddress(QByteArray("https://koracut.org/bogues"));
    // Remplace le bandeau par défaut de l'onglet Auteurs, qui pointe vers un autre projet.
    aboutData.setCustomAuthorText(i18n("Report bugs at koracut.org/bogues"),
                                  i18n("Report bugs at <a href=\"https://koracut.org/bogues\">koracut.org/bogues</a>."));

    aboutData.addComponent(aboutData.displayName(), QString(), KDENLIVE_FULL_VERSION_STRING, aboutData.homepage());

    aboutData.addComponent(i18n("MLT"), i18n("Open source multimedia framework."), mlt_version_get_string(),
                           QStringLiteral("https://mltframework.org") /*, KAboutLicense::LGPL_V2_1*/);
    aboutData.addComponent(i18n("FFmpeg"), i18n("A complete, cross-platform solution to record, convert and stream audio and video."), QString(),
                           QStringLiteral("https://ffmpeg.org"));

    aboutData.setDesktopFileName(QStringLiteral("pro.pixework.koracut"));

    // Set application data
    KAboutData::setApplicationData(aboutData);
#ifndef Q_OS_MACOS // skip this on macOS to have proper mime-type icon visible
    app.setWindowIcon(QIcon(QStringLiteral(":/pics/koracut.png")));
#endif

    app.setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, true);

    // KoraCut: thème par défaut
    // 1) Les schémas de couleurs KoraCut sont installés dans share/color-schemes. S'ils n'y sont pas
    //    (paquet incomplet, bundle macOS sans dossier share), on copie la version embarquée dans les
    //    ressources vers le dossier utilisateur, avant que KColorSchemeManager ne construise sa liste.
    // KoraCut, 25 septembre 2026 : les autres thèmes livrés (Très sombre, Gris, Contraste élevé, Matrix,
    // Spider-Man, Punk, Océan, Sable, Nuit violette) sont écrits depuis data/koracut-themes.json par le
    // moteur de thèmes, seulement quand ils diffèrent de ce qui est déjà sur le disque.
    KoraCutThemes::self()->installBuiltins();
    for (const QString &scheme : {QStringLiteral("KoraCutDark"), QStringLiteral("KoraCutLight")}) {
        const QString relPath = QStringLiteral("color-schemes/%1.colors").arg(scheme);
        const QString destDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/color-schemes");
        const QString userCopy = destDir + QStringLiteral("/%1.colors").arg(scheme);
        // KoraCut, 23 septembre 2026 : une copie déjà faite n'était jamais rafraîchie. Celle du
        // 19 septembre a masqué une heure durant la nouvelle marche #252525, parce que le dossier de
        // l'utilisateur passe avant celui de l'installation. Ces fichiers sont les nôtres (un thème
        // installé par l'utilisateur porte un autre nom) : s'ils diffèrent de la version embarquée,
        // on les remplace.
        QFile embedded(QStringLiteral(":/%1.colors").arg(scheme));
        QFile existing(userCopy);
        if (existing.exists() && embedded.open(QIODevice::ReadOnly) && existing.open(QIODevice::ReadOnly)) {
            const bool stale = embedded.readAll() != existing.readAll();
            existing.close();
            if (stale) {
                QFile::remove(userCopy);
                QFile::copy(embedded.fileName(), userCopy);
                QFile::setPermissions(userCopy, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
            }
        } else if (QStandardPaths::locate(QStandardPaths::GenericDataLocation, relPath).isEmpty()) {
            if (QDir().mkpath(destDir)) {
                QFile::copy(embedded.fileName(), userCopy);
                QFile::setPermissions(userCopy, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
            }
        }
    }
    // 2) Feuille de style Qt pour l'aplat KoraCut (boutons, champs, onglets, menus, barres de défilement).
    //    Chargée seulement si le schéma actif est un schéma KoraCut (ou aucun schéma, cas du premier
    //    lancement où l'écran de démarrage active « KoraCutDark »), pour ne pas casser les autres thèmes.
    //    Débrayable avec la variable d'environnement KORACUT_NO_QSS. Le code est dans UiUtils, il est rappelé
    //    par l'écran de démarrage et à chaque changement de palette.
    UiUtils::applyKoraCutStyleSheet();
    // 3) KoraCut: police d'interface au cran « base » de la charte, 12 px (« 11 - Refonte UI-UX », 3.2).
    //    Elle était à 11 px, c'est-à-dire sous le minimum normatif de Microsoft (12 px en Regular,
    //    « illisible dans certaines langues ») et au cran secondaire d'Apple. Le 11 px reste, mais
    //    comme cran dense volontaire, réservé aux listes longues et à la timeline.
    //    Posée sur la police de l'application, pas dans la feuille de style : un « font-size » global en QSS
    //    écrase les textes à taille relative (constaté le 19/09/2026 sur le message d'accueil du chutier).
    //    Même taille sur macOS, Windows et Linux. Débrayable avec KORACUT_NO_FONT.
    //    Depuis le 30 septembre 2026, la famille aussi est la même partout : IBM Plex Sans, embarquée
    //    (koracutfonts.h). Avec la police du système, la largeur de chaque libellé changeait d'un
    //    système à l'autre, et avec elle les alignements.
    if (!qEnvironmentVariableIsSet("KORACUT_NO_FONT")) {
        QFont koraCutFont = app.font();
        if (KoraCutFonts::active()) {
            koraCutFont = QFont(KoraCutFonts::uiFamily());
            koraCutFont.setStyleHint(QFont::SansSerif);
        }
        koraCutFont.setPixelSize(KoraCutMetrics::FontBase);
        app.setFont(koraCutFont);
    }

    // Create command line parser with options
    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);

    // config option is processed in KConfig (src/core/kconfig.cpp)
    parser.addOption(QCommandLineOption(QStringLiteral("config"), i18n("Set a custom config file name."), QStringLiteral("config file")));
    QCommandLineOption mltPathOption(QStringLiteral("mlt-path"), i18n("Set the path for MLT environment."), QStringLiteral("mlt-path"));
    parser.addOption(mltPathOption);
    QCommandLineOption mltLogLevelOption(QStringLiteral("mlt-log"), i18n("Set the MLT log level. Leave this unset for level \"warning\"."),
                                         QStringLiteral("verbose/debug"));
    parser.addOption(mltLogLevelOption);
    QCommandLineOption clipsOption(QStringLiteral("i"), i18n("Comma separated list of files to add as clips to the bin."), QStringLiteral("clips"));
    parser.addOption(clipsOption);

    // render options
    QCommandLineOption renderOption(QStringLiteral("render"), i18n("Directly render the project and exit."));
    parser.addOption(renderOption);

    QCommandLineOption presetOption(QStringLiteral("render-preset"), i18n("KoraCut render preset name (MP4-H264/AAC will be used if none given)."),
                                    QStringLiteral("renderPreset"), QString());
    parser.addOption(presetOption);

    QCommandLineOption exitOption(QStringLiteral("render-async"),
                                  i18n("Exit after (detached) render process started, without this flag it exists only after it finished."));
    parser.addOption(exitOption);

    QCommandLineOption disableWelcome(QStringLiteral("no-welcome"), i18n("Do not show any welcome screen."));
    parser.addOption(disableWelcome);

    QCommandLineOption debugOption(QStringLiteral("debug"), i18n("Show some development specific features in the UI, disable all exclude lists for assets."));
    parser.addOption(debugOption);

    QCommandLineOption saveDebugOption(QStringLiteral("setup-report"), i18n("Save a json report about components in the given path."), QStringLiteral("reportFile"));
    parser.addOption(saveDebugOption);

    parser.addPositionalArgument(QStringLiteral("file"), i18n("KoraCut document to open."));
    parser.addPositionalArgument(QStringLiteral("rendering"), i18n("Output file for rendered video."));

    // Parse command line
    parser.process(app);
    aboutData.processCommandLine(&parser);
    if (parser.isSet(saveDebugOption)) {
        QJsonObject report, property;
        QJsonArray properties;
        const auto components = KAboutData::applicationData().components();
        for (auto &component : components) {
            property["name"] = component.name();
            property["version"] = component.version();
            properties.append(property);
            qDebug() << component.name() << " = " << component.version();
        }
        qDebug() << "Packaging = " << packageName;
        report["components"] = properties;
        report["packageType"] = packageName;
        const QString outputFilename = parser.value(saveDebugOption);
        if (!outputFilename.isEmpty()) {
            QFile file(outputFilename);
            if (file.exists()) {
                qWarning() << "Cannot overwrite existing file " << outputFilename;
                return EXIT_FAILURE;
            }
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                qWarning() << "Cannot write into file " << outputFilename;
                return EXIT_FAILURE;
            }
            file.write(QJsonDocument(report).toJson());
        } else {
            qCritical() << "You need to provide a valid file path to the --setup-report command line option.";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    QUrl renderUrl;
    QString presetName;
    QStringList clipsToLoad;
    if (parser.positionalArguments().count() != 0) {
        const QString inputFilename = parser.positionalArguments().at(0);
        const QFileInfo fileInfo(inputFilename);
        app.url = QUrl(inputFilename);
        if (fileInfo.exists() || app.url.scheme().isEmpty()) { // easiest way to detect "invalid"/unintended URLs is no scheme
            app.url = QUrl::fromLocalFile(fileInfo.absoluteFilePath());
        }
        if (parser.positionalArguments().count() > 1) {
            if (parser.isSet(renderOption)) {
                // Output render
                const QString outputFilename = parser.positionalArguments().at(1);
                const QFileInfo outFileInfo(outputFilename);
                if (!outFileInfo.exists()) {
                    // easiest way to detect "invalid"/unintended URLs is no scheme
                    renderUrl = QUrl::fromLocalFile(outFileInfo.absoluteFilePath());
                }
            } else {
                // We may want to open several clips in a project
                if (!app.url.isEmpty()) {
                    // Check if first url is not a project file
                    if (!app.url.toLocalFile().endsWith(QLatin1String(".koracut")) && !app.url.toLocalFile().endsWith(QLatin1String(".kdenlive"))) {
                        if (!ProjectManager::isKdenliveProjectFile(app.url)) {
                            // We are trying to open clips
                            clipsToLoad << app.url.toLocalFile();
                            app.url.clear();
                        }
                    }
                    for (int i = 1; i < parser.positionalArguments().count(); i++) {
                        const QString outputFilename = parser.positionalArguments().at(i);
                        const QFileInfo outFileInfo(outputFilename);
                        if (outFileInfo.exists()) {
                            clipsToLoad << outFileInfo.absoluteFilePath();
                        }
                    }
                }
            }
        }
    }

    if (parser.isSet(renderOption)) {
        if (app.url.isEmpty()) {
            qCritical() << "You need to give a valid file if you want to render from the command line.";
            return EXIT_FAILURE;
        }
        if (renderUrl.isEmpty()) {
            qCritical() << "You need to give a non existing output file to render from the command line.";
            return EXIT_FAILURE;
        }
        if (parser.isSet(presetOption)) {
            presetName = parser.value(presetOption);
        } else {
            presetName = QStringLiteral("MP4-H264/AAC");
            qDebug() << "No render preset given, using default:" << presetName;
        }
        if (!Core::build(packageType, true)) {
            return EXIT_FAILURE;
        }
        pCore->initHeadless(app.url);
        app.processEvents();

        // ensure we have a proper koracut_render path, particular important for AppImage
        Wizard::fixKdenliveRenderPath();

        RenderRequest *renderrequest = new RenderRequest();
        renderrequest->setOutputFile(renderUrl.toLocalFile());
        renderrequest->loadPresetParams(presetName);
        // request->setPresetParams(m_params);
        renderrequest->setDelayedRendering(false);
        renderrequest->setProxyRendering(false);
        renderrequest->setEmbedSubtitles(false);
        renderrequest->setTwoPass(false);
        renderrequest->setAudioFilePerTrack(false);

        /*bool guideMultiExport = false;
        int guideCategory = m_view.guideCategoryChooser->currentCategory();
        renderrequest->setGuideParams(m_guidesModel, guideMultiExport, guideCategory);*/

        renderrequest->setOverlayData(QString());
        std::vector<RenderRequest::RenderJob> renderjobs = renderrequest->process();
        app.processEvents();

        if (!renderrequest->errorMessages().isEmpty()) {
            qInfo() << "The following errors occurred while trying to render:\n" << renderrequest->errorMessages().join(QLatin1Char('\n'));
        }

        int exitCode = EXIT_SUCCESS;

        for (const auto &job : renderjobs) {
            QStringList argsJob = RenderRequest::argsByJob(job, false);
            if (parser.value(mltLogLevelOption) == QStringLiteral("debug")) {
                argsJob << "--debug";
            }
            qDebug() << "* CREATED JOB WITH ARGS: " << argsJob;
            qDebug() << "starting koracut_render process using: " << KdenliveSettings::koracutrendererpath();
            if (!parser.isSet(exitOption)) {
                if (QProcess::execute(KdenliveSettings::koracutrendererpath(), argsJob) != EXIT_SUCCESS) {
                    exitCode = EXIT_FAILURE;
                    break;
                }
            } else {
                if (!QProcess::startDetached(KdenliveSettings::koracutrendererpath(), argsJob)) {
                    qCritical() << "Error starting render job" << argsJob;
                    exitCode = EXIT_FAILURE;
                    break;
                } else {
                    KNotification::event(QStringLiteral("RenderStarted"), i18n("Rendering %1 started", job.outputPath), QPixmap());
                }
            }
        }
        /*QMapIterator<QString, QString> i(rendermanager->m_renderFiles);
        while (i.hasNext()) {
            i.next();
            // qDebug() << i.key() << i.value() << rendermanager->startRendering(i.key(), i.value(), {});
        }*/
        pCore->projectManager()->closeCurrentDocument(false, false);
        app.processEvents();
        Core::clean();
        app.processEvents();
        return exitCode;
    }

#ifdef Q_OS_WIN
#include <stdlib.h>
    QString path = getShortPath(qApp->applicationDirPath()) + QLatin1Char(';') + qEnvironmentVariable("PATH");
    _wputenv_s(L"PATH", path.toStdWString().c_str());
#endif

    if (QQuickWindow::graphicsApi() == QSGRendererInterface::Vulkan) {
        qWarning() << "::: Detected QML VULKAN backend, switching to OpenGL...";
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    }

#ifndef NODBUS
    // Init DBus services
    KDBusService programDBusService;
#endif

    // qApp->processEvents(QEventLoop::AllEvents);

#if defined(KF5_USE_CRASH)
    KCrash::initialize();
#endif

    if (parser.value(mltLogLevelOption) == QStringLiteral("verbose")) {
        mlt_log_set_level(MLT_LOG_VERBOSE);
    } else if (parser.value(mltLogLevelOption) == QStringLiteral("debug")) {
        mlt_log_set_level(MLT_LOG_DEBUG);
    }
    if (parser.isSet(clipsOption)) {
        clipsToLoad = parser.value(clipsOption).split(QLatin1Char(','));
    }

    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);

    // KoraCut, relais de version (voir MainWindow::koraCutRelay). Tant que la fenêtre principale
    // n'existe pas, par exemple sur l'écran d'accueil, aucun projet n'est ouvert : une demande de
    // relais n'a rien à enregistrer, on quitte tout de suite. Dès que la fenêtre existe, c'est
    // elle qui prend la main et ce minuteur s'arrête.
    const QString relais = qEnvironmentVariable("KORACUT_RELAIS");
    if (!relais.isEmpty()) {
        auto *relaisTimer = new QTimer(&app);
        relaisTimer->setInterval(1000);
        QObject::connect(relaisTimer, &QTimer::timeout, &app, [relais, relaisTimer]() {
            if (Core::self() && Core::self()->window()) {
                relaisTimer->stop();
                return;
            }
            if (!QFile::exists(relais + QStringLiteral("/demande"))) {
                return;
            }
            QFile::remove(relais + QStringLiteral("/demande"));
            QFile::remove(Core::lockFilePath());
            QFile out(relais + QStringLiteral("/projet"));
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.close();
            }
            std::_Exit(0);
        });
        relaisTimer->start();
    }

#ifdef Q_OS_UNIX
    // KoraCut, 28 septembre 2026 : SIGTERM et SIGINT ferment proprement (copie de récupération, porte et
    // témoin retirés), au lieu de laisser une instance qui ne répond plus aux scripts et aux agents. Le
    // gestionnaire de signal ne fait qu'écrire un octet ; tout le reste se passe dans la boucle de Qt.
    static int terminatePipe[2] = {-1, -1};
    if (::pipe(terminatePipe) == 0) {
        struct sigaction action {};
        action.sa_handler = [](int) {
            const char c = 1;
            [[maybe_unused]] const ssize_t n = ::write(terminatePipe[1], &c, 1);
        };
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);
        auto *notifier = new QSocketNotifier(terminatePipe[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [notifier]() {
            char c;
            [[maybe_unused]] const ssize_t n = ::read(terminatePipe[0], &c, 1);
            if (Core::self() && Core::self()->window()) {
                QMetaObject::invokeMethod(Core::self()->window(), "koraCutTerminate", Qt::QueuedConnection);
            } else {
                // Écran d'accueil ou démarrage : rien à enregistrer.
                QFile::remove(Core::lockFilePath());
                notifier->setEnabled(false);
                QCoreApplication::quit();
            }
        });
    }
#endif

    if (!Core::build(packageType, false, parser.isSet(debugOption), app.url.isEmpty() && clipsToLoad.isEmpty() && !parser.isSet(disableWelcome))) {
        // App is crashing, delete config files and restart
        result = EXIT_CLEAN_RESTART;
    } else {
        pCore->initGUI(parser.value(mltPathOption), app.url, clipsToLoad);
        result = app.exec();
    }
    Core::clean();
    if (result == EXIT_RESTART || result == EXIT_CLEAN_RESTART) {
        qCDebug(KDENLIVE_LOG) << "restarting app";
        if (result == EXIT_CLEAN_RESTART) {
            resetConfig();
        }
        QStringList progArgs;
        if (argc > 1) {
            // Start at 1 to remove app name
            for (int i = 1; i < argc; i++) {
                progArgs << QString(argv[i]);
            }
        }
        auto *restart = new QProcess;
        restart->start(app.applicationFilePath(), progArgs);
        restart->waitForReadyRead();
        restart->waitForFinished(1000);
        result = EXIT_SUCCESS;
    }
    return result;
}
