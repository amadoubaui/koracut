/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

/** @brief Le chemin d'un fichier tel qu'il est écrit sur le disque, accents compris.
 *
 * Un « é » s'écrit de deux façons en Unicode : un seul caractère (forme composée, NFC) ou un « e »
 * suivi de l'accent (forme décomposée, NFD). macOS écrit les noms en NFD, Qt les rend en NFC, et un
 * partage réseau (SMB vers un serveur Linux) ne trouve le fichier que sous la forme exacte de son
 * nom. Qt retrouve le fichier quelle que soit la forme, MLT non : il cherche le chemin octet pour
 * octet. Un projet dont un chemin mélangeait les deux formes ouvrait donc sans message, et MLT
 * rendait le clip « INVALID » (vu le 1er novembre 2026 sur le Mac mini, clips à vitesse modifiée).
 *
 * onDisk() rend le chemin inchangé s'il existe tel quel, sinon sa forme NFD, puis NFC, si l'une
 * d'elles existe ; sinon le chemin inchangé. Rien ne change pour un chemin sans accent. */
namespace KoraCutPaths {

#ifndef Q_OS_WIN
inline bool existsAsBytes(const QString &path)
{
    struct stat st;
    return !path.isEmpty() && ::stat(path.toUtf8().constData(), &st) == 0;
}
#endif

inline QString onDisk(const QString &path)
{
#ifdef Q_OS_WIN
    // Windows : les noms passent en UTF-16 et NTFS ne distingue pas les formes ; rien à faire.
    return path;
#else
    bool ascii = true;
    for (const QChar c : path) {
        if (c.unicode() > 127) {
            ascii = false;
            break;
        }
    }
    if (ascii || existsAsBytes(path)) {
        return path;
    }
    for (const auto form : {QString::NormalizationForm_D, QString::NormalizationForm_C}) {
        const QString candidate = path.normalized(form);
        if (candidate != path && existsAsBytes(candidate)) {
            return candidate;
        }
    }
    return path;
#endif
}

} // namespace KoraCutPaths
