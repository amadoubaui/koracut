---
name: koracut-etalonnage-signature
description: "À lire avant d'étalonner n'importe quel montage. C'est le rendu que KoraCut pose **par défaut**, sur chaque vidéo, sans qu'on le demande. On ne s'en écarte que si la personne demande autre chose en toutes lettres (une image sombre, un look précis, une LUT de marque)."
---

# L'étalonnage par défaut de KoraCut : HDR, vif, peau, sujet

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-signature.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant d'étalonner n'importe quel montage. C'est le rendu que KoraCut pose **par défaut**, sur chaque vidéo, sans qu'on le demande. On ne s'en écarte que si la personne demande autre chose en toutes lettres (une image sombre, un look précis, une LUT de marque).

Règle fixée par Amadou Ba le 8 octobre 2026, après avoir fait le même retour sur plusieurs montages de suite. Elle remplace les essais précédents (`koracut-etalonnage-hdr-clair` garde le détail des mesures et des refus).

## L'idée, en une image

Un photographe d'architecture prend trois photos du même appartement : une sombre, une normale, une trop claire. Il les fusionne. Dans le résultat, les coins sombres se sont éclaircis, la fenêtre trop claire s'est assombrie, tout se lit, les couleurs sont nettes. C'est le HDR. On fait la même chose à chaque plan, puis on ajoute deux gestes : raviver les couleurs, et soigner ce que l'œil regarde (la peau, le visage, le produit) avec un masque, comme un coloriste le fait dans DaVinci Resolve.

## L'arbre d'un plan : quatre nœuds, toujours les mêmes étiquettes

| Ordre | Nœud | Ce qu'il fait | Réglages |
|---|---|---|---|
| 0 | `CST` | seulement si le plan est en log ou en HDR de téléphone | `etal_cst` (voir `koracut-etalonnage-log-commun`, `koracut-etalonnage-telephones`) |
| 1 | `HDR` | **fort** : les zones sombres remontent beaucoup, les zones claires redescendent beaucoup | `ombres` +85, `hautes_lumieres` -90, `mid_detail` +6, `contraste` 1,0, `gamma` Y réglé à la mesure (voir plus bas) |
| 2 | `Vif` | les couleurs ressortent, sans fluo | `saturation` 54, `boost` 6 (neutre : 50 et 0) |
| 3 | `Peau` | le teint est adouci, là seulement | qualificateur de peau (teinte centre 24, largeur 16, doux 8 ; saturation 8 à 80 ; luminance 6 à 88 ; flou 14) ; `mid_detail` -28, `gamma` Y +0,008 ; une fenêtre cercle sur le visage quand il ne bouge pas trop (aspect 0,8, doux 0,8 fois la taille) |
| 4 | `Sujet` | l'œil va où il faut | une fenêtre cercle douce. Sur un visage : `gamma` Y +0,04, `gain` Y 1,03. Sur un objet ou un écran qu'on montre : `contraste` 1,16, `saturation` 60 et netteté (`flou` rayon 0,45), **sans gain** (un gain dans une fenêtre fait un halo et grise le reste) |

L'outil `etal_signature` pose tout l'arbre et fait les mesures en un appel : `etal_signature` { `clip_id`, `visage`: { x, y, taille } } pour un plan, ou { `tous`: true } pour toute la séquence (peau au qualificateur seul, sans fenêtre). Sur un écran ou un objet qu'on montre : `sujet` { x, y, taille } à la place de `visage`. Dans une KoraCut plus ancienne, qui n'a pas l'outil, pose les nœuds un par un avec `etal_noeud_ajouter` et `etal_noeud_regler`.

## Le gamma du nœud HDR se règle à la mesure, pas à l'œil

Après `ombres` et `hautes_lumieres`, mesure le plan (`mesurer_image`) et bouge le `gamma` Y du nœud HDR jusqu'à la cible, par moitiés successives entre -0,45 et +0,25 (six ou sept mesures suffisent) :

| Ce que montre le plan | Luma moyenne visée (0 à 255) |
|---|---|
| une personne, un intérieur | 114 à 118 |
| des vêtements ou des produits devant un mur clair | 100 à 108 |
| une table blanche, un écran, un fond très clair qui remplit l'image | 134 à 138 |

