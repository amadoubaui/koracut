/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutmotioninspector.h"
#include "koracutmotionmirror.h"
#include "koracutmotionrenderer.h"

#include "core.h"
#include "koracut/koracutmetrics.h"

#include <KLocalizedString>

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
/** Le champ texte : Entrée applique, Maj+Entrée saute une ligne. */
class TextField : public QPlainTextEdit
{
public:
    explicit TextField(QWidget *parent, std::function<void()> apply)
        : QPlainTextEdit(parent)
        , m_apply(std::move(apply))
    {
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !(event->modifiers() & Qt::ShiftModifier)) {
            m_apply();
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

private:
    std::function<void()> m_apply;
};
} // namespace

KoraCutMotionInspector::KoraCutMotionInspector(KoraCutMotionMirror *mirror, KoraCutMotionStudio *studio, QWidget *parent)
    : QWidget(parent)
    , m_mirror(mirror)
    , m_studio(studio)
{
    setObjectName(QStringLiteral("koracutMotionInspector"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup);
    lay->setSpacing(KoraCutMetrics::SpaceTight);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("koracutMotionInspectorTitle"));
    lay->addWidget(m_title);

    m_textLabel = new QLabel(i18nc("@label", "Text"), this);
    m_textLabel->setObjectName(QStringLiteral("koracutMotionLabel"));
    lay->addWidget(m_textLabel);
    m_text = new TextField(this, [this]() { applyText(); });
    m_text->setObjectName(QStringLiteral("koracutMotionText"));
    m_text->setMaximumHeight(72);
    m_text->setPlaceholderText(i18nc("@info:placeholder", "Enter applies, Shift+Enter adds a line"));
    lay->addWidget(m_text);

    m_fileLabel = new QLabel(i18nc("@label", "File"), this);
    m_fileLabel->setObjectName(QStringLiteral("koracutMotionLabel"));
    lay->addWidget(m_fileLabel);
    auto *fileRow = new QHBoxLayout();
    fileRow->setSpacing(KoraCutMetrics::SpaceTight);
    m_file = new QLineEdit(this);
    connect(m_file, &QLineEdit::editingFinished, this, &KoraCutMotionInspector::applyFile);
    m_browse = new QPushButton(i18nc("@action:button", "Choose…"), this);
    m_browse->setObjectName(QStringLiteral("koracutMotionButton"));
    connect(m_browse, &QPushButton::clicked, this, [this]() {
        const QString dir = m_mirror->projectState(m_project).dir;
        const QString chosen = QFileDialog::getOpenFileName(this, i18nc("@title:window", "Media file"), dir);
        if (chosen.isEmpty()) {
            return;
        }
        // Un fichier hors du projet Motion est copié dans assets/ : la composition reste portable.
        QString relative = QDir(dir).relativeFilePath(chosen);
        if (relative.startsWith(QLatin1String("..")) || QFileInfo(relative).isAbsolute()) {
            QDir().mkpath(dir + QStringLiteral("/assets"));
            const QString target = dir + QStringLiteral("/assets/") + QFileInfo(chosen).fileName();
            QFile::remove(target);
            QFile::copy(chosen, target);
            relative = QStringLiteral("assets/") + QFileInfo(chosen).fileName();
        }
        m_file->setText(relative);
        applyFile();
    });
    fileRow->addWidget(m_file, 1);
    fileRow->addWidget(m_browse);
    lay->addLayout(fileRow);

    m_variablesLabel = new QLabel(i18nc("@label", "Composition variables"), this);
    m_variablesLabel->setObjectName(QStringLiteral("koracutMotionLabel"));
    lay->addWidget(m_variablesLabel);
    m_variables = new QLabel(this);
    m_variables->setObjectName(QStringLiteral("koracutMotionVariables"));
    m_variables->setWordWrap(true);
    m_variables->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_variables);

    m_studioButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), i18nc("@action:button", "Edit the animation in Studio"), this);
    m_studioButton->setObjectName(QStringLiteral("koracutMotionButton"));
    m_studioButton->setCursor(Qt::PointingHandCursor);
    connect(m_studioButton, &QPushButton::clicked, this, [this]() {
        if (KoraCutMotionHost::state() != KoraCutMotionHost::State::Ready) {
            pCore->displayMessage(i18nc("@info", "HyperFrames is not installed: open the Motion panel."), ErrorMessage, 6000);
            return;
        }
        m_studio->open(m_mirror->projectState(m_project).dir);
    });
    lay->addWidget(m_studioButton, 0, Qt::AlignLeft);
    hide();
}

