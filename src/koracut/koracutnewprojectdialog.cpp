/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutnewprojectdialog.h"

#include "kdenlivesettings.h"

#include <KColorScheme>
#include <KLocalizedString>
#include <QAbstractListModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace KoraCutVideoTypes;

namespace {

// Une couleur par famille, pour la vignette du format : douce, reconnaissable, jamais criarde.
QColor familyColor(const QString &code)
{
    static const QHash<QString, QColor> colors = {
        {QStringLiteral("F01"), QColor(0x8B, 0x5C, 0xF6)}, {QStringLiteral("F02"), QColor(0xF4, 0x72, 0xB6)}, {QStringLiteral("F03"), QColor(0x14, 0xA3, 0x7F)},
        {QStringLiteral("F04"), QColor(0xF5, 0x9E, 0x0B)}, {QStringLiteral("F05"), QColor(0x0D, 0x99, 0xFF)}, {QStringLiteral("F06"), QColor(0x22, 0xC5, 0x5E)},
        {QStringLiteral("F07"), QColor(0xEF, 0x44, 0x44)}, {QStringLiteral("F08"), QColor(0x64, 0x74, 0x8B)}, {QStringLiteral("F09"), QColor(0xE0, 0x7B, 0x39)},
        {QStringLiteral("F10"), QColor(0xA8, 0x55, 0xF7)}, {QStringLiteral("F11"), QColor(0x06, 0xB6, 0xD4)}, {QStringLiteral("F12"), QColor(0xEA, 0xB3, 0x08)},
        {QStringLiteral("F13"), QColor(0x9C, 0xA3, 0xAF)},
    };
    return colors.value(code, QColor(0x0D, 0x99, 0xFF));
}

// Le fond de la vignette : #1E1E1E en sombre, un gris clair en clair (25 septembre 2026 : en thème
// clair, les vignettes restaient des pavés noirs sur fond blanc).
QColor thumbBackground(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() > 128 ? QColor(0xE4, 0xE4, 0xE4) : QColor(0x1E, 0x1E, 0x1E);
}

// La vignette d'un format : le cadre de l'image, à ses proportions, dans la couleur de la famille.
void paintFrame(QPainter *painter, const QRect &box, const QString &format, const QColor &color, const QString &ratioText)
{
    const QSize frame = frameSize(format);
    const qreal scale = qMin(box.width() * 0.62 / frame.width(), box.height() * 0.78 / frame.height());
    const QSizeF size(frame.width() * scale, frame.height() * scale);
    const QRectF rect(box.center().x() - size.width() / 2, box.center().y() - size.height() / 2, size.width(), size.height());
    QLinearGradient g(rect.topLeft(), rect.bottomRight());
    g.setColorAt(0, color.lighter(115));
    g.setColorAt(1, color.darker(140));
    painter->setPen(Qt::NoPen);
    painter->setBrush(g);
    painter->drawRoundedRect(rect, 4, 4);
    // Le repère du format dans le cadre, en petit : 16:9, 9:16, 1:1, 4:5.
    painter->setPen(QColor(255, 255, 255, 220));
    QFont f = painter->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(rect, Qt::AlignCenter, ratioText);
}

QString ratioOf(const QString &format)
{
    if (format == QLatin1String("vertical")) {
        return QStringLiteral("9:16");
    }
    if (format == QLatin1String("carre")) {
        return QStringLiteral("1:1");
    }
    if (format == QLatin1String("portrait")) {
        return QStringLiteral("4:5");
    }
    return QStringLiteral("16:9");
}

QString shortDuration(const QString &duration)
{
    // « Idéal 15-60 s (fourchette 8 s – 3 min) » -> « 15-60 s »
    QString d = duration;
    d.remove(QRegularExpression(QStringLiteral("\\(.*\\)")));
    d.replace(QStringLiteral("Idéal"), QString()).replace(QStringLiteral("idéal"), QString());
    return d.simplified();
}

} // namespace

/** @brief Les types de vidéos filtrés (recherche, famille), une ligne par type. */
class KoraCutTypeListModel : public QAbstractListModel
{
public:
    enum Roles { IdRole = Qt::UserRole + 1, FamilyRole, FormatRole, FpsRole, DurationRole, DescriptionRole };
    using QAbstractListModel::QAbstractListModel;

