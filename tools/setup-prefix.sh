#!/usr/bin/env bash
# Prepares a Wine prefix so Serato DJ Pro can use a DJ controller as supported hardware.
# Usage: setup-prefix.sh [profile]   (a name from profiles/ or a path to a .conf file)
# Installs the interoperability DLLs next to Serato, the registry keys of the
# controller's vendor driver, and wineasio under the vendor ASIO driver's name.
set -euo pipefail

WINEPREFIX=${WINEPREFIX:-$HOME/.wine-serato}
export WINEPREFIX
REPO=$(cd "$(dirname "$0")/.." && pwd)
SERATO_DIR="$WINEPREFIX/drive_c/Program Files/Serato/Serato DJ Pro"
CONFIG_DIR=${XDG_CONFIG_HOME:-$HOME/.config}/wine-serato-hw

profile=${1:-hercules-djcontrol-inpulse-500}
[[ -f "$profile" ]] || profile="$REPO/profiles/$profile.conf"
[[ -f "$profile" ]] || { echo "Profile not found: $profile (see profiles/)" >&2; exit 1; }
ASIO_NAME='' DRIVER_KEYS=() WINEASIO_OUTPUT_PORTS='' WINEASIO_INPUT_PORTS=''
# shellcheck source=/dev/null
. "$profile"

[[ -f "$SERATO_DIR/Serato DJ Pro.exe" ]] || { echo "Serato DJ Pro not found in $SERATO_DIR" >&2; exit 1; }

make -C "$REPO"

if [[ ! -f "$SERATO_DIR/libusb-1.0.dll.orig" && -f "$SERATO_DIR/libusb-1.0.dll" ]]; then
    cp "$SERATO_DIR/libusb-1.0.dll" "$SERATO_DIR/libusb-1.0.dll.orig"
fi
cp "$REPO/build/winmm.dll" "$REPO/build/libusb-1.0.dll" "$SERATO_DIR/"

# Vendor driver markers. Serato enumerates the subkeys of the parent key (for Hercules,
# it parses the first subkey name as the driver version), so the parent is recreated
# with only this subkey. Imported as a .reg file because `reg add` would also create
# an empty default value, which the real keys do not have.
if (( ${#DRIVER_KEYS[@]} )); then
    regfile=$(mktemp --suffix=.reg)
    trap 'rm -f "$regfile"' EXIT
    printf 'Windows Registry Editor Version 5.00\r\n' > "$regfile"
    for key in "${DRIVER_KEYS[@]}"; do
        wine reg delete "HKLM\\${key%\\*}" /f /reg:64 >/dev/null 2>&1 || true
        printf '\r\n[HKEY_LOCAL_MACHINE\\%s]\r\n' "$key" >> "$regfile"
    done
    wine regedit /S "$(winepath -w "$regfile")"
fi

# ASIO: Serato picks the vendor driver by its key name under HKLM\Software\ASIO.
# Build a patched wineasio (see patches/) and expose it under that name. Its
# builtin name is wineasio.dll, which Wine 11 (no wine64, no wineasio-register
# support) only finds through WINEDLLPATH; serato-launch.sh sets it.
DLL_DIR=${DLL_DIR:-$HOME/.local/lib/wine-serato-hw/dlls}
export DLL_DIR
WINEASIO_CLSID='{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}'
"$REPO/tools/build-wineasio.sh"
export WINEDLLPATH="$DLL_DIR${WINEDLLPATH:+:$WINEDLLPATH}"
# wineasio registers its COM server as wineasio64.dll; both names must resolve.
cp "$DLL_DIR/x86_64-windows/wineasio.dll" "$WINEPREFIX/drive_c/windows/system32/wineasio.dll"
cp "$DLL_DIR/x86_64-windows/wineasio.dll" "$WINEPREFIX/drive_c/windows/system32/wineasio64.dll"
wine regsvr32 /s wineasio.dll
if [[ -n "$ASIO_NAME" ]]; then
    wine reg add "HKLM\\Software\\ASIO\\$ASIO_NAME" /v CLSID /d "$WINEASIO_CLSID" /f /reg:64 >/dev/null
    wine reg add "HKLM\\Software\\ASIO\\$ASIO_NAME" /v Description /d "$ASIO_NAME" /f /reg:64 >/dev/null
    echo "ASIO: patched wineasio registered as '$ASIO_NAME'"
fi

# serato-launch.sh reads the port regexes from here.
mkdir -p "$CONFIG_DIR"
cp "$profile" "$CONFIG_DIR/controller.conf"

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
