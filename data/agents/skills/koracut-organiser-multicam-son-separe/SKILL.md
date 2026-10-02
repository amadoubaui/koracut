---
name: koracut-organiser-multicam-son-separe
description: "À lire quand un tournage a deux caméras ou plus (Cam A, Cam B, téléphone, drone) ou un son enregistré à part (enregistreur, micro-cravate) : les reconnaître, les appairer, les nommer, les caler."
---

# Plusieurs caméras et son séparé

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/organiser-multicam-son-separe.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un tournage a deux caméras ou plus (Cam A, Cam B, téléphone, drone) ou un son enregistré à part (enregistreur, micro-cravate) : les reconnaître, les appairer, les nommer, les caler.

## Reconnaître

- **Deux caméras sur la même prise** : deux vidéos de durées proches, tournées au même moment (noms d'origine de deux séries différentes, `C0003` et `GX010045`), même personne, deux valeurs de plan (large et serré). `voir_image` au même instant sur les deux le confirme.
- **Un son séparé** : un fichier son seul (`.wav`, `ZOOM0004`, `TASCAM_`), d'une durée proche d'une vidéo. La vidéo a souvent un son témoin, plus faible et plus lointain.
- **La Cam A** est celle qui porte le plan principal (souvent le plan moyen face au sujet), la **Cam B** le second angle. Un téléphone ou une GoPro d'appoint devient Cam C.

## Nommer

Même numéro pour toute la prise, suffixe par source (`koracut-organiser-nommage`) : `05 - Interview Awa - Cam A.mp4`, `05 - Interview Awa - Cam B.mp4`, `05 - Interview Awa - Son.wav`. Dans le chutier, un sous-dossier par caméra seulement au-delà d'une dizaine de prises (`01 Rushs/Cam A`, `01 Rushs/Cam B`), sinon tout ensemble et le suffixe suffit.

## Caler (synchroniser)

KoraCut n'a pas encore d'outil qui synchronise deux sources par la forme d'onde (voir `koracut-quand-un-outil-manque`). En attendant :

1. **Si un clap ou un geste net existe** (claquement de mains, clap) : repère-le dans chaque source. Sur la vidéo, `voir_image` autour de l'instant ; sur le son, `detecter_silences` montre le silence qui précède le claquement. Pose les deux sources l'une au-dessus de l'autre (`ajouter_clip`) pour que le clap tombe à la même image, puis vérifie en disant à la personne d'écouter : un décalage d'une ou deux images s'entend comme un écho.
2. **Sinon, par la parole** : `transcrire_media` sur les deux sources (niveau `mots`) et cale sur le début d'un même mot net (une consonne comme « p », « t », « k »). Précision : une à deux images.
3. **Le son séparé remplace le son témoin** : pose l'image avec `partie: "video"` et le son séparé sur la piste Voix ; ou garde le son témoin muet sur une piste à part (`regler_piste` avec `masquee`) pour vérifier la synchro plus tard.
4. **Marque la synchro** : un marqueur catégorie 2 au clap, commentaire « Synchro Cam A / Cam B ».

Annonce toujours la méthode et sa précision. Si la personne veut une synchro à l'image près sur une longue interview et qu'il n'y a ni clap ni mot net, dis-le : c'est un manque de KoraCut, noté comme tel.

## Monter à plusieurs caméras

Voir `koracut-interview-podcast-multicam` : on change d'angle sur le sens (qui parle, une réaction), jamais au hasard, et jamais plus d'un changement toutes les 3 secondes sur une parole calme.

## Erreurs à éviter

- Garder à la fois le son témoin et le son séparé audibles : effet de salle et écho.
- Numéroter Cam A et Cam B séparément (`05` et `06`) : on perd le lien entre les deux.
- Caler sur l'image seule quand le son est séparé : c'est le son qui fait foi.

## Outils

`lister_medias`, `voir_image`, `transcrire_media`, `detecter_silences`, `ajouter_clip`, `deplacer_clip`, `regler_piste`, `renommer_media`, `ajouter_marqueur`.
