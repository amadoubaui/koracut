/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QCache>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QStringList>

namespace Mlt {
class Profile;
}

/** @brief Les vignettes des effets : chaque effet vidéo rendu sur une image d'exemple.
 *
 * Demande URGENTE d'Amadou Ba du 23 septembre 2026 : la liste des effets n'était que du texte
 * (« Ajustement RVB »…), on ne savait pas ce qu'un effet fait avant de l'essayer. Comme CapCut ou
 * DaVinci, chaque effet a maintenant une vignette qui montre son résultat.
 *
 * Fonctionnement : la vue demande une vignette (preview()). Si elle est en cache (mémoire, puis
 * disque), elle revient tout de suite. Sinon une vignette d'attente est rendue et l'effet part dans
 * une file : le filtre MLT est créé avec ses valeurs par défaut (par AssetParameterModel, comme
 * quand on pose l'effet sur un clip), appliqué à l'image d'exemple, et l'image est calculée dans un
 * fil à part, un effet à la fois. Quand elle est prête, previewReady() est émis et la vignette est
 * gardée sur le disque (cache de l'application, dossier « apercus-effets ») : chaque effet n'est
 * calculé qu'une fois par machine.
 *
 * Les effets audio n'ont pas d'image : ils reçoivent une vignette dessinée (une onde).
 */
class KoraCutEffectPreviews : public QObject
{
    Q_OBJECT
public:
    static KoraCutEffectPreviews *instance();

    /** @brief Taille des vignettes (16:9). */
    static QSize size();

    /** @brief La vignette de l'effet, ou une vignette d'attente si elle n'est pas encore calculée. */
    QPixmap preview(const QString &effectId, bool audio);

    /** @brief Vrai quand la vignette de cet effet est prête (en mémoire ou sur le disque). */
    bool isReady(const QString &effectId);

    /** @brief Calcule tout de suite la vignette, dans le fil appelant (pour les tests). */
    QImage renderNow(const QString &effectId);

    /** @brief Vrai si la vignette livrée de cet effet ne montre aucun changement visible (effets
     * techniques : désentrelacement, détection…). Liste écrite par le script de préparation. */
    static bool isNeutral(const QString &effectId);

    /** @brief L'image d'exemple, telle qu'elle est posée sous les effets. */
    static QImage sampleImage();

Q_SIGNALS:
    void previewReady(const QString &effectId);

private:
    explicit KoraCutEffectPreviews(QObject *parent = nullptr);
    ~KoraCutEffectPreviews() override;
    QString cacheFolder() const;
    void ensureProfile();
    QString samplePath();
    void next();
    QPixmap placeholder(const QString &effectId, bool audio) const;

    /** Vignettes décodées, bornées à 32 Mo (27 septembre 2026) : avec de vraies photos plus grandes,
     *  garder les 380 vignettes en mémoire coûterait 70 Mo ; le coût est en Ko. */
    void keep(const QString &effectId, const QPixmap &pix);
    QCache<QString, QPixmap> m_ready{32 * 1024};
    QStringList m_queue;
    QSet<QString> m_failed;
    bool m_busy{false};
    Mlt::Profile *m_profile{nullptr};
};
