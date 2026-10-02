---
name: koracut-organiser-le-projet
description: "À lire quand la personne dit « organise-moi ce tournage », « range mes rushs », ou au début de tout montage à partir de rushs bruts : l'enchaînement complet (inventaire, dossiers, noms, séquences, pistes) et le plan à proposer."
---

# Organiser un projet d'un coup

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/organiser-le-projet.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand la personne dit « organise-moi ce tournage », « range mes rushs », ou au début de tout montage à partir de rushs bruts : l'enchaînement complet (inventaire, dossiers, noms, séquences, pistes) et le plan à proposer.

C'est la compétence chapeau de l'organisation. Le détail de chaque geste est dans `koracut-organiser-inventaire-rushs`, `koracut-organiser-nommage`, `koracut-organiser-arborescence`, `koracut-organiser-multicam-son-separe`, `koracut-organiser-sequences-versions`, `koracut-organiser-pistes-couleurs`, `koracut-organiser-marqueurs`.

## L'ordre

1. **Lire** : `lister_medias`, `lister_dossiers_chutier`, `etat_timeline` (resume), `script_lire`. Importer ce qui manque dans `06 A trier`.
2. **Regarder et écouter** chaque rush : l'inventaire (`koracut-organiser-inventaire-rushs`). C'est l'étape qui coûte, c'est elle qui rend tout le reste juste.
3. **Proposer le plan** (`proposer_plan`) avec, en hypothèses : l'ordre de numérotation (script ou tournage), si les fichiers sont renommés sur le disque ou seulement dans le chutier, l'arborescence retenue.
4. **Créer les dossiers** utiles (`koracut-organiser-arborescence`).
5. **Renommer et ranger**, rush par rush, dans l'ordre des numéros, sous les yeux de la personne (`koracut-organiser-nommage`, `koracut-montage-visible`). Pour plus de trente rushs, `organiser_rushs` fait le lot, puis tu vérifies.
6. **Appairer** caméras et sons séparés (`koracut-organiser-multicam-son-separe`).
7. **Préparer la timeline** : séquence renommée `Nom - Format - v1` (`koracut-organiser-sequences-versions`), pistes nommées (`koracut-organiser-pistes-couleurs`).
8. **Bilan** en une ligne, et la suite proposée : « Rangé : 23 rushs dans 4 dossiers, numérotés 01 à 23 dans l'ordre du tournage, inventaire dans le script. Je passe au script ? »

## Le plan type

- Résumé : « Je range les 23 rushs du tournage de la boutique : dossiers, noms numérotés, inventaire dans le script. »
- Étape 1 : regarder et écouter les rushs (4 transcriptions, environ 3 min).
- Étape 2 : créer 01 Rushs, 02 Son, 03 Musique dans le chutier et le dossier du projet.
- Étape 3 : renommer et ranger les 23 rushs, de 01 à 23.
- Étape 4 : nommer la séquence et les pistes.
- Hypothèses : ordre du tournage (pas de script) ; les fichiers sont renommés sur le disque, dans le dossier du projet (un Annuler rend tout) ; 2 rushs flous vont dans 06 A trier.

## Critères de qualité

- On retrouve n'importe quel rush en trois secondes, dans le chutier comme sur le disque.
- Les numéros suivent l'histoire ou le tournage, sans trou ni doublon.
- La personne n'a rien perdu : noms d'origine dans l'inventaire, aucun fichier supprimé.
- Le tout tient dans un seul tour : un Annuler défait l'organisation entière.

## Erreurs à éviter

- Commencer à monter pendant l'organisation.
- Renommer les fichiers sans que le plan accepté le dise.
- Réorganiser un projet déjà rangé par la personne.
