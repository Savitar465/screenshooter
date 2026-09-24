#!/usr/bin/env bash
# Genera QAflow-<versión>-x86_64.AppImage y qaflow-<versión>-Linux-x86_64.tar.gz a partir de un
# directorio de build ya compilado. Los dos llevan Qt dentro: el .tar.gz es el mismo AppDir
# comprimido, para quien no quiera o no pueda usar AppImage (se ejecuta con ./AppRun).
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DQAFLOW_BUILD_TESTS=OFF && cmake --build build -j
#   packaging/linux/build-appimage.sh build
#
# Requiere linuxdeploy y su plugin de Qt (se descargan si no están en el PATH).
set -euo pipefail

BUILD_DIR="${1:-build}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APPDIR="$ROOT/$BUILD_DIR/AppDir"
TOOLS="$ROOT/$BUILD_DIR/appimage-tools"
VERSION="$(grep -oP 'project\(qaflow VERSION \K[0-9.]+' "$ROOT/CMakeLists.txt")"

mkdir -p "$TOOLS"
fetch() {
    local name="$1" url="$2"
    if command -v "$name" >/dev/null 2>&1; then echo "$(command -v "$name")"; return; fi
    if [ ! -x "$TOOLS/$name" ]; then
        curl -fsSL -o "$TOOLS/$name" "$url"
        chmod +x "$TOOLS/$name"
    fi
    echo "$TOOLS/$name"
}
LINUXDEPLOY="$(fetch linuxdeploy-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage)"
PLUGIN_QT="$(fetch linuxdeploy-plugin-qt-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage)"

rm -rf "$APPDIR"
cmake --install "$ROOT/$BUILD_DIR" --prefix "$APPDIR/usr"

export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export PATH="$(dirname "$PLUGIN_QT"):$PATH"
export LDAI_OUTPUT="$ROOT/$BUILD_DIR/QAflow-$VERSION-x86_64.AppImage"
export EXTRA_QT_PLUGINS="${EXTRA_QT_PLUGINS:-platforms;imageformats;iconengines}"

"$LINUXDEPLOY" --appdir "$APPDIR" \
    --desktop-file "$ROOT/packaging/linux/qaflow.desktop" \
    --icon-file "$ROOT/resources/icons/qaflow.svg" \
    --plugin qt --output appimage

echo "AppImage generado: $LDAI_OUTPUT"

# .tar.gz portátil: el AppDir ya desplegado, con una carpeta raíz con nombre y versión.
TARBALL="$ROOT/$BUILD_DIR/qaflow-$VERSION-Linux-x86_64.tar.gz"
STAGE="$ROOT/$BUILD_DIR/tarball"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a "$APPDIR" "$STAGE/QAflow-$VERSION"
tar -C "$STAGE" -czf "$TARBALL" "QAflow-$VERSION"
rm -rf "$STAGE"
echo ".tar.gz generado: $TARBALL"
