#!/bin/bash
# Run inside the image defined in .github/docker/x86.Dockerfile.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
export BUILD_DIR="$root/build-x86_64"
mkdir -p "$BUILD_DIR"
# Use Haiku's official x86_64 cross-compiler.
export CC=/tools/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc
export CXX=/tools/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++
export AR=/tools/cross-tools-x86_64/bin/x86_64-unknown-haiku-ar
export RANLIB=/tools/cross-tools-x86_64/bin/x86_64-unknown-haiku-ranlib
"$CC" --version
export CARGO_TARGET_X86_64_UNKNOWN_HAIKU_LINKER="$CC"
export CC_x86_64_unknown_haiku="$CC"
export AR_x86_64_unknown_haiku="$AR"
export CORE_TARGET=x86_64-unknown-haiku
export CORE_RUST_TOOLCHAIN=nightly-2026-10-01
export RUSTFLAGS='-C link-arg=-lnetwork'

# OpenSSL's Haiku configuration works with the target compiler. Build it explicitly
# with the same x86_64 configuration as HaikuPorts. No TLS weakening.
openssl="$BUILD_DIR/openssl-src"
if [ ! -d "$openssl/.git" ]; then
    git clone --depth 1 --branch openssl-3.5.9 https://github.com/openssl/openssl.git "$openssl"
fi
test "$(git -C "$openssl" rev-parse HEAD)" = 45e844fa2a14ec92d146bd8f5778ac130b6625fb
(
    cd "$openssl"
    ./Configure haiku-x86_64 no-shared no-tests --prefix="$BUILD_DIR/openssl" --libdir=lib
    make -j2
    make install_sw
)
export OPENSSL_NO_VENDOR=1 OPENSSL_STATIC=1
export OPENSSL_LIB_DIR="$BUILD_DIR/openssl/lib"
export OPENSSL_INCLUDE_DIR="$BUILD_DIR/openssl/include"
# Host build scripts must continue using Linux compilers.
unset CC CXX AR RANLIB
sh "$root/scripts/build-core.sh"

# Transport the actual Haiku binary to the native runtime-test job.
mkdir -p "$root/core-artifact"
cp "$BUILD_DIR/core/target/x86_64-unknown-haiku/release/deltachat-rpc-server" "$root/core-artifact/"
cp "$BUILD_DIR/core/target/x86_64-unknown-haiku/release/parla-runtime-probe" "$root/core-artifact/"
file "$root/core-artifact/deltachat-rpc-server" | tee "$BUILD_DIR/architecture.txt"
grep -q 'ELF 64-bit' "$BUILD_DIR/architecture.txt"
