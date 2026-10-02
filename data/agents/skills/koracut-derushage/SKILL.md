---
name: koracut-derushage
description: "À lire après l'organisation et avant le montage : passer chaque rush, repérer les bonnes prises et les bons moments, écarter le reste, et poser le résultat dans une séquence de sélection."
---

# Le dérushage : garder, jeter, marquer

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/derushage.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire après l'organisation et avant le montage : passer chaque rush, repérer les bonnes prises et les bons moments, écarter le reste, et poser le résultat dans une séquence de sélection.

## Les étapes

1. **Partir de l'inventaire** (`koracut-organiser-inventaire-rushs`) : les rushs « écarter » ne se regardent plus.
2. **Pour chaque rush gardé**, repère les moments utiles :
   - Parole : dans la transcription, les phrases complètes, claires, sans hésitation au début. Note leurs bornes (images dans le média).
   - Image : les passages stables (pas de correction de cadre, pas de mise au point qui cherche), 2 secondes après le début de l'action. `voir_image` sur 2 ou 3 instants si le doute porte sur la netteté ou le cadre.
   - Plusieurs prises d'un même texte : la meilleure est celle où la phrase entière passe sans accroc, avec l'énergie juste. À égalité, la dernière (la personne est plus à l'aise).
3. **Marque** chaque bon moment dans le rush (`ajouter_marqueur` avec `media_id`, catégorie 0, texte court : « Réponse prix, complète »). Les défauts gênants en catégorie 1 (« Micro qui frotte »).
4. **Pose la sélection** (sur une demande longue, plus de 20 rushs) : une séquence `Nom - Sélects` (`koracut-organiser-sequences-versions`), les bons moments posés à la suite sur V1 (`ajouter_clip` avec `entree` et `sortie`), dans l'ordre du script. C'est la matière du bout-à-bout.
5. **Mets à jour l'inventaire** (colonne Verdict) et annonce le bilan : « Sur 1 h 12 de rushs, 9 min 40 gardées en 38 moments. »

## Critères pour garder

- La parole : phrase entière, audible, sans bruit qui masque un mot, sens clair hors contexte.
- L'image : nette sur le sujet, exposée (la peau lisible), cadre stable ou mouvement voulu, pas de passant qui regarde la caméra.
- Le son : pas de saturation, pas de vent dans le micro, pas de moteur qui couvre la voix.
- Un plan d'illustration se garde s'il montre quelque chose que la voix dit.

## Critères pour écarter

Flou sur le sujet, sous-exposition qui mange les visages, voix saturée ou couverte, faux départ, répétition moins bonne d'une prise meilleure, regard caméra non voulu. Écarter ne supprime rien : le rush reste, avec son verdict.

## Erreurs à éviter

- Garder tout « au cas où » : le montage devient une pêche dans 1 h de rushs.
- Juger une parole sur la transcription seule : un mot mal reconnu n'est pas forcément mal dit, une phrase propre à l'écrit peut être mal jouée. En cas de doute, marque « à écouter » et demande.
- Couper dans une phrase pour gagner du temps pendant la sélection : on sélectionne des phrases entières, on resserre au montage.

## Outils

`lister_medias`, `transcrire_media`, `voir_image`, `detecter_silences`, `ajouter_marqueur`, `creer_sequence`, `activer_sequence`, `ajouter_clip`, `script_ecrire`.
