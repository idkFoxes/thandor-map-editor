#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:?Usage: build_appimage.sh BUILD_DIRECTORY}"
build="$(cd "$build" && pwd)"
arch="$(uname -m)"
case "$arch" in x86_64|aarch64) ;; *) echo "Unsupported AppImage architecture: $arch" >&2; exit 64 ;; esac
for required in thandor-map-editor shaders assets/fonts/DejaVuSans.ttf assets/fonts/LICENSE; do
  [[ -e "$build/$required" ]] || { echo "Missing AppImage input: $build/$required" >&2; exit 1; }
done
version="$(sed -n 's/.*kEditorVersion = "\([^"]*\)".*/\1/p' "$root/include/thandor/editor_version.hpp")"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Cannot read editor version.' >&2; exit 1; }
dist="${THANDOR_APPIMAGE_OUTPUT_DIR:-$root/dist}"
mkdir -p "$dist"
dist="$(cd "$dist" && pwd)"
tool="${THANDOR_LINUXDEPLOY:-}"
if [[ -z "$tool" ]]; then
  tool="$(command -v linuxdeploy || true)"
fi
if [[ -z "$tool" ]]; then
  cache="$root/.deps/appimage"
  mkdir -p "$cache"
  tool="$cache/linuxdeploy-$arch.AppImage"
  if [[ ! -x "$tool" ]]; then
    url="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$arch.AppImage"
    echo "[AppImage] Downloading linuxdeploy from $url"
    download="$(mktemp "$cache/.download.XXXXXX")"
    trap 'rm -f -- "$download"' EXIT
    if command -v curl >/dev/null 2>&1; then
      curl --fail --location --retry 2 "$url" -o "$download"
    elif command -v wget >/dev/null 2>&1; then
      wget -O "$download" "$url"
    else
      echo 'Install curl or wget, or set THANDOR_LINUXDEPLOY to a linuxdeploy executable.' >&2
      exit 1
    fi
    chmod +x "$download"
    mv -f -- "$download" "$tool"
    trap - EXIT
  fi
fi
tool="$(command -v "$tool")"
tool="$(cd "$(dirname "$tool")" && pwd)/$(basename "$tool")"
stage="$(mktemp -d "$build/.appimage-stage.XXXXXX")"
trap 'rm -rf -- "$stage"' EXIT
appdir="$stage/AppDir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/metainfo"
cp "$build/thandor-map-editor" "$appdir/usr/bin/"
cp -a "$build/shaders" "$build/assets" "$appdir/usr/bin/"
# appimagetool discovers upstream AppStream metadata by the desktop-file stem.
cp "$root/assets/appimage/io.github.idkfoxes.ThandorMapEditor.appdata.xml" \
  "$appdir/usr/share/metainfo/io.github.idkfoxes.ThandorMapEditor.appdata.xml"
output="Thandor_Map_Editor-$version-$arch.AppImage"
# Extraction avoids requiring FUSE in build containers. The Vulkan loader and
# hardware-specific graphics drivers remain supplied by the target machine.
(
  cd "$stage"
  APPIMAGE_EXTRACT_AND_RUN=1 LDAI_OUTPUT="$stage/$output" LDAI_VERSION="$version" \
    "$tool" --appdir "$appdir" --executable "$appdir/usr/bin/thandor-map-editor" \
    --desktop-file "$root/assets/appimage/io.github.idkfoxes.ThandorMapEditor.desktop" \
    --icon-file "$root/assets/appimage/thandor-map-editor.png" \
    --exclude-library 'libvulkan.so*' --output appimage
)
[[ -s "$stage/$output" ]] || { echo 'linuxdeploy did not create the requested AppImage.' >&2; exit 1; }
chmod +x "$stage/$output"
mv -f -- "$stage/$output" "$dist/$output"
echo "[AppImage] Created: $dist/$output"
