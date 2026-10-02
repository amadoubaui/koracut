---
name: koracut-methode-monter-une-video
description: "À lire quand la personne demande « monte-moi cette vidéo » ou toute demande qui part de rushs bruts pour arriver à un fichier livré : les neuf étapes dans l'ordre, qui fait quoi, et où s'arrêter pour faire valider."
---

# Monter une vidéo de A à Z

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/methode-monter-une-video.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand la personne demande « monte-moi cette vidéo » ou toute demande qui part de rushs bruts pour arriver à un fichier livré : les neuf étapes dans l'ordre, qui fait quoi, et où s'arrêter pour faire valider.

## Les neuf étapes

| # | Étape | Qui | Compétence | Ce qui sort |
|---|---|---|---|---|
| 1 | Comprendre la demande | le Chef | cette fiche | une phrase : quoi, pour qui, quel format, quelle durée |
| 2 | Organiser et renommer | le Monteur | `koracut-organiser-inventaire-rushs`, `koracut-organiser-nommage`, `koracut-organiser-arborescence` | rushs rangés, numérotés, nommés, inventaire dans le script |
| 3 | Le script | le Chef, le Monteur | `koracut-script-trouver`, `koracut-script-ecrire` | un tableau « deroule » validé, une ligne par scène |
| 4 | Le dérushage | le Monteur | `koracut-derushage` | bonnes prises marquées, mauvaises écartées |
| 5 | Le montage | le Monteur | `koracut-bout-a-bout`, `koracut-rythme-respiration`, la fiche du genre | la séquence montée, dans l'ordre du script |
| 6 | L'habillage | le Monteur, le Motion designer | `koracut-habillage-titres-bandeaux`, `koracut-animer`, `koracut-sous-titres` | titres, bandeaux, sous-titres, transitions dosées |
| 7 | Le son puis la couleur | le Mixeur, le Coloriste | `son-*`, `etalonnage-*` | voix claire, musique sous la voix, -14 LUFS ; plans justes et raccord |
| 8 | La vérification | le Chef | `koracut-verifier-le-montage` | rien de vide, rien de faux, regardé à l'image |
| 9 | L'export | le Chef | `koracut-export-formats-plateformes`, `export-verifications` | un fichier par plateforme, nom versionné |

Le son passe avant la couleur dans ce tableau parce qu'un problème de voix bloque la livraison plus souvent qu'un plan un peu chaud. Les deux peuvent s'inverser si la personne attend surtout l'image.

## 1. Comprendre en une lecture

Lis `etat_timeline` (resume), `lister_medias`, `script_lire`. Réponds pour toi à cinq questions : quel type de vidéo (le projet le garde, sinon devine d'après la demande), pour quelle plateforme (donc quel format et quelle durée), quel message unique, quels rushs parlent, y a-t-il une voix. Ce qui ne se lit pas et change le travail fait l'objet d'UNE question. Le reste devient une hypothèse annoncée dans le plan.

## Le plan à proposer

Une demande « monte-moi cette vidéo » touche plusieurs agents : `proposer_plan` toujours. Six étapes au plus, une phrase chacune, l'agent nommé. Exemple :

- Résumé : « Un Reel de 45 s sur la boutique d'Awa, 9:16, à partir des 14 rushs du dossier Tournage. »
- Le Monteur : ranger et renommer les 14 rushs (numérotés dans l'ordre du tournage), inventaire dans le script.
- Le Monteur : écrire le script d'après la transcription de l'interview, à valider.
- Le Monteur : monter d'après le script, sous-titres compris.
- Le Motion designer : bandeau « Awa Diop | Fondatrice » à l'entrée de l'interview.
- Le Mixeur : musique sous la voix, -14 LUFS.
- Hypothèses : 9:16 pour Instagram ; musique : la seule du chutier.

## Deux arrêts obligatoires

1. **Après le script** (étape 3), quand c'est toi qui l'as écrit : la personne le lit dans le panneau Script et dit oui ou corrige. On ne monte pas 3 minutes sur un script qu'elle n'a pas vu. Si le plan accepté disait « script à valider », attends sa réponse.
2. **Avant l'export** (étape 9) : le rendu se confirme toujours à part.

## Travailler sous ses yeux

Chaque étape se fait à l'écran, geste par geste : voir `koracut-montage-visible`. Tu annonces l'étape en une ligne (« Je range les rushs »), tu la fais, tu dis ce qui en sort.

## Critères de qualité

- Chaque étape laisse une trace lisible par la personne : noms de fichiers, dossiers du chutier, script coché, marqueurs.
- L'ordre du métier est tenu : on n'étalonne pas un plan qui va sauter, on ne mixe pas une musique qui va bouger.
- À la fin, le résumé en 3 lignes donne la durée, le nombre de plans, le niveau sonore, et ce qu'il faut regarder.

## Erreurs à éviter

- Monter avant d'avoir rangé : on perd le fil dès le vingtième rush.
- Sauter le script parce que « c'est court » : même un Reel de 20 s a une accroche, un milieu et une fin.
- Tout faire en un seul tour géant sans point d'arrêt : si la personne n'aime pas le script, tout le montage est à refaire.
- Bricoler quand un outil manque : voir `koracut-quand-un-outil-manque`.
