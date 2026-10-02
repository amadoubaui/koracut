---
name: koracut-etalonnage-log-red
description: "À lire quand un plan vient d'une RED (Komodo, V-Raptor, Raptor, Monstro, Gemini) livré en ProRes ou en fichier exporté en Log3G10."
---

# Log RED : Log3G10, REDWideGamutRGB

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-red.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

**Depuis un terminal.** Les noms d'outils cités sont ceux des agents de KoraCut. Chacun s'appelle par `koracut-ctl timeline outil nom=<outil> args='{…}'` (serveur MCP : `koracut_timeline_outil`), avec les mêmes paramètres et les mêmes réponses ; `koracut-ctl timeline outils` donne la liste et les schémas de cette version. Un outil absent de cette liste n'existe pas encore : dis-le, ne le simule pas (`koracut-quand-un-outil-manque`). Là où la fiche parle d'un autre agent (le Chef, le Coloriste, le Mixeur…), c'est toi qui fais le geste, avec la compétence qu'il suivrait. La personne regarde KoraCut pendant que tu travailles : `koracut-montage-visible`.

| Geste des agents | Méthode directe de la porte |
|---|---|
| `etat_timeline` | `timeline etat niveau=resume` (ou `normal`, `complet`) |
| `lister_medias` | `medias lister filtre=… dossier=…` |
| `importer_media` | `medias importer chemins='[…]' dossier=…` |
| `renommer_media` | `medias renommer media_id=… nom=…` |
| `renommer_fichier_media` | `fichiers renommer chemin=… nouveau_nom=…` (le chutier suit) |
| `ranger_media`, pour le fichier | `fichiers deplacer chemin=… dossier=…` (le chutier suit) |
| un dossier sur le disque | `fichiers creer_dossier chemin=…` |
| `creer_sequence`, `renommer_sequence`, `activer_sequence` | `projet sequence faire=creer` (ou `renommer`, `activer`) ; `projet sequences` pour les lire |
| `transcrire_media` | `medias analyser media_id=… quoi=transcription` |
| `detecter_silences`, sur un média | `medias analyser media_id=… quoi=silences` |
| `voir_image` | `medias analyser media_id=… quoi=vignette` (puis lis le PNG), `ui capture chemin=… quoi=moniteur` |
| `exporter` | `export lancer prereglage=… chemin=… niveau_lufs=-14` |
| `proposer_plan` | pas d'équivalent : écris le plan à la personne dans la conversation et attends son accord |
| `annuler_dernier_tour` | `historique tour faire=abandon` pendant le tour, `historique annuler` après |
| montrer ce que tu fais | `timeline tete position=…`, `timeline selection clip_ids='[…]'`, `timeline zoom ajuster=true`, `ui message texte=…` |

À lire quand un plan vient d'une RED (Komodo, V-Raptor, Raptor, Monstro, Gemini) livré en ProRes ou en fichier exporté en Log3G10.

Lire d'abord `koracut-etalonnage-log-commun`.

## Choisir

| Fichier | `camera` |
|---|---|
| Log3G10 / REDWideGamutRGB (IPP2) | `red_log3g10` |
| Rendu « Rec.709 » par la caméra ou REDCINE-X | pas de CST |

## Repères

- Log3G10 : gris à 341 sur 1023 (un tiers du code). Courbe très plate : après le CST, le plan demande souvent un Contraste 1,1 à 1,15.
- La courbe Log3G10 descend sous zéro dans les noirs : le CST le gère, n'ajoute pas de Lift pour « rattraper ».

## Pièges

- Le fichier R3D (.r3d) ne s'ouvre pas dans KoraCut : il faut un ProRes. Sinon, c'est un manque (`koracut-quand-un-outil-manque`).
- Les anciens fichiers REDlogFilm (avant IPP2) n'ont pas de `camera` : demande la LUT de la personne, ou un nouvel export en Log3G10.

## Outils

`etal_cst`, `mesurer_image`.
