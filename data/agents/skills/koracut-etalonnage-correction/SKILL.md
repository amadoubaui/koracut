---
name: koracut-etalonnage-correction
description: "À lire avant de toucher à la couleur d'un plan : la correction (log, balance, exposition, contraste) vient toujours avant le look, et chaque réglage se mesure."
---

# L'étalonnage, étape 1 : corriger chaque plan

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-correction.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de toucher à la couleur d'un plan : la correction (log, balance, exposition, contraste) vient toujours avant le look, et chaque réglage se mesure.

## L'ordre, toujours le même

Un nœud par étape, étiqueté (`koracut-etalonnage-methode-noeuds`).

1. **Log vers Rec.709** : un plan gris et plat (noirs vers 30 à 40, blancs vers 180 dans `mesurer_image`) est tourné en log. Nœud « CST » en tête : `etal_cst` avec la caméra (`koracut-etalonnage-log-commun` et la fiche de la marque). Si tu ne sais pas quel log, demande.
2. **Balance des blancs**, nœud « Balance » : sur ce qui doit être neutre (mur blanc, chemise, bitume). `primaires.temp` (positif réchauffe, pas de 200 à 500), `primaires.tint` (positif vers le magenta), ou `offset` [R, V, B] pour une dominante d'ensemble. Objectif : les trois canaux à moins de 3 points d'écart dans `dominante`.
3. **Exposition et contraste**, nœud « Expo » : Gain, Gamma, Lift, `contraste` autour du `pivot`. Plan trop sombre ou trop clair : `koracut-etalonnage-image-sombre`, `koracut-etalonnage-image-claire`. Noirs entre 8 et 20, blancs sous 240, moyenne d'un plan de jour entre 95 et 130. Moins de 1 % de pixels écrasés ou brûlés.
4. **Roues** pour une dominante dans une seule zone (ombres bleues, hautes lumières jaunes) : les canaux R, V, B de `lift`, `gamma`, `gain`, ou les roues `log`, par petits pas (0,01 à 0,03 sur le Lift, 0,02 à 0,05 ailleurs).
5. **Courbes** (`courbes`) pour un contraste doux en S, `courbes_teinte` pour une couleur précise.

## La boucle

Mesurer (`mesurer_image`), un réglage, mesurer. Trois petits pas valent mieux qu'un grand qu'il faut défaire. Regarder (`voir_image`) une fois à la fin de la scène, pas à chaque pas.

## Commencer par quel plan

Le plan de référence de chaque scène : souvent le plan large, ou celui où la peau se voit le mieux. On le corrige à fond, puis on raccorde les autres (`koracut-etalonnage-raccord-peau`).

## Critères de qualité

- Les mesures dans les repères, sans noirs bouchés ni blancs brûlés.
- Le blanc est blanc, sauf ambiance voulue (coucher de soleil, bougie : on garde une trace de chaleur).
- Un seul effet de couleur par plan : l'arbre de nœuds entier est calculé en une fois, il ne coûte pas plus cher avec cinq nœuds qu'avec un. Seuls les fenêtres, le qualificateur, le flou et le contraste local (Mid/Détail, Basses et Hautes lumières) coûtent : à doser sur une machine modeste.

## Erreurs à éviter

- Poser une LUT créative sur un plan pas corrigé.
- Régler la balance en regardant un mur coloré.
- Éclaircir une peau foncée pour la « ramener à la norme » : elle devient grise.
- Étalonner avant que le montage soit fixé.

## Outils

`mesurer_image`, `etal_lire`, `etal_cst`, `etal_noeud_ajouter`, `etal_noeud_regler`, `activer_etalonnage` (montrer l'avant), `voir_image`, `rendre_apercu`. Les anciens outils (`balance_blancs`, `regler_exposition`, `lift_gamma_gain`, `regler_courbes`, `appliquer_lut`) restent pour un geste isolé.
