---
name: koracut-etalonnage-image-sombre
description: "À lire quand un plan est trop sombre : moyenne sous 80 dans `mesurer_image`, visages bouchés, intérieur mal éclairé, fin de journée."
---

# Étalonner une image sombre ou sous-exposée

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-image-sombre.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan est trop sombre : moyenne sous 80 dans `mesurer_image`, visages bouchés, intérieur mal éclairé, fin de journée.

## Reconnaître le cas

- `luma.moyenne` sous 80, `luma.max` sous 200 alors que la scène a des blancs.
- Si `noirs_ecrases_pourcent` dépasse 5 % dès le départ, ces noirs sont perdus dans le fichier : on les laisse noirs, on ne remonte pas du bruit.
- Une image grise et plate n'est pas sombre : c'est un log (fiche de la caméra).

## L'ordre

1. Nœud « Balance » d'abord : une dominante se voit mal dans le sombre, elle éclate quand on éclaircit.
2. Nœud « Expo » :
   - **Gain** d'abord (0,05 par pas, jusqu'à 1,4 environ) : remonte les blancs à 220 à 235.
   - **Gamma** ensuite (+0,03 par pas) : ouvre les tons moyens, là où vivent les visages.
   - **Lift** en dernier, très peu (+0,01 à +0,02) : seulement si les noirs sont sous 8.
   - Ou **Offset** (+1 à +3 sur les trois canaux) si toute l'image est trop basse d'un bloc.
3. **Ombres** (`primaires.ombres`, +10 à +30) pour ouvrir les ombres d'un visage sans toucher au reste.
4. Si le bruit monte : `mid_detail` légèrement négatif (-10 à -20), et on s'arrête plus tôt. Mieux vaut un plan un peu sombre et propre qu'un plan clair et granuleux.

## Critères

- Moyenne entre 85 et 120 pour un intérieur, entre 95 et 130 en extérieur jour.
- Noirs entre 8 et 20, `noirs_ecrases_pourcent` sous 1 %.
- La peau reste à moins de 5° de sa ligne : éclaircir déplace souvent la teinte vers le jaune.

## Peaux foncées

Tout le modelé est dans les ombres. On ouvre au Gamma et aux Ombres, jamais au Lift seul qui grise la peau. On garde la densité : une peau foncée bien exposée n'est pas une peau claire.

## Erreurs à éviter

- Monter le Lift pour tout régler : image laiteuse, noirs gris.
- Pousser le Gain au point de brûler une fenêtre ou un boubou blanc.
- Saturer pour compenser : l'image sombre paraît terne, la saturation n'y change rien.

## Outils

`mesurer_image`, `etal_lire`, `etal_noeud_ajouter`, `etal_noeud_regler` (`primaires` : gain, gamma, lift, offset, ombres, mid_detail), `voir_image`.
