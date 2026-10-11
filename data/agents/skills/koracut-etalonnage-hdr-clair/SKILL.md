---
name: koracut-etalonnage-hdr-clair
description: "> Depuis le 8 octobre 2026, la recette à poser par défaut est dans `etalonnage-signature` (quatre nœuds : HDR, Vif, Peau, Sujet). Cette fiche garde l'histoire des essais et des refus, et les valeurs à ne pas dépasser."
---

# Étalonner clair, vif et équilibré (rendu « HDR »)

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-hdr-clair.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

> Depuis le 8 octobre 2026, la recette à poser par défaut est dans `koracut-etalonnage-signature` (quatre nœuds : HDR, Vif, Peau, Sujet). Cette fiche garde l'histoire des essais et des refus, et les valeurs à ne pas dépasser.

À lire quand la personne veut une image claire, colorée, attirante, où tout se lit, comme une photo HDR : ombres relevées, hautes lumières retenues, sujet mis en valeur sans jamais assombrir le reste.

C'est le goût d'Amadou Ba (1er octobre 2026) : « cinématique » veut dire pour lui clair, vif et équilibré. Un look sombre et dense (`koracut-etalonnage-look-cinema`) ne se pose que si la personne demande une image sombre.

## Avertissement du 2 octobre 2026 : clair ne veut pas dire surexposé

Amadou Ba a refusé trois vidéos étalonnées avec cette recette poussée trop loin (luminosité moyenne à 150 sur 255, ombres à +45, noirs relevés) : « trop clair, les vêtements ne ressortent pas ». Ce qu'il demande : **les couleurs et les détails ressortent, rien n'est brûlé**.

- Viser une luminosité moyenne de **105 à 128** selon le décor (105 pour une boutique aux murs blancs avec des vêtements à montrer, 125 à 128 pour un intérieur à vendre), jamais 150.
- Hautes lumières à **-58 à -78**, ombres à **+10 à +16** seulement, détail des tons moyens à **+26 à +30**, saturation 60.
- Courbe de séquence en S avec des **noirs à zéro** : ne jamais relever le pied de courbe.
- Contrôler sur l'export : un blanc (mur, chemise) doit garder sa matière.
- Dans le doute entre deux réglages, prendre le moins clair.

## Ce que veut dire « HDR » ici

Une photo HDR fusionne trois poses (sombre, moyenne, claire) : on voit dans les ombres et dans les blancs en même temps. En vidéo, on s'en approche avec quatre gestes : relever les ombres, retenir les hautes lumières, redonner du contraste local, raviver les couleurs.

## L'arbre d'un plan

1. **Balance** : `temp` -100 sur des blancs trop chauds (0 sur un gros plan d'écran).
2. **HDR** : `ombres` +50, `hautes_lumieres` -50, `mid_detail` +16, et un `gamma` Y selon la luma moyenne de la source : `(112 - moyenne) / 380`, entre 0 et 0,14. Un plan à 50 de moyenne reçoit +0,14, un plan à 110 ne reçoit rien.
3. **Couleur** : `boost` +22, `saturation` 55. Devant un mur rouge ou corail : `boost` 10, `saturation` 52.
4. **Écran** (gros plan de téléphone) : qualificateur teinte 76, largeur 10 ; `saturation` 26, le blanc du site redevient blanc.
5. **Reflet rouge** (mur corail qui déteint sur la joue) : qualificateur teinte 16,5 dans la fenêtre du visage ; `saturation` 38, `teinte` 47.
6. **Sujet** : une fenêtre cercle sur le visage ou le produit (taille 1,4 fois la demi-hauteur du visage, doux 1,8 fois) ; `gamma` +0,03, `gain` 1,03. Elle éclaire, c'est tout.

**Jamais** de nœud `outside` qui baisse le décor, **jamais** de vignette : c'est ce qui rendait les versions refusées sombres.

## L'arbre de la séquence

« Chaleur » : `gain` [1 ; 1,015 ; 1 ; 0,975]. « Courbe douce », la même dans R, V, B : `[[0,0.015],[0.25,0.265],[0.5,0.525],[0.75,0.775],[1,0.965]]`. Elle garde les noirs au-dessus de 4 et les blancs sous 246, sans écraser.

## La boucle de mesure

`mesurer_image` avec `sans_effets: true`, puis après chaque plan. À tenir sur chaque plan :
- luma moyenne **au moins égale à la source**, et au moins 100 ;
- noirs (min) entre 10 et 45, blancs (max) sous 240 ;
- saturation moyenne en hausse de 15 à 30 % ;
- teinte de peau à 3 degrés près d'un plan à l'autre.

Un plan qui sort plus sombre que sa source est faux : retire ce qui l'assombrit.

## Critères de qualité

- On voit le détail dans les cheveux noirs et dans les draps blancs du même plan.
- Aucune zone de l'image n'est plus sombre qu'au tournage.
- Les couleurs de la marque et des produits ressortent, la peau reste naturelle (pas orange).

## Erreurs à éviter

- Baisser le fond pour faire ressortir le visage.
- Monter la saturation au lieu du `boost` : la peau vire à l'orange.
- `mid_detail` au-dessus de 25 : halos et peau rugueuse.
- Le même `gamma` pour tous les plans : il se calcule sur chaque source.

## Outils

`mesurer_image`, `etal_reinitialiser`, `etal_noeud_ajouter`, `etal_noeud_regler`, `etal_lire`, `voir_image`.
