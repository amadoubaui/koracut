/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QColor>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

/** @class KoraCutThemes
 * @brief Le moteur de thèmes de KoraCut (25 septembre 2026, document 14, journal).
 *
 * Un thème est une **palette de jetons** : fond, surface, champ, texte, accent, couleurs des pistes…
 * (liste complète : tokenNames()). Tout le reste en est tiré au moment d'appliquer :
 * - la feuille de style, rendue depuis le gabarit `data/koracut.qss` (`{surface}`, `{voile@18}`…) ;
 * - la palette Qt et le jeu de couleurs KDE, par le fichier `.colors` du thème ;
 * - les couleurs des clips de la timeline (jetons `piste_*`, seulement si le thème les change) ;
 * - la police d'interface (`police`, « mono » = la police à chasse fixe du système) et le rayon des coins.
 *
 * Sur le disque, un thème reste un `.colors` de KDE avec une section `[KoraCut]` :
 * `Base=dark|light`, `Jetons=fond:#0A0A0A,texte:#7DFF9A,…` (seulement les jetons choisis, le reste est
 * dérivé), `Police=`, `Rayon=`, `Origine=fourni|cree`, `Inspiration=`. L'ancien format
 * (`Remplacements=#1E1E1E:#141414,…`) reste lu.
 *
 * Les thèmes livrés sont décrits dans `data/koracut-themes.json` ; ceux que crée un agent ou la
 * personne vont dans le dossier des thèmes de l'utilisateur. Changer de thème s'applique tout de
 * suite et se garde dans `koracutrc` (réglage `koracut_theme_mode` ; le thème d'avant dans
 * `[KoraCut] themePrecedent`, pour « reviens comme avant »).
 *
 * **Contraste** : chaque palette est contrôlée (WCAG 2.1 : 4,5:1 pour le texte, 3:1 pour les
 * contours et les repères). Une palette créée ou modifiée est corrigée automatiquement, et les
 * corrections sont rendues pour être dites. Rien ne se calcule pendant la lecture : tout est fait
 * une fois, au changement de thème.
 *
 * Pour la page Apparence des Préférences : list(), apply(), create(), modify(), remove(), revert(),
 * et le signal themeChanged().
 */
class KoraCutThemes : public QObject
{
    Q_OBJECT

public:
    static KoraCutThemes *self();

    struct Theme
    {
        /** @brief L'identifiant du schéma (« KoraCutMatrix »), celui que garde `koracut_theme_mode`. */
        QString id;
        /** @brief Le nom court sans accent qu'emploient les agents (« matrix », « spider-man »). */
        QString slug;
        /** @brief Le nom affiché, dans la langue de l'interface. */
        QString name;
        /** @brief « dark » ou « light ». */
        QString base;
        bool builtin{false};
        /** @brief Créé par un agent ou dans l'application : il peut être modifié et supprimé. */
        bool created{false};
        /** @brief Le thème à l'écran en ce moment. */
        bool active{false};
        QString file;
        QString inspiration;
        /** @brief Quelques jetons pour dessiner un aperçu : fond, surface, texte, accent. */
        QMap<QString, QColor> preview;
    };

    struct Correction
    {
        QString token;
        QColor before;
        QColor after;
        QString reason;
    };

    struct Check
    {
        QString fg;
        QString bg;
        double minimum{0};
        double ratio{0};
        bool ok{true};
        /** @brief Écart connu de la charte, accepté pour un thème livré (voir `ecarts_charte`). */
        bool accepted{false};
    };

    /** @brief Les thèmes : les livrés, ceux qui ont été créés, ceux qui ont été installés. */
    QList<Theme> list() const;
    /** @brief Le thème à l'écran (identifiant de schéma). */
    QString currentId() const;
    /** @brief Le réglage : « system », « light », « dark » ou un identifiant de thème. */
    QString mode() const;

    /** @brief Applique un thème, tout de suite. @p name : « sombre », « clair », « systeme »,
     * « precedent », un nom court (« matrix »), un identifiant ou un nom affiché ; la casse, les
     * accents, les espaces et les tirets ne comptent pas. Le thème d'avant est gardé. */
    bool apply(const QString &name, QString *error = nullptr);
    /** @brief Revient au thème d'avant le dernier changement. */
    bool revert(QString *error = nullptr);
    /** @brief Le thème d'avant le dernier changement (réglage), vide s'il n'y en a pas. */
    QString previous() const;

    /** @brief Crée un thème et l'applique. @p tokens : au moins `fond` ou `accent`, les autres
     * facultatifs (« #RRGGBB » ou « #RRGGBBAA »). La palette est complétée, contrôlée et corrigée
     * (@p corrections). @p police : vide, « mono » ou une famille installée. @p rayon : -1 (celui
     * de la charte) ou 0 à 12. Rend l'identifiant, vide en cas d'erreur. Un nom déjà pris par un
     * thème créé le remplace. */
    QString create(const QString &name, const QMap<QString, QString> &tokens, const QString &inspiration, const QString &police, int rayon,
                   QList<Correction> *corrections, QString *error = nullptr, const QStringList &ignore = {});
    /** @brief Change des jetons (ou la police, le rayon : clés « police » et « rayon ») d'un thème
     * créé. Sur un thème livré, crée « <nom> perso » à partir de lui. Rend l'identifiant du thème
     * modifié et l'applique s'il était à l'écran (ou s'il vient d'être créé). */
    QString modify(const QString &name, const QMap<QString, QString> &changes, QList<Correction> *corrections, QString *error = nullptr);
    /** @brief Supprime un thème créé (jamais un thème livré). S'il était à l'écran, revient au précédent. */
    bool remove(const QString &name, QString *error = nullptr);

