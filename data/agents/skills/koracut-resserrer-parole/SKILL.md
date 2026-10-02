---
name: koracut-resserrer-parole
description: "À lire avant de retirer des silences, des « euh », des hésitations ou des répétitions dans une interview, une voix off, un vlog ou un podcast : couper entre les mots, jamais dedans, et garder la respiration."
---

# Resserrer une parole

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/resserrer-parole.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de retirer des silences, des « euh », des hésitations ou des répétitions dans une interview, une voix off, un vlog ou un podcast : couper entre les mots, jamais dedans, et garder la respiration.

## Les étapes

1. **Lire** : `etat_timeline` sur la zone, puis `detecter_silences` sur le clip de voix (seuil -40 dB, durée mini 20 images pour commencer).
2. **Croiser avec les mots** quand ça compte (client, propos sensible, plus de 10 coupes) : `transcrire_media` niveau `mots`. Les bornes des mots disent où finit vraiment une phrase et où sont les « euh », « hum », « donc donc ».
3. **Proposer** (`proposer_plan`) : combien de coupes, combien de secondes gagnées, ce que tu gardes exprès (une hésitation avant une réponse difficile, un rire).
4. **Verrouiller** les pistes qui ne doivent pas bouger (musique continue) avec `regler_piste`, en notant leur état de départ.
5. **Couper de la fin vers le début** : pour chaque passage, `couper_toutes_pistes` à début + 6 images, `couper_toutes_pistes` à fin - 6 images, `supprimer_clip` du morceau du milieu avec `refermer: true`. Tous les 3 ou 4 passages, relis la zone.
6. **Remettre** les pistes comme elles étaient, vérifier la synchro (image et son d'un même plan à la même position) et l'absence de vide.
7. **Couvrir les sauts d'image** : chaque coupe dans un plan fixe d'interview fait un « saut ». Couvre-le par un plan de coupe (`koracut-b-roll-plans-de-coupe`) ou, s'il n'y en a pas, alterne un léger recadrage (`transformer_clip`, 110 à 120 %) d'un morceau sur deux. Sur un vlog, le saut est accepté, c'est le style.

## Les repères

- Il reste environ 12 images de silence entre deux phrases à 25 i/s (6 de chaque côté du point de coupe).
- On retire les silences de plus de 20 images. En dessous, c'est le souffle naturel.
- Un « euh » isolé au milieu d'une phrase fluide ne se coupe pas s'il faut couper à moins de 4 images d'un mot : le raccord s'entendrait.
- La coupe automatique de KoraCut (menu Séquence, « Coupe automatique… ») fait les silences d'un coup ; propose-la à la personne pour une longue parole, elle garde la main sur les réglages.

## Critères de qualité

- On n'entend aucune coupe : pas de mot mangé, pas de souffle coupé en deux, pas de clic.
- Le sens de chaque phrase est intact.
- La personne à l'écran a toujours l'air de respirer.

## Erreurs à éviter

- Couper du début vers la fin : les positions suivantes bougent et les coupes tombent au mauvais endroit.
- Faire confiance à `detecter_silences` seul : un souffle ou une fin de mot douce passe sous le seuil.
- Retirer toutes les hésitations d'une personne émue : on lui retire son émotion.

## Outils

`etat_timeline`, `detecter_silences`, `transcrire_media`, `proposer_plan`, `regler_piste`, `couper_toutes_pistes`, `supprimer_clip`, `transformer_clip`, `ajouter_fondu`.
