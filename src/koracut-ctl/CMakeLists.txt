# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
#
# koracut-ctl : la ligne de commande de la porte (document 18, PORTE.md § 6). Un petit exécutable
# Qt console, sans dépendance à kdenliveLib, installé à côté de koracut.

add_executable(koracut-ctl main.cpp)
ecm_mark_nongui_executable(koracut-ctl)
target_link_libraries(koracut-ctl Qt${QT_MAJOR_VERSION}::Core Qt${QT_MAJOR_VERSION}::Network)
set_property(TARGET koracut-ctl PROPERTY CXX_STANDARD 17)
install(TARGETS koracut-ctl DESTINATION ${KDE_INSTALL_BINDIR})
