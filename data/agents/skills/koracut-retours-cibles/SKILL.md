---
name: koracut-retours-cibles
description: "À lire dès que la personne donne un retour sur un montage déjà fait (« change ce plan », « ce texte est mal placé », « enlève ça ») : on localise, on modifie cela et rien d'autre, on ne remonte jamais la vidéo."
---

# Appliquer un retour : ne toucher que ce qui est demandé

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/retours-cibles.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire dès que la personne donne un retour sur un montage déjà fait (« change ce plan », « ce texte est mal placé », « enlève ça ») : on localise, on modifie cela et rien d'autre, on ne remonte jamais la vidéo.

Règle d'Amadou Ba (1er octobre 2026) : « à chaque fois que j'ai un retour, ça reprend tout le projet. Il faut modifier uniquement ce retour-là. Si on ne demande pas de reprendre la vidéo de A à Z, on ne le fait pas. »

## Les étapes

1. **Une ligne par retour.** Recopie chaque point dans une liste (script, ou message de plan) : ce qui est visé, où (séquence, timecode, clip), ce qui est demandé.
2. **Localise** : `etat_timeline` (niveau complet), `voir_image` au timecode. Note les `clip_id` visés. Si tu ne trouves pas, pose UNE question.
3. **Dis ce qui ne bouge pas** : dans `proposer_plan`, écris « tout le reste est laissé tel quel : coupes, étalonnage, son, habillage ».
4. **Modifie sur place**, dans la même séquence, un retour = un tour (une étape d'annulation) :
   - remplacer un plan : même position, même durée, puis `etal_copier` depuis un plan voisin de la même scène et vérifie (`mesurer_image`) ;
   - déplacer ou refaire un élément : `motion_element` ou `motion_ecrire` sur cet élément seul ;
   - un son : ajoute ou retire ce clip, ne refais pas le mixage.
5. **Ne régénère rien d'autre** : pas de nouvelle séquence, pas de nouvel étalonnage global, pas de nouvelle voix, pas de nouvelles coupes, pas de zoom ajouté « pour faire mieux ».
6. **Vérifie par différence** : exporte, compare à la version d'avant aux endroits NON visés (même image, mêmes couleurs), et montre à la personne la liste : fait, pas fait, pourquoi.
7. **Nouvelle version = nouveau fichier** (`- v4`), l'ancienne reste.

## Ce qui est validé ne se retouche pas

Quand la personne dit « c'est bien » d'une chose (l'étalonnage, un plan, un son), c'est figé : tu le notes et tu n'y reviens plus, même pour l'améliorer.

## Critères de qualité

- Hors des points demandés, la nouvelle version est identique à l'ancienne.
- Chaque retour a sa ligne « fait » avec le timecode.

## Erreurs à éviter

- Repartir d'une séquence neuve parce que c'est plus simple.
- Profiter d'un retour pour changer autre chose.
- Répondre « fait » sans avoir regardé l'export.

## Outils

`etat_timeline`, `voir_image`, `proposer_plan`, `supprimer_clip`, `ajouter_clip`, `etal_copier`, `mesurer_image`, `motion_element`, `motion_ecrire`, `annuler_dernier_tour`.
