---
name: koracut-sous-titres
description: "À lire avant de poser des sous-titres de dialogue : partir de la transcription, corriger l'orthographe française et les noms propres, découper en cartons lisibles, caler le rythme, mettre en avant les mots clés."
---

# Les sous-titres

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/sous-titres.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de poser des sous-titres de dialogue : partir de la transcription, corriger l'orthographe française et les noms propres, découper en cartons lisibles, caler le rythme, mettre en avant les mots clés.

## Les étapes

1. **Transcrire** la voix (`transcrire_media`, niveau `mots`, langue `fr` ou celle de la personne). Les positions sont dans le média : ajoute la position du clip sur la timeline et retire son `in` pour avoir le temps de la timeline (`pos + (d - in)`), en tenant compte des coupes faites depuis.
2. **Corriger le texte** avant tout découpage :
   - orthographe et accords (Whisper se trompe sur les homophones : « a / à », « ces / ses », « et / est ») ;
   - ponctuation française : espace avant `:` `;` `?`, guillemets « » ;
   - **noms propres** : personnes, lieux, marques, tels qu'écrits dans la demande, le script ou le bandeau. Un nom inconnu (« Ndèye », « Thiès », « Touba ») s'écrit comme la personne l'écrit, sinon on lui demande. Jamais une orthographe devinée pour un nom de personne ;
   - wolof ou autre langue dans une phrase française : écrit comme la personne l'écrit, sinon en italique entre étoiles seulement si c'est voulu ;
   - hésitations (« euh », « hum ») retirées, répétitions involontaires retirées, sens intact.
3. **Découper en cartons** :
   - 2 lignes au plus, 42 caractères par ligne au plus (32 à 37 en 9:16, où l'écran est étroit) ;
   - 1 à 6 s par carton, jamais plus de 17 caractères par seconde à lire ;
   - couper aux pauses naturelles, entre deux groupes de sens : jamais entre un article et son nom, un prénom et un nom, un verbe et son sujet court ;
   - un carton = une idée ; il commence au premier mot et finit 4 à 8 images après le dernier ;
   - pas de carton qui chevauche une coupe de plan quand on peut l'éviter.
4. **Style réseaux** (Reels, TikTok, vertical) : cartons plus courts (1 à 4 mots, une ligne), un mot clé par carton entre `*étoiles*` (couleur d'accent), centrés au-dessus du quart bas. Style classique (YouTube, film) : deux lignes en bas, blanc avec contour.
5. **Poser** avec `sous_titres_poser` (un seul appel pour tous les cartons, `remplacer: true` par défaut) et relire la réponse (nombre de cartons, premier et dernier).
6. **Vérifier** : `voir_image` sur deux ou trois cartons (lisibilité, zone sûre, rien sous les boutons de l'application).
7. **Livrer** : les sous-titres sont incrustés à l'export par défaut. Une version propre, sans sous-titres (télévision, traduction, une plateforme qui lit le `.srt`), se demande à l'export : `exporter` (ou chaque export de `exporter_serie`) avec `sous_titres: "sans"`. Ne pas désactiver la piste de sous-titres pour ça : elle resterait éteinte pour la suite.

## Critères de qualité

- Zéro faute. Les noms propres exacts.
- Le spectateur lit sans effort et regarde aussi l'image.
- Le sous-titre apparaît avec la parole, ni avant ni après.

## Erreurs à éviter

- Poser la transcription brute sans la relire.
- Des cartons de trois lignes ou de 60 caractères.
- Mettre des étoiles sur trois mots par carton : plus rien ne ressort.
- Sous-titrer avec `ajouter_titre` : les titres ne sont pas des sous-titres.

## Outils

`transcrire_media`, `etat_timeline`, `sous_titres_poser`, `voir_image`, `script_lire` (les noms propres du script), `exporter` (`sous_titres`).
