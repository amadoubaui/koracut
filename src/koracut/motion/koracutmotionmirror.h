/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutmotionsync.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

class KdenliveDoc;
class KoraCutMotionRenderer;
class QTimer;
class TimelineItemModel;

/** @brief Le miroir entre les compositions HyperFrames d'un projet et les clips de sa timeline.
 *
 * Document 19, § 2 : une composition Motion existe trois fois (le HTML, les clips, l'image rendue) et
 * un geste sur l'une se propage aux deux autres.
 * - HTML → timeline : quand l'index.html change (agent, Studio, panneau Code), on compare l'ancienne
 *   et la nouvelle liste d'éléments par id : ajout, retrait, retime des clips, et un nouveau rendu
 *   seulement pour ce dont l'empreinte a changé. L'ancien rendu reste affiché jusqu'au nouveau.
 * - timeline → HTML : quand un clip Motion bouge ou se rogne, on réécrit `data-start`,
 *   `data-duration` (et `data-media-start` pour un son) dans le fichier, sans nouveau rendu.
 * Chaque geste est une seule étape d'annulation. Les écritures de KoraCut ne repassent pas par la
 * surveillance du fichier (le synchroniseur retient ce qu'il vient d'écrire).
 *
 * Les pistes Motion sont des pistes ordinaires marquées `kdenlive:koracut_motion=<nom>` (et
 * `kdenlive:koracut_motion_track=<data-track-index>`), les clips des clips ordinaires dont le clip du
 * chutier porte `kdenlive:koracut_motion_project`, `_element`, `_hash` et `_in` (propriétés reprises au
 * rechargement du média, liste de ClipController::getPassPropertiesList).
 */
