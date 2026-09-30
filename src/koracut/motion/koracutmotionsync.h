/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;

/** @brief Le synchroniseur du module Motion : lit et écrit l'index.html d'une composition HyperFrames.
 *
 * Document 19, § 2 et § 3. Une composition HyperFrames est déjà une timeline écrite en HTML : chaque
 * élément dans le temps porte `data-start`, `data-duration`, `data-track-index`. Ce fichier en tire
 * la liste des éléments (KoraCutMotion::analyse), résout les débuts relatifs (`intro + 0.5` : la fin
 * du clip cité), calcule une empreinte par élément (le rendu n'est refait que si elle change) et
 * réécrit un attribut ou un texte par modification minimale du fichier : on remplace la valeur à
 * sa place, jamais le document entier, pour que les agents et Studio retrouvent leur mise en forme.
 *
 * Tout ce qui est dans le namespace KoraCutMotion travaille sur du texte, sans Qt GUI ni Node :
 * c'est ce que tests/koracutmotionsynctest.cpp vérifie.
 */
namespace KoraCutMotion {

/** Un attribut d'une balise ouvrante, avec sa place dans le texte source. */
struct Attribute
{
    QString name;
    QString value;
    int start = 0;      ///< début du nom
    int end = 0;        ///< fin de la valeur (guillemet fermant compris)
    int valueStart = 0; ///< début de la valeur, sans guillemet
    int valueEnd = 0;   ///< fin de la valeur, sans guillemet
    bool quoted = true;
};

/** Une balise ouvrante lue dans le texte. */
struct Tag
{
    QString name; ///< en minuscules
    int start = 0; ///< le « < »
    int end = 0;   ///< après le « > »
    bool closing = false;
    bool selfClosing = false;
    QList<Attribute> attributes;

    bool has(const QString &attr) const;
    QString attr(const QString &attr) const;
    const Attribute *find(const QString &attr) const;
};

/** Ce qu'est un élément temporel. */
enum class Kind { Block, Image, Video, Audio, Composition, Whole };

/** Un élément temporel de la composition, tel qu'il sera posé dans la timeline de KoraCut. */
struct Element
{
    QString id;
    Kind kind = Kind::Block;
    QString tag;
    double start = 0.;        ///< début résolu, en secondes
    QString startExpr;        ///< `data-start` tel qu'écrit (« intro + 0.5 » ou « 1.5 »)
    bool startResolved = true;
    double duration = 0.;     ///< durée, en secondes
    bool durationKnown = true; ///< faux pour un média sans `data-duration` : la durée du fichier fait foi
    int trackIndex = 0;
    double mediaStart = 0.;   ///< `data-media-start` d'un média, ou `data-koracut-in` (point d'entrée du rendu d'un bloc)
    double volume = 1.;
    QString text;             ///< texte visible d'un bloc
    QString src;              ///< fichier d'un média ou d'une sous-composition
    QString hash;             ///< empreinte : HTML de l'élément (sans sa place), styles et scripts de la page
    int outerStart = 0;       ///< du « < » de la balise ouvrante
    int outerEnd = 0;         ///< à la fin de la balise fermante
    int innerStart = 0;
    int innerEnd = 0;
    int tagIndex = -1;        ///< index de la balise ouvrante dans la liste des balises
    int textTagIndex = -1;    ///< composition d'un seul élément : la balise marquée data-koracut-texte, qui porte le texte

    QString kindName() const;
    /** Le nom montré dans la timeline : le texte d'un bloc, le fichier d'un média, l'id sinon. */
    QString displayName() const;
};

/** La composition lue. */
struct Composition
{
    QString id;
    int width = 0;
    int height = 0;
    double fps = 0.;         ///< 0 : non écrit
    double duration = 0.;
    bool durationKnown = false;
    bool hasRoot = false;
    QString variables;       ///< `data-composition-variables`, tel quel
    // Une composition d'un seul élément (lot 5 du document 19) : la racine porte ces attributs propres à
    // KoraCut, ignorés par HyperFrames et par l'empreinte (déplacer le clip ne refait pas le rendu).
    QString elementKind;     ///< `data-koracut-element` : titre, bandeau, compteur, logo, legende, transition… vide sinon
    QString style;           ///< `data-koracut-style` : le style du catalogue qui l'a écrite
    double cut = -1.;        ///< `data-koracut-cut` : la coupe que suit une transition, en secondes (-1 : aucune)
    QString sequence;        ///< `data-koracut-sequence` : la séquence (uuid) où la composition est posée ; vide : la séquence principale
    bool retired = false;    ///< `data-koracut-retire` : son clip a été supprimé de la timeline, elle ne revient pas seule
    /// `data-koracut-fond="opaque"` : la composition couvre toute l'image (un plan plein cadre, pas un
    /// habillage). Son rendu entier se fait en MP4 (H.264) plutôt qu'en ProRes 4444 : trente fois plus
    /// léger sur le disque, deux fois plus rapide, sans transparence (elle n'en a pas besoin).
    bool opaque = false;
    bool isElement() const { return !elementKind.isEmpty(); }
    bool isTransition() const { return elementKind == QLatin1String("transition"); }
    QList<Element> elements; ///< dans l'ordre du fichier
    QStringList errors;      ///< ce qui n'a pas pu être résolu, en clair

