---
name: koracut-verifier-le-montage
description: "À lire à la fin de chaque étape importante et avant tout export : la liste de contrôle, ce qui se vérifie par les outils, ce qui se regarde, et ce qu'on demande à la personne de regarder elle-même."
---

# Vérifier le montage avant de le dire fini

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/verifier-le-montage.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire à la fin de chaque étape importante et avant tout export : la liste de contrôle, ce qui se vérifie par les outils, ce qui se regarde, et ce qu'on demande à la personne de regarder elle-même.

## Ce qui se vérifie par l'état (`etat_timeline`)

- **Vides** : aucun trou sur la piste vidéo principale, sauf voulu.
- **Synchro** : l'image et le son d'un même plan ont le même `pos - in`.
- **Chevauchements** anormaux, clips minuscules oubliés (moins de 5 images) après des coupes.
- **Durée** totale dans la cible du type de vidéo et de la plateforme.
- **Pistes** nommées, aucune piste en solo ou muette par erreur.
- **Titres** et bandeaux présents, à leur place, durée de lecture suffisante.
- **Script** : toutes les scènes cochées (`script_lire`).

## Ce qui se mesure

- Son : `mesurer_audio` sur le mixage (niveau et crête dans la cible).
- Couleur : `mesurer_image` sur un plan par scène (noirs, blancs, peau).
- Export : `verifier_avant_export` (médias manquants, images noires, format, niveau).

## Ce qui se regarde

`voir_image` (avec l'accord de la personne) sur les points à risque, 3 à 6 images au plus :

- la première image (l'accroche n'est ni noire ni un fondu) ;
- un titre ou un bandeau (lisible, dans la zone sûre, sans faute) ;
- un sous-titre en 9:16 (au-dessus de la zone des boutons) ;
- un plan recadré (sujet dans le cadre) ;
- la dernière image (appel à l'action et contact lisibles).

## Ce que la personne doit regarder elle-même

Tu ne peux pas tout juger : les coupes qui s'entendent, le ton d'une voix, un bruit gênant, l'émotion d'une scène. Donne-lui deux ou trois timecodes précis, dans le résumé, et pose un marqueur catégorie 1 sur chacun. Rappelle le contrôle du guide : regarder la vidéo en entier sur téléphone et sur ordinateur, avec et sans le son.

## Critères de qualité

- Rien d'annoncé comme fait qui ne soit vérifié dans l'état.
- Les problèmes trouvés sont corrigés ou dits, jamais passés sous silence.

## Erreurs à éviter

- Dire « c'est fini » sur la foi de la réponse d'un outil sans relire l'état.
- Regarder vingt images au lieu de vérifier l'état : les chiffres d'abord, l'image pour ce que les chiffres ne disent pas.

## Outils

`etat_timeline`, `script_lire`, `mesurer_audio`, `mesurer_image`, `verifier_avant_export`, `voir_image`, `ajouter_marqueur`, `rendre_apercu` (pour que la personne regarde sans saccade).
