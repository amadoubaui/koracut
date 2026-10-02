---
name: koracut-etalonnage-log-dji
description: "À lire quand un plan vient d'un drone ou d'une caméra DJI (Mavic 3, Air 3, Mini 4 Pro, Avata 2, Osmo Pocket 3, Osmo Action, Ronin 4D, Inspire)."
---

# Log DJI : D-Log, D-Log M

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-dji.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan vient d'un drone ou d'une caméra DJI (Mavic 3, Air 3, Mini 4 Pro, Avata 2, Osmo Pocket 3, Osmo Action, Ronin 4D, Inspire).

Lire d'abord `koracut-etalonnage-log-commun`.

## Choisir

| Profil | Geste |
|---|---|
| D-Log (Inspire, Mavic 3 Cine, Ronin 4D, Zenmuse) | `etal_cst` { `camera: "dji_dlog"` } |
| D-Log M (Mini 4 Pro, Air 3, Pocket 3, Action 4 et 5, Avata 2) | `etal_lut` avec la LUT DJI du modèle, dans le nœud de tête |
| HLG | sans `camera` : `entree_gamut: "rec2020"`, `entree_gamma: "hlg"` |
| Normal | pas de conversion |

D-Log M n'a pas de formule publiée commune : chaque modèle a sa LUT officielle (site de DJI, page du produit). Si la personne ne l'a pas, dis-le : le CST D-Log sur du D-Log M donne un résultat faux. Fiche `koracut-quand-un-outil-manque`.

## Repères

- D-Log : gris à 408 sur 1023. Mesure attendue après le CST : noirs vers 10 à 20, saturation au-dessus de 0,25.
- Les drones grand public en 8 bits (certains modes du Mini) : bandes dans le ciel. Contraste léger, pas de gros Gain.

## Le ciel et le paysage

Les plans de drone ont souvent un ciel trop clair et un sol sombre : un nœud « Ciel » avec une fenêtre `degrade` en haut, `hautes_lumieres` -20 à -40 dedans. Un peu de `boost` (+10 à +20) plutôt que de la saturation, pour garder les verts naturels.

## Pièges

- D-Log M converti comme du D-Log : noirs bouchés, dominante.
- Le mode « Normal » avec un CST : image écrasée.

## Outils

`etal_cst`, `etal_lut`, `mesurer_image`, `etal_noeud_regler` (`fenetres`, `primaires`).
