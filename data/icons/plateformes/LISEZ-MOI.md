# Logos des plateformes (module Export)

Écrit le 28 septembre 2026. Ces fichiers servent aux tuiles des modèles d'export (module Export,
`src/koracut/export/`), avec le catalogue `data/koracut-export-models.json` (champ `logo`).

## D'où ils viennent

Tous les SVG viennent de **Simple Icons** (https://simpleicons.org, dépôt
https://github.com/simple-icons/simple-icons), sous licence **CC0 1.0** (domaine public, texte dans
`LICENSE-simple-icons.md`). Paquet npm `simple-icons` version 16.33.0, sauf `linkedin.svg`, pris dans la
version 10.4.0 (LinkedIn a demandé son retrait des versions suivantes ; la version 10.4.0 reste sous CC0).

Seule retouche : `width="24" height="24" fill="currentColor"` ajoutés à la balise `<svg>`. Les tracés ne
sont pas touchés. Le code remplace `currentColor` par la couleur du logo au moment de dessiner (champs
`logo_couleur` et `logo_fond` du catalogue, couleurs officielles relevées dans `simple-icons.json`) :
YouTube FF0000, TikTok 000000, Instagram FF0069, Facebook 0866FF, X 000000, LinkedIn 0A66C2,
Snapchat FFFC00, Vimeo 1AB7EA, WhatsApp 25D366, Twitch 9146FF.

Les noms et logos restent des **marques de leurs propriétaires** (Google, ByteDance, Meta, X Corp.,
Microsoft, Snap, Vimeo, Amazon). On les montre seulement pour désigner la plateforme visée par
l'export, comme le fait DaVinci Resolve dans sa liste de préréglages. Pas de logo dans la
communication de KoraCut, et aucun logo n'est redessiné ni inventé.

## Ajouter une plateforme

1. `npm pack simple-icons` (avec un cache npm à part, voir `CLAUDE.md`), prendre `icons/<slug>.svg`.
2. Appliquer la même retouche, déposer ici, ajouter à `plateformes.qrc`.
3. Ajouter le modèle dans `data/koracut-export-models.json` avec `logo`, `logo_couleur` et `logo_fond`.
