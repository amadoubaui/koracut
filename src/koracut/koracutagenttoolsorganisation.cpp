/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Les outils d'organisation (Agents in-app/outils/organisation.json, 25 septembre 2026) : ranger et
// renommer tout ce qu'on fait en montant. Les dossiers et les médias du chutier, les fichiers sur le
// disque (dans le dossier du projet seulement), les séquences, les pistes, les clips de la timeline.
// Chaque appel = une étape d'annulation ; organiser_rushs fait un lot entier en une seule étape.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"

#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "koracut/porte/koracutportemethodes.h"
#include "mainwindow.h"
#include "profiles/profilemodel.hpp"
#include "project/projectmanager.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinetabs.hpp"
#include "xml/xml.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTemporaryFile>

#include <mlt++/MltProducer.h>

namespace KoraCutAgentToolsPrivate {

namespace {

// ---------------------------------------------------------------------------------------------
// Petits outils

QString plainKey(const QString &text)
{
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out.append(c.toLower());
        }
    }
    return out.simplified();
}

// Le dossier du fichier .koracut, s'il est vraiment sur le disque (un projet neuf porte déjà l'URL
// proposée pour son premier enregistrement, sans fichier : il n'a pas encore de dossier).
QString projectFolder()
{
    if (!pCore || !pCore->currentDoc() || pCore->currentDoc()->url().isEmpty()) {
        return QString();
    }
    const QString file = pCore->currentDoc()->url().toLocalFile();
    return QFileInfo::exists(file) ? QFileInfo(file).absolutePath() : QString();
}

QString relativeToProject(const QString &absolute)
{
    const QString folder = projectFolder();
    if (folder.isEmpty() || !KoraCutPorteMethodes::isInsideFolder(folder, absolute)) {
        return absolute;
    }
    return QDir(folder).relativeFilePath(absolute);
}

bool validName(const QString &name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) && name != QLatin1String(".") && name != QLatin1String("..");
}

std::shared_ptr<ProjectItemModel> binModel()
{
    return pCore ? pCore->projectItemModel() : nullptr;
}

// Le chemin d'un dossier du chutier : « 01 Rushs/Jour 1 » ; vide pour la racine.
QString folderPath(const std::shared_ptr<AbstractProjectItem> &folder)
{
    QStringList parts;
    std::shared_ptr<AbstractProjectItem> item = folder;
    const auto root = binModel()->getRootFolder();
    while (item && item != root) {
        parts.prepend(item->name());
        item = std::static_pointer_cast<AbstractProjectItem>(item->parent());
    }
    return parts.join(QLatin1Char('/'));
}

std::shared_ptr<ProjectFolder> childFolder(const std::shared_ptr<AbstractProjectItem> &parent, const QString &name)
{
    const QString key = plainKey(name);
    for (int i = 0; i < parent->childCount(); ++i) {
        auto child = std::static_pointer_cast<AbstractProjectItem>(parent->child(i));
        if (child->itemType() == AbstractProjectItem::FolderItem && plainKey(child->name()) == key) {
            return std::static_pointer_cast<ProjectFolder>(child);
        }
    }
    return nullptr;
}

// Un dossier par son id, son chemin (« A/B ») ou son nom seul (le premier trouvé, en largeur).
// « racine » et le vide désignent la racine.
std::shared_ptr<ProjectFolder> findFolder(const QString &given)
{
    auto model = binModel();
    const QString wanted = given.trimmed();
    if (wanted.isEmpty() || plainKey(wanted) == QLatin1String("racine") || wanted == QLatin1String("/")) {
        return model->getRootFolder();
    }
    if (auto byId = model->getFolderByBinId(wanted)) {
        return byId;
    }
    if (wanted.contains(QLatin1Char('/'))) {
        std::shared_ptr<AbstractProjectItem> current = model->getRootFolder();
        for (const QString &part : wanted.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            auto next = childFolder(current, part);
            if (!next) {
                return nullptr;
            }
            current = next;
        }
        return std::static_pointer_cast<ProjectFolder>(current);
    }
    std::vector<std::shared_ptr<AbstractProjectItem>> queue{model->getRootFolder()};
    const QString key = plainKey(wanted);
    while (!queue.empty()) {
        auto item = queue.front();
        queue.erase(queue.begin());
        for (int i = 0; i < item->childCount(); ++i) {
            auto child = std::static_pointer_cast<AbstractProjectItem>(item->child(i));
            if (child->itemType() == AbstractProjectItem::FolderItem) {
                if (plainKey(child->name()) == key) {
                    return std::static_pointer_cast<ProjectFolder>(child);
                }
                queue.push_back(child);
            }
        }
    }
    return nullptr;
}

// Trouve ou crée (dans undo/redo) le dossier désigné par un chemin. Un nom seul qui n'existe nulle
// part se crée à la racine. Rend l'id, vide si le chemin est invalide.
QString ensureFolder(const QString &given, Fun &undo, Fun &redo, QStringList *created)
{
    if (auto existing = findFolder(given)) {
        return existing->clipId();
    }
    auto model = binModel();
    std::shared_ptr<AbstractProjectItem> current = model->getRootFolder();
    QString currentId = current->clipId();
    for (const QString &raw : given.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const QString part = raw.trimmed();
        if (!validName(part)) {
            return QString();
        }
        if (current) {
            if (auto next = childFolder(current, part)) {
                current = next;
                currentId = next->clipId();
                continue;
            }
        }
        QString newId;
        if (!model->requestAddFolder(newId, part, currentId, undo, redo)) {
            return QString();
        }
        if (created) {
            *created << part;
        }
        currentId = newId;
        current = model->getFolderByBinId(newId);
    }
    return currentId;
}

// Les médias d'un dossier (enfants directs), pour les collisions de noms.
QList<std::shared_ptr<ProjectClip>> clipsIn(const std::shared_ptr<AbstractProjectItem> &folder)
{
    QList<std::shared_ptr<ProjectClip>> list;
    if (!folder) {
        return list;
    }
    for (int i = 0; i < folder->childCount(); ++i) {
        auto child = std::static_pointer_cast<AbstractProjectItem>(folder->child(i));
        if (child->itemType() == AbstractProjectItem::ClipItem) {
            list << std::static_pointer_cast<ProjectClip>(child);
        }
    }
    return list;
}

bool hasFile(const std::shared_ptr<ProjectClip> &clip)
{
    switch (clip->clipType()) {
    case ClipType::AV:
    case ClipType::Video:
    case ClipType::Audio:
    case ClipType::Image:
    case ClipType::Playlist:
        return !clip->url().isEmpty();
    default:
        return false;
    }
}

// Le nom d'un média du chutier, dans undo/redo.
void renameBinClipWithUndo(const QString &binId, const QString &before, const QString &after, Fun &undo, Fun &redo)
{
    const auto apply = [binId](const QString &value) {
        auto c = binModel() ? binModel()->getClipByBinID(binId) : nullptr;
        if (!c) {
            return false;
        }
        QMap<QString, QString> props;
        props.insert(QStringLiteral("kdenlive:clipname"), value);
        c->setProperties(props, true);
        return true;
    };
    Fun local_redo = [apply, after]() { return apply(after); };
    Fun local_undo = [apply, before]() { return apply(before); };
    local_redo();
    UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
}

