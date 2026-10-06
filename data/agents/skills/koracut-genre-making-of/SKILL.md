---
name: koracut-genre-making-of
description: "À lire avant de monter les coulisses d'un tournage à partir de rushs de téléphone. Fiche du Chef et du Monteur, lue à la demande par `lire_competence`. Écrite le 2 octobre 2026, après le making-of « Coup de sifflet » (121 rushs, 78 minutes, 60 secondes montées)."
---

# Monter un making-of

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/genre-making-of.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de monter les coulisses d'un tournage à partir de rushs de téléphone. Fiche du Chef et du Monteur, lue à la demande par `lire_competence`. Écrite le 2 octobre 2026, après le making-of « Coup de sifflet » (121 rushs, 78 minutes, 60 secondes montées).

## La méthode

1. **Voir tout avant de couper.** Une bande de cinq images par rush, rangée par date de tournage. On repère les lieux, l'équipe au travail (caméra, perche, lumière, maquillage), les moments de vie.
2. **Découper en chapitres par lieu**, dans l'ordre du tournage, et finir par la post-production s'il y en a des images. Cinq à sept chapitres pour une minute.
3. **Choisir des plans où l'on voit le métier** : la caméra qui filme, la perche, le clap, le réflecteur, un sourire. Écarter les dos au premier plan, les plans flous, les mains devant l'objectif. Contrôler sur une planche d'une image par plan, remplacer les faibles.
4. **Poser sur les temps de la musique** : deux ou trois temps par plan, le premier chapitre sur le premier temps fort, le titre sur l'introduction.
5. **Habiller avec la marque du projet** : le logo au titre et à la fin, une étiquette numérotée par chapitre dans les couleurs du logo. Rien sur un visage.
6. **Étalonner au même niveau** tous les plans (fiche `koracut-etalonnage-hdr-clair`, avertissement du 2 octobre : équilibré, jamais surexposé).

## Deux formats

Les rushs de téléphone sont verticaux. La version horizontale se fait dans un second projet : le plan au centre (hauteur pleine) sur la piste du dessus, et dessous le même plan agrandi à toute la largeur avec un flou (`ajouter_effet`, `avfilter.gblur`, sigma 38). Le même étalonnage sur les deux (`etal_copier`). Un rush horizontal dans le projet vertical se recadre par `transformer_clip`, centré sur les personnes.

## Ce qu'il ne faut pas inventer

Les noms de lieux, les noms des personnes, une date de sortie. Les chapitres portent ce qu'on voit (« Sur le terrain », « À la plage ») et la personne corrige.

## Ce que le premier retour a appris (2 octobre 2026)

La première version (plans de deux secondes, musique douce, étiquette « numéro + texte ») a été jugée trop sage. Ce qui a été demandé, et qui devient la base :

- **Rythmé** : un morceau qui pousse (trap, 139 bpm), des plans de deux temps (moins d'une seconde), quatre temps pour le plan qui ouvre un chapitre, un coup de zoom sur le temps (`poser_keyframe` sur `rect` : 116 % à l'image 0, 103 % à l'image 7, 100 % à la fin, `exponentielle_sortie`), en alternance avec une lente avancée.
- **Des cases, pas des étiquettes** : une case arrondie avec une icône qui bouge (ballon qui tourne, clap qui claque, ciseaux qui coupent) et qui entre en pivotant (`rotationY`, perspective). Pas de numéro seul.
- **Derrière les personnes** : `detourer_sujet` sur le plan qui ouvre le chapitre (plan à 100 % de vitesse, étalonné et zoomé AVANT : le sujet recopie l'étalonnage et le Transformer), la case sur la piste Animations. Sujet `premier_plan` pour une statue ou un objet.
- **Lisible d'abord** : après le premier rendu, regarder chaque case sur une image. Si la personne cache plus qu'un bord, déplacer la case (au-dessus des têtes, ou de l'autre côté). Si aucune place ne marche, retirer le sujet détouré de ce plan et poser la case devant, sur un dos ou un décor, jamais sur un visage.
- **Le titre est celui que donne la personne** : ne pas déduire le titre d'un logo trouvé dans le dossier.
- **Étalonnage façon HDR** : ombres +40, hautes lumières -42, détail +14, saturation 66, luminosité moyenne visée 128, sans nœud de peau. Les plans d'une pièce sombre se visent plus bas (110) pour ne rien brûler.

## Deuxième retour (2 octobre 2026 au soir)

- **Les cases se posent en hauteur**, jamais vers les pieds des personnes : Amadou Ba les a remontées lui-même. Viser le tiers haut ou le milieu de l'image.
- **La musique ne doit pas piquer** : le premier morceau trap avait 11 % de son énergie au-dessus de 4 kHz, il a été jugé bruyant. Le remplaçant en a 1,6 %. Avant de proposer un morceau, mesurer cette part et rester sous 3 %.
- **Changer la musique sans toucher au montage** : étirer le nouveau morceau au tempo des coupes (`atempo`), puis mesurer sur l'export l'écart entre les coups de grosse caisse et les coupes, et décaler l'entrée du clip de musique jusqu'à zéro. Seul le clip de musique bouge.

## Troisième retour : « trap » veut dire trap (2 octobre 2026 au soir)

Le morceau hip-hop doux posé en remplacement n'était pas ce qui était demandé : il voulait un instrumental trap (basse 808, charleston en roulements), rythmé, et un enchaînement de deux morceaux. La banque du studio n'en avait qu'un. Ce qui a marché :

- **Chercher la matière** : la page « trap » de Mixkit (licence libre, sans attribution), 36 morceaux téléchargés et mesurés. Garder ceux dont le tempo est proche de celui des coupes (130 à 150 bpm pour des coupes à 139) et dont moins de 3 % de l'énergie est au-dessus de 4 kHz.
- **Enchaîner deux morceaux** : le premier part de son introduction calme sous le titre et « tombe » sur le premier chapitre ; le second prend le relais à un début de chapitre, lui aussi sur son départ franc. La bascule se fait sur une coupe, sous un effet de passage.
- **Caler chacun à part** : mesurer sur l'export l'écart entre les coups de grosse caisse et les coupes, morceau par morceau, et décaler l'entrée de chaque clip jusqu'à moins d'une image.
- Quand la personne nomme un genre, lui donner ce genre. Adoucir se fait en choisissant dans le genre, pas en changeant de genre.
