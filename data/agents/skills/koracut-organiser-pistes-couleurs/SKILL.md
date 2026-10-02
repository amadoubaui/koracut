---
name: koracut-organiser-pistes-couleurs
description: "À lire avant de créer des pistes, en début de montage, ou quand une timeline devient difficile à lire : l'ordre des pistes, leurs noms, leurs couleurs, et les couleurs de clips pour se repérer."
---

# Nommer et colorer les pistes et les clips

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/organiser-pistes-couleurs.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de créer des pistes, en début de montage, ou quand une timeline devient difficile à lire : l'ordre des pistes, leurs noms, leurs couleurs, et les couleurs de clips pour se repérer.

## L'ordre et les noms

De haut en bas :

| Piste | Nom | Ce qu'on y pose | Couleur |
|---|---|---|---|
| V5 | Transitions | transitions dessinées (Motion) | gris |
| V4 | Animations | bandeaux, titres animés, logos (Motion) | violet |
| V3 | Titres | titres KoraCut, cartons | violet |
| V2 | B-roll | plans de coupe, illustrations | bleu_nuit |
| V1 | Plans | le plan principal, l'interview, l'histoire | bleu |
| A1 | Voix | interview, voix off, son du plan principal | vert |
| A2 | Ambiances | sons d'ambiance, son témoin | turquoise |
| A3 | Bruitages | whoosh, impacts, pops | jaune |
| A4 | Musique | la musique | orange |

Les sous-titres ont leur piste à eux (native), à part. Les pistes « Animations » et « Transitions » sont créées par KoraCut au besoin, on ne les renomme pas.

Si le projet vient de la fenêtre Nouveau projet, les pistes existent déjà avec des noms : garde-les. Tu ne crées pas une piste si une piste libre du bon type existe, et tu nommes toute piste que tu crées.

## Les couleurs de clips

`couleur_clip` sert à repérer, pas à décorer. Une convention par projet, annoncée une fois :

- rouge : à revoir, problème (flou, son abîmé)
- vert : validé par la personne
- jaune : plan en attente d'un choix
- violet : réponse forte d'une interview, à garder coûte que coûte

Pour une interview à plusieurs voix, une couleur par personne (Awa en vert, Moussa en bleu) aide à lire la timeline d'un coup d'œil.

## Les étapes

1. `etat_timeline` : quelles pistes existent, leurs noms.
2. Renomme celles qui portent un nom par défaut (`regler_piste` avec `nom`, ou `renommer_piste`).
3. Crée seulement ce qui manque (`ajouter_piste` avec `nom` et `rang`).
4. Colore les pistes (`couleur_piste`) si le montage a plus de quatre pistes.
5. Relis l'état.

## Erreurs à éviter

- Des pistes nommées « Vidéo 3 », « Audio 2 » à la livraison.
- La musique sur la même piste que la voix : impossible de faire le ducking proprement.
- Huit pistes vides « au cas où » : elles alourdissent la timeline sur un petit écran.
- Colorer tous les clips de couleurs différentes : plus rien ne ressort.

## Outils

`etat_timeline`, `ajouter_piste`, `regler_piste`, `renommer_piste`, `couleur_piste`, `couleur_clip`, `renommer_clip`.
