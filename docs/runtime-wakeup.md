# Haiku polling stall investigation (0.1.5)

The device logs show `send_msg` returning a message ID in 7 ms. Later, local
`get_message_ids` and `get_full_chat_by_id` calls all time out. SMTP rejection
alone does not explain these local requests becoming unresponsive.

Mio 1.2.0 uses the portable poll selector and pipe waker on Haiku. Its
`modify_fds()` drained the shared pipe after registration, reregistration and
removal. A user/runtime wake token could still be pending after that drain.
The next idle poll had no readable notification and could sleep indefinitely.
There was also a race between taking the token and draining its pipe in select.

`patches/crates/mio-1.2.0.patch` leaves draining to select and serializes token
consumption with draining. Synthetic wake events remain readable even when
received together with socket readiness.

`bash scripts/test-runtime.sh` forces Haiku's poll+pipe backend on Linux. It
requires the checksum-pinned unpatched crate to fail with LOST WAKE, then patches
that crate and requires the same test to pass. The test also runs 128 TCP
exchanges on each of 2- and 4-worker Tokio runtimes, with partial writes,
blocking tasks, timers, idle wakeups and shutdown. The cross-built probe runs
inside the native x86_64 Haiku CI VM as well. This reproduces a real backend bug;
it does not substitute for testing the user's 32-bit machine and mail provider.

A separate scheduler bug dropped timed-out JoinHandles, detaching their tasks.
The core patch now aborts and joins those tasks before allowing shutdown to
finish. Parla starts profiles individually so starting all networking does not
hold the global account write lock.

Native account tests verify a deleted ID disappears from the menu and an
existing ID is marked, even when the core's selected-ID response is stale.
Selection writes are serialized, and stale account-list responses are ignored.

`tests/core_network.py` uses the actual server and loopback IMAP/SMTP fixtures.
It checks SMTP DATA acceptance and the stored delivered state, then repeats
scheduler stop/start and deletes an active account before using another one.
No real mail credentials or external recipients are involved.

The basic core connectivity integer only aggregates receiving connections.
Parla now displays the detailed incoming/outgoing connectivity report as native
text, including SMTP retry errors. Queued is never presented as delivered.
