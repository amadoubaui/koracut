/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDialog>
#include <QString>

class MainWindow;
class QAbstractButton;
class QLabel;
class QPushButton;

/** @brief L'accueil de premier lancement.
 *
 * Demande d'Amadou Ba du 4 octobre 2026 : une vraie personne a installé la version publique sur un
 * autre Mac et a vu, avant même l'application, la fenêtre « Quick Setup » héritée de Kdenlive
 * (résolution, fréquence, entrelacé, nombre de pistes). Elle est supprimée. À sa place, une fois la
 * fenêtre principale ouverte :
 *  1. un mot de bienvenue ;
 *  2. trois interrupteurs (apparence, projets récents à l'ouverture, terminal des agents) et le
 *     sens des vidéos en deux grandes cartes, Vertical ou Horizontal ;
 *  3. la fenêtre Nouveau projet par type de vidéo (document 16), ou « Ouvrir un projet… », ou
 *     « Commencer sans rien choisir ».
 * Rien sur la résolution, la cadence ou les pistes : KoraCut prend ses défauts (dix pistes et dix
 * pistes, 1080 lignes, 25 i/s ou la cadence du pays).
 *
 * Entrée valide (le bouton principal), Échap passe. La fenêtre ne bloque jamais la porte de
 * commande : dès qu'un client s'y connecte, elle se retire sans rien changer.
 */
namespace KoraCutFirstRun {

enum class Outcome {
    Empty,       ///< « Commencer sans rien choisir » ou Échap : le projet vide, dans le sens choisi s'il y en a un
    ChooseType,  ///< « Choisir un type de vidéo » : la fenêtre Nouveau projet
    OpenProject, ///< « Ouvrir un projet… »
    Dismissed,   ///< retirée par la porte de commande : rien n'est changé
};

struct Choice
{
    QString format; ///< « vertical », « horizontal », ou vide si aucune carte n'est choisie
    bool showRecents{true};
    bool terminal{false};
    Outcome outcome{Outcome::Empty};
};

/** @brief À appeler quand la fenêtre principale est prête. Ne fait rien hors premier lancement,
 *  ni avec `--no-welcome`, `showWelcome=false`, un fichier à ouvrir ou une capture `KORACUT_SHOT`.
 *
 * Essais sans souris : `KORACUT_ACCUEIL=vertical,sombre,sans-recents,terminal,type` joue les choix
 * (dernier mot : `type`, `vide`, `ouvrir`, `echap` ou `rester`), `KORACUT_SHOT_SPLASH=<png>` écrit
 * `<png>-accueil.png`, `-accueil-choix.png` et `-nouveau.png`. Lignes `KORACUT-ACCUEIL` au journal. */
void schedule(MainWindow *window);

} // namespace KoraCutFirstRun

class KoraCutFirstRunDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KoraCutFirstRunDialog(QWidget *parent = nullptr);
    KoraCutFirstRun::Choice choice() const;
    /** @brief Retire la fenêtre sans rien changer (la porte de commande a un client). */
    void dismiss();
    /** @brief Pour les essais : coche une carte ou un interrupteur par son nom interne. */
    bool play(const QString &word);

    void done(int result) override;

protected:
    void changeEvent(QEvent *event) override;

private:
    void finish(KoraCutFirstRun::Outcome outcome);
    void refreshSymbol();
    void refreshButtons();

    QLabel *m_symbol{nullptr};
    QAbstractButton *m_dark{nullptr};
    QAbstractButton *m_recents{nullptr};
    QAbstractButton *m_terminal{nullptr};
    QAbstractButton *m_vertical{nullptr};
    QAbstractButton *m_horizontal{nullptr};
    QPushButton *m_empty{nullptr};
    QPushButton *m_primary{nullptr};
    KoraCutFirstRun::Outcome m_outcome{KoraCutFirstRun::Outcome::Empty};
    bool m_finished{false};
};
