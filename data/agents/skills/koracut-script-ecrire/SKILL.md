---
name: koracut-script-ecrire
description: "À lire quand aucun script n'existe : comprendre la vidéo à partir de la demande et des rushs, puis écrire un script structuré dans le panneau Script, le faire valider, et seulement ensuite monter."
---

# Écrire le script quand il n'y en a pas

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/script-ecrire.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand aucun script n'existe : comprendre la vidéo à partir de la demande et des rushs, puis écrire un script structuré dans le panneau Script, le faire valider, et seulement ensuite monter.

## Les étapes

1. **Comprendre la demande** : le type de vidéo (celui du projet, sinon devine d'après la demande), la plateforme, la durée visée, le message unique, la cible. Une vidéo défend une seule idée.
2. **Comprendre les rushs** : l'inventaire (`koracut-organiser-inventaire-rushs`). Transcris ce qui parle (`transcrire_media`, niveau `segments`) : dans une interview, les meilleures phrases sont déjà le script.
3. **Poser le modèle du type** : chaque séquence a son script, écris dans celui de la séquence que tu montes (la séquence active, ou `sequence` : rang, nom ou uuid). `script_modele` (le type du projet, ou `type_video` si la demande en indique un autre). Il donne la structure du guide PIXEWORK : accroche, étapes, fin, appel à l'action, et le tableau `deroule` (colonnes scene, duree, voix, image, son, plans, fait).
4. **Remplir le déroulé** avec `script_ecrire` (`operations`, un seul appel si possible) :
   - `voix` : le texte exact qui se dit. Pour une interview, les phrases choisies dans la transcription, mot pour mot, avec le timecode du rush entre parenthèses. Pour une voix off à enregistrer, un texte court, à dire à voix haute : 2,5 mots par seconde environ en français.
   - `image` : ce qu'on voit, en quelques mots.
   - `plans` : les rushs à utiliser (cellule media : l'identifiant du chutier devient le nom du plan).
   - `son` : musique, ambiance, bruitage, silence.
   - `duree` : en secondes ; la somme tient dans la durée visée.
   - Ajoute ou supprime des lignes pour coller aux rushs réels (`ajouter_ligne`, `supprimer_ligne`).
5. **Vérifier la structure** : accroche forte dans les 3 premières secondes (voir `koracut-hook-teaser`), promesse avant 10 secondes, une idée par scène, une fin nette avec un seul appel à l'action.
6. **Faire valider** : dis à la personne que le script est dans le panneau Script, en deux lignes (durée totale, nombre de scènes, l'accroche retenue). Attends son accord avant de monter, sauf si le plan accepté disait de monter directement.

## Exemple de déroulé (Reel boutique, 45 s)

| Scène | Durée | Voix | Image | Plans |
|---|---|---|---|---|
| Accroche | 3 s | « Ce boubou, je l'ai cousu en une nuit. » | Awa tient le boubou face caméra | 01 - Accroche Awa |
| Qui | 7 s | « Je suis Awa, je couds à Médina depuis 12 ans. » | atelier, mains, machine | 05 - Interview Awa, 07 - Mains qui cousent |
| Le savoir-faire | 20 s | trois phrases de l'interview | gros plans tissu, coupe, essayage | 08, 09, 11 |
| La preuve | 10 s | une cliente parle | cliente qui sourit dans le miroir | 14 - Cliente essayage |
| Appel | 5 s | « Passe à l'atelier, [ADRESSE]. » | façade, carton avec l'adresse | 02 - Façade boutique |

## Critères de qualité

- Chaque ligne a une durée, un texte ou une image, et au moins un plan réel du chutier.
- Rien d'inventé : les phrases de l'interview sont celles que la personne a dites ; une adresse, un prix, un numéro viennent de la demande ou du projet, sinon `[À COMPLÉTER]`.
- Le script se lit en une minute.

## Erreurs à éviter

- Écrire un script « idéal » que les rushs ne permettent pas de monter.
- Changer le sens d'une phrase d'interview en la raccourcissant.
- Commencer par un logo, un générique ou « Bonjour à tous ».
- Monter tout de suite sans montrer le script.

## Outils

`script_lire`, `script_modele`, `script_ecrire`, `transcrire_media`, `lister_medias`, `voir_image`.
