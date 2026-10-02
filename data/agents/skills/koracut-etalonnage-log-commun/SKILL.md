---
name: koracut-etalonnage-log-commun
description: "À lire dès qu'un plan est gris et plat (noirs vers 30 à 40, blancs vers 180, saturation sous 0,2) : reconnaître le log, poser le CST, puis corriger."
---

# Un plan en log : ce qui vaut pour toutes les caméras

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-commun.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire dès qu'un plan est gris et plat (noirs vers 30 à 40, blancs vers 180, saturation sous 0,2) : reconnaître le log, poser le CST, puis corriger.

## Reconnaître

- `mesurer_image` : `luma.min` 25 à 45, `luma.max` 170 à 200, `saturation_moyenne` sous 0,2.
- Le nom du fichier, le dossier, la caméra citée par la personne, le script. Si rien ne le dit : **tu demandes** la caméra et le profil. Un mauvais log écrase les noirs et donne une dominante.

## Le geste

1. `etal_lire` : un CST existe peut-être déjà.
2. `etal_cst` { `clip_id`, `camera` } : pose un nœud « CST » en tête. Sortie Rec.709 gamma 2,4, tone mapping DaVinci, compression de gamut (les défauts, ne pas les changer sans raison).
3. `mesurer_image` : les noirs doivent descendre vers 8 à 20 et la saturation remonter au-dessus de 0,25. Sinon, le log n'est pas le bon, ou les niveaux sont faux (ci-dessous).
4. Ensuite seulement : « Balance », « Expo », etc. (`koracut-etalonnage-methode-noeuds`).

## CST ou LUT du constructeur

Le CST est calculé d'après les formules publiées par chaque marque : il garde toute l'information et on corrige après. Une LUT constructeur (`etal_lut` dans le nœud de tête) marche aussi, mais elle écrête ce qui dépasse. On prend la LUT quand la personne la demande, ou quand la caméra n'a pas de `camera` dans `etal_cst` (DJI D-Log M, Samsung Log). Rien n'est téléchargé : le fichier `.cube` doit être sur la machine.

## Les niveaux

Un fichier log lu en « plein » au lieu de « vidéo » décale noirs et gris : noirs trop bas ou trop hauts après le CST, alors que le log est le bon. C'est l'erreur la plus fréquente. Dis-le, sans corriger à l'aveugle.

## Exposition du tournage

Beaucoup tournent le log surexposé de +1 à +2 diaphragmes pour un grain plus propre : après le CST, l'image paraît trop claire. C'est voulu : baisse l'Offset (-2 à -6) ou le Gain dans « Expo », pas le CST.

## Plusieurs caméras dans une scène

Un CST par caméra, puis on raccorde (`koracut-etalonnage-raccord-peau`). `etal_copier` seulement entre plans de la même caméra et du même profil.

## Erreurs à éviter

- Corriger un log sans CST : on ne rattrape jamais les couleurs.
- Deux conversions : un CST plus une LUT log vers Rec.709.
- Un look avant le CST.

## Outils

`mesurer_image`, `etal_lire`, `etal_cst`, `etal_lut`, `etal_noeud_regler` (`cst`), `etal_copier`.
