---
name: koracut-genre-publicite
description: "À lire pour une publicité, un spot produit, une vidéo de promotion d'un commerce ou d'un service (réseaux, YouTube Ads, télévision) : structure courte, produit visible, un seul appel à l'action."
---

# Monter une publicité

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/genre-publicite.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire pour une publicité, un spot produit, une vidéo de promotion d'un commerce ou d'un service (réseaux, YouTube Ads, télévision) : structure courte, produit visible, un seul appel à l'action.

## La structure (15, 30 ou 60 s)

1. **Accroche** (0 à 3 s) : le problème, le résultat ou le produit en action (`koracut-hook-teaser`).
2. **Le produit ou le service** (3 à 10 s) : montré clairement, en gros plan, dans les mains, en usage.
3. **La preuve** : un bénéfice concret, un client qui parle, un avant et après, un chiffre vrai.
4. **L'offre** si elle existe : prix, promotion, date, en texte lisible 3 s au moins. Jamais un prix inventé : il vient de la demande, sinon `[PRIX]` et une question.
5. **L'appel à l'action et le contact** : un seul geste (appeler, écrire sur WhatsApp, passer en boutique), logo et contact à l'écran 3 à 5 s.

## Les règles

- Le nom de la marque ou le produit apparaît avant 5 s : une partie du public décroche tôt.
- Rythme vif (1 à 3 s par plan), musique qui porte, coupes sur les temps.
- Textes courts : 5 à 7 mots par carton, la marque en couleur d'accent.
- Formats : une version par plateforme (9:16 pour Reels et TikTok, 1:1 ou 4:5 pour le fil, 16:9 pour YouTube) ; les durées exactes que la plateforme accepte (`koracut-export-formats-plateformes`).
- Télévision : -23 LUFS, pas de texte dans les 5 % du bord, la fiche technique de la chaîne fait foi.

## Ce qu'une campagne réelle a appris (Univers Cosmetix, 1er octobre 2026)

- **Le script du client se recopie tel quel** dans le panneau Script, puis se suit : son minutage (0-3, 3-10, 10-18, 18-24, 24-30 s) donne la place de chaque phrase de voix, et la voix donne la longueur des plans.
- **Vérifie chaque chiffre contre ce que le client a fourni.** Un script qui dit « jusqu'à -50 % » alors que sa liste de produits s'arrête à -25 % : monte le script, et dis l'écart à la personne avant toute diffusion.
- **Un prix affiché se lit en entier** : le nom court, le prix de base barré, le prix réduit en grand, la remise calculée. 1,3 s par produit au moins.
- **Les photos de produits** : si le site refuse le téléchargement (erreur 403), ne t'acharne pas ; demande-les à la personne dès le plan, avec la liste des fichiers attendus.
- **Ce qui manque s'écrit entre crochets à l'écran** (`[DATE]`) dans une v1, et figure en tête de la liste « à valider ».
- **La même carte de fin sur toute la campagne** : logo, signature, un seul appel à l'action.
- **Trois versions** : v1 fidèle au script, v2 après ta propre relecture (lisibilité, plans trop sombres, mouvement), v3 après l'avis de la personne. Chaque version est un nouveau fichier.

## Critères de qualité

- Sans le son, on sait ce qui est vendu, par qui, et comment l'acheter.
- Une seule offre, un seul appel.
- Le contact affiché est exactement celui de la demande ou du projet.

## Erreurs à éviter

- Commencer par le logo animé pendant 3 s.
- Trois appels à l'action (appelle, abonne-toi, visite le site).
- Un contact ou un prix inventé ou recopié d'un autre projet.

## Outils

`script_modele`, `script_ecrire`, `ajouter_clip`, `rogner_clip`, `ajouter_titre`, `animer_entree`, `motion_element_seul` (par le Motion designer), `sous_titres_poser`, `ajouter_marqueur`.
