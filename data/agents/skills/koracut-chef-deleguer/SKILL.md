---
name: koracut-chef-deleguer
description: "À lire par le Chef avant chaque `deleguer` : écrire une consigne qu'un agent peut exécuter sans avoir vu la conversation, et vérifier ce qu'il rend."
---

# Déléguer une étape (pour le Chef)

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/chef-deleguer.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire par le Chef avant chaque `deleguer` : écrire une consigne qu'un agent peut exécuter sans avoir vu la conversation, et vérifier ce qu'il rend.

## Une consigne complète contient

1. **Le but** en une phrase : « Ranger et renommer les 14 rushs du chutier. »
2. **Les identifiants lus** : media_id, piste_id, clip_id, séquence, lignes du script (`s1`, `s2`). Jamais « les plans de drone » sans leurs id.
3. **Les valeurs** : durées en images et en timecode, niveaux en dB ou LUFS, noms exacts à écrire (titres, bandeaux, noms de fichiers).
4. **Les hypothèses acceptées** par la personne et `plan_accepte: true` si le plan est validé.
5. **Les limites** : ce qu'il ne doit pas toucher (piste verrouillée, fichiers du disque si le plan ne l'a pas prévu).
6. **La compétence** à suivre, par son nom : « suis `koracut-organiser-nommage` : ordre du tournage ».
7. **Le budget** en tours (`budget_tours`).

Exemple : « Renomme et range les 14 rushs (media_id 3 à 16) selon organiser-nommage et organiser-arborescence : ordre du tournage, dossiers 01 Rushs et 02 Son, fichiers renommés sur le disque (accepté). Inventaire dans le script, bloc inventaire. Plan accepté. Budget 25 tours. »

## Qui reçoit quoi dans un montage complet

| Étape | Agent | Compétences à citer |
|---|---|---|
| Organiser, renommer | `monteur` | `koracut-organiser-le-projet` et ses sous-compétences |
| Script | toi, ou `monteur` s'il faut transcrire | `koracut-script-trouver`, `koracut-script-ecrire` |
| Dérushage, montage | `monteur` | `koracut-derushage`, `koracut-bout-a-bout`, `koracut-rythme-respiration`, fiche du genre |
| Sous-titres, titres simples | `monteur` | `koracut-sous-titres`, `koracut-habillage-titres-bandeaux` |
| Bandeaux animés, logos, transitions dessinées | `motion_designer` | `koracut-animer` |
| Voix, musique, niveaux | `mixeur` | `koracut-son-voix-nettoyage`, `koracut-son-musique-ducking`, `koracut-son-niveaux-diffusion` |
| Bruitages, ambiances | `sound_designer` | `koracut-bruitages-ambiances` |
| Couleur | `coloriste` | `koracut-etalonnage-correction`, `koracut-etalonnage-raccord-peau`, `koracut-etalonnage-look-lut` |
| Photos, diaporama | `retoucheur` | `koracut-diaporama-photos` |
| Export | toi | `koracut-export-formats-plateformes`, `koracut-verifier-le-montage` |

## Après chaque délégation

1. Relis l'état (`etat_timeline`, `lister_medias`, `script_lire` selon l'étape) et compare avec ce que l'agent annonce.
2. Un écart : corrige la consigne, relance une fois. Deux échecs : arrête et dis où ça bloque.
3. Reprends dans ton résumé les lignes « Outil qui manque » des comptes rendus.

## Erreurs à éviter

- « Fais le montage » sans id ni durées.
- Déléguer deux étapes qui se marchent dessus en même temps (couleur pendant que le montage bouge).
- Recopier le compte rendu d'un agent au lieu de le résumer.
