---
name: koracut-organiser-plusieurs-videos
description: "À lire quand la personne donne un dossier qui contient plusieurs vidéos à monter (une campagne, une série, quatre publicités d'un même tournage) : un projet, une séquence par vidéo, un script par séquence, un dossier par vidéo sur le disque."
---

# Plusieurs vidéos dans un seul projet

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/organiser-plusieurs-videos.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand la personne donne un dossier qui contient plusieurs vidéos à monter (une campagne, une série, quatre publicités d'un même tournage) : un projet, une séquence par vidéo, un script par séquence, un dossier par vidéo sur le disque.

Méthode tirée du montage des quatre publicités Univers Cosmetix (1er octobre 2026).

## Le rangement sur le disque

```
Dossier de la campagne/
├── Campagne.koracut
├── Vidéo A - Titre/
│   ├── 00 Script/      le script reçu (image, PDF), les consignes du client
│   ├── 01 Rushs/       NN - Description.ext, numérotés dans l'ordre du script de CETTE vidéo
│   ├── 02 Son/         les voix de cette vidéo
│   ├── 03 Musique/
│   └── 05 Exports/     « Titre - v1.mp4 », « - v2 », « - v3 » ; les essais dans essais/
├── Vidéo B - Titre/    (même forme)
├── 02 Sons communs/    effets partagés par toutes les vidéos
└── 04 Graphismes/      logos, polices, couleurs de la marque
```

Le chutier porte les mêmes dossiers, avec le même chemin (« Vidéo A - Titre/01 Rushs »). Quatre dossiers s'appellent « 01 Rushs » : désigne toujours un dossier par son chemin complet.

## Les étapes

1. **Regarde tout avant de nommer** : `voir_image` ou `analyser_media` sur chaque rush. Les consignes du client peuvent être rangées dans le mauvais dossier (script d'une vidéo posé avec les rushs d'une autre) : fie-toi à ce que montrent les rushs, et dis-le.
2. **Enregistre le projet à la racine du dossier de la campagne** : les rushs sont alors « dans le projet », `organiser_rushs` avec `disque: "ranger"` peut les renommer et les déplacer. Un appel par vidéo, la numérotation repart de 01.
3. **Une séquence par vidéo**, au nom de la vidéo : `renommer_sequence` pour la première, `creer_sequence` pour les autres (elles reprennent les pistes de la première).
4. **Un script par séquence** : `script_ecrire` avec `sequence`. Recopie le script du client tel quel, mêmes colonnes, mêmes mots. Ce que tu proposes en plus va dans la colonne `son` ou `plans`, pas à la place de son texte.
5. **Monte une vidéo jusqu'au bout avant la suivante** : la première sert de pilote (titres, couleurs, niveaux), les autres reprennent ses choix.
6. **Les éléments communs** (carte de fin, étiquettes, couleurs, polices, effets sonores) sont les mêmes d'une vidéo à l'autre : c'est ce qui fait une campagne.
7. **Exporte dans le dossier de chaque vidéo**, `05 Exports/Titre - v1.mp4`. Une nouvelle version ne remplace jamais l'ancienne.

## Les pièges

- Les identifiants de pistes et de clips changent d'une séquence à l'autre et à chaque réouverture : relis `etat_timeline` après chaque `activer_sequence`.
- Attends la fin des rendus Motion (`motion_lister`, `en_cours` à 0) avant de changer de séquence.
- Un projet n'a qu'un format : des vidéos de formats différents (16:9 et 9:16) demandent deux projets.
- Ne déplace pas un fichier pendant qu'un export le lit.

## Critères de qualité

- En ouvrant le dossier d'une vidéo, on trouve son script, ses rushs, ses sons et ses exports, sans rien chercher ailleurs.
- Chaque séquence a son script, coché scène par scène.
- Les quatre vidéos se ressemblent à l'image et au son.

## Outils

`organiser_rushs`, `ranger_media`, `creer_dossier_chutier`, `renommer_sequence`, `creer_sequence`, `activer_sequence`, `script_ecrire` (`sequence`), `script_lire`, `etat_timeline`, `motion_lister`, `exporter`.
