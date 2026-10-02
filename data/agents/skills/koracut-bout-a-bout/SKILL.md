---
name: koracut-bout-a-bout
description: "À lire quand le dérushage est fait et qu'on construit la première version : poser l'histoire dans l'ordre du script, sans finesse, pour voir si elle tient avant de rythmer et d'habiller."
---

# Le bout-à-bout (l'ours)

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/bout-a-bout.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand le dérushage est fait et qu'on construit la première version : poser l'histoire dans l'ordre du script, sans finesse, pour voir si elle tient avant de rythmer et d'habiller.

## Les étapes

1. **Active la séquence de travail** (`Nom - Format - v1`) et nomme les pistes (`koracut-organiser-pistes-couleurs`).
2. **Pose la colonne vertébrale** sur V1 et A1 : la parole (interview, voix off) ou, sans parole, les plans qui racontent. Dans l'ordre du script, scène par scène, bout à bout, sans trou (`ajouter_clip` avec `entree` et `sortie` pris dans les marqueurs du dérushage, position = fin du clip précédent).
3. **Ne cherche pas encore le rythme** : des phrases entières, des plans un peu trop longs. On resserre à l'étape suivante.
4. **Pose la musique** sur A4 si elle porte la structure (clip, pub) : elle donne les points de coupe.
5. **Relis la séquence** : `etat_timeline`, durée totale, aucun vide sur V1.
6. **Annonce** : « Ours posé : 1 min 52 pour 1 min 30 visée, 14 plans. Je resserre ? » Sur une demande longue, c'est un bon moment pour laisser la personne regarder.

## Critères de qualité

- L'histoire se comprend en regardant l'ours seul, sans habillage.
- Chaque scène du script a sa place et son marqueur.
- L'ours dure 10 à 30 % de plus que la cible : il reste de la matière à couper.

## Erreurs à éviter

- Poser les plans de coupe avant la parole : ils se calent sur la parole, pas l'inverse.
- Ajouter titres, effets et couleur dès l'ours : tout sera à refaire si l'ordre change.
- Un ours plus court que la cible : on ne rallonge pas un montage fini avec du remplissage.

## Outils

`activer_sequence`, `etat_timeline`, `lister_medias`, `ajouter_clip`, `supprimer_espace`, `ajouter_marqueur`, `script_ecrire`.
