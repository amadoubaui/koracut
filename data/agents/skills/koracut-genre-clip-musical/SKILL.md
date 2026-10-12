---
name: koracut-genre-clip-musical
description: "À lire pour un clip, une performance filmée, une vidéo de danse ou toute vidéo où la musique commande : la musique est posée d'abord, le play-back est calé à l'image près, les coupes suivent la chanson."
---

# Monter un clip musical

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/genre-clip-musical.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire pour un clip, une performance filmée, une vidéo de danse ou toute vidéo où la musique commande : la musique est posée d'abord, le play-back est calé à l'image près, les coupes suivent la chanson.

## Les étapes

1. **Poser la musique maître** sur A4 (ou A1 s'il n'y a pas de voix), entière, à la position 0. Elle ne bouge plus.
2. **Marquer la structure** : intro, couplets, refrains, pont, fin (marqueurs catégorie 3), puis les temps forts (catégorie 2) sur les passages à couper serré. L'outil `temps_musique` rend le tempo et la place de chaque temps du morceau sur la timeline : s'en servir plutôt que de compter à l'oreille. Si sa confiance est sous 0,6 (morceau ternaire, swing), vérifier ses temps contre ses temps forts avant de tout caler dessus.
3. **Caler chaque prise de play-back** : le son témoin de la prise contre la musique maître. Repère une attaque nette (une consonne, un coup de caisse claire) dans les deux, aligne-les (`deplacer_clip`), vérifie les lèvres avec `voir_image` sur un mot net. Son témoin coupé ensuite (`partie: "video"` ou piste masquée).
4. **Monter par sections** : une idée visuelle par section (couplet : l'artiste en plan moyen ; refrain : énergie, plans plus courts, danse ; pont : rupture, ralenti, autre lieu).
5. **Couper sur les temps** : sur les temps forts, une mesure sur deux en couplet, chaque temps sur un passage intense. Un plan qui commence exactement sur le temps.
6. **Ralentis** : un plan tourné à 50 i/s ralenti à 50 % (`changer_vitesse_clip`) reste fluide ; à 25 i/s, un ralenti saccade : dis-le.

## Critères de qualité

- Les lèvres sont synchrones à une image près sur les gros plans.
- La musique n'est jamais coupée ni déplacée.
- Les refrains se reconnaissent à l'image.

## Erreurs à éviter

- Couper sur tous les temps du début à la fin : épuisant.
- Caler le play-back sur l'image au lieu du son.
- Laisser le son témoin audible sous la musique.

## Outils

`ajouter_clip`, `deplacer_clip`, `couper_clip`, `rogner_clip`, `changer_vitesse_clip`, `ajouter_marqueur`, `voir_image`, `regler_piste`, `etat_timeline`.
