/*
    SPDX-FileCopyrightText: 2017 Nicolas Carion
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "effectlistwidget.hpp"
#include "../model/effectfilter.hpp"
#include "../model/effecttreemodel.hpp"
#include "utils/uiutils.h"
#include "kdenlivesettings.h"
#include "koracut/koracutassetcards.h"
#include "koracut/koracuteffectpreviews.h"

#include <KActionCategory>
#include <KIO/FileCopyJob>
#include <KMessageBox>
#include <KRecentDirs>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QStandardPaths>
#include <QTextEdit>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>
#include <qsplitter.h>
#include <qstackedwidget.h>

EffectListWidget::EffectListWidget(QAction *includeList, QAction *tenBit, QWidget *parent)
    : AssetListWidget(true, includeList, tenBit, parent)
{
    QString effectCategory = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("koracuteffectscategory.rc"));
    m_model = EffectTreeModel::construct(effectCategory, this);
    m_proxyModel = std::make_unique<EffectFilter>(this);
    m_proxyModel->setSourceModel(m_model.get());
    m_proxyModel->setSortRole(EffectTreeModel::NameRole);
    setFilterType(QString());
    m_proxyModel->sort(0, Qt::AscendingOrder);
    m_effectsTree->setModel(m_proxyModel.get());
    m_effectsTree->setColumnHidden(1, true);
    m_effectsTree->setColumnHidden(2, true);
    m_effectsTree->setColumnHidden(3, true);
    m_effectsTree->setColumnHidden(4, true);
    m_effectsTree->setColumnHidden(5, true);
    m_effectsTree->header()->setStretchLastSection(true);
    QItemSelectionModel *sel = m_effectsTree->selectionModel();
    connect(sel, &QItemSelectionModel::currentChanged, this, &AssetListWidget::updateAssetInfo);

    // KoraCut, 23 septembre 2026 (demande URGENTE d'Amadou Ba) : les effets en cartes, avec une
    // vignette qui montre le résultat, ou en liste, avec la même vignette en petit.
    m_effectsTree->setIconSize(QSize(48, 27));
    auto *cards = new KoraCutFlatAssetProxy(this);
    cards->setSourceModel(m_proxyModel.get());
    m_effectsIcon->setModel(cards);
    // Au-dessus des cartes, les catégories en pastilles, qui passent à la ligne quand le panneau est
    // étroit : la liste aplatie les avait perdues.
    m_effectsView->removeWidget(m_effectsIcon);
    auto *cardsPage = new QWidget(this);
    auto *cardsLayout = new QVBoxLayout(cardsPage);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(0);
    cardsLayout->addWidget(new KoraCutCategoryChips(cards, cardsPage));
    cardsLayout->addWidget(m_effectsIcon, 1);
    // Sorti de la pile, le widget garde son état « caché » de page non affichée : on le remontre,
    // sinon la mise en page donne toute la place aux pastilles.
    m_effectsIcon->show();
    m_effectsView->insertWidget(1, cardsPage);
    auto *cardDelegate = new KoraCutAssetCardDelegate(m_effectsIcon);
    m_effectsIcon->setItemDelegate(cardDelegate);
    m_effectsIcon->setGridSize(KoraCutAssetCardDelegate::cardSize() + QSize(6, 6));
    m_effectsIcon->setSpacing(0);
    m_effectsIcon->setMouseTracking(true);
    m_effectsIcon->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_effectsIcon->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &previous) { updateAssetInfo(toTreeIndex(current), toTreeIndex(previous)); });
    // Une vignette calculée en direct (essais) : on repeint.
    connect(KoraCutEffectPreviews::instance(), &KoraCutEffectPreviews::previewReady, this, [this]() {
        m_effectsIcon->viewport()->update();
        m_effectsTree->viewport()->update();
    });
    // Outil de développement : KORACUT_SHOT_EXPAND_EFFECTS déplie toutes les catégories de la liste,
    // pour relire les petites vignettes sur une capture.
    if (qEnvironmentVariableIsSet("KORACUT_SHOT_EXPAND_EFFECTS")) {
        QTimer::singleShot(2000, this, [this]() { m_effectsTree->expandAll(); });
    }
    // KoraCut, 27 septembre 2026 (demande d'Amadou Ba) : − et + dans la barre, ⌘− et ⌘+ au clavier.
    // Cinq crans : la liste, puis des grilles de plus en plus grandes. La bascule cartes / liste
    // reste, et passe de la liste au dernier cran de grille choisi.
    auto *zoom = new KoraCutAssetZoom(QStringLiteral("effetsTaille"), 5, KdenliveSettings::effectViewAsIcon() ? 2 : 0, this);
    zoom->install(m_toolbar, m_toggleView);
    zoom->watch(m_effectsIcon);
    zoom->watch(m_effectsTree);
    zoom->followWidth(m_effectsIcon);
    auto lastGrid = std::make_shared<int>(zoom->level() > 0 ? zoom->level() : 2);
    auto layoutCards = [this, cardDelegate, lastGrid]() {
        // La largeur utile, barre de défilement toujours comptée : la grille ne change pas quand elle
        // apparaît, sinon les deux se relanceraient l'une l'autre.
        const int available = m_effectsIcon->contentsRect().width() - m_effectsIcon->style()->pixelMetric(QStyle::PM_ScrollBarExtent) - 2;
        int thumb = 0;
        const QSize cell = KoraCutAssetCardDelegate::gridFor(*lastGrid, std::max(80, available), &thumb);
        if (cell != m_effectsIcon->gridSize() || thumb != cardDelegate->thumbWidth()) {
            cardDelegate->setThumbWidth(thumb);
            m_effectsIcon->setGridSize(cell);
            m_effectsIcon->doItemsLayout();
            m_effectsIcon->viewport()->update();
        }
    };
    auto applyLevel = [this, lastGrid, layoutCards](int level) {
        if (level > 0) {
            *lastGrid = level;
        }
        const bool cardsView = level > 0;
        if (m_toggleView && m_toggleView->isChecked() != cardsView) {
            m_toggleView->setChecked(cardsView);
        }
        if (isIconView() != cardsView) {
            toggleViewMode(cardsView);
        }
        layoutCards();
    };
    connect(zoom, &KoraCutAssetZoom::levelChanged, this, applyLevel);
    connect(zoom, &KoraCutAssetZoom::widthChanged, this, [this, layoutCards]() { QTimer::singleShot(0, this, layoutCards); });
    if (m_toggleView) {
        connect(m_toggleView, &QAction::triggered, this, [zoom, lastGrid](bool checked) { zoom->setLevel(checked ? *lastGrid : 0); });
    }
    applyLevel(zoom->level());

    if (!KdenliveSettings::showEffectsInfo()) {
        m_viewSplitter->setSizes({50, 0});
    } else {
        const QByteArray restoreData = KdenliveSettings::effectsInfoHeight().toLatin1();
        if (restoreData.isEmpty()) {
            m_viewSplitter->setSizes({50, 20});
            const QByteArray splitterData = m_viewSplitter->saveState();
            KdenliveSettings::setEffectsInfoHeight(QString::fromLatin1(splitterData));
        } else {
            // Use a single-shot timer to restore the state
            QTimer::singleShot(0, this, [this, restoreData]() { m_viewSplitter->restoreState(restoreData); });
        }
    }
    connect(m_viewSplitter, &QSplitter::splitterMoved, this, [this]() {
        const QByteArray splitterData = m_viewSplitter->saveState();
        KdenliveSettings::setEffectsInfoHeight(QString::fromLatin1(splitterData));
    });
}

EffectListWidget::~EffectListWidget() {}

void EffectListWidget::switchSplitter(bool enable)
{
    KdenliveSettings::setShowEffectsInfo(enable);
    if (enable) {
        const QByteArray restoreData = KdenliveSettings::effectsInfoHeight().toLatin1();
        if (restoreData.isEmpty()) {
            m_viewSplitter->setSizes({50, 20});
            const QByteArray saveData = m_viewSplitter->saveState();
            KdenliveSettings::setEffectsInfoHeight(QString::fromLatin1(saveData));
        } else {
            m_viewSplitter->restoreState(restoreData);
        }
    } else {
        m_viewSplitter->setSizes({50, 0});
    }
}

void EffectListWidget::setFilterType(const QString &type)
{
    if (type == QLatin1String("video")) {
        static_cast<EffectFilter *>(m_proxyModel.get())->setFilterType(true, AssetListType::AssetType::Video);
    } else if (type == QLatin1String("audio")) {
        static_cast<EffectFilter *>(m_proxyModel.get())->setFilterType(true, AssetListType::AssetType::Audio);
    } else if (type == QLatin1String("custom")) {
        static_cast<EffectFilter *>(m_proxyModel.get())->setFilterType(true, AssetListType::AssetType::Custom);
    } else if (type == QLatin1String("favorites")) {
        static_cast<EffectFilter *>(m_proxyModel.get())->setFilterType(true, AssetListType::AssetType::Favorites);
    } else {
        static_cast<EffectFilter *>(m_proxyModel.get())->setFilterType(false, AssetListType::AssetType::Preferred);
    }
}

bool EffectListWidget::isAudio(const QString &assetId) const
{
    return EffectsRepository::get()->isAudioEffect(assetId);
}

QString EffectListWidget::getMimeType(const QString &assetId) const
{
    Q_UNUSED(assetId);
    return QStringLiteral("kdenlive/effect");
}

void EffectListWidget::reloadCustomEffectIx(const QModelIndex &index)
{
    static_cast<EffectTreeModel *>(m_model.get())->reloadEffectFromIndex(m_proxyModel->mapToSource(index));
    m_proxyModel->sort(0, Qt::AscendingOrder);
}

void EffectListWidget::reloadCustomEffect(const QString &path)
{
    static_cast<EffectTreeModel *>(m_model.get())->reloadEffect(path);
    m_proxyModel->sort(0, Qt::AscendingOrder);
}

void EffectListWidget::reloadTemplates()
{
    static_cast<EffectTreeModel *>(m_model.get())->reloadTemplates();
    m_proxyModel->sort(0, Qt::AscendingOrder);
}

void EffectListWidget::reloadEffectMenu(QMenu *effectsMenu, KActionCategory *effectActions)
{
    m_model->reloadAssetMenu(effectsMenu, effectActions);
}

void EffectListWidget::editCustomAsset(const QModelIndex &index)
{
    QDialog dialog(this);
    QFormLayout form(&dialog);
    QString currentName = getName(index);
    QString desc = getDescription(index);
    // Strip effect Name
    if (desc.contains(QLatin1Char('('))) {
        desc = desc.section(QLatin1Char('('), 0, -2).simplified();
    }
    auto *effectName = new QLineEdit(currentName, &dialog);
    auto *descriptionBox = new QTextEdit(desc, &dialog);
    form.addRow(i18n("Name : "), effectName);
    form.addRow(i18n("Comments : "), descriptionBox);
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    QObject::connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        QString name = effectName->text();
        QString enteredDescription = descriptionBox->toPlainText();
        if (name.trimmed().isEmpty() && enteredDescription.trimmed().isEmpty()) {
            return;
        }
        m_model->editCustomAsset(name, enteredDescription, m_proxyModel->mapToSource(index));
    }
}

void EffectListWidget::exportCustomEffect(const QModelIndex &index)
{
    const QString assetId = m_model->data(m_proxyModel->mapToSource(index), AssetTreeModel::IdRole).toString();
    if (assetId.isEmpty()) {
        return;
    }

    QString filter = QStringLiteral("%1 (*.xml);;%2 (*)").arg(i18n("KoraCut Effect definitions"), i18n("All Files"));
    QString startFolder = KRecentDirs::dir(QStringLiteral(":KdenliveExportCustomEffect"));
    QUrl source = QUrl::fromLocalFile(EffectsRepository::get()->getCustomPath(assetId));
    startFolder.append(source.fileName());

    QString filename = UiUtils::getSaveFileName(this, i18nc("@title:window", "Export Custom Effect"), startFolder, filter, QStringLiteral(".xml"));
    QUrl target = QUrl::fromLocalFile(filename);

    if (source.isValid() && target.isValid()) {
        KRecentDirs::add(QStringLiteral(":KdenliveExportCustomEffect"), target.adjusted(QUrl::RemoveFilename).toLocalFile());
        KIO::FileCopyJob *copyjob = KIO::file_copy(source, target);
        if (!copyjob->exec()) {
            KMessageBox::error(this, i18n("Unable to write to file %1", target.toLocalFile()));
        }
    }
}

bool EffectListWidget::isMasterOnly(const QString &assetId) const
{
    return static_cast<EffectTreeModel *>(m_model.get())->isMasterOnly(assetId);
}

void EffectListWidget::switchTenBitFilter()
{
    KdenliveSettings::setEffectsFilter(m_filterButton->isChecked());
    m_proxyModel->invalidate();
}
