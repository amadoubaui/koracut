/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutdeliverpage.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutemptystate.h"
#include "koracut/koracutmetrics.h"
#include "koracut/koracutthemes.h"
#include "koracutexportqueue.h"
#include "mainwindow.h"
#include "monitor/monitor.h"

#include <KIO/OpenFileManagerWindowJob>
#include <KLocalizedString>

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <KConfigGroup>
#include <KSharedConfig>

#include <cmath>
#include <functional>

using namespace KoraCutExport;

namespace {

QPointer<KoraCutDeliverPage> s_page;

QColor tok(const char *name, int alpha = -1)
{
    return KoraCutThemes::token(QString::fromLatin1(name), alpha);
}

QString timecode(int frames)
{
    const double fps = pCore ? std::max(1., pCore->getCurrentFps()) : 25.;
    const int seconds = int(frames / fps);
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0')).arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString humanSize(double megabytes)
{
    if (megabytes >= 1000.) {
        return i18nc("@info file size in gigabytes", "%1 GB", QLocale().toString(megabytes / 1000., 'f', 1));
    }
    if (megabytes >= 10.) {
        return i18nc("@info file size in megabytes", "%1 MB", QLocale().toString(qRound(megabytes)));
    }
    return i18nc("@info file size in megabytes", "%1 MB", QLocale().toString(megabytes, 'f', 1));
}

QString humanDuration(int seconds)
{
    if (seconds < 60) {
        return i18nc("@info duration", "%1 s", seconds);
    }
    return i18nc("@info duration in minutes and seconds", "%1 min %2 s", seconds / 60, seconds % 60);
}

bool revealAtEnd()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("exportRevelerFin", false);
}

void reveal(const QString &path)
{
    // Le fichier montré dans son dossier, sur les trois systèmes : le Finder, l'Explorateur
    // (explorer /select) ou le gestionnaire de fichiers du bureau Linux (FileManager1).
    KIO::highlightInFileManager({QUrl::fromLocalFile(path)});
}

QLabel *secondary(const QString &text, QWidget *parent);

// La petite fenêtre « Partager » d'un export fini : un texte prêt à coller, le lien mesuré par
// l'admin de koracut.org, et le fichier à portée de main. Non modale : on la laisse ouverte et on
// revient à la file. C'est le geste qui fait connaître KoraCut, une vidéo à la fois.
QDialog *showShareDialog(const QString &path, QWidget *parent)
{
    auto *d = new QDialog(parent);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setObjectName(QStringLiteral("koracutShareDialog"));
    d->setWindowTitle(i18nc("@title:window", "Share your video"));
    d->setMinimumWidth(460);
    auto *lay = new QVBoxLayout(d);
    lay->setContentsMargins(16, 16, 16, 16);
    lay->setSpacing(10);
    auto *title = new QLabel(i18nc("@title", "Your video is ready"), d);
    QFont tf = title->font();
    tf.setPixelSize(KoraCutMetrics::FontTitle + 3);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);
    lay->addWidget(title);
    lay->addWidget(secondary(i18nc("@info", "If KoraCut helped you, say it where your video goes: a line in the description, a comment, a message to a friend. "
                                            "It is the best help you can give to a free app. The text below is yours to change."),
                             d));
    auto *text = new QPlainTextEdit(shareText(), d);
    text->setMinimumHeight(96);
    text->setMaximumHeight(140);
    lay->addWidget(text);
    auto *fileLabel = secondary(QFileInfo(path).fileName(), d);
    fileLabel->setToolTip(path);
    lay->addWidget(fileLabel);
    auto *row = new QHBoxLayout();
    row->setSpacing(KoraCutMetrics::SpaceTight);
    auto *copyText = new QPushButton(i18nc("@action:button", "Copy the text"), d);
    copyText->setDefault(true);
    QObject::connect(copyText, &QPushButton::clicked, d, [text, copyText]() {
        QGuiApplication::clipboard()->setText(text->toPlainText());
        copyText->setText(i18nc("@action:button after copying", "Copied"));
        QTimer::singleShot(1500, copyText, [copyText]() { copyText->setText(i18nc("@action:button", "Copy the text")); });
    });
    auto *copyLink = new QPushButton(i18nc("@action:button", "Copy the link"), d);
    QObject::connect(copyLink, &QPushButton::clicked, d, []() { QGuiApplication::clipboard()->setText(shareLink()); });
    auto *folder = new QPushButton(i18nc("@action:button", "Show in folder"), d);
    QObject::connect(folder, &QPushButton::clicked, d, [path]() { reveal(path); });
    auto *close = new QPushButton(i18nc("@action:button", "Close"), d);
    QObject::connect(close, &QPushButton::clicked, d, &QDialog::close);
    for (QPushButton *b : {copyText, copyLink, folder}) {
        b->setCursor(Qt::PointingHandCursor);
        row->addWidget(b);
    }
    row->addStretch(1);
    row->addWidget(close);
    lay->addLayout(row);
    d->show();
    return d;
}

QLabel *secondary(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    // Relu à chaque changement de thème (KoraCutExportSettingsPanel::changeEvent).
    l->setProperty("koracutSecondaire", true);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, tok("texte_secondaire"));
    l->setPalette(p);
    l->setWordWrap(true);
    return l;
}

// Une tuile du bandeau : le logo, le nom, une ligne de détail au survol (infobulle).
class Tile : public QAbstractButton
{
public:
    Tile(const Model &m, QWidget *parent)
        : QAbstractButton(parent)
        , m_model(m)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(92, 80);
        setToolTip(QStringLiteral("<b>%1</b><br/>%2").arg(m.name.toHtmlEscaped(), m.detail.toHtmlEscaped()));
        setAccessibleName(m.name);
        setObjectName(QStringLiteral("koracutExportTile_%1").arg(m.id));
    }
    QString id() const { return m_model.id; }
    bool recommended{false};
    bool fits{true};

protected:
    void enterEvent(QEnterEvent *e) override
    {
        m_hover = true;
        update();
        QAbstractButton::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        m_hover = false;
        update();
        QAbstractButton::leaveEvent(e);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        const bool on = isChecked();
        p.setPen(on ? QPen(tok("accent"), 1.5) : QPen(tok("bordure"), 1));
        p.setBrush(on ? tok("accent", 38) : (m_hover ? tok("champ_survol") : tok("champ")));
        p.drawRoundedRect(r, KoraCutMetrics::RadiusCard, KoraCutMetrics::RadiusCard);
        const int icon = 28;
        const QPixmap pix = modelIcon(m_model, icon, devicePixelRatioF());
        p.setOpacity(fits ? 1. : 0.55);
        p.drawPixmap(QPoint((width() - icon) / 2, 11), pix);
        p.setOpacity(1.);
        QFont f = font();
        f.setPixelSize(KoraCutMetrics::FontLabel + 1);
        p.setFont(f);
        p.setPen(on ? tok("texte") : (fits ? tok("texte") : tok("texte_secondaire")));
        const QRect text(4, 44, width() - 8, height() - 46);
        p.drawText(text, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_model.name);
        if (recommended) {
            // Le modèle réglé par le type de vidéo du projet : un point bleu en haut à droite.
            p.setPen(Qt::NoPen);
            p.setBrush(tok("accent"));
            p.drawEllipse(QPointF(width() - 9, 9), 3.5, 3.5);
        }
    }

private:
    Model m_model;
    bool m_hover{false};
};

// Le défilement horizontal du bandeau à la molette (verticale ou horizontale).
class WheelToHorizontal : public QObject
{
public:
    explicit WheelToHorizontal(QScrollArea *area)
        : QObject(area)
        , m_area(area)
    {
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Wheel) {
            auto *w = static_cast<QWheelEvent *>(event);
            const QPoint d = w->angleDelta();
            const int delta = std::abs(d.x()) > std::abs(d.y()) ? d.x() : d.y();
            QScrollBar *bar = m_area->horizontalScrollBar();
            bar->setValue(bar->value() - delta / 2);
            return true;
        }
        return false;
    }

private:
    QScrollArea *m_area;
};

