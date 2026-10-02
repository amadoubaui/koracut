---
name: koracut-hook-teaser
description: "À lire pour ouvrir n'importe quelle vidéo destinée aux réseaux, et pour monter un teaser ou une bande-annonce : ce qui retient dans les 3 premières secondes, et comment construire 15 à 30 s qui donnent envie."
---

# L'accroche des 3 premières secondes et le teaser

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/hook-teaser.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire pour ouvrir n'importe quelle vidéo destinée aux réseaux, et pour monter un teaser ou une bande-annonce : ce qui retient dans les 3 premières secondes, et comment construire 15 à 30 s qui donnent envie.

## L'accroche (toute vidéo)

Les 3 premières secondes décident si la personne reste. On commence par l'une de ces entrées, jamais par un logo, un générique ou « Bonjour à tous » :

- **le résultat** : l'image finale, le plat servi, la maison terminée, avant de montrer comment ;
- **la phrase la plus forte** de l'interview, sortie de son contexte mais fidèle à son sens ;
- **la question** que la vidéo résout : « Pourquoi ton Reel ne passe pas 200 vues ? » ;
- **le conflit ou la surprise** : un avant et après, un chiffre inattendu ;
- **le mouvement** : l'image la plus dynamique des rushs.

Mécanique : un texte à l'écran dès la première image (la vidéo est souvent vue sans le son), une coupe avant 2 s, la promesse avant 10 s.

## Le teaser (15 à 30 s)

1. **Choisir 5 à 10 moments** dans le dérushage : les plus beaux plans, les deux ou trois phrases qui donnent envie sans tout dire.
2. **Structure** : ouverture forte (1 à 3 s) ; montée (plans de plus en plus courts, sur la musique) ; une phrase-clé qui pose l'enjeu ; un sommet (le plan le plus fort) ; un carton final (titre, date, où voir), 2 à 3 s ; une fin nette.
3. **Musique d'abord** : pose-la, marque ses temps forts (catégorie 2), coupe dessus. La montée suit la musique.
4. **Ne pas tout révéler** : pas la fin de l'histoire, pas la réponse à la question.
5. **Durée** : 15 s pour Instagram et TikTok, 30 s pour YouTube ou une bande-annonce d'événement.

## Critères de qualité

- Coupe le son et regarde les 3 premières secondes : on comprend de quoi parle la vidéo.
- Le premier plan n'est ni noir, ni un fondu, ni un logo.
- Le teaser se termine sur une information utile (quoi, quand, où).

## Erreurs à éviter

- Un fondu d'entrée de 1 s sur une vidéo réseaux : une seconde perdue.
- Une accroche qui trompe : la vidéo doit tenir la promesse.
- Un teaser qui résume tout le film dans l'ordre.

## Outils

`script_lire`, `ajouter_clip`, `rogner_clip`, `ajouter_marqueur`, `ajouter_titre`, `animer_texte`, `sous_titres_poser`, `etat_timeline`.
