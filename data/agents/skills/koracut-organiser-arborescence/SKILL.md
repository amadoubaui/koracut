---
name: koracut-organiser-arborescence
description: "À lire avant de créer ou de renommer un dossier, sur le disque ou dans le chutier : l'arborescence type d'un projet KoraCut, et la règle qui veut que les dossiers du chutier reflètent ceux du disque."
---

# Ranger le projet en dossiers

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/organiser-arborescence.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de créer ou de renommer un dossier, sur le disque ou dans le chutier : l'arborescence type d'un projet KoraCut, et la règle qui veut que les dossiers du chutier reflètent ceux du disque.

## L'arborescence type

```
Nom du projet/
├── Nom du projet.koracut
├── 01 Rushs/            vidéos tournées ; sous-dossiers si besoin : Jour 1, Jour 2, ou Cam A, Cam B, Drone
├── 02 Son/              sons enregistrés à part : enregistreur, micro-cravate, voix off, ambiances
├── 03 Musique/          musiques et leurs licences
├── 04 Graphismes/       logos, photos, images, polices, LUT du client
├── 05 Exports/          ce qui sort : versions livrées, extraits, images
├── 06 A trier/          ce qu'on ne sait pas classer ; rien n'est jamais supprimé
└── motion/              géré par KoraCut (compositions animées) : on n'y touche pas
```

Le chutier porte les mêmes dossiers, avec les mêmes noms : `01 Rushs`, `02 Son`, `03 Musique`, `04 Graphismes`, plus `Titres` et `Séquences` pour ce que KoraCut crée lui-même. La personne retrouve un rush au même endroit dans le chutier et dans son explorateur.

Si le projet a déjà une arborescence propre (celle du guide PIXEWORK `01_Rushes`, `02_Audio`… ou celle du client), on la garde. On ne réorganise pas un projet que la personne a déjà rangé : on complète.

## Les étapes

1. **Lire** : `lister_dossiers_chutier`, `lister_medias`. Repérer ce qui est déjà rangé.
2. **Créer ce qui manque**, seulement les dossiers utiles (pas de `03 Musique` vide) : `creer_dossier_chutier` (`nom`, `parent` pour un sous-dossier comme « 01 Rushs/Jour 1 ») ; un dossier qui existe déjà est rendu tel quel. Sur le disque, le sous-dossier du même nom se crée quand un fichier y est rangé par `ranger_media` avec `disque: true`, si le plan accepté prévoit de ranger les fichiers. `lister_dossiers_chutier` donne `dossier_projet` : s'il est vide (projet pas encore enregistré), rien ne peut bouger sur le disque.
3. **Ranger chaque média** dans son dossier : `ranger_media` (`media_ids`, `dossier`, `disque`), par petits groupes de même nature sous les yeux de la personne (tous les sons, puis la musique), ou `organiser_rushs` qui range et renomme tout un lot. Les médias hors du dossier du projet reviennent dans `hors_projet` : ils sont rangés dans le chutier, pas sur le disque, dis-le. Sous-dossiers seulement au-delà d'une vingtaine de rushs, ou quand il y a plusieurs caméras ou plusieurs jours.
4. **Renommer un dossier mal nommé** : `renommer_dossier_chutier` (« Nouveau dossier » devient « 01 Rushs »).
5. **Vérifier** : `lister_dossiers_chutier` ; plus rien à la racine du chutier sauf les séquences, et `06 A trier` vide ou presque.

## Disque ou chutier seulement

- Le chutier se range toujours : ça ne touche aucun fichier.
- Les fichiers ne bougent sur le disque que si la personne l'a accepté dans le plan, à l'intérieur du dossier du projet, et c'est annulable. Des rushs hors du dossier du projet (carte, disque externe, Téléchargements) ne bougent jamais : on les range dans le chutier, et on propose à la personne de les copier elle-même dans `01 Rushs` si elle veut un projet complet à archiver.

## Erreurs à éviter

- Un dossier par rush, ou quinze dossiers pour vingt rushs.
- Renommer ou déplacer le dossier `motion/` ou le fichier `.koracut`.
- Mettre les exports dans `01 Rushs` : un export n'est jamais un rush.
- Ranger en supprimant : rien ne se supprime, ce qui gêne va dans `06 A trier`.

## Outils

`lister_dossiers_chutier`, `creer_dossier_chutier`, `renommer_dossier_chutier`, `ranger_media`, `organiser_rushs`, `lister_medias`, `importer_media` (avec `dossier_chutier`).
