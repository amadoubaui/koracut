---
name: koracut-coupes-j-l
description: "À lire pour adoucir une coupe entre deux plans qui parlent, ou entre une scène et la suivante : faire arriver le son avant l'image (J) ou le laisser continuer sous l'image suivante (L)."
---

# Les coupes J et L

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/coupes-j-l.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire pour adoucir une coupe entre deux plans qui parlent, ou entre une scène et la suivante : faire arriver le son avant l'image (J) ou le laisser continuer sous l'image suivante (L).

## Quand

- **J-cut** : le son du plan suivant arrive 8 à 15 images avant son image. Pour entrer dans une interview depuis un plan d'illustration, pour annoncer une nouvelle scène par son ambiance.
- **L-cut** : le son du plan continue 8 à 15 images sous l'image suivante. Pour montrer la réaction de celui qui écoute, pour sortir d'une scène en douceur.
- Une coupe franche image et son à la même image reste la règle pour un choc, une blague, un changement de ton voulu.

## Comment

1. `etat_timeline` : les deux clips (image et son de chaque plan, même `grp`).
2. Vérifie la matière : pour un J-cut, le son du plan suivant doit avoir de la réserve avant son `in` ; pour un L-cut, le son du plan précédent doit en avoir après son `out`.
3. `rogner_clip` avec `seul: true` sur le clip audio seulement : `cote: "debut"` pour un J-cut (le son commence plus tôt), `cote: "fin"` pour un L-cut (le son finit plus tard). Si la place est prise (`place_occupee`), raccourcis d'abord le son voisin du même nombre d'images, avec un fondu de 6 images.
4. Relis : `pos - in` doit être le même pour l'image et le son de chaque plan. Sinon la synchro a bougé : annule et recommence.

## Critères de qualité

- On ne remarque pas la coupe ; on a l'impression que la scène continue.
- Pas de doublon de son : les deux sons ne se chevauchent pas, ou se croisent en fondu court.
- La synchro labiale est intacte dans chaque plan.

## Erreurs à éviter

- Faire des J et des L à chaque coupe : le montage devient mou.
- Oublier `seul: true` : l'image bouge avec le son et rien ne change.
- Un J-cut sur un son sans réserve : le son commence sur un bruit de fin de prise.

## Outils

`etat_timeline`, `rogner_clip`, `ajouter_fondu`, `deplacer_clip`.
