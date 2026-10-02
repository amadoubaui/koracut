---
name: koracut-etalonnage-methode-noeuds
description: "À lire avant tout étalonnage : l'arbre de nœuds d'un plan, un nœud par intention, l'ordre de travail et les valeurs de Resolve."
---

# L'étalonnage en nœuds, comme dans DaVinci Resolve

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-methode-noeuds.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant tout étalonnage : l'arbre de nœuds d'un plan, un nœud par intention, l'ordre de travail et les valeurs de Resolve.

## Le principe

Chaque plan a son arbre de nœuds, lu de gauche à droite, comme la page Color de Resolve. Un nœud = une intention, avec une étiquette courte. Un coloriste qui ouvre ton travail comprend tout d'un coup d'œil. Il existe aussi un arbre de séquence (`sequence: true`), appliqué après ceux des plans : il sert au look commun, jamais à corriger un plan.

## L'arbre type d'un plan

1. **CST** (seulement log ou HDR) : `etal_cst` avec la caméra. Toujours en tête.
2. **Balance** : Temp, Tons (tint), Offset. Le blanc devient blanc.
3. **Expo** : Lift, Gamma, Gain, Contraste, Pivot. Noirs, milieu, blancs.
4. **Secondaires**, s'il le faut : peau (qualificateur), ciel (fenêtre dégradé), visage (fenêtre cercle). En parallèle (`parallele`) quand deux zones se corrigent sur la même image, en `outside` pour corriger tout sauf la zone.
5. **Look** en dernier, dosé : `cle.sortie_gain` 0,3 à 0,7, ou `melange` d'une LUT.

## Les gestes

- Lire : `etal_lire` avant de toucher, pour connaître les `noeud_id`.
- Ajouter : `etal_noeud_ajouter` { type: `serie` | `serie_avant` | `fin` | `parallele` | `calque` | `outside` | `separateur`, `noeud_id`, `etiquette` }.
- Régler : `etal_noeud_regler` avec seulement les sections qui changent (`primaires`, `log`, `courbes`, `courbes_teinte`, `qualificateur`, `fenetres`, `flou`, `cle`, `cst`, `lut`).
- Couper un nœud pour comparer : `actif: false`, puis le remettre.
- Copier sur les plans de la même scène et de la même caméra : `etal_copier`, puis mesurer chaque plan.

## Les valeurs, celles de Resolve

Lift et Gamma neutres à 0, Gain 1, Offset 25, Contraste 1, Pivot 0,435, Saturation 50, Teinte 50, Lum Mix 100, Temp et Tons 0. Tableaux `[Y, R, V, B]` pour Lift, Gamma, Gain ; `[R, V, B]` pour Offset. Petits pas : Lift 0,01 à 0,03, Gamma et Gain 0,02 à 0,05, Temp 200 à 500.

Dans un nœud, l'ordre est fixe : CST, Boost, Temp et Tons, Lift Gamma Gain Offset, Contraste, courbes, roues Log, Teinte et Saturation, LUT, courbes de teinte. Pour faire une chose avant une autre, fais deux nœuds.

## La boucle

`mesurer_image` avant, un réglage, `mesurer_image` après, au même endroit du plan. Tu annonces les deux chiffres. Un réglage qui empire la mesure se retire (`actif: false` puis `etal_noeud_supprimer`) avant de continuer. `voir_image` une fois à la fin de la scène.

## Erreurs à éviter

- Tout faire dans un seul nœud : illisible et impossible à doser.
- Un nœud sans étiquette.
- Corriger un plan dans l'arbre de séquence.
- Deviner un log : demande la caméra.
- Poser le look avant la correction.

## Outils

`etal_lire`, `etal_noeud_ajouter`, `etal_noeud_regler`, `etal_noeud_supprimer`, `etal_cst`, `etal_lut`, `etal_copier`, `etal_reinitialiser` (demande confirmation), `etal_exporter_lut`, `mesurer_image`, `voir_image`, `activer_etalonnage`.
