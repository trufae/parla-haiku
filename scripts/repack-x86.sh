#!/bin/sh
# Packaging-only repair of the published preview; preserve both ELF binaries.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
curl -fsSL --retry 3 https://github.com/trufae/parla-haiku/releases/download/v0.1.0/parla-0.1.0-1-x86.hpkg -o "$tmp/original.hpkg"
echo "0ebad39739fd723629748bb115730c595d090217ae2603d198fa404b99f59632  $tmp/original.hpkg" | sha256sum -c -
mkdir "$tmp/payload" "$tmp/check"
package extract -C "$tmp/payload" "$tmp/original.hpkg"
cd "$tmp/payload"
sha256sum apps/Parla/Parla apps/Parla/deltachat-rpc-server > "$tmp/binaries.sha256"
python3 - <<'PY'
from pathlib import Path
p = Path('.PackageInfo')
text = p.read_text()
for old, new in [('name parla\n', 'name parla_x86\n'),
                 ('version 0.1.0-1\n', 'version 0.1.0-2\n'),
                 ('architecture x86\n', 'architecture x86_gcc2\n'),
                 ('provides { parla = 0.1.0 }', 'provides { parla_x86 = 0.1.0 }')]:
    assert text.count(old) == 1, old
    text = text.replace(old, new)
assert 'haiku_x86' in text
p.write_text(text)
PY
mkdir -p "$root/dist"
output="$root/dist/parla_x86-0.1.0-2-x86_gcc2.hpkg"
package create "$output"
package extract -C "$tmp/check" "$output"
cd "$tmp/check"
sha256sum -c "$tmp/binaries.sha256"
cmp .PackageInfo "$tmp/payload/.PackageInfo"
package list -i "$output"
cd "$root/dist"
sha256sum parla_x86-0.1.0-2-x86_gcc2.hpkg > SHA256SUMS-x86