    /** @brief Trouve un thème par son nom (voir apply) ; vide s'il n'existe pas. */
    QString resolve(const QString &name) const;
    /** @brief La palette complète d'un thème (jetons dérivés compris). */
    QMap<QString, QColor> tokens(const QString &id) const;
    /** @brief Les jetons écrits dans le thème (les autres sont dérivés). */
    QMap<QString, QColor> explicitTokens(const QString &id) const;
    /** @brief Le contrôle de contraste d'un thème. */
    QList<Check> checks(const QString &id) const;
    /** @brief Police et rayon d'un thème (vide et -1 : ceux de la charte). */
    QString police(const QString &id) const;
    int rayon(const QString &id) const;

    /** @brief Écrit (ou rafraîchit) les `.colors` des thèmes livrés dans le dossier de l'utilisateur.
     * À appeler au démarrage, avant que KColorSchemeManager ne dresse sa liste. Ne réécrit un
     * fichier que s'il diffère. */
    void installBuiltins();

    /** @brief Pose la feuille de style, la police et les couleurs du thème de ce fichier `.colors`
     * (vide : Sombre). Appelé par UiUtils à chaque changement de schéma. Émet themeChanged(). */
    void applyScheme(const QString &path, const QString &id);

    /** @brief Un jeton du thème à l'écran, gardé en mémoire (appel sans coût, pour la peinture). */
    static QColor token(const QString &name, int alpha = -1);
    /** @brief La couleur d'une piste : celle du thème s'il en choisit une (jeton `piste_*` différent
     * de la charte), sinon @p fallback (le réglage de l'utilisateur). */
    static QColor trackColor(const QString &name, const QColor &fallback);

    // ---- Fonctions pures, pour les tests et les outils ---------------------------------------

    /** @brief Les noms des jetons de couleur, dans l'ordre des explications. */
    static QStringList tokenNames();
    /** @brief Une phrase par jeton, pour les agents et la page Apparence. */
    static QString tokenLabel(const QString &name);
    /** @brief Complète une palette : ce qui manque est dérivé de ce qui est donné. */
    static QMap<QString, QColor> derive(const QMap<QString, QColor> &given);
    /** @brief Vrai si la palette est sombre (d'après son fond). */
    static bool isDark(const QMap<QString, QColor> &tokens);
    /** @brief Les paires contrôlées et leur résultat. Les jetons `piste_*` ne sont contrôlés que
     * s'ils sont dans @p explicitKeys. */
    static QList<Check> contrastChecks(const QMap<QString, QColor> &tokens, const QStringList &explicitKeys);
    /** @brief Corrige la palette jusqu'à ce que tous les contrôles passent ; les jetons touchés sont
     * ajoutés à @p explicitTokens. @p ignore : paires « texte/fond » laissées telles quelles (les
     * écarts connus de la charte, sur la copie d'un thème livré). */
    static QList<Correction> correct(QMap<QString, QColor> &explicitTokens, const QStringList &ignore = {});
    /** @brief Rapport de contraste WCAG (le premier plan translucide est composé sur le fond). */
    static double contrast(const QColor &fg, const QColor &bg);
    /** @brief Rend le gabarit avec une palette. @p rayon -1 : rayons de la charte. */
    static QString renderStyleSheet(const QString &tmpl, const QMap<QString, QColor> &tokens, int rayon = -1);
    /** @brief Le texte du `.colors` d'une palette. */
    static QString schemeText(const QString &id, const QString &name, const QString &nameFr, const QMap<QString, QColor> &explicitTokens, const QString &police,
                              int rayon, const QString &origine, const QString &inspiration);
    /** @brief « #RRGGBB » ou « #RRGGBBAA » ; invalide si le texte ne se lit pas (noms CSS acceptés). */
    static QColor parseColor(const QString &text);
    static QString colorText(const QColor &color);
    /** @brief Nom réduit pour comparer : minuscules, sans accent, sans espace ni tiret. */
    static QString simplify(const QString &text);

Q_SIGNALS:
    /** @brief Un thème vient d'être appliqué (identifiant du schéma). */
    void themeChanged(const QString &id);
    /** @brief Un thème a été créé, modifié ou supprimé : les menus se refont. */
    void listChanged();

private:
    explicit KoraCutThemes(QObject *parent = nullptr);
    struct Builtin
    {
        QString slug;
        QString id;
        QString name;
        QString nameFr;
        QString base;
        QString police;
        int rayon{-1};
        QString inspiration;
        QMap<QString, QColor> tokens;
        QStringList accepted;
    };
    QList<Builtin> m_builtins;
    void loadBuiltins();
    const Builtin *builtin(const QString &id) const;
    QString fileFor(const QString &id) const;
    bool setMode(const QString &mode, QString *error);
};