// Une carte de la file : logo, nom, séquence, chemin, barre bleue, temps restant, actions.
class JobCard : public QWidget
{
public:
    JobCard(const KoraCutExportQueue::JobPtr &job, int rank, QWidget *parent)
        : QWidget(parent)
        , m_job(job)
        , m_rank(rank)
    {
        setMinimumHeight(86);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(KoraCutMetrics::SpaceTight);
        row->addStretch(1);
        const QString state = job->state;
        auto button = [this, row](const QString &text, const QString &tip, const std::function<void()> &fn) {
            auto *b = new QPushButton(text, this);
            b->setToolTip(tip);
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedHeight(22);
            QFont f = b->font();
            f.setPixelSize(KoraCutMetrics::FontLabel + 1);
            b->setFont(f);
            connect(b, &QPushButton::clicked, this, fn);
            row->addWidget(b);
        };
        std::weak_ptr<KoraCutExportQueue::Job> weak = job;
        if (state == QLatin1String("en_cours") || state == QLatin1String("en_attente")) {
            button(i18nc("@action:button stop one render", "Cancel"), i18nc("@info:tooltip", "Stop this render. The partial file is removed."), [weak]() {
                if (auto j = weak.lock()) {
                    KoraCutExportQueue::self().abort(j);
                }
            });
        } else if (state == QLatin1String("termine")) {
            button(i18nc("@action:button", "Show in folder"), i18nc("@info:tooltip", "Open the folder of the exported file."), [weak]() {
                if (auto j = weak.lock()) {
                    reveal(j->path);
                }
            });
            button(i18nc("@action:button play the exported file", "Play"), i18nc("@info:tooltip", "Open the file with the system player."), [weak]() {
                if (auto j = weak.lock()) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(j->path));
                }
            });
            button(i18nc("@action:button share the exported video", "Share"),
                   i18nc("@info:tooltip", "A text ready to paste where your video goes, with the KoraCut link."), [weak, this]() {
                       if (auto j = weak.lock()) {
                           showShareDialog(j->path, window());
                       }
                   });
        } else {
            button(i18nc("@action:button render again", "Retry"), i18nc("@info:tooltip", "Render this file again with the same settings."), [weak]() {
                if (auto j = weak.lock()) {
                    KoraCutExportQueue::self().retry(j);
                }
            });
        }
        if (state != QLatin1String("en_cours")) {
            button(i18nc("@action:button remove from the queue", "Remove"), i18nc("@info:tooltip", "Take this line out of the queue. The file stays on the disk."), [weak]() {
                if (auto j = weak.lock()) {
                    if (j->state == QLatin1String("en_attente")) {
                        KoraCutExportQueue::self().abort(j);
                    }
                    KoraCutExportQueue::self().remove(j);
                }
            });
        }
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(52, 50, 10, 8);
        v->addLayout(row);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        auto job = m_job.lock();
        if (!job) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool running = job->state == QLatin1String("en_cours");
        p.setPen(QPen(running ? tok("accent") : tok("bordure"), 1));
        p.setBrush(tok("champ"));
        p.drawRoundedRect(r, KoraCutMetrics::RadiusCard, KoraCutMetrics::RadiusCard);
        if (const Model *m = model(job->presetId)) {
            p.drawPixmap(QPoint(12, 12), modelIcon(*m, 28, devicePixelRatioF()));
        }
        QFont f = font();
        f.setPixelSize(KoraCutMetrics::FontBase);
        f.setBold(true);
        p.setFont(f);
        p.setPen(tok("texte"));
        const QString title = i18nc("@info render job, %1 number, %2 model name", "Job %1 · %2", m_rank, job->presetName);
        const int textLeft = 52;
        const int right = width() - 10;
        // L'état à droite du titre : pourcentage et temps restant, ou le mot de l'état.
        QString stateText;
        QColor stateColor = tok("texte_secondaire");
        const int remaining = KoraCutExportQueue::self().remainingSeconds(job);
        if (running) {
            stateText = remaining >= 0 ? i18nc("@info progress, %1 percent, %2 time left", "%1 % · %2 left", job->progress, humanDuration(remaining))
                                       : i18nc("@info progress", "%1 %", job->progress);
            stateColor = tok("vert");
        } else if (job->state == QLatin1String("en_attente")) {
            stateText = job->held ? i18nc("@info render job state", "Queued") : i18nc("@info render job state", "Waiting");
        } else if (job->state == QLatin1String("termine")) {
            const double mb = QFileInfo(job->path).size() / 1000000.;
            stateText = i18nc("@info finished render, %1 size, %2 render time", "Done · %1 · %2", humanSize(mb),
                              humanDuration(int(std::lround((job->endMs - job->startMs) / 1000.))));
            stateColor = tok("vert");
        } else if (job->state == QLatin1String("annule")) {
            stateText = i18nc("@info render job state", "Cancelled");
        } else {
            stateText = i18nc("@info render job state", "Failed");
            stateColor = tok("rouge");
        }
        QFont small = font();
        small.setPixelSize(KoraCutMetrics::FontLabel + 1);
        const QFontMetrics sm(small);
        const int stateWidth = sm.horizontalAdvance(stateText);
        p.drawText(QRect(textLeft, 8, right - textLeft - stateWidth - 8, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(title, Qt::ElideRight, right - textLeft - stateWidth - 8));
        p.setFont(small);
        p.setPen(stateColor);
        p.drawText(QRect(right - stateWidth, 8, stateWidth, 18), Qt::AlignRight | Qt::AlignVCenter, stateText);
        p.setPen(tok("texte_secondaire"));
        QString second = job->sequence.isEmpty() ? QFileInfo(job->path).fileName() : QStringLiteral("%1 · %2").arg(job->sequence, QFileInfo(job->path).fileName());
        if (job->state == QLatin1String("echec") && !job->errorText.isEmpty()) {
            second = job->errorText;
            p.setPen(tok("rouge"));
        }
        p.drawText(QRect(textLeft, 26, right - textLeft, 16), Qt::AlignLeft | Qt::AlignVCenter, sm.elidedText(second, Qt::ElideMiddle, right - textLeft));
        // La barre bleue de la maquette (dix cases de progression, sans dégradé).
        if (running || job->state == QLatin1String("termine")) {
            const QRectF track(textLeft, 45, right - textLeft, 4);
            p.setPen(Qt::NoPen);
            p.setBrush(tok("voile", 60));
            p.drawRoundedRect(track, 2, 2);
            QRectF done = track;
            done.setWidth(track.width() * std::clamp(job->progress, 0, 100) / 100.);
            p.setBrush(tok("accent"));
            p.drawRoundedRect(done, 2, 2);
        }
    }

private:
    std::weak_ptr<KoraCutExportQueue::Job> m_job;
    int m_rank;
};

// La barre de la file entière : fine, bleue, sans dégradé.
class ProgressLine : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        const double v = property("progress").toDouble();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(tok("voile", 60));
        p.drawRoundedRect(QRectF(rect()), 2, 2);
        if (v > 0) {
            p.setBrush(tok("accent"));
            p.drawRoundedRect(QRectF(0, 0, width() * std::min(1., v), height()), 2, 2);
        }
    }
};

} // namespace

// -----------------------------------------------------------------------------------------------
// Les logos

QPixmap KoraCutExport::modelIcon(const Model &m, int size, qreal dpr)
{
    QPixmap pix(QSize(size, size) * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box(0, 0, size, size);
    const double radius = size * 0.24;
    if (!m.logo.isEmpty()) {
        QFile file(QStringLiteral(":/koracut/plateformes/%1.svg").arg(m.logo));
        QByteArray svg = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        const bool pastille = m.logoBackground.isValid();
        QColor ink = m.logoColor.isValid() ? m.logoColor : tok("texte");
        if (pastille) {
            p.setPen(Qt::NoPen);
            p.setBrush(m.logoBackground);
            p.drawRoundedRect(box, radius, radius);
            // Une pastille noire sur un thème sombre se perdrait : un filet autour.
            if (m.logoBackground.lightness() < 40) {
                p.setPen(QPen(tok("bordure_survol"), 1));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
            }
        }
        svg.replace("currentColor", ink.name().toLatin1());
        QSvgRenderer renderer(svg);
        const double inset = pastille ? size * 0.2 : size * 0.04;
        renderer.render(&p, box.adjusted(inset, inset, -inset, -inset));
        return pix;
    }
    // Un modèle technique : un badge à la manière des vignettes de codec de Resolve.
    p.setPen(Qt::NoPen);
    p.setBrush(m.logoBackground.isValid() ? m.logoBackground : tok("surface"));
    p.drawRoundedRect(box, radius, radius);
    QFont f = QApplication::font();
    f.setBold(true);
    f.setPixelSize(std::max(7, int(size * (m.badge.size() > 4 ? 0.24 : 0.3))));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, m.badge);
    return pix;
}

