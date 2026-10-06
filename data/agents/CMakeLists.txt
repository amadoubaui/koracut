# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK

# KoraCut, document 18 (1er octobre 2026) : ce que l'application livre aux agents en ligne de commande
# lancés dans son terminal. Les scripts d'installation (accord demandé, voie officielle) et les
# compétences koracut-*, que l'application pose elle-même dans ~/.claude/skills.
install(PROGRAMS installer-claude-code.sh installer-node.sh DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/agents)
install(FILES installer-claude-code.ps1 DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/agents)

# Les compétences : la source est Sources/koracut-skills dans le dossier KoraCut ; hors de ce dossier
# (GitHub Actions, paquets), la copie data/agents/skills, refaite par synchroniser-outils.sh.
get_filename_component(_koracut_skills_dev "${CMAKE_SOURCE_DIR}/../koracut-skills" ABSOLUTE)
if(EXISTS "${_koracut_skills_dev}/koracut-piloter/SKILL.md")
  set(_koracut_skills_default "${_koracut_skills_dev}")
else()
  set(_koracut_skills_default "${CMAKE_CURRENT_SOURCE_DIR}/skills")
endif()
set(KORACUT_SKILLS_DIR "${_koracut_skills_default}" CACHE PATH "KoraCut: folder of the koracut-* skills shipped with the application")
if(EXISTS "${KORACUT_SKILLS_DIR}")
  message(STATUS "KoraCut skills from ${KORACUT_SKILLS_DIR}")
  install(DIRECTORY "${KORACUT_SKILLS_DIR}/"
          DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/agents/skills
          PATTERN "generer.py" EXCLUDE
          PATTERN "LISEZ-MOI.md" EXCLUDE
          PATTERN ".DS_Store" EXCLUDE
          PATTERN "__pycache__" EXCLUDE)
else()
  message(WARNING "KoraCut skills not found in '${KORACUT_SKILLS_DIR}': the application will ship without them.")
endif()
