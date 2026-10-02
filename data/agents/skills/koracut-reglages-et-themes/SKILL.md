---
name: koracut-reglages-et-themes
description: "À lire quand la personne demande un thème (clair, sombre, un style comme Matrix ou Spider-Man, ses couleurs) ou veut changer un réglage de l'application."
---

# Régler l'interface et l'application

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/reglages-et-themes.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand la personne demande un thème (clair, sombre, un style comme Matrix ou Spider-Man, ses couleurs) ou veut changer un réglage de l'application.

## Un thème livré d'abord

« Mode clair », « mode sombre », « suis mon ordinateur » : `theme_appliquer` avec `clair`, `sombre` ou `systeme`. Pour un style, regarde d'abord `theme_lister` : matrix, spider-man, punk, ocean, sable, nuit-violette, tres-sombre, gris et contraste-eleve existent déjà et sont vérifiés. « Reviens comme avant » : `theme_appliquer` avec `precedent`.

## Traduire un style en palette

Si rien ne va, `theme_creer`. Pars de trois couleurs, pas de dix :

1. **Le fond** : presque toujours très sombre ou très clair. Une couleur vive en fond fatigue en une heure de montage : garde-la pour l'accent. Spider-Man : fond bleu nuit #070A14, pas rouge.
2. **L'accent** : la couleur qui porte le style (rouge Spider-Man #FF3B45, vert Matrix #00FF41, rose punk #FF2E93). C'est celle des boutons principaux, du focus, de la sélection.
3. **La surface** : le fond éclairci d'une marche (sur un fond sombre, 5 à 8 % vers le texte), teinté du style.

Ajoute seulement si le style l'exige : `texte` teinté (Matrix : vert pâle #7DFF9A, lisible, et non le vert pur), `voile` (teinte des survols et filets), `bordure`, les `piste_*` (couleurs des clips, garde-les distinctes entre elles), `police: "mono"` pour un style terminal, `rayon: 0` pour un style brut.

Donne une `inspiration` d'une phrase. Un personnage, une marque ou un film sert d'ambiance : ne recopie pas de logo, de nom déposé dans les textes de l'interface, ni d'image.

## La lisibilité passe avant le style

Le moteur contrôle chaque palette (texte 4,5:1 sur tous les fonds, contours et accent 3:1) et corrige lui-même ce qui ne passe pas. Lis `corrections` dans la réponse et dis-les simplement : « J'ai éclairci le texte secondaire pour qu'il reste lisible. » Si la personne insiste pour une couleur illisible, explique pourquoi en une phrase et propose la plus proche qui passe.

## Retoucher

« Un peu plus clair », « l'accent en orange » : `theme_modifier` avec le seul jeton concerné. Sur un thème livré, une copie « perso » est créée : dis-le. `theme_supprimer` ne vaut que pour un thème créé, et demande l'accord.

## Les réglages

`reglages_lister` donne la liste blanche par section (sauvegarde, timeline, durees, lecture, proxy, nouveau_projet, agents, apercu). `reglages_ecrire` change une valeur et rend `avant` : garde-la pour pouvoir revenir. Dis quand ça s'applique (`applique`). Hors liste (chemins, programmes, porte de commande), réponds que ça se règle à la main dans les Préférences.

## Quand demander l'accord

Changer de thème ou un réglage de confort : fais-le, puis dis-le en une ligne avec comment revenir. Demande avant : supprimer un thème, couper la sauvegarde automatique ou la rallonger au-delà de 5 minutes, activer les proxys sur un gros projet (ça calcule longtemps).

## Erreurs à éviter

- Créer un thème quand un thème livré répond déjà à la demande.
- Un accent aussi sombre que le fond : on ne voit plus le focus.
- Taire une correction de contraste : la personne croirait avoir eu sa couleur exacte.

## Outils

`theme_lister`, `theme_appliquer`, `theme_creer`, `theme_modifier`, `theme_supprimer`, `reglages_lister`, `reglages_lire`, `reglages_ecrire`.
