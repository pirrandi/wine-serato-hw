#!/usr/bin/env bash
# Builds wineasio 1.3.0 with patches/wineasio-1.3.0-pipewire.patch and installs
# it as the builtin "wineasio.dll" under $DLL_DIR (loaded through WINEDLLPATH).
# Build deps: wine (winegcc, winebuild, headers), gcc, make, curl, patch.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
DLL_DIR=${DLL_DIR:-$HOME/.local/lib/wine-serato-hw/dlls}
VERSION=1.3.0
URL=https://github.com/wineasio/wineasio/releases/download/v$VERSION/wineasio-$VERSION.tar.gz
SHA256=f8bf74c871233f65e5befecc7b4f2629f2566f84e951e363af0ad51eae516a4c
WORK="$REPO/build/wineasio"

rm -rf "$WORK"
mkdir -p "$WORK"
curl -fsSL "$URL" -o "$WORK/wineasio.tar.gz"
echo "$SHA256  $WORK/wineasio.tar.gz" | sha256sum -c --quiet
tar xzf "$WORK/wineasio.tar.gz" -C "$WORK"

src="$WORK/wineasio-$VERSION"
patch -d "$src" -p1 < "$REPO/patches/wineasio-$VERSION-pipewire.patch"
make -C "$src" 64

# The COM server is registered as wineasio64.dll, but the module's builtin name is
# wineasio.dll: Wine resolves that name through WINEDLLPATH.
mkdir -p "$DLL_DIR/x86_64-windows" "$DLL_DIR/x86_64-unix"
install -m 644 "$src/build64/wineasio64.dll" "$DLL_DIR/x86_64-windows/wineasio.dll"
install -m 755 "$src/build64/wineasio64.dll.so" "$DLL_DIR/x86_64-unix/wineasio.dll.so"
echo "wineasio $VERSION (patched) installed in $DLL_DIR"
