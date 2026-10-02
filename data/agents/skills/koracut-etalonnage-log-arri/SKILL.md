---
name: koracut-etalonnage-log-arri
description: "À lire quand un plan vient d'une ARRI (Alexa Mini, Mini LF, Amira, Alexa 35) ou d'un fichier livré en LogC."
---

# Log ARRI : LogC3, LogC4

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-log-arri.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan vient d'une ARRI (Alexa Mini, Mini LF, Amira, Alexa 35) ou d'un fichier livré en LogC.

Lire d'abord `koracut-etalonnage-log-commun`.

## Choisir

| Caméra | `camera` |
|---|---|
| Alexa Mini, Mini LF, LF, SXT, Amira (ALEXA Wide Gamut 3) | `arri_logc3` |
| Alexa 35, Alexa 265 (ALEXA Wide Gamut 4) | `arri_logc4` |

## Repères

- LogC3 : gris à 400 sur 1023, courbe prise à EI 800 (la valeur par défaut des caméras). Un plan tourné à un autre EI se voit à peine sur le gris, un peu sur les noirs et les hautes lumières : on corrige après le CST, dans « Expo ».
- LogC4 : gris à 285 sur 1023. La latitude est immense : le CST avec tone mapping DaVinci tient les hautes lumières, garde-le.

## Pièges

- Du LogC4 converti en LogC3 (ou l'inverse) : tout est faux, noirs et couleurs. Alexa 35 = LogC4, toujours.
- Les ProRes ARRI sont souvent en « Log » ou en « Rec.709 » selon le réglage de sortie : un fichier déjà contrasté n'a pas besoin de CST.
- Le fichier ARRIRAW (.ari, .mxf ARRIRAW) ne s'ouvre pas dans KoraCut : c'est un manque, fiche `koracut-quand-un-outil-manque`.

## Outils

`etal_cst`, `mesurer_image`.
