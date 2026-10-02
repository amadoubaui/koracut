---
name: koracut-genre-tutoriel
description: "À lire pour un tutoriel, une recette, une démonstration, une formation, un « comment faire » : le résultat d'abord, des étapes numérotées, et le temps de voir chaque geste."
---

# Monter un tutoriel

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/genre-tutoriel.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire pour un tutoriel, une recette, une démonstration, une formation, un « comment faire » : le résultat d'abord, des étapes numérotées, et le temps de voir chaque geste.

## La structure

1. **Le résultat** (0 à 5 s) : ce qu'on saura faire à la fin, en image.
2. **Ce qu'il faut** : ingrédients, outils, logiciel, en une image ou une liste à l'écran.
3. **Les étapes**, une par scène du script, numérotées à l'écran (« 1. Couper les oignons ») : le geste en plan serré, assez long pour être compris, la parole qui explique.
4. **Les pièges** : l'erreur courante, montrée ou dite.
5. **Le résultat final** et l'appel à l'action.

## Les règles

- On voit le geste en entier : on ne coupe pas au milieu d'une manipulation qu'il faut reproduire. Un tutoriel est plus lent qu'un Reel : 3 à 8 s par geste.
- Les temps morts (cuisson, chargement, séchage) se sautent : coupe franche avec un carton « 20 minutes plus tard » ou accéléré (`changer_vitesse_clip` à 400 à 800 %, son coupé).
- Un écran d'ordinateur : zoom sur la zone utile (`transformer_clip` et `poser_keyframe`), texte lisible sur téléphone.
- Les étapes deviennent des chapitres (marqueurs catégorie 3) : utile pour YouTube.
- Sous-titres toujours, les chiffres et quantités écrits à l'écran.

## Critères de qualité

- On peut refaire le geste en suivant la vidéo, pause par pause.
- Chaque étape a son numéro et son titre à l'écran.
- Aucune quantité, aucun réglage dit à l'oral sans être écrit.

## Erreurs à éviter

- Une longue introduction avant le premier geste.
- Accélérer le geste qu'il faut apprendre.
- Un zoom d'écran qui bouge sans cesse.

## Outils

`script_modele`, `ajouter_clip`, `couper_clip`, `changer_vitesse_clip`, `transformer_clip`, `poser_keyframe`, `ajouter_titre`, `ajouter_marqueur`, `sous_titres_poser`.
