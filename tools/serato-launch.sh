#!/usr/bin/env bash
# Starts Serato DJ Pro in its Wine prefix with the interoperability layers.
# Also used as the x-scheme-handler for seratodjpro:// (login callback): the
# URL is passed as the first argument, as the Windows registration does.
WINEPREFIX=${WINEPREFIX:-$HOME/.wine-serato}
export WINEPREFIX
export WINEDLLOVERRIDES="winmm=n,b${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
export WINEDLLPATH="$HOME/.local/lib/wine-serato-hw/dlls${WINEDLLPATH:+:$WINEDLLPATH}"
# Patched wineasio: connect only to the controller's own ports, in channel order.
# The regexes come from the profile installed by setup-prefix.sh; the environment wins.
conf=${XDG_CONFIG_HOME:-$HOME/.config}/wine-serato-hw/controller.conf
if [[ -f "$conf" ]]; then
    eval "$(. "$conf"; printf 'profile_out=%q profile_in=%q' "${WINEASIO_OUTPUT_PORTS-}" "${WINEASIO_INPUT_PORTS-}")"
    export WINEASIO_OUTPUT_PORTS=${WINEASIO_OUTPUT_PORTS-$profile_out}
    export WINEASIO_INPUT_PORTS=${WINEASIO_INPUT_PORTS-$profile_in}
fi

cd "$WINEPREFIX/drive_c/Program Files/Serato/Serato DJ Pro" || exit 1
exec wine "Serato DJ Pro.exe" "$@"
