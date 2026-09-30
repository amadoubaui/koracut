/*
    SPDX-FileCopyrightText: 2025 Julius Künzel <julius.kuenzel@kde.org>
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QIcon>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QtQmlIntegration>

class UiUtils : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT

    Q_PROPERTY(QFont fixedFont READ getFixedFont CONSTANT)
    Q_PROPERTY(QFont smallestReadableFont READ getSmallestReadableFont CONSTANT)
    /** @brief KoraCut : le cran Étiquette de la charte, 10 px en 600. Pour le nom d'un clip dans
     *  son bandeau, les badges et les timecodes de la règle. */
    Q_PROPERTY(QFont labelFont READ getLabelFont CONSTANT)
    Q_PROPERTY(qreal baseSizeMedium READ getBaseSizeMedium CONSTANT)

public:
    static UiUtils *instance();
    static UiUtils *create(QQmlEngine *, QJSEngine *);

    [[nodiscard]] QFont getFixedFont();
    [[nodiscard]] QFont getSmallestReadableFont();
    [[nodiscard]] QFont getLabelFont();
    [[nodiscard]] qreal getBaseSizeMedium();

public: // STATIC
    /** @returns a rotated version of the icon associated with @param iconName.
     *  It will be of size @param iconSize and @param rotation.
     */
    static QIcon rotatedIcon(const QString &iconName, const QSize iconSize, qreal rotation = -90);
    /** @returns a user selected filename to save stuff. Ensures the requested file extension is always appended to the filename
     */
    static QString getSaveFileName(QWidget *parent = nullptr, const QString &caption = QString(), const QString &dir = QString(),
                                   const QString &filter = QString(), const QString &extension = QString());
    static const QStringList getProxyForbiddenParams();
    static const QStringList getRenderForbiddenParams();
    static QStringList checkUnknownProxyParams(QString proxyData);
    static void addSafeParameters(QStringList unknownParams);
    /** KoraCut : pose la feuille de style qui va avec le schéma de couleurs actif
     *  (koracut.qss pour « KoraCutDark » ou aucun schéma, koracut-light.qss pour « KoraCutLight »,
     *  rien pour les autres schémas). Appelée au démarrage, par l'écran de démarrage et à chaque
     *  changement de palette. Débrayable avec la variable d'environnement KORACUT_NO_QSS.
     */
    static void applyKoraCutStyleSheet();
    /** @brief Pose la feuille de style KoraCut construite sur @p base (« dark », « light », vide = aucune),
     * avec les remplacements de couleurs d'un thème (« #1E1E1E:#101010, … »). */
    static void setKoraCutStyleSheet(const QString &base, const QString &remplacements);
    /** @brief Chemin du fichier .colors d'un thème, vide s'il n'existe pas. */
    static QString themeFile(const QString &id);
    /** @brief Nom affiché d'un thème ([General] Name), @p fallback s'il n'en a pas. */
    static QString themeName(const QString &path, const QString &fallback);
    /** @brief Base d'un thème KoraCut (« dark », « light »), vide pour un schéma ordinaire. */
    static QString themeBase(const QString &path);
    /** @brief Applique un thème depuis son fichier, sans passer par le gestionnaire de schémas
     * (qui ne connaît pas un thème installé pendant la séance). */
    static void applyThemeFile(const QString &path);
    /** @brief Les thèmes KoraCut construits sur une base (« dark » ou « light »), Sombre et Clair compris. */
    static QList<QPair<QString, QString>> koraCutThemesFor(const QString &base);

    /** KoraCut : applique le mode de thème choisi (`koracut_theme_mode`).
     *
     *  Trois états et non deux. « system » lit le mode clair ou sombre du système par
     *  `QStyleHints::colorScheme()` ; « light » et « dark » sont des choix explicites.
     *  Pose ensuite la feuille de style correspondante. Débrayable avec KORACUT_NO_THEME.
     */
    static void applyThemeMode();

    /** KoraCut : branche le suivi à chaud du mode système.
     *
     *  À appeler une seule fois au démarrage. Quand le système bascule en sombre à la tombée
     *  du jour, l'application suit sans redémarrer, mais seulement si le mode vaut « system ».
     */
    static void followSystemColorScheme();
    /** @brief KoraCut : les thèmes en plus de Clair et Sombre (identifiant, nom affiché).
     * Un thème KoraCut est un schéma de couleurs .colors qui porte une section [KoraCut]
     * (feuille de style de départ et remplacements de couleurs). Ceux livrés avec
     * l'application et ceux que l'utilisateur a installés. */
    static QList<QPair<QString, QString>> koraCutThemes();
    /** @brief KoraCut : le dossier où s'installent les thèmes de l'utilisateur. */
    static QString userThemesFolder();
    /** @brief KoraCut : copie un fichier .colors dans le dossier des thèmes. Rend l'identifiant, vide en cas d'échec. */
    static QString installTheme(const QString &path, QString &error);
};
