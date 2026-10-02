---
name: koracut-genre-vlog
description: "À lire pour un vlog, une vidéo face caméra, une journée racontée, une vidéo de créateur : énergie, sauts de coupe assumés, la personne au centre."
---

# Monter un vlog

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/genre-vlog.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire pour un vlog, une vidéo face caméra, une journée racontée, une vidéo de créateur : énergie, sauts de coupe assumés, la personne au centre.

## La structure

1. **Accroche** : la phrase ou le moment le plus fort de la journée, avant tout « Salut ».
2. **Le fil** : les moments dans l'ordre de la journée ou de l'histoire, chacun avec un début et une fin.
3. **Les respirations** : un plan d'ambiance, une musique qui monte, entre deux moments.
4. **La fin** : une conclusion en une phrase, l'appel à l'action.

## Les règles

- Le face caméra se resserre fort : silences et hésitations retirés (`koracut-resserrer-parole`), sauts de coupe acceptés. Un léger recadrage alterné (100 %, puis 115 %) sur une coupe sur deux rend les sauts plus doux.
- Des B-roll du lieu et de ce qu'on raconte, courts (1 à 3 s).
- Musique sous la voix en continu, qui change avec les séquences.
- Textes à l'écran pour les mots forts, sous-titres pour les Reels.
- La personne reste elle-même : on ne coupe pas ses rires, ses expressions, sa façon de parler.

## Critères de qualité

- Le rythme ne retombe jamais plus de 10 s.
- On comprend l'histoire de la journée sans avoir été là.

## Erreurs à éviter

- Garder toute la mise en route (« attends, ça filme ? »).
- Une musique au même volume que la voix.
- Couper tous les sauts par des B-roll : le face caméra est le cœur du vlog.

## Outils

`detecter_silences`, `transcrire_media`, `couper_toutes_pistes`, `supprimer_clip`, `transformer_clip`, `ajouter_clip`, `ajouter_titre`, `sous_titres_poser`.
