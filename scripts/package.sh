#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${BUILD_DIR:-"$root/build"}
arch=${PACKAGE_ARCH:-$(uname -m)}
case "$arch" in x86_64|x86) ;; i?86) arch=x86 ;; *) echo "Unsupported package architecture: $arch" >&2; exit 1 ;; esac
stage="$build/package-root"
mkdir -p "$stage" "$root/dist"
DESTDIR="$stage" cmake --install "$build/gui" --prefix /boot/system
payload="$stage/boot/system"
mkdir -p "$payload/data/deskbar/menu" "$payload/documentation/packages/parla"
ln -sf ../../../apps/Parla/Parla "$payload/data/deskbar/menu/Parla"
cp "$root/subprojects/chatmail-core/LICENSE" "$payload/documentation/packages/parla/Chatmail-MPL-2.0"
cat > "$payload/.PackageInfo" <<INFO
name parla
version 0.1.0-1
architecture $arch
summary "Native Chatmail and Delta Chat client"
description "A native Haiku Interface Kit messaging client with bundled Chatmail core."
packager "Parla contributors"
vendor "Parla contributors"
copyrights "2026 Parla contributors"
licenses "GNU GPL v3" "MPL v2.0" "MIT"
provides { parla = 0.1.0 }
requires {
    haiku$([ "$arch" = x86 ] && printf _x86 || true)
    lib:libssl$([ "$arch" = x86 ] && printf _x86 || true) >= 3
    lib:libcrypto$([ "$arch" = x86 ] && printf _x86 || true) >= 3
    ca_root_certificates
}
INFO
cd "$payload"
package create "$root/dist/parla-0.1.0-1-$arch.hpkg"
package list "$root/dist/parla-0.1.0-1-$arch.hpkg"