    const Element *element(const QString &id) const;
    /** Fin de la composition : la plus grande fin d'élément, ou data-duration de la racine. */
    double end() const;
};

/** @brief Lit toutes les balises du texte. Les contenus de script, style et commentaire sont sautés. */
QList<Tag> tokenize(const QString &html);

/** @brief Analyse une composition : la racine, ses éléments temporels, les débuts relatifs résolus.
 * Une composition sans élément temporel (le cas StockFinity : une seule timeline GSAP) donne un
 * seul élément « composition entière » (Kind::Whole), de la durée de la racine. */
Composition analyse(const QString &html);

/** @brief Le texte visible d'un fragment HTML : balises retirées, entités décodées, blancs repliés. */
QString visibleText(const QString &fragment);

/** @brief Résout un `data-start` : un nombre, ou « id », « id + s », « id - s » (la fin du clip cité).
 * @return faux si l'expression est inconnue, cite un élément absent, ou boucle. */
bool resolveStart(const QString &expr, const Composition &composition, double &seconds, QStringList *chain = nullptr);

/** @brief Écrit un nombre de secondes comme HyperFrames les lit : jusqu'à 3 décimales, sans zéro inutile. */
QString secondsText(double seconds);

/** @brief Remplace la valeur d'un attribut de la balise (ou l'ajoute avant le « > ») dans @p html.
 * Modification minimale : le reste du fichier ne bouge pas d'un caractère. Une valeur vide retire
 * l'attribut. */
QString setAttribute(const QString &html, const Tag &tag, const QString &name, const QString &value);

/** @brief Remplace le texte visible d'un bloc : le premier nœud de texte non vide prend @p text, les
 * autres se vident ; sans nœud de texte, le texte est posé au début du bloc. Les balises restent. */
QString setText(const QString &html, const Element &element, const QString &text);

/** @brief Encode ce qui casserait le HTML dans un texte (« & », « < », « > »). */
QString escapeText(const QString &text);

/** @brief Retire un élément du texte (sa balise, son contenu, la ligne qu'il occupait). @p offset reçoit
 * la place où il était, pour le remettre avec insertFragment. Une composition entière ne se retire pas. */
QString removeElement(const QString &html, const Element &element, int *offset = nullptr);

/** @brief Le HTML sans les attributs `data-koracut-*` de la racine : ce que l'empreinte d'une composition
 * entière regarde (sa place dans la timeline n'est pas son image). */
QString withoutKoracutRootAttributes(const QString &html);

/** @brief Pose un fragment de HTML à cette place du texte (bornée au fichier). */
QString insertFragment(const QString &html, int offset, const QString &fragment);

/** @brief Empreinte d'un élément, sans sa place dans le temps ni sa piste (ni son point d'entrée
 * `data-koracut-in`). */
QString fingerprint(const QString &html, const Element &element, const QString &pageSignature);

/** @brief Ce qui, hors des éléments, change leur image : styles, scripts, feuilles liées. */
QString pageSignature(const QString &html);

} // namespace KoraCutMotion

/** @brief Le synchroniseur d'un projet Motion : tient l'index.html, le surveille, écrit en retour.
 *
 * Les écritures de KoraCut lui-même ne repassent pas par la surveillance : on retient l'empreinte du
 * contenu qu'on vient d'écrire, et une notification qui rend la même empreinte est ignorée. Une
 * écriture extérieure (agent, Studio, panneau Code) arrive avec 300 ms de délai pour grouper les
 * enregistrements successifs ; tant qu'elle n'a pas été relue, le synchroniseur refuse d'écrire.
 */
class KoraCutMotionSync : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutMotionSync(const QString &indexPath, QObject *parent = nullptr);
    ~KoraCutMotionSync() override;

    QString indexPath() const { return m_path; }
    QString directory() const;
    const KoraCutMotion::Composition &composition() const { return m_composition; }
    const QString &html() const { return m_html; }

    /** @brief Relit le fichier. @return vrai si le contenu a changé depuis la dernière lecture. */
    bool reload();
    /** @brief Vrai si le fichier sur le disque n'est pas celui qu'on a lu en dernier. */
    bool changedOnDisk() const;

    /** @brief Écrit des attributs et, si @p text n'est pas nul, le texte d'un élément. Une seule
     * écriture du fichier pour tout. @return faux si l'élément n'existe pas, si une écriture
     * extérieure attend d'être relue, ou si le fichier ne s'écrit pas. */
    bool writeElement(const QString &id, const QMap<QString, QString> &attributes, const QString *text = nullptr);
    /** @brief Remplace tout le contenu (panneau Code). Le changement est traité comme extérieur. */
    bool writeAll(const QString &html);
    /** @brief Remplace tout le contenu sans rien signaler : l'appelant applique lui-même (les outils d'agents,
     * qui tiennent leur propre étape d'annulation). */
    bool writeAllSilently(const QString &html);

    /** @brief Pour les essais : le délai de regroupement, en ms (300 par défaut). */
    void setDebounce(int ms);

Q_SIGNALS:
    /** @brief Le fichier a changé de l'extérieur et a été relu : la composition est à jour. */
    void changedOutside();

private:
    void scheduleCheck();
    void checkNow();
    void rewatch();
    bool writeFile(const QString &content);
    static QByteArray digest(const QString &content);

    QString m_path;
    QString m_html;
    QByteArray m_digest;
    KoraCutMotion::Composition m_composition;
    QFileSystemWatcher *m_watcher;
    QTimer *m_debounce;
};
