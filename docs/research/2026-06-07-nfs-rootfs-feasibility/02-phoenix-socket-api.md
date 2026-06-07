# Phoenix userspace socket API — is it sufficient for libnfs?

**Answer: yes, end-to-end, from an arbitrary process.** The socket API is a real
BSD socket surface backed by message-passing to the lwIP server, reachable by *any*
process via a well-known path. Every primitive libnfs's sync API needs is present
and routes correctly for `AF_INET`/`SOCK_STREAM` (TCP) and `SOCK_DGRAM` (UDP).

## Architecture (how a socket reaches lwIP from another process)

1. **libc layer** — `libphoenix/include/sys/socket.h` declares the full BSD API
   (`socket/connect/bind/listen/accept/send/recv/sendto/recvfrom/getsockopt/
   setsockopt/shutdown/...`, lines 69-86). `recvmsg`/`sendmsg` carry a
   `__attribute__((warning("...not fully supported")))` (lines 77,80) — libnfs uses
   `writev`/`recvfrom`/`recv`/`send`, not `recvmsg`, so this caveat does not bite.

2. **syscall layer** — these are real kernel syscalls (not in-libc emulation):
   `phoenix-rtos-kernel/syscalls.c:1713 syscalls_sys_socket` →
   `posix_socket()` (`posix/posix.c:1758`).

3. **per-process fd + shared socket port** — `posix_socket` for `AF_INET`/`AF_INET6`
   calls `inet_socket()` (`posix/inet.c:269`), which sends `sockmSocket` to the
   socket server and stores the returned **port** in the new fd's
   `oid.port` (`posix.c:1789-1793`). The fd is per-process; the port belongs to the
   lwIP server process. Subsequent ops on the fd address that port — **so the socket
   lives in the lwIP process but is fully usable from the caller's process.**

4. **server** — `phoenix-rtos-lwip/port/sockets.c:1244-1250` creates the socketsrv
   port and registers it at the well-known path `PATH_SOCKSRV = "/dev/netsocket"`
   (`phoenix-rtos-kernel/include/sockport.h:35`). `socksrvcall()`
   (`libphoenix/sys/socket.c:56-66`) and `inet_socket` both reach it by
   `lookup(PATH_SOCKSRV)`. **Any process that can `lookup("/dev/netsocket")` can
   open sockets** — there is no "must be the lwip process" restriction.

## Cross-process verification (the load-bearing point)

- `inet_socket` returns a kernel **port** (`inet.c:286`); the fd→port mapping is
  generic IPC, so the calling process need not be the lwIP process.
- `posix_read`/`posix_write` (`posix.c:726/778`) route a socket fd through the
  generic `proc_read`/`proc_write` IPC path, which sends `mtRead`/`mtWrite` to the
  socket port. The lwIP socketsrv **handles `mtRead`→`lwip_read` and
  `mtWrite`→`lwip_write`** (`sockets.c:814-825`). So `read()`/`write()`/`writev()`/
  `readv()` on a socket fd all work cross-process — this matters because
  libnfs's `rpc_write_to_socket` uses `writev` (socket.c:415), and Phoenix's
  `writev` (`libphoenix/sys/uio.c:72`) is a userspace loop over `write()`.
- `poll()` is a kernel syscall → `posix_poll` (`posix.c:2419`). The poll-works
  conclusion rests on the **server side**, which is directly confirmed: the lwIP
  socketsrv answers `mtGetAttr/atPollStatus` via `poll_one()`→`lwip_select`
  (`sockets.c:826-833`, `79-110`). So a `poll()` on a socket fd resolves through the
  socket port's `atPollStatus` handler. **This is exactly what libnfs's sync loop
  needs** (`libnfs-sync.c` polls one fd). (OQ-1 below covers timeout fidelity under
  load, to be confirmed on HW.)

## libnfs-specific checks (the effort-estimate items)