// Le dossier d'un média du chutier, dans undo/redo.
bool moveBinClipWithUndo(const QString &binId, const QString &folderId, Fun &undo, Fun &redo)
{
    auto clip = binModel()->getClipByBinID(binId);
    if (!clip) {
        return false;
    }
    auto parent = std::static_pointer_cast<AbstractProjectItem>(clip->parent());
    const QString before = parent ? parent->clipId() : binModel()->getRootFolder()->clipId();
    if (before == folderId) {
        return true;
    }
    const auto apply = [binId](const QString &target) {
        auto model = binModel();
        auto c = model ? model->getClipByBinID(binId) : nullptr;
        auto f = model ? model->getFolderByBinId(target) : nullptr;
        if (!c || !f) {
            return false;
        }
        if (c->parent() == f) {
            return true;
        }
        return c->changeParent(f);
    };
    Fun local_redo = [apply, folderId]() { return apply(folderId); };
    Fun local_undo = [apply, before]() { return apply(before); };
    if (!local_redo()) {
        return false;
    }
    UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
    return true;
}

// Renomme un fichier sur le disque ; un changement de casse seule passe par un nom provisoire (les
// disques du Mac et de Windows ne voient pas la différence).
bool renameOnDisk(const QString &from, const QString &to)
{
    if (from == to) {
        return true;
    }
    if (from.compare(to, Qt::CaseInsensitive) == 0) {
        const QString temp = from + QStringLiteral(".koracut-renommage");
        return QFile::rename(from, temp) && QFile::rename(temp, to);
    }
    return QDir().mkpath(QFileInfo(to).absolutePath()) && QFile::rename(from, to);
}

// Déplace ou renomme le fichier d'un média, dans undo/redo : le chutier et les clips suivent.
bool moveFileWithUndo(const QString &binId, const QString &from, const QString &to, Fun &undo, Fun &redo)
{
    const auto apply = [binId](const QString &a, const QString &b) {
        if (!renameOnDisk(a, b)) {
            return false;
        }
        KoraCutAgentTools::relocateMedia(binId, a, b);
        return true;
    };
    Fun local_redo = [apply, from, to]() { return apply(from, to); };
    Fun local_undo = [apply, from, to]() { return apply(to, from); };
    if (!local_redo()) {
        return false;
    }
    UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
    return true;
}

// Un fichier « existe déjà » à cette place s'il y a un autre fichier (pas le même, dont on change
// seulement la casse du nom).
bool otherFileAt(const QString &target, const QString &source)
{
    if (!QFileInfo::exists(target)) {
        return false;
    }
    return QFileInfo(target).canonicalFilePath() != QFileInfo(source).canonicalFilePath();
}

// Le nom d'un fichier avec l'extension de l'ancien si le nouveau ne la porte pas.
QString withSuffix(const QString &name, const QString &suffix)
{
    if (suffix.isEmpty() || name.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive)) {
        return name;
    }
    return QString(name + QLatin1Char('.') + suffix);
}

QJsonObject addConflicts(QJsonObject failure, const QStringList &conflicts)
{
    QJsonObject e = failure.value(QStringLiteral("erreur")).toObject();
    e.insert(QStringLiteral("conflits"), QJsonArray::fromStringList(conflicts.mid(0, 20)));
    failure.insert(QStringLiteral("erreur"), e);
    return failure;
}

// ---------------------------------------------------------------------------------------------
// Les séquences

// Un clip de séquence par son uuid, son nom, ou l'id de son média ; vide : la séquence affichée.
std::shared_ptr<ProjectClip> findSequence(const QString &given, QUuid *uuidOut)
{
    auto model = binModel();
    if (!model || !pCore->currentDoc()) {
        return nullptr;
    }
    const QMap<QUuid, QString> all = model->getAllSequenceClips();
    const QString wanted = given.trimmed();
    QUuid found;
    if (wanted.isEmpty()) {
        if (auto tl = pCore->projectManager()->getTimeline()) {
            found = tl->uuid();
        }
    } else {
        for (auto it = all.cbegin(); it != all.cend(); ++it) {
            if (it.key().toString(QUuid::WithoutBraces).compare(wanted, Qt::CaseInsensitive) == 0 || it.key().toString().compare(wanted, Qt::CaseInsensitive) == 0 ||
                it.value() == wanted) {
                found = it.key();
                break;
            }
        }
        if (found.isNull()) {
            const QString key = plainKey(wanted);
            for (auto it = all.cbegin(); it != all.cend(); ++it) {
                auto clip = model->getClipByBinID(it.value());
                if (clip && plainKey(clip->clipName()) == key) {
                    found = it.key();
                    break;
                }
            }
        }
    }
    if (found.isNull() || !all.contains(found)) {
        return nullptr;
    }
    if (uuidOut) {
        *uuidOut = found;
    }
    return model->getClipByBinID(all.value(found));
}

bool sequenceNameTaken(const QString &name, const QString &exceptBinId = QString())
{
    const QString key = plainKey(name);
    const QMap<QUuid, QString> all = binModel()->getAllSequenceClips();
    for (auto it = all.cbegin(); it != all.cend(); ++it) {
        if (it.value() == exceptBinId) {
            continue;
        }
        auto clip = binModel()->getClipByBinID(it.value());
        if (clip && plainKey(clip->clipName()) == key) {
            return true;
        }
    }
    return false;
}

QJsonObject sequenceJson(const QUuid &uuid)
{
    QJsonObject o{{QStringLiteral("uuid"), uuid.toString(QUuid::WithoutBraces)}};
    const QString binId = binModel()->getSequenceId(uuid);
    if (auto clip = binId.isEmpty() ? nullptr : binModel()->getClipByBinID(binId)) {
        o.insert(QStringLiteral("nom"), clip->clipName());
        o.insert(QStringLiteral("media_id"), binId);
    }
    return o;
}

TimelineTabs *tabs()
{
    return pCore && pCore->window() ? pCore->window()->findChild<TimelineTabs *>() : nullptr;
}

QString orientationOf(const QUuid &uuid)
{
    if (auto *t = tabs()) {
        return t->isSequenceVertical(uuid) ? QStringLiteral("verticale") : QStringLiteral("horizontale");
    }
    return pCore->getCurrentProfile()->height() > pCore->getCurrentProfile()->width() ? QStringLiteral("verticale") : QStringLiteral("horizontale");
}

// Ouvre la séquence si elle est fermée, et la rend active (sans fenêtre : la timeline du document).
bool activate(const QString &binId, const QUuid &uuid)
{
    if (pCore->window()) {
        if (!pCore->projectManager()->openTimeline(binId, -1, uuid)) {
            return false;
        }
        if (auto *t = tabs()) {
            t->activateSequence(uuid);
        }
        return true;
    }
    if (!pCore->currentDoc()->getTimeline(uuid, true)) {
        return false;
    }
    pCore->projectManager()->setActiveTimeline(uuid);
    return true;
}

