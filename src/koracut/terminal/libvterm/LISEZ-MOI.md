# libvterm dans KoraCut

L'émulateur de terminal du panneau Terminal (document 18, journal du 25 septembre 2026). Il lit les
séquences d'échappement (couleurs, curseur, écran alterné, souris, collage entre crochets) et tient
l'écran ; KoraCut le dessine avec Qt (`../koracutterminalview.cpp`) et fait tourner le processus
dans un PTY (`../koracutpty.cpp`).

- Origine : https://www.leonerd.org.uk/code/libvterm/ (Paul Evans), version 0.3.3 et les correctifs
  du miroir https://github.com/neovim/libvterm, commit `934bc2fbf21800ac3458a499df8820ca5fb45fd3`.
  Clone complet dans `KoraCut/Sources/libvterm/`.
- Licence : MIT (`LICENSE`, et `LICENSES/MIT.txt` du dépôt).
- Copié sans modification : `include/`, `src/*.c`, `src/*.h`, `src/*.inc`, `src/encoding/*.inc`
  (déjà générés, pas besoin de Perl à la compilation). Les tests (`t/`) et les outils (`bin/`)
  restent dans le clone.
- Compilé dans la bibliothèque de l'application (`src/koracut/CMakeLists.txt`), en C, sans
  avertissements (code tiers).

Pour mettre à jour : copier les mêmes fichiers depuis un clone plus récent, relancer
`koracutterminaltest`.
