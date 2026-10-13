#!/bin/bash
# Run inside the image defined in .github/docker/x86.Dockerfile.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
export BUILD_DIR="$root/build-x86"
mkdir -p "$BUILD_DIR"
# Use the modern compiler on Haiku's 32-bit hybrid ABI, never GCC 2.
export CC=/tools/cross-tools-x86/bin/i586-pc-haiku-gcc
export CXX=/tools/cross-tools-x86/bin/i586-pc-haiku-g++
export AR=/tools/cross-tools-x86/bin/i586-pc-haiku-ar
export RANLIB=/tools/cross-tools-x86/bin/i586-pc-haiku-ranlib
"$CC" --version
export CARGO_TARGET_I686_UNKNOWN_HAIKU_LINKER="$CC"
export CC_i686_unknown_haiku="$CC"
export AR_i686_unknown_haiku="$AR"
export CORE_TARGET=i686-unknown-haiku
export CORE_RUST_TOOLCHAIN=nightly-2026-10-01
export RUSTFLAGS='-C link-arg=-lnetwork'

# OpenSSL's Haiku configuration works with the target compiler. Build it explicitly
# with assembly disabled, as in HaikuPorts' x86 recipe. No TLS weakening.
openssl="$BUILD_DIR/openssl-src"
if [ ! -d "$openssl/.git" ]; then
    git clone --depth 1 --branch openssl-3.5.9 https://github.com/openssl/openssl.git "$openssl"
fi
test "$(git -C "$openssl" rev-parse HEAD)" = 45e844fa2a14ec92d146bd8f5778ac130b6625fb
(
    cd "$openssl"
    ./Configure haiku-x86 no-asm no-shared no-tests --prefix="$BUILD_DIR/openssl" --libdir=lib
    make -j2
    make install_sw
)
export OPENSSL_NO_VENDOR=1 OPENSSL_STATIC=1
export OPENSSL_LIB_DIR="$BUILD_DIR/openssl/lib"
export OPENSSL_INCLUDE_DIR="$BUILD_DIR/openssl/include"
# Host build scripts must continue using Linux compilers.
unset CC CXX AR RANLIB
sh "$root/scripts/build-core.sh"

# Official Linux-hosted resource and hpkg tools, with a verified release checksum.
tools="$BUILD_DIR/hosttools"
mkdir -p "$tools"
curl -fsSL --retry 3 https://github.com/haiku/haiku-toolchains-ubuntu/releases/download/hosttools-hrev60192/x86_64-linux-hosttools-hrev60192.zip -o "$tools.zip"
echo "4472719de58fae1517fc2c1eb892e2284ad719cb0f878701486c2c08cd2e7af3  $tools.zip" | sha256sum -c -
unzip -o -q "$tools.zip" -d "$tools"
chmod +x "$tools/rc" "$tools/xres" "$tools/package"
export LD_LIBRARY_PATH="$tools${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$tools:$PATH"
# This pinned hosttools build embeds its build-time system-data path. The writer
# requires that directory to exist even when every license is bundled in hpkg.
mkdir -p /home/runner/work/haiku-toolchains-ubuntu/haiku-toolchains-ubuntu/haiku/data/system/data
cmake -S "$root" -B "$BUILD_DIR/gui" -G Ninja \
    -DCMAKE_SYSTEM_NAME=Haiku -DCMAKE_SYSTEM_PROCESSOR=x86 \
    -DCMAKE_CXX_COMPILER=/tools/cross-tools-x86/bin/i586-pc-haiku-g++ \
    -DCMAKE_HAIKU_SECONDARY_ARCH=x86 \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF \
    -DCORE_BINARY="$BUILD_DIR/core/target/i686-unknown-haiku/release/deltachat-rpc-server"
cmake --build "$BUILD_DIR/gui" -j2
# Fail rather than accidentally shipping a host or 64-bit executable as x86.
file "$BUILD_DIR/gui/Parla" "$BUILD_DIR/core/target/i686-unknown-haiku/release/deltachat-rpc-server" | tee "$BUILD_DIR/architecture.txt"
test "$(grep -c 'ELF 32-bit' "$BUILD_DIR/architecture.txt")" = 2
PACKAGE_ARCH=x86 sh "$root/scripts/package.sh"
