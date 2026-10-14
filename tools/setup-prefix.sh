#!/usr/bin/env bash
# Prepares a Wine prefix so Serato DJ Pro can use a Hercules DJControl Inpulse 500.
# Installs the interoperability DLLs next to Serato and registers the Hercules
# driver marker that Serato checks before activating the controller.
set -euo pipefail

WINEPREFIX=${WINEPREFIX:-$HOME/.wine-serato}
export WINEPREFIX
REPO=$(cd "$(dirname "$0")/.." && pwd)
SERATO_DIR="$WINEPREFIX/drive_c/Program Files/Serato/Serato DJ Pro"

# Serato enumerates the subkeys of this key and parses the first name as the
# Hercules driver version. On Windows the key holds a single empty subkey named
# after the installed driver package (DisplayVersion of "DJHERCULESMIX Products
# Series drivers"); no values are read.
HERCULES_DRIVER_VERSION=${HERCULES_DRIVER_VERSION:-2.HDJS.2023}
HERCULES_KEY='HKLM\Software\Wow6432Node\Guillemot Corporation\DJHERCULESMIX Series Drivers'

[[ -f "$SERATO_DIR/Serato DJ Pro.exe" ]] || { echo "Serato DJ Pro not found in $SERATO_DIR" >&2; exit 1; }

make -C "$REPO"

if [[ ! -f "$SERATO_DIR/libusb-1.0.dll.orig" && -f "$SERATO_DIR/libusb-1.0.dll" ]]; then
    cp "$SERATO_DIR/libusb-1.0.dll" "$SERATO_DIR/libusb-1.0.dll.orig"
fi
cp "$REPO/build/winmm.dll" "$REPO/build/libusb-1.0.dll" "$SERATO_DIR/"

# Imported as a .reg file because `reg add` would also create an empty default
# value, which the real key does not have.
wine reg delete "$HERCULES_KEY" /f /reg:64 >/dev/null 2>&1 || true
regfile=$(mktemp --suffix=.reg)
trap 'rm -f "$regfile"' EXIT
printf 'Windows Registry Editor Version 5.00\r\n\r\n[HKEY_LOCAL_MACHINE\\%s\\%s]\r\n' \
    "${HERCULES_KEY#HKLM\\}" "$HERCULES_DRIVER_VERSION" > "$regfile"
wine regedit /S "$(winepath -w "$regfile")"

# ASIO: Serato picks the Hercules driver by its key name under HKLM\Software\ASIO.
# Build a patched wineasio (see patches/) and expose it under that name. Its
# builtin name is wineasio.dll, which Wine 11 (no wine64, no wineasio-register
# support) only finds through WINEDLLPATH; serato-launch.sh sets it.
DLL_DIR=${DLL_DIR:-$HOME/.local/lib/wine-serato-hw/dlls}
export DLL_DIR
WINEASIO_CLSID='{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}'
ASIO_NAME='DJControl Inpulse 500 ASIO'
"$REPO/tools/build-wineasio.sh"
export WINEDLLPATH="$DLL_DIR${WINEDLLPATH:+:$WINEDLLPATH}"
# wineasio registers its COM server as wineasio64.dll; both names must resolve.
cp "$DLL_DIR/x86_64-windows/wineasio.dll" "$WINEPREFIX/drive_c/windows/system32/wineasio.dll"
cp "$DLL_DIR/x86_64-windows/wineasio.dll" "$WINEPREFIX/drive_c/windows/system32/wineasio64.dll"
wine regsvr32 /s wineasio.dll
wine reg add "HKLM\\Software\\ASIO\\$ASIO_NAME" /v CLSID /d "$WINEASIO_CLSID" /f /reg:64 >/dev/null
wine reg add "HKLM\\Software\\ASIO\\$ASIO_NAME" /v Description /d "$ASIO_NAME" /f /reg:64 >/dev/null
echo "ASIO: patched wineasio registered as '$ASIO_NAME'"

# Serato's account login returns through a seratodjpro:// URL; without a session the
# controller's MIDI connection never starts.
apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p "$apps"
cat > "$apps/serato-djpro-url.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Serato DJ Pro (URL handler)
Exec=$REPO/tools/serato-launch.sh %u
MimeType=x-scheme-handler/seratodjpro;
NoDisplay=true
Terminal=false
EOF
xdg-mime default serato-djpro-url.desktop x-scheme-handler/seratodjpro

echo "Prefix ready: $WINEPREFIX"