// Remet devant une séquence déjà ouverte (après l'ouverture d'une autre).
void reactivate(const QUuid &uuid)
{
    if (uuid.isNull()) {
        return;
    }
    const QString binId = binModel()->getSequenceId(uuid);
    if (!binId.isEmpty()) {
        activate(binId, uuid);
    }
}

// ---------------------------------------------------------------------------------------------
// Les outils

QJsonObject listerDossiersChutier(const Timeline &, const Args &)
{
    auto model = binModel();
    if (!model) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    QJsonArray folders;
    int rootMedias = 0;
    const auto root = model->getRootFolder();
    std::vector<std::shared_ptr<AbstractProjectItem>> queue{root};
    while (!queue.empty()) {
        auto item = queue.front();
        queue.erase(queue.begin());
        int medias = 0;
        int subfolders = 0;
        for (int i = 0; i < item->childCount(); ++i) {
            auto child = std::static_pointer_cast<AbstractProjectItem>(item->child(i));
            if (child->itemType() == AbstractProjectItem::FolderItem) {
                ++subfolders;
                queue.push_back(child);
            } else if (child->itemType() == AbstractProjectItem::ClipItem) {
                ++medias;
            }
        }
        if (item == root) {
            rootMedias = medias;
            continue;
        }
        auto parent = std::static_pointer_cast<AbstractProjectItem>(item->parent());
        folders.append(QJsonObject{{QStringLiteral("id"), item->clipId()},
                                   {QStringLiteral("nom"), item->name()},
                                   {QStringLiteral("chemin"), folderPath(item)},
                                   {QStringLiteral("parent_id"), parent && parent != root ? QJsonValue(parent->clipId()) : QJsonValue()},
                                   {QStringLiteral("medias"), medias},
                                   {QStringLiteral("sous_dossiers"), subfolders}});
    }
    const QString folder = projectFolder();
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("dossiers"), folders},
                       {QStringLiteral("racine_medias"), rootMedias},
                       {QStringLiteral("dossier_projet"), folder.isEmpty() ? QJsonValue() : QJsonValue(folder)}};
}

QJsonObject creerDossierChutier(const Timeline &, const Args &args)
{
    auto model = binModel();
    const QString name = args.value(QStringLiteral("nom")).toString().trimmed();
    if (!validName(name)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le nom « %1 » ne convient pas à un dossier.").arg(name), QStringLiteral("Un nom simple, sans « / »."));
    }
    std::shared_ptr<ProjectFolder> parent = model->getRootFolder();
    if (args.contains(QStringLiteral("parent"))) {
        parent = findFolder(args.value(QStringLiteral("parent")).toString());
        if (!parent) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun dossier du chutier ne s'appelle « %1 ».").arg(args.value(QStringLiteral("parent")).toString()),
                         QStringLiteral("Lis lister_dossiers_chutier."));
        }
    }
    if (auto existing = childFolder(parent, name)) {
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("dossier_id"), existing->clipId()},
                           {QStringLiteral("nom"), existing->name()},
                           {QStringLiteral("chemin"), folderPath(existing)},
                           {QStringLiteral("existait"), true}};
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QString id;
    if (!model->requestAddFolder(id, name, parent->clipId(), undo, redo)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le dossier « %1 » n'a pas pu être créé.").arg(name));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text, %1 folder name", "Create folder %1", name));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("dossier_id"), id},
                       {QStringLiteral("nom"), name},
                       {QStringLiteral("chemin"), folderPath(model->getFolderByBinId(id))},
                       {QStringLiteral("existait"), false}};
}

QJsonObject renommerDossierChutier(const Timeline &, const Args &args)
{
    auto model = binModel();
    const QString given = args.value(QStringLiteral("dossier")).toString();
    auto folder = findFolder(given);
    if (!folder || folder == model->getRootFolder()) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun dossier du chutier ne correspond à « %1 ».").arg(given), QStringLiteral("Lis lister_dossiers_chutier."));
    }
    const QString name = args.value(QStringLiteral("nom")).toString().trimmed();
    if (!validName(name)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le nom « %1 » ne convient pas à un dossier.").arg(name), QStringLiteral("Un nom simple, sans « / »."));
    }
    const QString before = folder->name();
    auto parent = std::static_pointer_cast<AbstractProjectItem>(folder->parent());
    if (auto other = parent ? childFolder(parent, name) : nullptr; other && other != folder) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("Le dossier « %1 » existe déjà au même endroit.").arg(name),
                     QStringLiteral("Range les médias dans ce dossier (ranger_media), ou choisis un autre nom."));
    }
    if (before == name) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("dossier_id"), folder->clipId()}, {QStringLiteral("avant"), before}, {QStringLiteral("apres"), name}, {QStringLiteral("chemin"), folderPath(folder)}};
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!model->requestRenameFolder(folder, name, undo, redo)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le dossier n'a pas pu être renommé."));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text, %1 old name, %2 new name", "Rename folder %1 to %2", before, name));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("dossier_id"), folder->clipId()},
                       {QStringLiteral("avant"), before},
                       {QStringLiteral("apres"), name},
                       {QStringLiteral("chemin"), folderPath(folder)}};
}

