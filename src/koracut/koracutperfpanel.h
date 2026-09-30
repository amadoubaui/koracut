/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;

/** @brief Le panneau Diagnostic (chantier 10, document 6, section 3h).
 *
 * Ce que la lecture fait vraiment sur cette machine, mis à jour deux fois par seconde tant que le
 * panneau est visible (rien ne tourne quand il est fermé) : i/s affichées, images sautées, temps
 * entre deux images, résolution d'aperçu (et si elle a été baissée), effets en pause, décodeur,
 * proxys, fils, cache, mémoire. « Tester cette machine » essaie le décodage, lit dix secondes de la
 * séquence et donne un verdict en clair, avec le profil conseillé. Couleurs : jetons du thème. */
class KoraCutPerfPanel : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutPerfPanel(QWidget *parent = nullptr);
    /** @brief Les mesures en ce moment (aussi pour la porte et les agents). */
    static QJsonObject snapshot();
    /** @brief Lance l'essai (sans souris : pseudo-action diagnostic:tester). */
    void runTest();

Q_SIGNALS:
    /** @brief L'essai est fini : { verdict, ips, images_sautees, profil_conseille… }. */
    void testFinished(const QJsonObject &result);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refresh();
    QLabel *addValue(class KoraCutPrefsCard *card, const QString &label, const QString &key, const QString &help = QString());
    QTimer m_timer;
    QLabel *m_profile{nullptr};
    QLabel *m_fps{nullptr};
    QLabel *m_dropped{nullptr};
    QLabel *m_frameTime{nullptr};
    QLabel *m_preview{nullptr};
    QLabel *m_effects{nullptr};
    QLabel *m_decoder{nullptr};
    QLabel *m_proxies{nullptr};
    QLabel *m_threads{nullptr};
    QLabel *m_cache{nullptr};
    QLabel *m_memory{nullptr};
    QLabel *m_verdict{nullptr};
    QPushButton *m_test{nullptr};
    QPushButton *m_apply{nullptr};
    QString m_proposed;
    bool m_testing{false};
};
