# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Vincent PINON <vincent.pinon@laposte.net>, Jean-Baptiste Mardelle <jb@kdenlive.org>, Julius Künzel <julius.kuenzel@kde.org>

add_subdirectory(effects)
add_subdirectory(generators)
add_subdirectory(layouts)
add_subdirectory(transitions)
add_subdirectory(icons)
add_subdirectory(knewstuff)
add_subdirectory(lumas)
add_subdirectory(scripts)
add_subdirectory(man)
add_subdirectory(titles)
add_subdirectory(profiles)
add_subdirectory(resourceproviders)
add_subdirectory(shortcuts)
add_subdirectory(luts)

install(FILES
  meta_magiclantern.png
  encodingprofiles.rc
  externalproxies.rc
  koracuteffectscategory.rc
  koracuttranscodingrc
  slideanimations.rc
  clipjobsettings.rc

  DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX})

install(FILES koracut.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})

# KoraCut : schémas de couleurs KColorScheme (lus par KColorSchemeManager dans share/color-schemes)
install(FILES
  color-schemes/KoraCutDark.colors
  color-schemes/KoraCutLight.colors

  DESTINATION ${KDE_INSTALL_DATADIR}/color-schemes)

# KoraCut : thème d'icônes en trait fin (Lucide, ISC). Aussi embarqué dans :/icons/koracut
install(DIRECTORY icons/koracut DESTINATION ${KDE_INSTALL_ICONDIR})

install(FILES profiles.xml DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/export)
install(FILES pro.pixework.koracut.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES pro.pixework.koracut.desktop DESTINATION ${KDE_INSTALL_APPDIR})

find_package(SharedMimeInfo REQUIRED)
install(FILES pro.pixework.koracut.xml westley.xml DESTINATION ${KDE_INSTALL_MIMEDIR})
update_xdg_mimetypes(${KDE_INSTALL_MIMEDIR})

