#!/bin/sh
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installe Node.js dans le dossier personnel, après accord de la personne. Lancé par KoraCut dans son
# terminal quand un agent en a besoin (Gemini CLI s'installe par npm) et que Node.js manque
# (document 18, 4 octobre 2026). Voie : l'archive officielle de nodejs.org (branche LTS), contrôlée
# par sa somme SHA-256, dépliée dans ~/.local/share/koracut/node, commandes liées dans ~/.local/bin.
# Aucun mot de passe, rien hors du dossier personnel ; pour le retirer, supprimer ce dossier.
#
# Codes de retour : 0 installé (ou déjà là), 1 échec, 2 refusé, 3 rien pour installer.
# KORACUT_LANGUE=fr|en choisit la langue ; KORACUT_INSTALLER_SIMULER=1 montre ce qui serait fait sans le faire.

BRANCHE=latest-v24.x
BASE="https://nodejs.org/dist/$BRANCHE"
DEST="$HOME/.local/share/koracut/node"
LIENS="$HOME/.local/bin"

case "${KORACUT_LANGUE:-${LC_ALL:-${LANG:-fr}}}" in
  fr*) L=fr ;;
  *) L=en ;;
esac

dire() { # dire <style> <français> <anglais>
  if [ "$L" = fr ]; then t=$2; else t=$3; fi
  printf '\033[%sm%s\033[0m\n' "$1" "$t"
}

PATH="$DEST/bin:$LIENS:$PATH"
export PATH

if [ "${KORACUT_NODE_ABSENT:-0}" != 1 ] && command -v node >/dev/null 2>&1 && command -v npm >/dev/null 2>&1; then
  dire 32 "Node.js est déjà installé : $(command -v node) ($(node --version 2>/dev/null))" "Node.js is already installed: $(command -v node) ($(node --version 2>/dev/null))"
  exit 0
fi

case "$(uname -s)" in
  Darwin) OS=darwin ;;
  Linux) OS=linux ;;
  *) OS= ;;
esac
case "$(uname -m)" in
  arm64|aarch64) ARCH=arm64 ;;
  x86_64|amd64) ARCH=x64 ;;
  *) ARCH= ;;
esac
if [ -z "$OS" ] || [ -z "$ARCH" ] || ! command -v curl >/dev/null 2>&1 || ! command -v tar >/dev/null 2>&1; then
  dire 31 "Impossible d'installer Node.js tout seul sur cet ordinateur : prends-le sur https://nodejs.org/fr/download puis reviens." \
          "Node.js cannot be installed automatically on this computer: get it from https://nodejs.org/en/download then come back."
  exit 3
fi

dire 1 "Cet agent a besoin de Node.js, qui n'est pas encore sur cet ordinateur." "This agent needs Node.js, which is not on this computer yet."
dire 0 "KoraCut peut l'installer depuis le site officiel :" "KoraCut can install it from the official site:"
printf '\n    \033[36m%s\033[0m\n\n' "$BASE/ (node-…-$OS-$ARCH.tar.gz)"
dire 90 "Il sera posé dans ~/.local/share/koracut/node (environ 190 Mo), avec les commandes node, npm et npx dans ~/.local/bin." \
        "It goes to ~/.local/share/koracut/node (about 190 MB), with the node, npm and npx commands in ~/.local/bin."
dire 90 "Aucun mot de passe, rien n'est changé hors de ton dossier personnel. Pour le retirer : supprime ce dossier." \
        "No password, nothing is changed outside your home folder. To remove it: delete that folder."
if [ "$L" = fr ]; then
  printf '\nInstaller Node.js maintenant ? [o/N] '
else
  printf '\nInstall Node.js now? [y/N] '
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
  dire 33 "Essai : rien n'est téléchargé." "Trial: nothing is downloaded."
  exit 0
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/koracut-node.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT
dire 1 "Téléchargement de Node.js…" "Downloading Node.js…"
if ! curl -fsSL "$BASE/SHASUMS256.txt" -o "$TMP/sommes.txt"; then
  dire 31 "nodejs.org ne répond pas. Vérifie la connexion Internet, puis réessaie." "nodejs.org does not answer. Check the Internet connection, then try again."
  exit 1
fi
ARCHIVE=$(awk '{print $2}' "$TMP/sommes.txt" | grep -E "^node-v[0-9.]+-$OS-$ARCH\.tar\.gz$" | head -n 1)
if [ -z "$ARCHIVE" ]; then
  dire 31 "Pas d'archive Node.js pour cet ordinateur ($OS-$ARCH)." "No Node.js archive for this computer ($OS-$ARCH)."
  exit 1
fi
if ! curl -fL --progress-bar "$BASE/$ARCHIVE" -o "$TMP/$ARCHIVE"; then
  dire 31 "Le téléchargement a échoué. Vérifie la connexion Internet, puis réessaie." "The download failed. Check the Internet connection, then try again."
  exit 1
fi
ATTENDUE=$(grep " $ARCHIVE\$" "$TMP/sommes.txt" | awk '{print $1}')
if command -v shasum >/dev/null 2>&1; then
  OBTENUE=$(shasum -a 256 "$TMP/$ARCHIVE" | awk '{print $1}')
else
  OBTENUE=$(sha256sum "$TMP/$ARCHIVE" | awk '{print $1}')
fi
if [ -z "$ATTENDUE" ] || [ "$ATTENDUE" != "$OBTENUE" ]; then
  dire 31 "La somme de contrôle de l'archive ne correspond pas : rien n'est installé." "The archive checksum does not match: nothing is installed."
  exit 1
fi
mkdir -p "$TMP/deplie" "$(dirname "$DEST")" "$LIENS" || exit 1
if ! tar -xzf "$TMP/$ARCHIVE" -C "$TMP/deplie" --strip-components 1; then
  dire 31 "L'archive n'a pas pu être dépliée (disque plein ?)." "The archive could not be unpacked (disk full?)."
  exit 1
fi
rm -rf "$DEST"
mv "$TMP/deplie" "$DEST" || exit 1
for c in node npm npx; do
  ln -sf "$DEST/bin/$c" "$LIENS/$c"
done
hash -r 2>/dev/null
if ! "$DEST/bin/node" --version >/dev/null 2>&1; then
  dire 31 "Node.js est posé mais ne se lance pas." "Node.js is in place but does not start."
  exit 1
fi
dire 32 "Node.js est installé : $("$DEST/bin/node" --version)" "Node.js is installed: $("$DEST/bin/node" --version)"
exit 0
