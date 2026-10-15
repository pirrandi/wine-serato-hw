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

# A URL launch while Serato is running is handed to the running instance.
# Otherwise, leftovers of a previous session that hung are stopped first.
if ! pgrep -f 'Serato DJ Pro\.exe' >/dev/null; then
    wineserver -k 2>/dev/null
fi

# Serato crashes under Wine while quitting, and sometimes while starting. With
# wineasio loaded its crash reporter then waits forever and the process never
# exits. A crash shows up as Dumps/last_crash being rewritten; quitting shows up
# as the shutdown phase in the log created by this launch.
#  - Crash while quitting: MIDI and audio are already released, so after a grace
#    period the prefix is stopped.
#  - Crash at any other time: the prefix is stopped right away; if it happened
#    during startup, Serato is launched again once.
logs_dir=$(xdg-user-dir MUSIC 2>/dev/null || echo "$HOME/Music")/_Serato_/Logs
crash_file="$WINEPREFIX/drive_c/users/$USER/AppData/Local/Serato/SeratoDJ/Dumps/last_crash"
grace=${SERATO_QUIT_GRACE:-20}
start_window=${SERATO_START_WINDOW:-90}
marker=$(mktemp)
trap 'rm -f "$marker"' EXIT

# Returns 0 when Serato exits on its own or after quitting, 2 after a startup crash.
run_serato() {
    touch "$marker"
    local started=$SECONDS log= pid
    wine "Serato DJ Pro.exe" "$@" &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        sleep 2
        if [[ -z "$log" ]]; then
            for f in "$logs_dir"/*.log; do
                [[ "$f" -nt "$marker" ]] && { log=$f; break; }
            done
        fi
        if [[ -n "$log" ]] && grep -q 'Application phase set to Shutdown' "$log"; then
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
        if [[ "$crash_file" -nt "$marker" ]]; then
            echo "serato-launch: Serato crashed, stopping the prefix" >&2
            wineserver -k
            wait "$pid"
            (( SECONDS - started < start_window )) && return 2
            return 0
        fi
    done
    wait "$pid"
    return 0
}

run_serato "$@"
if (( $? == 2 )); then
    echo "serato-launch: crash during startup, launching again" >&2
    sleep 2
    run_serato "$@"
fi
