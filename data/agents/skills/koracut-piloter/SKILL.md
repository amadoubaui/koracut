---
name: koracut-piloter
description: "Piloter KoraCut pendant qu'elle tourne, par sa porte de commande (koracut-ctl en ligne de commande, ou le serveur MCP koracut) : lire l'état, importer, organiser les rushs, poser, couper, exporter, en un seul Annuler. À lire en premier, avant toute action dans KoraCut."
---

# Piloter KoraCut

KoraCut (montage vidéo, PIXEWORK) ouvre une porte de commande locale quand elle tourne : une prise sur la machine, un jeton, des méthodes nommées `famille.methode` qui répondent en JSON. Tout ce que tu fais par la porte se voit à l'écran de la personne (le clip apparaît dans le chutier, se pose sur la timeline, la coupe se fait) et se défait d'un seul Annuler. C'est la méthode, pas les captures d'écran ni les clics.

## Deux façons d'entrer

**`koracut-ctl`** (dans le terminal) : `koracut-ctl <famille> <methode> clef=valeur …`. Une valeur qui se lit en JSON (nombre, `true`, `[liste]`, `{objet}`) le reste, sinon c'est une chaîne. `--json` compacte la sortie, `--aide [famille]` liste tout avec les paramètres. Code de retour 0 ok, 1 erreur (le JSON de l'erreur sur stderr), 2 aucune KoraCut ouverte. Si le terminal a été ouvert par KoraCut, `KORACUT_PORTE` désigne déjà la bonne instance ; sinon, avec plusieurs KoraCut ouvertes, `--reglage <nom>` choisit (le nom est dans `porte.json`).

**Le serveur MCP `koracut`** : un outil par méthode, `koracut_<famille>_<methode>` (`koracut_timeline_etat`, `koracut_medias_importer`…), mêmes paramètres, même réponse. Les ressources `koracut://projet`, `koracut://timeline`, `koracut://medias`, `koracut://actions` donnent l'état. Le serveur est livré avec l'application, sans Node : c'est `koracut-ctl mcp` (stdio). Si l'outil n'est pas là : `claude mcp add koracut -- koracut-ctl mcp` (chemin complet de `koracut-ctl` hors du terminal de KoraCut).

Les deux parlent à la même porte ; ci-dessous les commandes sont écrites pour `koracut-ctl`, l'outil MCP se déduit.

## La règle qui passe avant tout

**Tu montes DANS KoraCut.** Chaque geste passe par les outils `koracut` (MCP) ou par `koracut-ctl`. Tu n'utilises jamais ffmpeg, melt, un script Python ou un autre logiciel pour fabriquer la vidéo, et tu n'écris jamais une vidéo toi-même dans un dossier. Le résultat est la timeline du projet ouvert ; l'export se fait par `export lancer`, quand la personne le demande. Si un outil manque ou échoue, tu t'arrêtes et tu le dis. Dans le terminal de KoraCut, `ffmpeg` et `melt` refusent de s'exécuter : c'est voulu, ne cherche pas un autre chemin. Si `koracut-ctl` répond `porte_fermee` alors que KoraCut est ouverte, tu es sans doute dans un bac à sable (Codex) qui coupe l'accès à l'application : passe par les outils MCP, qui tournent hors du bac à sable.

## Les conventions

