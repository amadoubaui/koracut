# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Bruno Santos <bsantos@gmail.com>
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK

# KXmlGui cherche les schémas de clavier dans <données>/koracut/shortcuts, quel que soit
# le système : le dossier porte toujours le nom de l'application, même sur macOS où le
# reste des données de KoraCut s'installe à plat dans share. Corrigé le 21 septembre 2026,
# sans quoi aucun schéma n'apparaissait dans la fenêtre des raccourcis sur ce Mac.
INSTALL(FILES
	KoraCut
	Classique
	Premiere
DESTINATION ${KDE_INSTALL_DATADIR}/koracut/shortcuts)
