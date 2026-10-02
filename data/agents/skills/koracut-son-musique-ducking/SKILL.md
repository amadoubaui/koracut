---
name: koracut-son-musique-ducking
description: "À lire avant de poser ou régler une musique dans un montage qui parle : choisir, placer, faire baisser la musique quand la voix arrive (ducking), la faire sortir proprement."
---

# Le son, étape 2 : la musique sous la voix

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/son-musique-ducking.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de poser ou régler une musique dans un montage qui parle : choisir, placer, faire baisser la musique quand la voix arrive (ducking), la faire sortir proprement.

## Les étapes

1. **La musique a sa piste** (Musique, A4), jamais celle de la voix.
2. **Le niveau de base** : 6 à 12 dB sous la voix. 6 dB pour une musique douce et peu chargée, 9 dB dans le cas courant, 12 dB pour une musique dense, chantée ou rythmée. Une musique avec des paroles sous une parole : 12 dB au moins, et signale que ça gêne.
3. **Le ducking** : `appliquer_ducking` (piste musique, piste voix), réduction 9 dB, descente en 10 images, remontée en 25, pas de remontée pour un silence de moins d'une seconde. Relis les zones rendues : si la musique remonte au milieu d'une phrase, baisse le seuil de 5 dB et relance (l'outil remplace le ducking précédent).
4. **Les passages sans voix** (ouverture, respirations, fin) : la musique reprend son niveau plein, c'est là qu'elle porte l'émotion.
5. **Les ambiances** 15 à 20 dB sous la voix ; les bruitages (whoosh, pop) discrets, vers -18 à -24 dB.
6. **La sortie** : une musique coupée avant sa fin sort en fondu de 25 à 50 images (`ajouter_fondu`). Mieux : finir sur une fin de phrase musicale, en coupant le morceau là où il respire.

## Choisir la musique

- Une musique du chutier, dont la personne a les droits. Rien ne se télécharge. Le nom du fichier garde titre et artiste (`koracut-organiser-nommage`).
- Le tempo suit le rythme voulu : lent pour un témoignage, rythmé pour une pub ou un Reel.
- Si aucune musique ne convient, le Compositeur peut en faire une (`creer_pattern`, `poser_notes`, `rendre_pattern`).

## Critères de qualité

- On comprend chaque mot sans tendre l'oreille, et on sent la musique.
- Le ducking ne s'entend pas : pas de pompage.
- La musique finit, elle ne s'arrête pas.

## Erreurs à éviter

- La musique au même niveau que la voix « parce qu'elle est belle ».
- Un ducking qui remonte entre chaque mot.
- Une musique coupée net en plein milieu d'une mesure.

## Outils

`appliquer_ducking`, `regler_gain_clip`, `regler_piste_audio`, `ajouter_fondu`, `mesurer_audio`, `poser_keyframe`, `ajouter_clip`, `rogner_clip`.
