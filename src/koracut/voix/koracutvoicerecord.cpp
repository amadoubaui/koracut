/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutvoicerecord.h"
#include "../koracutfonts.h"

#include "capture/mediacapture.h"
#include "core.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QPermissions>
#include <QUrl>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QAbstractButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QPointer<KoraCutVoiceRecord> s_panel;

KConfigGroup reglages()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
}

QString duree(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

/** Le niveau du micro : une barre fine, verte, jaune près du rouge, rouge à la saturation. */
class KoraCutVoiceLevel : public QWidget
{
public:
    explicit KoraCutVoiceLevel(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(6);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setDb(qreal db)
    {
        const qreal v = qBound(0.0, (db + 60.0) / 60.0, 1.0);
        if (!qFuzzyCompare(v + 1.0, m_value + 1.0)) {
            m_value = v;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect();
        QColor fond = palette().color(QPalette::Text);
        fond.setAlphaF(0.12);
        p.setPen(Qt::NoPen);
        p.setBrush(fond);
        p.drawRoundedRect(r, 3, 3);
        if (m_value > 0) {
            const QColor couleur = m_value > 0.95 ? QColor(0xFF7262) : m_value > 0.8 ? QColor(0xFFD166) : QColor(0x4ADE80);
            p.setBrush(couleur);
            p.drawRoundedRect(QRectF(r.left(), r.top(), r.width() * m_value, r.height()), 3, 3);
        }
    }

private:
    qreal m_value{0};
};

/** Le bouton rouge : un disque qui devient un carré arrondi pendant l'enregistrement (comme CapCut). */
class KoraCutRecordDisc : public QAbstractButton
{
public:
    explicit KoraCutRecordDisc(QWidget *parent)
        : QAbstractButton(parent)
    {
        setFixedSize(60, 60);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }
    void setRecording(bool on)
    {
        m_recording = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c = QRectF(rect()).center();
        const QColor rouge(underMouse() ? 0xFF5A52 : 0xFF3B30);
        QColor anneau = m_recording ? rouge : palette().color(QPalette::WindowText);
        if (!m_recording) {
            anneau.setAlphaF(hasFocus() ? 0.6 : 0.25);
        }
        p.setPen(QPen(anneau, 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 27, 27);
        p.setPen(Qt::NoPen);
        p.setBrush(rouge);
        if (m_recording) {
            p.drawRoundedRect(QRectF(c.x() - 10, c.y() - 10, 20, 20), 4, 4);
        } else {
            p.drawEllipse(c, 21, 21);
        }
    }
    void enterEvent(QEnterEvent *e) override
    {
        QAbstractButton::enterEvent(e);
        update();
    }
    void leaveEvent(QEvent *e) override
    {
        QAbstractButton::leaveEvent(e);
        update();
    }

private:
    bool m_recording{false};
};

KoraCutVoiceRecord *KoraCutVoiceRecord::instance(QWidget *window)
{
    if (!s_panel && window) {
        s_panel = new KoraCutVoiceRecord(window);
    }
    return s_panel.data();
}

KoraCutVoiceRecord::KoraCutVoiceRecord(QWidget *window)
    : QFrame(window, Qt::Tool | Qt::FramelessWindowHint)
    , m_window(window)
{
    setObjectName(QStringLiteral("koracutVoiceRecord"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(300);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 12, 16, 14);
    lay->setSpacing(10);

    auto *tete = new QHBoxLayout;
    auto *titre = new QLabel(i18nc("@title panel to record a voice-over with the microphone", "Voice-over"), this);
    QFont f = titre->font();
    f.setBold(true);
    titre->setFont(f);
    tete->addWidget(titre, 1);
    auto *fermer = new QToolButton(this);
    fermer->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    fermer->setAutoRaise(true);
    fermer->setToolTip(i18nc("@info:tooltip", "Close"));
    connect(fermer, &QToolButton::clicked, this, &QWidget::hide);
    tete->addWidget(fermer);
    lay->addLayout(tete);

    auto *micro = new QLabel(i18nc("@label:listbox audio input device", "Microphone"), this);
    lay->addWidget(micro);
    m_device = new QComboBox(this);
    m_device->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    lay->addWidget(m_device);
    connect(m_device, &QComboBox::activated, this, [this](int) {
        KdenliveSettings::setDefaultaudiocapture(m_device->currentText());
        if (auto capture = pCore->getAudioDevice()) {
            capture->setAudioCaptureDevice();
        }
    });
    m_level = new KoraCutVoiceLevel(this);
    lay->addWidget(m_level);

    m_mute = new QCheckBox(i18nc("@option:check", "Mute the project while recording"), this);
    m_mute->setToolTip(i18nc("@info:tooltip", "What plays in the timeline does not come out of the speakers while you talk, so the microphone does not pick it up."));
    m_mute->setChecked(reglages().readEntry("voixOffCouperSon", true));
    connect(m_mute, &QCheckBox::toggled, this, [this](bool on) {
        reglages().writeEntry("voixOffCouperSon", on);
        if (m_active && pCore->monitorManager()->projectMonitor()) {
            pCore->monitorManager()->projectMonitor()->mute(on);
            m_muted = on;
        }
    });
    lay->addWidget(m_mute);

    m_countdown = new QCheckBox(i18nc("@option:check", "3-second countdown"), this);
    m_countdown->setChecked(!KdenliveSettings::disablereccountdown());
    connect(m_countdown, &QCheckBox::toggled, this, [](bool on) { KdenliveSettings::setDisablereccountdown(!on); });
    lay->addWidget(m_countdown);

    auto *centre = new QVBoxLayout;
    centre->setSpacing(4);
    m_record = new KoraCutRecordDisc(this);
    m_record->setObjectName(QStringLiteral("koracutVoiceRecordButton"));
    m_record->setAccessibleName(i18nc("@action:button", "Record"));
    connect(m_record, &QAbstractButton::clicked, this, &KoraCutVoiceRecord::startOrStop);
    centre->addWidget(m_record, 0, Qt::AlignHCenter);
    m_time = new QLabel(QStringLiteral("00:00"), this);
    m_time->setAlignment(Qt::AlignCenter);
    m_time->setFont(KoraCutFonts::numbers(KoraCutMetrics::FontBase));
    centre->addWidget(m_time);
    m_state = new QLabel(this);
    m_state->setAlignment(Qt::AlignCenter);
    m_state->setWordWrap(true);
    centre->addWidget(m_state);
    lay->addLayout(centre);

    auto *aide = new QLabel(i18nc("@info", "The voice lands at the playhead, on a free voice track (or a new one)."), this);
    aide->setWordWrap(true);
    QPalette pal = aide->palette();
    QColor gris = pal.color(QPalette::WindowText);
    gris.setAlphaF(0.6);
    pal.setColor(QPalette::WindowText, gris);
    aide->setPalette(pal);
    lay->addWidget(aide);

    m_timer = new QTimer(this);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, &KoraCutVoiceRecord::tick);

    if (auto capture = pCore->getAudioDevice()) {
        connect(capture.get(), &MediaCapture::recordStateChanged, this, &KoraCutVoiceRecord::recordStateChanged);
        connect(capture.get(), &MediaCapture::levelsChanged, this, [this]() {
            if (!m_active || !isVisible()) {
                return;
            }
            const QVector<qreal> niveaux = pCore->getAudioDevice()->levels();
            qreal max = -100;
            for (qreal v : niveaux) {
                max = qMax(max, v);
            }
            m_level->setDb(max);
        });
    }
    updateUi();
}

bool KoraCutVoiceRecord::isRecording() const
{
    return m_active;
}

void KoraCutVoiceRecord::refreshDevices()
{
    const QStringList devices = pCore->getAudioCaptureDevices();
    const QString courant = KdenliveSettings::defaultaudiocapture();
    m_device->clear();
    m_device->addItems(devices);
    const int i = devices.indexOf(courant);
    m_device->setCurrentIndex(i >= 0 ? i : 0);
    m_device->setEnabled(!devices.isEmpty() && !m_active);
    if (devices.isEmpty()) {
        m_device->addItem(i18nc("@item:inlistbox", "No microphone found"));
    }
}

void KoraCutVoiceRecord::toggleNear(QWidget *anchor)
{
    if (isVisible() && !m_active) {
        hide();
        return;
    }
    refreshDevices();
    m_countdown->setChecked(!KdenliveSettings::disablereccountdown());
    updateUi();
    placeNear(anchor);
    show();
    raise();
    activateWindow();
    m_record->setFocus();
}

void KoraCutVoiceRecord::placeNear(QWidget *anchor)
{
    adjustSize();
    QPoint haut;
    if (anchor) {
        // Au-dessus du bouton (la barre de la timeline est en bas de la fenêtre), aligné à droite du bouton.
        const QPoint coin = anchor->mapToGlobal(QPoint(anchor->width(), 0));
        haut = QPoint(coin.x() - width(), coin.y() - height() - 6);
    } else if (m_window) {
        haut = m_window->mapToGlobal(QPoint(m_window->width() / 2 - width() / 2, m_window->height() / 2 - height() / 2));
    }
    const QScreen *ecran = anchor ? anchor->screen() : QGuiApplication::primaryScreen();
    if (ecran) {
        const QRect zone = ecran->availableGeometry();
        haut.setX(qBound(zone.left() + 8, haut.x(), zone.right() - width() - 8));
        haut.setY(qBound(zone.top() + 8, haut.y(), zone.bottom() - height() - 8));
    }
    move(haut);
}

void KoraCutVoiceRecord::startOrStop()
{
    if (m_active) {
        stop();
    } else {
        start();
    }
}

void KoraCutVoiceRecord::start()
{
    auto *main = pCore->window();
    if (!main || !main->getCurrentTimeline() || !main->getCurrentTimeline()->controller()) {
        pCore->displayMessage(i18n("Open a project to record a voice-over"), ErrorMessage, 800);
        return;
    }
    if (pCore->isMediaCapturing()) {
        return;
    }
#if QT_CONFIG(permissions)
    // Le micro : la première fois, le système demande. On attend la réponse et on enregistre dès qu'elle
    // est oui (le moteur de Kdenlive, lui, abandonnait sans rien dire). Refusé : on dit où le rétablir.
    QMicrophonePermission micro;
    switch (qApp->checkPermission(micro)) {
    case Qt::PermissionStatus::Undetermined:
        m_state->setText(i18nc("@info", "Allow the microphone in the system window…"));
        qApp->requestPermission(micro, this, [this](const QPermission &reponse) {
            if (reponse.status() == Qt::PermissionStatus::Granted) {
                refreshDevices();
                start();
            } else {
                updateUi();
                m_state->setText(i18nc("@info", "The microphone is not allowed for KoraCut."));
            }
        });
        return;
    case Qt::PermissionStatus::Denied:
        m_state->setText(i18nc("@info", "The microphone is not allowed for KoraCut."));
#ifdef Q_OS_MAC
        QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone")));
        pCore->displayMessage(i18n("Allow KoraCut in System Settings, Privacy & Security, Microphone, then record again."), InformationMessage, 1200);
#endif
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
#endif
    TimelineController *ctl = main->getCurrentTimeline()->controller();
    m_track = ctl->koraCutVoiceOverTrack();
    if (m_track < 0) {
        pCore->displayMessage(i18n("No audio track can take the voice-over here"), ErrorMessage, 800);
        return;
    }
    m_active = true;
    m_capturing = false;
    m_since.start();
    if (m_mute->isChecked()) {
        if (Monitor *projet = pCore->monitorManager()->projectMonitor()) {
            projet->mute(true);
            m_muted = true;
        }
    }
    m_timer->start();
    updateUi();
    Q_EMIT recordingChanged(true);
    // Si le moteur refuse (piste verrouillée, place trop courte, micro pas encore autorisé), rien ne
    // commence : tick() le voit et remet le panneau au repos.
    ctl->switchRecording(m_track, true);
}

void KoraCutVoiceRecord::stop()
{
    auto *main = pCore->window();
    if (main && main->getCurrentTimeline() && main->getCurrentTimeline()->controller()) {
        main->getCurrentTimeline()->controller()->switchRecording(m_track, false);
    }
    finish();
}

void KoraCutVoiceRecord::recordStateChanged(int tid, bool recording)
{
    Q_UNUSED(tid)
    if (!m_active) {
        return;
    }
    if (recording) {
        m_capturing = true;
        m_since.start();
        // La lecture vient de (re)démarrer : le moniteur a repris son volume, on le recoupe.
        if (m_muted && pCore->monitorManager()->projectMonitor()) {
            pCore->monitorManager()->projectMonitor()->mute(true);
        }
        updateUi();
    } else if (m_capturing) {
        finish();
    }
}

void KoraCutVoiceRecord::tick()
{
    if (!m_active) {
        m_timer->stop();
        return;
    }
    auto capture = pCore->getAudioDevice();
    const auto statut = capture ? capture->recordStatus() : MediaCapture::RecordReady;
    // Rien n'a commencé, ou compte à rebours annulé ailleurs (Échap sur le moniteur) : on revient au repos.
    if (!m_capturing && statut != MediaCapture::RecordShowingCountDown && !pCore->isMediaCapturing() && m_since.elapsed() > 1500) {
        finish();
        return;
    }
    if (m_muted && pCore->monitorManager()->projectMonitor()) {
        pCore->monitorManager()->projectMonitor()->mute(true);
    }
    if (m_capturing && m_since.isValid()) {
        m_time->setText(duree(m_since.elapsed()));
    }
}

void KoraCutVoiceRecord::finish()
{
    const bool etait = m_active;
    m_active = false;
    m_capturing = false;
    m_timer->stop();
    m_level->setDb(-100);
    if (m_muted) {
        if (Monitor *projet = pCore->monitorManager()->projectMonitor()) {
            projet->mute(false);
        }
        m_muted = false;
    }
    updateUi();
    if (etait) {
        Q_EMIT recordingChanged(false);
    }
}

void KoraCutVoiceRecord::updateUi()
{
    const bool enregistre = m_active && m_capturing;
    static_cast<KoraCutRecordDisc *>(m_record)->setRecording(m_active);
    m_record->setToolTip(m_active ? i18nc("@info:tooltip", "Stop (Esc)") : i18nc("@info:tooltip", "Record (Space)"));
    m_record->setAccessibleName(m_active ? i18nc("@action:button", "Stop") : i18nc("@action:button", "Record"));
    if (!m_active) {
        m_time->setText(QStringLiteral("00:00"));
        m_state->setText(i18nc("@info", "Click to record"));
    } else if (!enregistre) {
        m_state->setText(i18nc("@info", "Get ready…"));
    } else {
        m_state->setText(i18nc("@info", "Recording · click to stop"));
    }
    m_device->setEnabled(!m_active && m_device->count() > 0);
    m_countdown->setEnabled(!m_active);
    update();
}

void KoraCutVoiceRecord::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_active) {
            stop();
        } else {
            hide();
        }
        return;
    }
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        startOrStop();
        return;
    }
    QFrame::keyPressEvent(event);
}

void KoraCutVoiceRecord::hideEvent(QHideEvent *event)
{
    // Fermer le panneau n'arrête pas un enregistrement en cours : le micro de la barre reste rouge et l'arrête.
    QFrame::hideEvent(event);
}

void KoraCutVoiceRecord::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Le fond : surface de la charte, rayon 12, un trait fin.
    QColor fond = palette().color(QPalette::Window);
    QColor trait = palette().color(QPalette::WindowText);
    trait.setAlphaF(0.14);
    p.setPen(QPen(trait, 1));
    p.setBrush(fond);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);

}
