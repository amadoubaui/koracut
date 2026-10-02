---
name: koracut-etalonnage-log-sony
description: "À lire quand un plan vient d'une Sony en log (FX3, FX30, FX6, A7S III, A7 IV, ZV-E1, Venice, anciennes A7S) : quel `camera` choisir."
---

# Log Sony : S-Log3, S-Log2

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-sony.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan vient d'une Sony en log (FX3, FX30, FX6, A7S III, A7 IV, ZV-E1, Venice, anciennes A7S) : quel `camera` choisir.

Lire d'abord `koracut-etalonnage-log-commun` pour la méthode.

## Choisir

| Profil de la caméra | `camera` |
|---|---|
| S-Log3 / S-Gamut3.Cine (le plus courant) | `sony_slog3_sgamut3cine` |
| S-Log3 / S-Gamut3 | `sony_slog3_sgamut3` |
| S-Log2 (A7S, A7 II, FS7 anciens) | `sony_slog2` |
| HLG (profils HLG, HLG1 à 3) | sans `camera` : `entree_gamut: "rec2020"`, `entree_gamma: "hlg"` |

Si la personne dit « S-Log3 » sans le gamut, prends `sony_slog3_sgamut3cine` et dis-le comme une hypothèse : c'est le réglage des Picture Profiles PP8 et PP9 et des modes Cine EI.

## Repères

- Gris 18 % en S-Log3 : 41 % du code (420 sur 1023). Noir vers 95.
- Tourné « au gris » : ombres bruitées. Beaucoup surexposent de +1 à +2 : après le CST, baisse l'Offset.
- En 8 bits (A7 III, A6400…), le S-Log3 fait des bandes dans le ciel : pas de gros Gain, contraste modéré.

## Pièges

- Un S-Log2 converti comme un S-Log3 : noirs trop bas, contraste dur.
- Les PP HLG ne sont pas du log : ne pas les passer en `sony_slog3`.

## Outils

`etal_cst`, `mesurer_image`, `etal_lire`.