// -----------------------------------------------------------------------------------------------
// Le coordinateur

KoraCutDeliverPage::KoraCutDeliverPage(QWidget *window, KoraCutExportTiles *tiles, KoraCutExportSettingsPanel *settings, KoraCutExportQueuePanel *queue)
    : QObject(window)
    , m_tiles(tiles)
    , m_panel(settings)
    , m_queue(queue)
{
    s_page = this;
    connect(tiles, &KoraCutExportTiles::modelChosen, this, &KoraCutDeliverPage::applyModel);
    connect(settings, &KoraCutExportSettingsPanel::modelChosen, this, &KoraCutDeliverPage::applyModel);
    connect(settings, &KoraCutExportSettingsPanel::edited, this, [this]() { setSettings(m_panel->read(), true); });
    connect(settings, &KoraCutExportSettingsPanel::addToQueue, this, [this]() { render(true); });
    connect(settings, &KoraCutExportSettingsPanel::renderNow, this, [this]() { render(false); });
    connect(&KoraCutExportQueue::self(), &KoraCutExportQueue::jobFinished, this, [this](const QString &id, const QString &state) {
        if (state != QLatin1String("termine")) {
            return;
        }
        auto j = KoraCutExportQueue::self().find(id);
        if (!j) {
            return;
        }
        if (revealAtEnd()) {
            reveal(j->path);
        }
        // Le tout premier export réussi d'une personne : la fenêtre Partager se propose une fois, d'elle-même,
        // puis plus jamais (le bouton reste). Pas pendant un export piloté par la porte ou une capture.
        KConfigGroup g = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
        const bool sansFenetre = qApp->property("koracut_sans_fenetre").toBool() || !qEnvironmentVariable("KORACUT_SHOT").isEmpty();
        if (!sansFenetre && !g.readEntry("exportPartagePropose", false)) {
            g.writeEntry("exportPartagePropose", true);
            g.sync();
            showShareDialog(j->path, m_queue ? m_queue->window() : nullptr);
        }
    });
    // Le projet ou la séquence change : on relit ses réglages. Une comparaison d'identifiant, seulement
    // quand la page est à l'écran (rien ne tourne pendant qu'on monte).
    m_watch = new QTimer(this);
    m_watch->setInterval(800);
    connect(m_watch, &QTimer::timeout, this, &KoraCutDeliverPage::check);
    m_watch->start();
}

KoraCutDeliverPage *KoraCutDeliverPage::instance()
{
    return s_page.data();
}

void KoraCutDeliverPage::check()
{
    if (!m_panel || !m_panel->isVisible()) {
        return;
    }
    const QString uuid = pCore && pCore->currentDoc() ? pCore->currentTimelineId().toString() : QString();
    if (uuid != m_uuid) {
        reload();
    } else {
        m_panel->refreshSummary();
    }
}

QString KoraCutDeliverPage::recommendedModel() const
{
    const KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    const QSize size = pCore ? pCore->getCurrentFrameSize() : QSize(1920, 1080);
    const QString type = doc ? doc->getDocumentProperty(QStringLiteral("koracut:videotype")) : QString();
    return type.isEmpty() ? modelForSize(size) : modelForVideoType(type, size);
}

void KoraCutDeliverPage::reload()
{
    if (!pCore || !pCore->currentDoc()) {
        return;
    }
    m_uuid = pCore->currentTimelineId().toString();
    const QSize size = pCore->getCurrentFrameSize();
    m_settings = documentSettings(m_uuid, size, pCore->getCurrentFps());
    if (m_tiles) {
        m_tiles->setRecommended(recommendedModel(), size);
        m_tiles->setCurrent(m_settings.model);
    }
    if (m_panel) {
        m_panel->showSettings(m_settings);
    }
    Q_EMIT settingsChanged();
}

void KoraCutDeliverPage::applyModel(const QString &id)
{
    const Model *m = model(id);
    if (!m || !pCore || !pCore->currentDoc()) {
        return;
    }
    Settings s = Settings::forModel(*m, pCore->getCurrentFrameSize(), pCore->getCurrentFps());
    // Ce que la personne a posé pour tout le projet reste : le dossier, le nom, la plage.
    s.folder = m_settings.folder;
    s.name = m_settings.name;
    s.inOut = m_settings.inOut;
    setSettings(s, false);
}

void KoraCutDeliverPage::setSettings(const Settings &s, bool customized)
{
    m_settings = s;
    m_settings.customized = customized;
    if (m_uuid.isEmpty() && pCore && pCore->currentDoc()) {
        m_uuid = pCore->currentTimelineId().toString();
    }
    saveDocumentSettings(m_uuid, m_settings);
    if (m_tiles) {
        m_tiles->setCurrent(m_settings.model);
    }
    if (m_panel && !customized) {
        m_panel->showSettings(m_settings);
    } else if (m_panel) {
        m_panel->refreshSummary();
    }
    Q_EMIT settingsChanged();
}

Enqueued KoraCutDeliverPage::render(bool held)
{
    if (m_panel) {
        m_settings = m_panel->read();
        saveDocumentSettings(m_uuid, m_settings);
    }
    const Enqueued r = enqueue(m_settings, held);
    if (!pCore) {
        return r;
    }
    if (!r.ok) {
        pCore->displayMessage(r.message, ErrorMessage, 8000);
    } else if (r.still) {
        pCore->displayMessage(i18nc("@info", "Image saved: %1", QFileInfo(r.path).fileName()), OperationCompletedMessage, 6000);
    } else if (held) {
        pCore->displayMessage(i18nc("@info", "Added to the render queue: %1. Start it with “Render All”.", QFileInfo(r.path).fileName()), InformationMessage, 6000);
    }
    if (m_panel) {
        m_panel->refreshSummary();
    }
    return r;
}

// -----------------------------------------------------------------------------------------------
// Le bandeau des modèles

KoraCutExportTiles::KoraCutExportTiles(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("koracutExportTiles"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceTight, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceTight);
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->installEventFilter(new WheelToHorizontal(m_scroll));
    m_scroll->setMinimumHeight(92);
    m_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_scroll->setFixedHeight(100);
    layout->setAlignment(m_scroll, Qt::AlignVCenter);
    layout->addWidget(m_scroll);
    rebuild();
}

void KoraCutExportTiles::rebuild()
{
    m_row = new QWidget();
    m_row->setAutoFillBackground(false);
    auto *row = new QHBoxLayout(m_row);
    row->setContentsMargins(0, 4, 0, 4);
    row->setSpacing(6);
    m_tiles.clear();
    bool platformDone = false;
    for (const Model &m : models()) {
        if (!m.platform && !platformDone) {
            // La séparation entre les réseaux et les fichiers techniques.
            platformDone = true;
            auto *sep = new QFrame(m_row);
            sep->setFrameShape(QFrame::VLine);
            sep->setFixedWidth(12);
            row->addWidget(sep);
        }
        auto *tile = new Tile(m, m_row);
        tile->setChecked(m.id == m_current);
        tile->recommended = m.id == m_recommended;
        tile->fits = modelFits(m, m_sequence);
        connect(tile, &QAbstractButton::clicked, this, [this, id = m.id]() { Q_EMIT modelChosen(id); });
        row->addWidget(tile);
        m_tiles << tile;
    }
    row->addStretch(1);
    m_scroll->setWidget(m_row);
}

void KoraCutExportTiles::setCurrent(const QString &id)
{
    m_current = id;
    for (QAbstractButton *b : std::as_const(m_tiles)) {
        auto *t = static_cast<Tile *>(b);
        t->setChecked(t->id() == id);
        if (t->id() == id) {
            m_scroll->ensureWidgetVisible(t, 40, 0);
        }
    }
}

void KoraCutExportTiles::setRecommended(const QString &id, const QSize &sequence)
{
    m_recommended = id;
    m_sequence = sequence;
    for (QAbstractButton *b : std::as_const(m_tiles)) {
        auto *t = static_cast<Tile *>(b);
        t->recommended = t->id() == id;
        if (const Model *m = model(t->id())) {
            t->fits = modelFits(*m, sequence);
            t->setToolTip(QStringLiteral("<b>%1</b><br/>%2%3")
                              .arg(m->name.toHtmlEscaped(), m->detail.toHtmlEscaped(),
                                   t->fits ? QString()
                                           : QStringLiteral("<br/><i>%1</i>")
                                                 .arg(i18nc("@info:tooltip", "The sequence has another shape: the file keeps the shape of the sequence.")
                                                          .toHtmlEscaped())));
            if (t->recommended) {
                t->setToolTip(t->toolTip() + QStringLiteral("<br/><i>%1</i>").arg(i18nc("@info:tooltip", "Chosen for this project's video type.").toHtmlEscaped()));
            }
        }
        t->update();
    }
}

