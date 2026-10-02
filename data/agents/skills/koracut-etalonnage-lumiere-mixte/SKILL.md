---
name: koracut-etalonnage-lumiere-mixte
description: "À lire quand deux lumières de couleurs différentes se mélangent : fenêtre et ampoule, néon et soleil, écran et lampe, visage vert d'un côté et orange de l'autre."
---

# Étalonner une lumière mixte

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-lumiere-mixte.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand deux lumières de couleurs différentes se mélangent : fenêtre et ampoule, néon et soleil, écran et lampe, visage vert d'un côté et orange de l'autre.

## Reconnaître le cas

Une balance d'ensemble corrige une zone et casse l'autre. `dominante` bouge beaucoup quand tu mesures à deux endroits du plan. Les néons donnent une dominante verte (`dominante.v` positif), les ampoules une dominante orange.

## Choisir la lumière de référence

Celle qui éclaire le visage principal. Le reste peut garder un peu de sa couleur : une fenêtre un peu bleue dans un intérieur chaud, c'est naturel.

## L'arbre

1. « Balance » sur la lumière du visage : Temp et Tons. Contre un néon vert, Tons positif (vers le magenta), +5 à +20.
2. « Expo ».
3. « Zone 2 » : un nœud `parallele` qui isole l'autre lumière,
   - par une fenêtre si elle est à un endroit fixe (fenêtre, écran) ;
   - par le qualificateur sur sa teinte (bleu vers 75 à 85, vert vers 45 à 55, orange vers 22 à 30 sur l'échelle de 0 à 100) ;
   et dedans une Temp ou un Offset qui la rapproche, sans la neutraliser.
4. Si une dominante verte reste dans les ombres seulement : roues `log`, `ombres` [R, V, B] avec le vert en baisse.

## Critères

- Le visage à moins de 5° de sa ligne de peau.
- Les blancs de la scène à moins de 5 points d'écart entre canaux, sauf la lumière gardée volontairement.
- Pas de bord coloré autour des zones isolées : `flou` de la clé 5 à 15.

## Erreurs à éviter

- Neutraliser toutes les lumières : l'image devient plate et fausse.
- Corriger un néon par la Temp seule : c'est l'axe vert-magenta (Tons) qui compte.
- Un qualificateur trop large qui prend aussi la peau.

## Outils

`mesurer_image` (à deux endroits), `etal_noeud_ajouter` (`parallele`), `etal_noeud_regler` (`primaires.temp`, `primaires.tint`, `qualificateur`, `fenetres`, `log`), `voir_image`.
