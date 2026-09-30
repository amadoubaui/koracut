/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutmotionpanel.h"
#include "../koracutfonts.h"
#include "koracutmotioncatalog.h"
#include "koracutmotionmirror.h"
#include "koracutmotionrenderer.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "koracut/koracutmetrics.h"

#include <KLocalizedString>

#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

KoraCutMotionPanel::KoraCutMotionPanel(KoraCutMotionMirror *mirror, KoraCutMotionStudio *studio, QWidget *parent)
    : QWidget(parent)
    , m_mirror(mirror)
    , m_studio(studio)
    , m_installer(new KoraCutMotionInstaller(this))
    , m_linter(new KoraCutMotionLinter(this))
{
    setObjectName(QStringLiteral("koracutMotionPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    // Un panneau en onglet impose sa largeur minimale à tout son groupe : on borne.
    setMinimumWidth(240);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup);
    root->setSpacing(KoraCutMetrics::SpaceGroup);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("koracutMotionTabs"));
    m_tabs->setDocumentMode(true);
    root->addWidget(m_tabs, 1);
    buildCompositionsTab();
    buildCodeTab();

    connect(m_mirror, &KoraCutMotionMirror::projectsChanged, this, &KoraCutMotionPanel::refreshProjects);
    connect(m_mirror, &KoraCutMotionMirror::elementsChanged, this, [this](const QString &name) {
        refreshProjects();
        if (name == currentProject()) {
            refreshElements();
        }
        if (name == m_codeProject && !m_editor->isDirty()) {
            loadCode();
        }
    });
    connect(m_mirror, &KoraCutMotionMirror::message, this, [](const QString &text) { pCore->displayMessage(text, ErrorMessage, 6000); });
    // Un élément ouvert (double-clic dans la timeline, motion_ouvrir) : sa composition, et elle seule, devient
    // celle du panneau ; l'onglet Code la montre s'il est ouvert, « Ouvrir dans Studio » s'ouvre sur son dossier.
    connect(m_mirror, &KoraCutMotionMirror::elementOpened, this, [this](const QString &project, const QString &elementId) {
        selectProject(project);
        if (m_tabs->currentIndex() == 1 && m_codeProject != project) {
            m_codeProject = project;
            loadCode();
        }
        Q_UNUSED(elementId)
    });
    connect(m_mirror->renderer(), &KoraCutMotionRenderer::logChanged, this, &KoraCutMotionPanel::refreshLog);
    connect(m_mirror->renderer(), &KoraCutMotionRenderer::pendingChanged, this, &KoraCutMotionPanel::refreshProjects);
    connect(m_installer, &KoraCutMotionInstaller::progress, this, [this](const QString &line) {
        m_log->appendPlainText(line);
        m_hostState->setText(i18nc("@info", "Installing HyperFrames…"));
    });
    connect(m_installer, &KoraCutMotionInstaller::finished, this, [this](bool ok, const QString &message) {
        if (!ok) {
            pCore->displayMessage(i18nc("@info", "HyperFrames could not be installed: %1", message), ErrorMessage, 8000);
        }
        refreshHostState();
    });
    connect(m_studio, &KoraCutMotionStudio::opened, this, [this](const QString &url) {
        m_studioButton->setEnabled(true);
        QDesktopServices::openUrl(QUrl(url));
    });
    connect(m_studio, &KoraCutMotionStudio::failed, this, [this](const QString &message) {
        m_studioButton->setEnabled(true);
        pCore->displayMessage(i18nc("@info", "Studio could not start: %1", message), ErrorMessage, 8000);
    });
    connect(m_linter, &KoraCutMotionLinter::finished, this, &KoraCutMotionPanel::showLint);
    refreshProjects();
    refreshHostState();
}

// ---------------------------------------------------------------------------------------------
// Onglet Compositions

