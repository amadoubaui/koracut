---
name: koracut-etalonnage-look-lut
description: "À lire quand la personne veut une ambiance (chaud, cinéma, froid, sarcelle et orange) ou qu'une LUT créative est demandée : doser un rendu sur des plans déjà justes."
---

# L'étalonnage, étape 3 : le look et les LUT créatives

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-look-lut.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand la personne veut une ambiance (chaud, cinéma, froid, sarcelle et orange) ou qu'une LUT créative est demandée : doser un rendu sur des plans déjà justes.

## Les étapes

1. Les plans sont corrigés et raccordés (étapes 1 et 2). Sinon, on commence par là et on le dit.
2. **Choisir** : une LUT livrée (`BLUE_TINT`, `CINEMATIC`, `GREEN_TINT`, `TEAL_ORANGE`) ou un fichier de la personne dans le dossier `luts` du projet. Rien ne se télécharge.
3. **Poser dans un nœud « Look » en fin d'arbre** (`etal_lut` { `fichier`, `melange` 30 à 70 }) sur le plan de référence, mesurer, regarder (`voir_image`). Un look commun à toute la vidéo va dans l'arbre de séquence (`sequence: true`).
4. **Doser** : `melange` de la LUT, ou `cle.sortie_gain` du nœud. Si la LUT écrase les noirs ou abîme la peau, corrige dans les nœuds qui la précèdent, pas en empilant un deuxième look.
5. **Étendre** au reste de la scène (`etal_copier` en mode `ajouter`, ou l'arbre de séquence), puis vérifier les peaux plan par plan.
6. **Montrer l'avant et l'après** : `activer_etalonnage` à faux puis à vrai sur un plan, pour que la personne compare.

## Les ambiances courantes

- **Chaud, doré** : `temp` +300 à +600, un peu de jaune dans les hautes lumières (roues `log`).
- **Froid, matin** : `temp` -300 à -600, ombres légèrement bleues.
- **Cinéma** : `contraste` 1,1 à 1,2, saturation 45, noirs à 12 à 16 (pas écrasés).
- **Exporter** le look en `.cube` pour un autre logiciel : `etal_exporter_lut`.
- **Sarcelle et orange** : `TEAL_ORANGE`, à surveiller sur les peaux foncées, qui virent vite au rouge.

## Critères de qualité

- Le look est le même sur toute la vidéo, ou change avec une raison (un flash-back, un autre lieu).
- Les peaux restent justes sous le look.

## Erreurs à éviter

- Un look différent par plan.
- Une LUT créative posée avant la correction.
- Empiler deux LUT créatives.

## Outils

`etal_lut`, `etal_noeud_regler`, `etal_copier`, `etal_exporter_lut`, `mesurer_image`, `activer_etalonnage`, `voir_image`, `rendre_apercu`.
