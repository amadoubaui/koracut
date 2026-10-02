---
name: koracut-organiser-marqueurs
description: "À lire avant de poser un marqueur sur la timeline ou sur un rush : les catégories, le texte, et quand un marqueur aide la personne au lieu de l'encombrer."
---

# Poser des marqueurs utiles

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/organiser-marqueurs.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de poser un marqueur sur la timeline ou sur un rush : les catégories, le texte, et quand un marqueur aide la personne au lieu de l'encombrer.

## Les catégories (convention des agents)

| Catégorie | Sens | Exemple de commentaire |
|---|---|---|
| 1 | à revoir, problème, manque | « Flou de mise au point 2 s » ; « Manque : synchro caméras par le son » |
| 2 | synchro musique, clap | « Temps fort musique » ; « Clap Cam A / Cam B » |
| 3 | chapitre, scène du script | « Scène 2 : l'atelier » |
| 0 | repère neutre | « Début de la réponse sur les prix » |

## Sur un rush (dans le chutier)

Pendant le dérushage, marque les bons moments dans le rush lui-même (`ajouter_marqueur` avec `media_id` et la position dans le média) : « Bonne réponse prix », « Rire », « Plan stable ». Le monteur les retrouve au moment de poser le plan.

## Sur la timeline

- Un marqueur de chapitre par scène du script, relié à sa ligne (`script_ecrire`, `relier_ligne` avec `poser_marqueur: true`) : la personne clique la scène dans le script et arrive au bon endroit.
- Un marqueur « à revoir » à chaque endroit où tu as un doute ou un manque : c'est la liste de ce que la personne doit regarder.
- Des marqueurs de synchro sur les temps forts de la musique avant de caler les coupes.

## Le texte

Court (80 caractères au plus), concret, sans jugement : ce qu'il y a, pas ce que tu en penses. Un timecode n'a rien à faire dans le texte : le marqueur est déjà à sa place.

## Erreurs à éviter

- Un marqueur par coupe : la timeline devient illisible.
- Laisser des marqueurs « à revoir » déjà traités : retire-les (`supprimer: true`) quand le problème est réglé.
- Un marqueur de chapitre qui ne suit pas le script : si l'ordre change, mets à jour le lien.

## Outils

`ajouter_marqueur`, `script_ecrire` (`relier_ligne`), `etat_timeline` (les marqueurs existants).
