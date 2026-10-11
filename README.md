![](data/pics/koracut-logo.png)

# KoraCut

KoraCut est une application de création multimédia de bureau : montage vidéo,
étalonnage, audio, VFX, photo, illustration, motion design. Chaque module est
pilotable par des agents IA, avec le modèle de langage de ton choix.

Développée par PIXEWORK à Dakar. Logiciel libre, sous licence GPL v3.

Site, documentation et téléchargement : https://koracut.org

## Modules

| Numéro | Module | État |
|---|---|---|
| 1 | Montage | en place |
| 2 | Étalonnage | en place |
| 3 | Audio et DAW | en place, module complet à venir |
| 4 | VFX | en place, graphe de nœuds à venir |
| 5 | Photo | à venir |
| 6 | Illustration | à venir |
| 7 | Export | en place |
| 8 | Motion design | en place |

## Construire

Les dépendances sont Qt 6.10 ou plus récent, KDE Frameworks 6.21 ou plus
récent, MLT 7.38 ou plus récent, KDDockWidgets 2.4 et CMake. Le détail par
système est dans [dev-docs/build.md](dev-docs/build.md).

En résumé :

```
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Sur macOS, la chaîne utilisée au studio passe par KDE Craft.

## Empaqueter

- Flatpak : `.flatpak-manifest.json` à la racine, notes dans [packaging/flatpak/README.md](packaging/flatpak/README.md)
- Snap : `snapcraft.yaml`
- Windows avec MSYS2 : `packaging/koracut-msys2.sh`
- Linux avec toutes les dépendances : `packaging/koracut-lnx-withalldeps.sh`

## Licence

GPL v3. Le texte complet est dans `COPYING`, les licences des composants dans
`LICENSES/`.

Copyright © 2026 PIXEWORK.

KoraCut est un dérivé d'un logiciel libre de montage vidéo publié sous GPL v3 :
https://invent.kde.org/multimedia/kdenlive. Le code d'origine, ses auteurs et
sa licence sont conservés. Les détenteurs de droits d'auteur sont listés dans
`AUTHORS` et dans les en-têtes SPDX des fichiers source.
