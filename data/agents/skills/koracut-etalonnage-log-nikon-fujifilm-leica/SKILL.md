---
name: koracut-etalonnage-log-nikon-fujifilm-leica
description: "À lire quand un plan vient d'une Nikon (Z6 III, Z8, Z9, Zf, Z30), d'une Fujifilm (X-H2, X-H2S, X-T5, X-S20, GFX) ou d'une Leica (SL2, SL3, Q3) en log."
---

# Log Nikon, Fujifilm, Leica : N-Log, F-Log, F-Log2, L-Log

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-nikon-fujifilm-leica.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan vient d'une Nikon (Z6 III, Z8, Z9, Zf, Z30), d'une Fujifilm (X-H2, X-H2S, X-T5, X-S20, GFX) ou d'une Leica (SL2, SL3, Q3) en log.

Lire d'abord `koracut-etalonnage-log-commun`.

## Choisir

| Profil | `camera` |
|---|---|
| Nikon N-Log | `nikon_nlog` |
| Fujifilm F-Log | `fujifilm_flog` |
| Fujifilm F-Log2 (plus de latitude, X-H2, X-H2S, X-T5) | `fujifilm_flog2` |
| Leica L-Log | `leica_llog` |
| HLG | sans `camera` : `entree_gamut: "rec2020"`, `entree_gamma: "hlg"` |

Les quatre sont en gamut BT.2020 : `etal_cst` s'en charge.

## Repères

- N-Log : gris à 372 sur 1023.
- F-Log : gris à 470 ; F-Log2 : gris à 400, plus plat, demande plus de contraste après le CST.
- Les Fujifilm ont aussi des simulations de film (Eterna, Classic Chrome) : ce n'est pas du log, pas de CST.
- Le RAW des Nikon (N-RAW) et le ProRes RAW ne s'ouvrent pas dans KoraCut : il faut un fichier en N-Log. Sinon, `koracut-quand-un-outil-manque`.

## Pièges

- F-Log et F-Log2 confondus : gris décalé d'environ un diaphragme, noirs faux.
- Un plan en Eterna passé en F-Log : image écrasée et saturée.

## Outils

`etal_cst`, `mesurer_image`.
