/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutmotionproject.h"
#include "koracutmotionsync.h"

#include "doc/kdenlivedoc.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace KoraCutMotionProject {

QString sanitizeName(const QString &name)
{
    QString out;
    for (const QChar c : name.trimmed()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) {
            out += c;
        } else if (c.isSpace() || c == QLatin1Char('.')) {
            out += QLatin1Char('-');
        }
    }
    while (out.contains(QLatin1String("--"))) {
        out.replace(QLatin1String("--"), QLatin1String("-"));
    }
    while (out.startsWith(QLatin1Char('-'))) {
        out.remove(0, 1);
    }
    while (out.endsWith(QLatin1Char('-'))) {
        out.chop(1);
    }
    return out.isEmpty() ? QStringLiteral("composition") : out;
}

QString motionRoot(KdenliveDoc *doc)
{
    if (!doc) {
        return QString();
    }
    // Un projet neuf porte déjà l'URL proposée pour son premier enregistrement (~/Movies/Nom.koracut,
    // newFileFromPlan) : tant que ce fichier n'est pas sur le disque, le projet n'a pas de dossier, et
    // lire ~/Movies/motion lui ferait hériter des compositions d'un autre projet (vu le 24 septembre 2026).
    if (!doc->url().isEmpty() && QFileInfo::exists(doc->url().toLocalFile())) {
        return QFileInfo(doc->url().toLocalFile()).absolutePath() + QStringLiteral("/motion");
    }
    // Projet pas encore enregistré : un dossier par document, pour ne rien mélanger.
    return doc->projectDataFolder() + QStringLiteral("/KoraCut/motion-") + doc->getDocumentProperty(QStringLiteral("documentid"));
}

QString cacheDir(KdenliveDoc *doc, const QString &name)
{
    if (!doc) {
        return QString();
    }
    bool ok = false;
    const QDir base = doc->getCacheDir(CacheBase, &ok);
    const QString dir = base.absolutePath() + QStringLiteral("/motion/") + name;
    QDir().mkpath(dir);
    return dir;
}

QList<Info> list(const QString &root)
{
    QList<Info> infos;
    if (root.isEmpty()) {
        return infos;
    }
    const QDir dir(root);
    const QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QString index = dir.absoluteFilePath(name) + QStringLiteral("/index.html");
        if (QFile::exists(index)) {
            infos.append({name, dir.absoluteFilePath(name), index});
        }
    }
    return infos;
}

QString templateHtml(const QString &compositionId, int width, int height, double fps)
{
    const QString fpsText = KoraCutMotion::secondsText(fps);
    return QStringLiteral(R"(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=%2, height=%3">
<script src="assets/gsap.min.js"></script>
<style>
  body { margin: 0; background: transparent; }
  #root { position: relative; width: %2px; height: %3px; overflow: hidden; }
  .clip { position: absolute; inset: 0; display: grid; place-items: center; }
  #titre { color: #FFFFFF; font: 700 %5px system-ui, sans-serif; letter-spacing: -0.02em; text-align: center; margin: 0; }
</style>
</head>
<body>
<div id="root" data-composition-id="%1" data-start="0" data-duration="4" data-fps="%4" data-width="%2" data-height="%3">
  <section id="titre-1" class="clip" data-start="0" data-duration="4" data-track-index="1">
    <h1 id="titre">Nouveau titre</h1>
  </section>
</div>
<script>
  window.__timelines = window.__timelines || {};
  const tl = gsap.timeline({ paused: true });
  tl.fromTo("#titre", { y: 40, opacity: 0 }, { y: 0, opacity: 1, duration: 0.6, ease: "power3.out" }, 0.2);
  tl.to("#titre", { opacity: 0, y: -24, duration: 0.4, ease: "power2.in" }, 3.5);
  window.__timelines["%1"] = tl;
</script>
</body>
</html>
)")
        .arg(compositionId)
        .arg(width)
        .arg(height)
        .arg(fpsText)
        .arg(qMax(40, qMin(width, height) / 9));
}

bool copyDirectory(const QString &from, const QString &to)
{
    QDir source(from);
    if (!source.exists()) {
        return false;
    }
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = source.relativeFilePath(path);
        if (relative.startsWith(QLatin1String("node_modules")) || relative.startsWith(QLatin1String("renders"))) {
            continue;
        }
        const QString target = to + QLatin1Char('/') + relative;
        if (it.fileInfo().isDir()) {
            QDir().mkpath(target);
        } else {
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::remove(target);
            if (!QFile::copy(path, target)) {
                return false;
            }
        }
    }
    return true;
}

