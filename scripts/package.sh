#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${BUILD_DIR:-"$root/build"}
arch=${PACKAGE_ARCH:-$(uname -m)}
case "$arch" in x86_64|x86) ;; i?86) arch=x86 ;; *) echo "Unsupported package architecture: $arch" >&2; exit 1 ;; esac
name=parla
package_arch=$arch
revision=1
# The package targets the hybrid OS; x86 denotes its modern compiler ABI.
if [ "$arch" = x86 ]; then
    name=parla_x86
    package_arch=x86_gcc2
    revision=2
fi
stage="$build/package-root"
mkdir -p "$stage" "$root/dist"
DESTDIR="$stage" cmake --install "$build/gui" --prefix /boot/system
payload="$stage/boot/system"
mkdir -p "$payload/data/deskbar/menu/Applications" "$payload/documentation/packages/parla" "$payload/data/licenses"
cp "$root/COPYING" "$payload/data/licenses/GNU GPL v3"
cp "$root/subprojects/chatmail-core/LICENSE" "$payload/data/licenses/MPL v2.0"
cp "$root/data/nlohmann-LICENSE" "$payload/data/licenses/MIT"
cp "$root/data/Apache-2.0" "$payload/data/licenses/Apache v2"
# Remove the obsolete root-menu entry when reusing a build directory.
rm -f "$payload/data/deskbar/menu/Parla"
ln -sf ../../../../apps/Parla/Parla "$payload/data/deskbar/menu/Applications/Parla"
test -x "$payload/data/deskbar/menu/Applications/Parla"
# OpenSSL is linked statically in the x86 cross-build; ship its notice as well.
if [ -f "$build/openssl-src/LICENSE.txt" ]; then
    cp "$build/openssl-src/LICENSE.txt" "$payload/documentation/packages/parla/OpenSSL-Apache-2.0"
fi
cp "$root/subprojects/chatmail-core/LICENSE" "$payload/documentation/packages/parla/Chatmail-MPL-2.0"
cat > "$payload/.PackageInfo" <<INFO
name $name
version 0.1.3-$revision
architecture $package_arch
summary "Native Chatmail and Delta Chat client"
description "A native Haiku Interface Kit messaging client with bundled Chatmail core."
packager "Parla contributors"
vendor "Parla contributors"
copyrights "2026 Parla contributors"
licenses {
    "GNU GPL v3"
    "MPL v2.0"
    "MIT"
    "Apache v2"
}
provides { $name = 0.1.3 }
requires {
    haiku$([ "$arch" = x86 ] && printf _x86 || true)
$([ "$arch" = x86_64 ] && printf '    lib:libssl >= 3\n    lib:libcrypto >= 3' || true)
    ca_root_certificates
}
INFO
cd "$payload"
package create "$root/dist/$name-0.1.3-$revision-$package_arch.hpkg"
package list "$root/dist/$name-0.1.3-$revision-$package_arch.hpkg"
