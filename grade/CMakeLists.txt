# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK

# Le moteur d'étalonnage (document 22) : une bibliothèque statique sans Qt, utilisée par l'application
# et les tests, et le module MLT libmltkoracut (filtre koracut_grade) chargé aussi par melt et koracut_render.

set(kcgrade_SRCS
    kcjson.cpp
    kccolor.cpp
    kclut.cpp
    kcops.cpp
    kcgrade.cpp
)
add_library(kcgrade STATIC ${kcgrade_SRCS})
set_target_properties(kcgrade PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
target_include_directories(kcgrade PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(kcgrade PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(kcgrade PRIVATE /O2)
else()
    # Le moteur tourne à chaque image : optimisé même dans une compilation de débogage.
    target_compile_options(kcgrade PRIVATE -O2)
endif()

add_library(mltkoracut MODULE mlt/factory.c mlt/filter_koracut_grade.cpp)
# MLT cherche le symbole mlt_register : il doit rester visible (les projets KDE cachent tout par défaut).
set_target_properties(mltkoracut PROPERTIES CXX_STANDARD 17 PREFIX "lib" OUTPUT_NAME "mltkoracut"
    C_VISIBILITY_PRESET default CXX_VISIBILITY_PRESET default VISIBILITY_INLINES_HIDDEN OFF)
target_link_libraries(mltkoracut PRIVATE kcgrade Mlt7::mlt)
if(APPLE)
    # MLT cherche ses modules en « .so », y compris sur macOS.
    set_target_properties(mltkoracut PROPERTIES SUFFIX ".so")
endif()

# Où MLT cherche ses modules : Contents/PlugIns/mlt dans l'application macOS (Craft pose plugins/mlt),
# lib/mlt-7 sous Linux, lib/mlt sous Windows.
if(APPLE)
    set(KORACUT_MLT_MODULE_DIR "plugins/mlt" CACHE STRING "Dossier des modules MLT")
elseif(WIN32)
    set(KORACUT_MLT_MODULE_DIR "lib/mlt" CACHE STRING "Dossier des modules MLT")
else()
    set(KORACUT_MLT_MODULE_DIR "${CMAKE_INSTALL_LIBDIR}/mlt-7" CACHE STRING "Dossier des modules MLT")
endif()
install(TARGETS mltkoracut DESTINATION ${KORACUT_MLT_MODULE_DIR})
if(APPLE OR WIN32)
    set(KORACUT_MLT_DATA_DIR "share/mlt")
else()
    set(KORACUT_MLT_DATA_DIR "share/mlt-7")
endif()
install(FILES mlt/filter_koracut_grade.yml DESTINATION ${KORACUT_MLT_DATA_DIR}/koracut)