void KoraCutMotionPanel::buildCompositionsTab()
{
    auto *page = new QWidget(m_tabs);
    page->setObjectName(QStringLiteral("koracutMotionPage"));
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, KoraCutMetrics::SpaceGroup, 0, 0);
    lay->setSpacing(KoraCutMetrics::SpaceGroup);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(KoraCutMetrics::SpaceTight);
    m_newButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:button", "New composition"), page);
    m_newButton->setObjectName(QStringLiteral("koracutMotionPrimaryButton"));
    m_newButton->setCursor(Qt::PointingHandCursor);
    connect(m_newButton, &QPushButton::clicked, this, &KoraCutMotionPanel::newComposition);
    m_renderButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action:button", "Render all"), page);
    m_renderButton->setObjectName(QStringLiteral("koracutMotionButton"));
    m_renderButton->setCursor(Qt::PointingHandCursor);
    connect(m_renderButton, &QPushButton::clicked, this, &KoraCutMotionPanel::renderAll);
    m_studioButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), i18nc("@action:button", "Open in Studio"), page);
    m_studioButton->setObjectName(QStringLiteral("koracutMotionButton"));
    m_studioButton->setCursor(Qt::PointingHandCursor);
    m_studioButton->setToolTip(i18nc("@info:tooltip", "The HyperFrames editor in your browser, on the same files: keyframes, layers, code"));
    connect(m_studioButton, &QPushButton::clicked, this, &KoraCutMotionPanel::openStudio);
    buttons->addWidget(m_newButton);
    buttons->addWidget(m_renderButton);
    buttons->addWidget(m_studioButton);
    buttons->addStretch(1);
    lay->addLayout(buttons);

    m_projects = new QTreeWidget(page);
    m_projects->setObjectName(QStringLiteral("koracutMotionProjects"));
    m_projects->setHeaderLabels({i18nc("@title:column", "Composition"), i18nc("@title:column", "Elements"), i18nc("@title:column", "Renders")});
    // Lot 5 : les compositions rangées par élément (éléments animés, transitions, compositions à plusieurs éléments),
    // chacune avec son nom lisible et la vignette de son rendu.
    m_projects->setRootIsDecorated(false);
    m_projects->setIndentation(0);
    m_projects->setUniformRowHeights(false);
    m_projects->header()->setStretchLastSection(false);
    m_projects->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_projects->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_projects->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_projects->setIconSize(QSize(40, 22)); // la vignette du rendu, en 16:9
    connect(m_projects, &QTreeWidget::currentItemChanged, this, [this]() {
        m_mirror->setPanelProject(currentProject());
        refreshElements();
        m_studioButton->setEnabled(!currentProject().isEmpty() && KoraCutMotionHost::state() == KoraCutMotionHost::State::Ready);
    });
    connect(m_projects, &QTreeWidget::itemDoubleClicked, this, [this]() { showCode(currentProject()); });
    lay->addWidget(m_projects, 2);

    m_emptyHint = new QLabel(i18nc("@info empty state of the Motion panel",
                                   "No composition yet. Create one from the template, or import a HyperFrames folder or a single .html file."),
                             page);
    m_emptyHint->setObjectName(QStringLiteral("koracutMotionHint"));
    m_emptyHint->setWordWrap(true);
    lay->addWidget(m_emptyHint);

    m_elements = new QTreeWidget(page);
    m_elements->setObjectName(QStringLiteral("koracutMotionElements"));
    m_elements->setHeaderLabels({i18nc("@title:column", "Element"), i18nc("@title:column", "Kind"), i18nc("@title:column", "Start"),
                                 i18nc("@title:column", "Duration"), i18nc("@title:column", "State")});
    m_elements->setRootIsDecorated(false);
    m_elements->setUniformRowHeights(true);
    m_elements->header()->setStretchLastSection(false);
    m_elements->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 5; ++c) {
        m_elements->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }
    lay->addWidget(m_elements, 2);

    auto *stateRow = new QHBoxLayout();
    stateRow->setSpacing(KoraCutMetrics::SpaceGroup);
    m_hostState = new QLabel(page);
    m_hostState->setObjectName(QStringLiteral("koracutMotionState"));
    m_hostState->setOpenExternalLinks(true);
    m_hostState->setWordWrap(true);
    stateRow->addWidget(m_hostState, 1);
    m_installButton = new QPushButton(i18nc("@action:button", "Install HyperFrames"), page);
    m_installButton->setObjectName(QStringLiteral("koracutMotionPrimaryButton"));
    m_installButton->setCursor(Qt::PointingHandCursor);
    m_installButton->hide();
    connect(m_installButton, &QPushButton::clicked, this, [this]() {
        m_installButton->setEnabled(false);
        m_logToggle->setChecked(true);
        m_log->show();
        m_log->clear();
        m_installer->start();
    });
    stateRow->addWidget(m_installButton);
    // « Mettre à jour HyperFrames » : npm install hyperframes@latest dans le dossier de données (document 19, § 4).
    m_updateButton = new QPushButton(i18nc("@action:button", "Update HyperFrames"), page);
    m_updateButton->setCursor(Qt::PointingHandCursor);
    m_updateButton->setToolTip(i18nc("@info:tooltip", "Installs the latest published version of HyperFrames (npm install hyperframes@latest)."));
    m_updateButton->hide();
    connect(m_updateButton, &QPushButton::clicked, this, [this]() {
        m_updateButton->setEnabled(false);
        m_logToggle->setChecked(true);
        m_log->show();
        m_log->clear();
        m_installer->start(true);
    });
    stateRow->addWidget(m_updateButton);
    lay->addLayout(stateRow);

    m_logToggle = new QToolButton(page);
    m_logToggle->setObjectName(QStringLiteral("koracutMotionLogToggle"));
    m_logToggle->setText(i18nc("@action:button collapsible section", "Last render log"));
    m_logToggle->setCheckable(true);
    m_logToggle->setArrowType(Qt::RightArrow);
    m_logToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_logToggle->setAutoRaise(true);
    m_logToggle->setCursor(Qt::PointingHandCursor);
    lay->addWidget(m_logToggle);
    m_log = new QPlainTextEdit(page);
    m_log->setObjectName(QStringLiteral("koracutMotionLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(140);
    m_log->setFont(KoraCutFonts::code(KoraCutMetrics::FontLabel));
    m_log->hide();
    connect(m_logToggle, &QToolButton::toggled, this, [this](bool on) {
        m_logToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        m_log->setVisible(on);
    });
    lay->addWidget(m_log);

    m_tabs->addTab(page, i18nc("@title:tab", "Compositions"));
}

QString KoraCutMotionPanel::currentProject() const
{
    const QTreeWidgetItem *item = m_projects->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void KoraCutMotionPanel::selectProject(const QString &name)
{
    for (int i = 0; i < m_projects->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_projects->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            if (group->child(j)->data(0, Qt::UserRole).toString() == name) {
                m_projects->setCurrentItem(group->child(j));
                m_projects->scrollToItem(group->child(j));
                return;
            }
        }
    }
}

void KoraCutMotionPanel::refreshProjects()
{
    const QString selected = currentProject();
    m_projects->clear();
    const QStringList names = m_mirror->projects();
    // Trois groupes, dans l'ordre de la timeline : les transitions (en haut), les éléments animés, puis les
    // compositions à plusieurs éléments. Un groupe vide n'est pas montré.
    QTreeWidgetItem *groups[3] = {nullptr, nullptr, nullptr};
    const QString groupNames[3] = {i18nc("@title group of the Motion panel", "Transitions"), i18nc("@title group of the Motion panel", "Animated elements"),
                                   i18nc("@title group of the Motion panel", "Compositions")};
    auto groupFor = [&](int index) {
        if (!groups[index]) {
            groups[index] = new QTreeWidgetItem();
            groups[index]->setText(0, groupNames[index]);
            groups[index]->setFlags(Qt::ItemIsEnabled);
            QFont bold = groups[index]->font(0);
            bold.setBold(true);
            groups[index]->setFont(0, bold);
            groups[index]->setForeground(0, QColor(255, 255, 255, 140));
        }
        return groups[index];
    };
    for (const QString &name : names) {
        const auto state = m_mirror->projectState(name);
        const int groupIndex = state.element == QLatin1String("transition") ? 0 : (state.element.isEmpty() ? 2 : 1);
        auto *item = new QTreeWidgetItem(groupFor(groupIndex));
        item->setText(0, state.display.isEmpty() ? name : state.display);
        QIcon icon = QIcon::fromTheme(QStringLiteral("motion_path_animations"));
        if (!state.binId.isEmpty()) {
            // La vignette du rendu (le clip du chutier), sinon l'icône du module.
            if (auto clip = pCore->projectItemModel()->getClipByBinID(state.binId); clip && clip->statusReady()) {
                const QPixmap thumb = clip->thumbnail(64, 36);
                if (!thumb.isNull()) {
                    icon = QIcon(thumb);
                }
            }
        }
        item->setIcon(0, icon);
        item->setData(0, Qt::UserRole, name);
        item->setText(1, QString::number(state.elements));
        QString renders = QStringLiteral("%1/%2").arg(state.rendered).arg(state.elements);
        if (state.pending > 0) {
            renders = i18nc("@item render state, N in progress", "%1, %2 running", renders, state.pending);
        }
        if (state.failed > 0) {
            renders = i18nc("@item render state, N failed", "%1, %2 failed", renders, state.failed);
        }
        item->setText(2, renders);
        QString tip = state.errors.isEmpty() ? state.indexPath : state.errors.join(QLatin1Char('\n'));
        if (state.retired) {
            tip = i18nc("@info:tooltip", "Removed from the timeline. Its files stay in the project folder.") + QLatin1Char('\n') + tip;
            item->setForeground(0, QColor(255, 255, 255, 97));
        }
        item->setToolTip(0, tip);
        if (!state.errors.isEmpty()) {
            item->setForeground(0, QColor(0xFF, 0xD1, 0x66));
        }
    }
    for (QTreeWidgetItem *group : groups) {
        if (group) {
            m_projects->addTopLevelItem(group);
            group->setExpanded(true);
        }
    }
    m_emptyHint->setVisible(names.isEmpty());
    m_elements->setVisible(!names.isEmpty());
    if (!selected.isEmpty()) {
        selectProject(selected);
    } else if (m_projects->topLevelItemCount() > 0 && m_projects->topLevelItem(0)->childCount() > 0) {
        m_projects->setCurrentItem(m_projects->topLevelItem(0)->child(0));
    }
    m_renderButton->setEnabled(!names.isEmpty() && KoraCutMotionHost::state() == KoraCutMotionHost::State::Ready);
    m_studioButton->setEnabled(!currentProject().isEmpty() && KoraCutMotionHost::state() == KoraCutMotionHost::State::Ready);
    refreshElements();
}

void KoraCutMotionPanel::refreshElements()
{
    m_elements->clear();
    const QString name = currentProject();
    if (name.isEmpty()) {
        return;
    }
    const auto list = m_mirror->elements(name);
    const QString single = m_mirror->projectState(name).element;
    for (const auto &e : list) {
        auto *item = new QTreeWidgetItem(m_elements);
        item->setText(0, e.name);
        item->setToolTip(0, e.id);
        // Une composition d'un seul élément : sa sorte (titre, transition…), pas « entière ».
        item->setText(1, single.isEmpty() ? e.kind : KoraCutMotionCatalog::kindLabel(single));
        item->setText(2, KoraCutMotion::secondsText(e.start) + QStringLiteral(" s"));
        item->setText(3, KoraCutMotion::secondsText(e.duration) + QStringLiteral(" s"));
        QString state;
        if (e.kind == QLatin1String("audio")) {
            state = e.clipId >= 0 ? i18nc("@item element state", "placed") : i18nc("@item element state", "missing file");
        } else if (e.pending) {
            state = i18nc("@item element state", "rendering…");
        } else if (e.failed) {
            state = i18nc("@item element state", "render failed");
            item->setForeground(4, QColor(0xFF, 0x72, 0x62));
        } else if (e.clipId >= 0) {
            state = i18nc("@item element state", "placed");
        } else if (!e.renderPath.isEmpty()) {
            state = i18nc("@item element state", "rendered");
        } else {
            state = i18nc("@item element state", "to render");
        }
        item->setText(4, state);
    }
}

void KoraCutMotionPanel::refreshHostState()
{
    const auto state = KoraCutMotionHost::state();
    m_installButton->setEnabled(true);
    m_updateButton->setEnabled(!m_installer->isRunning());
    m_updateButton->setVisible(state == KoraCutMotionHost::State::Ready);
    switch (state) {
    case KoraCutMotionHost::State::Ready:
        m_hostState->setText(i18nc("@info", "HyperFrames %1 ready (Node %2).", KoraCutMotionHost::installedVersion(),
                                   QFileInfo(KoraCutMotionHost::nodePath()).fileName().isEmpty() ? QString() : KoraCutMotionHost::nodePath()));
        m_installButton->hide();
        break;
    case KoraCutMotionHost::State::NotInstalled:
        m_hostState->setText(i18nc("@info", "HyperFrames is not installed yet (about 150 MB, plus a headless Chrome at the first render)."));
        m_installButton->show();
        break;
    case KoraCutMotionHost::State::NoNode:
        m_hostState->setText(i18nc("@info", "Node.js was not found on this computer. Install it from <a href=\"https://nodejs.org\">nodejs.org</a> (version 22 or later), "
                                            "then come back here."));
        m_installButton->hide();
        break;
    }
    m_renderButton->setEnabled(state == KoraCutMotionHost::State::Ready && m_projects->topLevelItemCount() > 0);
    m_studioButton->setEnabled(state == KoraCutMotionHost::State::Ready && !currentProject().isEmpty());
    m_saveButton->setEnabled(true);
}

void KoraCutMotionPanel::refreshLog()
{
    m_log->setPlainText(m_mirror->renderer()->log());
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void KoraCutMotionPanel::renderAll()
{
    m_mirror->renderAll();
    refreshProjects();
}

void KoraCutMotionPanel::openStudio()
{
    const QString name = currentProject();
    if (name.isEmpty()) {
        return;
    }
    m_studioButton->setEnabled(false);
    m_studio->open(m_mirror->projectState(name).dir);
}

QString KoraCutMotionPanel::importWithoutDialog(const QString &path, const QString &name)
{
    QString error;
    const QString created = m_mirror->importProject(path, name, &error);
    if (created.isEmpty()) {
        qWarning() << "KoraCut: motion, import refusé" << error;
        pCore->displayMessage(error, ErrorMessage, 6000);
    } else {
        selectProject(created);
    }
    return created;
}

void KoraCutMotionPanel::newComposition()
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18nc("@title:window", "New Motion composition"));
    auto *lay = new QVBoxLayout(&dialog);
    lay->setSpacing(KoraCutMetrics::SpaceZone);
    auto *form = new QFormLayout();
    auto *name = new QLineEdit(&dialog);
    name->setPlaceholderText(i18nc("@info:placeholder", "bandeau-nom"));
    form->addRow(i18nc("@label:textbox", "Name"), name);
    lay->addLayout(form);
    auto *fromTemplate = new QRadioButton(i18nc("@option:radio", "Empty template, at the project size and frame rate"), &dialog);
    auto *fromFolder = new QRadioButton(i18nc("@option:radio", "Import a HyperFrames folder (with its index.html)"), &dialog);
    auto *fromFile = new QRadioButton(i18nc("@option:radio", "Import a single .html composition"), &dialog);
    fromTemplate->setChecked(true);
    lay->addWidget(fromTemplate);
    lay->addWidget(fromFolder);
    lay->addWidget(fromFile);
    auto *pathRow = new QHBoxLayout();
    auto *path = new QLineEdit(&dialog);
    path->setPlaceholderText(i18nc("@info:placeholder", "Folder or .html file to import"));
    path->setEnabled(false);
    auto *browse = new QPushButton(i18nc("@action:button", "Choose…"), &dialog);
    browse->setEnabled(false);
    pathRow->addWidget(path, 1);
    pathRow->addWidget(browse);
    lay->addLayout(pathRow);
    auto updateMode = [&]() {
        const bool importing = !fromTemplate->isChecked();
        path->setEnabled(importing);
        browse->setEnabled(importing);
    };
    connect(fromTemplate, &QRadioButton::toggled, &dialog, updateMode);
    connect(fromFolder, &QRadioButton::toggled, &dialog, updateMode);
    connect(fromFile, &QRadioButton::toggled, &dialog, updateMode);
    connect(browse, &QPushButton::clicked, &dialog, [&]() {
        QString chosen;
        if (fromFolder->isChecked()) {
            chosen = QFileDialog::getExistingDirectory(&dialog, i18nc("@title:window", "HyperFrames folder"));
        } else {
            chosen = QFileDialog::getOpenFileName(&dialog, i18nc("@title:window", "HyperFrames composition"), QString(), i18n("HTML files (*.html *.htm)"));
        }
        if (!chosen.isEmpty()) {
            path->setText(chosen);
            if (name->text().isEmpty()) {
                const QFileInfo info(chosen);
                name->setText(info.isDir() ? info.fileName() : info.completeBaseName());
            }
        }
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(i18nc("@action:button", "Create"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    lay->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString error;
    QString created;
    if (fromTemplate->isChecked()) {
        created = m_mirror->createProject(name->text().isEmpty() ? QStringLiteral("composition") : name->text(), &error);
    } else {
        created = m_mirror->importProject(path->text(), name->text(), &error);
    }
    if (created.isEmpty()) {
        pCore->displayMessage(error.isEmpty() ? i18nc("@info", "The composition could not be created.") : error, ErrorMessage, 6000);
        return;
    }
    selectProject(created);
}

// ---------------------------------------------------------------------------------------------
// Onglet Code

void KoraCutMotionPanel::buildCodeTab()
{
    auto *page = new QWidget(m_tabs);
    page->setObjectName(QStringLiteral("koracutMotionPage"));
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, KoraCutMetrics::SpaceGroup, 0, 0);
    lay->setSpacing(KoraCutMetrics::SpaceGroup);

    auto *header = new QHBoxLayout();
    header->setSpacing(KoraCutMetrics::SpaceGroup);
    m_codeFile = new QLabel(i18nc("@info", "Choose a composition in the Compositions tab."), page);
    m_codeFile->setObjectName(QStringLiteral("koracutMotionState"));
    m_codeFile->setTextInteractionFlags(Qt::TextSelectableByMouse);
    header->addWidget(m_codeFile, 1);
    m_saveButton = new QPushButton(i18nc("@action:button", "Save"), page);
    m_saveButton->setObjectName(QStringLiteral("koracutMotionButton"));
    m_saveButton->setToolTip(i18nc("@info:tooltip %1 is the Save shortcut, Cmd+S or Ctrl+S", "Saved on its own 800 ms after the last keystroke, or with %1",
                                   QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
    m_saveButton->setCursor(Qt::PointingHandCursor);
    header->addWidget(m_saveButton);
    lay->addLayout(header);

    m_editor = new KoraCutMotionCodeEditor(page);
    lay->addWidget(m_editor, 3);
    connect(m_saveButton, &QPushButton::clicked, m_editor, &KoraCutMotionCodeEditor::saveNow);
    connect(m_editor, &KoraCutMotionCodeEditor::saveRequested, this, &KoraCutMotionPanel::saveCode);

    m_lintState = new QLabel(page);
    m_lintState->setObjectName(QStringLiteral("koracutMotionState"));
    lay->addWidget(m_lintState);
    m_lint = new QListWidget(page);
    m_lint->setObjectName(QStringLiteral("koracutMotionLint"));
    m_lint->setMaximumHeight(120);
    m_lint->setWordWrap(true);
    connect(m_lint, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int line = item->data(Qt::UserRole).toInt();
        if (line > 0) {
            m_editor->goToLine(line);
        }
    });
    lay->addWidget(m_lint, 1);

    m_tabs->addTab(page, i18nc("@title:tab", "Code"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1 && m_codeProject != currentProject()) {
            m_codeProject = currentProject();
            loadCode();
        }
    });
}

void KoraCutMotionPanel::showCode(const QString &name)
{
    if (!name.isEmpty()) {
        selectProject(name);
    }
    m_codeProject = currentProject();
    loadCode();
    m_tabs->setCurrentIndex(1);
    m_editor->setFocus();
}

void KoraCutMotionPanel::loadCode()
{
    KoraCutMotionSync *sync = m_mirror->sync(m_codeProject);
    if (!sync) {
        m_codeFile->setText(i18nc("@info", "Choose a composition in the Compositions tab."));
        m_editor->setContent(QString());
        m_editor->setReadOnly(true);
        m_lint->clear();
        m_lintState->clear();
        return;
    }
    m_editor->setReadOnly(false);
    m_codeFile->setText(QStringLiteral("motion/%1/index.html").arg(m_codeProject));
    m_editor->setContent(sync->html());
    m_lintState->setText(i18nc("@info", "Checking with hyperframes lint…"));
    m_linter->run(sync->directory());
}

void KoraCutMotionPanel::saveCode(const QString &text)
{
    KoraCutMotionSync *sync = m_mirror->sync(m_codeProject);
    if (!sync) {
        return;
    }
    if (sync->writeAll(text)) {
        m_editor->markSaved();
        m_lintState->setText(i18nc("@info", "Saved. Checking with hyperframes lint…"));
        m_linter->run(sync->directory());
    } else {
        pCore->displayMessage(i18nc("@info", "The file could not be saved."), ErrorMessage, 6000);
    }
}

void KoraCutMotionPanel::showLint(const QList<KoraCutMotionLintMessage> &messages, const QString &error)
{
    m_lint->clear();
    if (!error.isEmpty()) {
        m_lintState->setText(error);
        return;
    }
    int errors = 0;
    int warnings = 0;
    for (const auto &m : messages) {
        if (m.severity == QLatin1String("error")) {
            ++errors;
        } else if (m.severity == QLatin1String("warning")) {
            ++warnings;
        }
        auto *item = new QListWidgetItem(m_lint);
        const QString where = m.line > 0 ? i18nc("@item lint message location", "line %1", m.line) : QString();
        item->setText(QStringLiteral("%1 %2%3 %4").arg(m.severity == QLatin1String("error") ? QStringLiteral("✗") : QStringLiteral("⚠"), m.code,
                                                        where.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(where), m.message));
        item->setData(Qt::UserRole, m.line);
        item->setForeground(m.severity == QLatin1String("error") ? QColor(0xFF, 0x72, 0x62) : QColor(0xFF, 0xD1, 0x66));
    }
    if (messages.isEmpty()) {
        m_lintState->setText(i18nc("@info", "Lint: nothing to report."));
    } else {
        m_lintState->setText(i18nc("@info", "Lint: %1 errors, %2 warnings. Click a message to go to its line.", errors, warnings));
    }
}