QJsonObject rangerMedia(const Timeline &, const Args &args)
{
    auto model = binModel();
    QStringList ids;
    for (const QJsonValue &v : args.value(QStringLiteral("media_ids")).toArray()) {
        const QString id = v.toVariant().toString();
        if (!model->hasClip(id)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun média n'a l'identifiant « %1 » dans le chutier.").arg(id), QStringLiteral("Relis lister_medias (champ id)."));
        }
        if (!ids.contains(id)) {
            ids << id;
        }
    }
    if (ids.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("media_ids est vide."));
    }
    const QString target = args.value(QStringLiteral("dossier")).toString().trimmed();
    const bool disk = args.value(QStringLiteral("disque")).toBool(false);
    const QString project = projectFolder();
    if (disk && project.isEmpty()) {
        return error(QStringLiteral("projet_absent"), QStringLiteral("Le projet n'est pas encore enregistré : il n'a pas de dossier où ranger les fichiers."),
                     QStringLiteral("Range le chutier seul (disque false), ou demande à la personne d'enregistrer le projet."));
    }
    // Ce que les fichiers vont devenir, vérifié avant de toucher à quoi que ce soit.
    std::shared_ptr<ProjectFolder> existing = findFolder(target);
    const QString diskFolderName = existing ? folderPath(existing) : target;
    QList<QPair<QString, QPair<QString, QString>>> moves; // id -> (de, vers)
    QJsonArray outside;
    QStringList conflicts;
    if (disk) {
        if (diskFolderName.contains(QLatin1String(".."))) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Le dossier « %1 » ne convient pas.").arg(target));
        }
        const QString dir = diskFolderName.isEmpty() ? project : QDir::cleanPath(project + QLatin1Char('/') + diskFolderName);
        QSet<QString> targets;
        for (const QString &id : std::as_const(ids)) {
            auto clip = model->getClipByBinID(id);
            if (!hasFile(clip) || !KoraCutPorteMethodes::isInsideFolder(project, clip->url())) {
                outside.append(id);
                continue;
            }
            const QString from = QFileInfo(clip->url()).absoluteFilePath();
            const QString to = QDir(dir).absoluteFilePath(QFileInfo(from).fileName());
            if (from == to) {
                continue;
            }
            if (otherFileAt(to, from) || targets.contains(to.toLower())) {
                conflicts << relativeToProject(to);
                continue;
            }
            targets.insert(to.toLower());
            moves.append({id, {from, to}});
        }
        if (!conflicts.isEmpty()) {
            return addConflicts(error(QStringLiteral("fichier_existe"), QStringLiteral("Un fichier porte déjà ce nom dans le dossier d'arrivée : %1.").arg(conflicts.join(QStringLiteral(", "))),
                                      QStringLiteral("Renomme d'abord les médias (renommer_fichier_media), ou range le chutier seul.")),
                                conflicts);
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QStringList created;
    const QString folderId = ensureFolder(target, undo, redo, &created);
    if (folderId.isEmpty()) {
        undo();
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le dossier « %1 » ne se crée pas.").arg(target), QStringLiteral("Des noms simples séparés par « / »."));
    }
    QJsonArray moved;
    QJsonArray already;
    for (const QString &id : std::as_const(ids)) {
        auto clip = model->getClipByBinID(id);
        auto parent = std::static_pointer_cast<AbstractProjectItem>(clip->parent());
        if (parent && parent->clipId() == folderId) {
            already.append(id);
            continue;
        }
        if (!moveBinClipWithUndo(id, folderId, undo, redo)) {
            undo();
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le média %1 n'a pas pu être rangé.").arg(id));
        }
        moved.append(id);
    }
    int filesMoved = 0;
    for (const auto &m : std::as_const(moves)) {
        if (!moveFileWithUndo(m.first, m.second.first, m.second.second, undo, redo)) {
            undo();
            return error(QStringLiteral("ecriture_impossible"), QStringLiteral("Le système a refusé de déplacer %1.").arg(relativeToProject(m.second.first)));
        }
        ++filesMoved;
    }
    if (!moved.isEmpty() || filesMoved > 0 || !created.isEmpty()) {
        auto folder = model->getFolderByBinId(folderId);
        pCore->pushUndo(undo, redo, i18ncp("@action undo text, %2 folder", "Move %1 media to %2", "Move %1 media to %2", std::max(int(moved.size()), filesMoved),
                                           folder ? folder->name() : target));
    }
    auto folder = model->getFolderByBinId(folderId);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("dossier_id"), folderId},
                       {QStringLiteral("dossier"), folder ? folderPath(folder) : QString()},
                       {QStringLiteral("ranges"), moved},
                       {QStringLiteral("deja_la"), already},
                       {QStringLiteral("fichiers_deplaces"), filesMoved},
                       {QStringLiteral("hors_projet"), outside}};
}

QJsonObject renommerMedia(const Timeline &, const Args &args)
{
    auto model = binModel();
    const QString id = args.value(QStringLiteral("media_id")).toVariant().toString();
    auto clip = model->hasClip(id) ? model->getClipByBinID(id) : nullptr;
    if (!clip) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun média n'a l'identifiant « %1 » dans le chutier.").arg(id), QStringLiteral("Relis lister_medias (champ id)."));
    }
    const QString name = args.value(QStringLiteral("nom")).toString().trimmed();
    if (name.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("nom est vide."));
    }
    const QString before = clip->clipName();
    const auto parent = std::static_pointer_cast<AbstractProjectItem>(clip->parent());
    for (const auto &other : clipsIn(parent)) {
        if (other->clipId() != id && plainKey(other->clipName()) == plainKey(name)) {
            return error(QStringLiteral("existe_deja"), QStringLiteral("Le média %1 porte déjà le nom « %2 » dans ce dossier.").arg(other->clipId(), other->clipName()),
                         QStringLiteral("Ajoute un numéro ou un suffixe (« - Prise 2 », « - Cam B »)."));
        }
    }
    if (clip->clipType() == ClipType::Timeline && sequenceNameTaken(name, id)) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("Une autre séquence s'appelle déjà « %1 ».").arg(name), QStringLiteral("Ajoute un numéro de version (v2)."));
    }
    if (before != name) {
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        renameBinClipWithUndo(id, before, name, undo, redo);
        pCore->pushUndo(undo, redo, i18nc("@action undo text, %1 old name, %2 new name", "Rename %1 to %2", before, name));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("media_id"), id}, {QStringLiteral("avant"), before}, {QStringLiteral("apres"), name}};
}

QJsonObject renommerFichierMedia(const Timeline &, const Args &args)
{
    auto model = binModel();
    const QString id = args.value(QStringLiteral("media_id")).toVariant().toString();
    auto clip = model->hasClip(id) ? model->getClipByBinID(id) : nullptr;
    if (!clip) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun média n'a l'identifiant « %1 » dans le chutier.").arg(id), QStringLiteral("Relis lister_medias (champ id)."));
    }
    if (!hasFile(clip)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le média %1 (« %2 ») n'a pas de fichier à lui sur le disque.").arg(id, clip->clipName()),
                     QStringLiteral("Renomme-le dans le chutier (renommer_media)."));
    }
    const QString project = projectFolder();
    if (project.isEmpty()) {
        return error(QStringLiteral("projet_absent"), QStringLiteral("Le projet n'est pas encore enregistré : aucun fichier ne se renomme tant qu'il n'a pas de dossier."),
                     QStringLiteral("Renomme dans le chutier (renommer_media), ou demande à la personne d'enregistrer le projet."));
    }
    const QString from = QFileInfo(clip->url()).absoluteFilePath();
    if (!KoraCutPorteMethodes::isInsideFolder(project, from)) {
        return error(QStringLiteral("hors_projet"), QStringLiteral("« %1 » est hors du dossier du projet : on ne renomme pas un fichier d'une carte, d'un disque externe ou d'un autre dossier.").arg(from),
                     QStringLiteral("Renomme seulement dans le chutier (renommer_media), et propose à la personne de copier ses rushs dans le dossier du projet."));
    }
    if (!QFileInfo::exists(from)) {
        return error(QStringLiteral("fichier_introuvable"), QStringLiteral("Le fichier de ce média a disparu du disque : %1.").arg(relativeToProject(from)));
    }
    const QString given = args.value(QStringLiteral("nom")).toString().trimmed();
    if (!validName(given)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("nom est un nom de fichier simple, sans dossier."));
    }
    const QString newName = withSuffix(given, QFileInfo(from).suffix());
    const QString to = QFileInfo(from).absolutePath() + QLatin1Char('/') + newName;
    if (otherFileAt(to, from)) {
        return error(QStringLiteral("fichier_existe"), QStringLiteral("Un fichier s'appelle déjà « %1 » dans ce dossier.").arg(newName), QStringLiteral("Ajoute un numéro ou un suffixe."));
    }
    const bool binFollows = args.value(QStringLiteral("chutier_suit")).toBool(true);
    const QString beforeName = clip->clipName();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!moveFileWithUndo(id, from, to, undo, redo)) {
        return error(QStringLiteral("ecriture_impossible"), QStringLiteral("Le système a refusé de renommer %1.").arg(relativeToProject(from)));
    }
    auto after = model->getClipByBinID(id);
    if (binFollows && after && after->clipName() != newName) {
        renameBinClipWithUndo(id, after->clipName(), newName, undo, redo);
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text, %1 old name, %2 new name", "Rename %1 to %2", QFileInfo(from).fileName(), newName));
    after = model->getClipByBinID(id);
    Q_UNUSED(beforeName)
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("media_id"), id},
                       {QStringLiteral("avant"), relativeToProject(from)},
                       {QStringLiteral("apres"), relativeToProject(to)},
                       {QStringLiteral("nom_chutier"), after ? after->clipName() : newName}};
}

