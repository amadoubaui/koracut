---
name: koracut-etalonnage-look-cinema
description: "À lire quand la personne veut un étalonnage « cinéma », « comme un film », riche et dense (inspiration House of the Dragon) sur des rushs Rec.709 tournés en intérieur, surtout avec des peaux foncées : l'arbre de séquence, l'arbre par plan, les valeurs et les bornes."
---

# Le look cinéma en nœuds, peaux foncées comprises

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-look-cinema.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand la personne veut un étalonnage « cinéma », « comme un film », riche et dense (inspiration House of the Dragon) sur des rushs Rec.709 tournés en intérieur, surtout avec des peaux foncées : l'arbre de séquence, l'arbre par plan, les valeurs et les bornes.

**Attention (1er octobre 2026)** : ce look baisse le décor de 20 à 40 %. Amadou Ba l'a refusé pour des publicités : il veut une image claire et vive. Par défaut, prends `koracut-etalonnage-hdr-clair` ; ce look-ci ne se pose que si la personne demande une image sombre et dense.

## L'idée

Le look de cette série, c'est une courbe douce qui garde le détail des ombres, des couleurs pleines mais jamais criardes, le chaud des sources contre le froid du reste, et des visages qui sortent d'un décor plus sombre. On l'obtient par la densité (le décor baisse), pas en éclaircissant les gens. Une peau foncée ne s'éclaircit jamais : on lui garde sa luma et on fait vivre ses reflets.

Recette vérifiée le 1er octobre 2026 sur neuf images de quatre décors (chambre claire, bureau gris, mur corail, mur à plumes). Les graphes complets sont dans `Recherche/look-cinema-graphes.json`.

## Étape 1 : mesurer, puis corriger chaque plan

`mesurer_image` sur chaque plan, puis `voir_image` (avec l'accord de la personne, sur un plan par cadre) pour repérer le visage (centre x, y et demi-hauteur t, en part de l'image). Chaque plan reçoit le même arbre de six nœuds, en série :

1. **Balance** : `temp` −350 à 0. Un blanc (blouse, oreiller) garde 5 à 10 points de rouge de plus que de bleu : crème, jamais bleu. Temp −100 retire environ 5 points.
2. **Expo** : rien si la peau est entre 55 et 95 de luma. `gain` [0,92 à 1] si des blancs dépassent 240. `gamma` [0 à +0,03] au plus.
3. **Peau** : qualificateur et fenêtre ensemble.
   - `qualificateur` : teinte `centre` 21, `largeur` 6, `doux` 4 ; sat `bas` 15, `doux_bas` 8 ; lum `bas` 8, `haut` 60, `doux_bas` 6, `doux_haut` 12 ; `flou` 8.
   - `fenetres` : cercle sur le visage, `taille` 1,15 t, `doux` 0,5 t, `aspect` 0,75 à 0,9.
   - `primaires` : `teinte` 49,5 (48 à 50), `contraste` 1,08, `pivot` 0,32.
4. **Visage** : fenêtre cercle `taille` 1,3 t, `doux` 1,6 t ; `gamma` [+0,02 à +0,06], `gain` [1 à 1,05].
5. **Fond** : `etal_noeud_ajouter` type `outside` sur le nœud Visage ; `gamma` [−0,06 à −0,14], `saturation` 43 (39 sur un décor criard).
6. **Vignette** : type `fin` ; fenêtre cercle x 0,5, y 0,5, `taille` 0,6, `aspect` 1,6, `doux` 0,55, `inverser` true ; `gain` [0,80 à 0,90].

Tableaux `[Y, R, V, B]` : `gamma` [0.02, 0, 0, 0] veut dire Y seul.

## Les valeurs de départ par type de plan

| Type | Expo | Visage | Fond | Remarque |
|---|---|---|---|---|
| Visage en gros plan | rien | gamma +0,02, gain 1,04 | gamma −0,10, sat 43 | t vers 0,20 |
| Plan large clair | gain 0,92 | gamma +0,05, gain 1,04 | gamma −0,10, sat 44 | fenêtre sur le buste, t 0,12 à 0,16 |
| Gros plan de téléphone | gamma +0,03 | gain 1,04, fenêtre taille 0,42 aspect 1,3 | gamma −0,10 | pas de qualificateur de peau ; nœud « Écran » |
| Produit | gain 1,06 | gain 1,04, fenêtre en hauteur (aspect 0,4) | gamma −0,06, sat 45 | pas de qualificateur de peau |

