# Haiku port

## Build design

The UI is C++17 using only the Haiku Application, Interface, Storage and
Notification kits. CMake builds the UI; Cargo builds the actual upstream
Chatmail RPC server. The executable and server live together under
`/boot/system/apps/Parla/`; the Deskbar entry launches the GUI directly.

The core submodule is pinned to Chatmail 2.63.0
(`de6f46971002c64b293d6095e66d8a1b7a11fd43`). Build scripts export that commit
and apply patches to a private build tree. They do not modify the submodule.

`0001-system-openssl.patch` adds one feature to select bundled SQLCipher without
vendored OpenSSL. Native x86_64 builds use `openssl3_devel` from HaikuPorts.
The x86 cross-build explicitly configures OpenSSL 3.5.9 for `haiku-x86` and links
it statically. The 32-bit target uses modern GCC on Haiku's hybrid x86 ABI,
not the BeOS GCC 2 ABI. Rust's standard library is cross-built for
`i686-unknown-haiku` using the pinned nightly and `rust-src`.

Both paths preserve TLS verification and core's OpenPGP implementation.
System root certificates come from Haiku's `ca_root_certificates` package;
`openssl-probe` includes Haiku's certificate directory.

## UI and lifecycle

- Standard menus, system fonts/colors and layout spacing; resizable split view.
- Worker-owned nonblocking pipes. GUI callbacks are posted to the window looper.
- RPC request IDs, per-request timeouts, bounded request/response buffers, and
  independent stderr draining. A timed-out send is never automatically retried.
- Account/chat generation checks discard stale conversation responses.
- Message reads are acknowledged only while the window is active and a full row
  is in the viewport. Oversized rows are not automatically marked read.
- Draft text, attachments and replies are retained locally across navigation and
  quit. Profile and message data remain owned by Chatmail.
- Attachments open only on an explicit user action with confirmation.
- Notifications omit message bodies. No web content or remote images execute.

## Verification

`ctest --test-dir build/gui --output-on-failure` runs transport tests against a
real child process: fragmented JSON, Unicode, full stderr pipe, concurrent calls,
nonblocking event polling, timeout, EOF, invalid JSON and restart.

`tests/core_smoke.py` launches the real core with a temporary account directory.
It verifies profile configuration storage, Unicode, failed offline sends,
chat/message list shapes, attachment copying and deletion. It never starts
network I/O. Offline send failure is expected without a configured transport.

The x86_64 workflow additionally launches the native window and verifies a
successful handshake with the bundled core. The x86 cross-build checks that
both executables are ELF32 before packaging. A 32-bit Haiku runtime test is
still required separately; a Linux cross-link is not proof of runtime behavior.

Live relay provisioning, cross-device delivery/read receipts, backup restore,
and secure-join must be exercised on Haiku with another Delta Chat client before
publishing a stable release. No real user account is used by CI.

## Remaining parity work

This first native implementation focuses on messaging. Parla's Webxdc sandbox,
voice/video capture and calls, inline media playback, sticker management,
location sharing, rich message search, multiple relay administration, encrypted
local vault and translations are not implemented here. Attachments can be saved
and opened with native Haiku applications. The upstream core is retained rather
than replacing protocol functionality with mocks or unencrypted shortcuts.

Branding is provisional. Update `src/Branding.h` and `data/Parla.rdef` together;
keep the settings folder ID stable to preserve existing profiles.

## Network dependency patches

The core's Iroh dependencies need explicit Haiku support in addition to the SSL
build fix. `scripts/patch-crates.py` downloads the exact versions/checksums from
upstream's lockfile, applies the small patches in `patches/crates`, and substitutes
local paths without changing dependency versions. Cargo still uses `--locked`.

- `iroh-quinn-udp`: select upstream's portable UDP implementation on Haiku, and
  fix its send return type. UDP remains functional; ECN, packet batching and
  segmentation offload are unavailable on this path.
- `netdev`: enumerate IPv4/IPv6 interfaces with Haiku's `getifaddrs`, including
  native address lengths and link flags.
- `netwatch`: compare interface snapshots every five seconds instead of using
  unsupported route sockets. Gateway discovery/automatic router port mapping
  is unavailable; ordinary sockets and Iroh relays remain enabled.

These patches do not alter TLS, certificate verification or message encryption.

The native CI uses LLVM LLD to link Rust artifacts: the bundled Haiku GNU linker
rejected a `syn` archive with “ELF section name out of range” during a procedural
macro link. The C++ frontend continues to use the native GCC toolchain. The x86
cross-build uses Haiku's official GNU cross-linker, which has linked the full
server successfully.
