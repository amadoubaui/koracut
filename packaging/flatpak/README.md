# Empaqueter KoraCut en Flatpak

Le manifeste est `.flatpak-manifest.json`, à la racine du dépôt. Il déclare
l'application `pro.pixework.koracut` et tire ses dépendances de
`packaging/flatpak/pro.pixework.koracut-dependencies.json`.

KoraCut n'est encore publié sur aucun dépôt Flatpak. Ce qui suit sert à
construire et à essayer le paquet sur sa propre machine.

## Préparer la machine

Il faut `flatpak` et [`flatpak-builder`](https://docs.flatpak.org/en/latest/flatpak-builder.html).
Sur Ubuntu :

```
sudo apt install flatpak flatpak-builder
```

Puis le SDK :

```
flatpak install org.kde.Sdk
```

## Construire et installer

Depuis la racine du dépôt :

```
flatpak-builder ~/flatpak-buildir .flatpak-manifest.json --install
```

Le reste des options est documenté ici : https://docs.flatpak.org/en/latest/building.html

## Vérifier les mises à jour des dépendances

Le manifeste porte des champs `x-checker-data` lus par
[flatpak-external-data-checker](https://github.com/flathub/flatpak-external-data-checker).
Une fois l'outil installé :

```
flatpak install --from https://dl.flathub.org/repo/appstream/org.flathub.flatpak-external-data-checker.flatpakref
flatpak run org.flathub.flatpak-external-data-checker .flatpak-manifest.json
```

il sort la liste des dépendances qui ont une version plus récente.
