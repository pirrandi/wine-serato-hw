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

# Serato crashes while quitting under Wine; with wineasio loaded its crash reporter
# then waits forever and the process never exits. By then MIDI and audio are already
# released, so once Serato logs its shutdown phase, give it a grace period and stop
# the prefix if it is still alive. Only the log created by this launch is watched.
logs_dir=$(xdg-user-dir MUSIC 2>/dev/null || echo "$HOME/Music")/_Serato_/Logs
grace=${SERATO_QUIT_GRACE:-20}
marker=$(mktemp)
trap 'rm -f "$marker"' EXIT

wine "Serato DJ Pro.exe" "$@" &
pid=$!
log=
while kill -0 "$pid" 2>/dev/null; do
    sleep 2
    if [[ -z "$log" ]]; then
        for f in "$logs_dir"/*.log; do
            [[ "$f" -nt "$marker" ]] && { log=$f; break; }
        done
    elif grep -q 'Application phase set to Shutdown' "$log"; then
        for _ in $(seq "$grace"); do
            kill -0 "$pid" 2>/dev/null || break
            sleep 1
        done
        if kill -0 "$pid" 2>/dev/null; then
            echo "serato-launch: Serato still running ${grace}s after quitting, stopping the prefix" >&2
            wineserver -k
        fi
        break
    fi
done
wait "$pid"
