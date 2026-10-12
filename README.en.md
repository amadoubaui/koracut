![](data/pics/koracut-logo.png)

# KoraCut

*[Version française](README.md)*

KoraCut is a desktop application for multimedia creation: video editing,
color grading, audio, VFX, photo, illustration, motion design. Every module can
be driven by AI agents, with the language model of your choice.

Developed by PIXEWORK in Dakar. Free software, licensed under GPL v3.

Website, documentation and download: https://koracut.org/en

## Modules

| Number | Module | Status |
|---|---|---|
| 1 | Editing | in place |
| 2 | Color grading | in place |
| 3 | Audio and DAW | in place, full module to come |
| 4 | VFX | in place, node graph to come |
| 5 | Photo | to come |
| 6 | Illustration | to come |
| 7 | Export | in place |
| 8 | Motion design | in place |

## Build

Dependencies are Qt 6.10 or newer, KDE Frameworks 6.21 or newer, MLT 7.38 or
newer, KDDockWidgets 2.4 and CMake. The details for each system are in
[dev-docs/build.md](dev-docs/build.md).

In short:

```
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

On macOS, the studio's toolchain goes through KDE Craft.

## Packaging

- Flatpak: `.flatpak-manifest.json` at the root, notes in [packaging/flatpak/README.md](packaging/flatpak/README.md)
- Snap: `snapcraft.yaml`
- Windows with MSYS2: `packaging/koracut-msys2.sh`
- Linux with all dependencies: `packaging/koracut-lnx-withalldeps.sh`

## Contributing

See [CONTRIBUTING.en.md](CONTRIBUTING.en.md).

## License

GPL v3. The full text is in `COPYING`, the licenses of the components in
`LICENSES/`.

Copyright © 2026 PIXEWORK.

KoraCut is derived from a free video editing software published under GPL v3:
https://invent.kde.org/multimedia/kdenlive. The original code, its authors and
its license are preserved. Copyright holders are listed in `AUTHORS` and in the
SPDX headers of the source files.
