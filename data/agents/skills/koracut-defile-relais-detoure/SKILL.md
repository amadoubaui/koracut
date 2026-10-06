---
name: koracut-defile-relais-detoure
description: "À lire avant de monter un défilé de tenues filmé sur pied, une personne par rush. Fiche du Monteur, lue à la demande par `lire_competence`. Écrite le 2 octobre 2026, après le montage Khasim (9 personnes, 27 secondes)."
---

# Défilé en relais : la personne suivante entre avant que la précédente soit sortie

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/defile-relais-detoure.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de monter un défilé de tenues filmé sur pied, une personne par rush. Fiche du Monteur, lue à la demande par `lire_competence`. Écrite le 2 octobre 2026, après le montage Khasim (9 personnes, 27 secondes).

## Le principe

Chaque rush montre une personne qui entre, s'arrête, se retourne et sort. Pour que deux personnes soient dans le cadre en même temps, on pose l'entrée de la suivante, détourée, par-dessus la sortie de la précédente.

| Piste | Ce qu'on y met |
|---|---|
| V2 | l'entrée de la personne suivante, détourée (`detourer_sujet`, sujet `personne`), de son entrée dans le cadre jusqu'à son arrêt |
| V1 | pour chaque personne : la pose (accélérée), puis la sortie |

Quand l'entrée détourée se termine, V1 passe au rush de cette personne, à l'instant de son arrêt. Le raccord ne se voit pas si les rushs sont calés sur le même cadre.

## Les étapes

1. **Vérifier le format du projet avant d'importer** (`projet info`) : un premier rush en 4K 60 i/s peut changer le profil du projet. Tous les calculs d'images en dépendent.
2. **Repérer quatre instants par rush** : entrée dans le cadre, arrêt, départ, sortie du cadre. La trajectoire se lit sur le détourage (centre de la silhouette image par image).
3. **Caler les rushs entre eux** avant tout : mesurer échelle, rotation et décalage du décor contre un rush de référence (points du décor, la personne masquée), puis `transformer_clip` avec `rotation_degres` et un recadrage commun de 5 à 7 %. Le même réglage vaut pour le clip détouré tiré du même rush.
4. **Poser sur les temps de la musique** : une demi-mesure pour l'entrée et la sortie, une pour la pose. La pose longue s'accélère (`changer_vitesse_clip`, 250 à 350 %), l'entrée et la sortie restent entre 100 et 200 %.
5. **Finir la sortie** : prendre la source jusqu'à une demi-seconde après la sortie du cadre, sinon la personne disparaît d'un coup au raccord.
6. **Étalonner par rush**, le même arbre pour tous les plans d'un rush (clip détouré compris), avec une luminosité visée commune.

## Contrôle

Exporter, puis regarder une planche de 4 à 6 images par seconde : à chaque relais, deux personnes dans le cadre. Mesurer le décor de part et d'autre de chaque raccord : moins de 2 px d'écart.