void KoraCutExportTiles::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        for (QAbstractButton *b : std::as_const(m_tiles)) {
            b->update();
        }
    }
    QWidget::changeEvent(event);
}

// -----------------------------------------------------------------------------------------------
// Les paramètres

namespace {
struct Choice
{
    QString label;
    QVariant value;
};
void fill(QComboBox *box, const QList<Choice> &choices)
{
    for (const Choice &c : choices) {
        box->addItem(c.label, c.value);
    }
}
void select(QComboBox *box, const QVariant &value)
{
    const int i = box->findData(value);
    if (i >= 0) {
        box->setCurrentIndex(i);
    }
}
QFormLayout *form(QWidget *w)
{
    auto *f = new QFormLayout(w);
    f->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone);
    f->setHorizontalSpacing(KoraCutMetrics::SpaceZone);
    f->setVerticalSpacing(KoraCutMetrics::SpaceGroup);
    f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return f;
}
} // namespace

KoraCutExportSettingsPanel::KoraCutExportSettingsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("koracutExportSettings"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // En tête : le préréglage (la liste de Resolve 21, avec les logos), nom, emplacement, plage.
    auto *head = new QWidget(this);
    auto *hf = form(head);
    m_preset = new QComboBox(head);
    m_preset->setObjectName(QStringLiteral("koracutExportPreset"));
    m_preset->setIconSize(QSize(18, 18));
    fillModels();
    hf->addRow(i18nc("@label:listbox the export preset", "Preset"), m_preset);

    auto *nameRow = new QHBoxLayout();
    m_name = new QLineEdit(head);
    m_name->setObjectName(QStringLiteral("koracutExportName"));
    auto *tokens = new QToolButton(head);
    tokens->setText(QStringLiteral("+"));
    tokens->setToolTip(i18nc("@info:tooltip", "Insert a word that changes with each export: project, sequence, date, format, model, version."));
    tokens->setPopupMode(QToolButton::InstantPopup);
    auto *tokenMenu = new QMenu(tokens);
    const QList<QPair<QString, QString>> words = {{QStringLiteral("{projet}"), i18nc("@item:inmenu file name token", "Project name")},
                                                   {QStringLiteral("{sequence}"), i18nc("@item:inmenu file name token", "Sequence name")},
                                                   {QStringLiteral("{date}"), i18nc("@item:inmenu file name token", "Date")},
                                                   {QStringLiteral("{heure}"), i18nc("@item:inmenu file name token", "Time")},
                                                   {QStringLiteral("{format}"), i18nc("@item:inmenu file name token", "Format (1920x1080)")},
                                                   {QStringLiteral("{modele}"), i18nc("@item:inmenu file name token", "Model name")},
                                                   {QStringLiteral("{version}"), i18nc("@item:inmenu file name token", "Version (v1, v2…)")}};
    for (const auto &w : words) {
        tokenMenu->addAction(QStringLiteral("%1   %2").arg(w.second, w.first), this, [this, token = w.first]() {
            m_name->insert(token);
            Q_EMIT edited();
        });
    }
    tokens->setMenu(tokenMenu);
    nameRow->addWidget(m_name, 1);
    nameRow->addWidget(tokens);
    hf->addRow(i18nc("@label:textbox", "File name"), nameRow);
    m_namePreview = secondary(QString(), head);
    hf->addRow(QString(), m_namePreview);

    auto *folderRow = new QHBoxLayout();
    m_folder = new QLineEdit(head);
    m_folder->setPlaceholderText(i18nc("@info:placeholder", "Exports folder next to the project"));
    auto *browse = new QPushButton(i18nc("@action:button choose a folder", "Browse…"), head);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString start = m_folder->text().isEmpty() ? defaultFolder() : m_folder->text();
        const QString dir = QFileDialog::getExistingDirectory(this, i18nc("@title:window", "Export Location"), start);
        if (!dir.isEmpty()) {
            m_folder->setText(dir);
            Q_EMIT edited();
        }
    });
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browse);
    hf->addRow(i18nc("@label:textbox where the file goes", "Location"), folderRow);

    auto *rangeRow = new QHBoxLayout();
    m_whole = new QRadioButton(i18nc("@option:radio export range", "Entire timeline"), head);
    m_inOut = new QRadioButton(i18nc("@option:radio export range", "In/Out range"), head);
    auto *rangeGroup = new QButtonGroup(this);
    rangeGroup->addButton(m_whole);
    rangeGroup->addButton(m_inOut);
    rangeRow->addWidget(m_whole);
    rangeRow->addWidget(m_inOut);
    rangeRow->addStretch(1);
    hf->addRow(i18nc("@label what part of the timeline is exported", "Export"), rangeRow);
    m_rangeInfo = secondary(QString(), head);
    hf->addRow(QString(), m_rangeInfo);
    outer->addWidget(head);

    // Les trois onglets de Resolve : Vidéo, Audio, Fichier.
    auto *tabRow = new QHBoxLayout();
    tabRow->setContentsMargins(KoraCutMetrics::SpaceZone, 0, KoraCutMetrics::SpaceZone, 0);
    tabRow->setSpacing(0);
    const QStringList tabNames = {i18nc("@title:tab export settings", "Video"), i18nc("@title:tab export settings", "Audio"), i18nc("@title:tab export settings", "File")};
    auto *tabGroup = new QButtonGroup(this);
    for (int i = 0; i < tabNames.size(); ++i) {
        auto *b = new QToolButton(this);
        b->setText(tabNames.at(i));
        b->setCheckable(true);
        b->setProperty("koracutSegment", i == 0 ? QStringLiteral("premier") : (i == tabNames.size() - 1 ? QStringLiteral("dernier") : QStringLiteral("milieu")));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumHeight(26);
        tabGroup->addButton(b, i);
        tabRow->addWidget(b);
        m_tabButtons << b;
    }
    m_tabButtons.first()->setChecked(true);
    outer->addLayout(tabRow);
    m_tabs = new QStackedWidget(this);
    m_tabs->addWidget(videoTab());
    m_tabs->addWidget(audioTab());
    m_tabs->addWidget(fileTab());
    connect(tabGroup, &QButtonGroup::idClicked, m_tabs, &QStackedWidget::setCurrentIndex);
    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_tabs);
    outer->addWidget(scroll, 1);

    // Le bas, comme Resolve : taille estimée, avertissements, puis les deux boutons.
    auto *foot = new QWidget(this);
    auto *fv = new QVBoxLayout(foot);
    fv->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone);
    fv->setSpacing(KoraCutMetrics::SpaceGroup);
    auto *estimateRow = new QHBoxLayout();
    m_estimate = new QLabel(foot);
    m_estimate->setObjectName(QStringLiteral("koracutExportEstimate"));
    estimateRow->addWidget(m_estimate, 1);
    m_guides = new QPushButton(i18nc("@action:button show platform safe zones on the monitor", "Safe zones"), foot);
    m_guides->setToolTip(i18nc("@info:tooltip", "Show on the monitor the zones this platform covers with its buttons and captions."));
    connect(m_guides, &QPushButton::clicked, this, [this]() {
        const Model *m = model(read().model);
        if (m && pCore && pCore->getMonitor(Kdenlive::ProjectMonitor)) {
            pCore->getMonitor(Kdenlive::ProjectMonitor)->koraCutSetPlatform(m->guides);
        }
    });
    estimateRow->addWidget(m_guides);
    fv->addLayout(estimateRow);
    m_warnings = new QLabel(foot);
    m_warnings->setWordWrap(true);
    m_warnings->setObjectName(QStringLiteral("koracutExportWarnings"));
    fv->addWidget(m_warnings);
    auto *buttons = new QHBoxLayout();
    auto *advanced = new QPushButton(i18nc("@action:button all the MLT render presets", "Advanced…"), foot);
    advanced->setToolTip(i18nc("@info:tooltip", "The full export window with every render preset, scripts and options."));
    connect(advanced, &QPushButton::clicked, this, &KoraCutExportSettingsPanel::advancedRequested);
    m_add = new QPushButton(i18nc("@action:button", "Add to Queue"), foot);
    m_add->setObjectName(QStringLiteral("koracutExportAdd"));
    m_add->setToolTip(i18nc("@info:tooltip", "Keep this export in the queue. Start everything later with “Render All”."));
    m_now = new QPushButton(i18nc("@action:button", "Export Now"), foot);
    m_now->setObjectName(QStringLiteral("koracutExportNow"));
    m_now->setDefault(true);
    m_now->setToolTip(i18nc("@info:tooltip", "Start this export right away (after the ones already running)."));
    connect(m_add, &QPushButton::clicked, this, &KoraCutExportSettingsPanel::addToQueue);
    connect(m_now, &QPushButton::clicked, this, &KoraCutExportSettingsPanel::renderNow);
    buttons->addWidget(advanced);
    buttons->addStretch(1);
    buttons->addWidget(m_add);
    buttons->addWidget(m_now);
    fv->addLayout(buttons);
    outer->addWidget(foot);

    // Tout changement enregistre aussitôt (comme les Préférences de KoraCut).
    const auto changed = [this]() {
        if (!m_loading) {
            updateEnabled();
            Q_EMIT edited();
        }
    };
    connect(m_preset, &QComboBox::activated, this, [this](int index) { Q_EMIT modelChosen(m_preset->itemData(index).toString()); });
    connect(m_name, &QLineEdit::textEdited, this, changed);
    connect(m_folder, &QLineEdit::editingFinished, this, changed);
    connect(m_whole, &QRadioButton::toggled, this, changed);
    for (QComboBox *box : findChildren<QComboBox *>()) {
        if (box != m_preset) {
            connect(box, &QComboBox::activated, this, changed);
        }
    }
    for (QCheckBox *box : findChildren<QCheckBox *>()) {
        if (box != m_reveal) {
            connect(box, &QCheckBox::toggled, this, changed);
        }
    }
    for (QRadioButton *r : {m_qualityAuto, m_keysAuto}) {
        connect(r, &QRadioButton::toggled, this, changed);
    }
    for (QSpinBox *sp : {m_width, m_height, m_keys, m_lufs}) {
        connect(sp, &QSpinBox::editingFinished, this, changed);
    }
    connect(m_cap, &QDoubleSpinBox::editingFinished, this, changed);
}

void KoraCutExportSettingsPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        for (QLabel *l : findChildren<QLabel *>()) {
            if (l->property("koracutSecondaire").toBool()) {
                QPalette p = l->palette();
                p.setColor(QPalette::WindowText, tok("texte_secondaire"));
                l->setPalette(p);
            }
        }
        if (!m_loading && m_preset) {
            const QString current = m_preset->currentData().toString();
            m_loading = true;
            fillModels();
            select(m_preset, current);
            m_loading = false;
            refreshSummary();
        }
    }
    QWidget::changeEvent(event);
}

void KoraCutExportSettingsPanel::fillModels()
{
    m_preset->clear();
    const qreal dpr = devicePixelRatioF();
    for (const Model &m : models()) {
        m_preset->addItem(QIcon(modelIcon(m, 18, dpr)), m.name, m.id);
    }
}

QWidget *KoraCutExportSettingsPanel::videoTab()
{
    auto *w = new QWidget(this);
    auto *f = form(w);
    m_video = new QCheckBox(i18nc("@option:check", "Export video"), w);
    f->addRow(QString(), m_video);
    m_container = new QComboBox(w);
    fill(m_container, {{QStringLiteral("MP4"), QStringLiteral("mp4")},
                       {QStringLiteral("QuickTime (MOV)"), QStringLiteral("mov")},
                       {QStringLiteral("GIF"), QStringLiteral("gif")},
                       {QStringLiteral("PNG"), QStringLiteral("png")},
                       {i18nc("@item:inlistbox sound only file", "WAV (sound only)"), QStringLiteral("wav")},
                       {i18nc("@item:inlistbox sound only file", "M4A (sound only)"), QStringLiteral("m4a")},
                       {i18nc("@item:inlistbox sound only file", "MP3 (sound only)"), QStringLiteral("mp3")}});
    f->addRow(i18nc("@label:listbox file format", "Format"), m_container);
    m_codec = new QComboBox(w);
    fill(m_codec, {{QStringLiteral("H.264"), QStringLiteral("h264")},
                   {QStringLiteral("H.265 (HEVC)"), QStringLiteral("h265")},
                   {QStringLiteral("Apple ProRes 422 HQ"), QStringLiteral("prores_hq")},
                   {QStringLiteral("Apple ProRes 4444"), QStringLiteral("prores_4444")},
                   {QStringLiteral("GIF"), QStringLiteral("gif")},
                   {QStringLiteral("PNG"), QStringLiteral("png")}});
    f->addRow(i18nc("@label:listbox", "Codec"), m_codec);
    m_hardware = new QCheckBox(i18nc("@option:check", "Hardware acceleration if possible"), w);
    m_hardware->setToolTip(i18nc("@info:tooltip", "Encode with the graphics chip (VideoToolbox on a Mac): the computer stays free while the file is made."));
    f->addRow(QString(), m_hardware);
    m_resolution = new QComboBox(w);
    f->addRow(i18nc("@label:listbox", "Resolution"), m_resolution);
    auto *sizeRow = new QHBoxLayout();
    m_width = new QSpinBox(w);
    m_height = new QSpinBox(w);
    for (QSpinBox *sp : {m_width, m_height}) {
        sp->setRange(16, 8192);
        sp->setSingleStep(2);
    }
    sizeRow->addWidget(m_width);
    auto *times = new QLabel(QStringLiteral("×"), w);
    times->setObjectName(QStringLiteral("koracutExportTimes"));
    sizeRow->addWidget(times);
    sizeRow->addWidget(m_height);
    sizeRow->addStretch(1);
    f->addRow(QString(), sizeRow);
    m_fps = secondary(QString(), w);
    f->addRow(i18nc("@label frame rate", "Frame rate"), m_fps);

    auto *quality = new QWidget(w);
    auto *qv = new QVBoxLayout(quality);
    qv->setContentsMargins(0, 0, 0, 0);
    m_qualityAuto = new QRadioButton(i18nc("@option:radio quality", "Automatic"), quality);
    auto *capRow = new QHBoxLayout();
    m_qualityCap = new QRadioButton(i18nc("@option:radio quality, followed by a bitrate", "Restrict to"), quality);
    m_cap = new QDoubleSpinBox(quality);
    m_cap->setRange(0.5, 400.);
    m_cap->setDecimals(1);
    m_cap->setSuffix(i18nc("@item:valuesuffix megabits per second", " Mb/s"));
    capRow->addWidget(m_qualityCap);
    capRow->addWidget(m_cap);
    capRow->addStretch(1);
    qv->addWidget(m_qualityAuto);
    qv->addLayout(capRow);
    auto *qGroup = new QButtonGroup(quality);
    qGroup->addButton(m_qualityAuto);
    qGroup->addButton(m_qualityCap);
    f->addRow(i18nc("@label", "Quality"), quality);

    m_profile = new QComboBox(w);
    fill(m_profile, {{i18nc("@item:inlistbox encoding profile", "Auto"), QStringLiteral("auto")},
                     {i18nc("@item:inlistbox encoding profile", "High"), QStringLiteral("high")},
                     {i18nc("@item:inlistbox encoding profile", "Main"), QStringLiteral("main")},
                     {i18nc("@item:inlistbox encoding profile", "Baseline"), QStringLiteral("baseline")}});
    f->addRow(i18nc("@label:listbox", "Encoding profile"), m_profile);

    auto *keys = new QWidget(w);
    auto *kv = new QVBoxLayout(keys);
    kv->setContentsMargins(0, 0, 0, 0);
    m_keysAuto = new QRadioButton(i18nc("@option:radio key frames", "Automatic"), keys);
    auto *keysRow = new QHBoxLayout();
    m_keysEvery = new QRadioButton(i18nc("@option:radio key frames, followed by a number", "Every"), keys);
    m_keys = new QSpinBox(keys);
    m_keys->setRange(1, 600);
    m_keys->setSuffix(i18nc("@item:valuesuffix", " frames"));
    keysRow->addWidget(m_keysEvery);
    keysRow->addWidget(m_keys);
    keysRow->addStretch(1);
    kv->addWidget(m_keysAuto);
    kv->addLayout(keysRow);
    auto *kGroup = new QButtonGroup(keys);
    kGroup->addButton(m_keysAuto);
    kGroup->addButton(m_keysEvery);
    f->addRow(i18nc("@label", "Key frames"), keys);
    m_reorder = new QCheckBox(i18nc("@option:check B-frames", "Frame reordering"), w);
    m_reorder->setToolTip(i18nc("@info:tooltip", "Lets the encoder store frames out of order (B-frames): a smaller file for the same quality."));
    f->addRow(QString(), m_reorder);

    m_subtitles = new QComboBox(w);
    fill(m_subtitles, {{i18nc("@item:inlistbox subtitles", "Burned into the image"), QStringLiteral("incruster")},
                       {i18nc("@item:inlistbox subtitles", "Separate .srt file"), QStringLiteral("fichier")},
                       {i18nc("@item:inlistbox subtitles", "No subtitles"), QStringLiteral("sans")}});
    m_subtitles->setToolTip(i18nc("@info:tooltip", "The subtitle track of the timeline stays as it is in every case."));
    f->addRow(i18nc("@label:listbox", "Subtitles"), m_subtitles);
    return w;
}