    void setFilter(const QString &search, const QString &family)
    {
        beginResetModel();
        m_rows.clear();
        const QString needle = searchable(search);
        for (const VideoType &t : all()) {
            if (!family.isEmpty() && t.family != family) {
                continue;
            }
            if (!needle.isEmpty()) {
                const QString hay = searchable(t.name + QLatin1Char(' ') + t.description + QLatin1Char(' ') + t.platforms);
                if (!hay.contains(needle)) {
                    continue;
                }
            }
            m_rows << &t;
        }
        endResetModel();
    }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size()) {
            return {};
        }
        const VideoType *t = m_rows.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return t->name;
        case IdRole:
            return t->id;
        case FamilyRole:
            return t->family;
        case FormatRole:
            return t->format;
        case FpsRole:
            return t->fps;
        case DurationRole:
            return shortDuration(t->duration);
        case DescriptionRole:
            return t->description;
        default:
            return {};
        }
    }

private:
    QList<const VideoType *> m_rows;
};

namespace {
/** @brief Une carte de type : la vignette du format, le nom, le format et la durée, la description. */
class TypeCardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static QSize cardSize() { return {236, 176}; }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return cardSize(); }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect card = option.rect.adjusted(4, 4, -4, -4);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const QColor accent = KColorScheme(QPalette::Active, KColorScheme::Selection).background().color();
        painter->setPen(Qt::NoPen);
        // Le fond de la carte : un voile de la couleur du texte, qui se voit en clair comme en sombre.
        QColor veil = option.palette.color(QPalette::Text);
        veil.setAlpha(selected ? 22 : (hover ? 14 : 8));
        painter->setBrush(veil);
        painter->drawRoundedRect(card, 9, 9);
        if (selected) {
            painter->setPen(QPen(accent, 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(QRectF(card).adjusted(1, 1, -1, -1), 9, 9);
        }
        // La vignette : un fond sombre, le cadre du format dedans.
        const QRect thumb(card.left() + 8, card.top() + 8, card.width() - 16, 78);
        painter->setPen(Qt::NoPen);
        painter->setBrush(thumbBackground(option.palette));
        painter->drawRoundedRect(thumb, 6, 6);
        const QString format = index.data(KoraCutTypeListModel::FormatRole).toString();
        paintFrame(painter, thumb, format, familyColor(index.data(KoraCutTypeListModel::FamilyRole).toString()), ratioOf(format));
        // Le nom, gras, sur une ligne.
        QFont title = option.font;
        title.setBold(true);
        painter->setFont(title);
        painter->setPen(option.palette.color(QPalette::Text));
        const QRect nameRect(card.left() + 10, thumb.bottom() + 8, card.width() - 20, QFontMetrics(title).height());
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(title).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, nameRect.width()));
        // Le format, les images par seconde et la durée, en gris.
        QFont small = option.font;
        small.setPointSizeF(small.pointSizeF() * 0.92);
        painter->setFont(small);
        QColor muted = option.palette.color(QPalette::Text);
        muted.setAlphaF(0.62);
        painter->setPen(muted);
        const QString meta = QStringLiteral("%1 · %2 i/s · %3").arg(formatLabel(format)).arg(index.data(KoraCutTypeListModel::FpsRole).toInt()).arg(index.data(KoraCutTypeListModel::DurationRole).toString());
        const QRect metaRect(nameRect.left(), nameRect.bottom() + 2, nameRect.width(), QFontMetrics(small).height());
        painter->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(small).elidedText(meta, Qt::ElideRight, metaRect.width()));
        // La description, deux lignes au plus.
        const QRect descRect(nameRect.left(), metaRect.bottom() + 4, nameRect.width(), card.bottom() - metaRect.bottom() - 8);
        // Deux lignes au plus : la première coupée au mot, la seconde terminée par « … ».
        const QString description = index.data(KoraCutTypeListModel::DescriptionRole).toString();
        const QFontMetrics fm(small);
        const QStringList words = description.split(QLatin1Char(' '));
        QString line;
        int i = 0;
        for (; i < words.size(); ++i) {
            const QString candidate = line.isEmpty() ? words.at(i) : line + QLatin1Char(' ') + words.at(i);
            if (fm.horizontalAdvance(candidate) > descRect.width() && !line.isEmpty()) {
                break;
            }
            line = candidate;
        }
        painter->drawText(QRect(descRect.left(), descRect.top(), descRect.width(), fm.height()), Qt::AlignLeft | Qt::AlignTop, fm.elidedText(line, Qt::ElideRight, descRect.width()));
        const QString rest = words.mid(i).join(QLatin1Char(' '));
        if (!rest.isEmpty()) {
            painter->drawText(QRect(descRect.left(), descRect.top() + fm.height(), descRect.width(), fm.height()), Qt::AlignLeft | Qt::AlignTop,
                              fm.elidedText(rest, Qt::ElideRight, descRect.width()));
        }
        painter->restore();
    }
};
} // namespace

