---
name: koracut-animer
description: "Animer dans KoraCut : quand prendre les gestes de l'application (animer_entree, animer_sortie, animer_texte, transition_app) et quand HyperFrames (motion_element_seul, motion_transition), la règle un élément = une composition = un clip, le mouvement. À lire avant tout titre animé, bandeau, logo ou transition."
---

# Animer dans KoraCut

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/animer.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

Fiche du Motion designer et du Monteur (document 19).

## Choisir l'outil

| Ce qu'il faut | Outil | Pourquoi |
|---|---|---|
| Un plan, une image, un titre qui entre ou sort | `animer_entree`, `animer_sortie` | l'effet Transformer du clip, rien à rendre |
| Un titre qui s'écrit lettre à lettre, mot à mot | `animer_texte` | la machine à écrire du titreur |
| Une transition simple | `transition_app` | fondu enchaîné, zoom, poussée, fondu au noir |
| Un titre dessiné, un bandeau, un compteur, un logo | `motion_element_seul` | HyperFrames, un élément = un clip |
| Une transition dessinée | `motion_transition` | à cheval sur la coupe, elle la suit |
| Montrer l'élément à changer | `motion_ouvrir` | comme un double-clic |

L'application d'abord (rien à rendre). HyperFrames quand il faut dessiner.

## La règle des éléments

- Un élément = une composition = un clip, sauf demande de la personne.
- Les éléments vont sur la piste « Animations », les transitions sur « Transitions », juste au-dessus. Un élément dont le clip croise celui d'une transition la suit quand la coupe bouge (`suit_transition` dans `motion_lister`) : ne le recale pas à la main.
- Rallonger un élément refait son rendu (la sortie reste à la fin).
- Chaque élément vit dans la séquence qui était devant à sa création et n'en sort pas (`motion_lister` : `sequence`).
- Une création annulée libère le nom de l'élément : le recréer sous le même nom est permis.

## Le contrat HyperFrames (pour `brief_html`)

- KoraCut pose la racine et ses attributs. Pas de `data-start` dans l'élément : il est seul.
- Dans un fragment, `tl` (déjà créée, en pause), `D` (durée), `W` et `H` existent : ne crée pas d'autre timeline.
- `gsap.fromTo` (état de départ explicite), jamais une transformation CSS sur ce que GSAP anime. Pas de `Math.random`, pas de `Date.now`, pas de `repeat: -1`. Pas de `<br>`.
- Polices du système ou fichiers de `assets/`, jamais en ligne ni en base64. Les polices de la charte et les images du client se donnent par `fichiers` (`motion_element_seul`, `motion_creer`, `motion_ecrire`) et se citent par `url("assets/Outfit.ttf")`.
- N'anime pas `letterSpacing` ni une largeur (saccades au rendu) : des transformations. Un second `fromTo` sur le même élément prend `immediateRender:false`, ou devient un `to`.
- Marque le texte principal `data-koracut-texte="1"` : la personne le change dans l'inspecteur.

## Le mouvement

- Entrée 0,4 à 0,6 s (`power3.out`), sortie 0,3 à 0,4 s (`power2.in`), rien ne bouge sur le plateau.
- Mots décalés de 0,06 à 0,08 s (`stagger`), fonction 0,15 s après le nom.
- Rebond pour les logos et les chiffres, jamais pour un texte d'interview.
- Transitions : 0,4 à 0,6 s, milieu sur la coupe, image couverte deux images au moins. Une principale, une ou deux en accent.
- Interdits : dégradé violet et cyan, néon, verre flou, ombre lourde, damier de pixels dans un coin.

## Varier, sans gâcher l'image (refus du 1er octobre 2026)

- Même famille (polices, couleurs), une animation différente par élément : mise au point, masque, tampon, compteur, surligneur, rouleau. Jamais le même gabarit répété.
- Pas de panneau plein qui coupe l'écran, pas de bordure colorée sur un seul côté, pas de bande inclinée qui balaie l'image.
- Chaque élément tient dans une zone libre (x, y, largeur, hauteur) choisie sur le plan avec `voir_image` : rien sur un visage ni sur un écran de téléphone. Image claire : texte foncé à halo clair ; sombre : texte clair à ombre douce. Pas de voile sur toute l'image.
- Regarde l'élément rendu sur le plan avant de livrer.

## Les gestes de l'application

`animer_entree`, `animer_sortie` : fondu, glisse_gauche, glisse_droite, glisse_haut, glisse_bas, monte, zoom_avant, zoom_arriere, rebond, pop. `animer_texte` : machine_a_ecrire, mot_a_mot, ligne_a_ligne. `transition_app` : fondu_enchaine, volet, glissement, fondu_noir, zoom, pousse, secousse. Un appel = une étape d'annulation.
