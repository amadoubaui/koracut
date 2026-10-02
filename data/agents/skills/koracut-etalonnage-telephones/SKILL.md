---
name: koracut-etalonnage-telephones
description: "À lire quand un plan vient d'un téléphone : iPhone (vidéo HDR Dolby Vision, Apple Log), Samsung, Pixel, autres Android."
---

# Les téléphones : iPhone HDR, Apple Log, Android

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-telephones.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan vient d'un téléphone : iPhone (vidéo HDR Dolby Vision, Apple Log), Samsung, Pixel, autres Android.

## iPhone

| Fichier | Geste |
|---|---|
| Vidéo HDR (réglage par défaut depuis l'iPhone 12 : Dolby Vision) | `etal_cst` { `camera: "iphone_hdr_hlg"` } |
| Apple Log (iPhone 15 Pro et suivants, ProRes Log) | `etal_cst` { `camera: "apple_log"` } |
| HDR coupé dans les réglages, ou vidéo reçue par WhatsApp | pas de CST : déjà du Rec.709 |

- La vidéo HDR de l'iPhone est du **HLG en BT.2020**. Sans CST, elle paraît délavée et trop claire. Ne jamais la traiter comme du Rec.709, et ne pas ajouter une LUT HLG en plus du CST.
- Apple Log sans conversion paraît délavé : noir à 0,15, gris à 0,49. Le CST le ramène.
- Mesure attendue après le CST : noirs 8 à 20, saturation au-dessus de 0,25.

## Samsung, Pixel et autres Android

| Fichier | Geste |
|---|---|
| HDR10+ ou HDR10 (Samsung, Pixel en HDR) | sans `camera` : `entree_gamut: "rec2020"`, `entree_gamma: "pq"` |
| HLG (certains Pixel, Xiaomi) | `entree_gamut: "rec2020"`, `entree_gamma: "hlg"` |
| Samsung Log (Galaxy S25 Ultra et suivants) | la LUT Samsung officielle avec `etal_lut` |
| Vidéo standard | pas de conversion |

Samsung Log n'a pas de formule publiée : sans la LUT de Samsung, dis-le (`koracut-quand-un-outil-manque`).

## Comment savoir

KoraCut ne lit pas encore l'espace couleur du fichier pour toi. Indices : une vidéo d'iPhone récente, très claire et un peu délavée, est presque toujours en HDR. Si tu doutes, demande « HDR activé dans l'appareil photo ? ». Un mauvais choix se voit tout de suite à la mesure : noirs bouchés ou image grise.

## Après la conversion

Les téléphones lissent la peau et poussent la netteté : pas de `mid_detail` positif. Leur balance automatique change pendant le plan : mesure au début et à la fin.

## Outils

`etal_cst`, `etal_lut`, `mesurer_image`, `etal_lire`.
