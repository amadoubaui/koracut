---
name: koracut-son-voix-nettoyage
description: "À lire avant de toucher au son d'un montage qui parle : la voix est l'élément le plus fort, on la nettoie et on l'égalise avant de placer quoi que ce soit dessous."
---

# Le son, étape 1 : la voix d'abord

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/son-voix-nettoyage.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire avant de toucher au son d'un montage qui parle : la voix est l'élément le plus fort, on la nettoie et on l'égalise avant de placer quoi que ce soit dessous.

## Les étapes

1. **Mesurer** chaque prise de voix (`mesurer_audio`, cible clip) : niveau, crête.
2. **Égaliser les voix entre elles** : dans une interview à deux ou un micro-trottoir, ramène les prises à 1 LU d'écart par le gain de clip (`regler_gain_clip`), avant tout traitement.
3. **Réduire le bruit de fond** s'il s'entend (climatiseur, ventilateur, rue, souffle) : `reduire_bruit`, niveau `moyen` d'abord (le réseau RNNoise enlève le bruit sous la voix) ; `fort` pour un endroit très bruyant (coupe aussi le son entre les phrases) ; `leger` si l'ambiance de la pièce doit rester. Toujours en premier dans la chaîne : l'outil se range lui-même avant les autres.
4. **Rendre la voix claire** : `ameliorer_voix` pose en un appel la chaîne de l'étape suivante (passe-haut 80 Hz, -3 dB à 300 Hz, +3 dB à 3,5 kHz, de-esseur, compresseur ratio 3, limiteur à -1 dB). Si la voix demande un réglage différent, fais-le à la main plutôt, avec :
5. **Nettoyer à la main** (`ajouter_effet_audio`, sur le clip ou la piste Voix), dans cet ordre :
   - passe-haut à 80 Hz (voix grave) ou 100 Hz (voix aiguë) : ronflement, chocs, vent ; 120 Hz pour un micro-cravate qui frotte sous un boubou ;
   - égaliseur léger : -3 dB vers 250 à 350 Hz si la voix est sourde, +2 dB vers 3000 à 5000 Hz pour la clarté, jamais plus de 4 dB ;
   - compresseur : ratio 3, attaque 10 ms, relâchement 150 ms, 3 à 6 dB de réduction sur les mots forts ;
   - de-esseur seulement si les « s » sifflent après la compression.
6. **Les clics** : tout son coupé net reçoit au moins 3 images de fondu (`ajouter_fondu`).
7. **Les silences** entre les phrases restent : c'est la respiration (voir `koracut-resserrer-parole`).
8. **Faire écouter** : tu lis des chiffres, tu n'entends pas. Donne un timecode précis à écouter quand un choix dépend de l'oreille.

## Ce que la réduction de bruit ne fait pas

Elle ne sauve pas une voix couverte par une musique forte, un vent qui sature le micro ou deux personnes qui parlent en même temps. Dans ce cas, dis-le, pose un marqueur catégorie 1 sur le passage et propose une autre prise ou un son séparé (`koracut-quand-un-outil-manque`). Elle décale le son de 10 ms (25 ou 50 i/s) à 50 ms (24 i/s) : invisible à l'image, mais ne la pose pas sur une musique.

## Les effets de voix

`effet_voix` donne un caractère (mégaphone, micro ancien, téléphone, radio, écho, grande salle, grotte, voix grave, voix aiguë, robot). C'est un choix de style (un appel, un flashback, un personnage) : jamais pour corriger, et seulement si la personne le demande. Un seul à la fois, `aucun` le retire.

## Critères de qualité

- La voix se comprend au téléphone, dans la rue, à faible volume.
- Toutes les voix d'une même vidéo sonnent au même niveau.
- Pas de souffle qui monte entre les phrases à cause d'une compression trop forte.

## Erreurs à éviter

- Normaliser avant d'avoir équilibré.
- Corriger une prise faible en montant toute la piste.
- Compresser une musique déjà masterisée.

## Outils

`mesurer_audio`, `regler_gain_clip`, `regler_piste_audio`, `reduire_bruit`, `ameliorer_voix`, `effet_voix`, `ajouter_effet_audio`, `regler_effet`, `ajouter_fondu`, `detecter_silences`.
