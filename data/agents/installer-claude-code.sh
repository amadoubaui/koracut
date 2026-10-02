#!/bin/sh
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installe Claude Code par la voie officielle d'Anthropic, après accord de la personne.
# Lancé par KoraCut dans son terminal quand « claude » est absent (document 18, 1er octobre 2026).
# Voie vérifiée le 1er octobre 2026 sur https://code.claude.com/docs/en/setup :
#   installeur natif (recommandé) : curl -fsSL https://claude.ai/install.sh | bash
#   sinon, avec npm :               npm install -g @anthropic-ai/claude-code
# Le script ne lit, n'écrit et ne transmet aucun identifiant. La connexion au compte est faite par
# Claude Code lui-même, dans le navigateur, à son premier lancement.
#
# Codes de retour : 0 installé (ou déjà là), 1 échec, 2 refusé, 3 rien pour installer.
# KORACUT_LANGUE=fr|en choisit la langue ; KORACUT_CLAUDE_ABSENT=1 fait comme si claude manquait ;
# KORACUT_INSTALLER_SIMULER=1 montre la commande sans la lancer (essais).

NATIF='curl -fsSL https://claude.ai/install.sh | bash'
NPM='npm install -g @anthropic-ai/claude-code'

case "${KORACUT_LANGUE:-${LC_ALL:-${LANG:-fr}}}" in
  fr*) L=fr ;;
  *) L=en ;;
esac

dire() { # dire <style> <français> <anglais>
  if [ "$L" = fr ]; then t=$2; else t=$3; fi
  printf '\033[%sm%s\033[0m\n' "$1" "$t"
}

PATH="$HOME/.local/bin:$PATH"
export PATH

if [ "${KORACUT_CLAUDE_ABSENT:-0}" != 1 ] && command -v claude >/dev/null 2>&1; then
  dire 32 "Claude Code est déjà installé : $(command -v claude)" "Claude Code is already installed: $(command -v claude)"
  exit 0
fi

if command -v curl >/dev/null 2>&1 && command -v bash >/dev/null 2>&1; then
  COMMANDE=$NATIF
  OU_FR="Il sera posé dans ~/.local/bin/claude et se mettra à jour seul."
  OU_EN="It goes to ~/.local/bin/claude and updates itself."
elif command -v npm >/dev/null 2>&1; then
  COMMANDE=$NPM
  OU_FR="curl est absent : installation par npm (Node.js est déjà là)."
  OU_EN="curl is missing: installing with npm (Node.js is already there)."
else
  dire 31 "Ni curl ni npm sur cet ordinateur : installe Claude Code depuis https://code.claude.com/docs/en/setup puis reviens." \
          "Neither curl nor npm on this computer: install Claude Code from https://code.claude.com/docs/en/setup then come back."
  exit 3
fi

dire 1 "Claude Code n'est pas encore installé sur cet ordinateur." "Claude Code is not installed on this computer yet."
dire 0 "KoraCut peut l'installer avec la commande officielle d'Anthropic :" "KoraCut can install it with Anthropic's official command:"
printf '\n    \033[36m%s\033[0m\n\n' "$COMMANDE"
dire 90 "$OU_FR" "$OU_EN"
dire 90 "Cela télécharge le programme depuis claude.ai. Rien d'autre n'est modifié, KoraCut ne voit aucun identifiant." \
        "This downloads the program from claude.ai. Nothing else is changed, KoraCut sees no credential."
dire 90 "Il faut un abonnement Claude (Pro, Max, Team, Enterprise) ou un compte Console." \
        "A Claude subscription (Pro, Max, Team, Enterprise) or a Console account is needed."
if [ "$L" = fr ]; then
  printf '\nInstaller maintenant ? [o/N] '
else
  printf '\nInstall now? [y/N] '
fi
REPONSE=
read -r REPONSE || REPONSE=
case "$REPONSE" in
  o|O|oui|Oui|OUI|y|Y|yes|Yes|YES) ;;
  *)
    dire 33 "Installation annulée. Rien n'a été téléchargé ni installé." "Installation cancelled. Nothing was downloaded or installed."
    exit 2
    ;;
esac

if [ "${KORACUT_INSTALLER_SIMULER:-0}" = 1 ]; then
  dire 33 "Essai : la commande n'est pas lancée." "Trial: the command is not run."
  exit 0
fi

dire 1 "Installation en cours…" "Installing…"
if ! sh -c "$COMMANDE"; then
  dire 31 "L'installation a échoué. Lis les messages au-dessus, puis réessaie." "The installation failed. Read the messages above, then try again."
  exit 1
fi
hash -r 2>/dev/null
if ! command -v claude >/dev/null 2>&1; then
  dire 31 "L'installation s'est terminée mais la commande claude reste introuvable." "The installation ended but the claude command is still not found."
  exit 1
fi
dire 32 "Claude Code est installé : $(claude --version 2>/dev/null)" "Claude Code is installed: $(claude --version 2>/dev/null)"
dire 0 "La première fois, Claude Code ouvre ton navigateur pour te connecter à ton compte Claude. C'est toi qui te connectes, KoraCut ne garde rien." \
       "The first time, Claude Code opens your browser to sign in to your Claude account. You sign in yourself, KoraCut keeps nothing."
exit 0
