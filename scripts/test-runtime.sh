#!/bin/bash
# Exercise Haiku's poll + pipe backend on Linux, with an upstream negative control.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
work="$root/build/runtime-regression"
mkdir -p "$work"
python3 - "$root" "$work" <<'PY'
import hashlib, io, pathlib, re, sys, tarfile, urllib.request
root, work = map(pathlib.Path,sys.argv[1:])
lock = (root/'subprojects/chatmail-core/Cargo.lock').read_text()
checksum = re.search(r'name = "mio"\nversion = "1.2.0"\nsource = .*\nchecksum = "([^"]+)"',lock)[1]
data = urllib.request.urlopen('https://static.crates.io/crates/mio/mio-1.2.0.crate',timeout=60).read()
assert hashlib.sha256(data).hexdigest() == checksum
with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive: archive.extractall(work,filter='data')
PY
export RUSTFLAGS='--cfg mio_unsupported_force_poll_poll --cfg mio_unsupported_force_waker_pipe'
args=(run --locked --manifest-path "$root/tests/runtime/Cargo.toml" --config "patch.crates-io.mio.path=\"$work/mio-1.2.0\"")
if cargo "${args[@]}" > "$work/upstream.log" 2>&1; then
    echo 'Unpatched Mio unexpectedly passed the lost-wake regression' >&2; exit 1
fi
grep -q 'LOST WAKE:' "$work/upstream.log"
echo 'Confirmed: unpatched Mio loses runtime wakeup'
patch --batch -d "$work/mio-1.2.0" -p1 < "$root/patches/crates/mio-1.2.0.patch"
cargo "${args[@]}"