class KoraCutMotionMirror : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutMotionMirror(QObject *parent = nullptr);
    ~KoraCutMotionMirror() override;

    /** Ce que le panneau montre d'un élément. */
    struct ElementState
    {
        QString id;
        QString kind;
        QString name;
        double start = 0.;
        double duration = 0.;
        int trackIndex = 0;
        QString hash;
        int clipId = -1;
        QString binId;
        QString renderPath;
        bool pending = false;
        bool failed = false;
        QString text;
        QString src;
    };
    struct ProjectState
    {
        QString name;
        QString dir;
        QString indexPath;
        int elements = 0;
        int rendered = 0;
        int pending = 0;
        int failed = 0;
        QStringList errors;
        // Lot 5 : une composition d'un seul élément (data-koracut-element) et son clip.
        QString element;   ///< titre, bandeau, compteur, logo, legende, transition… vide pour une composition ordinaire
        QString style;
        QString display;   ///< le nom montré : « Titre · Big Buck Bunny », ou le nom du projet
        int clipId = -1;   ///< le clip de l'élément (composition d'un seul élément), -1 s'il n'est pas posé
        QString binId;
        bool retired = false;
        double cut = -1.;  ///< transition : la coupe suivie, en secondes
    };

    /** Un élément dont le rendu est prêt mais dont le clip n'a pas pu être posé : où il devait aller, pourquoi.
     * Ou (clipId >= 0) un clip déjà posé qui n'a pas pu être déplacé là où le fichier ou motion_poser le
     * demandait : il est resté à stayedTrack / stayedPosition. */
    struct PlacementRefusal
    {
        QString elementId;
        int trackId = -1;      ///< la piste visée, -1 si aucune n'a pu être trouvée ou créée
        int position = 0;      ///< en images
        int length = 0;        ///< en images
        QString reason;        ///< place_occupee, piste_verrouillee, piste_indisponible, pose_refusee, deplacement_refuse, rendu_trop_court, rendu_echoue
        int blockingClip = -1; ///< place_occupee : le premier clip qui tient la place
        int clipId = -1;       ///< un déplacement refusé : le clip de l'élément, resté où il était
        int stayedTrack = -1;
        int stayedPosition = -1;
        bool resize = false;   ///< un allongement refusé (length : la durée voulue) : le clip garde stayedLength
        int stayedLength = -1;
    };

    /** @brief Le projet KoraCut courant et sa timeline. Relit les projets Motion du dossier. */
    void setDocument(KdenliveDoc *doc, const std::shared_ptr<TimelineItemModel> &timeline);
    /** @brief La séquence active a changé. Chaque composition vit dans une séquence (data-koracut-sequence ;
     * sans : la séquence principale du projet) et n'en sort jamais : pose, nouveau rendu, retime, retrait et
     * annulation se font dans SA séquence, active ou non (1er octobre 2026 : quatre publicités dans un
     * projet, des clips de la séquence 1 posés dans la séquence 3 parce qu'elle était devant à la fin du
     * rendu). Une séquence dont l'onglet est fermé attend : ses compositions sont posées à sa réouverture. */
    void setTimeline(const std::shared_ptr<TimelineItemModel> &timeline);
    /** @brief La séquence (uuid sans accolades) où vit cette composition. */
    QString sequenceOf(const QString &project) const;
    /** @brief La timeline de la séquence de cette composition, nul si son onglet est fermé. */
    std::shared_ptr<TimelineItemModel> timelineFor(const QString &project) const;
    /** @brief Applique tout de suite ce que la timeline a changé (clip supprimé, déplacé, annulation) sans
     * attendre le regroupement de 200 ms : les outils lisent un état à jour. */
    void flush();
    /** @brief Range le dossier d'une composition dans `motion/.corbeille/<nom>-<date>/` et l'oublie : son
     * nom redevient libre, rien n'est supprimé. @return le dossier de la corbeille, vide en cas d'échec. */
    QString trashProject(const QString &project);
    void clear();
    /** @brief Relit le dossier motion/ du projet : nouvelles compositions enregistrées, disparues retirées.
     * Le dossier dépend de l'emplacement du fichier projet, qui peut changer après l'ouverture. */
    void rescan();
    KdenliveDoc *document() const { return m_doc; }
    QString motionRoot() const;

    QStringList projects() const;
    ProjectState projectState(const QString &name) const;
    QList<ElementState> elements(const QString &name) const;
    KoraCutMotionSync *sync(const QString &name) const;
    KoraCutMotionRenderer *renderer() const { return m_renderer; }

    /** @brief Crée un projet depuis le gabarit, aux dimensions et i/s du projet KoraCut (ou celles données,
     * quand elles sont > 0), avec @p html à la place du gabarit s'il n'est pas vide. @return son nom. */
    QString createProject(const QString &name, QString *error = nullptr, int width = 0, int height = 0, double fps = 0., const QString &html = QString());
    /** @brief Importe un dossier ou un .html. @return le nom du projet Motion. */
    QString importProject(const QString &source, const QString &name = QString(), QString *error = nullptr);
    /** @brief Relance les rendus manquants (ou tous avec @p force) d'un projet, ou de tous. */
    void renderAll(const QString &name = QString(), bool force = false);
    /** @brief Relance le rendu de ces éléments seulement (ou de tous ceux du projet si @p ids est vide).
     * @return le nombre de rendus demandés (ceux déjà en cache comptent, ils reviennent tout de suite). */
    int renderElements(const QString &name, const QStringList &ids, bool force = false);

    /** @brief Change le texte d'un bloc : écrit le fichier, relance le rendu de cet élément seul. Une étape d'annulation. */
    bool setElementText(const QString &project, const QString &id, const QString &text);
    /** @brief Change un attribut d'un élément (le fichier d'un média, par exemple). Une étape d'annulation. */
    bool setElementAttribute(const QString &project, const QString &id, const QString &attribute, const QString &value);
    /** @brief Déplace l'élément à cette seconde : c'est le clip de la timeline qui bouge, le fichier suit. */
    bool moveElement(const QString &project, const QString &id, double seconds);

    // Les outils d'agents (lot 3 du document 19) : un appel = une étape d'annulation, qui remet aussi le fichier.
    /** @brief Remplace tout l'index.html : clips ajoutés, retirés, retimés, rendus relancés. Une étape. */
    bool replaceHtml(const QString &project, const QString &html, const QString &undoText = QString());
    /** @brief Change des attributs et, si @p text n'est pas nul, le texte d'un élément. Une étape. */
    bool setElement(const QString &project, const QString &id, const QMap<QString, QString> &attributes, const QString *text = nullptr,
                    const QString &undoText = QString());
    /** @brief Pose (ou repose) les clips du projet Motion : ceux dont le rendu est prêt et qui manquent dans
     * la timeline. @p trackId >= 0 : cette piste vidéo devient la première piste Motion du projet (si elle
     * n'en est pas déjà une). @p atSeconds >= 0 : le premier élément (début absolu) est amené à cette
     * seconde, les autres suivent d'autant. Une étape. @return vrai si quelque chose a bougé. */
    bool place(const QString &project, int trackId = -1, double atSeconds = -1., QString *error = nullptr);
    /** @brief Les éléments de cette composition que la dernière mise en accord n'a pas pu poser (place
     * prise sur leur piste, piste verrouillée). Vide quand tout ce qui est rendu est posé. */
    QList<PlacementRefusal> placementRefusals(const QString &project) const;
    /** @brief Après un rendu : laisse au chutier le temps de charger les rendus prêts, jusqu'à ce que chaque
     * élément rendu ait son clip ou son refus (au plus @p ms). @return vrai si plus rien n'est en suspens. */
    bool waitForPlacement(const QString &project, int ms);
    /** @brief Vrai si le clip de cet élément attend un rendu plus long pour prendre la durée du fichier
     * (motion_element avec une durée plus longue que le rendu en place). */
    bool lengthPending(const QString &project, const QString &elementId) const;

    // Lot 5 du document 19 : un élément = une composition, posée comme un seul clip.
    /** @brief Où poser une composition d'un seul élément qui vient d'être créée : la piste voulue (-1 : la
     * piste partagée « Animations » ou « Transitions ») et, pour une transition, les deux plans de la coupe. */
    void setElementPlacement(const QString &project, int trackId, int clipBefore = -1, int clipAfter = -1);
    /** @brief Attend (la boucle d'événements tourne) que l'élément ait son clip. @return son id, -1 sinon. */
    int waitForClip(const QString &project, const QString &elementId, int ms);
    /** @brief La piste partagée où poser un élément qui doit s'accorder à une transition : celle des
     * animations, sous les transitions (-1 si elle n'existe pas encore). */
    int animationsTrack() const;
    /** @brief La transition sous laquelle est posé cet élément seul (son clip croise la sienne, sur une piste
     * plus basse) : quand la coupe bouge, il la suit. Vide sinon. */
    QString followedTransition(const QString &name) const;
    /** @brief Dit au panneau et aux essais qu'un élément vient d'être ouvert (double-clic, motion_ouvrir). */
    void announceOpened(const QString &project, const QString &elementId);
    QString lastOpenedProject() const { return m_lastOpened; }
    /** @brief La composition que le panneau Motion montre (il la déclare à chaque changement). */
    void setPanelProject(const QString &project) { m_panelProject = project; }
    QString panelProject() const { return m_panelProject; }

    /** @brief Vrai si ce clip de timeline (de n'importe quelle séquence ouverte) vient d'un projet Motion. */
    bool isMotionClip(int clipId) const;
    struct ClipInfo
    {
        QString project;
        QString elementId;
    };
    ClipInfo clipInfo(int clipId) const;
    /** @brief Le projet Motion qui contient cet élément (le premier trouvé), vide si aucun. */
    QString projectOfElement(const QString &elementId) const;
    /** @brief Le clip de timeline d'un élément, -1 s'il n'y en a pas. */
    int clipFor(const QString &project, const QString &elementId) const;

    /** @brief Pour les essais : une ligne KORACUT-MOTION par élément. */
    QString stateDump() const;

