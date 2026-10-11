---
name: koracut-flouter-vie-privee
description: "À lire avant d'exporter tout montage où un écran, un document ou un tiers apparaît. C'est toi qui cherches, la personne n'a pas à te le demander."
---

# Flouter ce qui est privé à l'écran

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/flouter-vie-privee.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant d'exporter tout montage où un écran, un document ou un tiers apparaît. C'est toi qui cherches, la personne n'a pas à te le demander.

## Ce qu'on cherche

Regarde chaque plan qui montre un écran de téléphone ou d'ordinateur, un papier, une plaque, un badge :

- un **nom de contact**, une photo de profil, un « vu à » dans une messagerie ;
- un numéro de téléphone, une adresse e-mail ou postale, une plaque d'immatriculation ;
- des onglets de navigateur, un historique, des notifications qui n'ont rien à voir avec la vidéo ;
- le visage d'une personne qui n'a pas donné son accord, un enfant.

Le message ou l'écran que la vidéo veut montrer reste net : on floute ce qui identifie quelqu'un, pas l'histoire.

## Comment

Avec l'outil `flouter_zone` : un appel par zone. `zone` { x_px, y_px, largeur_px, hauteur_px } se donne dans l'image que tu regardes (`repere: "image"`, celle de `voir_image` ou d'`exporter_image`) ou dans l'image du rush (`repere: "source"`, par défaut) ; `suivre` à vrai (par défaut) suit la zone quand le téléphone bouge ; `type` : `flou`, `pixels` ou `plein` ; `etiquette` : ce que c'est (« nom WhatsApp »). `flous_lister` rend les flous posés, pour les retrouver et les retirer (`supprimer_effet`). Si la réponse dit `suivi_perdu`, le rectangle est resté fixe : coupe le plan plus court ou pose le flou à la main.

À la main, quand l'outil n'est pas dans ta liste ou que le suivi décroche : l'effet `opencv.tracker` posé par `ajouter_effet`, avec `shape_width` 0 (pas de cadre), `blur` 45, `blur_type` 1, et dans `results` le rectangle à plusieurs images : `début=x y largeur hauteur;milieu=…;fin=…`.

- Les images de `results` se comptent **dans le média source** : ajoute le point d'entrée du clip (son `in`) à chaque position, sinon le flou reste figé sur la dernière valeur.
- L'effet se pose après le Transformer : les coordonnées sont celles de l'image finale, en pixels du projet.
- Un rectangle un peu large (20 px de marge) vaut mieux qu'un rectangle juste qui laisse dépasser une lettre.
- Si ce qui est privé n'apparaît qu'en cours de plan, garde le rectangle à 2 px jusqu'à l'image d'avant.

## Vérifier

Exporte trois images par plan flouté (début, milieu, fin) et regarde-les : le nom est couvert aux trois, rien d'utile n'est caché. Un téléphone tenu en main ne bouge pas en ligne droite : trois images clés au moins.

## Outils

`flouter_zone`, `flous_lister`, `ajouter_effet`, `regler_effet`, `exporter_image`, `supprimer_effet`.
