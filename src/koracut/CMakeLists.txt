# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK

set(koracut_SRCS
    koracut/koracutagenttools.cpp
    koracut/koracutagenttoolsmontage.cpp
    koracut/koracutagenttoolsmedia.cpp
    koracut/koracutagenttoolsetalonnage.cpp
    koracut/koracutagenttoolsgrade.cpp
    # Document 22 : le module Étalonnage comme la page Color de DaVinci Resolve.
    koracut/color/koracutgradecontroller.cpp
    koracut/color/koracutnodegraph.cpp
    koracut/color/koracutcolorwidgets.cpp
    koracut/color/koracutnodeeditor.cpp
    koracut/color/koracutpalettes.cpp
    koracut/color/koracutclipstrip.cpp
    koracut/color/koracutcolorpage.cpp
    koracut/koracutagenttoolsexport.cpp
    # Document 23 : le module Export comme la page Deliver de DaVinci Resolve (modèles, réglages, file).
    koracut/export/koracutexport.cpp
    koracut/export/koracutexportqueue.cpp
    koracut/export/koracutdeliverpage.cpp
    koracut/koracutagenttoolsapercu.cpp
    koracut/koracutrender.cpp
    # Chantier 10 (29 septembre 2026) : les réglages selon la machine, l'aperçu adaptatif, le diagnostic.
    koracut/koracuthwdecode.cpp
    koracut/koracutperformance.cpp
    koracut/koracutperfpanel.cpp
    koracut/koracutagenttoolsscript.cpp
    koracut/koracutagenttoolsanimation.cpp
    # Document 19, lot 5 suite : Animer et Transition vers le plan suivant au clic droit sur un clip.
    koracut/koracutanimatemenu.cpp
    # Document 13 : la couleur des clips et des pistes (menus, outils couleur_clip et couleur_piste).
    koracut/koracuttimelinecolors.cpp
    koracut/koracutagenttoolscouleur.cpp
    # 25 septembre 2026 : ranger et renommer (chutier, fichiers du projet, séquences, pistes, clips).
    koracut/koracutagenttoolsorganisation.cpp
    # 25 septembre 2026 : le moteur de thèmes (palettes de jetons, gabarit de feuille de style,
    # contraste) et les outils du thème et des réglages de l'application.
    koracut/koracutthemes.cpp
    # 25 septembre 2026 : la fenêtre Préférences refaite (barre latérale groupée, recherche, cartes, page Apparence).
    koracut/koracutpreferences.cpp
    koracut/koracutpreferencesappearance.cpp
    koracut/koracutagenttoolsreglages.cpp
    # Chantier 5 (29 septembre 2026) : la voix off de synthèse (moteur, outils, fenêtre du panneau Script).
    koracut/voix/koracutvoiceover.cpp
    koracut/voix/koracutvoiceoverdialog.cpp
    koracut/voix/koracutvoicerecord.cpp
    koracut/koracutagenttoolsvoix.cpp
    # Document 24 (30 septembre 2026) : la section Outils de l'inspecteur (réduire le bruit, améliorer
    # la voix, effets de voix, stabiliser) et ses outils d'agent.
    koracut/koracutcliptools.cpp
    koracut/koracutcliptoolspanel.cpp
    koracut/koracutstabilizer.cpp
    koracut/koracutagenttoolsoutils.cpp
    # 1er octobre 2026 : detourer_sujet, le sujet d'un plan détouré et posé au-dessus de lui.
    koracut/koracutagenttoolsdetourage.cpp
    # Le mode « montage visible » : l'écran suit chaque geste des agents et de la porte.
    koracut/koracutvisible.cpp
    koracut/koracutassetcards.cpp
    koracut/koracutarchive.cpp
    koracut/koracutautocut.cpp
    koracut/koracutautocutdialog.cpp
    koracut/koracutducking.cpp
    koracut/koracutduckingdialog.cpp
    koracut/koracuteffectpreviews.cpp
    koracut/koracutemptystate.cpp
    koracut/koracutextensionsdialog.cpp
    koracut/koracuticons.cpp
    koracut/koracutloudness.cpp
    koracut/koracutmodules.cpp
    koracut/koracutnewprojectdialog.cpp
    koracut/koracutpanelfocus.cpp
    koracut/koracutsync.cpp
    koracut/koracutvideotypes.cpp
    koracut/koracutworkspace.cpp
    koracut/koracuttimelineaudio.cpp
    koracut/koracuttranscriber.cpp
    koracut/koracutplaceholder.cpp
    koracut/koracutstyle.cpp
    koracut/koracuttopbar.cpp
    # Document 13, § 5 : plusieurs séquences visibles côte à côte dans le panneau Timeline.
    koracut/koracuttimelinesplit.cpp
    # La porte de commande (document 18, lot 1) : prise locale, méthodes, Console, terminal.
    koracut/porte/koracutporte.cpp
    koracut/porte/koracutportemethodes.cpp
    koracut/porte/koracutconsolepanel.cpp
    koracut/porte/koracutterminal.cpp
    # Document 18, 25 septembre 2026 : le terminal intégré (libvterm + PTY), le choix de l'agent en
    # ligne de commande et l'init automatique du dossier du projet.
    koracut/terminal/koracutpty.cpp
    koracut/terminal/koracutterminalview.cpp
    koracut/terminal/koracutagentscli.cpp
    koracut/terminal/koracutagentinit.cpp
    koracut/terminal/koracutskills.cpp
    koracut/terminal/koracutterminalpanel.cpp
    koracut/terminal/libvterm/src/encoding.c
    koracut/terminal/libvterm/src/keyboard.c
    koracut/terminal/libvterm/src/mouse.c
    koracut/terminal/libvterm/src/parser.c
    koracut/terminal/libvterm/src/pen.c
    koracut/terminal/libvterm/src/screen.c
    koracut/terminal/libvterm/src/state.c
    koracut/terminal/libvterm/src/unicode.c
    koracut/terminal/libvterm/src/vterm.c
    # Chantier 9 : le module Motion, HyperFrames dans la timeline (document 19).
    koracut/motion/koracutmotionsync.cpp
    koracut/motion/koracutmotionproject.cpp
    koracut/motion/koracutmotionrenderer.cpp
    koracut/motion/koracutmotionmirror.cpp
    koracut/motion/koracutmotioncodeeditor.cpp
    koracut/motion/koracutmotionpanel.cpp
    koracut/motion/koracutmotioninspector.cpp
    koracut/motion/koracutmotioncatalog.cpp
    # Document 21 : l'éditeur de script, un document en blocs enregistré dans le projet.
    koracut/script/koracutscript.cpp
    koracut/script/koracutscripttable.cpp
    koracut/script/koracutscriptpanel.cpp)

