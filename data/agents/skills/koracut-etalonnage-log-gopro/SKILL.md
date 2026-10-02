---
name: koracut-etalonnage-log-gopro
description: "À lire quand un plan vient d'une GoPro (HERO 11, 12, 13) en log ou en Protune plat."
---

# Log GoPro : GP-Log, Protune

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-gopro.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan vient d'une GoPro (HERO 11, 12, 13) en log ou en Protune plat.

Lire d'abord `koracut-etalonnage-log-commun`.

## Choisir

| Réglage de la caméra | Geste |
|---|---|
| Profil couleur « Log » (GP-Log) | `etal_cst` { `camera: "gopro_gplog2"` }, ou la LUT GoPro officielle avec `etal_lut` |
| Protune « Plat » | pas de CST : Rec.709 doux. Contraste 1,1 à 1,2, `boost` +10 |
| « Vif » ou « Naturel » | pas de conversion |
| HDR | pas de CST |

GoPro ne publie pas son gamut : KoraCut prend BT.2020. Si les couleurs paraissent un peu fausses après le CST (verts ou bleus trop forts), la LUT GoPro fait foi : propose-la.

## Repères

- Le GP-Log est codé en plein (0 à 1), pas en niveaux vidéo.
- Grand angle et soleil : ciel vite brûlé, horizon courbe. On tient les hautes lumières (`hautes_lumieres` -20 à -40).
- Le 8 bits des modèles anciens fait des bandes : pas de gros contraste.

## Pièges

- Du « Plat » converti comme du log : image très contrastée, noirs bouchés.

## Outils

`etal_cst`, `etal_lut`, `mesurer_image`.
