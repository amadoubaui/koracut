# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Jean-Baptiste Mardelle <jb@kdenlive.org>, Julius Künzel <julius.kuenzel@kde.org>, Eric Jiang <erjiang@alumni.iu.edu>, Vincent Pinon <vincent.pinon@asygn.com>

include_directories(..)
set(TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})
configure_file(tests_definitions.h.in tests_definitions.h)
kde_enable_exceptions()

set(KdenliveTest_SOURCES
    avcurvetest.cpp
    audiolevelstasktest.cpp
    cachetest.cpp
    cliploadrecoverytest.cpp
    colorscopestest.cpp
    compositiontest.cpp
    documenttest.cpp
    effectstest.cpp
    effectsgrouptest.cpp
    filetest.cpp # codespell:ignore filetest
    gradienteditwidgettest.cpp
    groupstest.cpp
    hidetest.cpp
    keyframetest.cpp
    koracutagenttoolstest.cpp
    koracutanimationtest.cpp
    koracutagenttoolstest2.cpp
    koracutagenttoolstest3.cpp
    koracutarchivetest.cpp
    koracutautocuttest.cpp
    koracutcliptoolstest.cpp
    koracutcolorstest.cpp
    koracuteffectpreviewstest.cpp
    koracutgradetest.cpp
    koracutgradeuitest.cpp
    koracuthwdecodetest.cpp
    koracutduckingtest.cpp
    koracutexporttest.cpp
    koracutloudnesstest.cpp
    koracutmonitortest.cpp
    koracutmotionsynctest.cpp
    koracutmotiontoolstest.cpp
    koracutnewtracktest.cpp
    koracutorganisationtest.cpp
    koracutperformancetest.cpp
    koracutportetest.cpp
    koracutscripttest.cpp
    koracutterminaltest.cpp
    koracutthemetest.cpp
    koracuttimelinesplittest.cpp
    koracutvoixtest.cpp
    markertest.cpp
    mixtest.cpp
    modeltest.cpp
    movetest.cpp
    nestingtest.cpp
    otiotest.cpp
    regressions.cpp
    rendermodeltest.cpp
    replacetest.cpp
    sequencetest.cpp
    snaptest.cpp
    spacertest.cpp
    subtitlestest.cpp
    timelinepreviewtest.cpp
    timewarptest.cpp
    titlertest.cpp
    treetest.cpp
    trimmingtest.cpp
    utilstest.cpp
)

include(ECMAddTests)

foreach(_source ${KdenliveTest_SOURCES})
  get_filename_component(_targetname ${_source} NAME_WE)
  ecm_add_test(
      TestMain.cpp
      test_utils.cpp
      abortutil.cpp
      ${_source}
      TEST_NAME ${_targetname}
      LINK_LIBRARIES kdenliveLib
  )
  set_property(TARGET ${_targetname} PROPERTY CXX_STANDARD 14)
endforeach()

# KoraCut : le test des thèmes a besoin de la feuille de style, qui est une ressource de l'application.
qt_add_resources(koracutperformancetest_RES ../src/uiresources.qrc)
target_sources(koracutperformancetest PRIVATE ${koracutperformancetest_RES})
qt_add_resources(koracutthemetest_RES ../src/uiresources.qrc)
target_sources(koracutthemetest PRIVATE ${koracutthemetest_RES})
# KoraCut, document 20 : le test du moniteur lit les repères de plateforme dans la même ressource.
qt_add_resources(koracutmonitortest_RES ../src/uiresources.qrc)
target_sources(koracutmonitortest PRIVATE ${koracutmonitortest_RES})
# KoraCut, document 19 (lots 3 et 5) : les outils Motion copient GSAP depuis la même ressource.
qt_add_resources(koracutanimationtest_RES ../src/uiresources.qrc)
target_sources(koracutanimationtest PRIVATE ${koracutanimationtest_RES})
qt_add_resources(koracutmotiontoolstest_RES ../src/uiresources.qrc)
target_sources(koracutmotiontoolstest PRIVATE ${koracutmotiontoolstest_RES})
# KoraCut, document 18 (25 septembre 2026) : le catalogue des agents en ligne de commande et leurs logos.
qt_add_resources(koracutterminaltest_RES ../data/koracut-agents-cli.qrc)
target_sources(koracutterminaltest PRIVATE ${koracutterminaltest_RES})
# KoraCut, document 21 : les modèles de script par type de vidéo sont dans la même ressource.
qt_add_resources(koracutexporttest_RES ../src/uiresources.qrc ../data/icons/plateformes/plateformes.qrc)
target_sources(koracutexporttest PRIVATE ${koracutexporttest_RES})
qt_add_resources(koracutscripttest_RES ../src/uiresources.qrc)
target_sources(koracutscripttest PRIVATE ${koracutscripttest_RES})

# KoraCut, chantier 3 : la session d'agents, avec un faux service en mémoire (pas de réseau). Le test
# lit les définitions d'outils dans la même ressource que l'application.
if(KORACUT_AGENTS)
  ecm_add_test(
      TestMain.cpp
      test_utils.cpp
      abortutil.cpp
      koracutagentsessiontest.cpp
      TEST_NAME koracutagentsessiontest
      LINK_LIBRARIES kdenliveLib
  )
  qt_add_resources(koracutagentsessiontest_RES ${KORACUT_AGENTS_QRC})
  target_sources(koracutagentsessiontest PRIVATE ${koracutagentsessiontest_RES})
  # La porte lit les mêmes définitions pour savoir quels outils demandent l'accord (x_confirmation).
  qt_add_resources(koracutportetest_RES ${KORACUT_AGENTS_QRC})
  target_sources(koracutportetest PRIVATE ${koracutportetest_RES})
  # Connexions IA (25 septembre 2026) : catalogue, coffre des clés, liste ordonnée, vérification
  # auprès d'un faux fournisseur local. Le catalogue et les logos sont dans leur propre ressource.
  ecm_add_test(
      TestMain.cpp
      test_utils.cpp
      abortutil.cpp
      koracutaiconnectionstest.cpp
      TEST_NAME koracutaiconnectionstest
      LINK_LIBRARIES kdenliveLib
  )
  qt_add_resources(koracutaiconnectionstest_RES ../data/icons/fournisseurs/fournisseurs.qrc)
  target_sources(koracutaiconnectionstest PRIVATE ${koracutaiconnectionstest_RES})
endif()
