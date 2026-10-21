<p align="center">
  <img src="data/pics/koracut-logo.png" alt="KoraCut" width="420">
</p>

<p align="center">
  <b>Le logiciel de montage libre que ton agent IA sait piloter.</b><br>
  Monte, étalonne, mixe et anime toi-même, ou demande à Claude Code, Codex, Gemini ou au modèle de ton choix de le faire avec toi, dans la timeline, à un Annuler près.
</p>

<p align="center">
  <a href="https://github.com/amadoubaui/koracut/releases/latest"><img src="https://img.shields.io/github/v/release/amadoubaui/koracut?label=t%C3%A9l%C3%A9charger&color=0D99FF" alt="Dernière version"></a>
  <a href="COPYING"><img src="https://img.shields.io/badge/licence-GPL--3.0-blue" alt="GPL-3.0"></a>
  <img src="https://img.shields.io/badge/macOS-puce%20Apple-lightgrey" alt="macOS puce Apple">
  <img src="https://img.shields.io/badge/Windows%20et%20Linux-bient%C3%B4t-lightgrey" alt="Windows et Linux bientôt">
  <img src="https://img.shields.io/badge/MCP-serveur%20int%C3%A9gr%C3%A9-7C5CFF" alt="Serveur MCP intégré">
  <a href="https://github.com/amadoubaui/koracut/stargazers"><img src="https://img.shields.io/github/stars/amadoubaui/koracut?style=social" alt="Étoiles"></a>
</p>

<p align="center">
  <a href="https://koracut.org"><b>Site</b></a> ·
  <a href="https://koracut.org/telecharger"><b>Télécharger pour macOS</b></a> ·
  <a href="#monter-avec-ton-agent-ia">Agents IA</a> ·
  <a href="#compiler">Compiler</a> ·
  <a href="CONTRIBUTING.md">Contribuer</a> ·
  <a href="README.md">English</a>
</p>

![KoraCut, le module Montage sur un vrai projet : quatre séquences, voix, musique, pistes de motion design](.github/readme/koracut-editing.jpg)

## Pourquoi KoraCut

La plupart des logiciels collent un bouton IA sur une application fermée. KoraCut fait l'inverse : c'est un logiciel de montage complet (bâti sur les fondations solides de Kdenlive et de MLT) où **chaque geste est aussi un outil qu'un agent peut appeler**. Tu gardes ton compte, ton modèle et tes fichiers. Tes rushs ne quittent jamais ton ordinateur.

- **Une fenêtre, sept modules** : Montage, Étalonnage, Audio, VFX, Photo, Illustration, Export, et le motion design dans la timeline.
- **Des agents qui montent pour de vrai** : 126 outils (couper, rogner, poser, étalonner, mixer, sous-titrer, animer, exporter), chaque appel est une étape de l'historique. Tu regardes l'agent travailler dans la timeline.
- **Ton agent à toi** : 15 agents en ligne de commande sont pris en charge, de Claude Code, Codex CLI et Gemini CLI à OpenCode, Aider et goose. KoraCut écrit leurs consignes, les branche et les lance dans une fenêtre qui reste ouverte même si l'application se ferme.
- **Serveur MCP inclus** : `koracut-ctl mcp` ouvre KoraCut à tout client du Model Context Protocol, sans Node.js.
- **Pensé d'abord pour les machines modestes** : copies légères, aperçu adaptatif, un profil de machine choisi au premier lancement, pour monter sans à-coups sur 4 cœurs et 8 Go.

## Ce que tu peux faire aujourd'hui