Deux nœuds en plus, en série après Peau, quand le plan le demande :
- **Reflet rouge** (un mur rouge ou corail déteint sur la joue, peau à plus de 8° de sa ligne) : même fenêtre que Peau, qualificateur teinte `centre` 16,5, `largeur` 4, `doux` 3, sat `bas` 50, `doux_bas` 15, `flou` 10 ; `saturation` 36, `teinte` 46,5.
- **Écran** (le blanc d'un téléphone sort bleu) : qualificateur teinte `centre` 76, `largeur` 10, `doux` 5, sat `bas` 20, lum `bas` 30 ; `saturation` 24. Le vert d'une marque n'est pas dans cette clé.

## Étape 2 : le look, dans l'arbre de séquence

Une seule fois, avec `sequence: true`, trois nœuds en série :

1. **Ombres froides** : `primaires.gamma` [0, −0,015, 0, +0,015], avec le qualificateur teinte `centre` 22, `largeur` 12, `doux` 6, sat `bas` 12, `doux_bas` 8, `flou` 0, `inverser` true. Tout refroidit un peu, sauf la peau et le bois.
2. **Hautes chaudes** : `gain` [1, 1,03, 1, 0,94], `saturation` 45.
3. **Courbe film** : la même courbe dans `courbes.r`, `v` et `b` : [[0, 0.03], [0.08, 0.072], [0.22, 0.197], [0.45, 0.46], [0.7, 0.765], [0.88, 0.905], [1, 0.94]]. Noirs à 8, blancs retenus à 240, un S doux entre les deux.

Aucun flou ni fenêtre dans cet arbre : il tient dans une LUT, la lecture reste légère.

## Les bornes

- Peau : luma médiane 55 à 95, jamais plus de 12 points au-dessus de la source (20 pour un visage sous 70 dans un plan large) ; reflets sous 180 ; teinte entre 116 et 121° (`peau.teinte_degres`), la même à 3° près sur tous les plans ; saturation 0,45 à 0,65.
- Image : noirs 8 à 14, blancs sous 240, aucun pixel écrasé. La luma moyenne baisse de 25 % au plus et ne passe pas sous 45.
- Plan déjà sombre (moyenne sous 60) : pas de nœud Fond, vignette à 0,90.
- Le froid des ombres : 0,02 au plus par canal. Au-delà, les murs gris deviennent bleus.
- Les couleurs d'une marque (logo, site à l'écran) se mesurent avant et après : 5 points d'écart au plus.

## Critères de qualité

- Le visage est ce qu'on voit en premier, sans halo autour de la tête.
- La peau garde sa carnation et ses reflets : ni grise, ni rouge, ni éclaircie.
- Les cheveux et les vêtements noirs restent noirs, pas bleus.
- On ne sent pas le look changer d'un décor à l'autre.

## Erreurs à éviter

- Monter le Gamma de tout le plan pour « voir la peau » : +0,05 éclaircit une peau foncée de 10 points. C'est le décor qui baisse.
- Refroidir les ombres au Lift : les cheveux bleuissent. Au Gamma, et hors des teintes de peau.
- Le qualificateur de peau sans fenêtre : le bois, le cannage, un mur corail ont la même teinte.
- Laisser `lum.haut` à 100 : le qualificateur prend les blancs cassés (oreiller, blouse).
- `teinte` au-dessus de 50 sur une peau foncée : elle rougit.
- Poser le look sur chaque plan au lieu de la séquence.
- Promettre le grain et la diffusion de la série : l'étalonnage de KoraCut ne les a pas encore (fiche `koracut-quand-un-outil-manque`).

## Outils

`mesurer_image`, `voir_image`, `etal_lire`, `etal_noeud_ajouter` (`serie`, `outside`, `fin`), `etal_noeud_regler` (`primaires`, `qualificateur`, `fenetres`, `courbes`), `etal_copier` (même décor, même cadre), `activer_etalonnage`. Fiches liées : `koracut-etalonnage-fenetres-visage`, `koracut-etalonnage-raccord-peau`, `koracut-etalonnage-methode-noeuds`.
