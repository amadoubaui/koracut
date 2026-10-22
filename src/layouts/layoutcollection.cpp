/*
SPDX-FileCopyrightText: 2012 Jean-Baptiste Mardelle <jb@kdenlive.org>
SPDX-FileCopyrightText: 2014 Till Theato <root@ttill.de>
SPDX-FileCopyrightText: 2020 Julius Künzel <julius.kuenzel@kde.org>
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "layouts/layoutcollection.h"
#include "kdenlivesettings.h"
#include <KConfigGroup>
#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <QHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include "koracut/koracutmodules.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

LayoutCollection::LayoutCollection()
{
    initTranslations();
}

void LayoutCollection::initTranslations()
{
    // Ensure we have a reference to the translated names
    // KoraCut : les noms des modules, tels qu'ils apparaissent dans les fichiers de data/layouts.
    QStringList translations = {i18nc("Nom court du module de montage", "Montage"), i18nc("Nom court du module d'étalonnage", "Étalonnage"),
                                i18nc("Nom court du module audio", "Audio"), i18nc("Nom court du module d'effets visuels", "VFX"), i18nc("Nom court du module d'export", "Export")};
}

void LayoutCollection::addLayout(const LayoutInfo &layout)
{
    qDebug() << ":::::: ADDING LAYOUT: " << layout.internalId;
    if (!layout.isValid()) {
        qDebug() << ":::::: ADDING LAYOUT INVALID!!! " << layout.internalId;
        return;
    }

    m_layouts[layout.internalId] = layout;

    // Update order list if not already present
    if (!KdenliveSettings::layoutsOrder().contains(layout.internalId)) {
        QStringList order = KdenliveSettings::layoutsOrder();
        order << layout.internalId;
        KdenliveSettings::setLayoutsOrder(order);
    }
}

bool LayoutCollection::removeLayout(const QString &id)
{
    if (!m_layouts.contains(id)) {
        return false;
    }

    m_layouts.remove(id);
    QStringList order = KdenliveSettings::layoutsOrder();
    order.removeAll(id);
    KdenliveSettings::setLayoutsOrder(order);
    return true;
}

LayoutInfo LayoutCollection::getLayout(const QString &id) const
{
    if (m_layouts.contains(id)) {
        return m_layouts.value(id);
    }
    return LayoutInfo();
}

bool LayoutCollection::hasLayout(const QString &id) const
{
    return m_layouts.contains(id);
}

QList<LayoutInfo> LayoutCollection::getAllLayouts() const
{
    QList<LayoutInfo> result;
    // Return layouts in the correct order
    for (const QString &id : KdenliveSettings::layoutsOrder()) {
        if (m_layouts.contains(id)) {
            result.append(m_layouts.value(id));
        }
    }

    return result;
}

void LayoutCollection::resetDefaultLayouts()
{
    LayoutCollection collection;

    // Remove any custom layout overwriting defaults
    QDir customLayoutsFolder(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    if (customLayoutsFolder.cd(QStringLiteral("layouts"))) {
        for (auto &id : KdenliveSettings::defaultLayoutsOrderValue()) {
            if (!m_layouts.contains(id)) {
                // This should not happen, default layouts should always be there
                continue;
            }
            if (customLayoutsFolder.exists(id + QStringLiteral(".json"))) {
                customLayoutsFolder.remove(id + QStringLiteral(".json"));
            }
            if (customLayoutsFolder.exists(id + QStringLiteral("_vertical.json"))) {
                customLayoutsFolder.remove(id + QStringLiteral("_vertical.json"));
            }
        }
    }

    // Reset layouts order and reload
    KdenliveSettings::setLayoutsOrder({});
    loadLayouts();
}

void LayoutCollection::loadLayouts()
{
    // Keep translated names but clear any existing layout data
    m_layouts.clear();

    // Search layouts
    QMap<QString, LayoutInfo> foundlayouts;
    const QStringList layoutsFolders =
        QStandardPaths::locateAll(QStandardPaths::AppLocalDataLocation, QStringLiteral("layouts"), QStandardPaths::LocateDirectory);
    for (const QString &folderpath : layoutsFolders) {
        QDir dir(folderpath);
        QDirIterator it(dir.absolutePath(), {QStringLiteral("*.json")}, QDir::Files);
        while (it.hasNext()) {
            const QFileInfo fInfo(dir.absoluteFilePath(it.next()));
            QString layoutId = fInfo.baseName();
            if (foundlayouts.contains(layoutId) && !fInfo.isWritable()) {
                // Only overwrite if the folder is writable
                auto layout = foundlayouts.value(layoutId);
                if (layoutId.endsWith(QLatin1String("_vertical"))) {
                    // We are processing a vertical layout, ensure we have none yet
                    if (layout.hasVerticalData()) {
                        continue;
                    }
                } else if (layout.hasHorizontalData()) {
                    continue;
                }
            }
            // Read layout info
            QFile loadFile(fInfo.absoluteFilePath());
            if (!loadFile.open(QIODevice::ReadOnly)) {
                qDebug() << "// Cannot read layout: " << fInfo.absoluteFilePath();
                continue;
            }
            const QByteArray saveData = loadFile.readAll();
            QJsonDocument loadDoc(QJsonDocument::fromJson(saveData));
            QVariantList results = loadDoc["kdenliveInfo"].toArray().toVariantList();
            if (results.isEmpty()) {
                qDebug() << "// No Kdenlive data found in layout: " << fInfo.absoluteFilePath();
                continue;
            }
            // KoraCut : « _compact » puis « _vertical » à retirer du nom de fichier (montage_vertical_compact.json).
            if (layoutId.endsWith(QLatin1String("_compact"))) {
                layoutId.chop(8);
            }
            if (layoutId.endsWith(QLatin1String("_vertical"))) {
                // Remove _vertical suffix from id
                layoutId.chop(9);
            }
            const QString displayName = results[0].toMap().value(QStringLiteral("displayName")).toString();
            const QString insideId = results[0].toMap().value(QStringLiteral("id")).toString();
            bool isDefault = results[0].toMap().value(QStringLiteral("default"), false).toBool();
            bool isVertical = results[0].toMap().value(QStringLiteral("vertical"), false).toBool();
            const bool isCompact = results[0].toMap().value(QStringLiteral("compact"), false).toBool();
            if (!insideId.isEmpty()) {
                // Allow overriding id based on file name
                layoutId = insideId;
            }
            qDebug() << ":::: LOADING LAYOUT: " << layoutId << ", VERTICAL: " << isVertical;
            LayoutInfo layout;
            if (foundlayouts.contains(layoutId)) {
                layout = foundlayouts.value(layoutId);
                const QString &slot = isCompact ? (isVertical ? layout.verticalCompactPath : layout.compactPath) : (isVertical ? layout.verticalPath : layout.path);
                if (!slot.isEmpty()) {
                    // Duplicate layout
                    qDebug() << "Duplicate layout, ignoring: " << layoutId << " = " << fInfo.absoluteFilePath();
                    continue;
                }
            } else {
                // KoraCut : les noms des plans livrés sont écrits en français dans les .json (« Montage », « Étalonnage ») ;
                // ils sont traduits par leur identifiant, en anglais dans la source (22 octobre 2026). Le nom du .json
                // reste tel quel : il peut servir de clé à un plan enregistré.
                QString shown = displayName;
                if (isDefault) {
                    static const QHash<QString, KLazyLocalizedString> names{{QStringLiteral("montage"), kli18nc("@item workspace layout name", "Editing")},
                                                                    {QStringLiteral("etalonnage"), kli18nc("@item workspace layout name", "Color")},
                                                                    {QStringLiteral("audio"), kli18nc("@item workspace layout name", "Audio")},
                                                                    {QStringLiteral("vfx"), kli18nc("@item workspace layout name", "VFX")},
                                                                    {QStringLiteral("export"), kli18nc("@item workspace layout name", "Export")}};
                    shown = names.contains(layoutId) ? names.value(layoutId).toString() : i18n(displayName.toUtf8().constData());
                }
                layout = LayoutInfo(layoutId, shown);
            }
            layout.isDefault = isDefault;
            if (isCompact) {
                (isVertical ? layout.verticalCompactPath : layout.compactPath) = fInfo.absoluteFilePath();
            } else if (isVertical) {
                layout.verticalPath = fInfo.absoluteFilePath();
            } else {
                layout.path = fInfo.absoluteFilePath();
            }
            foundlayouts.insert(layoutId, layout);
        }
    }

    QStringList previousOrder = KdenliveSettings::layoutsOrder();
    KdenliveSettings::setLayoutsOrder({});
    if (previousOrder.isEmpty()) {
        // Default layout order
        previousOrder = KdenliveSettings::defaultLayoutsOrderValue();
    }

    // Now load the layouts from the entries
    for (const QString &id : std::as_const(previousOrder)) {
        if (id.isEmpty() || !foundlayouts.contains(id)) {
            continue;
        }
        LayoutInfo layout = foundlayouts.take(id);
        if (layout.isValid()) {
            addLayout(layout);
        }
    }
    // Add remaining layouts not yet in order
    for (auto i = foundlayouts.cbegin(), end = foundlayouts.cend(); i != end; ++i) {
        // KoraCut : la disposition d'un module (Export depuis le 28 septembre 2026) arrive dans une version
        // plus récente que la liste enregistrée ; elle n'a pas été retirée par la personne, on la garde.
        if (i.value().isDefault && !KoraCutModules::byId(i.key())) {
            // Default layout not in list, was removed so skip
            continue;
        }
        if (i.value().isValid()) {
            addLayout(i.value());
        }
    }
}

QString LayoutCollection::getTranslatedName(const QString &id) const
{
    // If we have a translation in our collection, use it
    if (m_layouts.contains(id)) {
        return m_layouts[id].displayName;
    }

    // Otherwise just return the ID
    return id;
}
