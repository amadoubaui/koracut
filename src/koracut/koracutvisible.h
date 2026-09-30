/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

class QLabel;
class QTimer;
class QToolButton;
class QWidget;

/** @brief Le mode « montage visible » (25 septembre 2026, demande d'Amadou Ba) : quand un agent ou un
 *  programme extérieur monte, la personne le voit faire, geste par geste.
 *
 * Avant chaque geste qui change le projet, l'écran montre l'endroit : la tête de lecture y va (la
 * timeline défile d'elle-même), le clip visé est sélectionné, le média visé est sélectionné dans le
 * chutier et son dossier s'ouvre. Une ligne d'état dit ce qui se passe (« Le Monteur · Coupe à
 * 00:01:12:04 »). Puis le geste se fait, l'écran montre le résultat, et la réponse part après une
 * courte pause : l'agent ne peut pas enchaîner plus vite que l'œil.
 *
 * Rien ne bloque l'interface : les pauses sont des minuteurs, jamais un sommeil. Le geste lui-même
 * reste synchrone, donc un appel = une étape d'annulation, comme avant. Sans fenêtre (tests), mode
 * désactivé ou bouton « Accélérer » pressé, tout se fait tout de suite, sans minuteur.
 *
 * Réglage : `koracutrc`, groupe `[KoraCut]`, clé `montageVisible` = normal (350 ms), rapide (120 ms)
 * ou desactive. La porte accepte `visible: false` sur une commande (koracut-ctl --rapide).
 */
class KoraCutVisible : public QObject
{
    Q_OBJECT
public:
    enum class Speed { Off, Fast, Normal };

    static KoraCutVisible *self();

    Speed speed() const { return m_speed; }
    void setSpeed(Speed speed);
    static QString speedKey(Speed speed);
    /** @brief La pause entre deux gestes : 0 sans fenêtre, désactivé ou accéléré. */
    int pauseMs() const;
    /** @brief Vrai si l'écran suit les gestes (une fenêtre, le mode actif). */
    bool active() const;
    /** @brief Les outils qui ne changent rien : ni pause, ni mise en scène. */
    static bool isReadOnly(const QString &tool);

    /** @brief Montrer, attendre, faire, montrer, attendre, répondre. @p gesture exécute l'outil (dans
     *  le tour d'annulation de l'appelant) et rend son résultat ; @p done reçoit ce résultat. Si
     *  @p guard disparaît pendant une pause, ni le geste ni la réponse n'ont lieu. Sans pause, tout
     *  se fait tout de suite, dans l'appel. */
    void run(const QString &who, const QString &tool, const QJsonObject &args, QObject *guard, const std::function<QJsonObject()> &gesture,
             const std::function<void(const QJsonObject &)> &done, bool enabled = true);

    /** @brief Montre l'endroit du geste à venir, et la ligne d'état. */
    void before(const QString &who, const QString &tool, const QJsonObject &args);
    /** @brief Montre le résultat. Rend un délai en plus (balayage du chutier après un lot). */
    int after(const QString &tool, const QJsonObject &args, const QJsonObject &result);

    /** @brief Le petit bloc de la barre d'état : la ligne, « Accélérer », la vitesse. */
    QWidget *createStatusWidget(QWidget *parent);
    QString line() const { return m_line; }
    /** @brief Une ligne d'état pendant un geste long (voix off en cours de génération), sans pause. */
    void progressLine(const QString &who, const QString &text);
    /** @brief « Accélérer » : plus de pause jusqu'à la fin de la série de gestes. */
    void hurry();

    /** @brief Pour les essais sans souris (pseudo-action `visible:etat`). */
    QString debugState() const;

Q_SIGNALS:
    void lineChanged(const QString &text);
    void speedChanged();

private:
    explicit KoraCutVisible(QObject *parent = nullptr);
    void later(int ms, QObject *guard, const std::function<void()> &fn);
    void setLine(const QString &text);
    void touchActivity();
    QString describe(const QString &tool, const QJsonObject &args) const;
    void seek(int position);
    void selectMedia(const QString &binId);
    void revealClip(int clipId, int position);
    void refreshWidget();

    Speed m_speed{Speed::Normal};
    bool m_hurry{false};
    QString m_line;
    QTimer *m_idle{nullptr};
    QPointer<QWidget> m_widget;
    QPointer<QLabel> m_label;
    QPointer<QToolButton> m_hurryButton;
    QPointer<QToolButton> m_speedButton;
    int m_gestures{0};
};
