# Contributing to KoraCut

KoraCut is developed by PIXEWORK in Dakar. The repository is not public yet, so
there is no contribution process to point you at for now. This page describes
how we work in the meantime.

## Coding guidelines

* Use the existing code style. `clang-format` config is at the root of the repository.
* Write unit tests. Run them with `-DBUILD_TESTING=ON` then `ctest`.
* Document the unclear parts, for example what a given `QMutex` is locking.
* Do not rename the internal identifiers listed in [coding.md](coding.md). They are
  invisible to the user and renaming them breaks the build.
* Every string shown to a user goes through `i18n()`, and the French catalog
  `po/fr/koracut.po` is updated in the same commit.

## Branches

* `koracut` is the working branch, forked from the v26.08.1 tag of the project
  KoraCut derives from.
* Work branches are welcome for anything that takes more than a day.

## Where the decisions live

The project documents are in the parent folder, outside the source tree:

* `TACHES.md` is the task list. A finished task is deleted from it, not ticked.
* `2 - Charte graphique KoraCut.md` holds the application design tokens.
* `9 - UX de l'application.md` and `11 - Refonte UI-UX, la passe moderne.md` hold
  the interface decisions and why they were taken.
* `Sources/BUILD.md` holds the build procedure and its pitfalls.