// Le nom sans son ancien numéro « 03 - », « M01 - » ni son extension.
QString bareName(const QString &name, const QString &suffix)
{
    QString base = name;
    if (!suffix.isEmpty() && base.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive)) {
        base.chop(suffix.size() + 1);
    }
    static const QRegularExpression numbered(QStringLiteral("^[A-Za-z]{0,3}\\d{1,4}\\s*[-_.]\\s*"));
    base.remove(numbered);
    return base.trimmed();
}

QJsonObject organiserRushs(const Timeline &, const Args &args)
{
    auto model = binModel();
    const QJsonArray plan = args.value(QStringLiteral("plan")).toArray();
    if (plan.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("plan est vide."));
    }
    const QString diskMode = args.value(QStringLiteral("disque")).toString(QStringLiteral("non"));
    if (diskMode != QLatin1String("non") && diskMode != QLatin1String("renommer") && diskMode != QLatin1String("ranger")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("disque vaut « non », « renommer » ou « ranger »."));
    }
    const QString project = projectFolder();
    if (diskMode != QLatin1String("non") && project.isEmpty()) {
        return error(QStringLiteral("projet_absent"), QStringLiteral("Le projet n'est pas encore enregistré : il n'a pas de dossier, aucun fichier ne se renomme."),
                     QStringLiteral("Relance avec disque « non » (le chutier seul), ou demande à la personne d'enregistrer le projet."));
    }
    const bool numbered = args.value(QStringLiteral("numeroter")).toBool(true);
    const QString prefix = args.value(QStringLiteral("prefixe")).toString();
    int counter = args.value(QStringLiteral("premier_numero")).toInt(1);

    struct Entry
    {
        QString id;
        QString before;
        QString name; // nom final (chutier)
        QString folderGiven;
        QString folderId;   // dossier existant, sinon vide
        QString folderPath; // chemin du dossier d'arrivée
        QString from, to;   // fichier, vide s'il ne bouge pas
        bool outside{false};
        int number{0};
    };
    std::vector<Entry> entries;
    QSet<QString> seen;
    int maxNumber = 0;
    for (const QJsonValue &v : plan) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QStringLiteral("media_id")).toVariant().toString();
        auto clip = model->hasClip(e.id) ? model->getClipByBinID(e.id) : nullptr;
        if (!clip) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun média n'a l'identifiant « %1 » dans le chutier.").arg(e.id), QStringLiteral("Relis lister_medias (champ id)."));
        }
        if (seen.contains(e.id)) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Le média %1 est deux fois dans le plan.").arg(e.id));
        }
        seen.insert(e.id);
        e.before = clip->clipName();
        e.number = o.contains(QStringLiteral("numero")) ? o.value(QStringLiteral("numero")).toInt() : counter++;
        maxNumber = std::max(maxNumber, e.number);
        e.folderGiven = o.value(QStringLiteral("dossier")).toString().trimmed();
        if (e.folderGiven.isEmpty()) {
            auto parent = std::static_pointer_cast<AbstractProjectItem>(clip->parent());
            e.folderId = parent ? parent->clipId() : model->getRootFolder()->clipId();
            e.folderPath = parent ? folderPath(parent) : QString();
        } else if (auto f = findFolder(e.folderGiven)) {
            e.folderId = f->clipId();
            e.folderPath = folderPath(f);
        } else {
            for (const QString &part : e.folderGiven.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
                if (!validName(part.trimmed()) || part.contains(QLatin1String(".."))) {
                    return error(QStringLiteral("entree_invalide"), QStringLiteral("Le dossier « %1 » ne convient pas.").arg(e.folderGiven), QStringLiteral("Des noms simples séparés par « / »."));
                }
            }
            e.folderPath = e.folderGiven;
        }
        const QString suffix = hasFile(clip) ? QFileInfo(clip->url()).suffix() : QString();
        const QString wanted = o.value(QStringLiteral("nom")).toString().trimmed();
        if (!wanted.isEmpty() && !validName(wanted)) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Le nom « %1 » ne convient pas (pas de « / »).").arg(wanted));
        }
        e.name = wanted.isEmpty() ? bareName(e.before, suffix) : bareName(wanted, suffix);
        if (e.name.isEmpty()) {
            e.name = e.before;
        }
        if (hasFile(clip)) {
            e.from = QFileInfo(clip->url()).absoluteFilePath();
            e.outside = !KoraCutPorteMethodes::isInsideFolder(project, e.from);
        }
        entries.push_back(e);
    }
    const int width = std::max(2, int(QString::number(maxNumber).size()));
    QStringList binConflicts;
    QStringList diskConflicts;
    QHash<QString, QString> binTargets;  // dossier + nom -> média
    QHash<QString, QString> diskTargets; // chemin -> média
    QSet<QString> planIds = seen;
    QJsonArray outside;
    for (Entry &e : entries) {
        auto clip = model->getClipByBinID(e.id);
        const QString suffix = hasFile(clip) ? QFileInfo(clip->url()).suffix() : QString();
        QString finalName = e.name;
        if (numbered) {
            finalName = QStringLiteral("%1%2 - %3").arg(prefix).arg(e.number, width, 10, QLatin1Char('0')).arg(e.name);
        }
        e.name = withSuffix(finalName, suffix);
        // Le chutier : deux médias du plan au même endroit sous le même nom, ou un média hors du plan.
        const QString binKey = plainKey(e.folderPath) + QLatin1Char('|') + plainKey(e.name);
        if (binTargets.contains(binKey)) {
            binConflicts << QStringLiteral("%1 (médias %2 et %3)").arg(e.name, binTargets.value(binKey), e.id);
        }
        binTargets.insert(binKey, e.id);
        if (!e.folderId.isEmpty()) {
            for (const auto &other : clipsIn(model->getFolderByBinId(e.folderId))) {
                if (!planIds.contains(other->clipId()) && plainKey(other->clipName()) == plainKey(e.name)) {
                    binConflicts << QStringLiteral("%1 (déjà le média %2)").arg(e.name, other->clipId());
                }
            }
        }
        // Le disque.
        if (e.from.isEmpty() || diskMode == QLatin1String("non")) {
            e.from.clear();
            continue;
        }
        if (e.outside) {
            outside.append(e.id);
            e.from.clear();
            continue;
        }
        const QString dir = diskMode == QLatin1String("ranger") ? (e.folderPath.isEmpty() ? project : QDir::cleanPath(project + QLatin1Char('/') + e.folderPath))
                                                                : QFileInfo(e.from).absolutePath();
        e.to = QDir(dir).absoluteFilePath(e.name);
        if (e.to == e.from) {
            e.from.clear();
            continue;
        }
        const QString diskKey = e.to.toLower();
        if (diskTargets.contains(diskKey)) {
            diskConflicts << QStringLiteral("%1 (médias %2 et %3)").arg(relativeToProject(e.to), diskTargets.value(diskKey), e.id);
        } else if (otherFileAt(e.to, e.from)) {
            diskConflicts << QStringLiteral("%1 (le fichier existe déjà)").arg(relativeToProject(e.to));
        }
        diskTargets.insert(diskKey, e.id);
    }
    if (!binConflicts.isEmpty()) {
        return addConflicts(error(QStringLiteral("existe_deja"), QStringLiteral("Rien n'a été fait : %1 nom(s) déjà pris dans le chutier : %2.").arg(binConflicts.size()).arg(binConflicts.mid(0, 5).join(QStringLiteral(" ; "))),
                                  QStringLiteral("Donne un numéro ou un suffixe différent à chaque média (« - Cam B », « - Prise 2 »), puis relance.")),
                            binConflicts);
    }
    if (!diskConflicts.isEmpty()) {
        return addConflicts(error(QStringLiteral("fichier_existe"), QStringLiteral("Rien n'a été fait : %1 nom(s) déjà pris sur le disque : %2.").arg(diskConflicts.size()).arg(diskConflicts.mid(0, 5).join(QStringLiteral(" ; "))),
                                  QStringLiteral("Change les noms en conflit, ou relance avec disque « non ».")),
                            diskConflicts);
    }
    // Tout est vérifié : les dossiers, puis chaque média (fichier, dossier, nom), dans une seule étape.
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QStringList created;
    QJsonArray done;
    int filesRenamed = 0;
    for (Entry &e : entries) {
        if (e.folderId.isEmpty()) {
            e.folderId = ensureFolder(e.folderPath, undo, redo, &created);
            if (e.folderId.isEmpty()) {
                undo();
                return error(QStringLiteral("entree_invalide"), QStringLiteral("Le dossier « %1 » ne se crée pas.").arg(e.folderPath));
            }
            // Les entrées suivantes vers le même dossier le trouvent maintenant.
        }
        if (!e.from.isEmpty()) {
            if (!moveFileWithUndo(e.id, e.from, e.to, undo, redo)) {
                undo();
                return error(QStringLiteral("ecriture_impossible"), QStringLiteral("Le système a refusé de renommer %1. Rien n'a été changé.").arg(relativeToProject(e.from)));
            }
            ++filesRenamed;
        }
        if (!moveBinClipWithUndo(e.id, e.folderId, undo, redo)) {
            undo();
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le média %1 n'a pas pu être rangé. Rien n'a été changé.").arg(e.id));
        }
        auto clip = model->getClipByBinID(e.id);
        if (clip && clip->clipName() != e.name) {
            renameBinClipWithUndo(e.id, clip->clipName(), e.name, undo, redo);
        }
        QJsonObject line{{QStringLiteral("media_id"), e.id}, {QStringLiteral("avant"), e.before}, {QStringLiteral("nom"), e.name}, {QStringLiteral("dossier"), e.folderPath}};
        line.insert(QStringLiteral("fichier"), e.from.isEmpty() ? QJsonValue() : QJsonValue(relativeToProject(e.to)));
        done.append(line);
    }
    created.removeDuplicates();
    pCore->pushUndo(undo, redo, i18ncp("@action undo text", "Organize %1 media", "Organize %1 media", int(entries.size())));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("ranges"), done},
                       {QStringLiteral("dossiers_crees"), QJsonArray::fromStringList(created)},
                       {QStringLiteral("fichiers_renommes"), filesRenamed},
                       {QStringLiteral("hors_projet"), outside}};
}

