---
name: koracut-etalonnage-contre-jour
description: "À lire quand le sujet est devant une fenêtre, un ciel ou le soleil : visage sombre, fond très clair, halo."
---

# Étalonner un contre-jour

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/etalonnage-contre-jour.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand le sujet est devant une fenêtre, un ciel ou le soleil : visage sombre, fond très clair, halo.

## Reconnaître le cas

`mesurer_image` donne à la fois `noirs_ecrases_pourcent` et `blancs_brules_pourcent` élevés, ou une moyenne correcte avec une peau sous-exposée. Deux zones, deux traitements : un seul nœud ne suffit jamais.

## L'arbre

1. « Balance » : réglée sur la lumière du sujet, pas sur le fond.
2. « Expo » d'ensemble, prudente : `hautes_lumieres` -30 à -60 pour tenir le fond, `ombres` +20 à +40 pour ouvrir le sujet.
3. « Visage » : un nœud `parallele` avec une fenêtre `cercle` sur le visage (`doux` 0,1 à 0,15, `aspect` 0,8) et dedans Gamma +0,03 à +0,08, un peu de Gain. Ou un qualificateur de peau (fiche `koracut-etalonnage-raccord-peau`) si le visage bouge : la fenêtre, elle, ne suit pas le mouvement.
4. « Fond » si besoin : un nœud `outside` du nœud Visage (il reçoit la clé inversée) pour baisser le fond sans toucher au visage.
5. Contraste en dernier, léger.

## Critères

- Le visage à une moyenne de 90 à 130 à l'œil, sa peau près de sa ligne.
- Le fond garde un peu de matière, ou devient un blanc doux.
- Pas de halo sombre autour de la tête : si un bord apparaît, augmente le `doux` de la fenêtre.

## Si la personne bouge

Une fenêtre fixe ne suit pas un visage : le suivi (tracking) n'existe pas encore dans KoraCut. Prends le qualificateur de peau, ou une fenêtre large et douce, et dis-le (fiche `koracut-quand-un-outil-manque`).

## Erreurs à éviter

- Remonter le Lift de toute l'image : le fond devient laiteux.
- Une fenêtre au bord dur.
- Vouloir un fond parfait et un visage parfait sur un plan tourné sans réflecteur : vise le visage d'abord.

## Outils

`mesurer_image`, `etal_noeud_ajouter` (`parallele`, `outside`), `etal_noeud_regler` (`primaires`, `fenetres`, `qualificateur`), `voir_image`.
