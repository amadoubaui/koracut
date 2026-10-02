---
name: koracut-habillage-titres-bandeaux
description: "À lire avant d'habiller un montage : quoi mettre à l'écran (titre, bandeau nom et fonction, carton, logo, appel à l'action), avec quel outil (l'application ou HyperFrames), combien, et comment doser les transitions."
---

# L'habillage : titres, bandeaux, cartons, transitions

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/habillage-titres-bandeaux.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant d'habiller un montage : quoi mettre à l'écran (titre, bandeau nom et fonction, carton, logo, appel à l'action), avec quel outil (l'application ou HyperFrames), combien, et comment doser les transitions.

Le choix entre les gestes de l'application et HyperFrames, et les règles du mouvement, sont dans `koracut-animer`. Ici : quoi mettre, où et combien.

## Ce qu'on met

| Élément | Quand | Durée | Outil |
|---|---|---|---|
| Titre d'ouverture | après l'accroche, pas avant | 2 à 3 s | `ajouter_titre` style `ouverture`, ou `motion_element_seul` sorte `titre` |
| Bandeau nom et fonction | première apparition d'une personne | 3 à 5 s | `motion_element_seul` sorte `bandeau` (texte « Nom \| Fonction »), ou `ajouter_titre` style `bas_ecran` |
| Carton (chiffre, étape, citation) | pour un fait qu'il faut lire | 2 à 4 s | `ajouter_titre` style `carton`, `motion_element_seul` sorte `compteur` pour un chiffre |
| Logo | discret dans un coin (5 à 8 % de la largeur) ou seulement à la fin | fin : 3 s | `motion_element_seul` sorte `logo`, ou le fichier posé sur V4 |
| Appel à l'action et contact | fin | 3 à 5 s | `ajouter_titre` style `carton` |

## Les règles

- **La charte d'abord** : deux polices au plus, les couleurs de la marque (le projet les donne dans `motion_lister`, champ `charte`, quand elle existe). Sans charte : blanc, police du système, rien de criard.
- **Lisible** : un titre se lit deux fois dans le temps où il reste ; 40 caractères par ligne au plus ; il entre 12 images après le début du plan, pas sur la coupe.
- **Jamais sur un visage ni sur l'écran d'un téléphone filmé** (refusé par Amadou Ba le 1er octobre 2026). Avant de poser un élément, regarde les plans qu'il recouvre (`voir_image`) et donne-lui une zone libre. Un élément qui dure sur plusieurs plans doit être dégagé sur chacun. Un texte long se pose par-dessus l'image, dans sa zone, avec une ombre douce ou un halo : pas de panneau plein qui coupe l'écran, pas de voile sur toute l'image.
- **Varier** (même refus) : même charte, mais une forme et une animation différentes d'un élément à l'autre (sceau, compteur, surligneur, bulles, rouleau de noms). Le même gabarit répété sur toute la vidéo fait modèle tout fait. Jamais de carte à bordure colorée sur un seul côté.
- **Zone sûre** : à plus de 5 % des bords en 16:9 ; en 9:16, hors du quart bas et du huitième haut.
- **Texte exact** : tel que la personne l'a donné, sans guillemets ajoutés, sans majuscules partout. Un nom, une fonction, un contact viennent de la demande ou du projet, jamais inventés.
- **Transitions** : la coupe franche dans 90 % des cas. Un fondu enchaîné pour le temps qui passe, un fondu au noir pour une fin de chapitre. Une transition principale et une ou deux en accent sur toute la vidéo, pas une différente à chaque coupe. Transitions « PowerPoint » (étoiles, volets fantaisie, cubes) et bande ou trait incliné qui balaie l'image : jamais.
- **Qui fait quoi** : le Monteur pose titres, cartons, fondus et gestes de l'application ; le Motion designer dessine ce qui demande HyperFrames (bandeau animé, compteur, logo qui arrive, transition graphique).

## Erreurs à éviter

- Habiller avant que le montage soit fixé : chaque déplacement de plan décale les titres.
- Un bandeau sur chaque apparition d'une même personne.
- Un titre posé sur la coupe, ou qui disparaît au milieu de sa lecture.
- Des effets de texte partout : l'habillage sert le propos, il ne le remplace pas.
- Le même panneau, la même étiquette ou la même transition d'un bout à l'autre.

## Outils

`ajouter_titre`, `ajouter_fondu`, `ajouter_transition`, `transition_app`, `animer_entree`, `animer_sortie`, `animer_texte`, `motion_element_seul`, `motion_transition`, `motion_lister`, `transformer_clip`, `voir_image`.
