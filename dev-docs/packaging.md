# Package KoraCut

KoraCut is not published on any store yet. This page lists the four paths that
exist in the repository, and what is still missing on each.

## Overview

| Package | OS | Where it is defined | State |
| --- | --- | --- | --- |
| `.dmg` | macOS | KDE Craft, `craft --package` | needs a Craft blueprint of our own, see below |
| `.exe` | Windows | KDE Craft, or `packaging/koracut-msys2.sh` | untested |
| AppImage | Linux | KDE Craft, or `packaging/koracut-lnx-withalldeps.sh` | untested |
| Flatpak | Linux | [`.flatpak-manifest.json`](../.flatpak-manifest.json), notes in [packaging/flatpak](../packaging/flatpak) | builds locally, not published |
| Snap | Linux | [`snapcraft.yaml`](../snapcraft.yaml) | not published |

## Craft

Craft builds the application and every third-party dependency, then packages
the result for the platform it runs on. Set it up as described on
https://community.kde.org/Craft, then package with `--package`.

One thing is still in the way on macOS: the Craft blueprint we currently reuse
looks for an entitlements file named after the project it was written for,
while ours is `koracut.entitlements`. Do not patch that blueprint in place,
`craft --update` overwrites it. Write our own blueprint in a folder of ours
under `CraftRoot/etc/blueprints/locations/` instead. The task is written down
in `TACHES.md`.

## Flatpak

The manifest declares `pro.pixework.koracut` and pulls its dependencies from
`packaging/flatpak/pro.pixework.koracut-dependencies.json`. Build and install
instructions are in [packaging/flatpak/README.md](../packaging/flatpak/README.md).

## Snap

`snapcraft.yaml` at the root is a symbolic link to `packaging/snap/snapcraft.yaml`.
The snap is named `koracut`, runs `usr/bin/koracut` and reads its metadata from
`pro.pixework.koracut.appdata.xml`. The general guide is
https://community.kde.org/Guidelines_and_HOWTOs/Snap.

## Before the first public build

- Sign the macOS bundle, which needs an Apple developer certificate.
- Decide the domain and the public repository, both are still open in `TACHES.md`.
- Check the version number in `CMakeLists.txt` against the one shown in the About window.