// ---- Les séquences ------------------------------------------------------------------------------

QString sequenceParentFolder()
{
    auto model = binModel();
    QString parent = model->getRootFolder()->clipId();
    if (model->defaultSequencesFolder() > -1) {
        if (auto folder = model->getFolderByBinId(QString::number(model->defaultSequencesFolder()))) {
            parent = folder->clipId();
        }
    }
    return parent;
}

QString profileNote(const QString &orientation)
{
    const auto &profile = pCore->getCurrentProfile();
    const bool projectVertical = profile->height() > profile->width();
    if (orientation.isEmpty() || (orientation == QLatin1String("verticale")) == projectVertical) {
        return QString();
    }
    if (tabs()) {
        return QStringLiteral("Le projet est en %1 × %2 : la séquence %3 retourne le profil quand elle est affichée, et s'exporte dans son format.")
            .arg(profile->width())
            .arg(profile->height())
            .arg(orientation);
    }
    return QStringLiteral("Le projet n'a qu'un profil (%1 × %2) : l'orientation %3 n'a pas pu être posée ici.").arg(profile->width()).arg(profile->height()).arg(orientation);
}

QJsonObject creerSequence(const Timeline &, const Args &args)
{
    auto model = binModel();
    if (!model || !pCore->currentDoc()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    QString name = args.value(QStringLiteral("nom")).toString().trimmed();
    if (name.isEmpty()) {
        const QStringList names = pCore->currentDoc()->getSequenceNames();
        int n = int(model->getAllSequenceClips().size()) + 1;
        name = i18n("Sequence %1", n);
        while (names.contains(name) || sequenceNameTaken(name)) {
            name = i18n("Sequence %1", ++n);
        }
    } else if (sequenceNameTaken(name)) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("Une séquence s'appelle déjà « %1 ».").arg(name), QStringLiteral("Ajoute un numéro de version (v2) ou active celle-ci (activer_sequence)."));
    }
    // Sans nombre de pistes demandé, la séquence neuve reprend celles de la séquence en cours, noms
    // compris (« Plans », « Voix »…) : un agent qui crée la version 9:16 ne doit pas retomber sur dix
    // pistes V1 à V10 sans nom (vu le 25 septembre 2026 à l'essai du montage visible).
    const auto model0 = pCore->projectManager() ? pCore->projectManager()->getTimeline() : nullptr;
    const bool copyLayout = model0 && !args.contains(QStringLiteral("pistes_video")) && !args.contains(QStringLiteral("pistes_audio"));
    QList<QPair<bool, QString>> layout;
    int prevVideo = 0;
    int prevAudio = 0;
    if (copyLayout) {
        for (int pos = 0; pos < model0->getTracksCount(); ++pos) {
            const int tid = model0->getTrackIndexFromPosition(pos);
            const bool isAudio = model0->isAudioTrack(tid);
            (isAudio ? prevAudio : prevVideo)++;
            layout.append({isAudio, model0->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString()});
        }
    }
    const int video = qBound(0, copyLayout ? prevVideo : args.value(QStringLiteral("pistes_video")).toInt(KdenliveSettings::videotracks()), 20);
    const int audio = qBound(0, copyLayout ? prevAudio : args.value(QStringLiteral("pistes_audio")).toInt(KdenliveSettings::audiotracks()), 20);
    if (video + audio == 0) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Il faut au moins une piste."));
    }
    const QString orientation = args.value(QStringLiteral("orientation")).toString();
    // La séquence devant avant la création : créer la met déjà devant (l'étape poussée rouvre la neuve),
    // donc on la lit ici, pas après (sinon « activer: false » laissait la neuve devant, 28 septembre 2026).
    QUuid restorePrevious = model0 ? model0->uuid() : QUuid();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const QString binId = ClipCreator::createPlaylistClipWithUndo(name, {audio, video}, sequenceParentFolder(), model, undo, redo);
    if (binId.isEmpty() || binId == QLatin1String("-1")) {
        undo();
        return error(QStringLiteral("non_disponible"), QStringLiteral("La séquence ne s'est pas créée."));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Create sequence %1", name));
    auto clip = model->getClipByBinID(binId);
    const QUuid uuid = clip ? clip->getSequenceUuid() : QUuid();
    bool active = false;
    if (!uuid.isNull() && pCore->window()) {
        // Ouvrir une séquence la met devant : sans « activer », on remet ensuite celle qui y était.
        if (!pCore->currentDoc()->getTimeline(uuid, true)) {
            pCore->projectManager()->openTimeline(binId, -1, uuid);
        }
        // Les noms des pistes de la séquence d'origine, rang par rang (même forme, même ordre).
        if (auto fresh = pCore->currentDoc()->getTimeline(uuid, true); fresh && copyLayout && fresh->getTracksCount() == layout.size()) {
            for (int pos = 0; pos < layout.size(); ++pos) {
                const int tid = fresh->getTrackIndexFromPosition(pos);
                if (fresh->isAudioTrack(tid) == layout.at(pos).first && !layout.at(pos).second.isEmpty()) {
                    fresh->setTrackProperty(tid, QStringLiteral("kdenlive:track_name"), layout.at(pos).second);
                }
            }
        }
        if (auto *t = tabs(); t && !orientation.isEmpty()) {
            t->setSequenceOrientation(uuid, orientation == QLatin1String("verticale"));
        }
        if (args.value(QStringLiteral("activer")).toBool(true)) {
            active = activate(binId, uuid);
        } else {
            reactivate(restorePrevious);
        }
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("sequence"), uuid.isNull() ? QJsonObject{{QStringLiteral("media_id"), binId}, {QStringLiteral("nom"), name}} : sequenceJson(uuid)},
                       {QStringLiteral("orientation"), orientation.isEmpty() ? orientationOf(uuid) : orientation},
                       {QStringLiteral("active"), active}};
    if (const QString note = profileNote(orientation); !note.isEmpty()) {
        result.insert(QStringLiteral("note"), note);
    }
    return result;
}