KoraCutNewProjectDialog::KoraCutNewProjectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "New Project"));
    setMinimumSize(860, 600);
    resize(980, 680);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_pages = new QStackedWidget(this);
    layout->addWidget(m_pages);
    buildChoosePage();
    buildReviewPage();
    m_pages->setCurrentIndex(0);
    // Outil de développement : KORACUT_SHOT_NEWPROJECT_TYPE=F04-13 ouvre directement l'étape 2 sur ce type.
    const QString devType = qEnvironmentVariable("KORACUT_SHOT_NEWPROJECT_TYPE");
    if (!devType.isEmpty()) {
        QTimer::singleShot(0, this, [this, devType]() { chooseType(devType); });
    }
}

void KoraCutNewProjectDialog::buildChoosePage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 20, 24, 16);
    lay->setSpacing(10);

    auto *title = new QLabel(i18nc("@title", "What kind of video are you making?"), page);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.5);
    tf.setBold(true);
    title->setFont(tf);
    lay->addWidget(title);
    auto *subtitle = new QLabel(i18nc("@info", "Pick a type: KoraCut sets the format, the frame rate, the tracks and the export for you. You can change everything on the next step."), page);
    subtitle->setWordWrap(true);
    subtitle->setEnabled(false);
    lay->addWidget(subtitle);

    m_search = new QLineEdit(page);
    m_search->setPlaceholderText(i18nc("@info:placeholder", "Search: podcast, reel, mariage, tutoriel, pub…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(32);
    lay->addWidget(m_search);

    // Les familles en pastilles, sur plusieurs lignes.
    auto *chips = new QWidget(page);
    auto *chipsLay = new QHBoxLayout(chips);
    chipsLay->setContentsMargins(0, 0, 0, 0);
    chipsLay->setSpacing(4);
    auto addChip = [this, chips, chipsLay](const QString &code, const QString &text) {
        auto *chip = new QToolButton(chips);
        chip->setObjectName(QStringLiteral("koracutCategoryChip"));
        chip->setText(text);
        chip->setCheckable(true);
        chip->setAutoRaise(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setChecked(code == m_family);
        connect(chip, &QToolButton::clicked, this, [this, code, chip]() {
            m_family = code;
            for (QToolButton *other : std::as_const(m_familyChips)) {
                other->setChecked(other == chip);
            }
            m_model->setFilter(m_search->text(), m_family);
        });
        chipsLay->addWidget(chip);
        m_familyChips << chip;
    };
    addChip(QString(), i18nc("@item family chip", "All"));
    for (const Family &f : families()) {
        // Nom court : la partie avant « : » ou « & », la pastille reste lisible.
        addChip(f.code, f.name.section(QLatin1Char(':'), 0, 0).section(QStringLiteral(" & "), 0, 0).trimmed());
    }
    chipsLay->addStretch();
    // Deux rangées : on coupe la liste en deux pour rester lisible à 900 px.
    auto *chipsWrap = new QVBoxLayout;
    chipsWrap->setSpacing(4);
    auto *row2 = new QWidget(page);
    auto *row2Lay = new QHBoxLayout(row2);
    row2Lay->setContentsMargins(0, 0, 0, 0);
    row2Lay->setSpacing(4);
    const int half = (m_familyChips.size() + 1) / 2;
    for (int i = half; i < m_familyChips.size(); ++i) {
        chipsLay->removeWidget(m_familyChips.at(i));
        row2Lay->addWidget(m_familyChips.at(i));
    }
    row2Lay->addStretch();
    chipsWrap->addWidget(chips);
    chipsWrap->addWidget(row2);
    lay->addLayout(chipsWrap);

    m_model = new KoraCutTypeListModel(this);
    m_model->setFilter(QString(), QString());
    m_cards = new QListView(page);
    m_cards->setModel(m_model);
    m_cards->setViewMode(QListView::IconMode);
    m_cards->setResizeMode(QListView::Adjust);
    m_cards->setMovement(QListView::Static);
    m_cards->setUniformItemSizes(true);
    m_cards->setSpacing(0);
    m_cards->setGridSize(TypeCardDelegate::cardSize() + QSize(4, 4));
    m_cards->setItemDelegate(new TypeCardDelegate(m_cards));
    m_cards->setMouseTracking(true);
    m_cards->setSelectionMode(QAbstractItemView::SingleSelection);
    m_cards->setFrameShape(QFrame::NoFrame);
    lay->addWidget(m_cards, 1);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { m_model->setFilter(text, m_family); });
    connect(m_cards, &QListView::activated, this, [this](const QModelIndex &ix) { chooseType(ix.data(KoraCutTypeListModel::IdRole).toString()); });
    connect(m_cards, &QListView::clicked, this, [this](const QModelIndex &ix) { chooseType(ix.data(KoraCutTypeListModel::IdRole).toString()); });

    auto *buttons = new QHBoxLayout;
    auto *advanced = new QPushButton(i18nc("@action:button", "Advanced settings…"), page);
    advanced->setFlat(true);
    connect(advanced, &QPushButton::clicked, this, [this]() {
        m_advanced = true;
        accept();
    });
    auto *blank = new QPushButton(i18nc("@action:button", "Free project"), page);
    blank->setToolTip(i18nc("@info:tooltip", "Your usual settings, no video type"));
    connect(blank, &QPushButton::clicked, this, [this]() { chooseType(QString()); });
    auto *cancel = new QPushButton(i18nc("@action:button", "Cancel"), page);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(advanced);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(blank);
    lay->addLayout(buttons);
    m_pages->addWidget(page);
}

void KoraCutNewProjectDialog::buildReviewPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 20, 24, 16);
    lay->setSpacing(12);

    m_typeTitle = new QLabel(page);
    QFont tf = m_typeTitle->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.5);
    tf.setBold(true);
    m_typeTitle->setFont(tf);
    lay->addWidget(m_typeTitle);
    m_typeDescription = new QLabel(page);
    m_typeDescription->setWordWrap(true);
    m_typeDescription->setEnabled(false);
    lay->addWidget(m_typeDescription);

    auto *columns = new QHBoxLayout;
    columns->setSpacing(24);
    // À gauche, la vignette du format et l'export ; à droite, les réglages.
    auto *left = new QVBoxLayout;
    m_preview = new QLabel(page);
    m_preview->setFixedSize(300, 190);
    left->addWidget(m_preview);
    m_export = new QLabel(page);
    m_export->setWordWrap(true);
    left->addWidget(m_export);
    m_tips = new QLabel(page);
    m_tips->setWordWrap(true);
    m_tips->setEnabled(false);
    left->addWidget(m_tips);
    left->addStretch();
    columns->addLayout(left, 0);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    m_name = new QLineEdit(page);
    m_name->setMinimumHeight(30);
    form->addRow(i18nc("@label:textbox", "Project name"), m_name);
    m_format = new QComboBox(page);
    for (const char *f : {"horizontal", "vertical", "carre", "portrait"}) {
        m_format->addItem(formatLabel(QString::fromLatin1(f)) + QStringLiteral("  ·  %1 × %2").arg(KoraCutVideoTypes::frameSize(QString::fromLatin1(f)).width()).arg(KoraCutVideoTypes::frameSize(QString::fromLatin1(f)).height()),
                          QString::fromLatin1(f));
    }
    m_format->addItem(i18nc("@item video format", "Horizontal 4K") + QStringLiteral("  ·  3840 × 2160"), QStringLiteral("4k"));
    form->addRow(i18nc("@label:listbox", "Format"), m_format);
    m_fps = new QComboBox(page);
    // Les cadences entières et celles de la télévision américaine (23,976, 29,97, 59,94).
    for (double fps : {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0}) {
        m_fps->addItem(QStringLiteral("%1 i/s").arg(QLocale().toString(fps, 'g', 5)), fps);
    }
    form->addRow(i18nc("@label:listbox", "Frames per second"), m_fps);
    auto *tracks = new QHBoxLayout;
    m_videoCount = new QSpinBox(page);
    m_videoCount->setRange(1, 20);
    m_videoCount->setSuffix(i18nc("@item suffix, video tracks", " video"));
    m_audioCount = new QSpinBox(page);
    m_audioCount->setRange(1, 20);
    m_audioCount->setSuffix(i18nc("@item suffix, audio tracks", " audio"));
    tracks->addWidget(m_videoCount);
    tracks->addWidget(m_audioCount);
    tracks->addStretch();
    form->addRow(i18nc("@label", "Tracks"), tracks);
    m_trackNames = new QLabel(page);
    m_trackNames->setWordWrap(true);
    m_trackNames->setEnabled(false);
    form->addRow(QString(), m_trackNames);
    columns->addLayout(form, 1);
    lay->addLayout(columns, 1);
    connect(m_videoCount, &QSpinBox::valueChanged, this, &KoraCutNewProjectDialog::refreshTracks);
    connect(m_audioCount, &QSpinBox::valueChanged, this, &KoraCutNewProjectDialog::refreshTracks);
    connect(m_format, &QComboBox::currentIndexChanged, this, &KoraCutNewProjectDialog::refreshReview);

    auto *buttons = new QHBoxLayout;
    auto *back = new QPushButton(i18nc("@action:button", "Back"), page);
    connect(back, &QPushButton::clicked, this, [this]() { m_pages->setCurrentIndex(0); });
    auto *start = new QPushButton(i18nc("@action:button", "Start Editing"), page);
    start->setDefault(true);
    start->setMinimumHeight(34);
    connect(start, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(back);
    buttons->addStretch();
    buttons->addWidget(start);
    lay->addLayout(buttons);
    m_pages->addWidget(page);
}

