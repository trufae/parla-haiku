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
for i in $(seq 1 30); do
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 1
done
if kill -0 "$pid" 2>/dev/null; then kill "$pid"; echo "GUI timed out" >&2; exit 1; fi
wait "$pid"
test -s "$PARLA_SMOKE_MARKER"
cat "$PARLA_SMOKE_MARKER"
