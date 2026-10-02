---
name: koracut-detourer-passer-derriere
description: "À lire avant de faire passer un cadre, un texte ou un pop-up derrière une personne, une main ou un objet filmé. Fiche du Monteur et du Motion designer, lue à la demande par `lire_competence`. Écrite le 1er octobre 2026, après les publicités Univers Cosmetix (des cadres qui flottent derrière un téléphone tenu en main)."
---

# Détourer un sujet, faire passer un élément derrière lui

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/detourer-passer-derriere.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de faire passer un cadre, un texte ou un pop-up derrière une personne, une main ou un objet filmé. Fiche du Monteur et du Motion designer, lue à la demande par `lire_competence`. Écrite le 1er octobre 2026, après les publicités Univers Cosmetix (des cadres qui flottent derrière un téléphone tenu en main).

## Le principe

Un élément posé au-dessus d'un plan le recouvre. Pour qu'il passe derrière le sujet, il faut trois étages :

| Étage | Piste | Ce qu'on y voit |
|---|---|---|
| en haut | « Sujets détourés » | le sujet seul, le reste transparent |
| au milieu | « Animations » | le cadre, le texte, le pop-up |
| en bas | la piste du plan | le plan entier |

`detourer_sujet` fabrique l'étage du haut : il calcule une vidéo du sujet seul et la pose exactement au-dessus du plan, même position, même durée. Ce qui est sur « Animations » se retrouve alors entre le plan et son sujet.

## Les étapes

1. Repère le plan et le moment où l'élément doit passer derrière. Si le plan est long, coupe-le autour de ce moment (`couper_clip`) : on ne détoure que le morceau utile, 30 secondes au plus.
2. Finis d'abord ce qui touche à l'image du plan : sa place, son recadrage (`transformer_clip`), son étalonnage. Le sujet détouré reçoit une copie de l'étalonnage et du Transformer au moment de l'appel, il ne suit pas les changements faits après.
3. Appelle `detourer_sujet` avec `clip_id` et `sujet`. Laisse `piste_id` vide.
4. Pose l'élément (`motion_element_seul`, `ajouter_titre`, `ajouter_clip`) sur une piste entre les deux. Les éléments Motion vont d'eux-mêmes sur « Animations », sous « Sujets détourés ».
5. Regarde (`voir_image`) à deux ou trois moments : au début, au milieu d'un geste, à la fin. Le bord du sujet doit être propre et l'élément bien coupé par lui.

## Quel sujet choisir

- `personne` : une ou plusieurs personnes, de face, de dos, en pied. Ce qu'elle tient (un sac, un produit) est souvent pris avec elle.
- `premier_plan` : ce qui se détache du fond, personne ou non. C'est le bon choix pour une main qui tient un téléphone, un produit posé, un objet.

Si l'outil répond `sujet_introuvable`, essaie l'autre valeur. Si le résultat prend un morceau de décor (un bord de porte avec `premier_plan`), essaie `personne`.

`adoucir` règle le bord : 2 par défaut. Monte à 4 ou 6 pour des cheveux ou un sujet flou, descends à 0 ou 1 pour un objet net comme un téléphone.

## Réussir l'effet

- L'élément doit dépasser du sujet. Un cadre entièrement caché derrière une personne ne se voit pas : place-le pour qu'il déborde d'un côté.
- Un élément qui bouge derrière le sujet (il glisse, il flotte) vend mieux la profondeur qu'un élément fixe.
- Un texte derrière une personne : gros, peu de mots, et lisible même à moitié caché.
- Pas de fondu d'entrée sur le sujet détouré : il est toujours là, c'est le plan lui-même.

## Les limites, à dire à la personne

- macOS seulement pour l'instant. Ailleurs l'outil répond `non_disponible` : dis-le en une phrase, ne cherche pas à contourner.
- Les cheveux fins et les gestes très rapides sortent adoucis. Sur un bord qui gêne, un élément placé ailleurs que contre ce bord règle souvent le problème.
- Un sujet flou en amorce (une épaule au tout premier plan) n'est en général pas pris.
- Le sujet détouré ne suit pas le plan. Après avoir déplacé, rogné, ralenti ou réétalonné le plan : supprime le sujet (`supprimer_clip`) et rappelle `detourer_sujet`.
- Seuls l'étalonnage et le Transformer sont recopiés. Si la réponse liste des `effets_non_copies` qui déplacent ou déforment l'image (rogner, miroir, stabilisation), pose-les aussi sur le sujet, sinon il se décale.

## Le coût

Compte 3 secondes de calcul par seconde de plan, et 5 à 10 Mo de disque par seconde (le fichier va dans le dossier `detourages` du projet). Un plan de 4 secondes : une douzaine de secondes, 30 Mo. Ne détoure pas « au cas où » : seulement les plans où un élément passe vraiment derrière. Le même appel sur le même plan reprend le fichier déjà calculé.

## Les erreurs à éviter

- Détourer un rush entier au lieu du morceau utile.
- Poser l'élément au-dessus de « Sujets détourés » : il recouvre tout, l'effet disparaît.
- Détourer avant d'avoir cadré et étalonné le plan.
- Oublier de regarder le résultat : un détourage se juge à l'image, pas à la réponse de l'outil.