| libnfs requirement | Phoenix evidence | Verdict |
|--------------------|------------------|---------|
| `socket(AF_INET, SOCK_STREAM)` (TCP for v3/v4) | `posix_socket`→`inet_socket` | OK |
| `socket(AF_INET, SOCK_DGRAM)` (UDP for v3 portmap/mount) | same path, `SOCK_DGRAM` | OK (v3 only; v4 avoids it) |
| `connect()` non-blocking + `getsockopt(SO_ERROR)` completion | `getsockopt`→`lwip_getsockopt` (`sockets.c:794-801`) handles `SO_ERROR` | OK |
| `fcntl(F_SETFL, O_NONBLOCK)` | `inet_setfl` issues `sockmSetFl` (`inet.c:319-327`); socketsrv handles it | OK |
| `writev` on socket | `writev`→`write`→`mtWrite`→`lwip_write` | OK (loop, see note below) |
| `setsockopt(TCP_NODELAY)` | routed to `lwip_setsockopt` | OK (lwIP honors or ignores) |
| reserved source-port `bind()` (<1024) | **no privileged-port check** in `sockets.c` | likely OK; mitigate with host `insecure` (see risk) |
| `getaddrinfo(server, ...)` (libnfs resolves the server arg) | implemented at `libphoenix/sys/socket.c:321`, routes `sockmGetAddrInfo` to lwIP; numeric IP literals resolved by lwIP's resolver (no DNS needed for `10.42.0.1`) | OK |

### `writev` nuance (small, documented)

libnfs writes a PDU as one `writev(fd, iov, niov)`. Phoenix `writev` issues `niov`
separate `mtWrite`/`lwip_write` calls and **stops early on a short write**
(`uio.c:95-96`), returning the partial byte count. libnfs already handles partial
writes (its `rpc_write_to_socket` advances `out.pos` by the returned count and
retries on the next `POLLOUT`), so this is *correct but chattier* (more IPC
round-trips per PDU). Not a blocker. If profiling shows it matters, a one-line
libnfs patch can coalesce the iov into one buffer + `send()` — defer.

### Reserved-port / NFS export security (risk 2)

libnfs's `rpc_bind_reserved` walks down from port 1023 looking for a free reserved
port and bails out of the reserved range on `EACCES` (`lib/socket.c` ~1480/1539).
Phoenix lwIP does **not** enforce the <1024 privilege check, so the bind should
simply succeed. To be robust regardless of lwIP policy, **export with `insecure`**
on the host (accept >1023 source ports) — see [03-host-side.md](03-host-side.md).
libnfs also exposes a knob to skip reserved-port binding if needed.

## TCP vs UDP, v3 vs v4

- **TCP** is fully supported (the only transport NFSv4 uses; also valid for v3).
- **UDP** is supported but only NFSv3's portmap/mount steps would use it, and even
  those can run over TCP.
- **NFSv4.1 sidesteps the portmapper entirely** (single port 2049, mount folded into
  the protocol). With v4 the *only* socket libnfs opens is one TCP connection to
  `host:2049` — minimal surface, lowest risk. **Recommend v4**, keep v3-over-TCP as
  fallback. Switch via `nfs_set_version(nfs, NFS_V4)` (`libnfs.h:361`) or URL
  `?version=4` (`libnfs.h:235`).

## Open questions (state explicitly, with how to resolve)

- **OQ-1 (poll timeout fidelity):** does `posix_poll` honor sub-second timeouts and
  return promptly on socket readiness under load? *Resolve:* T1 test app that times a
  `poll`+`recv` round-trip on HW.
- **OQ-2 (reserved-port bind actually succeeds on lwIP):** confirm `bind()` to e.g.
  port 800 returns 0. *Resolve:* T1 test app, or just run with `insecure` export and
  observe libnfs not needing the reserved port.
- **OQ-3 (large `mtRead`/`mtWrite` payload limits):** the message transport may cap
  single-message size; NFS reads up to `rsize` (often 64KB–1MB). *Resolve:* check the
  msg `o.size` cap and set libnfs `rsize`/`wsize` accordingly (libnfs lets you tune
  these); chunking is automatic. This affects throughput tuning, not feasibility.
- **OQ-4 (DHCP-bound before `nfs_mount`):** lwIP's DHCP completes asynchronously after
  the `lwip;genet` process launches; `nfs_mount` must not run until the interface has
  an address. **No existing component waits for DHCP** (confirmed: no
  `dhcp_supplied`/bound-wait anywhere in the tree), so the NFS client is the first
  that must. *Resolve:* the client polls interface status (the `netif_is_dhcp` /
  address state behind `devs.c:242`'s `%s%d_dhcp=%u`) with a timeout before mounting.
  Required for T1; hard requirement for option (c) (must fail gracefully when the
  network never comes up).
