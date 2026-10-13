#!/usr/bin/env python3
"""Download only patched, lockfile-pinned crates; preserve all other resolutions."""
import hashlib
import io
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request

root = pathlib.Path(__file__).resolve().parent.parent
core = pathlib.Path(sys.argv[1]).resolve()
lockfile = core / 'Cargo.lock'
lock = lockfile.read_text()
manifest = core / 'Cargo.toml'
patches = []
for patch in sorted((root / 'patches/crates').glob('*.patch')):
    name, version = patch.stem.rsplit('-', 1)
    pattern = (r'(?m)^name = "' + re.escape(name) + r'"\nversion = "'
               + re.escape(version) + r'"\nsource = "registry\+https://github.com/rust-lang/crates.io-index"\nchecksum = "([a-f0-9]{64})"\n')
    match = re.search(pattern, lock)
    if not match:
        raise SystemExit(f'{name} {version} does not match the pinned upstream lockfile')
    url = f'https://static.crates.io/crates/{name}/{name}-{version}.crate'
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != match.group(1):
        raise SystemExit(f'{name}: crate checksum mismatch')
    vendor = core / 'haiku-crates'
    vendor.mkdir(exist_ok=True)
    destination = vendor / patch.stem
    if destination.exists():
        shutil.rmtree(destination)
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        # Every crate member must remain under its one expected directory.
        for member in archive.getmembers():
            path = pathlib.PurePosixPath(member.name)
            if (path.is_absolute() or '..' in path.parts or path.parts[0] != patch.stem
                    or not (member.isfile() or member.isdir())):
                raise SystemExit(f'{name}: unsafe archive member')
        archive.extractall(vendor, filter="data")
    subprocess.run(['patch', '--batch', '-d', str(vendor / patch.stem), '-p1', '-i', str(patch)], check=True)
    patches.append(f'{name} = {{ path = "haiku-crates/{patch.stem}" }}')
    # A path override removes only source/checksum, never resolves newer versions.
    lock = lock[:match.start()] + f'name = "{name}"\nversion = "{version}"\n' + lock[match.end():]
manifest.write_text(manifest.read_text() + '\n[patch.crates-io]\n' + '\n'.join(patches) + '\n')
lockfile.write_text(lock)
