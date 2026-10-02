---
name: koracut-motion-design-camera
description: "À lire quand on te demande une vidéo entièrement en motion design (pub, explainer, présentation d'une appli) : un plan = une composition, la caméra qui file d'un plan à l'autre, le flou de mouvement. Écrit le 27 septembre 2026 d'après les trois vidéos AFEBABA (`AFEBABA Shop online : Marketplace Sénégal/VIdeos/GUIDE - Comment ces vidéos ont été montées.md`)."
---

# Motion design avec caméra : plans pleins cadre, coutures, flou de mouvement

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/motion-design-camera.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire quand on te demande une vidéo entièrement en motion design (pub, explainer, présentation d'une appli) : un plan = une composition, la caméra qui file d'un plan à l'autre, le flou de mouvement. Écrit le 27 septembre 2026 d'après les trois vidéos AFEBABA (`AFEBABA Shop online : Marketplace Sénégal/VIdeos/GUIDE - Comment ces vidéos ont été montées.md`).

## Le découpage

- Un plan du script = une composition HyperFrames = un clip sur la piste « Motion ». La personne rallonge, déplace ou change le texte d'un plan sans toucher aux autres.
- La racine porte `data-koracut-element="autre"` (le plan suit sa durée : le rallonger refait le rendu, la sortie reste à la fin), `data-koracut-fond="opaque"` (rendu en MP4, trente fois plus léger que le ProRes transparent), `data-koracut-start` (sa place) et `data-koracut-sequence` (sa séquence).
- Une composition écrite sur le disque avec ses polices et son logo s'importe par `motion_creer` avec `source` (dossier ou `.html`), puis `motion_poser` avec `piste_id` et `position`, puis `motion_rendre`.
- La voix fait l'horloge : durée du plan = 0,3 s + voix + 0,55 s. Les gestes tombent sur les mots dits (transcription de la voix).

## La caméra

- Des couches (fond à 0,5, interface à 1, premier plan à 1,3) : la caméra vise un point, zoome, tourne à peine ; chaque couche suit selon sa profondeur. C'est la parallaxe.
- Un chemin avec une intention : large, puis on va vers ce qui compte, puis on se pose. Jamais de balancement pour occuper.
- Un fond qui déborde largement de l'image : un travelling ne montre jamais un bord.

## Les coutures entre plans

- Un sens par film (gauche, haut, ou poussée en avant). Le plan qui part accélère (`power4.in`, 0,4 s, 500 px environ), le suivant arrive encore lancé dans le même sens (`power4.out`, 0,5 s). La coupe tombe au milieu du mouvement, pendant le flou.
- Les autres sens se gardent pour un sens : monter pour l'appel final, reculer pour une arrivée (la carte qui se révèle).
- Une poussée qui sort appelle une poussée qui entre (le zoom grossit des deux côtés).

## Le flou de mouvement

- Caméra : à chaque image, lire la caméra une image avant et après ; la vitesse de chaque couche donne un flou directionnel (`feGaussianBlur` avec un écart horizontal et vertical séparés), plafonné vers 45 px.
- Objet qui glisse seul (une carte, une pilule) : flou directionnel à sa vitesse, ou le composant `motion-blur` du registre HyperFrames (`data-hf-motion-blur`) sur un objet qui tourne ou change d'échelle.
- Les deux se vérifient sur un vrai rendu : un instantané ne suffit pas toujours.

## L'interface expliquée

- Montrer le vrai parcours de l'appli : l'écran, le doigt (un rond blanc cerclé de la couleur d'action) qui arrive, tape (écrasement, onde), et l'écran suivant qui glisse. Un tic doux à chaque tape.
- Les textes de l'interface viennent du vrai site. Aucun chiffre, prix, note ou nom de boutique hors du script validé.

## Le son

- Une musique douce (lounge, lo-fi, nappes), une par vidéo, 8 dB sous la voix (`appliquer_ducking`) ; des tics, pops et souffles feutrés passés au passe-bas ; trois pistes de sons au plus.
- En dernier : `normaliser_loudness` du mixage à -14 LUFS (un limiteur est posé si la crête dépasse), puis `mesurer_audio`.

## Erreurs à éviter

- Une première image vide : le hook doit montrer quelque chose dès l'image 1, la caméra déjà en mouvement.
- Des étiquettes posées sur l'interface : les mettre dans une couche de profondeur 0 (fixe à l'écran) ou hors du téléphone.
- Deux vidéos qui se ressemblent : changer le sens du film, la palette dominante, la courbe (vive ou lente) et la musique.
- Du texte blanc sur un fond hérité blanc : l'écran du téléphone force sa couleur de texte.
