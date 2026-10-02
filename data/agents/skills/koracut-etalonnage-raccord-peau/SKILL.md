---
name: koracut-etalonnage-raccord-peau
description: "À lire quand plusieurs plans d'une même scène doivent se ressembler (deux caméras, deux moments, un drone et un plan au sol) et chaque fois qu'un visage est à l'image."
---

# L'étalonnage, étape 2 : raccorder les plans et soigner les peaux

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-raccord-peau.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand plusieurs plans d'une même scène doivent se ressembler (deux caméras, deux moments, un drone et un plan au sol) et chaque fois qu'un visage est à l'image.

## Raccorder

1. Choisis la référence de la scène, corrigée à fond (`koracut-etalonnage-correction`).
2. **Même caméra, même lumière** : `etal_copier` de la référence vers les autres plans (tout l'arbre de nœuds), puis une mesure de chacun.
3. **Autre caméra ou autre moment** : chaque plan garde son CST, puis un nœud « Raccord » réglé à la main (Offset pour l'ensemble, Gain et Lift par canal) jusqu'aux mesures de la référence. `matcher_plans` peut donner un premier pas.
4. **Vérifie** chaque plan contre la référence : écart de luma moyenne sous 8 points, dominante sous 4 points.
5. Un drone et une caméra au sol ne seront jamais identiques : on les rapproche, c'est tout.

## Les peaux

- Toutes les peaux tombent sur la même ligne du vecteurscope : `peau.ecart_ligne_peau_degres` sous 5 degrés.
- Une peau foncée bien exposée se mesure plus bas en luma qu'une peau claire (souvent 60 à 110) : c'est juste, on ne la remonte pas vers 150.
- Le modelé d'un visage foncé vit dans les ombres et les bas tons moyens : pour du contraste, monte les tons moyens et les hautes lumières avant de descendre les ombres.
- Peau verte ou magenta : c'est la teinte de la balance. Peau orange : saturation ou température trop hautes.
- Saturation rarement au-dessus de 60 (neutre 50) : les couleurs du Sénégal (wax, pirogues, latérite) sont déjà fortes. `boost` touche moins les peaux.
- Une peau à part : un nœud `parallele` « Peau » avec le `qualificateur` HSL, teinte `centre` 20 à 28 (échelle 0 à 100 : rouge 16,7, jaune 33,3 ; une peau foncée se mesure vers 20 à 22), `largeur` 6 à 15, `doux` 4 à 8, `flou` 5 à 15, et `lum.haut` à 60 pour ne pas prendre les blancs cassés. Si le décor a la teinte de la peau (bois, cannage, mur corail), ajoute une fenêtre sur le visage (fiche `koracut-etalonnage-fenetres-visage`). Dedans, `teinte` à 49 ou 51 pour la ramener vers sa ligne, `mid_detail` -10 à -20 pour adoucir.

## Critères de qualité

- Dans une scène, on ne sent pas le changement de caméra à la coupe.
- Les visages ont l'air vivants, ni gris, ni orange, ni cireux.

## Erreurs à éviter

- Raccorder avant d'avoir corrigé la référence.
- Juger une dominante sur le décor au lieu de la peau.
- Pousser la saturation pour « plus de peps » : un peu de contraste en S fait mieux.

## Outils

`mesurer_image`, `etal_copier`, `etal_lire`, `etal_noeud_ajouter`, `etal_noeud_regler` (`primaires`, `qualificateur`), `matcher_plans`, `voir_image`.