- **Lis avant d'agir** : `timeline etat niveau=resume` (pistes, clips, ids, sélection, tête), `projet info` (chemin, dossier, format, type de vidéo, séquences), `medias lister`, `fichiers lister`. Relis `timeline etat` après une coupe, une suppression ou un Annuler : les identifiants de clips changent.
- **Les temps** sont en images (entier) ou en timecode `HH:MM:SS:II` (II = images, à la cadence du projet) ; ils sortent sous les deux formes `{ "i": 150, "tc": "00:00:06:00" }`. Un clip va de `pos` à `pos + dur` ; `out` est exclu.
- **Les ids** : `media_id` (chaîne) pour le chutier, `clip_id` et `piste_id` (entiers) pour la timeline. Ils viennent toujours d'une lecture, jamais d'une supposition.
- **Un travail = un tour** : `historique tour faire=debut titre='Organiser les rushs'` avant, `historique tour faire=fin` après. Tout ce que tu as fait entre les deux devient une seule étape « <toi> : <titre> » dans l'historique de KoraCut ; `faire=abandon` défait tout. Sans tour, chaque commande est une étape.
- **La confirmation** : `app quitter`, `fichiers corbeille`, `timeline outil` avec `supprimer_clip` ou `supprimer_piste`, `export lancer` avec `ecraser=true` répondent `confirmation_requise` avec un `demande_id`. La personne accepte ou refuse dans la Console de KoraCut. Explique-lui ce que tu veux faire, attends, puis rappelle la même commande avec `confirmation=<demande_id>` (ou écoute l'événement `confirmation`). Jamais en boucle. La personne qui lance KoraCut peut accorder la confiance d'avance, pour un travail sans personne devant l'écran : `KORACUT_PORTE_CONFIANCE=koracut-ctl,claude` dans l'environnement de KoraCut (débuts de noms de clients, « * » pour tous). C'est elle qui décide, pas toi : ne la pose jamais toi-même, et `app quitter` comme l'écrasement d'un fichier demandent toujours.
- **Les erreurs** ont un `code`, un `message` et souvent un `conseil` : lis le conseil, il dit quoi relire ou quoi changer. `hors_projet` : tu as visé un chemin hors du dossier du projet ; `id_inconnu` : relis l'état ; `hors_media` : la `sortie` d'`ajouter_clip` dépasse la fin du média (`duree_media` la donne) ; `autre_sequence`, `sequence_fermee` : la composition Motion vit dans une autre séquence, active-la ; `action_desactivee` : il manque une sélection ou un projet ; `projet_absent` : le projet n'est pas enregistré (pas de dossier) ; `sequence_introuvable` : relis `projet sequences`.
- **Une commande n'ouvre jamais de fenêtre.** Si KoraCut en ouvre une quand même (une action de menu, par exemple), la commande répond tout de suite `fenetre_ouverte` avec `fenetre` (titre, boutons, champs) : réponds par `ui fenetre faire='bouton:<libellé>'` (et `champ=… valeur=…` pour remplir), puis relis l'état ; la réponse de la commande d'origine ne revient pas. Ne rappelle pas la commande en boucle. Les cas connus sont déjà réglés sans fenêtre : un import d'un fichier déjà dans le chutier rend `existait: true` et son `media_id` ; `ajouter_clip` crée les pistes audio qui manquent et rend `pistes_creees`.
- **Les chemins** : absolus, `~`, ou relatifs au dossier du projet (`Rushs/A001.mp4`), partout (`medias importer`, `fichiers`, `projet ouvrir|enregistrer`, `export lancer`, `ui capture`). Un projet neuf (`projet nouveau`) n'a pas de dossier tant que `projet enregistrer` n'est pas passé : `projet info` rend alors `chemin: null` et `chemin_propose`, et un chemin relatif répond `projet_absent` : enregistre d'abord (`projet enregistrer chemin=~/Movies/Mon projet/Mon projet.koracut`).

## Les familles de méthodes

| Famille | Ce qu'elle fait | Exemples |
|---|---|---|
| `app` | qui répond | `app ping`, `app version` |
| `projet` | le projet et ses séquences | `projet info`, `projet nouveau plan='{"type_video": "F04-13"}'` (avec `enregistrer=true` ou `false` si le projet ouvert a des changements), `projet format largeur=1080 hauteur=1920` (ou `ips=25` sur un projet sans clip), `projet ouvrir chemin=…`, `projet enregistrer [chemin=…]`, `projet reglages clef=koracut:videotype`, `projet sequences` (uuid, nom, durée, pistes, courante), `projet sequence faire=creer nom='FR 9:16' pistes_video=2 pistes_audio=2`, `faire=renommer sequence='Séquence 1' nom='FR 16:9'`, `faire=activer sequence=…`, `faire=supprimer sequence=…` (à confirmation, pas annulable). Un projet a un seul profil : une séquence 9:16 dans un projet 16:9 reste en 16:9, la réponse le dit (`note`) ; pour un autre format, un autre projet |
| `fichiers` | le disque, dans le dossier du projet seulement | `fichiers lister dossier=Rushs`, `fichiers creer_dossier chemin=Exports`, `fichiers renommer chemin=Rushs/A001.mp4 nouveau_nom='01 - arrivée.mp4'`, `fichiers deplacer chemin=x.mp4 dossier=Rushs`, `fichiers corbeille chemin=…` (à confirmation) |
| `medias` | le chutier | `medias lister` (`dossier` est le chemin complet du dossier du chutier, « Script 01/01 Rushs » ; `dossier_nom` son dernier nom), `medias info media_id=3`, `medias importer chemins='["Rushs/A001.mp4"]' dossier=Rushs`, `medias renommer media_id=3 nom='01 - Interview Aminata'` (le nom du chutier, pas le fichier), `medias analyser media_id=3 quoi=vignette options='{"temps": "00:00:02:00"}'`, `quoi=transcription`, `quoi=silences` (les blancs à couper, `{ silences: [{ debut, fin, duree }], seuil_db }` ; options `seuil_db`, `duree_min_ms`), `quoi=niveaux` (`lufs_integre`, `crete_dbtp` : à comparer à la cible de la plateforme), `quoi=plans` (les changements de plan, option `seuil` 0.4) |
| `timeline` | la séquence | `timeline etat niveau=resume`, `timeline outils` (la liste et les schémas), `timeline outil nom=ajouter_clip args='{"media_id": "3", "piste_id": 2, "position": 0}'`, `nom=couper_clip`, `deplacer_clip`, `rogner_clip`, `supprimer_espace`, `changer_vitesse_clip`, `ajouter_fondu`, `ajouter_piste`, `regler_piste`, `supprimer_clip` ; depuis le lot A (24 septembre 2026) aussi `ajouter_titre`, `ajouter_transition`, `ajouter_effet`, `regler_effet`, `supprimer_effet`, `poser_keyframe`, `transformer_clip`, `ajouter_marqueur`, `importer_media`, `retirer_media` (à confirmation), `voir_image`, `transcrire_media`, `detecter_silences`, `regler_gain_clip`, `regler_piste_audio`, `mesurer_audio`, `normaliser_loudness`, `appliquer_ducking`, `ajouter_effet_audio`, et depuis le lot B (24 septembre 2026) l'étalonnage `mesurer_image`, `balance_blancs`, `regler_exposition`, `lift_gamma_gain`, `regler_courbes`, `appliquer_lut`, `copier_etalonnage`, `matcher_plans`, `activer_etalonnage`, `reinitialiser_etalonnage` (à confirmation) et l'export `lister_prereglages`, `verifier_avant_export`, `estimer_export`, `exporter` et `exporter_serie` (à confirmation, toujours), `etat_exports`, `annuler_export` (à confirmation), `exporter_image` (les schémas : `timeline outils`) ; `timeline selection clip_ids='[12]'`, `timeline tete position=150`, `timeline lecture faire=lire`, `timeline zoom ajuster=true` |
| `actions` | les 850 actions des menus, par leur nom | `actions lister filtre=zoom`, `actions declencher nom=koracut_auto_cut`, `actions declencher nom=show_track_names cocher=true` : ce qui n'a pas d'outil se fait par là |
| `ui` | l'écran | `ui module id=etalonnage`, `ui panneaux`, `ui panneau nom=console faire=montrer`, `ui fenetres` (les fenêtres ouvertes, leurs boutons et champs), `ui fenetre faire='bouton:Commencer le montage'`, `ui capture chemin=/tmp/v.png quoi=timeline` (pour vérifier, jamais pour cliquer), `ui message texte='…'` |
| `export` | le rendu | `export prereglages`, `export lancer prereglage='MP4-H264/AAC' chemin=Exports/final.mp4 niveau_lufs=-14` (`zone='{"in": 0, "out": 250}'` pour un extrait, `zone=zone` pour la zone du moniteur), `export etat tache_id=…` (le `tache_id` rendu par `export lancer`), `export annuler tache_id=…` |
| `historique` | annuler | `historique lister`, `historique annuler`, `historique refaire`, `historique tour faire=debut|fin|abandon titre=…` |
| `theme` | l'apparence de KoraCut, tout de suite | `theme lister`, `theme appliquer nom=clair` (ou `sombre`, `systeme`, `matrix`, `spider-man`, `precedent`), `theme creer nom='Kora nuit' couleurs='{"fond": "#0E0B1F", "accent": "#FF5A1F"}'` (contraste vérifié et corrigé, lis `corrections`), `theme modifier couleurs='{"accent": "#FF5A1F"}'`, `theme supprimer nom=…` (à confirmation) |
| `reglages` | les réglages sûrs de l'application | `reglages lister section=timeline`, `reglages lire cle=autosave_time`, `reglages ecrire cle=snaptopoints valeur=false` (rend `avant` pour revenir) ; compétence `koracut-reglages-et-themes` |
| `terminal` | le terminal de KoraCut, pour confier le montage à un agent qui y tourne | `terminal ouvrir` (lance Claude Code dans le projet, `agent=<id>` pour un autre agent), `terminal etat historique=60` (écran, `agent_travaille`, `question_ouverte`), `terminal ecrire --params '{"texte": "Retour : …"}'` (à confirmation), `terminal touche touche=echap` |
| `evenements` | recevoir sans demander | `evenements abonner types='["tete", "selection", "journal", "projet", "timeline", "taches", "fenetre"]' --suivre` |

`koracut-ctl --aide` donne chaque méthode avec ses paramètres, tels que KoraCut les annonce.

## Les gestes courants

**Le script du projet : `script_lire`, `script_ecrire` ; écris-le avant de monter** (document 21, panneau Script, ⌘⌥S). Tant que rien n'est écrit, `script_lire` rend le modèle du type de vidéo : un tableau `deroule`, une ligne par scène (`s1`, `s2`…), colonnes `scene`, `duree`, `voix`, `image`, `son`, `plans`, `fait`. Un appel = une étape d'annulation. **Chaque séquence a son script** : sans `sequence`, c'est celui de la séquence active ; `"sequence": 2` ou `"sequence": "Pub 2"` (rang, nom ou uuid) vise une autre séquence, et les marqueurs de `relier_ligne` vont dans cette séquence.

```sh
koracut-ctl timeline outil nom=script_lire args='{"format": "markdown"}'
koracut-ctl timeline outil nom=script_ecrire args='{"operations": [{"faire": "modifier_cellule", "bloc_id": "deroule", "ligne_id": "s1", "colonne": "voix", "valeur": "Le vrai yassa en 45 minutes."}, {"faire": "ajouter_ligne", "bloc_id": "deroule", "apres": "s2", "cellules": {"scene": "Marinade", "duree": "8 s"}}]}'
koracut-ctl timeline outil nom=script_ecrire args='{"operations": [{"faire": "relier_ligne", "bloc_id": "deroule", "ligne_id": "s1", "marqueur": "00:00:00:00", "poser_marqueur": true}, {"faire": "modifier_cellule", "bloc_id": "deroule", "ligne_id": "s1", "colonne": "fait", "valeur": true}]}'
```

`script_modele` pose le modèle d'un autre type (`type_video`, `remplacer: true`). Monte dans l'ordre du script, coche `fait` au fur et à mesure.

**Organiser des rushs** (la demande type : « organise-moi ce tournage »)

```sh
koracut-ctl projet info                                  # le dossier du projet, ses sous-dossiers
koracut-ctl historique tour faire=debut titre='Organiser les rushs'
koracut-ctl fichiers creer_dossier chemin=Rushs
koracut-ctl fichiers creer_dossier chemin=Exports
koracut-ctl fichiers lister dossier=. recursif=true       # les fichiers, avec media_id quand ils sont déjà dans le chutier
koracut-ctl medias importer chemins='["Rushs/A001.mp4", "Rushs/A002.mp4"]' dossier=Rushs
koracut-ctl medias analyser media_id=3 quoi=vignette      # regarde l'image (le chemin PNG rendu) pour comprendre le rush
koracut-ctl medias analyser media_id=3 quoi=transcription # ce qui se dit (Whisper, en local, si installé)
koracut-ctl fichiers renommer chemin=Rushs/A001.mp4 nouveau_nom='01 - Interview Aminata.mp4'   # le chutier suit
koracut-ctl historique tour faire=fin
```

Des noms qui disent le contenu, `NN - Description courte.ext`, numérotés dans l'ordre du script ou du tournage ; les dossiers `01 Rushs`, `02 Son`, `03 Musique`, `04 Graphismes`, `05 Exports`, `06 A trier`, les mêmes dans le chutier. Jamais de suppression : ce qui ne sert pas va dans `06 A trier`. Le détail, et les outils d'organisation (`organiser_rushs`, `ranger_media`, `renommer_media`, `renommer_fichier_media`, séquences, pistes) par `timeline outil` : les compétences `koracut-organiser-le-projet`, `koracut-organiser-nommage`, `koracut-organiser-arborescence`.

**Poser et couper**

```sh
koracut-ctl timeline etat niveau=resume                   # les ids des pistes (V1 est la plus basse des vidéo, A1 la plus haute des audio)
koracut-ctl timeline outil nom=ajouter_clip args='{"media_id": "3", "piste_id": 12, "position": 0}'
koracut-ctl timeline etat                                 # les ids des clips posés
koracut-ctl timeline outil nom=couper_clip args='{"clip_id": 21, "position": "00:00:04:00"}'
koracut-ctl timeline outil nom=supprimer_espace args='{"piste_id": 12, "position": 0}'
koracut-ctl timeline outil nom=ajouter_fondu args='{"clip_id": 21, "cote": "entree", "duree": 12}'     # cote : entree ou sortie
koracut-ctl timeline outil nom=supprimer_clip args='{"clip_ids": [21], "refermer": true}'           # une liste, toujours ; à confirmation
koracut-ctl timeline tete position=0 ; koracut-ctl timeline lecture faire=lire     # la personne voit le résultat
```

**Habiller et mixer** (lot A des outils d'agent, 24 septembre 2026 : mêmes réponses que pour les agents, une étape d'annulation par appel)

```sh
koracut-ctl timeline outil nom=ajouter_titre args='{"texte": "Saly, vue du ciel", "piste_id": 13, "position": 12, "duree": 63, "style": "ouverture"}'
koracut-ctl timeline outil nom=ajouter_transition args='{"clip_id": 21, "duree": 12}'          # fondu enchaîné avec le clip qui suit (pas_de_matiere si la réserve manque)
koracut-ctl timeline outil nom=transformer_clip args='{"clip_id": 21, "x_px": 0, "y_px": 0, "largeur_px": 1920, "hauteur_px": 1080}'
koracut-ctl timeline outil nom=poser_keyframe args='{"clip_id": 21, "effet_index": 0, "parametre": "rect", "position_dans_clip": 100, "valeur": "-96 -54 2112 1188 1", "interpolation": "courbe_douce"}'
koracut-ctl timeline outil nom=ajouter_marqueur args='{"position": 125, "commentaire": "Début interview", "categorie": 3}'
koracut-ctl timeline outil nom=detecter_silences args='{"clip_id": 31, "seuil_db": -40, "duree_mini": 20}'
koracut-ctl timeline outil nom=regler_gain_clip args='{"clip_id": 18, "gain_db": -6}'
koracut-ctl timeline outil nom=appliquer_ducking args='{"piste_musique_id": 4, "piste_voix_id": 3, "reduction_db": 9}'
koracut-ctl timeline outil nom=mesurer_audio args='{"cible": "mixage"}'                       # lufs_integre, crete_vraie_dbtp
koracut-ctl timeline outil nom=normaliser_loudness args='{"cible": "mixage", "lufs_cible": -14}'
koracut-ctl timeline etat niveau=complet                  # les effets avec leurs paramètres et leurs images clés
```

**Étalonner** (lot B des outils d'agent, 24 septembre 2026 : mesurer, corriger, remesurer ; une étape d'annulation par appel)

```sh
koracut-ctl timeline outil nom=mesurer_image args='{"clip_id": 21}'                          # luma, saturation, dominante r/v/b, peau (milieu du clip)
koracut-ctl timeline outil nom=balance_blancs args='{"clip_id": 21, "temperature_k": 7500}'  # plus haut refroidit, plus bas réchauffe (6500 neutre)
koracut-ctl timeline outil nom=regler_exposition args='{"clip_id": 21, "exposition_ev": 0.5, "contraste": 1.1}'
koracut-ctl timeline outil nom=lift_gamma_gain args='{"clip_id": 21, "ombres": {"r": 0, "v": 0, "b": 0.02}}'
koracut-ctl timeline outil nom=regler_courbes args='{"clip_id": 21, "canal": "luma", "points": [{"entree": 0, "sortie": 0}, {"entree": 0.25, "sortie": 0.21}, {"entree": 0.75, "sortie": 0.79}, {"entree": 1, "sortie": 1}]}'
koracut-ctl timeline outil nom=appliquer_lut args='{"clip_id": 21, "fichier": "TEAL_ORANGE.cube"}'   # un nom : cherché dans les LUT livrées, puis <projet>/luts
koracut-ctl timeline outil nom=copier_etalonnage args='{"clip_source_id": 21, "clip_cible_ids": [22, 23]}'
koracut-ctl timeline outil nom=matcher_plans args='{"clip_reference_id": 21, "clip_cible_id": 22}'  # exposition et balance des tons moyens sur la cible
koracut-ctl timeline outil nom=activer_etalonnage args='{"clip_id": 21, "actif": false}'   # l'avant ; remettre true ensuite
```

**Exporter** (les outils d'agent du lot B, ou les méthodes `export` de la porte ; `exporter` demande toujours l'accord dans la Console)

```sh
koracut-ctl timeline outil nom=lister_prereglages args='{"categorie": "KoraCut"}'          # les cinq préréglages KoraCut, au format du projet
koracut-ctl timeline outil nom=verifier_avant_export args='{"preset": "youtube_1080p", "chemin": "/Users/moi/Projet/Exports/final.mp4"}'
koracut-ctl timeline outil nom=estimer_export args='{"preset": "youtube_1080p"}'           # taille, et le temps mesuré sur un rendu d'essai de 2 s
koracut-ctl timeline outil nom=exporter args='{"preset": "youtube_1080p", "chemin": "/Users/moi/Projet/Exports/final.mp4", "niveau_lufs": -14}'
koracut-ctl timeline outil nom=etat_exports                                                   # en_attente, en_cours, termine, echec, annule
koracut-ctl timeline outil nom=exporter_image args='{"position": "00:00:12:00", "chemin": "/Users/moi/Projet/Exports/vignette.png"}'
```

**Animer, élément par élément** (document 19, lot 5) : un élément = une composition = un clip, posé exactement ; la personne ne change que lui. L'application d'abord (rien à rendre), HyperFrames quand il faut dessiner.

```sh
koracut-ctl timeline outil nom=motion_element_seul args='{"nom": "titre-reveil", "sorte": "titre", "style": "eclat", "texte": "Le réveil", "position": 12, "duree": 70}'   # sur la piste « Animations »
koracut-ctl timeline outil nom=motion_transition args='{"clip_avant": 21, "style": "cordes"}'   # à cheval sur la coupe, piste « Transitions », elle suit la coupe
koracut-ctl timeline outil nom=motion_element_seul args='{"nom": "legende", "sorte": "legende", "texte": "Sintel, au couchant", "position": 100, "duree": 75}'   # sous la transition
koracut-ctl timeline outil nom=motion_ouvrir args='{"nom": "legende"}'                              # comme un double-clic : lui seul s'ouvre
koracut-ctl timeline outil nom=animer_entree args='{"clip_id": 23, "style": "zoom_avant", "duree": 12}'   # sans HyperFrames : l'effet Transformer
koracut-ctl timeline outil nom=animer_texte args='{"clip_id": 27, "style": "machine_a_ecrire", "duree": 22}'
koracut-ctl timeline outil nom=transition_app args='{"clip_avant": 22, "style": "zoom", "duree": 12}'   # fondu_enchaine, volet, glissement, fondu_noir, zoom, pousse, secousse
```

Catalogue HyperFrames : titre (monte, machine, eclat), bandeau (glisse, texte « Nom | Fonction »), compteur (compte), logo (rebond, fichier), legende (trait), ou `brief_html` (ton HTML, un seul élément ; dans le script, `tl`, `D`, `W`, `H` existent). Transitions : cordes, balayage, secousse, voile, eclair. Lis `/hyperframes-core` et `/hyperframes-animation` avant d'écrire du HTML.

Les polices de la charte et les images se donnent par `fichiers` (`motion_element_seul`, `motion_creer`, `motion_ecrire`) : `"fichiers": ["Charte/Outfit.ttf", "~/Logos/logo.png"]` les copie dans `assets/` avant le rendu, et le HTML les cite par `url("assets/Outfit.ttf")` et `<img src="assets/logo.png">`. Jamais de police en base64 dans le HTML.

**Plusieurs séquences dans un projet** : chaque composition vit dans la séquence qui était devant à sa création, et n'en sort pas (`motion_lister` dit `sequence`, `sequence_nom`, `sequence_active` ; les `clip_id` et `pos` sont ceux de sa séquence). Tu peux changer de séquence (`projet sequence faire=activer`) pendant un rendu : le clip se pose quand même dans la sienne. Une création annulée (`historique annuler`) retire la composition de la liste et libère son nom ; `motion_lister` avec `retirees: true` la montre encore.

**Rendre pour lire sans saccade** (le bouton « Rendu » de la timeline ; un cache, le montage ne change pas) : après un étalonnage ou des effets lourds, `koracut-ctl timeline outil nom=rendre_apercu args='{"zone": "tout", "attendre": true}'` (ou `"zone": "selection"`, ou `{"in": 0, "out": 250}`), puis fais lire. `nom=etat_apercu` dit ce qui est rendu, ce qui reste, la taille du cache. Le rendu automatique (actif par défaut hors machine modeste) refait seul les passages touchés une seconde après chaque changement : pas besoin de le relancer.

**Exporter par la porte (sans accord si le fichier n'existe pas)**

```sh
koracut-ctl export prereglages --json | head -c 2000      # les noms exacts
koracut-ctl export lancer prereglage='MP4-H264/AAC' chemin=Exports/final.mp4 niveau_lufs=-14
koracut-ctl export etat                                   # avancement, fini, échec
```

Sans `niveau_lufs`, le son est rendu tel quel : le fichier contient ce que `mesurer_audio` (cible `mixage`) annonce, à un demi-point près. La cible du type de vidéo du projet (-23 LUFS pour une publicité télévisée, par exemple) n'est pas appliquée d'office : la réponse la rappelle (`cible_du_type`), à toi de la donner si c'est elle que tu veux.

Le niveau sonore selon la plateforme est dans la compétence `koracut-son` (-14 LUFS pour le web) ; les specs des plateformes dans `koracut-export-publication`.

**Une fenêtre s'est ouverte** (KoraCut pose une question) : la commande a répondu `fenetre_ouverte` avec la description, ou l'événement `fenetre` l'a dit ; `ui fenetres` la décrit aussi avec ses boutons ; `ui fenetre faire='bouton:Oui'` répond. Puis relis l'état.

**Analyser un rush avant de couper**

```sh
koracut-ctl medias analyser media_id=3 quoi=silences options='{"duree_min_ms": 700}'   # les blancs, prêts pour supprimer_espace ou couper_clip
koracut-ctl medias analyser media_id=3 quoi=niveaux                                    # -14 LUFS pour le web : sinon niveau_lufs à l'export
koracut-ctl medias analyser media_id=3 quoi=plans                                      # où la caméra a coupé
```

**Vérifier** : `ui capture chemin=/tmp/apres.png quoi=timeline` puis regarde l'image. C'est pour contrôler, pas pour piloter.

## Ce qu'il ne faut jamais faire

- **Retélécharger HyperFrames.** Il est déjà dans KoraCut, une seule fois pour tous les projets : la commande `hyperframes` dans le terminal de KoraCut, ou le chemin que rend `koracut-ctl app version` (`hyperframes.commande`). Jamais `npx hyperframes`, `npm install`, `npm init` ni `hyperframes init` : pas de `node_modules`, pas de `package.json`, pas de code source étranger dans le dossier du projet. Une composition, c'est `motion/<nom>/index.html` et ses fichiers. Le rendu passe par les outils `motion_*`.

- Supprimer, écraser ou déplacer un fichier hors du dossier du projet : la porte le refuse (`hors_projet`), et le disque de la personne n'est pas le tien. `fichiers corbeille` est le seul retrait possible, à confirmation.
- Agir sans avoir lu l'état, ou réutiliser un `clip_id` après une coupe ou un Annuler.
- Relancer une commande `confirmation_requise` en boucle, ou contourner la confirmation par une autre voie (`actions declencher` sur une action de suppression est aussi une suppression : demande d'abord).
- Quitter KoraCut (`app quitter`) sans que la personne l'ait demandé.
- Toucher aux réglages de l'application : la porte n'y donne pas accès, c'est voulu.

## Les autres compétences

`koracut-montage` (le métier du Monteur, type de vidéo par type), `koracut-tournage` (conseiller avant les rushs), `koracut-son` (mixage, niveaux, baisse automatique de la musique, coupe automatique), `koracut-export-publication` (préréglages, plateformes, titres, publication). Le type de vidéo du projet est dans `projet info` (`type_video`) : lis sa fiche avant de monter.

Et, depuis le 25 septembre 2026, les compétences des agents de l'application, une par geste de métier, produites depuis `Agents in-app/competences/` : la méthode (`koracut-methode-monter-une-video`, `koracut-montage-visible`, `koracut-quand-un-outil-manque`, `koracut-verifier-le-montage`), l'organisation (`koracut-organiser-*`), le script (`koracut-script-*`), le montage (`koracut-derushage`, `koracut-bout-a-bout`, `koracut-rythme-respiration`, `koracut-resserrer-parole`, `koracut-coupes-j-l`, `koracut-b-roll-plans-de-coupe`, `koracut-recadrer-vertical`, `koracut-interview-podcast-multicam`, `koracut-hook-teaser`, `koracut-genre-*`, `koracut-diaporama-photos`), l'habillage (`koracut-habillage-titres-bandeaux`, `koracut-animer`, `koracut-sous-titres`), la couleur (`koracut-etalonnage-*`), le son (`koracut-son-*`, `koracut-bruitages-ambiances`) et l'export (`koracut-export-formats-plateformes`). Pour « monte-moi cette vidéo », commence par `koracut-methode-monter-une-video`.