| Module | Ce qu'il fait |
|---|---|
| **Montage** | Timeline à plusieurs séquences, séquences côte à côte, raccourcis à la DaVinci, coupe automatique des silences et des mots parasites, panneau Script relié à la timeline, sous-titres |
| **Étalonnage** | Étalonnage par nœuds (roues, courbes, qualificateur, fenêtres, LUT, conversion d'espace couleur), le même grade à l'aperçu et à l'export |
| **Audio** | Mixeur, normalisation du niveau aux cibles des plateformes, nettoyage de la voix (RNNoise), baisse automatique de la musique, effets de voix, voix off par IA |
| **Motion design** | Compositions HTML + GSAP ([HyperFrames](https://github.com/heygen-com/hyperframes)) rendues directement dans la timeline, titres et transitions animés |
| **VFX** | Stabilisation, détourage du sujet pour passer un élément derrière une personne (macOS), flou de ce qui est privé qui suit un téléphone ou un nom |
| **Export** | Une page de livraison avec 25 modèles (YouTube, Shorts, TikTok, Reels Instagram, LinkedIn, statut WhatsApp, masters ProRes…) et une file de rendu |
| **Photo, Illustration** | En cours |

## Monter avec ton agent IA

1. Ouvre ou enregistre ton projet, puis choisis **Monter avec un agent**.
2. Choisis ton agent (Claude Code par défaut). KoraCut écrit ses consignes dans le dossier du projet, le branche sur l'application ouverte et l'ouvre dans sa propre fenêtre de terminal.
3. Demande avec tes mots : *« Coupe les silences de l'interview, mets la musique dessous à -18 LUFS, et ajoute un titre au début. »*
4. Chaque changement arrive dans la timeline. Un seul Annuler reprend tout un tour de l'agent.

Depuis n'importe quel terminal, les mêmes portes sont ouvertes :

```bash
koracut-ctl timeline etat niveau=resume          # lire la timeline
koracut-ctl timeline outil nom=couper_clip args='{"clip_id": 21, "position": "00:00:04:00"}'
koracut-ctl mcp                                   # serveur MCP en stdio
```

Les agents ne touchent jamais à tes médias avec ffmpeg ou un script dans ton dos : ils montent dans KoraCut, par ses outils, ou ils s'arrêtent et disent ce qui manque.

## Télécharger

- **macOS** (puce Apple) : [koracut.org/telecharger](https://koracut.org/telecharger), ou en une ligne : `curl -fsSL https://koracut.org/telechargements/installer-mac.sh | bash`. Le paquet n'est pas encore notarié par Apple ; la page de téléchargement explique comment l'ouvrir.
- **Windows et Linux** : Windows se compile et passe les tests dans l'intégration continue, Linux se compile et les passe dans un conteneur ; les paquets arrivent. Laisse ton adresse sur le site pour être prévenu.

Chaque version est livrée avec l'archive exacte de son code source, comme la GPL le demande. Voir les [versions](https://github.com/amadoubaui/koracut/releases).

## Compiler

Qt 6.10 ou plus récent, KDE Frameworks 6.21 ou plus récent, MLT 7.38 ou plus récent, KDDockWidgets 2.4 et CMake. Le détail par système : [dev-docs/build.md](dev-docs/build.md).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Sur macOS, la chaîne du studio passe par KDE Craft. Une version sans agents : `-DKORACUT_AGENTS=OFF`.

Empaqueter : Flatpak (`.flatpak-manifest.json`, [notes](packaging/flatpak/README.md)), Snap (`snapcraft.yaml`), Windows avec MSYS2 (`packaging/koracut-msys2.sh`), Linux avec toutes les dépendances (`packaging/koracut-lnx-withalldeps.sh`).

## Contribuer

Les signalements de défauts, les idées et les demandes de fusion sont les bienvenus : [CONTRIBUTING.md](CONTRIBUTING.md). Si KoraCut te rend service, une étoile sur ce dépôt aide d'autres monteurs à le trouver.

## Licence et origine

GPL v3. Le texte complet est dans `COPYING`, les licences des composants dans `LICENSES/`. Copyright © 2026 PIXEWORK, Dakar.

KoraCut est un dérivé d'un logiciel libre de montage vidéo publié sous GPL v3 : https://invent.kde.org/multimedia/kdenlive. Le code d'origine, ses auteurs et sa licence sont conservés. Les détenteurs de droits d'auteur sont listés dans `AUTHORS` et dans les en-têtes SPDX des fichiers source.
