---
name: koracut-bruitages-ambiances
description: "À lire avant de poser un bruitage (whoosh, pop, impact, riser) ou une ambiance : où, à quel niveau, combien, pour que le son soutienne l'image sans se faire remarquer."
---

# Les bruitages et les ambiances

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/bruitages-ambiances.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de poser un bruitage (whoosh, pop, impact, riser) ou une ambiance : où, à quel niveau, combien, pour que le son soutienne l'image sans se faire remarquer.

## Où

- **Whoosh** sur un mouvement rapide, un texte qui entre, une transition dessinée : le cœur du whoosh sur l'image où le mouvement est le plus vif.
- **Pop, clic** sur l'apparition d'un élément (bandeau, chiffre, icône) : calé à l'image près sur son arrivée.
- **Riser** (son qui monte) avant une révélation, fini exactement sur la coupe ou l'impact.
- **Impact** sur un moment fort, un logo final.
- **Ambiance** : sous chaque scène, le son du lieu (marché, mer, atelier, bureau), continu d'un plan à l'autre de la même scène, en fondu d'entrée et de sortie de 12 à 25 images. Elle rend les coupes invisibles.

## Combien et à quel niveau

- Discret : -18 à -24 dB, 15 à 20 dB sous la voix pour une ambiance.
- Pas un bruitage à chaque coupe : sur les éléments d'habillage et les moments forts seulement. Un Reel de 30 s en porte rarement plus de 5 ou 6.
- Le même son pour le même geste dans toute la vidéo (tous les bandeaux ont le même pop).

## Les étapes

1. `etat_timeline` : repère les éléments d'habillage, les transitions, les coupes de scène.
2. `lister_medias` avec la recherche `whoosh`, `pop`, `ambiance` : seulement des sons présents dans le chutier.
3. Pose sur les pistes Ambiances et Bruitages, jamais sur Voix ou Musique (`ajouter_clip`, `partie: "audio"`). Deux sons qui se chevauchent ne tiennent pas sur une même piste : ajoute `mode: "piste_libre"`, le second part sur une autre piste audio libre (créée s'il le faut), et la réponse te dit laquelle (`piste_id`, `piste_creee`). Regarde où il est allé : si c'est une piste Voix ou Musique, déplace-le.
4. Fondus de 3 images au moins sur chaque bruitage coupé, 12 à 25 sur les ambiances.
5. Le Mixeur règle l'équilibre final.

## Percutant, pas doux (8 octobre 2026)

Par défaut, le sound design d'un montage rythmé (publicité, teaser, making-of, clip) est **franc** : des coups graves, des claps, des coups secs, calés à l'image près sur les coupes et sur les entrées de texte. Un effet qu'on n'entend pas ne sert à rien. Ce qui reste interdit : les glissements de hauteur (« tiou »), les sifflements, les balayages stridents. Percutant veut dire du grave et de l'attaque, pas de l'aigu.

- Cale la **crête** de l'effet sur l'image de la coupe, pas son début : pose le clip `crête × cadence` images avant.
- Coupe la queue d'un effet à sa fin utile, pour qu'il ne bave pas sur le suivant.
- Un clip par son, sur des pistes à part (« Effets », « Impacts ») : la personne déplace chaque coup.
- Mesure à la fin : le fichier sort à -14 LUFS pour le web, crête sous -1 dB.

## Erreurs à éviter

- Des bruitages plus forts que la voix.
- Une ambiance qui s'arrête net à chaque coupe.
- Un son de banque « comique » sur un sujet sérieux.

## Outils

`etat_timeline`, `lister_medias`, `ajouter_clip`, `deplacer_clip`, `rogner_clip`, `ajouter_fondu`, `regler_gain_clip`, `ajouter_piste`.
