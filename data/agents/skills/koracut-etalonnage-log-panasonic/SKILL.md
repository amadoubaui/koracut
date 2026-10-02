---
name: koracut-etalonnage-log-panasonic
description: "À lire quand un plan vient d'une Panasonic ou d'une Lumix en log (GH5, GH6, GH7, G9 II, S5, S5 II, S1H, BGH1, EVA1, Varicam)."
---

# Log Panasonic : V-Log, V-Log L

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-panasonic.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan vient d'une Panasonic ou d'une Lumix en log (GH5, GH6, GH7, G9 II, S5, S5 II, S1H, BGH1, EVA1, Varicam).

Lire d'abord `koracut-etalonnage-log-commun`.

## Choisir

| Profil | `camera` |
|---|---|
| V-Log / V-Gamut | `panasonic_vlog` |
| V-Log L (GH5, G9 premier du nom) | `panasonic_vlog` (même courbe) |
| HLG | sans `camera` : `entree_gamut: "rec2020"`, `entree_gamma: "hlg"` |

## Repères

- Gris 18 % : 433 sur 1023 (42 %). Noir vers 128.
- **V-Log L** a la même courbe, mais le haut est écrêté vers 80 IRE : les hautes lumières saturent plus tôt. Après le CST, ne cherche pas de détail au-dessus : `hautes_lumieres` négatif pour un blanc doux.
- Les GH5 en 8 bits font des bandes : pas de gros contraste.

## Pièges

- Le « Cinelike D2 » ou « Cinelike V2 » n'est pas du log : c'est déjà du Rec.709 doux. Pas de CST, juste Contraste 1,1 à 1,2 et un peu de saturation.
- Le LUT « V-Log vers V-709 » de Panasonic donne un rendu propre à la marque : il remplace le CST, il ne s'ajoute pas.

## Outils

`etal_cst`, `etal_lut`, `mesurer_image`.
