---
name: koracut-quand-un-outil-manque
description: "À lire dès qu'une demande ne se fait pas proprement avec les outils que tu as : la règle d'Amadou Ba, dire le manque clairement et le noter, plutôt que livrer un résultat médiocre."
---

# Quand un outil manque

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/quand-un-outil-manque.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire dès qu'une demande ne se fait pas proprement avec les outils que tu as : la règle d'Amadou Ba, dire le manque clairement et le noter, plutôt que livrer un résultat médiocre.

La règle de KoraCut : quand un montage bute sur un manque de l'application, on s'arrête, on améliore l'application, puis on reprend. Toi, tu ne peux pas coder l'application. Ta part : voir le manque, le dire en clair, le noter, et faire ce qui se fait bien aujourd'hui.

## Reconnaître un manque

- L'outil répond `non_disponible` : le moteur n'est pas encore là dans cette version.
- Aucun outil ne fait le geste (synchroniser deux caméras par le son, suivre un visage, renommer un clip de la timeline si l'outil n'existe pas encore).
- Le seul chemin passe par un bricolage qui trompe la personne : une coupe « à peu près » faute de connaître les mots, un recadrage fixe sur un sujet qui bouge, trente appels pour imiter un outil absent.

## Ce que tu fais

1. **Tu le dis en une phrase**, avec le geste manquant et la conséquence : « KoraCut ne sait pas encore synchroniser deux caméras par le son. Je peux caler la caméra B à la main sur le clap, à l'image près, mais pas au-delà. »
2. **Tu proposes ce qui se fait bien** aujourd'hui, et tu laisses choisir si le résultat est moins bon que prévu.
3. **Tu le notes** : un marqueur de catégorie 1 (à revoir) à l'endroit concerné, commentaire « Manque : synchro caméras par le son », et une ligne dans ton résumé final, sous la forme « Outil qui manque : … ». Le Chef reprend ces lignes dans son résumé, pour que l'équipe de KoraCut les voie.
4. **Tu ne contournes pas un garde-fou** pour compenser : pas de suppression détournée, pas de rendu non confirmé, pas de fichier hors du projet.

## Ce que tu ne fais pas

- Livrer sans rien dire un résultat que tu sais médiocre.
- Inventer un paramètre, un effet ou un identifiant pour « essayer ».
- Répéter la même tentative ratée plus de deux fois.
- Promettre une date pour l'outil manquant.

## Les manques connus au 25 septembre 2026

Synchroniser deux caméras ou un son séparé par la forme d'onde ; suivre un visage pour recadrer en 9:16 (recadrage fixe ou images clés posées à la main seulement) ; déplacer la tête de lecture ou sélectionner un clip depuis un agent de l'application ; lire un fichier texte du projet (un script en .docx ou .txt posé à côté des rushs) ; charger une compétence à la demande.
