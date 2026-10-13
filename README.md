# Parla for Haiku

A native C++17 / Haiku Interface Kit client using the real Chatmail core,
for chatting with Delta Chat users. No GTK, Qt, WebView or Electron UI.
The name and artwork are provisional.

**Development preview:** CI builds must pass before installing their artifacts.
Live account setup and interoperability still need testing on Haiku. This is
not yet feature-complete with [Parla](https://github.com/trufae/parla).

## What is implemented

- Native menus, split views, chat search, message list, keyboard shortcuts,
  system colors/fonts, file panels, drag-and-drop attachments and notifications.
- New Chatmail profiles, existing email accounts, second-device setup codes,
  multiple profiles and backup import/export.
- One-to-one chats and groups, contact/group invitations, contact requests,
  text and attachments, replies, edits, reactions, delivery states and read receipts.
- Earlier-message loading, archive, message deletion, send retry, local drafts
  and restored window geometry.
- A private stdio JSON-RPC subprocess, with asynchronous I/O, timeouts and
  bounded buffers. Account databases and keys remain owned by Chatmail core.

Use **Profile → New profile** to create an identity, then exchange invitation
links with another Delta Chat user using **Copy my invitation** and
**Chat → Join invitation**. Enter sends; Shift+Enter inserts a newline.
Attachments open with their native associated application after confirmation.

## Packages and architectures

The [Actions workflows](https://github.com/trufae/parla-haiku/actions) produce:

| Architecture | Build | Artifact |
| --- | --- | --- |
| `x86_64` | Haiku R1/beta6 VM; native compiler and runtime tests | `parla-haiku-x86_64` |
| `x86` (32-bit) | Linux cross-compiler, Haiku hybrid ABI, modern GCC | `parla-haiku-x86` |

Download an artifact from a **successful** run, unzip it, and open the `.hpkg`
in Haiku to install it. The application is added to Deskbar. The 32-bit build
uses the modern `x86` secondary ABI; it is not a GCC 2 / BeOS binary.
Cross-compilation alone does not verify execution on a 32-bit Haiku system.

## Build on 64-bit Haiku

```sh
pkgman install rust_bin cmake ninja git openssl3_devel pkgconfig python3.11 llvm21_lld
git clone --recurse-submodules https://github.com/trufae/parla-haiku.git
cd parla-haiku
RUSTFLAGS="-C link-arg=-fuse-ld=lld" PYTHON=python3.11 sh scripts/build-core.sh
cmake -S . -B build/gui -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/gui -j4
ctest --test-dir build/gui --output-on-failure
python3.11 tests/core_smoke.py build/core/target/release/deltachat-rpc-server
sh scripts/package.sh
sh tests/gui_smoke.sh
```

Packages go into `dist/`. Development launches can set `PARLA_RPC_SERVER` to
the built server path. Normal installed launches find the server beside the GUI.
Settings and profiles live in `~/config/settings/Parla`; do not delete that
folder without exporting a backup.

## Cross-build 32-bit

The complete reproducible invocation is in
[`.github/workflows/haiku-x86.yml`](.github/workflows/haiku-x86.yml).
It uses Haiku's official `x86_gcc2h-r1beta5` toolchain on Ubuntu 24.04, a pinned Rust nightly
with `rust-src`, and `scripts/cross-x86.sh`. Both executables are checked for
ELF32 before package creation; the job fails if no hpkg is produced.

## Core port and verification

Chatmail core 2.63.0 is a pinned submodule. Patches are applied to an exported
build tree, leaving the submodule clean. Native builds link Haiku's OpenSSL 3;
x86 cross-builds use pinned OpenSSL 3.5.9 with assembly disabled, as in
HaikuPorts. SQLCipher remains bundled. TLS and OpenPGP checks remain enabled.
Small, checksum-verified crate patches add Haiku networking support for Iroh.
See [docs/PORTING.md](docs/PORTING.md) for details and limitations.

The portable transport tests also run on Linux using ordinary CMake/CTest.
The core integration test uses temporary offline profiles and does not provision
accounts or send mail. The native CI additionally checks the GUI/core handshake.
Webxdc, voice/video, inline media and other remaining parity work are listed
in the porting document. Branding can change independently of the protocol.

GPL-3.0-or-later for the client; Chatmail core is MPL-2.0. Third-party dependency
licenses remain applicable.
