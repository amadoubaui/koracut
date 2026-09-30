/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QWidget>

class QLabel;

/** @brief Le panneau central d'un module qui n'a pas encore de contenu.
 *
 * Photo, Illustration et Export ne sont pas encore écrits. Plutôt que de charger une disposition
 * vide, on affiche une page sobre qui nomme le module et le chantier où le travail est décrit.
 */
class KoraCutPlaceholder : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutPlaceholder(QWidget *parent = nullptr);

    /** @brief Affiche le module @p moduleId (Photo, Illustration, Export). */
    void setModule(const QString &moduleId);

protected:
    void changeEvent(QEvent *event) override;

private:
    void refreshPalette();

    QLabel *m_symbol{nullptr};
    QLabel *m_title{nullptr};
    QLabel *m_detail{nullptr};
};
