---
name: koracut-son-niveaux-diffusion
description: "À lire en dernier, quand l'équilibre voix, musique et bruitages est fait : amener le mixage au niveau de la plateforme, crête sous -1 dBTP."
---

# Le son, étape 3 : le niveau de diffusion

> Compétence KoraCut, la même que celle des agents de l'application (`Agents in-app/competences/son-niveaux-diffusion.md`), produite par `Sources/koracut-skills/generer.py`. Lis d'abord `koracut-piloter` : lire l'état, un travail = un tour (`historique tour faire=debut titre=…` puis `faire=fin`), la confirmation dans la Console.

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

À lire en dernier, quand l'équilibre voix, musique et bruitages est fait : amener le mixage au niveau de la plateforme, crête sous -1 dBTP.

## Les cibles

| Destination | Niveau intégré | Crête vraie |
|---|---|---|
| YouTube, Instagram, TikTok, Facebook, WhatsApp | -14 LUFS | -1 dBTP |
| Podcast, voix au casque | -16 LUFS | -1 dBTP |
| Télévision (EBU R128) | -23 LUFS, à 0,5 près | -1 dBTP |
| Cinéma | la fiche technique de la salle, jamais devinée | |

Destination inconnue : -14 LUFS, annoncé comme hypothèse. Web et télévision : deux exports, deux niveaux.

## Les étapes

1. `mesurer_audio` (cible mixage) : niveau et crête de départ.
2. Un limiteur sur le master à -1 dB (`ajouter_effet_audio`, `piste_id: -1`, traitement `limiteur`, `seuil_db: -1`), pour les accidents seulement.
3. `normaliser_loudness` (cible mixage, `lufs_cible` de la plateforme).
4. `mesurer_audio` : niveau à 0,5 LU près, crête sous -1 dBTP. Sinon, baisse la piste qui déborde, pas le limiteur.
5. À l'export, `niveau_lufs` dans `exporter` fait le même travail pendant le rendu : l'un ou l'autre, pas les deux sans mesurer.

## Critères de qualité

- Niveau dans la cible, crête sous -1 dBTP.
- Une dynamique de 6 à 10 LU pour le web et le téléphone.

## Erreurs à éviter

- Normaliser avant l'équilibre.
- Pousser au-dessus de -14 LUFS : les plateformes baissent d'elles-mêmes, la dynamique est perdue.
- Un limiteur qui travaille en permanence : le mixage est trop fort en amont.

## Outils

`mesurer_audio`, `ajouter_effet_audio`, `normaliser_loudness`, `regler_piste_audio`, `exporter` (`niveau_lufs`).
