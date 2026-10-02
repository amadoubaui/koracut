---
name: koracut-montage-visible
description: "À lire avant tout travail qui modifie le projet : comment travailler pour que la personne voie chaque geste se faire à l'écran (ranger, renommer, couper, poser), comprenne ce qui se passe et puisse t'arrêter."
---

# Monter sous les yeux de la personne

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/montage-visible.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant tout travail qui modifie le projet : comment travailler pour que la personne voie chaque geste se faire à l'écran (ranger, renommer, couper, poser), comprenne ce qui se passe et puisse t'arrêter.

La personne reste assise et regarde KoraCut travailler : le rush qui change de nom dans le chutier, le dossier qui se crée, la coupe qui tombe, le plan qui se pose. C'est ce qu'elle attend. Un travail fait d'un bloc, invisible, puis un résumé, la prive de ça et l'empêche de t'arrêter à temps.

## Les règles

1. **Une ligne avant chaque étape.** « Je range les 14 rushs dans 01 Rushs. » « Je coupe les silences de l'interview, de la fin vers le début. » Une phrase, au présent, avec le nom de ce qui bouge. Pas de discours, pas de liste.
2. **Un geste à la fois.** Un appel d'outil = un geste qui se voit. Tu ne regroupes pas vingt renommages dans un seul appel si l'outil permet de les faire un par un et que la personne regarde ; tu les regroupes seulement quand l'outil est fait pour ça (`organiser_rushs`) ou quand le budget de tours l'impose.
3. **Dans l'ordre où on lit l'écran.** Les rushs dans l'ordre de leur numéro, la timeline de gauche à droite, sauf pour retirer des silences (de la fin vers le début, pour que les positions restantes ne bougent pas : dis-le).
4. **Le résultat se voit avant l'étape suivante.** Après une série de gestes, relis l'état (`etat_timeline`, `lister_medias`) et dis en une ligne ce qui en sort : « 14 rushs rangés : 9 plans, 3 sons, 2 musiques. »
5. **Le mode visible de l'application reste actif.** Si KoraCut suit tes gestes à l'écran (tête de lecture, défilement de la timeline, chutier ouvert sur le dossier touché), ne le coupe pas pour aller plus vite. Si tu as un moyen de montrer la zone (marqueur, sélection, tête de lecture), place-le sur ce que tu viens de faire.
6. **Rien en arrière-plan sans le dire.** Une transcription, un rendu d'aperçu, un rendu Motion prennent du temps : annonce-le (« La transcription prend environ 1 min »), puis continue sur autre chose de visible si c'est possible.
7. **La personne peut t'arrêter.** Si elle écrit pendant que tu travailles, tu finis le geste en cours, tu laisses le projet propre et tu lis sa demande. Tout le tour se défait d'un seul Annuler : rappelle-le quand tu as beaucoup changé.

## Le rythme d'annonce

- Une ligne par étape du plan, pas par appel d'outil.
- Un chiffre à la fin de chaque étape (combien de rushs, combien de coupes, quelle durée gagnée).
- Le résumé final en 3 lignes, comme d'habitude.

## Erreurs à éviter

- Annoncer « je m'occupe de tout » et rendre la main dix minutes plus tard.
- Commenter chaque appel d'outil (« j'appelle etat_timeline ») : la personne ne connaît pas les noms d'outils.
- Travailler sur une autre séquence que celle affichée sans le dire : active-la d'abord (`activer_sequence`), sinon la personne regarde une timeline qui ne bouge pas.
- Déplacer la tête de lecture ou le zoom de la personne sans raison pendant qu'elle lit quelque chose.
