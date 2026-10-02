---
name: koracut-organiser-sequences-versions
description: "À lire avant de créer, renommer, dupliquer ou activer une séquence, et avant de nommer un export : la convention des séquences et des versions, pour que la personne sache toujours laquelle est la bonne."
---

# Nommer les séquences et les versions

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/organiser-sequences-versions.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de créer, renommer, dupliquer ou activer une séquence, et avant de nommer un export : la convention des séquences et des versions, pour que la personne sache toujours laquelle est la bonne.

## Le nom d'une séquence

`Nom du projet - Format - vN`, et un mot de plus si besoin :

- `Boutique Awa - 16x9 - v1`
- `Boutique Awa - 9x16 - v1`
- `Boutique Awa - 9x16 - v2 teaser 15 s`
- `Boutique Awa - Ours` (le bout-à-bout avant le montage fin, sans numéro de version)
- `Boutique Awa - Sélects` (les bonnes prises posées à la suite, pour le dérushage)

Le format s'écrit `16x9`, `9x16`, `1x1`, `4x5` (pas de deux-points : il est interdit dans un nom de fichier sous Windows).

## Quand créer une séquence

- Une séquence de travail par étape qui mérite d'être gardée : `Sélects`, `Ours`, puis la version montée.
- Une nouvelle version (`v2`) quand la personne demande un changement important et qu'on veut garder l'ancienne : `dupliquer_sequence` avec le `nom` de la nouvelle version (`… - v2`) : la copie s'affiche, on travaille dedans. Pour une petite retouche, on reste dans la même version : l'historique suffit.
- Un autre format de la même vidéo : une séquence à part (`9x16`). `dupliquer_sequence` avec `orientation: "verticale"` part du montage horizontal (les plans ne sont pas recadrés : ensuite `koracut-recadrer-vertical`), `creer_sequence` avec `orientation` part de zéro. Le projet n'a qu'un profil d'image, que la séquence affichée retourne : lis `note` dans la réponse et dis-la à la personne si le format diffère.

## Travailler sur la bonne séquence

1. `activer_sequence` avant de monter : c'est celle que la personne voit et celle que les outils modifient.
2. Annonce laquelle : « Je travaille dans Boutique Awa - 9x16 - v2. »
3. Ne supprime jamais une séquence : une ancienne version se garde.

## Les versions d'export

Le fichier porte le nom de la séquence et sa date si la personne livre souvent : `Boutique Awa - 9x16 - v2.mp4` dans `05 Exports`. Si le fichier existe déjà, on ne l'écrase pas : on passe à `v3` ou on demande. Un extrait ou une image fixe dit ce qu'il est : `Boutique Awa - 9x16 - v2 - vignette.png`.

## Les étapes

1. Lis l'état : `etat_timeline` donne la séquence ouverte, `lister_medias` avec `type: "sequence"` donne toutes les autres.
2. Renomme la séquence de départ (souvent « Séquence 1 ») : `renommer_sequence` vers `Nom - Format - v1`.
3. Crée ou duplique selon le besoin, active, annonce.

## Erreurs à éviter

- `final`, `final2`, `vraiment final` : toujours un numéro de version.
- Monter dans une séquence que la personne ne voit pas.
- Dupliquer pour chaque petite retouche : dix séquences presque pareilles, personne ne s'y retrouve.

## Outils

`creer_sequence`, `renommer_sequence`, `activer_sequence`, `dupliquer_sequence`, `etat_timeline`, `exporter` (le nom du fichier).
