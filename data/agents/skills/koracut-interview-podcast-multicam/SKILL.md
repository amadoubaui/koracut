---
name: koracut-interview-podcast-multicam
description: "À lire pour une interview, un podcast filmé, un débat ou une table ronde, avec une ou plusieurs caméras : structure, choix de l'angle, resserrage, habillage minimal."
---

# Monter une interview ou un podcast à plusieurs caméras

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/interview-podcast-multicam.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire pour une interview, un podcast filmé, un débat ou une table ronde, avec une ou plusieurs caméras : structure, choix de l'angle, resserrage, habillage minimal.

## La structure

- **Interview courte (réseaux, 30 s à 2 min)** : la meilleure phrase en accroche, puis qui parle (bandeau nom et fonction), puis trois à cinq réponses fortes, une fin qui conclut. On ne garde pas les questions, sauf si la réponse ne se comprend pas sans.
- **Podcast filmé (long)** : l'ordre de la conversation, resserré. Un extrait fort de 20 à 30 s en ouverture (le teaser), puis la conversation. Chapitres par sujet (marqueurs catégorie 3).
- **Extraits de podcast (Reels)** : un seul propos par extrait, de 30 à 60 s, qui se comprend sans le reste, en 9:16 recadré sur celui qui parle.

## Choisir l'angle (plusieurs caméras)

1. Pose la caméra principale (plan large ou celui qui cadre tout le monde) sur V1, les autres sur V2 et V3, calées (`koracut-organiser-multicam-son-separe`), un seul son de référence sur A1.
2. Change d'angle pour une raison : celui qui parle (plan serré sur lui), une réaction qui compte (celui qui écoute rit, fronce les sourcils), un nouveau sujet (retour au plan large).
3. Pour montrer un angle, tu coupes les plans de la piste du dessus pour ne garder que les passages voulus (`couper_clip`, `supprimer_clip` sans `refermer`) : la piste la plus haute qui a une image est celle qu'on voit.
4. Rythme : pas plus d'un changement toutes les 3 s sur une parole calme ; un plan large toutes les 30 à 60 s pour situer.
5. Change d'angle sur une fin de phrase ou une respiration, ou en L-cut (on entend encore celui qui finit, on voit déjà celui qui répond).

## Resserrer

Voir `koracut-resserrer-parole`. Sur un podcast, on garde les rires et les relances naturelles : c'est ce qui fait la conversation. On retire les faux départs, les « attends, je recommence », les longs blancs techniques.

## Habiller

- Un bandeau nom et fonction à la première apparition de chaque personne, 3 à 5 s (`koracut-habillage-titres-bandeaux`).
- Sous-titres pour les extraits réseaux, toujours (`koracut-sous-titres`).
- Pas de transition dessinée entre deux angles : la coupe franche.

## Erreurs à éviter

- Changer d'angle au hasard ou toutes les deux secondes : l'œil se fatigue.
- Montrer celui qui parle au moment exact où il commence, à chaque fois : un peu d'anticipation ou de retard (J et L) rend la conversation vivante.
- Garder deux sons de micro ouverts en même temps sans mixage : écho, bruit de salle.

## Outils

`etat_timeline`, `ajouter_clip`, `couper_clip`, `supprimer_clip`, `rogner_clip`, `detecter_silences`, `transcrire_media`, `ajouter_marqueur`, `transformer_clip`, `voir_image`.
