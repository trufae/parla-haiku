#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
export DC_ACCOUNTS_PATH="$tmp/accounts"
export PARLA_RPC_SERVER="$root/build/core/target/release/deltachat-rpc-server"
export PARLA_SMOKE_MARKER="$tmp/handshake"
"$root/build/gui/Parla" --smoke-test &
pid=$!
captured=false
mkdir -p "$root/dist"
for i in $(seq 1 30); do
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    if [ -s "$PARLA_SMOKE_MARKER" ] && [ "$captured" = false ]; then
        # Haiku's screenshot main() returns BApplication::Run(), a thread ID,
        # rather than a POSIX success status. Check the saved image instead.
        screenshot -s "$root/dist/parla-native.png" || true
        test -s "$root/dist/parla-native.png"
        captured=true
    fi
    sleep 1
done
if kill -0 "$pid" 2>/dev/null; then kill "$pid"; echo "GUI timed out" >&2; exit 1; fi
wait "$pid"
test -s "$PARLA_SMOKE_MARKER"
test -s "$root/dist/parla-native.png"
cat "$PARLA_SMOKE_MARKER"
