---
name: koracut-voix-off-synthese
description: "À lire quand il faut une voix off générée (publicité, motion design, voix de guide) ou remplacer une voix de guide dans un montage : moteur, réglages, vérification par Whisper, recalage des plans."
---

# Voix off de synthèse

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/voix-off-synthese.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand il faut une voix off générée (publicité, motion design, voix de guide) ou remplacer une voix de guide dans un montage : moteur, réglages, vérification par Whisper, recalage des plans.

## Le moteur

- **Par défaut, en local et gratuit : Kyutai TTS 1.6B en/fr**, voix française CML-TTS 4482 (homme, grave, posé). Poids et voix en CC-BY 4.0 : usage publicitaire permis, **crédit obligatoire** (« Voix : Kyutai TTS, voix CML-TTS 4482, CC-BY 4.0 ») dans les licences du projet.
- **Si une clé ElevenLabs est posée** (`~/.config/elevenlabs/cle`) : meilleure encore, droits commerciaux dès l'offre Starter.
- **Jamais** pour une pub : F5-TTS, XTTS-v2, Voxtral en local, Fish Audio (licences non commerciales). Jamais la voix clonée d'une vraie personne sans son accord écrit.
- Kokoro (`ff_siwis`) reste bon pour un repère rapide, pas pour une livraison : jugé robotique le 28 septembre 2026.

## Les étapes

1. **Un fichier par plan**, jamais une voix d'un seul tenant : chaque plan garde sa place et sa durée. Un appel de `voix_off_generer` = une phrase = un fichier.
2. **Avant la première voix** : `voix_off_voix` dit quels moteurs sont prêts et quelles voix existent. Si le moteur local manque (`moteur_absent`), **dis à la personne la taille (environ 4,1 Go) et le dossier** (`~/.cache/koracut-voix`, `~/.cache/huggingface`), demande son accord, puis appelle `voix_off_installer` (il demande aussi la confirmation) ; l'installation tourne en fond, `voix_off_voix` dit où elle en est. Python absent (`python_absent`) : dis-lui d'installer Python depuis python.org.
3. **Le texte à dire** peut différer du sous-titre : `texte` garde l'orthographe vraie (Whisper vérifie contre lui), `dire` porte la prononciation : `afebaba.com` se dit « afébaba point com », `TV` se dit « télé », `HLM` « H L M ».
4. **Générer et poser** : `voix_off_generer { texte, dire?, voix?, ton?, vitesse?, dossier?, poser: { position } }`. Le fichier va dans `Voix off/NN - Premiers mots.wav` du projet, le son dans le dossier « Voix off » du chutier, et avec `poser` sur la piste audio « Voix off » (créée si besoin). Avec `dossier` (relatif au dossier du projet, « Script 01/02 Son », créé au besoin), le fichier va là et le dossier du chutier suit le même chemin : c'est ce qu'il faut dans un projet rangé par script ou par séquence. Sans position : à la tête de lecture, ou juste après ce qui l'occupe (`decalee`). Une étape d'annulation par appel. Compte 15 à 60 s par phrase en local.
5. **Vérifier la réponse** : `fidelite` 1 = tous les mots entendus. Si `a_reecouter` est vrai, lis `entendu` et `mots_douteux`, réécris le mot fautif dans `dire`, régénère, puis retire l'ancienne avec `retirer_media`. Vérifie à part les noms de marque, les prix, les chiffres (« 24 à 48 heures », « 0 F CFA »). « Wave » entendu « waive » est juste (prononciation anglaise). Un nom de marque écrit autrement par Whisper (« Coracut » pour KoraCut) est bien dit.
6. **Le traitement est déjà fait** par l'outil : bords de silence, pauses ramenées à 0,36 s au plus, passe-haut 75 Hz, -2 dB à 300 Hz, +2 dB à 3,5 kHz, de-esseur, compression douce (ratio 2,5), -16 LUFS, crête -1,5 dBTP. N'ajoute pas un second traitement dans la timeline.
7. **Le crédit** (`credit` dans la réponse, moteur local) va dans les licences du projet.

Hors application (Claude Code, sans KoraCut ouverte), `Sources/koracut-voix/voix_off.py phrases.json --dossier <projet>/02 Voix off` fait la même chaîne.

## Remplacer une voix de guide dans un montage

1. **Copie de sauvegarde** du projet d'abord (le `.koracut` et son dossier `motion/`).
2. Mesure l'écart de durée de chaque plan. Si la nouvelle phrase est plus longue, **rallonge le plan** (un plan Motion se rallonge : son animation est refaite à la nouvelle durée, sa sortie reste à la fin) et décale la suite ; plus courte, raccourcis-le. Règle d'un plan : 0,34 s avant la voix, la voix, 0,56 s après.
3. Pose chaque voix sur la même piste (« Voix guide » ou « Voix off »), à la même place relative dans son plan.
4. Si les gestes d'animation sont calés sur des mots, refais le calage sur la nouvelle voix (transcription mot à mot par Whisper), sinon les gestes tombent à côté.
5. Refais le ducking (`appliquer_ducking`, musique 8 dB sous la voix), les sous-titres calés sur la nouvelle voix (`sous_titres_poser`), puis `normaliser_loudness` à -14 et l'export sous un nouveau nom (`- v2`), en gardant la v1.
6. Écoute finale : transcris le fichier exporté avec Whisper et compare au script.

## Dans l'application

La personne peut aussi le faire elle-même : panneau Script, bouton micro (« Voix off de la sélection » ou « de tout le script, scène par scène »), ou clic droit sur une ligne du tableau. Elle choisit la voix, écoute chaque prise gardée, puis pose tout en une étape. Si elle préfère le faire ainsi, dis-le lui plutôt que de générer à sa place.

Moteurs : `local` (Kyutai, gratuit, sur Mac, Windows et Linux ; sans carte graphique NVIDIA ni puce Apple il calcule sur le processeur, compte une à deux minutes par phrase), `elevenlabs` et `openai` quand une connexion avec sa clé existe dans Préférences > Connexions IA (payants ; OpenAI oblige à annoncer une voix générée). Avec un service en ligne, le texte à dire part chez lui : ne le choisis que si la personne l'a branché.

## Critères de qualité

- Tous les mots entendus par Whisper, marques et prix compris.
- Même voix, même niveau sur toute la vidéo.
- Un rythme de pub : pas de pause de plus d'une demi-seconde, pas de phrase pressée.

## Outils

`voix_off_voix`, `voix_off_generer`, `voix_off_installer`, `retirer_media`, `ajouter_clip`, `motion_poser`, `appliquer_ducking`, `sous_titres_poser`, `normaliser_loudness`, `mesurer_audio`, et hors application `voix_off.py`.
