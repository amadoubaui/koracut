---
name: koracut-organiser-nommage
description: "À lire avant de renommer un rush, un son, un fichier, un dossier, un clip ou un média du chutier : la convention de nommage de KoraCut, la numérotation, et quand renommer le fichier sur le disque ou seulement son nom dans le chutier."
---

# Nommer et numéroter les rushs et les éléments

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/organiser-nommage.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de renommer un rush, un son, un fichier, un dossier, un clip ou un média du chutier : la convention de nommage de KoraCut, la numérotation, et quand renommer le fichier sur le disque ou seulement son nom dans le chutier.

## La convention

**Un rush : `NN - Description courte.ext`**

- `NN` : deux chiffres (trois au-delà de 99 rushs), dans l'ordre du script s'il existe, sinon dans l'ordre du tournage (date de création, puis nom d'origine de la caméra, qui se suit : `C0001`, `C0002`…).
- Description : trois à six mots, le contenu d'abord, sans article inutile : `Interview Awa`, `Façade boutique plan large`, `Mains qui cousent gros plan`, `Drone plage Saly`.
- Majuscule au premier mot et aux noms propres seulement. Accents gardés. Pas de `_` ni de `final`, `ok`, `bon`.
- Exemples : `01 - Accroche Awa face caméra.mp4`, `07 - Rue Sandaga plan large.mp4`, `12 - Drone Saly lever du soleil.mp4`.

**Plusieurs caméras sur la même prise** : même numéro, suffixe de caméra. `05 - Interview Awa - Cam A.mp4`, `05 - Interview Awa - Cam B.mp4`. Voir `koracut-organiser-multicam-son-separe`.

**Son séparé** : même numéro que l'image qu'il accompagne, suffixe `Son`. `05 - Interview Awa - Son.wav`. Un son sans image (ambiance, voix off) prend son propre numéro : `31 - Ambiance marché Tilène.wav`, `32 - Voix off version 1.wav`.

**Plusieurs prises d'un même plan** : même numéro, suffixe `Prise 1`, `Prise 2`. `09 - Accroche Awa - Prise 3.mp4`.

**Musique** : `M01 - Titre du morceau - Artiste.mp3`. Garde le vrai titre, c'est lui qui prouve la licence.

**Graphismes, images** : `G01 - Logo blanc.png`, `G02 - Photo boutique.jpg`.

**Les dossiers** : voir `koracut-organiser-arborescence` (`01 Rushs`, `02 Son`…).

**Les clips de la timeline** (`renommer_clip`) : seulement quand ça aide à lire la timeline, par le rôle du plan : `Accroche`, `Réponse 3`, `B-roll atelier`. Pas besoin de renommer chaque clip.

## Nom du chutier ou nom du fichier

- **Le nom dans le chutier** (`renommer_media`) ne touche rien sur le disque. Toujours possible, toujours d'abord.
- **Le nom du fichier** (`renommer_fichier_media`, ou `organiser_rushs` avec `disque: "renommer"`) renomme le vrai fichier, dans le dossier du projet, et le chutier suit. Le projet doit être enregistré (sinon `projet_absent` : demande à la personne de l'enregistrer) ; un fichier hors du dossier du projet répond `hors_projet` et ne bouge pas. Seulement si le plan accepté par la personne le dit (« renommer les fichiers sur le disque »). C'est annulable dans le tour. Si ton prompt t'interdit de toucher aux fichiers, tu t'en tiens au nom du chutier.
- Si les rushs sont encore sur la carte ou sur un disque externe, jamais de renommage de fichier : on renomme dans le chutier et on le dit.

## Les étapes

1. L'inventaire est fait (`koracut-organiser-inventaire-rushs`) : tu sais ce que montre chaque rush.
2. Décide l'ordre (script ou tournage) et dis-le dans le plan.
3. Un rush à la fois, dans l'ordre des numéros, sous les yeux de la personne : `renommer_media` avec le nom complet (« 03 - Plan large boutique.mp4 »), ou `renommer_fichier_media` si le renommage des fichiers est accordé (le chutier suit par défaut).
   Pour un lot (plus d'une vingtaine de rushs) : `organiser_rushs` en un seul appel. Le `plan` donne, dans l'ordre des numéros, chaque `media_id` avec sa description courte (`nom`, sans numéro ni extension : l'outil les ajoute) et son `dossier` ; `numero` impose le même numéro à la Cam A, la Cam B et le son d'une même prise ; `prefixe` « M » pour les musiques, « G » pour les graphismes (un appel par préfixe) ; `disque` vaut `non` (chutier seul, par défaut), `renommer` (le fichier prend le nom) ou `ranger` (renommé et déplacé dans le sous-dossier du même nom), ces deux derniers seulement si la personne l'a accepté. Si un nom est déjà pris, rien n'est fait et l'erreur liste les conflits : corrige le plan et relance.
4. Mets à jour la colonne Nom de l'inventaire (une cellule media suit le média, rien à faire) et garde le nom d'origine dans sa colonne.
5. Relis `lister_medias` : pas deux fois le même numéro pour deux prises différentes, pas de trou dans la numérotation sauf voulu.

## Erreurs à éviter

- Numéroter `1, 2, … 10` sans zéro : le tri alphabétique met `10` avant `2`.
- Un nom qui juge (`plan raté`, `nul`) : le verdict va dans l'inventaire, pas dans le nom.
- Renommer un fichier utilisé par un autre projet ou hors du dossier du projet.
- Renommer pendant qu'un export tourne sur ces fichiers : attends la fin.
- Changer la convention en cours de projet : si des rushs portent déjà une numérotation propre, garde-la et complète-la.

## Outils

`renommer_media`, `renommer_fichier_media`, `organiser_rushs`, `renommer_clip`, `lister_medias`, `script_ecrire`.
