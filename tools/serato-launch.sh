#!/usr/bin/env bash
# Starts Serato DJ Pro in its Wine prefix with the interoperability layers.
# Also used as the x-scheme-handler for seratodjpro:// (login callback): the
# URL is passed as the first argument, as the Windows registration does.
WINEPREFIX=${WINEPREFIX:-$HOME/.wine-serato}
export WINEPREFIX
export WINEDLLOVERRIDES="winmm=n,b${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
export WINEDLLPATH="$HOME/.local/lib/wine-serato-hw/dlls${WINEDLLPATH:+:$WINEDLLPATH}"
# Patched wineasio: connect only to the controller's own ports, in channel order
# (ASIO out 1-2 = master, 3-4 = headphones). pipewire-jack names physical ports
# "<node description>:<port>", e.g. "DJControl Inpulse 500 Analog Surround 4.0:playback_FL".
export WINEASIO_OUTPUT_PORTS=${WINEASIO_OUTPUT_PORTS-'^DJControl Inpulse 500 .*:playback_'}
export WINEASIO_INPUT_PORTS=${WINEASIO_INPUT_PORTS-'^DJControl Inpulse 500 .*:capture_'}

cd "$WINEPREFIX/drive_c/Program Files/Serato/Serato DJ Pro" || exit 1
exec wine "Serato DJ Pro.exe" "$@"
