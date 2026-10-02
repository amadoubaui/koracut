---
name: koracut-etalonnage-log-canon
description: "À lire quand un plan vient d'une Canon en log (C70, C300, C500, R5, R5 C, R6, R7, R8, R3, EOS R, C100)."
---

# Log Canon : C-Log, C-Log 2, C-Log 3

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-canon.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan vient d'une Canon en log (C70, C300, C500, R5, R5 C, R6, R7, R8, R3, EOS R, C100).

Lire d'abord `koracut-etalonnage-log-commun`.

## Choisir

| Profil | `camera` |
|---|---|
| Canon Log 3 (le plus courant sur les hybrides) | `canon_log3` |
| Canon Log 2 (cinéma, plus de latitude) | `canon_log2` |
| Canon Log (le premier : C100, C300, EOS R au début) | `canon_log` |
| HLG / PQ (vidéo HDR) | sans `camera` : `entree_gamut: "rec2020"`, `entree_gamma: "hlg"` ou `"pq"` |

Le gamut est Cinema Gamut dans les trois cas. Si la caméra enregistrait en gamut BT.709 ou BT.2020 (réglage « Color Gamut » de certaines Canon), passe par `etal_cst` sans `camera`, avec `entree_gamut` `rec709` ou `rec2020` et `entree_gamma` `clog`, `clog2` ou `clog3`.

## Repères

- Gris 18 % : 351 sur 1023 en Canon Log et Canon Log 3, 407 en Canon Log 2.
- Canon Log 2 est très plat : il demande plus de contraste après le CST (1,1 à 1,2) et une caméra 10 bits.

## Pièges

- Confondre Log 2 et Log 3 : noirs décalés, contraste faux. Demande, ou regarde le nom de profil dans les réglages de la personne.
- La 8 bits en Canon Log : bandes dans le ciel.

## Outils

`etal_cst`, `mesurer_image`, `etal_lire`.
