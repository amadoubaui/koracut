# Logos des fournisseurs de modèles (page « Connexions IA »)

Écrit le 25 septembre 2026. Ces fichiers servent à la page « Connexions IA » des Préférences
(`src/koracut/ai/`), avec le catalogue `data/koracut-fournisseurs-ia.json`.

## D'où ils viennent

Tous les SVG viennent de **LobeHub Icons** (paquet npm `@lobehub/icons-static-svg`, version 1.95.1,
dépôt https://github.com/lobehub/lobe-icons), sous **licence MIT**, Copyright (c) 2023 LobeHub. Le
texte complet de la licence est dans `LICENSE-lobehub-icons.txt`, à garder à côté des fichiers.

Seule retouche : `width="1em"` et `height="1em"` remplacés par 24, l'attribut `style` retiré (le
moteur SVG de Qt ne comprend pas l'unité `em`). Les tracés ne sont pas touchés.

Les noms et logos restent des **marques de leurs propriétaires** (Anthropic, OpenAI, Google, etc.).
On les montre seulement pour désigner le service que la personne branche, comme le font les autres
logiciels qui permettent de choisir son fournisseur. Pas de logo dans la communication de KoraCut.

## La règle

- Un fournisseur sans logo sûr (Scaleway, OVHcloud, « Autre service compatible OpenAI ») reçoit une
  **pastille ronde avec son initiale**, dessinée par le code. Jamais un logo inventé ou redessiné.
- Les logos d'une seule couleur utilisent `fill="currentColor"` : le code remplace ce mot par la
  couleur du texte du thème au moment de dessiner, pour qu'ils restent lisibles en sombre et en clair.
  Les variantes `-color` gardent les couleurs de la marque.
- Même façon de faire pour les logos des agents en ligne de commande (`data/icons/agents-cli/`).

## Ajouter un fournisseur

1. Chercher le logo dans LobeHub Icons (`npm pack @lobehub/icons-static-svg`, dossier `icons/`) :
   préférer `<nom>-color.svg`, sinon `<nom>.svg`. Appliquer la même retouche (1em vers 24).
2. Le déposer ici et l'ajouter à `fournisseurs.qrc`.
3. Ajouter l'entrée dans `data/koracut-fournisseurs-ia.json` (champ `logo`). Sans logo : `"logo": ""`.

Un catalogue modifié peut aussi se poser sans recompiler : `koracut-fournisseurs-ia.json` dans le
dossier de données de KoraCut passe devant celui de l'application (les logos, eux, sont embarqués).

## Champs du catalogue

`id`, `nom`, `logo`, `description`, `url_doc` (la doc sur les clés), `url_inscription`,
`url_cle` (la page de la console qui crée la clé ; pour un modèle local, la page de téléchargement),
`url_base`, `url_base_modifiable`, `url_base_aide` (un gabarit entre accolades, comme `{ressource}`,
oblige la personne à le remplacer), `format` (`anthropic`, `openai` pour tout ce qui parle
chat/completions, `gemini`), `auth` (`bearer`, `x-api-key`, `api-key`, `x-goog-api-key`, `aucune`),
`chemin_modeles` (vide : pas de liste, les modèles conseillés servent), `chemin_verification`
(une adresse qui vérifie la clé quand la liste des modèles est publique, ou `chat` pour un appel
minimal d'un jeton), `cle_requise`, `local`, `gratuit`, `gratuit_note`, `modeles_conseilles`,
`note`, `entetes`.

## Laissés de côté (25 septembre 2026)

- **Perplexity** : son API ne prend plus les outils qu'au format « Responses », pas chat/completions.
- **GitHub Models** : service fermé le 30 juillet 2026.
