/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;

/** @brief Fenêtre « Soutenir KoraCut » : un mot du fondateur et un bouton qui ouvre la page de don.
 *
 * Demande d'Amadou Ba du 4 octobre 2026. La fenêtre ne s'ouvre que par l'action `koracut_support`
 * (menu Aide, palette de commandes) : jamais au démarrage, jamais après un export, aucun rappel.
 * Elle n'appelle aucun service et n'affiche aucun donateur : elle n'a donc besoin d'aucune clé.
 * Elle n'est pas modale, pour ne bloquer ni la personne ni la porte de commande.
 */
class KoraCutSupportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KoraCutSupportDialog(QWidget *parent = nullptr);

    /** @brief Ouvre la fenêtre, ou ramène devant celle qui est déjà ouverte (une seule à la fois). */
    static KoraCutSupportDialog *open(QWidget *parent);

    /** @brief La page de don et la page « Autres façons d'aider ». */
    static QUrl donationUrl();
    static QUrl otherWaysUrl();

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateSymbol();
    QLabel *m_symbol{nullptr};
};
