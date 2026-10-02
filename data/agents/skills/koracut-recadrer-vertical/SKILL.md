---
name: koracut-recadrer-vertical
description: "À lire quand un plan horizontal doit entrer dans un montage vertical ou carré : garder le sujet, ne jamais déformer, et choisir entre recadrage serré et fond flou."
---

# Recadrer du 16:9 vers le 9:16 (et 1:1, 4:5)

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/recadrer-vertical.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand un plan horizontal doit entrer dans un montage vertical ou carré : garder le sujet, ne jamais déformer, et choisir entre recadrage serré et fond flou.

## Les mesures (projet 1080 × 1920)

- **Recadrage plein cadre** : `transformer_clip` avec `hauteur_px: 1920`, `largeur_px: 3413` (rapport 16:9 gardé), `y_px: 0`, et `x_px` qui place le sujet au centre : `x_px = 540 - (position du sujet dans la source, en pixels de la source 1920 × 1080) × 1,778`. Sujet au centre de la source : `x_px = -1167`. Sujet au tiers gauche (x = 640) : `x_px = -598`. `x_px` reste entre `-2333` (bord droit) et `0` (bord gauche), sinon du noir apparaît.
- **En 1:1 (1080 × 1080)** : `hauteur_px: 1080`, `largeur_px: 1920`, `x_px` entre -840 et 0. **En 4:5 (1080 × 1350)** : `hauteur_px: 1350`, `largeur_px: 2400`, `x_px` entre -1320 et 0.
- Regarde où est le sujet avec `voir_image` sur la source (au début, au milieu, à la fin du plan).

## Suivre le visage

KoraCut ne suit pas encore un visage tout seul (voir `koracut-quand-un-outil-manque`). Si le sujet bouge dans le cadre :

1. Regarde 3 à 5 instants du plan (`voir_image`), note la position du visage.
2. Pose une image clé sur `rect` à chaque instant (`poser_keyframe`, `interpolation: "courbe_douce"`, valeur `"x y largeur hauteur 1"`, par exemple `"-598 0 3413 1920 1"`), avec le `x_px` calculé. Le premier appel de `transformer_clip` pose l'effet Transformer et donne son `effet_index`.
3. Des mouvements lents : pas plus de 2 à 3 images clés par tranche de 5 s. Un recadrage qui tremble est pire qu'un sujet un peu décentré.
4. Dis à la personne que c'est un suivi à la main et où vérifier.

## Le fond flou (plan large, paysage, groupe)

Quand le recadrage serré couperait ce qui compte (deux personnes aux deux bords, un paysage) :

1. Pose le plan deux fois : une copie sur la piste du dessous (`ajouter_clip`, `partie: "video"`), la même portion, à la même position.
2. Le fond (dessous) : `transformer_clip` plein cadre (3413 × 1920, centré, `x_px: -1167`), puis `ajouter_effet` `avfilter.gblur` (flou fort) et, si besoin, un léger assombrissement (`regler_exposition` à -0,5 EV, par le Coloriste si tu n'as pas l'outil).
3. Le plan (dessus) : `transformer_clip` en largeur pleine, `largeur_px: 1080`, `hauteur_px: 608`, `x_px: 0`, `y_px: 656` (centré verticalement).
4. Sur une machine modeste, préviens : le flou double le coût du plan, un rendu d'aperçu (`rendre_apercu`) aide à la lecture.

## Critères de qualité

- Jamais d'image étirée : le rapport largeur / hauteur de la source est gardé.
- Le regard et les gestes restent dans le cadre ; les yeux vers le tiers haut.
- Rien d'important sous la zone des boutons (quart bas) ni sous la barre du haut (huitième haut) en 9:16.

## Erreurs à éviter

- Réduire le plan entier avec des bandes noires en haut et en bas.
- Recentrer un plan au milieu de la source par défaut sans avoir regardé où est le sujet.
- Un fond flou sur une interview serrée : le recadrage plein cadre suffit.

## Outils

`voir_image`, `transformer_clip`, `poser_keyframe`, `ajouter_clip`, `ajouter_effet`, `regler_exposition`, `rendre_apercu`, `etat_timeline`.
