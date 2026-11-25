#!/bin/sh
set -eu
umask 077

state_dir="${HOME}/.cache/metal-sonic-screen-reader"
mkdir -p "$state_dir"
chmod 700 "$state_dir"
exec 9>"$state_dir/session.lock"
flock -n 9 || exit 0

# Current accessibility bus: avoid a stale X11 accessibility socket.
accessibility_address=$(/usr/bin/gdbus call --session --dest org.a11y.Bus \
    --object-path /org/a11y/bus --method org.a11y.Bus.GetAddress | \
    /usr/bin/python3 -c 'import ast,sys; print(ast.literal_eval(sys.stdin.read())[0])')
export AT_SPI_BUS_ADDRESS="$accessibility_address"

/usr/bin/orca --replace >>"$state_dir/screen-reader.log" 2>&1 9>&- &
orca_pid=$!
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
/usr/bin/python3 "$project_dir/clipboard_reader.py" \
    >>"$state_dir/screen-reader.log" 2>&1 9>&- &
clipboard_pid=$!

cleanup() {
    kill "$clipboard_pid" "$orca_pid" 2>/dev/null || true
    wait "$clipboard_pid" 2>/dev/null || true
    wait "$orca_pid" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 0' INT TERM
wait "$orca_pid" || true