Q_SIGNALS:
    void projectsChanged();
    void elementsChanged(const QString &project);
    void message(const QString &text);
    /** @brief Un élément a été ouvert : le panneau montre sa composition, et elle seule. */
    void elementOpened(const QString &project, const QString &elementId);

private:
    /** Un élément retiré du HTML parce que son clip a été supprimé : de quoi le remettre si le clip revient. */
    struct RemovedElement
    {
        QString fragment;
        int offset = 0;
    };
    struct Entry
    {
        QString name;
        QString dir;
        QString indexPath;
        KoraCutMotionSync *sync = nullptr;
        QSet<QString> failed;
        QMap<QString, int> progress;
        QSet<QString> placed;                  ///< les éléments qui ont eu un clip dans la timeline
        QMap<QString, PlacementRefusal> refused; ///< les éléments dont la pose a été refusée (place prise…)
        QMap<QString, RemovedElement> removed; ///< retirés du HTML après la suppression de leur clip
        /** Un clip plus court que la durée du fichier, parce que son rendu en place est trop court : il
         * attend le nouveau rendu. Pendant ce temps, le miroir ne réécrit pas l'ancienne durée dans le HTML. */
        struct PendingLength
        {
            int clipId = -1;
            int from = 0; ///< la durée du clip, en images
            int to = 0;   ///< la durée du fichier
            bool longerRender = false; ///< un bloc (sa durée n'entre pas dans l'empreinte) : rendu plus long demandé
        };
        QMap<QString, PendingLength> pendingLength;
        int homeTrack = -1;                    ///< composition d'un seul élément : la piste où poser son clip
        int cutBefore = -1;                    ///< transition : le plan qui finit à la coupe
        int cutAfter = -1;                     ///< transition : le plan qui commence à la coupe
    };
    using Fun = std::function<bool(void)>;

    void registerProject(const QString &name, const QString &dir, const QString &indexPath);
    /** @brief Met la timeline en accord avec le fichier. Avec @p outUndo et @p outRedo, rien n'est inscrit
     * dans l'historique : l'appelant reçoit les fonctions et inscrit lui-même. @return vrai si un clip ou
     * une piste a changé. */
    bool applyFromFile(const QString &name, bool logUndo, const QString &undoText = QString(), Fun *outUndo = nullptr, Fun *outRedo = nullptr);
    /** @brief Les clips Motion supprimés à la main : leur élément quitte le HTML ; ceux qui reviennent (Annuler)
     * ramènent leur élément. @return les projets réécrits. */
    QStringList reconcileRemovals(const std::shared_ptr<TimelineItemModel> &tl, const QString &sequence);
    static int entryPoint(const KoraCutMotion::Element &element, double fps);
    void requestRender(Entry &entry, const KoraCutMotion::Element &element, int lengthFrames = 0);
    void onRendered(const QString &project, const QString &elementId, const QString &hash, const QString &path);
    void onRenderFailed(const QString &project, const QString &elementId, const QString &error);
    void scheduleReconcile(const QString &sequence);
    void reconcileTimeline();
    /** @brief Timeline → HTML pour une séquence : retraits, transitions, places et durées. */
    void reconcileSequence(const QString &sequence);
    std::shared_ptr<TimelineItemModel> timeline() const;
    /** @brief La séquence d'une composition : celle écrite, ou la principale. */
    QString resolved(const QString &sequence) const;
    /** @brief La timeline d'une séquence ouverte (active ou non), nul sinon. */
    std::shared_ptr<TimelineItemModel> timelineOf(const QString &sequence) const;
    /** @brief Écoute cette timeline : un clip Motion qui y bouge (à la main, par une annulation) est suivi. */
    void watchModel(const std::shared_ptr<TimelineItemModel> &timeline);
    static ClipInfo clipInfoIn(const std::shared_ptr<TimelineItemModel> &timeline, int clipId);
    /** @brief Une composition neuve sans séquence écrite prend la séquence active (si ce n'est pas la principale). */
    void stampSequence(const QString &indexPath) const;
    double fps() const;
    int ensureTrack(const Entry &entry, int trackIndex, bool audio, Fun &undo, Fun &redo);
    QString ensureFolder(Fun &undo, Fun &redo);
    QString findBinClip(const QString &project, const QString &elementId) const;
    /** @brief La vignette du clip du chutier au milieu du rendu (un rendu transparent est vide à sa première image). */
    void scheduleThumbnail(const QString &binId, int attempt = 0);
    /** @brief Le fichier de rendu d'un élément. @p lengthFrames > 0 : la variante d'un bloc rendu plus long
     * que son premier rendu (même empreinte, autre durée). */
    QString renderOutput(const Entry &entry, const KoraCutMotion::Element &element, int lengthFrames = 0) const;
    /** @brief Le clip de @p cid ne peut pas encore prendre @p length images : place libre, il attend un rendu
     * plus long ; place prise ou piste verrouillée, le refus est gardé. */
    void lengthenLater(Entry &entry, const std::shared_ptr<TimelineItemModel> &tl, const KoraCutMotion::Element &element, int cid, int track, int position,
                       int length);
    /** @brief Après l'arrivée du rendu : redonne au clip la durée du fichier, quelques fois tant que le chutier recharge. */
    void settleLength(const QString &project, const QString &elementId, int attempt);
    /** @brief L'allongement attendu n'aura pas lieu (@p reason : rendu_trop_court, rendu_echoue) : gardé comme un refus. */
    void abandonLength(const QString &project, const QString &elementId, const QString &reason);
    QString envelopeFor(const Entry &entry, const KoraCutMotion::Element &element) const;
    bool insertClip(const Entry &entry, const KoraCutMotion::Element &element, const QString &binId, int in, int length, Fun &undo, Fun &redo);
    /** @brief La piste d'une composition d'un seul élément, libre de @p position sur @p length images : sa
     * piste à elle si elle est libre, sinon une piste partagée « Animations » (ou « Transitions », tout en
     * haut), créée au besoin dans undo/redo. */
    int elementTrack(const QString &project, int position, int length, Fun &undo, Fun &redo);
    /** @brief Les transitions suivent leur coupe : une coupe déplacée les recentre, une transition déplacée à la
     * main s'accroche à la coupe la plus proche de son milieu (ou se détache). @return les projets réécrits. */
    QStringList retimeTransitions(const std::shared_ptr<TimelineItemModel> &tl, const QString &sequence);
    /** @brief Les clips des éléments seuls posés sous la transition @p transitionClip. */
    QList<int> followersOf(const std::shared_ptr<TimelineItemModel> &tl, const QString &transition, int transitionClip) const;
    /** @brief Une coupe (un plan qui finit où le suivant commence, sur une piste vidéo sous @p aboveTrack) à
     * moins de @p radius images de @p center. */
    bool findCutNear(const std::shared_ptr<TimelineItemModel> &tl, int aboveTrack, int center, int radius, int &before, int &after, int &cut) const;
    QString trackName(const Entry &entry, int trackIndex, bool audio) const;
    static int frames(double seconds, double fps);
    static void merge(Fun &undo, Fun &redo, const Fun &localUndo, const Fun &localRedo);

    KdenliveDoc *m_doc{nullptr};
    std::weak_ptr<TimelineItemModel> m_timeline;
    QMap<QString, Entry> m_entries;
    KoraCutMotionRenderer *m_renderer;
    QTimer *m_reconcile;
    QTimer *m_rescan;
    bool m_applying{false};
    QString m_lastOpened;
    QString m_panelProject;
    QList<QMetaObject::Connection> m_timelineConnections;
    QMap<QString, QPointer<TimelineItemModel>> m_watched; ///< les timelines écoutées, par séquence
    QSet<QString> m_dirty;  ///< les séquences dont la timeline a bougé depuis la dernière réconciliation
    QString m_sequence;     ///< uuid (sans accolades) de la séquence active
    QString m_mainSequence; ///< la séquence des compositions sans data-koracut-sequence
};
