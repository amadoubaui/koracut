---
name: koracut-rythme-respiration
description: "À lire quand on passe du bout-à-bout au montage fin : durée des plans, variété, couper dans le mouvement, laisser respirer, caler sur la musique."
---

# Le rythme et la respiration

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/rythme-respiration.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand on passe du bout-à-bout au montage fin : durée des plans, variété, couper dans le mouvement, laisser respirer, caler sur la musique.

## Les repères de durée (à 25 i/s)

| Ce qu'on monte | Durée d'un plan d'illustration | Remarque |
|---|---|---|
| Reel, TikTok, Short | 1 à 3 s (25 à 75 images) | un changement visuel toutes les 1 à 3 s |
| Publicité, teaser | 1 à 3 s, accélération vers la fin | le meilleur plan d'abord |
| YouTube, film d'entreprise | 3 à 6 s | plus long sur un plan large ou un drone |
| Documentaire, reportage | 4 à 10 s | on laisse voir |
| Parole | le temps de la phrase | on ne coupe jamais un mot |

## Les règles

- **Varier.** Trois plans de même durée à la suite sonnent mécanique, sauf si la musique l'impose. Alterne court et long, large et serré.
- **Couper dans le mouvement.** Entrer dans un plan quand l'action a commencé, en sortir avant qu'elle s'arrête. Un geste (une main qui prend, une porte qui s'ouvre) qui continue d'un plan à l'autre cache la coupe.
- **Respirer.** Après une phrase forte, une émotion, une révélation : 1 à 2 secondes d'image sans parole. Entre deux phrases d'une interview resserrée : environ 12 images de silence.
- **La relance.** En format long, un changement de rythme toutes les 30 à 60 secondes : nouvelle scène, musique qui change, plan inattendu.
- **Avec la musique.** Coupe sur les temps forts (pose d'abord des marqueurs catégorie 2 sur les temps). Tous les temps, c'est trop : les temps forts d'une mesure sur deux, et une coupe exactement sur le temps (pas une image avant).
- **Finir net.** La dernière image dure le temps de lire l'appel à l'action (3 à 5 s), puis coupe ou fondu au noir de 12 à 25 images.

## Les étapes

1. Regarde la durée totale contre la cible (`etat_timeline`).
2. Resserre d'abord la parole (`koracut-resserrer-parole`), puis raccourcis les plans d'illustration trop longs (`rogner_clip` avec `refermer`), puis enlève les scènes qui ne portent pas le message.
3. Vérifie la variété : liste les durées des plans de V1 et V2 ; casse les séries de durées égales.
4. Relis l'état, vérifie la synchro et les vides.

## Erreurs à éviter

- Couper vite partout parce que « c'est pour TikTok » : une émotion a besoin de temps, même en 9:16.
- Couper sur une image fixe au milieu d'un mouvement de caméra qui va s'arrêter : le plan « freine » à l'écran.
- Rallonger pour atteindre une durée : la bonne durée est la plus courte possible pour le message.

## Outils

`etat_timeline`, `rogner_clip`, `couper_clip`, `supprimer_clip`, `supprimer_espace`, `deplacer_clip`, `ajouter_marqueur`, `detecter_silences`.
