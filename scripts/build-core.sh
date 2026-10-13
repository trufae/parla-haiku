#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${BUILD_DIR:-"$root/build"}
mkdir -p "$build/core"
# Export the pinned tree: never modify a developer's submodule checkout.
git -C "$root/subprojects/chatmail-core" archive HEAD | tar -x -C "$build/core"
for patch in "$root"/patches/*.patch; do
    patch -d "$build/core" -p1 < "$patch"
done
cd "$build/core"
if [ "$(uname -s)" = Haiku ]; then
    export OPENSSL_NO_VENDOR=1
    export OPENSSL_LIB_DIR=/boot/system/develop/lib
    export OPENSSL_INCLUDE_DIR=/boot/system/develop/headers
    export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-lnetwork"
fi
# Bundle current SQLCipher, use system OpenSSL. TLS verification is unchanged.
set -- build --locked --release -p deltachat-rpc-server --no-default-features --features deltachat/haiku
if [ -n "${CORE_TARGET:-}" ]; then
    set -- "$@" --target "$CORE_TARGET" -Zbuild-std=std,panic_abort
fi
if [ -n "${CORE_RUST_TOOLCHAIN:-}" ]; then
    cargo "+$CORE_RUST_TOOLCHAIN" "$@"
else
    cargo "$@"
fi