QWidget *KoraCutExportSettingsPanel::audioTab()
{
    auto *w = new QWidget(this);
    auto *f = form(w);
    m_audio = new QCheckBox(i18nc("@option:check", "Export audio"), w);
    f->addRow(QString(), m_audio);
    m_audioCodec = new QComboBox(w);
    fill(m_audioCodec, {{QStringLiteral("AAC"), QStringLiteral("aac")},
                        {QStringLiteral("MP3"), QStringLiteral("mp3")},
                        {i18nc("@item:inlistbox uncompressed sound", "Linear PCM 24 bits"), QStringLiteral("pcm24")},
                        {i18nc("@item:inlistbox uncompressed sound", "Linear PCM 16 bits"), QStringLiteral("pcm16")}});
    f->addRow(i18nc("@label:listbox", "Codec"), m_audioCodec);
    m_audioRate = new QComboBox(w);
    for (int kbps : {96, 128, 160, 192, 256, 320, 384}) {
        m_audioRate->addItem(i18nc("@item:inlistbox kilobits per second", "%1 kb/s", kbps), kbps);
    }
    f->addRow(i18nc("@label:listbox audio bitrate", "Bitrate"), m_audioRate);
    m_sampleRate = new QComboBox(w);
    fill(m_sampleRate, {{QStringLiteral("44 100 Hz"), 44100}, {QStringLiteral("48 000 Hz"), 48000}});
    f->addRow(i18nc("@label:listbox", "Sample rate"), m_sampleRate);
    m_channels = new QComboBox(w);
    fill(m_channels, {{i18nc("@item:inlistbox", "Mono"), 1}, {i18nc("@item:inlistbox", "Stereo"), 2}});
    f->addRow(i18nc("@label:listbox", "Channels"), m_channels);

    auto *title = new QLabel(i18nc("@title:group", "Normalize Audio"), w);
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);
    f->addRow(title);
    m_normalize = new QCheckBox(i18nc("@option:check", "Normalize audio"), w);
    m_normalize->setToolTip(i18nc("@info:tooltip", "KoraCut measures the mix, then sets one gain so the file lands on the target loudness. The peak stays under -1 dBTP."));
    f->addRow(QString(), m_normalize);
    m_lufs = new QSpinBox(w);
    m_lufs->setRange(-30, -6);
    m_lufs->setSuffix(QStringLiteral(" LUFS"));
    f->addRow(i18nc("@label:spinbox", "Target loudness"), m_lufs);
    m_peak = secondary(i18nc("@info", "-1.0 dBTP (limited at -1.5 before encoding)"), w);
    f->addRow(i18nc("@label true peak", "True peak"), m_peak);
    f->addRow(QString(), secondary(i18nc("@info", "-14 LUFS for YouTube and social networks, -16 for a podcast, -23 for television."), w));
    return w;
}

QWidget *KoraCutExportSettingsPanel::fileTab()
{
    auto *w = new QWidget(this);
    auto *f = form(w);
    f->addRow(secondary(i18nc("@info",
                              "The file name is written above. Words between braces change with each export: {projet}, {sequence}, {date}, {heure}, "
                              "{format}, {modele}, {version}."),
                        w));
    f->addRow(secondary(i18nc("@info", "KoraCut never writes over a file: when the name is taken, the export gets v2, v3…"), w));
    m_reveal = new QCheckBox(i18nc("@option:check", "Show the file in its folder when the export is done"), w);
    m_reveal->setChecked(revealAtEnd());
    connect(m_reveal, &QCheckBox::toggled, this, [](bool on) {
        KConfigGroup g = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
        g.writeEntry("exportRevelerFin", on);
        g.sync();
    });
    f->addRow(QString(), m_reveal);
    m_signature = new QCheckBox(i18nc("@option:check", "Write “Made with KoraCut, koracut.org” in the file's information"), w);
    m_signature->setToolTip(i18nc("@info:tooltip", "A line in the file's metadata (the comment field), never on the image. "
                                                   "It helps people discover KoraCut. Uncheck it for a client who wants nothing of the kind."));
    m_signature->setObjectName(QStringLiteral("koracut_export_signature")); // l'outil reglages_ecrire la retrouve par ce nom
    m_signature->setChecked(signatureEnabled());
    connect(m_signature, &QCheckBox::toggled, this, [](bool on) { setSignatureEnabled(on); });
    f->addRow(QString(), m_signature);
    return w;
}

void KoraCutExportSettingsPanel::setTab(int index)
{
    if (index >= 0 && index < m_tabButtons.size()) {
        m_tabButtons.at(index)->setChecked(true);
        m_tabs->setCurrentIndex(index);
    }
}

void KoraCutExportSettingsPanel::showSettings(const Settings &s)
{
    m_loading = true;
    m_base = s;
    select(m_preset, s.model);
    m_name->setText(s.name);
    m_folder->setText(s.folder);
    m_whole->setChecked(!s.inOut);
    m_inOut->setChecked(s.inOut);
    m_video->setChecked(s.video);
    select(m_container, s.container);
    select(m_codec, s.videoCodec.isEmpty() ? QStringLiteral("h264") : s.videoCodec);
    m_hardware->setChecked(s.hardware);
    // La liste des tailles : comme la séquence, puis les tailles courantes du même sens.
    m_resolution->clear();
    const QSize seq = pCore ? pCore->getCurrentFrameSize() : QSize(1920, 1080);
    m_resolution->addItem(i18nc("@item:inlistbox resolution, %1 x %2", "Same as the sequence (%1 × %2)", seq.width(), seq.height()), QSize());
    const bool vertical = seq.height() > seq.width();
    QList<QSize> sizes = vertical ? QList<QSize>{{2160, 3840}, {1080, 1920}, {720, 1280}}
                                  : QList<QSize>{{3840, 2160}, {2560, 1440}, {1920, 1080}, {1280, 720}};
    sizes << QSize(1080, 1350) << QSize(1080, 1080) << QSize(720, 720);
    if (s.width > 0 && !sizes.contains(QSize(s.width, s.height))) {
        sizes.prepend(QSize(s.width, s.height));
    }
    for (const QSize &z : std::as_const(sizes)) {
        m_resolution->addItem(QStringLiteral("%1 × %2").arg(z.width()).arg(z.height()), z);
    }
    m_resolution->addItem(i18nc("@item:inlistbox", "Custom"), QSize(1, 1));
    select(m_resolution, s.width > 0 ? QSize(s.width, s.height) : QSize());
    m_width->setValue(s.width > 0 ? s.width : seq.width());
    m_height->setValue(s.height > 0 ? s.height : seq.height());
    m_qualityAuto->setChecked(s.bitrateKbps <= 0);
    m_qualityCap->setChecked(s.bitrateKbps > 0);
    m_cap->setValue(s.bitrateKbps > 0 ? s.bitrateKbps / 1000. : 10.);
    select(m_profile, s.profile.isEmpty() ? QStringLiteral("auto") : s.profile);
    m_keysAuto->setChecked(s.keyframes <= 0);
    m_keysEvery->setChecked(s.keyframes > 0);
    m_keys->setValue(s.keyframes > 0 ? s.keyframes : 60);
    m_reorder->setChecked(s.reorder);
    select(m_subtitles, s.subtitles);
    m_audio->setChecked(s.audio);
    select(m_audioCodec, s.audioCodec.isEmpty() ? QStringLiteral("aac") : s.audioCodec);
    select(m_audioRate, s.audioKbps);
    select(m_sampleRate, s.sampleRate);
    select(m_channels, s.channels);
    m_normalize->setChecked(s.normalize);
    m_lufs->setValue(s.lufs);
    m_loading = false;
    updateEnabled();
    refreshSummary();
}

