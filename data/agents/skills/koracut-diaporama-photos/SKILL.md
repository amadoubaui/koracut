---
name: koracut-diaporama-photos
description: "À lire quand un montage utilise des images fixes : photos d'un événement, d'un produit, d'archives, logos : les cadrer au format, les animer lentement, les rythmer."
---

# Monter des photos (diaporama, zoom lent, formats réseaux)

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/diaporama-photos.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un montage utilise des images fixes : photos d'un événement, d'un produit, d'archives, logos : les cadrer au format, les animer lentement, les rythmer.

## Les étapes

1. **Ranger** les photos dans `04 Graphismes` ou un dossier `Photos`, numérotées dans l'ordre du récit (`koracut-organiser-nommage` : `G01 - Remise des prix.jpg`).
2. **Poser** (`ajouter_clip`) : 3 à 5 s par photo en diaporama calme, 1,5 à 2 s sur une musique rythmée.
3. **Cadrer** au format du projet (`transformer_clip`) : la photo remplit le cadre, rapport gardé ; le sujet dans le cadre, surtout en 9:16 (voir `koracut-recadrer-vertical` pour le calcul).
4. **Animer lentement** (effet Ken Burns) : deux images clés sur `rect` (`poser_keyframe`, `interpolation: "courbe_douce"`, après un `transformer_clip` qui pose l'effet), du 100 % au 108 à 115 % sur toute la durée, vers le sujet (un visage, un détail). Alterne zoom avant et zoom arrière d'une photo à l'autre. Jamais plus de 15 % sur 5 s.
5. **Corriger** une photo trop sombre ou trop jaune avec les mêmes outils qu'un plan (`regler_exposition`, `balance_blancs`), mesure à l'appui (`mesurer_image`).
6. **Enchaîner** : coupe franche sur la musique, ou fondu enchaîné court (12 images) pour un diaporama d'émotion.

## Critères de qualité

- Pas de bande noire, pas de photo étirée.
- Le mouvement est à peine perceptible : on sent la vie, pas le zoom.
- Les visages restent entiers dans le cadre du début à la fin du mouvement.

## Erreurs à éviter

- Un zoom rapide sur chaque photo.
- Une photo en basse définition agrandie à 150 % : elle devient floue ; dis-le.
- Le même mouvement sur toutes les photos.

## Outils

`ajouter_clip`, `rogner_clip`, `transformer_clip`, `poser_keyframe`, `mesurer_image`, `regler_exposition`, `balance_blancs`, `ajouter_transition`, `exporter_image`.
