---
name: koracut-script-monter-d-apres
description: "À lire quand un script existe et qu'on monte : suivre le déroulé scène par scène, relier chaque scène à sa place dans la timeline, cocher ce qui est fait, et signaler les écarts."
---

# Monter d'après le script

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/script-monter-d-apres.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un script existe et qu'on monte : suivre le déroulé scène par scène, relier chaque scène à sa place dans la timeline, cocher ce qui est fait, et signaler les écarts.

## Les étapes

1. `script_lire` (format `json`) : les identifiants du tableau (`deroule`) et des lignes (`s1`, `s2`…). Chaque séquence a son script : monte la séquence avec le sien (`sequence` si ce n'est pas la séquence active), les marqueurs de `relier_ligne` vont dans cette séquence.
2. **Scène par scène, dans l'ordre** : annonce la scène (« Scène 2 : l'atelier »), pose ses plans (colonne `plans`), cale la voix, respecte la durée à 20 % près.
3. **Relie la scène** à sa place : `script_ecrire` avec `relier_ligne` (`marqueur` au début de la scène, `poser_marqueur: true`), ou une zone (`debut`, `fin`) pour une scène longue.
4. **Coche** la colonne `fait` dès que la scène est montée (`modifier_cellule`, valeur `true`), dans le même appel que le lien.
5. **À la fin** : relis le script ; toutes les scènes sont cochées, la durée totale de la séquence est proche de celle du script.

## Les écarts

- Une scène impossible (pas de plan, plan inutilisable) : monte le reste, laisse la case vide, écris la raison dans la cellule `image` ou `son` (« Pas de plan de la façade de nuit »), et pose un marqueur catégorie 1.
- Une durée qui déborde de plus de 20 % : dis-le dans le résumé, propose où couper.
- Un meilleur ordre découvert en montant : ne le change pas de toi-même. Monte selon le script et propose le nouvel ordre à la personne.

## Critères de qualité

- Le script et la timeline racontent la même chose, dans le même ordre.
- Un clic sur une scène du script amène à sa place dans la timeline.
- Rien de monté qui ne soit dans le script, sauf les plans de coupe et l'habillage.

## Erreurs à éviter

- Cocher avant d'avoir vérifié la scène dans `etat_timeline`.
- Réécrire le script pour qu'il colle au montage fait.
- Oublier de relier les scènes : la personne perd le fil entre les deux panneaux.

## Outils

`script_lire`, `script_ecrire` (`relier_ligne`, `modifier_cellule`), `ajouter_clip`, `etat_timeline`, `ajouter_marqueur`.
