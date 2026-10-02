---
name: koracut-export-formats-plateformes
description: "À lire avant d'exporter : quel modèle, quel format, quel niveau sonore, quel nom de fichier pour chaque plateforme, et comment sortir plusieurs versions. Mis à jour le 28 septembre 2026 avec le module Export (document 23, page Deliver de DaVinci Resolve) et l'étude `Recherche/DaVinci Resolve - page Deliver.md`."
---

# Exporter pour chaque plateforme

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/export-formats-plateformes.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant d'exporter : quel modèle, quel format, quel niveau sonore, quel nom de fichier pour chaque plateforme, et comment sortir plusieurs versions. Mis à jour le 28 septembre 2026 avec le module Export (document 23, page Deliver de DaVinci Resolve) et l'étude `Recherche/DaVinci Resolve - page Deliver.md`.

## D'abord : les modèles du module Export

`modeles_export` (action `lister`) donne les modèles, comme les tuiles de la page Export, et le modèle **déjà choisi pour ce projet** : à la création, KoraCut le règle d'après le type de vidéo (Recette de cuisine verticale : TikTok ; interview 16:9 : YouTube 1080p). La personne peut en changer, son choix est gardé dans le projet. Commence donc par lui, sauf si elle demande autre chose.

| Modèle | Taille visée | Débit plafond | Son | Durée max |
|---|---|---|---|---|
| `youtube_1080p` | 1920 × 1080 | 8 Mb/s (12 à 50-60 i/s) | AAC 384 kb/s, -14 LUFS | 12 h, 256 Go |
| `youtube_4k` | 3840 × 2160 | 45 Mb/s (68 à 50-60 i/s) | AAC 384 kb/s, -14 LUFS | 12 h |
| `youtube_shorts` | 1080 × 1920 | 8 Mb/s | AAC 384 kb/s, -14 LUFS | 3 min |
| `tiktok` | 1080 × 1920 | 12 Mb/s | AAC 192 kb/s, -14 LUFS | 10 min, 500 Mo (annonces) |
| `instagram_reels` | 1080 × 1920 | 12 Mb/s | AAC 192 kb/s, -14 LUFS | 3 min conseillées |
| `instagram_feed` (4:5), `instagram_carre` | 1080 × 1350, 1080 × 1080 | 10 Mb/s | AAC 192, -14 | 3 min conseillées |
| `instagram_stories`, `facebook_reels` | 1080 × 1920 | 10 Mb/s | AAC 192, -14 | 60 s par story, 90 s |
| `facebook` | 1920 × 1080 | 10 Mb/s | AAC 192, -14 | 240 min, 4 Go |
| `x` | 1920 × 1080 | 8 Mb/s | AAC 128, -14 | 140 s, 512 Mo sans abonnement |
| `linkedin` | 1920 × 1080 | 10 Mb/s (30 au plus) | AAC 192, -14 | 15 min, 5 Go |
| `snapchat` | 1080 × 1920 | 8 Mb/s | AAC 192, -14 | 60 s (Spotlight) |
| `vimeo` | 1920 × 1080 | 20 Mb/s | AAC 320, -14 | selon l'abonnement |
| `whatsapp` (statut) | 720 × 1280 | 1,5 Mb/s, profil Main | AAC 128, -14 | 60 s par segment, 16 Mo |
| `twitch` | 1920 × 1080 | 10 Mb/s | AAC 160, -14 | 10 Go |
| `master_h264`, `master_h265` | comme la séquence | Automatique | AAC 320, tel quel | |
| `prores_hq`, `prores_4444` (alpha) | comme la séquence | débit du codec | PCM 24 bits, tel quel (-23 pour la télévision) | |
| `audio_wav`, `audio_aac`, `audio_mp3` | son seul | | WAV 48 kHz 24 bits ; AAC 256 et MP3 320 à -16 (podcast) | |
| `gif`, `image_png` | 720 px de large ; image sous la tête de lecture | | sans | GIF : 15 s conseillées |