# Chantier 3 : le client du service d'agents, la session et le panneau. La version libre se compile
# sans (KORACUT_AGENTS=OFF) ; le registre d'outils (koracutagenttools) reste dans les deux cas.
if(KORACUT_AGENTS)
  list(APPEND koracut_SRCS
    koracut/koracutagentclient.cpp
    koracut/koracutagentpanel.cpp
    koracut/koracutagentsession.cpp
    # Connexions IA (25 septembre 2026) : catalogue des fournisseurs, clés au trousseau, page des
    # Préférences, lien avec le service local.
    koracut/ai/koracutaicatalog.cpp
    koracut/ai/koracutaisecrets.cpp
    koracut/ai/koracutaiconnections.cpp
    koracut/ai/koracutaiconnectionspage.cpp
    koracut/ai/koracutaibridge.cpp)
endif()

# libvterm est du C tiers (MIT) : ses en-têtes, et pas d'avertissements (ils seraient des erreurs ici).
file(GLOB _koracut_vterm_c "${CMAKE_CURRENT_SOURCE_DIR}/terminal/libvterm/src/*.c")
set_source_files_properties(${_koracut_vterm_c} DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
  PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/terminal/libvterm/include"
             COMPILE_OPTIONS "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>;$<$<C_COMPILER_ID:MSVC>:/w>"
             SKIP_AUTOGEN ON)
# Le dossier de développement (Agents in-app/mcp, Sources/koracut-skills) : trouvé tant qu'on compile
# depuis le dépôt ; une application installée prend sa copie à elle.
get_filename_component(_koracut_root "${CMAKE_SOURCE_DIR}/../.." REALPATH)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/terminal/koracutagentinit.cpp" DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
  PROPERTIES COMPILE_DEFINITIONS "KORACUT_SOURCE_ROOT=\"${_koracut_root}\"")

set(kdenlive_SRCS
  ${kdenlive_SRCS}
  ${koracut_SRCS}
  PARENT_SCOPE)