namespace {

bool writeGsap(const QString &dir)
{
    QDir().mkpath(dir + QStringLiteral("/assets"));
    const QString target = dir + QStringLiteral("/assets/gsap.min.js");
    if (QFile::exists(target)) {
        return true;
    }
    QFile source(QStringLiteral(":data/motion/gsap.min.js"));
    if (!source.exists()) {
        return false;
    }
    return source.copy(target) && QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
}

bool writeText(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(content.toUtf8());
    return true;
}

bool isLocalPath(const QString &value)
{
    if (value.isEmpty() || value.startsWith(QLatin1Char('#')) || value.startsWith(QLatin1String("data:"))) {
        return false;
    }
    static const QRegularExpression scheme(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*:"));
    return !scheme.match(value).hasMatch() && !value.startsWith(QLatin1String("//"));
}

/** Le chemin de destination d'un fichier cité : relatif au dossier du HTML d'origine, les « ../ » retirés. */
QString destinationFor(const QString &sourceHtmlDir, const QString &absolute)
{
    QString relative = QDir(sourceHtmlDir).relativeFilePath(absolute);
    while (relative.startsWith(QLatin1String("../"))) {
        relative.remove(0, 3);
    }
    if (relative.startsWith(QLatin1String("./"))) {
        relative.remove(0, 2);
    }
    return relative;
}

/** Copie un fichier cité et, pour une feuille de style, ce qu'elle cite à son tour. */
void copyReferenced(const QString &sourceHtmlDir, const QString &absolute, const QString &destRoot, QSet<QString> &done)
{
    if (done.contains(absolute) || !QFile::exists(absolute)) {
        return;
    }
    done.insert(absolute);
    const QString target = destRoot + QLatin1Char('/') + destinationFor(sourceHtmlDir, absolute);
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::remove(target);
    QFile::copy(absolute, target);
    if (absolute.endsWith(QLatin1String(".css"), Qt::CaseInsensitive)) {
        QFile css(absolute);
        if (css.open(QIODevice::ReadOnly)) {
            const QString content = QString::fromUtf8(css.readAll());
            static const QRegularExpression url(QStringLiteral("url\\(\\s*['\"]?([^'\")]+)['\"]?\\s*\\)"));
            auto it = url.globalMatch(content);
            while (it.hasNext()) {
                const QString ref = it.next().captured(1).trimmed();
                if (isLocalPath(ref)) {
                    copyReferenced(sourceHtmlDir, QFileInfo(QFileInfo(absolute).absolutePath() + QLatin1Char('/') + ref).absoluteFilePath(), destRoot, done);
                }
            }
        }
    }
}

} // namespace

QString create(const QString &root, const QString &name, int width, int height, double fps, QString *error)
{
    const QString dir = root + QLatin1Char('/') + sanitizeName(name);
    if (QFile::exists(dir + QStringLiteral("/index.html"))) {
        if (error) {
            *error = QStringLiteral("Un projet Motion « %1 » existe déjà.").arg(sanitizeName(name));
        }
        return QString();
    }
    QDir().mkpath(dir + QStringLiteral("/compositions"));
    if (!writeGsap(dir)) {
        if (error) {
            *error = QStringLiteral("GSAP n'a pas pu être copié dans %1/assets.").arg(dir);
        }
        return QString();
    }
    const QString index = dir + QStringLiteral("/index.html");
    if (!writeText(index, templateHtml(sanitizeName(name), width, height, fps))) {
        if (error) {
            *error = QStringLiteral("Le fichier %1 n'a pas pu être écrit.").arg(index);
        }
        return QString();
    }
    return index;
}

QString import(const QString &root, const QString &source, const QString &name, QString *error)
{
    const QFileInfo info(source);
    if (!info.exists()) {
        if (error) {
            *error = QStringLiteral("Le fichier %1 n'existe pas.").arg(source);
        }
        return QString();
    }
    const QString dir = root + QLatin1Char('/') + sanitizeName(name);
    const QString index = dir + QStringLiteral("/index.html");
    if (QFile::exists(index)) {
        if (error) {
            *error = QStringLiteral("Un projet Motion « %1 » existe déjà.").arg(sanitizeName(name));
        }
        return QString();
    }
    if (info.isDir()) {
        if (!QFile::exists(info.absoluteFilePath() + QStringLiteral("/index.html"))) {
            if (error) {
                *error = QStringLiteral("Le dossier %1 n'a pas d'index.html.").arg(source);
            }
            return QString();
        }
        if (!copyDirectory(info.absoluteFilePath(), dir)) {
            if (error) {
                *error = QStringLiteral("Le dossier %1 n'a pas pu être copié.").arg(source);
            }
            return QString();
        }
        return index;
    }
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Le fichier %1 ne se lit pas.").arg(source);
        }
        return QString();
    }
    QString html = QString::fromUtf8(file.readAll());
    const QString sourceDir = info.absolutePath();
    QSet<QString> done;
    // Les fichiers cités par src et href, réécrits sans leurs « ../ ». De la fin vers le début pour
    // que les positions restent justes.
    QList<KoraCutMotion::Tag> tags = KoraCutMotion::tokenize(html);
    for (int i = tags.size() - 1; i >= 0; --i) {
        const KoraCutMotion::Tag &t = tags.at(i);
        if (t.closing) {
            continue;
        }
        for (int a = t.attributes.size() - 1; a >= 0; --a) {
            const KoraCutMotion::Attribute &attr = t.attributes.at(a);
            if (attr.name != QLatin1String("src") && attr.name != QLatin1String("href") && attr.name != QLatin1String("data-composition-src")) {
                continue;
            }
            if (!isLocalPath(attr.value)) {
                continue;
            }
            const QString absolute = QFileInfo(sourceDir + QLatin1Char('/') + attr.value).absoluteFilePath();
            const QString destination = destinationFor(sourceDir, absolute);
            copyReferenced(sourceDir, absolute, dir, done);
            if (destination != attr.value) {
                html.replace(attr.valueStart, attr.valueEnd - attr.valueStart, destination);
            }
        }
    }
    QDir().mkpath(dir + QStringLiteral("/compositions"));
    QDir().mkpath(dir + QStringLiteral("/assets"));
    if (!writeText(index, html)) {
        if (error) {
            *error = QStringLiteral("Le fichier %1 n'a pas pu être écrit.").arg(index);
        }
        return QString();
    }
    return index;
}

} // namespace KoraCutMotionProject