Settings KoraCutExportSettingsPanel::read() const
{
    Settings s = m_base;
    s.model = m_preset->currentData().toString();
    s.name = m_name->text().trimmed().isEmpty() ? QStringLiteral("{projet} - {modele}") : m_name->text().trimmed();
    s.folder = m_folder->text().trimmed();
    s.inOut = m_inOut->isChecked();
    s.container = m_container->currentData().toString();
    const bool soundOnly = s.container == QLatin1String("wav") || s.container == QLatin1String("m4a") || s.container == QLatin1String("mp3");
    s.video = m_video->isChecked() && !soundOnly;
    s.videoCodec = s.video ? m_codec->currentData().toString() : QString();
    s.hardware = m_hardware->isChecked();
    const QSize wanted = m_resolution->currentData().toSize();
    if (wanted == QSize(1, 1)) {
        s.width = m_width->value() - (m_width->value() % 2);
        s.height = m_height->value() - (m_height->value() % 2);
    } else {
        s.width = wanted.isValid() ? wanted.width() : 0;
        s.height = wanted.isValid() ? wanted.height() : 0;
    }
    s.bitrateKbps = m_qualityCap->isChecked() ? int(std::lround(m_cap->value() * 1000.)) : 0;
    s.profile = m_profile->currentData().toString();
    s.keyframes = m_keysEvery->isChecked() ? m_keys->value() : 0;
    s.reorder = m_reorder->isChecked();
    s.subtitles = m_subtitles->currentData().toString();
    s.audio = m_audio->isChecked() && s.container != QLatin1String("gif") && s.container != QLatin1String("png");
    s.audioCodec = s.audio ? m_audioCodec->currentData().toString() : QString();
    s.audioKbps = m_audioRate->currentData().toInt();
    s.sampleRate = m_sampleRate->currentData().toInt();
    s.channels = m_channels->currentData().toInt();
    s.normalize = m_normalize->isChecked();
    s.lufs = m_lufs->value();
    return s;
}

void KoraCutExportSettingsPanel::updateEnabled()
{
    const QString container = m_container->currentData().toString();
    const bool soundOnly = container == QLatin1String("wav") || container == QLatin1String("m4a") || container == QLatin1String("mp3");
    const bool video = m_video->isChecked() && !soundOnly;
    const QString codec = m_codec->currentData().toString();
    const bool h26x = codec == QLatin1String("h264") || codec == QLatin1String("h265");
    for (QWidget *w : std::initializer_list<QWidget *>{m_codec, m_resolution, m_subtitles}) {
        w->setEnabled(video);
    }
    m_hardware->setEnabled(video && h26x && hardwareAvailable(codec));
    const bool custom = m_resolution->currentData().toSize() == QSize(1, 1);
    m_width->setVisible(custom);
    m_height->setVisible(custom);
    if (auto *times = findChild<QLabel *>(QStringLiteral("koracutExportTimes"))) {
        times->setVisible(custom);
    }
    for (QWidget *w : std::initializer_list<QWidget *>{m_qualityAuto, m_qualityCap, m_profile, m_keysAuto, m_keysEvery, m_reorder}) {
        w->setEnabled(video && h26x);
    }
    m_cap->setEnabled(video && h26x && m_qualityCap->isChecked());
    m_keys->setEnabled(video && h26x && m_keysEvery->isChecked());
    const bool audio = m_audio->isChecked() && container != QLatin1String("gif") && container != QLatin1String("png");
    const QString acodec = m_audioCodec->currentData().toString();
    m_audioCodec->setEnabled(audio);
    m_audioRate->setEnabled(audio && !acodec.startsWith(QLatin1String("pcm")));
    m_sampleRate->setEnabled(audio);
    m_channels->setEnabled(audio);
    m_normalize->setEnabled(audio);
    m_lufs->setEnabled(audio && m_normalize->isChecked());
}

void KoraCutExportSettingsPanel::refreshSummary()
{
    if (!pCore || !pCore->currentDoc()) {
        return;
    }
    const Settings s = read();
    const Model *m = model(s.model);
    const QSize seq = pCore->getCurrentFrameSize();
    const double fps = pCore->getCurrentFps();
    const int duration = pCore->projectDuration();
    int frames = duration;
    Monitor *monitor = pCore->getMonitor(Kdenlive::ProjectMonitor);
    const bool zone = monitor && monitor->getZoneEnd() > monitor->getZoneStart();
    if (s.inOut && zone) {
        frames = monitor->getZoneEnd() - monitor->getZoneStart();
    }
    m_inOut->setEnabled(zone || s.inOut);
    m_rangeInfo->setText(s.inOut && !zone ? i18nc("@info", "No In/Out range on the timeline: set one with I and O.")
                                          : i18nc("@info duration of the export", "Duration %1", timecode(std::max(0, frames))));
    m_fps->setText(i18nc("@info frame rate, %1 value", "Same as the sequence · %1 fps", QLocale().toString(fps, 'g', 4)));
    const QString path = outputPath(s, currentProjectName(), currentSequenceName());
    m_namePreview->setText(QFileInfo(path).fileName());
    m_namePreview->setToolTip(path);
    const double seconds = frames / std::max(1., fps);
    const double mb = s.estimatedKbps(seq, fps) * seconds / 8. / 1000.;
    m_estimate->setText(i18nc("@info estimated size of the exported file", "Estimated file size  <b>%1</b>", humanSize(std::max(0.1, mb))));
    // Les avertissements, comme la fenêtre Nouveau projet : court, précis, jamais bloquant.
    QStringList warn;
    if (m && !modelFits(*m, seq) && s.video) {
        warn << i18nc("@info", "%1 expects %2 × %3, the sequence is %4 × %5: the file keeps the sequence shape. For a true vertical video, work in a vertical sequence.",
                      m->name, m->width, m->height, seq.width(), seq.height());
    }
    if (m && m->maxSeconds > 0 && seconds > m->maxSeconds) {
        warn << i18nc("@info", "%1 accepts %2 at most, this export lasts %3.", m->name, humanDuration(m->maxSeconds), humanDuration(int(seconds)));
    }
    if (m && m->maxMegabytes > 0 && mb > m->maxMegabytes) {
        warn << i18nc("@info", "%1 accepts files up to %2.", m->name, humanSize(m->maxMegabytes));
    }
    if (m && m->maxFps > 0 && fps > m->maxFps + 0.5) {
        warn << i18nc("@info", "%1 shows %2 fps at most, the sequence runs at %3.", m->name, m->maxFps, QLocale().toString(fps, 'g', 4));
    }
    m_warnings->setText(warn.join(QLatin1Char('\n')));
    m_warnings->setVisible(!warn.isEmpty());
    QPalette p = m_warnings->palette();
    p.setColor(QPalette::WindowText, tok("jaune"));
    m_warnings->setPalette(p);
    m_guides->setVisible(m && !m->guides.isEmpty() && s.video);
    const bool can = duration > 0 && !(s.inOut && !zone);
    m_add->setEnabled(can);
    m_now->setEnabled(can);
}

// -----------------------------------------------------------------------------------------------
// La file de rendu