void KoraCutNewProjectDialog::chooseType(const QString &id)
{
    m_typeId = id;
    const VideoType *type = byId(id);
    if (type) {
        m_typeTitle->setText(type->name);
        m_typeDescription->setText(type->description);
        m_name->setText(type->name);
        m_format->setCurrentIndex(qMax(0, m_format->findData(type->format)));
        m_fps->setCurrentIndex(qMax(0, m_fps->findData(double(type->fps))));
        // Demande d'Amadou Ba du 23 septembre 2026 : dix pistes vidéo et dix pistes audio pour
        // tout projet, pas seulement les pistes que le guide nomme. Les noms du guide occupent
        // les premières, les autres restent V5, V6… et arrivent pliées.
        m_videoNames = type->videoTracks;
        m_audioNames = type->audioTracks;
        while (m_videoNames.size() < KdenliveSettings::videotracks()) {
            m_videoNames << QString();
        }
        while (m_audioNames.size() < KdenliveSettings::audiotracks()) {
            m_audioNames << QString();
        }
        m_exportLufs = type->exportLufs;
        m_exportCodec = type->exportCodec;
        QStringList tips;
        for (const QString &t : type->tips) {
            tips << QStringLiteral("• ") + t;
        }
        m_tips->setText(tips.isEmpty() ? QString() : i18nc("@info", "Editing tips for this type:") + QLatin1Char('\n') + tips.join(QLatin1Char('\n')));
    } else {
        m_typeTitle->setText(i18nc("@title", "Free project"));
        m_typeDescription->setText(i18nc("@info", "Your usual settings. You can still change them here."));
        m_name->setText(i18nc("@item default project name", "Untitled"));
        m_format->setCurrentIndex(0);
        m_fps->setCurrentIndex(qMax(0, m_fps->findData(25.0)));
        m_videoNames.clear();
        m_audioNames.clear();
        for (int i = 0; i < KdenliveSettings::videotracks(); ++i) {
            m_videoNames << QString();
        }
        for (int i = 0; i < KdenliveSettings::audiotracks(); ++i) {
            m_audioNames << QString();
        }
        m_exportLufs = -14;
        m_exportCodec = QStringLiteral("H.264");
        m_tips->clear();
    }
    {
        // Les deux compteurs d'un coup : poser le premier déclenchait refreshTracks(), qui rognait
        // les noms audio à l'ancienne valeur du second.
        const QSignalBlocker blockVideo(m_videoCount);
        const QSignalBlocker blockAudio(m_audioCount);
        m_videoCount->setValue(m_videoNames.size());
        m_audioCount->setValue(m_audioNames.size());
    }
    refreshTracks();
    refreshReview();
    m_pages->setCurrentIndex(1);
    m_name->setFocus();
    m_name->selectAll();
}

