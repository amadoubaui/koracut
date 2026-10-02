---
name: koracut-etalonnage-nuit
description: "À lire pour un plan de nuit : rue éclairée, fête, concert, intérieur la nuit, nuit américaine."
---

# Étalonner une scène de nuit

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-nuit.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire pour un plan de nuit : rue éclairée, fête, concert, intérieur la nuit, nuit américaine.

## Ce qu'on cherche

La nuit doit rester la nuit. Des noirs profonds mais pas bouchés, des sources de lumière qui brillent, des visages lisibles. On n'éclaircit pas une nuit en jour.

## L'arbre

1. « Balance » : garde la couleur des sources (lampadaires oranges, écrans bleus). On retire seulement une dominante qui salit les visages.
2. « Expo » :
   - Noirs entre 5 et 15 (un peu plus bas que le jour), `noirs_ecrases_pourcent` sous 3 %.
   - Moyenne entre 40 et 80.
   - `ombres` +10 à +20 pour les visages, Gamma à peine (+0,02).
3. « Bruit » : `mid_detail` -10 à -25, et désaturer les noirs par la courbe `lum_sat` : `[[0, 0.7], [0.15, 1], [1, 1]]`. Le bruit coloré des ombres disparaît.
4. « Visages » si besoin : qualificateur de peau en `parallele`, Gamma +0,03.
5. Look en dernier : souvent des ombres légèrement froides (roues `log`, `ombres` bleu +0,01 à +0,02) et des sources chaudes. Dosé.

## Nuit américaine (jour tourné, nuit voulue)

Gain -0,2 à -0,35, Temp -600 à -1000, saturation 35 à 40, ciel assombri par une fenêtre `degrade`. Mesure : moyenne 40 à 60. Fais-le valider sur un plan avant de copier.

## Critères

- Les sources lumineuses peuvent être à 255, c'est normal.
- Pas de bruit coloré visible dans les noirs (`voir_image` une fois).
- Les visages restent à moins de 7° de leur ligne de peau.

## Erreurs à éviter

- Remonter la moyenne au-dessus de 100 : c'est le jour, et le bruit apparaît.
- Neutraliser les lampadaires : la nuit perd son âme.
- Monter la saturation : les ombres deviennent sales.

## Outils

`mesurer_image`, `etal_noeud_ajouter`, `etal_noeud_regler` (`primaires`, `courbes_teinte.lum_sat`, `log`, `qualificateur`, `fenetres`), `voir_image`, `rendre_apercu`.