Ce qui est officiel et ce qui ne l'est pas : YouTube, Vimeo et LinkedIn publient leurs débits ; TikTok, Instagram, Facebook, Snapchat et X n'en publient pas (choix KoraCut). **Aucune plateforme vidéo ne publie de cible sonore** : -14 LUFS est la mesure de YouTube relevée par la communauté et la valeur de Spotify ; -16 est celle d'Apple Podcasts, -23 celle de la télévision européenne (EBU R128). Crête vraie : -1 dBTP partout (KoraCut limite à -1,5 avant l'encodage).

**La taille suit la séquence.** Un modèle 9:16 sur une séquence 16:9 garde la séquence (comme Resolve, où il faut cocher « Utiliser la résolution verticale ») ; `modeles_export` le dit dans `attention`. Un vrai 9:16 demande une séquence verticale préparée par le Monteur (`koracut-recadrer-vertical`).

## Les gestes

1. `modeles_export` action `lister` : le modèle recommandé et les réglages (`fichier_prevu` dit où ira le fichier : `Exports/` à côté du projet, nom `{projet} - {modele}`).
2. Si la personne veut autre chose : action `appliquer` avec `modele` et, au besoin, `reglages` (`debit_kbps`, `lufs`, `sous_titres` : `incruster`, `fichier` pour un `.srt` à côté, `sans`, `nom` avec les jetons `{projet} {sequence} {date} {heure} {format} {modele} {version}`, `entree_sortie` pour la zone du moniteur).
3. Un seul fichier : action `exporter` (accord de la personne). Plusieurs : action `ajouter` pour chacun (ils attendent), puis `lancer_file` (accord de la personne), comme « Tout exporter » dans la file de rendu.
4. Ne boucle pas sur `etat_exports` : la file de rendu montre la progression et le temps restant, et une notification arrive à la fin.
5. Une image fixe pour la miniature : `exporter_image` au meilleur plan, `Nom - vignette.png`, ou le modèle `image_png`.

Les anciens outils restent : `verifier_avant_export`, `estimer_export`, `exporter`, `exporter_serie` avec les préréglages `youtube_1080p`, `reels_tiktok_9x16`, `master_4k_prores`, `audio_wav`, `apercu_gif` ou `personnalise` (`lister_prereglages`). Utilise-les pour un préréglage précis de la bibliothèque, ou pour vérifier le projet avant un long rendu.

## Les durées à respecter

D'après le guide PIXEWORK : Reels 3 min au plus (idéal 15 à 45 s), TikTok 10 min (idéal 15 à 60 s), Shorts 3 min (idéal 20 à 60 s), stories 60 s par story, statut WhatsApp 60 s par segment (idéal 15 à 30 s, fichier sous 16 Mo, textes gros), fil Instagram et Facebook en 4:5 ou 1:1 (idéal 30 à 90 s), YouTube 8 à 20 min selon le sujet, spot télévision 15, 20, 30 ou 45 s. Zones sûres en 9:16 : rien d'important dans les 250 px du haut ni les 340 px du bas (bouton « Zones sûres » du module Export).

## Le nom et la place du fichier

`Exports/` à côté du projet, nom de la séquence et version : `Boutique Awa - 9x16 - v2.mp4` (`koracut-organiser-sequences-versions`). KoraCut n'écrit jamais par-dessus un fichier : le nom prend `v2`, `v3`. Avec les anciens outils, un fichier existant ne s'écrase pas sans l'accord de la personne pour ce fichier précis.

## Erreurs à éviter

- Exporter sans regarder le modèle recommandé ni l'avertissement de forme.
- Écraser un export existant sans accord.
- Publier : KoraCut écrit un fichier, la personne le met en ligne elle-même.

## Outils

`modeles_export`, `lister_prereglages`, `verifier_avant_export`, `estimer_export`, `exporter`, `exporter_serie`, `etat_exports`, `annuler_export`, `exporter_image`.