void KoraCutNewProjectDialog::refreshTracks()
{
    while (m_videoNames.size() < m_videoCount->value()) {
        m_videoNames << QString();
    }
    while (m_videoNames.size() > m_videoCount->value()) {
        m_videoNames.removeLast();
    }
    while (m_audioNames.size() < m_audioCount->value()) {
        m_audioNames << QString();
    }
    while (m_audioNames.size() > m_audioCount->value()) {
        m_audioNames.removeLast();
    }
    QStringList lines;
    for (int i = 0; i < m_videoNames.size(); ++i) {
        lines << QStringLiteral("V%1 %2").arg(i + 1).arg(m_videoNames.at(i));
    }
    for (int i = 0; i < m_audioNames.size(); ++i) {
        lines << QStringLiteral("A%1 %2").arg(i + 1).arg(m_audioNames.at(i));
    }
    // Le point reste collé au nom qui le précède (espace insécable) : à la coupure de ligne, il
    // finit la ligne au lieu d'ouvrir la suivante (« · A7 », vu le 25 septembre 2026).
    m_trackNames->setText(lines.join(QStringLiteral("\u00A0·  ")));
}

void KoraCutNewProjectDialog::refreshReview()
{
    const QString format = m_format->currentData().toString();
    const QSize frame = format == QLatin1String("4k") ? QSize(3840, 2160) : KoraCutVideoTypes::frameSize(format);
    // La vignette : le cadre du format, à ses proportions.
    QPixmap pix(m_preview->size() * m_preview->devicePixelRatioF());
    pix.setDevicePixelRatio(m_preview->devicePixelRatioF());
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(thumbBackground(palette()));
    p.drawRoundedRect(QRect(QPoint(0, 0), m_preview->size()), 9, 9);
    const VideoType *type = byId(m_typeId);
    paintFrame(&p, QRect(QPoint(0, 0), m_preview->size()), format == QLatin1String("4k") ? QStringLiteral("horizontal") : format,
               familyColor(type ? type->family : QString()), QStringLiteral("%1 × %2").arg(frame.width()).arg(frame.height()));
    p.end();
    m_preview->setPixmap(pix);
    // Une valeur et son unité ne se séparent pas : « -14 » en fin de ligne et « LUFS » au début de
    // la suivante se lisaient mal. L'espace devient insécable après traduction.
    QString exportText = i18nc("@info %1 codec, %2 loudness", "Export planned: MP4 %1, %2 × %3, sound at %4 LUFS.", m_exportCodec, frame.width(), frame.height(), m_exportLufs);
    exportText.replace(QStringLiteral(" LUFS"), QStringLiteral("\u00A0LUFS"));
    m_export->setText(exportText);
}

KoraCutProjectPlan KoraCutNewProjectDialog::plan() const
{
    KoraCutProjectPlan p;
    p.typeId = m_typeId;
    p.name = m_name->text().trimmed();
    const QString format = m_format->currentData().toString();
    p.frame = format == QLatin1String("4k") ? QSize(3840, 2160) : KoraCutVideoTypes::frameSize(format);
    p.setRate(m_fps->currentData().toDouble());
    p.videoTracks = m_videoNames;
    p.audioTracks = m_audioNames;
    p.exportLufs = m_exportLufs;
    p.exportCodec = m_exportCodec;
    return p;
}
