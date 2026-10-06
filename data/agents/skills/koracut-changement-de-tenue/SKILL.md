---
name: koracut-changement-de-tenue
description: "À lire avant de monter une vidéo où la personne reste en place et où sa tenue change sur la musique. Fiche du Monteur, lue à la demande par `lire_competence`. Écrite le 2 octobre 2026, après le montage Khasim."
---

# Changement de tenue d'un coup

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/changement-de-tenue.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de monter une vidéo où la personne reste en place et où sa tenue change sur la musique. Fiche du Monteur, lue à la demande par `lire_competence`. Écrite le 2 octobre 2026, après le montage Khasim.

## Ce qui fait marcher l'effet

- **Le décor ne bouge pas** : caler tous les rushs sur le même cadre (voir `koracut-defile-relais-detoure`, étape 3). C'est le décor qu'on aligne, pas la personne.
- **La pose se ressemble** de part et d'autre de la coupe : repérer dans chaque rush les moments de même posture (bras le long du corps, mains jointes) sur une planche d'une image par seconde, et couper entre deux postures pareilles.
- **La coupe tombe sur le temps** : mesurer le tempo et l'instant du premier temps fort, poser l'entrée de la personne sur l'introduction, le premier changement sur le temps fort.

## Le rythme

Commencer large, puis serrer : quatre temps par tenue, puis deux, puis un. Finir sur une tenue tenue plus longtemps et la sortie de la personne.

## L'accent

Un coup de zoom à chaque changement : deux images clés sur `rect` (107 % à l'image 0, 100 % à l'image 6, `courbe_douce`). Un souffle doux sur les premiers changements seulement, à -14 dB : au-delà, la musique suffit.

## Contrôle

Planche de l'export : la personne reste au même endroit, le décor ne saute pas. Niveau sonore mesuré (-14 LUFS pour le web).
