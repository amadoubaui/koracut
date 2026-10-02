---
name: koracut-b-roll-plans-de-coupe
description: "À lire quand une parole a besoin d'images (interview, voix off, tutoriel) ou qu'une coupe dans un plan fixe se voit : choisir, poser et doser les plans d'illustration sur V2."
---

# Le B-roll et les plans de coupe

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/b-roll-plans-de-coupe.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand une parole a besoin d'images (interview, voix off, tutoriel) ou qu'une coupe dans un plan fixe se voit : choisir, poser et doser les plans d'illustration sur V2.

## Choisir

- Le plan montre ce que la voix dit, au moment où elle le dit : « je couds » sur les mains qui cousent, pas sur la façade.
- Sinon, il montre le lieu, la personne au travail, un détail qui ajoute (une matière, un geste, un visage qui écoute).
- Un plan stable, net, de 2 à 5 s utiles. Les marqueurs du dérushage disent où sont les bons passages.

## Poser

1. `etat_timeline` : la parole sur V1 et A1, les coupes à couvrir.
2. `ajouter_clip` du plan sur V2, `partie: "video"` (le son du B-roll reste de côté, sauf ambiance voulue sur A2), à cheval sur la coupe : environ 1 s avant, 2 à 3 s après.
3. Entre sur un mot fort ou au début d'une idée, pas au milieu d'un mot.
4. Si le son d'ambiance du plan sert, pose-le à part sur A2 (`partie: "audio"`), 15 à 20 dB sous la voix (le Mixeur règle).
5. Relis la zone.

## Doser

- Interview de plus d'une minute : le visage de la personne revient régulièrement. On veut la voir parler, surtout au début (qui est-ce ?) et sur les phrases fortes.
- Sur les réseaux, un B-roll toutes les 3 à 5 s tient l'attention ; en film d'entreprise, par passages de 10 à 20 s.
- Deux B-roll de suite : du large au serré, ou du serré au large, pas deux plans identiques.

## Erreurs à éviter

- Le B-roll « papier peint » qui n'a aucun rapport avec la phrase.
- Couvrir la première phrase d'un intervenant : on ne sait pas qui parle.
- Laisser le son du B-roll par-dessus la voix.
- Un B-roll qui déborde sur la scène suivante.

## Outils

`etat_timeline`, `lister_medias`, `ajouter_clip`, `rogner_clip`, `deplacer_clip`, `voir_image`.