QJsonObject renommerSequence(const Timeline &, const Args &args)
{
    QUuid uuid;
    const QString given = args.value(QStringLiteral("sequence")).toString();
    auto clip = findSequence(given, &uuid);
    if (!clip) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucune séquence ne correspond à « %1 ».").arg(given), QStringLiteral("lister_medias avec type « sequence » donne leurs noms et leurs id."));
    }
    const QString name = args.value(QStringLiteral("nom")).toString().trimmed();
    if (name.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("nom est vide."));
    }
    if (sequenceNameTaken(name, clip->clipId())) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("Une autre séquence s'appelle déjà « %1 ».").arg(name), QStringLiteral("Ajoute un numéro de version (v2)."));
    }
    const QString before = clip->clipName();
    if (before != name) {
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        renameBinClipWithUndo(clip->clipId(), before, name, undo, redo);
        pCore->pushUndo(undo, redo, i18nc("@action undo text, %1 old name, %2 new name", "Rename sequence %1 to %2", before, name));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("sequence"), sequenceJson(uuid)}, {QStringLiteral("avant"), before}};
}

QJsonObject activerSequence(const Timeline &, const Args &args)
{
    QUuid uuid;
    const QString given = args.value(QStringLiteral("sequence")).toString();
    auto clip = findSequence(given, &uuid);
    if (!clip) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucune séquence ne correspond à « %1 ».").arg(given), QStringLiteral("lister_medias avec type « sequence » donne leurs noms et leurs id."));
    }
    if (!activate(clip->clipId(), uuid)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La séquence « %1 » ne s'ouvre pas.").arg(clip->clipName()));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("sequence"), sequenceJson(uuid)}, {QStringLiteral("orientation"), orientationOf(uuid)}};
}

