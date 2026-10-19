---
description: Paquetes, CI, publicación de versiones y firma de las actualizaciones.
---

# Versiones y actualizaciones

## Paquetes

```bash
cpack --config build/CPackConfig.cmake      # .deb (Linux), instalador NSIS y .zip (Windows), .dmg (macOS)
packaging/linux/build-appimage.sh build     # AppImage y .tar.gz portátil
```

El `.deb` usa el Qt del sistema, así que se compila con el Qt de la distro (`qt6-base-dev`): el CI lo genera en
Debian 12.

## CI

`.github/workflows/ci.yml` compila y pasa los tests en Linux, Windows y macOS en cada push, genera los paquetes
como artefactos y, en los tags `v*`, los adjunta a la release de GitHub.

## Publicar una versión

La versión sale del tag:

```bash
git tag v1.6.0 && git push origin v1.6.0                # release estable
git tag v1.6.0-beta.1 && git push origin v1.6.0-beta.1  # prerelease: sólo la ofrece el canal beta
```

`QAFLOW_VERSION` en `CMakeLists.txt` es sólo para los builds locales (o `-DQAFLOW_VERSION=…`). El CI rechaza
tags que no sean `vX.Y.Z` o `vX.Y.Z-pre`.

## Firma de las actualizaciones

QAflow busca versiones en las releases del repositorio `QAFLOW_UPDATE_REPO`. Para que se instalen solas, las
releases van firmadas (Ed25519 sobre `SHA256SUMS`). Una sola vez:

```bash
packaging/release/update-signing-key.sh
```

* La clave **privada** va al secreto `QAFLOW_UPDATE_SIGNING_KEY` del repositorio.
* La **pública**, a `QAFLOW_UPDATE_PUBLIC_KEY` en `CMakeLists.txt`.

Sin la pública compilada, QAflow sólo avisa de las versiones nuevas; sin el secreto, la release sale sin firma y
nadie se actualiza solo.
