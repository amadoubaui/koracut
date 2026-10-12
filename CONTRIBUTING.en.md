# Contributing to KoraCut

*[Version française](CONTRIBUTING.md)*

Thank you for your interest in KoraCut. The project is young and developed by a small team (PIXEWORK, Dakar): every bit of help counts, a precise report as much as a fix.

## Reporting a defect or suggesting an idea

Open an issue at https://github.com/amadoubaui/koracut/issues, in French or in English. For a defect, tell us:

- your system (macOS, Windows, Linux) and your KoraCut version (Help menu, About);
- what you did, what you expected, what happened;
- if you can, a screenshot or a small project that reproduces the defect.

You can also write through the website: https://koracut.org/en/contact

## Building

Dependencies and the procedure for each system are in [dev-docs/build.md](dev-docs/build.md). In short: Qt 6.10 or newer, KDE Frameworks 6.21 or newer, MLT 7.38 or newer, KDDockWidgets 2.4, CMake.

```
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
```

## Proposing a change

1. Start from the `koracut` branch and open a pull request against it.
2. One change per pull request, with a commit message that says what changes and why.
3. Code specific to KoraCut lives in `src/koracut/`. Every new file carries an SPDX header (`SPDX-License-Identifier: GPL-3.0-or-later`, like the neighbouring files).
4. A displayed string is written in English inside `i18nc()`, and its French translation goes into `po/fr/koracut.po` with the same context.
5. Tests must pass (`ctest`). A fixed defect deserves a test that would have caught it.
6. KoraCut must stay identical on macOS, Windows and Linux, and light on a modest machine (4 cores, 8 GB): nothing that loads the processor while editing.

## License

KoraCut is licensed under GPL v3 (`COPYING`). By proposing a change, you agree that it is published under this license.
