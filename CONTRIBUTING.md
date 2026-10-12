# Contribuer à KoraCut

*[English version](CONTRIBUTING.en.md)*

Merci de t'intéresser à KoraCut. Le projet est jeune et développé par une petite équipe (PIXEWORK, Dakar) : toute aide compte, un signalement précis autant qu'un correctif.

## Signaler un défaut ou proposer une idée

Ouvre une issue sur https://github.com/amadoubaui/koracut/issues, en français ou en anglais. Pour un défaut, dis :

- ton système (macOS, Windows, Linux) et la version de KoraCut (menu Aide, À propos) ;
- ce que tu as fait, ce que tu attendais, ce qui s'est passé ;
- si tu peux, une capture d'écran ou un petit projet qui reproduit le défaut.

Tu peux aussi écrire par le site : https://koracut.org/contact

## Compiler

Les dépendances et la procédure par système sont dans [dev-docs/build.md](dev-docs/build.md). En résumé : Qt 6.10 ou plus récent, KDE Frameworks 6.21 ou plus récent, MLT 7.38 ou plus récent, KDDockWidgets 2.4, CMake.

```
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
```

## Proposer un changement

1. Pars de la branche `koracut` et ouvre une pull request vers elle.
2. Un changement par pull request, avec un message de commit qui dit ce qui change et pourquoi.
3. Le code propre à KoraCut vit dans `src/koracut/`. Chaque nouveau fichier porte un en-tête SPDX (`SPDX-License-Identifier: GPL-3.0-or-later`, comme les fichiers voisins).
4. Une chaîne affichée s'écrit en anglais dans `i18nc()`, et sa traduction française va dans `po/fr/koracut.po` avec le même contexte.
5. Les tests doivent passer (`ctest`). Un défaut corrigé mérite un test qui l'aurait attrapé.
6. KoraCut doit rester identique sur macOS, Windows et Linux, et légère sur une machine modeste (4 cœurs, 8 Go) : rien qui charge le processeur pendant qu'on monte.

## Licence

KoraCut est sous licence GPL v3 (`COPYING`). En proposant un changement, tu acceptes qu'il soit publié sous cette licence.
