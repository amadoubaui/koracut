---
name: koracut-organiser-inventaire-rushs
description: "À lire au début de tout montage à partir de rushs, avant de ranger ou de renommer : savoir ce qu'on a (chaque rush, ce qu'il montre, ce qu'il dit, sa qualité) et l'écrire dans un tableau que la personne peut lire."
---

# Faire l'inventaire des rushs

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/organiser-inventaire-rushs.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire au début de tout montage à partir de rushs, avant de ranger ou de renommer : savoir ce qu'on a (chaque rush, ce qu'il montre, ce qu'il dit, sa qualité) et l'écrire dans un tableau que la personne peut lire.

On ne range pas ce qu'on n'a pas regardé. L'inventaire vient avant le nommage, parce que le nom dit le contenu.

## Les étapes

1. **Lister.** `lister_medias` (limite 200) et `lister_dossiers_chutier`. Si des fichiers du dossier du projet ne sont pas encore dans le chutier, `importer_media` avec le dossier entier, dans le dossier de chutier « A trier » : on rangera après.
2. **Trier par nature**, d'après le type et la durée rendus par `lister_medias` : vidéo avec son, vidéo muette (drone, B-roll souvent), son seul (enregistreur, micro-cravate), musique (son long, nom de morceau), image (photo, logo), titre, séquence.
3. **Regarder chaque vidéo.** `voir_image` au milieu du rush (après accord, une fois pour la session) : qui, où, quelle valeur de plan (large, moyen, gros plan, insert), intérieur ou extérieur, caméra (la définition, la cadence et le nom d'origine disent souvent la caméra : `DJI_` drone, `C0001` Sony, `GX01` GoPro, `IMG_` téléphone). Sur un rush long (plus de 2 min), regarde aussi le début et la fin.
4. **Écouter ce qui parle.** Pour une interview, une voix off, un vlog : `transcrire_media` (niveau `segments`, après accord : c'est long, annonce la durée). Les premières phrases disent le sujet et la personne.
5. **Écrire l'inventaire dans le script** (panneau Script, `script_ecrire`) : un bloc tableau `inventaire` titré « Inventaire des rushs », colonnes N° (texte), Nom (media), Nom d'origine (texte), Durée (duree), Contenu (texte), Qualité (choix : bon, moyen, inutilisable), Verdict (choix : garder, à voir, écarter). Une ligne par rush, dans l'ordre de la numérotation (`koracut-organiser-nommage`). En un seul appel : `{ "operations": [{ "faire": "ajouter_bloc", "type": "tableau", "id": "inventaire", "titre": "Inventaire des rushs", "colonnes": [{ "titre": "N°", "type": "texte" }, { "titre": "Nom", "type": "media" }, { "titre": "Nom d'origine", "type": "texte" }, { "titre": "Durée", "type": "duree" }, { "titre": "Contenu", "type": "texte" }, { "titre": "Qualité", "type": "choix", "choix": ["bon", "moyen", "inutilisable"] }, { "titre": "Verdict", "type": "choix", "choix": ["garder", "à voir", "écarter"] }], "lignes": [{ "cellules": { "N°": "01", "Nom": "<media_id>", "Nom d'origine": "C0001.MP4", "Contenu": "Awa face caméra, accroche" } }] }] }`. Place-le après le déroulé s'il existe (`apres`).
6. **Annoncer le bilan** en une ligne : « 23 rushs : 11 plans d'interview (Cam A et B), 8 plans d'illustration, 1 drone, 2 sons séparés, 1 musique. 2 inutilisables (flous). »

## Critères de qualité

- Chaque rush a une ligne, un contenu en moins de dix mots, un verdict.
- Le nom d'origine est gardé dans l'inventaire : on peut toujours retrouver le fichier de la carte.
- Les doublons sont repérés (même durée, même nom d'origine dans deux dossiers).
- Les rushs manquants (`manquant: true`) sont signalés à la personne, pas ignorés.

## Erreurs à éviter

- Renommer d'après le nom de fichier sans avoir regardé : `C0012.MP4` ne dit rien.
- Appeler `voir_image` vingt fois sans accord de session : demande-le une fois, au début, en disant combien d'images tu vas regarder.
- Transcrire tous les rushs : seulement ceux qui parlent et dont le texte compte.
- Supprimer ou retirer un rush jugé inutilisable : il reste dans le projet, verdict « écarter », dans le dossier « A trier » si besoin.

## Outils

`lister_medias`, `lister_dossiers_chutier`, `importer_media`, `voir_image`, `transcrire_media`, `script_lire`, `script_ecrire`, `ajouter_marqueur` (sur un média, pour noter un moment à garder).
