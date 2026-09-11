# AF_UNIX: scoping the upstream re-port (2026-09-11)

Upstream rebuilt UNIX sockets on an endpoint/channel model and **deleted `posix/unix.c`**. We have
1473 lines of local work in that file, so this is a **re-port, not a merge** — git reports the file
as `UD` (deleted by them, modified by us), plus 8 conflict hunks in `posix/posix.c` and 2 in
`posix_private.h`. Both repos were left untouched; this doc is the scope for doing it properly.

⚠ **AF_UNIX is the X11 transport on this target.** Every X client talks to Xphoenix over it, so a
regression here takes out the desktop half of the showcase. Any attempt at this needs an X11 test on
hardware, not just a boot.

## Upstream commits to absorb

| commit | what |
|---|---|
| `a8300c72` | posix/unix: rebuild UNIX sockets on endpoint/channel model |
| `6f44d973` | posix: add UNIX socket data channel |
| `a118e1ad` | lib/cbuffer: add `_cbuffer_peekAt()` |
| `1fcdcc57` | posix: clear `open_file_t` lock before closing underlying object |
| `c7348487` | posix/fdpass: don't take process lock in `fdpass_discard()` |

Plus phoenix-rtos-tests `e8d960a` (UNIX-socket regression tests) and `31324bc` (error codes); the
tests conflict is a single file, `libc/socket/unix-socket.c`, and will likely fall out of whichever
way the kernel side lands.

## Our local AF_UNIX work that must survive (5 substantive commits)

| commit | what it fixes | why it matters here |
|---|---|---|
| `69d9a448` | stop recycling socket ids; refuse a dead destination | lowest-free-id reuse let a stale bound pathname resolve to a **different live socket**, and `send()` delivered the payload there |
| `9c60b783` | `unix_accept4` must not use a connecting socket that may already be freed | use-after-free on the accept path |
| `381152c6` | `recvmsg` must report the control length it delivered | SCM_RIGHTS fd passing correctness |
| `7a52147c` | readiness-woken `poll()`/`select()` for AF_UNIX | **the snappy local X IPC change** — without it X is slow |
| `137ec58f` | SO_RCVBUF ceiling 64 kB → 256 kB | a 1.2 MB write otherwise crosses in ~300 blocking round-trips (see the AF_UNIX one-page-buffer note) |

`cc9a3544` (volume-gated cross-talk trace) is diagnostic and need not survive verbatim.

## How to approach it

1. **Check each local fix against the new model first.** Several may be obsolete by construction —
   an endpoint/channel design may not have a socket-id namespace to recycle at all (`69d9a448`), and
   upstream's `1fcdcc57` already touches the `open_file_t` lifetime area our `e88c8b75` addressed.
   Re-applying a fix for a bug the rewrite deleted is worse than not applying it.
2. **Keep `7a52147c` in mind as the performance-critical one.** If the new model does not wake
   `poll()` on readiness, X IPC regresses and the desktop feels slow — that is measurable
   (`docs/misc` has the X11 timing work) and should be measured, not assumed.
3. **Verify on hardware with X11**, not a boot test: `startx_gpu` plus a client (xterm/xclock), and
   compare against the current baseline of 90.2% non-black frames.
4. Take the tests repo's new regression tests — they are the cheapest check that the port is honest.

## Why it was deferred

The session that found this had a verified demo image delivered and a Pi busy with an allocator
hunt. A ground-up transport rewrite on the demo-critical path is not a thing to land unattended at
the end of a long session; the standing rule ("if it is not clearly mechanical, leave it and record
it") is exactly right here.
