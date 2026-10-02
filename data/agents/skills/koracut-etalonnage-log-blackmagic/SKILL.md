---
name: koracut-etalonnage-log-blackmagic
description: "À lire quand un plan vient d'une Blackmagic (Pocket 4K, 6K, 6K Pro, Cinema Camera 6K, URSA) ou d'un fichier livré en DaVinci Intermediate."
---

# Log Blackmagic : Film Gen5, DaVinci Intermediate

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-blackmagic.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan vient d'une Blackmagic (Pocket 4K, 6K, 6K Pro, Cinema Camera 6K, URSA) ou d'un fichier livré en DaVinci Intermediate.

Lire d'abord `koracut-etalonnage-log-commun`.

## Choisir

| Fichier | `camera` |
|---|---|
| ProRes en « Film » avec la science couleur Gen5 | `blackmagic_film_gen5` |
| Fichier livré en DaVinci Wide Gamut / Intermediate | `davinci_intermediate` |
| « Extended Video » ou « Video » | pas de CST : c'est déjà du Rec.709 |

## Repères

- Film Gen5 : gris à 392 sur 1023.
- Les Pocket enregistrent souvent en Blackmagic RAW (.braw) : KoraCut ne le lit pas encore. Il faut un ProRes (rendu en Film Gen5 par la personne ou sur la caméra). Sinon, c'est un manque : fiche `koracut-quand-un-outil-manque`.
- L'ancienne science Gen4 n'a pas de `camera` : un fichier Gen4 se convertit avec la LUT Blackmagic « Film to Video » (`etal_lut`), si la personne l'a.

## Pièges

- Un plan « Film » converti comme de la vidéo : image plate et grise sous un faux contraste.
- DaVinci Intermediate est un espace de travail, pas une caméra : on le rencontre dans des fichiers déjà passés par Resolve.

## Outils

`etal_cst`, `etal_lut`, `mesurer_image`.
