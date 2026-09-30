# Logos des agents en ligne de commande

Ces fichiers servent à la fenêtre de KoraCut qui propose de choisir un agent de code (liste dans `data/koracut-agents-cli.json`). Aucun n'a été dessiné ni retouché : ce sont les fichiers publiés, copiés tels quels le 25 septembre 2026.

Ces logos sont des marques de leurs propriétaires. KoraCut les utilise seulement pour désigner l'outil correspondant, sans lien ni partenariat avec ces sociétés.

| Fichier | Outil | Source exacte | Licence du fichier |
|---|---|---|---|
| `claude-code.svg` | Claude Code | `@lobehub/icons-static-svg` 1.95.1, `icons/claudecode-color.svg` | MIT (LobeHub) |
| `codex.svg` | Codex CLI | `@lobehub/icons-static-svg` 1.95.1, `icons/codex-color.svg` | MIT (LobeHub) |
| `gemini-cli.svg` | Gemini CLI | `@lobehub/icons-static-svg` 1.95.1, `icons/geminicli-color.svg` | MIT (LobeHub) |
| `opencode.svg` | OpenCode | `simple-icons` 16.32.0, `icons/opencode.svg` | CC0-1.0 (Simple Icons) |
| `qwen-code.svg` | Qwen Code | `@lobehub/icons-static-svg` 1.95.1, `icons/qwen-color.svg` | MIT (LobeHub) |
| `copilot-cli.svg` | GitHub Copilot CLI | `simple-icons` 16.32.0, `icons/githubcopilot.svg` (tracé tiré des Primer Octicons) | CC0-1.0 pour Simple Icons, MIT pour le tracé d'origine |
| `cursor-cli.svg` | Cursor CLI | `simple-icons` 16.32.0, `icons/cursor.svg` | CC0-1.0 (Simple Icons) |
| `amp.svg` | Amp | `@lobehub/icons-static-svg` 1.95.1, `icons/amp-color.svg` | MIT (LobeHub) |
| `goose.svg` | goose | `@lobehub/icons-static-svg` 1.95.1, `icons/goose.svg` | MIT (LobeHub) |
| `kilo-code.svg` | Kilo Code CLI | `@lobehub/icons-static-svg` 1.95.1, `icons/kilocode.svg` | MIT (LobeHub) |
| `mistral-vibe.svg` | Mistral Vibe | `@lobehub/icons-static-svg` 1.95.1, `icons/mistral-color.svg` (logo de Mistral AI, Vibe n'a pas de symbole à part dans ces paquets) | MIT (LobeHub) |
| `kimi-code.svg` | Kimi Code CLI | `@lobehub/icons-static-svg` 1.95.1, `icons/kimi-color.svg` | MIT (LobeHub) |

Paquets npm : https://www.npmjs.com/package/simple-icons et https://www.npmjs.com/package/@lobehub/icons-static-svg. La licence MIT de LobeHub est déclarée dans le `package.json` du paquet (`"license": "MIT"`), le paquet ne contient pas de fichier LICENSE séparé ; le dépôt est https://github.com/lobehub/lobe-icons.

Monochromes (`"logo_mono": true` dans le JSON, à teindre en blanc sur fond sombre) : `opencode.svg`, `copilot-cli.svg`, `cursor-cli.svg` (noir par défaut, sans attribut de couleur) et `goose.svg`, `kilo-code.svg` (`currentColor`).

Sans logo (`"logo": null`, l'application affiche une pastille avec l'initiale) :
- **Crush** : aucun SVG dans simple-icons, LobeHub ni le dépôt charmbracelet/crush.
- **Aider** : le seul logo officiel est un logo texte de 200 × 60 avec une police embarquée (`aider/website/assets/logo.svg`), illisible en pastille. Non repris.
- **Droid (Factory)** : le dépôt Factory-AI/factory n'a pas de licence, son `docs/favicon.svg` n'est donc pas réutilisable.

Tous les fichiers passent `xmllint --noout`.
