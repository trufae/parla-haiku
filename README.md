# Parla for Haiku

A native C++/Haiku Interface Kit client for the Chatmail network, interoperable
with Delta Chat. Name and artwork are provisional.

## Build

Target: x86_64 Haiku R1/beta6. CI builds in an actual Haiku VM.

```
pkgman install rust_bin cmake ninja git openssl_devel pkgconfig python3
git submodule update --init --recursive
sh scripts/build-core.sh
```

Chatmail core is pinned to 2.63.0 as a submodule. The patch adds a build feature
that keeps bundled SQLCipher but links Haiku's maintained OpenSSL; upstream's
vendored OpenSSL path is not used. No certificate or encryption checks are
removed. Build output and patched sources stay in `build/`.

This repository is under active implementation; a successful core/GUI/package
build and runtime verification are required before calling it a usable release.
