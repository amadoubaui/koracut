/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutskills.h"
#include "koracutagentinit.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace {

const QString kManifest = QStringLiteral(".koracut-skills.json");
const QString kPrefix = QStringLiteral("koracut-");

// Les fichiers d'une compétence, chemins relatifs triés ; les fichiers du Finder ne comptent pas.
QStringList filesOf(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir root(dir);
    while (it.hasNext()) {
        const QString path = it.next();
        if (QFileInfo(path).fileName() == QLatin1String(".DS_Store")) {
            continue;
        }
        out << root.relativeFilePath(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

QStringList skillNames(const QString &root)
{
    QStringList out;
    const QDir dir(root);
    const QStringList entries = dir.entryList({kPrefix + QLatin1Char('*')}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : entries) {
        if (QFileInfo::exists(dir.absoluteFilePath(name + QStringLiteral("/SKILL.md")))) {
            out << name;
        }
    }
    return out;
}

bool copyTree(const QString &from, const QString &to)
{
    const QDir source(from);
    const QStringList files = filesOf(from);
    for (const QString &relative : files) {
        const QString target = QDir(to).absoluteFilePath(relative);
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            return false;
        }
        QFile::remove(target);
        if (!QFile::copy(source.absoluteFilePath(relative), target)) {
            return false;
        }
        // Une copie venue d'un paquet en lecture seule doit rester modifiable par la personne.
        QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::WriteOwner | QFileDevice::ReadOwner);
    }
    return true;
}

// Remplace @p dest par une copie de @p source : la copie est faite à côté, puis échangée, pour ne
// jamais laisser une compétence à moitié écrite.
bool replaceSkill(const QString &source, const QString &dest)
{
    const QFileInfo info(dest);
    const QString temp = info.absolutePath() + QStringLiteral("/.") + info.fileName() + QStringLiteral(".koracut-tmp");
    QDir(temp).removeRecursively();
    if (!copyTree(source, temp)) {
        QDir(temp).removeRecursively();
        return false;
    }
    if (QFileInfo::exists(dest) && !QDir(dest).removeRecursively()) {
        QDir(temp).removeRecursively();
        return false;
    }
    if (!QDir().rename(temp, dest)) {
        QDir(temp).removeRecursively();
        return false;
    }
    return true;
}

QJsonObject readManifest(const QString &dest)
{
    QFile f(QDir(dest).absoluteFilePath(kManifest));
    if (!f.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("competences")).toObject();
}

bool writeManifest(const QString &dest, const QJsonObject &skills)
{
    QSaveFile f(QDir(dest).absoluteFilePath(kManifest));
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QJsonObject root{{QStringLiteral("version"), 1},
                           {QStringLiteral("note"), QStringLiteral("Écrit par KoraCut : l'empreinte des compétences koracut-* qu'elle a posées ici. "
                                                                   "Une compétence modifiée à la main n'est plus remplacée.")},
                           {QStringLiteral("competences"), skills}};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

} // namespace

QString KoraCutSkills::bundledRoot()
{
    return KoraCutAgentInit::bundledSkillsRoot();
}

QString KoraCutSkills::userRoot()
{
    // KORACUT_SKILLS_DEST : un autre dossier pour les essais.
    const QString env = qEnvironmentVariable("KORACUT_SKILLS_DEST");
    return env.isEmpty() ? QDir::homePath() + QStringLiteral("/.claude/skills") : env;
}

QString KoraCutSkills::fingerprint(const QString &dir)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QDir root(dir);
    const QStringList files = filesOf(dir);
    for (const QString &relative : files) {
        QFile f(root.absoluteFilePath(relative));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        hash.addData(relative.toUtf8());
        hash.addData(QByteArrayView("\0", 1));
        hash.addData(f.readAll());
        hash.addData(QByteArrayView("\0", 1));
    }
    return QString::fromLatin1(hash.result().toHex());
}

KoraCutSkills::Result KoraCutSkills::install(const QString &source, const QString &dest)
{
    Result result;
    if (source.isEmpty() || !QFileInfo(source).isDir()) {
        result.error = QStringLiteral("competences_absentes");
        return result;
    }
    if (QFileInfo(source).canonicalFilePath() == QFileInfo(dest).canonicalFilePath()) {
        result.unchanged = int(skillNames(source).size());
        return result;
    }
    if (!QDir().mkpath(dest)) {
        result.error = QStringLiteral("ecriture_impossible : %1").arg(dest);
        return result;
    }
    const QJsonObject before = readManifest(dest);
    QJsonObject after = before;
    const QStringList names = skillNames(source);
    for (const QString &name : names) {
        const QString from = QDir(source).absoluteFilePath(name);
        const QString to = QDir(dest).absoluteFilePath(name);
        const QString wanted = fingerprint(from);
        if (!QFileInfo::exists(to)) {
            if (replaceSkill(from, to)) {
                result.installed << name;
                after.insert(name, wanted);
            } else {
                result.error = QStringLiteral("ecriture_impossible : %1").arg(to);
            }
            continue;
        }
        const QString present = fingerprint(to);
        if (present == wanted) {
            ++result.unchanged;
            after.insert(name, wanted);
            continue;
        }
        // Différente : on ne la remplace que si elle est restée telle que KoraCut l'avait posée.
        if (before.value(name).toString() == present) {
            if (replaceSkill(from, to)) {
                result.updated << name;
                after.insert(name, wanted);
            } else {
                result.error = QStringLiteral("ecriture_impossible : %1").arg(to);
            }
        } else {
            result.kept << name;
        }
    }
    // Une compétence que l'application ne livre plus : retirée si personne n'y a touché.
    const QStringList known = before.keys();
    for (const QString &name : known) {
        if (names.contains(name) || !name.startsWith(kPrefix)) {
            continue;
        }
        const QString to = QDir(dest).absoluteFilePath(name);
        if (QFileInfo::exists(to) && fingerprint(to) == before.value(name).toString() && QDir(to).removeRecursively()) {
            result.removed << name;
        }
        after.remove(name);
    }
    if (after != before && !writeManifest(dest, after) && result.error.isEmpty()) {
        result.error = QStringLiteral("ecriture_impossible : %1").arg(QDir(dest).absoluteFilePath(kManifest));
    }
    return result;
}

QString KoraCutSkills::summary(const Result &result)
{
    if (!result.ok() && result.total() == 0) {
        return i18nc("@info line in the terminal", "KoraCut skills: not installed (%1).", result.error);
    }
    QString text = i18ncp("@info line in the terminal; %1 number of skills", "%1 KoraCut skill ready for Claude Code", "%1 KoraCut skills ready for Claude Code",
                          result.total());
    QStringList details;
    if (!result.installed.isEmpty()) {
        details << i18ncp("@info part of a sentence", "%1 installed", "%1 installed", result.installed.size());
    }
    if (!result.updated.isEmpty()) {
        details << i18ncp("@info part of a sentence", "%1 updated", "%1 updated", result.updated.size());
    }
    if (!result.kept.isEmpty()) {
        details << i18ncp("@info part of a sentence; skills changed by the person are not replaced", "%1 kept as you changed it", "%1 kept as you changed them",
                          result.kept.size());
    }
    if (!result.removed.isEmpty()) {
        details << i18ncp("@info part of a sentence", "%1 removed", "%1 removed", result.removed.size());
    }
    if (!details.isEmpty()) {
        text += QStringLiteral(" (") + details.join(QStringLiteral(", ")) + QLatin1Char(')');
    }
    return text + QLatin1Char('.');
}
