---
name: koracut-genre-reportage
description: "À lire pour un reportage, un sujet d'information, un documentaire court, une vidéo d'événement ou de témoignages : raconter une histoire vraie avec des voix, des lieux et des faits, sans les trahir."
---

# Monter un reportage ou un documentaire court

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/genre-reportage.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire pour un reportage, un sujet d'information, un documentaire court, une vidéo d'événement ou de témoignages : raconter une histoire vraie avec des voix, des lieux et des faits, sans les trahir.

## La structure

1. **Ouverture** : une scène forte ou une image qui situe (lieu, ambiance), avec son son direct.
2. **L'enjeu** en une phrase (voix off ou témoin).
3. **Les séquences**, chacune avec un lieu, un personnage, une idée : témoignage, images qui le prouvent, retour au personnage.
4. **La chute** : ce qui a changé, ce qui reste à faire, une dernière image qui reste en tête.

## Les règles

- **Le son direct compte** : laisser respirer une ambiance (le marché, la mer, l'atelier) 2 à 3 s au début d'une séquence avant la voix.
- **Les témoins** : une personne = un bandeau nom et fonction ou rôle, à sa première apparition. Orthographe du nom vérifiée auprès de la personne.
- **Fidélité** : on ne coupe pas une phrase de manière à lui faire dire autre chose ; on ne monte pas une réaction sur une question qui n'a pas été posée.
- **Voix off** : posée d'abord, les images se calent dessus (`koracut-b-roll-plans-de-coupe`).
- **Chiffres et dates** : écrits à l'écran, tels que donnés par la source, jamais arrondis de toi-même.
- **Événement** : l'ordre de la journée, avec les moments clés (discours, remise, foule) et les visages.

## Critères de qualité

- Chaque affirmation a une image ou une voix qui la porte.
- On sait toujours qui parle et où on est.
- Le montage respecte ce que les gens ont dit.

## Erreurs à éviter

- Une musique dramatique sur un sujet qui ne l'est pas.
- Des B-roll génériques qui ne viennent pas du lieu.
- Montrer un visage (mineur, personne qui a refusé) sans autorisation : si le doute existe, demande.

## Outils

`transcrire_media`, `script_ecrire`, `ajouter_clip`, `rogner_clip`, `ajouter_titre`, `motion_element_seul` (bandeaux, par le Motion designer), `ajouter_marqueur`, `sous_titres_poser`.
