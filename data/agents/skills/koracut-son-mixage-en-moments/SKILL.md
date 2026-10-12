---
name: koracut-son-mixage-en-moments
description: "À lire quand une publicité, un Reel ou un film court doit avoir un vrai mixage : plusieurs moments musicaux, des silences, des effets doux, et pas une seule musique qui tourne du début à la fin."
---

# Le son, étape 4 : un mixage en plusieurs moments

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/son-mixage-en-moments.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand une publicité, un Reel ou un film court doit avoir un vrai mixage : plusieurs moments musicaux, des silences, des effets doux, et pas une seule musique qui tourne du début à la fin.

Demande d'Amadou Ba (1er octobre 2026) : « un son s'arrête, d'autres réactions arrivent, un autre son arrive », et des sons « modernes, doux, classes », jamais « piquants ».

## Découper le film en moments

Un moment par idée du script, deux à quatre pour 30 secondes. Exemple d'une pub de 30 s : accroche calme (0 à 10 s), le produit avec un rythme qui arrive (10 à 24 s), la signature qui se pose (24 à 30 s). Écris-les dans la colonne `son` du script avant de poser quoi que ce soit.

Trois façons de changer de moment, de la plus douce à la plus marquée :
1. **La même musique qui évolue** : pose-la pour que l'entrée de sa batterie tombe sur la coupe (elle était calme, elle démarre).
2. **Un silence** : la musique sort en 8 à 12 images, 10 à 20 images de vide avec un seul effet (un pop, une note), l'autre musique entre sur son temps fort.
3. **Une montée** : un effet qui monte sur 1 seconde, finit sur la coupe, et la nouvelle musique entre avec un coup de basse discret.

## Caler une musique sur l'image

- Trouve ses temps forts : `analyser_media` (`quoi: "plans"` ne sert pas ici) ou `mesurer_audio` par tranches ; hors application, les attaques de grosse caisse se lisent dans les graves (sous 160 Hz). Note la seconde où la batterie entre et la durée d'une mesure.
- Pose le clip avec `entree` choisie pour que ce temps fort tombe sur la coupe voulue : `entree = temps_fort - (position_de_la_coupe - position_du_clip)`.
- Deux musiques de tempos différents ne se fondent pas l'une dans l'autre : sépare-les par un silence ou une montée.

## Choisir des sons doux

Tu n'entends pas : choisis sur mesure et dis-le. Un son pique quand son énergie est entre 2 et 6 kHz. Dans la banque du projet, garde ceux dont cette part est faible (moins de 5 %), au centre de gravité bas (sous 1 500 Hz) : souffles d'air, pops ronds, impacts graves, carillons feutrés. Écarte les scintillements, les clics secs, les cymbales. Une musique : part des aigus au-dessus de 5 kHz sous 2 %, et pas tout dans l'infra-grave (plus de 80 % sous 120 Hz ne s'entend pas sur un téléphone).

## Les niveaux

- Voix : -16 LUFS par phrase, jamais retouchée ensuite.
- Musique : `regler_gain_clip` -6 à -9 dB, puis `appliquer_ducking` 8 à 9 dB sous la voix.
- Effets : -10 à -14 dB, un seul à la fois, posés 2 à 4 images AVANT le geste à l'image (le son précède).
- Impact grave ou coup de basse sur une entrée de musique : -9 à -12 dB, il se sent plus qu'il ne s'entend.
- Fin : fondu de sortie de 45 images, ou une fin de phrase musicale.
- Total : `normaliser_loudness` (mixage, -14 LUFS, crête -1 dBTP), puis `mesurer_audio`. À l'export, donne toujours `niveau_lufs`.

## Où poser quoi

Musique sur « Musique », effets en alternance sur « Bruitages et SFX 1 » et « 2 » (deux effets qui se chevauchent ne tiennent pas sur une piste ; `ajouter_clip` avec `mode: "piste_libre"` envoie le second sur une piste audio libre et te dit laquelle), voix sur « Voix off ». Un effet par événement visible : entrée d'un titre (souffle court), apparition d'une étiquette (pop), changement de lieu (souffle qui monte), logo (carillon doux).

## Critères de qualité

- On peut nommer les moments en écoutant sans l'image.
- Aucun effet ne couvre un mot.
- Mesure finale à -14 LUFS, plage dynamique de 4 à 8 LU.
- Tu dis à la personne que le choix est fait sur mesure et qu'elle doit écouter.

## Un instrumental par moment (8 octobre 2026)

« Plusieurs musiques mixées » veut dire **des morceaux différents**, pas trois clips du même morceau. La musique change quand ce qui se passe à l'image change : la question, la démonstration, la phrase forte, la sortie. Trois moments, trois instrumentaux.

- Choisis des morceaux de tempos voisins (5 % d'écart au plus) et **cale-les tous sur le tempo du montage** : `atempo` de ffmpeg sur une copie du fichier, rangée dans le dossier Musique avec « calé à N BPM » dans son nom. Les coupes restent sur les mesures.
- Fais partir chaque morceau **de son drop**, sur la première image du nouveau moment, avec un impact par-dessus pour souder le changement.
- Mesure le niveau de chaque morceau (LUFS sur sa partie utilisée) et règle le gain de chaque clip pour qu'ils sortent au même niveau sous la voix.
- Un clip par morceau et par moment sur la piste Musique : la personne peut en changer un sans toucher aux autres.

## Erreurs à éviter

- Une musique unique à niveau constant.
- Un effet sur chaque coupe.
- Des sons brillants en rafale (scintillements, clics).
- Deux musiques fondues l'une dans l'autre sans silence.

## Outils

`ajouter_clip` (`partie: "audio"`, `entree`, `sortie`), `regler_gain_clip`, `ajouter_fondu` (`cote`), `appliquer_ducking`, `normaliser_loudness`, `mesurer_audio`, `script_ecrire`, `exporter` (`niveau_lufs`).
