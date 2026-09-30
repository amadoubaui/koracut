# SPDX-FileCopyrightText: Pino Toscano  <pino@kde.org>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

function get_files
{
    echo pro.pixework.koracut.xml
}

function po_for_file
{
    case "$1" in
       pro.pixework.koracut.xml)
           echo koracut_xml_mimetypes.po
       ;;
    esac
}

function tags_for_file
{
    case "$1" in
       pro.pixework.koracut.xml)
           echo comment
       ;;
    esac
}