Clair ne veut pas dire surexposé, vif ne veut pas dire fluo. La première version de cette fiche (ombres +40, saturation 66, moyenne 126 avec une personne et 160 sur fond blanc) a été refusée le jour même : « un peu trop vif et beaucoup trop clair ». La deuxième (ombres +18, hautes lumières -55, contraste 1,08, moyenne 110) est revenue elle aussi : « des parties trop exposées et d'autres sous-exposées, des zones trop sombres ». La moyenne était bonne, la répartition ne l'était pas : le fond derrière la personne était à 38, son visage à 100, la table à 175. **Le HDR se juge zone par zone, pas à la moyenne** : c'est la compression forte (ombres +85, hautes lumières -90) qui a donné fond 64, visage 122, table 150. Les blancs restent sous 230, un mur ou une table blanche garde sa matière, les noirs descendent sous 15.

## Ce qu'on ne fait jamais

- **Assombrir le décor pour faire ressortir le sujet.** Pas de nœud `outside` qui baisse le fond, pas de vignette. Le nœud `Sujet` éclaire, c'est tout.
- **Pousser le détail sur la peau.** `mid_detail` est positif dans le nœud HDR (toute l'image), négatif dans le nœud Peau (la peau seule).
- **Un look par-dessus un plan qui n'a pas cet arbre.** Une LUT ou une ambiance se pose après, dans un nœud `Look`, et seulement si on la demande.
- **Changer de recette d'un plan à l'autre.** Les quatre nœuds partout ; seuls le `gamma` (à la mesure) et les fenêtres changent.

## Les fenêtres

Coordonnées de 0 à 1 dans l'image source, origine en haut à gauche. Un visage : centre sur le nez, taille 0,30 à 0,36 pour un plan taille, plus petit pour un plan large. Un téléphone ou un produit tenu : centre sur l'objet, taille 0,30, aspect 1,5 s'il est couché. Si le sujet traverse l'image, pas de fenêtre : le qualificateur de peau suffit, et on se passe du nœud `Sujet`.

## Regarde ton travail avant de le rendre (obligatoire)

Les chiffres ne suffisent pas : une image peut être dans la cible et laide. Mesure d'abord **trois zones** de l'image exportée (la plus sombre qui compte, le sujet, la plus claire), avec `mesurer_image` et son paramètre `zone` (une partie du cadre en fractions de 0 à 1, par exemple `{ x: 0.3, y: 0.1, largeur: 0.4, hauteur: 0.5 }` pour un sujet au centre haut ; sans zone, `p10` et `p90` disent déjà où sont les ombres et les hautes lumières) : l'écart entre la zone sombre et la zone claire doit s'être resserré par rapport à la source. Avant de dire que l'étalonnage est fait, **exporte quatre ou cinq images** (un plan de chaque sorte : visage, plan large, gros plan d'objet, plan le plus clair, plan le plus sombre), pose-les côte à côte et regarde-les comme un spectateur. Corrige tant qu'une de ces réponses est oui :

| Question | Si oui |
|---|---|
| Une zone est-elle bouchée (un fond, un visage, des mains à moins de 55 de luma) pendant qu'une autre est presque blanche (plus de 165) ? | le HDR n'est pas assez fort : monte `ombres` vers +85, descends `hautes_lumieres` vers -90, et remesure les deux zones |
| Y a-t-il un halo ou un effet de projecteur autour du sujet ? | retire le gain de la fenêtre du nœud Sujet |
| Une couleur crie-t-elle (un néon, un vêtement, une lumière verte ou bleue qui bave) ? | baisse `saturation` et `boost` du nœud Vif |
| Un blanc est-il plat, sans matière (table, mur, chemise) ? | baisse le `gamma` du nœud HDR, pousse `hautes_lumieres` vers -65 |
| L'image a-t-elle un voile gris, des noirs délavés ? | baisse `ombres`, descends le `lift`, monte un peu le `contraste` |
| La peau est-elle en cire, ou brille-t-elle ? | remonte `mid_detail` du nœud Peau vers -20, retire le `gamma` du nœud |
| Un plan saute-t-il aux yeux à côté des autres (plus clair, plus coloré) ? | ramène-le à la moyenne de ses voisins |
| Le sujet se détache-t-il moins qu'avant l'étalonnage ? | reprends la fenêtre du nœud Sujet |

Dans le doute entre deux réglages, prends le plus sobre. La personne ne doit pas avoir à dire « trop clair » ou « trop vif » : c'est à toi de le voir.

## Vérifier

1. `mesurer_image` sur chaque plan : moyenne dans la cible, `blancs_brules_pourcent` à 0, saturation en hausse par rapport à la source.
2. Regarder un export, pas le moniteur : le teint est doux sans être en cire, les vêtements ont leur matière, rien n'est gris.
3. Deux plans qui se suivent dans la même scène ont la même moyenne à 3 points près.

## Outils

`etal_signature`, `etal_cst`, `etal_noeud_ajouter`, `etal_noeud_regler`, `etal_lire`, `mesurer_image`, `etal_copier`.
