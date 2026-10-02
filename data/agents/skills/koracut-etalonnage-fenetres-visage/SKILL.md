---
name: koracut-etalonnage-fenetres-visage
description: "À lire quand il faut guider le regard vers un visage ou un produit : fenêtre (power window), fond baissé par l'outside, vignette, lumière refaite en douceur, et que faire quand le visage bouge."
---

# Fenêtres sur les visages, vignette et rééclairage

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-fenetres-visage.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand il faut guider le regard vers un visage ou un produit : fenêtre (power window), fond baissé par l'outside, vignette, lumière refaite en douceur, et que faire quand le visage bouge.

## Le principe

Une fenêtre réussie ne se voit pas. Elle prolonge une lumière qui existe déjà (une baie, une lampe) : on monte un peu ce qu'elle éclaire, on baisse un peu le reste. Si on devine un rond autour de la tête, c'est raté.

## Poser la fenêtre du visage

1. `voir_image` (avec l'accord de la personne, une fois par plan de référence) : repère le centre du visage (x, y entre 0 et 1, origine en haut à gauche) et sa demi-hauteur t, en part de la hauteur d'image. Un visage en gros plan : t vers 0,20. En plan large : 0,06 à 0,10, et on prend plutôt le buste (0,12 à 0,16).
2. Nœud « Visage » en série : `fenetres` [{ type `cercle`, x, y, `taille` 1,3 t, `aspect` 0,75 à 0,9, `doux` 1,6 t }]. Le bord est plus large que la fenêtre : c'est voulu.
3. Dedans : `gamma` [+0,02 à +0,06, 0, 0, 0], `gain` [1 à 1,05, 1, 1, 1]. Le Gain fait briller les reflets, le Gamma ouvre les traits. Sur une peau foncée, préfère le Gain.
4. `mesurer_image` : la peau ne monte pas de plus de 12 points.

`taille` est une demi-hauteur. `aspect` au-dessus de 1 élargit, en dessous resserre. `rotation` en degrés pour une tête penchée.

## Baisser le fond : l'outside

`etal_noeud_ajouter` { type `outside`, `noeud_id` : le nœud Visage }. Le nouveau nœud reçoit la clé du visage inversée : il corrige tout sauf lui. Dedans : `gamma` [−0,06 à −0,14], `saturation` 39 à 46. Si tu règles sa section `cle`, redonne toujours `entree_inverser` true.

L'écart entre le visage et le fond fait le relief : 0,10 à 0,18 de Gamma au total. Au-delà, le visage a l'air découpé.

## La vignette

Un nœud à la fin de l'arbre du plan : fenêtre cercle x 0,5, y 0,5, `taille` 0,6, `aspect` 1,6, `doux` 0,55, `inverser` true ; `gain` [0,80 à 0,90, 1, 1, 1]. Sujet décentré : déplace le centre vers lui (x 0,4 à 0,6). Jamais sous 0,80, jamais un `doux` sous 0,4.

## Rééclairer

- **Une lumière qui vient d'un côté** : une fenêtre `degrade` tournée vers la source, Gain 1,03 à 1,06 du côté clair. Ou un cercle très large (taille 0,5, doux 0,6) posé du côté de la baie.
- **La peau seule dans le visage** : ajoute le `qualificateur` de peau dans le nœud qui a la fenêtre (les deux clés se multiplient). Indispensable quand le décor a la teinte de la peau : bois, cannage, mur corail.
- **Un produit** : fenêtre en hauteur (`aspect` 0,4 à 0,6), Gain 1,04 à 1,06, pas de Gamma (l'étiquette doit rester blanche).
- **Deux visages** : deux fenêtres dans la même liste `fenetres`, elles s'additionnent.
- **Retirer une zone** d'une fenêtre (un téléphone devant la joue) : une seconde fenêtre avec `soustraire` true.

## Quand le visage bouge

Ce que KoraCut sait faire aujourd'hui : une fenêtre est fixe pendant tout le plan. Pas de suivi, pas d'image clé sur les fenêtres ni sur les nœuds. Donc, dans cet ordre :

1. **Demande ou regarde** : la personne sait si le sujet bouge. Sinon, `voir_image` au milieu du plan, et une seconde fois à la fin si le plan dure plus de 4 secondes. `mesurer_image` ne donne pas la place du visage. Si le visage bouge de moins de la moitié de t, garde la fenêtre, elle est assez douce.
2. **Élargis** : une fenêtre qui couvre tout le trajet du visage, `doux` 2 t, avec un réglage plus faible (Gamma +0,02).
3. **Passe au qualificateur de peau** sans fenêtre, si le décor n'a pas la teinte de la peau : la clé suit la peau toute seule.
4. **Fais couper le plan** en deux ou trois morceaux au milieu du mouvement (c'est le Monteur qui coupe, tu n'as pas `couper_clip`), puis place la fenêtre de chaque morceau. Seulement si le mouvement est lent : à la coupe, la fenêtre saute.
5. **Sinon, renonce à la fenêtre** sur ce plan, garde le look et la vignette, et dis-le à la personne (fiche `koracut-quand-un-outil-manque`) : il manque le suivi des fenêtres.

## Critères de qualité

- Coupe le nœud (`actif` false) puis remets-le : la différence se sent, la forme ne se voit pas.
- Aucun halo sombre ou clair autour de la tête, ni sur un mur uni derrière elle.
- Le visage reste dans la fenêtre du début à la fin du plan.
- Sur un plan large, la fenêtre ne se voit pas sur le mur : sinon élargis le `doux`.

## Erreurs à éviter

- Un `doux` de 0,05 sur un visage : on voit le rond. Jamais sous 0,5 t.
- Éclaircir le visage au lieu de baisser le fond : la peau perd sa carnation.
- Une fenêtre dans l'arbre de séquence : elle se poserait au même endroit sur tous les plans.
- Copier l'arbre d'un plan sur un autre cadre (`etal_copier`) sans replacer la fenêtre.
- Baisser le fond d'un plan déjà sombre (moyenne sous 60).

## Outils

`voir_image`, `mesurer_image`, `etal_lire`, `etal_noeud_ajouter` (`serie`, `outside`, `fin`), `etal_noeud_regler` (`fenetres`, `primaires`, `qualificateur`, `cle`). Fiches liées : `koracut-etalonnage-look-cinema`, `koracut-etalonnage-contre-jour`.