void KoraCutMotionInspector::clear()
{
    m_clipId = -1;
    m_project.clear();
    m_elementId.clear();
    hide();
}

void KoraCutMotionInspector::showClip(int clipId)
{
    const auto info = m_mirror->clipInfo(clipId);
    if (info.project.isEmpty()) {
        clear();
        return;
    }
    m_clipId = clipId;
    m_project = info.project;
    m_elementId = info.elementId;
    refresh();
    show();
    // Le panneau Motion montre cette composition, et elle seule (Code, Studio) : lot 5 du document 19.
    m_mirror->announceOpened(m_project, m_elementId);
}

void KoraCutMotionInspector::refresh()
{
    KoraCutMotionSync *sync = m_mirror->sync(m_project);
    const KoraCutMotion::Element *e = sync ? sync->composition().element(m_elementId) : nullptr;
    if (!e) {
        clear();
        return;
    }
    const KoraCutMotionMirror::ProjectState state = m_mirror->projectState(m_project);
    if (!state.element.isEmpty()) {
        // Une composition d'un seul élément : son nom lisible, pas son identifiant.
        m_title->setText(i18nc("@title Motion inspector, a single animated element", "Motion · %1", state.display));
    } else {
        m_title->setText(i18nc("@title Motion inspector, composition then element", "Motion · %1 · %2", m_project, e->id));
    }
    const bool block = e->kind == KoraCutMotion::Kind::Block || e->kind == KoraCutMotion::Kind::Composition ||
                       (e->kind == KoraCutMotion::Kind::Whole && e->textTagIndex >= 0);
    m_textLabel->setVisible(block);
    m_text->setVisible(block);
    if (block && m_text->toPlainText() != e->text) {
        m_text->setPlainText(e->text);
    }
    const bool media = e->kind == KoraCutMotion::Kind::Image || e->kind == KoraCutMotion::Kind::Video || e->kind == KoraCutMotion::Kind::Audio;
    m_fileLabel->setVisible(media);
    m_file->setVisible(media);
    m_browse->setVisible(media);
    if (media) {
        m_file->setText(e->src);
    }
    // Les variables déclarées : une ligne par variable, id, type et valeur par défaut.
    QStringList lines;
    const QJsonArray vars = QJsonDocument::fromJson(sync->composition().variables.toUtf8()).array();
    for (const QJsonValue &v : vars) {
        const QJsonObject o = v.toObject();
        lines << QStringLiteral("%1 (%2) = %3")
                     .arg(o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("type")).toString(),
                          o.value(QStringLiteral("default")).toVariant().toString());
    }
    m_variablesLabel->setVisible(!lines.isEmpty());
    m_variables->setVisible(!lines.isEmpty());
    m_variables->setText(lines.join(QLatin1Char('\n')));
}

void KoraCutMotionInspector::focusText()
{
    if (m_text->isVisible()) {
        m_text->setFocus();
        m_text->selectAll();
    }
}

void KoraCutMotionInspector::applyText()
{
    if (m_project.isEmpty()) {
        return;
    }
    if (!m_mirror->setElementText(m_project, m_elementId, m_text->toPlainText().trimmed())) {
        pCore->displayMessage(i18nc("@info", "The text could not be written: the file just changed, try again."), ErrorMessage, 5000);
    }
}

void KoraCutMotionInspector::applyFile()
{
    if (m_project.isEmpty() || !m_file->isVisible()) {
        return;
    }
    KoraCutMotionSync *sync = m_mirror->sync(m_project);
    const KoraCutMotion::Element *e = sync ? sync->composition().element(m_elementId) : nullptr;
    if (!e || e->src == m_file->text()) {
        return;
    }
    const QString attribute = e->kind == KoraCutMotion::Kind::Composition ? QStringLiteral("data-composition-src") : QStringLiteral("src");
    m_mirror->setElementAttribute(m_project, m_elementId, attribute, m_file->text());
}