QJsonObject dupliquerSequence(const Timeline &, const Args &args)
{
    auto model = binModel();
    KdenliveDoc *doc = pCore->currentDoc();
    QUuid uuid;
    const QString given = args.value(QStringLiteral("sequence")).toString();
    auto source = findSequence(given, &uuid);
    if (!source || !doc) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucune séquence ne correspond à « %1 ».").arg(given), QStringLiteral("lister_medias avec type « sequence » donne leurs noms et leurs id."));
    }
    QString name = args.value(QStringLiteral("nom")).toString().trimmed();
    if (name.isEmpty()) {
        name = i18nc("@item name of a duplicated sequence", "%1 (copy)", source->clipName());
        int n = 2;
        while (sequenceNameTaken(name)) {
            name = i18nc("@item name of a duplicated sequence, %2 number", "%1 (copy %2)", source->clipName(), n++);
        }
    } else if (sequenceNameTaken(name)) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("Une séquence s'appelle déjà « %1 ».").arg(name), QStringLiteral("Choisis un autre numéro de version."));
    }
    // Comme la duplication du chutier (Bin::slotDuplicateClip) : la séquence ouverte est d'abord
    // synchronisée, puis copiée par son XML.
    if (doc->getTimelinesUuids().contains(uuid)) {
        doc->storeGroups(uuid);
        pCore->projectManager()->syncTimeline(uuid, true);
    }
    QTemporaryFile src(QDir::temp().absoluteFilePath(QStringLiteral("koracut-XXXXXX.mlt")));
    if (!src.open()) {
        return error(QStringLiteral("ecriture_impossible"), QStringLiteral("Pas de fichier temporaire pour copier la séquence."));
    }
    src.close();
    source->cloneProducerToFile(src.fileName());
    QDomDocument xml;
    if (!Xml::docContentFromFile(xml, src.fileName(), false)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La séquence « %1 » n'a pas pu être copiée.").arg(source->clipName()));
    }
    QReadLocker lock(&pCore->xmlMutex);
    const QByteArray content = xml.toString().toUtf8();
    std::shared_ptr<Mlt::Producer> xmlProd(new Mlt::Producer(pCore->getProjectProfile(), "xml-string", content.constData()));
    lock.unlock();
    if (!xmlProd->is_valid()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La séquence « %1 » n'a pas pu être copiée.").arg(source->clipName()));
    }
    xmlProd->set("kdenlive:clipname", name.toUtf8().constData());
    xmlProd->set("kdenlive:sequenceproperties.documentuuid", doc->uuid().toString().toUtf8().constData());
    Mlt::Properties props(xmlProd->get_properties());
    props.clear("kdenlive:control_uuid");
    bool ready = false;
    std::function<void(const QString &)> onReady = [&ready](const QString &) { ready = true; };
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QString binId;
    auto parent = std::static_pointer_cast<AbstractProjectItem>(source->parent());
    if (!model->requestAddBinClip(binId, xmlProd, parent ? parent->clipId() : model->getRootFolder()->clipId(), undo, redo, onReady)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La copie n'a pas pu entrer dans le chutier."));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text, %1 sequence name", "Duplicate sequence %1", source->clipName()));
    QElapsedTimer timer;
    timer.start();
    while (!ready && timer.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    auto copy = model->getClipByBinID(binId);
    const QUuid copyUuid = copy ? copy->getSequenceUuid() : QUuid();
    const QString orientation = args.value(QStringLiteral("orientation")).toString();
    bool active = false;
    if (!copyUuid.isNull() && pCore->window()) {
        const auto previous = pCore->projectManager()->getTimeline();
        // La timeline de la copie doit être construite, sinon son producteur ne s'enregistre pas bien.
        pCore->projectManager()->openTimeline(binId, -1, copyUuid, -1, true);
        if (auto *t = tabs()) {
            t->setSequenceOrientation(copyUuid, orientation.isEmpty() ? t->isSequenceVertical(uuid) : orientation == QLatin1String("verticale"));
        }
        if (args.value(QStringLiteral("activer")).toBool(true)) {
            active = activate(binId, copyUuid);
        } else if (previous) {
            reactivate(previous->uuid());
        }
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("sequence"), copyUuid.isNull() ? QJsonObject{{QStringLiteral("media_id"), binId}, {QStringLiteral("nom"), name}} : sequenceJson(copyUuid)},
                       {QStringLiteral("source"), sequenceJson(uuid)},
                       {QStringLiteral("orientation"), orientation.isEmpty() ? orientationOf(uuid) : orientation},
                       {QStringLiteral("active"), active}};
    if (!ready) {
        result.insert(QStringLiteral("note"), QStringLiteral("La copie est dans le chutier mais n'était pas encore prête : relis lister_medias dans un instant."));
    } else if (const QString note = profileNote(orientation); !note.isEmpty()) {
        result.insert(QStringLiteral("note"), QString(note + QStringLiteral(" Les plans ne sont pas recadrés : recadre-les (transformer_clip).")));
    }
    return result;
}

// ---- Pistes et clips ----------------------------------------------------------------------------

QJsonObject renommerPiste(const Timeline &timeline, const Args &args)
{
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
    if (QJsonObject e = trackIdCheck(timeline, tid, false); !e.isEmpty()) {
        return e;
    }
    const QString name = args.value(QStringLiteral("nom")).toString().trimmed();
    if (name.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("nom est vide."));
    }
    const QString before = timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString();
    // setTrackName inscrit sa propre étape d'annulation, et rien si le nom ne change pas.
    timeline->setTrackName(tid, name);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("piste_id"), tid}, {QStringLiteral("avant"), before.isEmpty() ? trackLabel(timeline, tid) : before}, {QStringLiteral("nom"), name}};
}

QJsonObject renommerClip(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    // Pas clipIdCheck : un nom ne touche pas au montage, un clip d'une piste verrouillée se renomme aussi.
    if (!timeline->isClip(cid)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(cid), QStringLiteral("Relis etat_timeline : les identifiants changent après une coupe ou une annulation."));
    }
    const QString name = args.value(QStringLiteral("nom")).toString().trimmed();
    const QString before = timeline->getClipName(cid);
    auto media = binModel()->getClipByBinID(timeline->getClipBinId(cid));
    const QString mediaName = media ? media->clipName() : QString();
    // Le nom du média lui-même : rien de propre à garder.
    const QString stored = name == mediaName ? QString() : name;
    if (before != (stored.isEmpty() ? mediaName : stored)) {
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        if (!TimelineFunctions::koraCutSetClipName(timeline, cid, stored, undo, redo)) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le clip %1 n'a pas pu être renommé.").arg(cid));
        }
        pCore->pushUndo(undo, redo, stored.isEmpty() ? i18nc("@action undo text", "Reset clip name") : i18nc("@action undo text, %1 new name", "Rename clip to %1", stored));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), cid},
                       {QStringLiteral("avant"), before},
                       {QStringLiteral("nom"), timeline->getClipName(cid)},
                       {QStringLiteral("media_nom"), mediaName}};
}

} // namespace

QMap<QString, Tool> organisationTools()
{
    return {
        {QStringLiteral("lister_dossiers_chutier"), listerDossiersChutier},
        {QStringLiteral("creer_dossier_chutier"), creerDossierChutier},
        {QStringLiteral("renommer_dossier_chutier"), renommerDossierChutier},
        {QStringLiteral("ranger_media"), rangerMedia},
        {QStringLiteral("renommer_media"), renommerMedia},
        {QStringLiteral("renommer_fichier_media"), renommerFichierMedia},
        {QStringLiteral("organiser_rushs"), organiserRushs},
        {QStringLiteral("creer_sequence"), creerSequence},
        {QStringLiteral("renommer_sequence"), renommerSequence},
        {QStringLiteral("activer_sequence"), activerSequence},
        {QStringLiteral("dupliquer_sequence"), dupliquerSequence},
        {QStringLiteral("renommer_piste"), renommerPiste},
        {QStringLiteral("renommer_clip"), renommerClip},
    };
}

} // namespace KoraCutAgentToolsPrivate

namespace KoraCutAgentTools {

int relocateMedia(const QString &binId, const QString &from, const QString &to)
{
    if (!pCore || !pCore->projectItemModel()) {
        return 0;
    }
    int count = 0;
    if (auto clip = pCore->projectItemModel()->getClipByBinID(binId)) {
        clip->koraCutRelocate(to);
        ++count;
    }
    // Les producteurs de piste des séquences ouvertes : ce sont eux que le projet enregistre. Une
    // séquence fermée garde son ancien chemin (limite connue, document 5, 25 septembre 2026).
    if (KdenliveDoc *doc = pCore->currentDoc()) {
        const QList<QUuid> uuids = doc->getTimelinesUuids();
        for (const QUuid &uuid : uuids) {
            if (auto tl = doc->getTimeline(uuid, true)) {
                count += TimelineFunctions::koraCutRelocateMedia(tl, binId, from, to);
            }
        }
    }
    return count;
}

} // namespace KoraCutAgentTools
