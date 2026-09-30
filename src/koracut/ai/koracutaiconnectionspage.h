/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutaiconnections.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QAction;

/** @brief La page « Connexions IA » des Préférences.
 *
 * À gauche, les connexions de la personne dans leur ordre de priorité (glisser pour réordonner),
 * puis les fournisseurs du catalogue avec leur logo et un badge « Gratuit pour commencer ». À droite,
 * le fournisseur choisi : Documentation, Créer un compte, Obtenir une clé ; le nom du compte, la clé
 * (masquée), l'adresse de base quand il en faut une ; « Vérifier et enregistrer » interroge vraiment
 * le fournisseur et affiche ses modèles ; puis le choix du modèle. Tout s'enregistre tout de suite,
 * sans attendre le bouton OK des Préférences. Une clé enregistrée ne se redemande jamais.
 */
class KoraCutAiConnectionsPage : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutAiConnectionsPage(QWidget *parent = nullptr);

    /** @brief Montre un fournisseur du catalogue (nouvelle connexion) ou une connexion existante. */
    void selectProvider(const QString &providerId);
    void selectConnection(const QString &connectionId);

    /** @brief Ouvre les Préférences sur cette page ; @p id : un fournisseur ou une connexion à montrer. */
    static void openInPreferences(const QString &id = QString());

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildCatalog();
    void refreshConnections();
    void showProvider(const KoraCutAiProvider &provider, const QString &connectionId);
    void verifyAndSave();
    void onVerified(bool ok, const QStringList &models, const QString &message);
    void saveModel();
    void removeConnection();
    void setStatus(const QString &text, bool error);
    void applyColors();
    QColor ink() const;

    QListWidget *m_connections{nullptr};
    QListWidget *m_catalog{nullptr};
    QLineEdit *m_search{nullptr};
    QLabel *m_logo{nullptr};
    QLabel *m_title{nullptr};
    QLabel *m_badge{nullptr};
    QLabel *m_description{nullptr};
    QPushButton *m_doc{nullptr};
    QPushButton *m_signup{nullptr};
    QPushButton *m_getKey{nullptr};
    QLineEdit *m_account{nullptr};
    QLabel *m_keyLabel{nullptr};
    QLineEdit *m_key{nullptr};
    QAction *m_showKey{nullptr};
    QLabel *m_keyHint{nullptr};
    QLabel *m_baseLabel{nullptr};
    QLineEdit *m_baseUrl{nullptr};
    QLabel *m_baseHint{nullptr};
    QPushButton *m_verify{nullptr};
    QLabel *m_status{nullptr};
    QComboBox *m_model{nullptr};
    QLabel *m_note{nullptr};
    QPushButton *m_remove{nullptr};
    QLabel *m_footer{nullptr};
    QWidget *m_detail{nullptr};
    QWidget *m_empty{nullptr};

    KoraCutAiVerifier *m_verifier{nullptr};
    KoraCutAiProvider m_provider;
    QString m_connectionId;
    bool m_filling{false};
};
