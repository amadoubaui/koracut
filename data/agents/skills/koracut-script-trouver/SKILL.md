---
name: koracut-script-trouver
description: "À lire avant de monter : chercher s'il existe déjà un script (panneau Script du projet, texte donné par la personne, fichier posé avec les rushs) et le lire, avant d'en écrire un."
---

# Trouver le script existant

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/script-trouver.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de monter : chercher s'il existe déjà un script (panneau Script du projet, texte donné par la personne, fichier posé avec les rushs) et le lire, avant d'en écrire un.

## Où chercher, dans l'ordre

1. **Le panneau Script** : `script_lire` (format `markdown` pour lire, `json` pour les identifiants). Chaque séquence du projet a son script : sans `sequence`, tu lis celui de la séquence active ; `sequence` (rang, nom ou uuid) vise une autre séquence, et `sequences` dans la réponse dit lesquelles ont un script écrit. Si la réponse dit `enregistre: false`, ce n'est que le modèle du type de vidéo, pas un vrai script : rien n'a été écrit. Si elle dit `enregistre: true`, un script existe : lis-le entier.
2. **La demande de la personne** : un texte collé dans le panneau des agents, une liste de scènes, un conducteur. C'est un script, même court.
3. **Les médias du projet** : `lister_medias` avec la recherche `script`, `conducteur`, `texte`, `brief`, `voix off`. Un fichier texte ou PDF ne se lit pas encore depuis un agent de l'application (voir `koracut-quand-un-outil-manque`) : demande à la personne de coller son contenu dans le panneau Script ou dans la conversation.
4. **Une voix off déjà enregistrée** : sa transcription (`transcrire_media`) est le script de fait. On monte l'image sur elle.
5. **Les marqueurs** de la timeline de catégorie 3 (chapitres) : la personne a peut-être déjà découpé ses scènes.

## Ce qu'on lit dans un script

- L'accroche (les 3 premières secondes), la promesse, les scènes dans l'ordre, la fin et l'appel à l'action.
- Pour chaque scène : la durée visée, ce qui se dit, ce qu'on voit, le son, les plans prévus, et la case Fait.
- La durée totale : somme des durées. Si elle dépasse la durée idéale du type de vidéo, dis-le avant de monter.

## Ensuite

- Script trouvé et complet : monter d'après lui (`koracut-script-monter-d-apres`).
- Script trouvé mais partiel (pas de plans, pas de durées) : compléter les colonnes vides à partir de l'inventaire, sans changer le texte de la personne, et le lui montrer.
- Pas de script : `koracut-script-ecrire`.

## Erreurs à éviter

- Prendre le modèle du type (`enregistre: false`) pour le script de la personne.
- Réécrire le texte d'un script existant sans qu'on te le demande : tu complètes, tu ne corriges pas son propos.
- Ignorer un texte collé dans la conversation parce qu'il n'est pas dans le panneau Script : recopie-le dans le panneau (`script_ecrire`) pour qu'il y reste.

## Outils

`script_lire`, `script_ecrire`, `lister_medias`, `transcrire_media`, `etat_timeline` (marqueurs).