KoraCutExportQueuePanel::KoraCutExportQueuePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("koracutExportQueue"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(m_scroll, 1);

    auto *foot = new QWidget(this);
    auto *fv = new QVBoxLayout(foot);
    fv->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone);
    fv->setSpacing(KoraCutMetrics::SpaceGroup);
    m_status = new QLabel(foot);
    m_status->setWordWrap(true);
    fv->addWidget(m_status);
    // La barre de la file entière (Exportation en cours, comme Resolve).
    m_bar = new ProgressLine(foot);
    m_bar->setFixedHeight(4);
    fv->addWidget(m_bar);
    auto *row = new QHBoxLayout();
    m_clear = new QPushButton(i18nc("@action:button remove finished renders from the list", "Clear Finished"), foot);
    m_stop = new QPushButton(i18nc("@action:button", "Stop"), foot);
    m_all = new QPushButton(i18nc("@action:button start every render of the queue", "Render All"), foot);
    m_all->setObjectName(QStringLiteral("koracutExportRenderAll"));
    m_all->setDefault(true);
    connect(m_clear, &QPushButton::clicked, this, []() { KoraCutExportQueue::self().clearFinished(); });
    connect(m_stop, &QPushButton::clicked, this, []() {
        for (const auto &j : KoraCutExportQueue::self().jobs()) {
            if (j->state == QLatin1String("en_cours")) {
                KoraCutExportQueue::self().abort(j);
            }
        }
    });
    connect(m_all, &QPushButton::clicked, this, []() { KoraCutExportQueue::self().startAll(); });
    row->addWidget(m_clear);
    row->addStretch(1);
    row->addWidget(m_stop);
    row->addWidget(m_all);
    fv->addLayout(row);
    outer->addWidget(foot);

    // Les cartes se refont quand la liste change de forme ; la progression ne fait que repeindre.
    connect(&KoraCutExportQueue::self(), &KoraCutExportQueue::changed, this, [this]() {
        int shape = 0;
        for (const auto &j : KoraCutExportQueue::self().jobs()) {
            shape = shape * 7 + int(qHash(j->state) % 7) + 1;
        }
        shape += int(KoraCutExportQueue::self().jobs().size()) * 1000003;
        if (shape != m_builtCount) {
            rebuild();
        } else {
            for (QWidget *card : m_list->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
                card->update();
            }
            refreshTotals();
        }
    });
    // Le temps restant bouge sans nouvelle du programme de rendu : une seconde, seulement pendant un rendu.
    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, [this]() {
        if (!isVisible() || KoraCutExportQueue::self().count(QStringLiteral("en_cours")) == 0) {
            m_tick->stop();
            return;
        }
        for (QWidget *card : m_list->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            card->update();
        }
        refreshTotals();
    });
    rebuild();
}

void KoraCutExportQueuePanel::rebuild()
{
    const auto &jobs = KoraCutExportQueue::self().jobs();
    int shape = 0;
    for (const auto &j : jobs) {
        shape = shape * 7 + int(qHash(j->state) % 7) + 1;
    }
    m_builtCount = shape + int(jobs.size()) * 1000003;
    m_list = new QWidget();
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceGroup);
    m_listLayout->setSpacing(KoraCutMetrics::SpaceGroup);
    int rank = 0;
    for (const auto &j : jobs) {
        m_listLayout->addWidget(new JobCard(j, ++rank, m_list));
    }
    if (jobs.empty()) {
        // L'état vide de la maquette, dessiné comme ceux du chutier.
        class Empty : public QWidget
        {
        public:
            using QWidget::QWidget;

        protected:
            void paintEvent(QPaintEvent *) override
            {
                QPainter p(this);
                KoraCutEmptyState::paint(p, rect(), palette(),
                                         KoraCutEmptyState::Modele{QStringLiteral("media-playback-start"), i18nc("@info empty render queue", "The render queue is empty"),
                                                                   i18nc("@info", "Choose a model, then “Add to Queue” or “Export Now”."), QString()});
            }
        };
        auto *empty = new Empty(m_list);
        empty->setMinimumHeight(220);
        m_listLayout->addWidget(empty, 1);
    }
    m_listLayout->addStretch(1);
    m_scroll->setWidget(m_list);
    refreshTotals();
    if (KoraCutExportQueue::self().count(QStringLiteral("en_cours")) > 0 && !m_tick->isActive()) {
        m_tick->start();
    }
}

void KoraCutExportQueuePanel::refreshTotals()
{
    const auto &q = KoraCutExportQueue::self();
    const int running = q.count(QStringLiteral("en_cours"));
    const int waiting = q.count(QStringLiteral("en_attente"));
    const int done = q.count(QStringLiteral("termine"));
    const int total = int(q.jobs().size());
    int remaining = 0;
    bool known = true;
    double progress = 0.;
    int counted = 0;
    for (const auto &j : q.jobs()) {
        if (j->state == QLatin1String("en_cours") || j->state == QLatin1String("en_attente")) {
            const int r = q.remainingSeconds(j);
            if (r < 0) {
                known = false;
            } else {
                remaining += r;
            }
            progress += j->progress;
            ++counted;
        }
    }
    if (running > 0) {
        m_status->setText(known ? i18nc("@info render queue status", "Rendering · job %1 of %2 · %3 left", done + 1, done + running + waiting, humanDuration(remaining))
                                : i18nc("@info render queue status", "Rendering · job %1 of %2", done + 1, done + running + waiting));
    } else if (waiting > 0) {
        m_status->setText(i18ncp("@info render queue status", "%1 export waits for “Render All”.", "%1 exports wait for “Render All”.", waiting));
    } else if (total > 0) {
        m_status->setText(i18ncp("@info render queue status", "%1 file exported.", "%1 files exported.", done));
    } else {
        m_status->clear();
    }
    m_status->setVisible(!m_status->text().isEmpty());
    const double overall = counted > 0 ? progress / counted / 100. : 0.;
    m_bar->setProperty("progress", running > 0 ? overall : -1.);
    m_bar->setVisible(running > 0);
    m_bar->update();
    m_all->setEnabled(waiting > 0 && running == 0);
    m_stop->setEnabled(running > 0);
    m_clear->setEnabled(done > 0);
}

void KoraCutExportQueuePanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange) {
        update();
    }
    QWidget::changeEvent(event);
}

void KoraCutExportQueuePanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (KoraCutExportQueue::self().count(QStringLiteral("en_cours")) > 0) {
        m_tick->start();
    }
}

// -----------------------------------------------------------------------------------------------
// Les pseudo-actions de capture

void KoraCutDeliverPage::shotAction(const QString &command)
{
    KoraCutDeliverPage *page = instance();
    if (!page) {
        return;
    }
    const QString key = command.section(QLatin1Char('='), 0, 0);
    const QString value = command.section(QLatin1Char('='), 1);
    if (key == QLatin1String("modele")) {
        page->applyModel(value);
    } else if (key == QLatin1String("onglet") && page->m_panel) {
        page->m_panel->setTab(value == QLatin1String("audio") ? 1 : (value == QLatin1String("fichier") ? 2 : 0));
    } else if (key == QLatin1String("ajouter") || key == QLatin1String("exporter")) {
        const Enqueued r = page->render(key == QLatin1String("ajouter"));
        qWarning().noquote() << "KORACUT-LIVRAISON" << key << (r.ok ? QStringLiteral("ok") : r.code) << r.path << r.message;
    } else if (key == QLatin1String("tout")) {
        KoraCutExportQueue::self().startAll();
    } else if (key == QLatin1String("reglages")) {
        // « livraison:reglages=<json> » : fusionne des réglages (clés de Settings::toJson).
        QJsonObject o = page->m_settings.toJson();
        const QJsonObject extra = QJsonDocument::fromJson(value.toUtf8()).object();
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            o.insert(it.key(), it.value());
        }
        page->setSettings(Settings::fromJson(o), false);
    } else if (key == QLatin1String("recharger")) {
        page->reload();
    } else if (key == QLatin1String("partager")) {
        // « livraison:partager[=<fichier>] » : la fenêtre Partager, sur ce fichier ou sur le dernier export fini.
        QString path = value;
        if (path.isEmpty()) {
            for (const auto &j : KoraCutExportQueue::self().jobs()) {
                if (j->state == QLatin1String("termine")) {
                    path = j->path;
                }
            }
        }
        QDialog *d = showShareDialog(path.isEmpty() ? QStringLiteral("export.mp4") : path, page->m_queue ? page->m_queue->window() : nullptr);
        const QString shot = qEnvironmentVariable("KORACUT_SHOT");
        if (!shot.isEmpty()) {
            // La fenêtre est à part : on la capture elle-même, dans <capture>-partager.png.
            QTimer::singleShot(400, d, [d, shot]() { d->grab().save(QFileInfo(shot).path() + QLatin1Char('/') + QFileInfo(shot).completeBaseName() + QStringLiteral("-partager.png")); });
        }
        qWarning().noquote() << "KORACUT-LIVRAISON partager" << shareText();
    }
    // Chaque réponse : les réglages courants et l'état de la file, une ligne chacun.
    qWarning().noquote() << "KORACUT-LIVRAISON reglages" << QString::fromUtf8(QJsonDocument(page->m_settings.toJson()).toJson(QJsonDocument::Compact));
    QJsonArray jobs;
    for (const auto &j : KoraCutExportQueue::self().jobs()) {
        jobs.append(KoraCutExportQueue::self().json(j));
    }
    qWarning().noquote() << "KORACUT-LIVRAISON file" << QString::fromUtf8(QJsonDocument(jobs).toJson(QJsonDocument::Compact));
}
