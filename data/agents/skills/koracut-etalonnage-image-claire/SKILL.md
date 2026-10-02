---
name: koracut-etalonnage-image-claire
description: "À lire quand un plan est trop clair : moyenne au-dessus de 150, ciel blanc, peau brillante, soleil de midi, blancs à 255."
---

# Étalonner une image claire ou surexposée

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-image-claire.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan est trop clair : moyenne au-dessus de 150, ciel blanc, peau brillante, soleil de midi, blancs à 255.

## Reconnaître le cas

- `luma.moyenne` au-dessus de 150, `blancs_brules_pourcent` au-dessus de 1 %.
- Ce qui est à 255 dans le fichier n'a plus d'information : on ne le récupère pas, on le rend propre (blanc doux, sans bord coloré). Tu le dis à la personne.
- Un log mal converti paraît aussi délavé : vérifie qu'un CST n'est pas faux ou absent (`etal_lire`).

## L'ordre

1. Nœud « Balance ».
2. Nœud « Expo » :
   - **Hautes lumières** (`primaires.hautes_lumieres`, -20 à -60) : récupère le ciel et les blancs sans assombrir le reste. C'est le premier geste.
   - **Gain** en baisse (-0,03 par pas) si les blancs restent au-dessus de 240.
   - **Gamma** en baisse (-0,02 à -0,04) pour redonner de la densité aux tons moyens.
   - **Lift** en baisse très légère (-0,01) seulement si les noirs sont au-dessus de 25.
3. **Contraste** 1,05 à 1,15 si l'image reste plate après la baisse.
4. Ciel encore blanc : un nœud « Ciel » avec une fenêtre `degrade` en haut (`doux` 0,1 à 0,2) et un Gain en baisse dedans.
5. Saturation : une image surexposée paraît délavée. `boost` +10 à +20 plutôt que la saturation.

## Critères

- Blancs entre 225 et 238, `blancs_brules_pourcent` sous 1 % (sauf soleil ou lampe dans le champ).
- Moyenne entre 95 et 130.
- Pas de bord cyan ou magenta autour des zones brûlées.

## Soleil de midi au Sénégal

Ombres dures sous les yeux, murs blancs, sable. On baisse les hautes lumières, on ouvre un peu les Basses lumières (+10) pour les visages, et on ne cherche pas à garder le détail du ciel à tout prix.

## Erreurs à éviter

- Baisser le Gain seul : les blancs deviennent gris.
- Assombrir toute l'image pour sauver le ciel.
- Promettre de sauver une zone blanche pure.

## Outils

`mesurer_image`, `etal_noeud_ajouter`, `etal_noeud_regler` (`primaires` : hautes_lumieres, gain, gamma, contraste, boost ; `fenetres`), `voir_image`.
