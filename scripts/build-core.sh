#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${BUILD_DIR:-"$root/build"}
mkdir -p "$build/core"
# Export the pinned tree: never modify a developer's submodule checkout.
git -c safe.directory="$root/subprojects/chatmail-core" -C "$root/subprojects/chatmail-core" archive HEAD | tar -x -C "$build/core"
for patch in "$root"/patches/*.patch; do
    patch -d "$build/core" -p1 < "$patch"
done
${PYTHON:-python3} "$root/scripts/patch-crates.py" "$build/core"
cd "$build/core"
if [ "$(uname -s)" = Haiku ]; then
    export OPENSSL_NO_VENDOR=1
    export OPENSSL_LIB_DIR=/boot/system/develop/lib
    export OPENSSL_INCLUDE_DIR=/boot/system/develop/headers
    export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-lnetwork"
fi
# Keep names and line tables: stripped Rust/C frames made device faults opaque.
export CARGO_PROFILE_RELEASE_STRIP=none
export CARGO_PROFILE_RELEASE_DEBUG=1
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

# Use the same patched Mio/Tokio versions for an executable native runtime probe.
set -- build --locked --release --manifest-path "$root/tests/runtime/Cargo.toml" \
    --target-dir "$build/core/target" --config "patch.crates-io.mio.path=\"$build/core/haiku-crates/mio-1.2.0\""
if [ -n "${CORE_TARGET:-}" ]; then
    set -- "$@" --target "$CORE_TARGET" -Zbuild-std=std,panic_abort
fi
if [ -n "${CORE_RUST_TOOLCHAIN:-}" ]; then
    cargo "+$CORE_RUST_TOOLCHAIN" "$@"
else
    cargo "$@"
fi
