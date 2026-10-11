/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// Le cœur du module Étalonnage (document 22, lot 2) : quel plan on étalonne, son graphe de nœuds
// (le filtre koracut_grade de sa pile, ou celui de la séquence en mode « Timeline »), le nœud
// courant, les stills de la galerie. Chaque geste de l'interface ou d'un agent passe par ici :
// une étape d'annulation par geste, jamais une par mouvement de souris.

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

class EffectItemModel;
class EffectStackModel;
class TimelineItemModel;
class TimelineController;

class KoraCutGradeController : public QObject
{
    Q_OBJECT
public:
    enum class Scope { Clip, Timeline };

    static KoraCutGradeController *self();

    std::shared_ptr<TimelineItemModel> timeline() const;
    /** @brief Les plans vidéo de la séquence, dans l'ordre de la bande de plans (par position, puis
     * de la piste du haut à celle du bas). */
    QList<int> videoClips() const;
    int currentClip() const { return m_clip; }
    void setCurrentClip(int cid, bool seek = false);
    Scope scope() const { return m_scope; }
    void setScope(Scope scope);

    /** @brief Le graphe du plan courant (ou de la séquence), le graphe neuf s'il n'y en a pas. */
    QJsonObject graph() const;
    QJsonObject graphFor(int cid) const;
    bool hasGrade(int cid) const;
    int currentNode() const { return m_node; }
    void setCurrentNode(int id);

    /** @brief Remplace le graphe courant. final = false pendant un glisser (aperçu seulement, sans
     * étape d'annulation) ; final = true referme le geste en une étape « Étalonnage : <geste> ». */
    void edit(const QJsonObject &graph, const QString &gesture, bool final = true);
    /** @brief Pose un graphe sur un plan quelconque (-1 : la séquence), en une étape d'annulation. */
    bool setGraphFor(int cid, const QJsonObject &graph, const QString &gesture);
    /** @brief Pose plusieurs graphes en une seule étape d'annulation (outils des agents, copie de grade). */
    bool setGraphsFor(const QList<QPair<int, QJsonObject>> &graphs, const QString &gesture);
    /** @brief Pendant un outil d'agent : la séquence sur laquelle il travaille (nullptr : la séquence active). */
    void setTimelineOverride(const std::shared_ptr<TimelineItemModel> &timeline) { m_override = timeline; }
    /** @brief Copie le grade d'un plan vers d'autres (clic milieu de Resolve). append : ajoute les nœuds
     * à la suite au lieu de remplacer. */
    int copyGrade(int from, const QList<int> &to, bool append = false);
    /** @brief L'étalonnage par défaut de KoraCut (HDR, Vif, Peau) sur un plan, ou sur tous les plans de la
     * séquence avec cid = -1 : le même chemin que l'outil etal_signature, une étape d'annulation. */
    bool applySignature(int cid);
    /** @brief Remet le grade à zéro ; keepNodes garde la structure (Shift-Command-Home). */
    void resetGrade(bool keepNodes);
    /** @brief Coupe ou rétablit tout l'étalonnage du plan (Shift-D de Resolve). */
    void toggleBypass();
    bool bypassed() const;
    /** @brief Avant / après dans le moniteur (split), sans rien enregistrer dans le projet. */
    void setCompare(bool split, double position = 0.5);
    bool comparing() const { return m_compare; }

    /** @brief Une image du plan (sa vignette), étalonnée ou non, à la largeur voulue. */
    QImage clipImage(int cid, int width, bool graded, const QJsonObject &graph = QJsonObject(), int upToNode = -1) const;

    struct Still
    {
        QString id;
        QString label;
        QJsonObject graph;
        QImage image;
    };
    QList<Still> stills() const;
    /** @brief Prend un still du plan courant (Option-Command-G) : image et grade, enregistrés dans le projet. */
    void grabStill();
    void applyStill(int index, bool append);
    void removeStill(int index);

    /** @brief Les dossiers où chercher les LUT (livrées avec KoraCut, puis celles du projet). */
    static QStringList lutFolders();

Q_SIGNALS:
    void clipChanged(int cid);
    void graphChanged();
    void nodeChanged(int id);
    void clipsChanged();
    void stillsChanged();

private:
    explicit KoraCutGradeController(QObject *parent);
    void attach();
    void followSelection();
    void followPosition(int position);
    std::shared_ptr<EffectStackModel> stack(int cid) const;
    std::shared_ptr<EffectItemModel> gradeEffect(const std::shared_ptr<EffectStackModel> &stack) const;
    std::shared_ptr<EffectItemModel> ensureGradeEffect(int cid, const QString &json, bool *created);
    void applyCompare();
    int targetId() const; // le plan courant, ou -1 pour la séquence

    int m_clip = -1;
    int m_node = 1;
    Scope m_scope = Scope::Clip;
    bool m_compare = false;
    double m_comparePos = 0.5;
    QString m_gestureStart; // graphe au début du geste en cours (glisser)
    int m_gestureTarget = -2;
    QPointer<TimelineController> m_attached;
    mutable QHash<QString, QImage> m_thumbCache;
    std::function<bool()> m_createUndo, m_createRedo; // pose de l'effet pendant le geste en cours
    std::shared_ptr<TimelineItemModel> m_override;
    bool applyGraph(int cid, const QString &json, std::function<bool()> &undo, std::function<bool()> &redo);
};
