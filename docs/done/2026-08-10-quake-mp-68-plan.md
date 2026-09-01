# Quake 1 multiplayer #68 — diagnosis plan

**KNOWN-ISSUES #68:** "Quake multiplayer hangs at the LOADING screen" (open;
single-player + demos work). Owner's #1-listed continue task. quakespasm port.

## Infra built (2026-08-10)

A headless host dedicated NetQuake server: `scripts/quake-mp-server.sh` builds
`external/quakespasm/Quake/quakespasm` (same codebase as the Phoenix client →
matching NetQuake protocol) and runs it `-dedicated` with the SDL dummy drivers,
binding `0.0.0.0:26000`. Verified running + bound; reachable from the Pi over the
netboot network at the host IP (10.42.0.1). The Pi client connects with
`connect 10.42.0.1`.

## Net-path analysis (what #68 is NOT, and where it is)

Read the client UDP path in `external/quakespasm/Quake/net_udp.c`:
- **The client UDP socket is NON-BLOCKING** (`UDP_OpenSocket` → `ioctlsocket(FIONBIO)`;
  the port confirms FIONBIO is implemented on Phoenix, task #26).
- **`UDP_Read` busy-polls** `recvfrom` every frame; `NET_EWOULDBLOCK → return 0`.
- So the client net loop does **not** use `poll()`/`select()`.

**→ #68 is NOT the Phoenix `poll()`-readiness stall** ([[project_nfs_poll_stall_fix]]
is a red herring here — that path never runs for quakespasm's UDP). The client
spins the LOADING screen busy-polling `recvfrom`; it hangs because the **signon /
precache message exchange never completes**, i.e. one of:
1. **recv:** the server's signon packets never come back from `recvfrom` on Phoenix
   (UDP delivery / source-addr / bind-vs-connect / large-datagram issue), or
2. **send/ack:** the client's reliable-message ACK send fails, so the server never
   advances to the next signon block, or
3. **large datagram / fragmentation:** signon precache blocks are large (~near the
   NET_MAXMESSAGE / MTU limit); if Phoenix lwIP drops/mis-handles a large or
   fragmented UDP datagram, the signon payload is lost.

Single-player works because it uses the loopback landriver (`net_loop.c`), no real
UDP — consistent with the fault being in the real-UDP signon exchange.

## ★ 2026-08-10 RESULT — #68 LOCALIZED to the NET_Connect silent-slist loop

Built a Pi client that auto-connects (via `id1/phoenix-connect.cfg` → `connect
<ip>`, added to pl_phoenix_main.c) + net-trace logging, ran it against the host
server. UART trace:
```
PHXNET68: boot connect -> 10.42.0.1
Playing demo from demo1.dem.
PHXNET68: NET_Connect host=10.42.0.1
PHXNET68: NET_Connect -> silent slist (SearchForHosts broadcast)
   <hangs here — no "slist done", no JustDoIt, no _Datagram_Connect>
```
**The connect reaches `NET_Connect` (net_main.c:415) and hangs in its silent
server-list phase:** `slistSilent = true; NET_Slist_f(); while (slistInProgress)
NET_Poll();` — the `while (slistInProgress) NET_Poll()` loop never terminates on
Phoenix (`slistInProgress` never clears), so the client spins there forever =
"hangs at LOADING." It never reaches `_Datagram_Connect` (the actual TCP-less
handshake) or signon. FitzQuake/quakespasm does a broadcast `SearchForHosts`
slist before *every* connect (to resolve the host into the cache).

**So #68 is NOT the signon exchange** (as first hypothesized) — it's earlier, in
the pre-connect host-discovery slist. Two candidate roots:
1. **UDP broadcast** send/recv broken on Phoenix lwIP (SearchForHosts broadcasts
   to the subnet; if the broadcast never goes out or no response, but the slist
   completion still hinges on it), or
2. the **slist timeout** never fires (`slistInProgress` is cleared on a
   `SetNetTime()`-based deadline in `_Datagram_SearchForHosts`/`NET_Poll`; if
   net_time doesn't advance or the deadline logic stalls, the loop is infinite).

Also confirmed: the connect competes with the demo loop (startdemos runs first),
but the connect DID run — the hang is the slist, not the demo.

## Next-step test/fix (a fresh heartbeat)

1. Add logging inside `NET_Slist_f` / `_Datagram_SearchForHosts` / `NET_Poll`:
   log `slistInProgress`, the deadline vs `net_time`, and whether the broadcast
   send + any response happen. One netboot cycle → is it (1) broadcast or (2)
   timeout?
2. Compare vs Linux-on-Pi4 (owner directive): a Linux quakespasm client slists
   fine → confirms a Phoenix UDP-broadcast/lwIP bug → fix it (lwip broadcast RX/TX
   or SO_BROADCAST).
3. Pragmatic parallel fix: for a **direct IP** `connect`, the slist is
   unnecessary — skip `NET_Slist_f` and go straight to `JustDoIt`/`_Datagram_Connect`
   with the given address (a small net_main.c change gated on "host is a literal
   addr"). That both fixes #68 for direct connects and sidesteps the broadcast
   dependency; the slist bug is then fixed separately for LAN discovery.

--- original plan below (signon hypothesis, now superseded by the slist finding) ---
## Next-step test (original)

1. Build a Pi quakespasm client that, instead of the hardcoded boot map, runs
   `connect 10.42.0.1` at startup (mirror the port's boot-command hook), with net
   logging: in `net_dgrm.c` (`Datagram_Connect` / the signon-driving
   `_Datagram_CheckMessage` / `NET_GetMessage`) and `UDP_Read`, log each read
   result + byte count + the signon state (`cls.signon`, `msg` reliable seq/ack).
2. Run `./scripts/quake-mp-server.sh start` on the host; netboot the Pi client;
   capture over UART where it stalls:
   - Does `UDP_Read` ever return the server's connection-accept + signon packets?
     (recv side works vs not.)
   - Does the client's ACK/reliable send succeed? (send side.)
   - What byte size are the signon blocks that arrive vs stall? (large-datagram.)
3. Compare vs Linux-on-Pi4 (owner directive): if a Linux client joins the same
   server fine and the Phoenix client doesn't, it's a Phoenix UDP/lwIP bug → fix
   at the socket/lwip level (e.g. large-datagram RX, or the connected-socket recv
   filter). If Linux also struggles → protocol/config.

The fix likely lands in the lwIP UDP path or a small net_udp/net_dgrm adaptation,
depending on which of (1)/(2)/(3) the capture shows.

## ★ 2026-08-10 RESULT #2 — connect + signon fixed; stall is now the BSP precache load

After the slist-skip (direct-IP → `JustDoIt`, skipping the hanging broadcast slist)
and the lwIP `getnameinfo` OOB crash fix (both shipped), the Pi client now
**connects and receives the full signon serverinfo**. Traced end-to-end against the
host dedicated server:
```
PHXNET68: handshake OK -- CCREP_ACCEPT, connection accepted
PHXNET68: recv LARGE actual=2507 header_claims=2507       <- FULL datagram delivered
PHXNET68: RELMSG delivered seq=0 total=2499 (frag len=2499 EOM)
PHXNET68: SVMSG signon=0 cursize=2499
PHXNET68:  svc=8 svc_print
PHXNET68:  svc=11 svc_serverinfo
PHXNET68: CL_ParseServerInfo loading 102 models, 81 sounds
PHXNET68: model[1/102] maps/start.bsp
   <- then nothing: no model[2/102], no "precache DONE", 0 faults >
```

**Two earlier hypotheses REFUTED, in order:**
- **NOT lwIP truncation / fragment-reassembly.** `recv LARGE actual=2507
  header_claims=2507` — the >MTU signon datagram is delivered whole. lwIP
  `IP_REASSEMBLY` works. (This overturns the "net_dgrm.c:330 trusts header length
  → Phoenix truncates the fragmented datagram" theory — no fix needed there.)
- **NOT a serverinfo parse desync.** The full 2499-byte reliable message is intact
  and parses cleanly through `svc_print` + `svc_serverinfo` into
  `CL_ParseServerInfo`.

**The stall is inside `CL_ParseServerInfo`'s precache LOAD** (cl_parse.c:402): the
client starts loading the 102 map models and stops at `model[1/102] maps/start.bsp`
— the map BSP, the first `Mod_ForName`. The earlier "0.2 fps + keepalives" symptom
= `CL_KeepaliveMessage()` (cl_parse.c:409/416) called between precache loads +
loading-plaque redraws, i.e. the client is alive and *inside the load*, not
disconnected. So #68 is now a **map-load** problem, not a net/protocol problem:
the MP client hangs (or is very slow) loading `maps/start.bsp` from NFS during
signon.

**Stuck-vs-slow NOT yet disambiguated** — confound: the 26 MB quakespasm client
itself exec-loads over NFS slowly (minutes), eating much of the capture window, so
`model[1/102]` near the end may be "out of window," not a hard hang. This overlaps
the known caches-off (TD-16) + NFS large-read slowness ([[project_large_binary_exec_hang]],
[[project_pi4_genet_rx_perf]], [[project_sdboot_largeexec_slowstart]] read-ahead
clustering).

### Next-step test (fresh heartbeat)
1. Timestamp the load logs (or log wall-clock at `boot connect`, `model[1]`,
   each `model[N]`) and run a LONG window (400 s+, split across Bash calls if
   needed) — does `model[N]` advance (slow, would finish) or is `model[1]`
   truly stuck (hang in `Mod_ForName(maps/start.bsp)` / an NFS read)?
2. If slow-but-progressing: this is the same class as the netboot large-load
   slowness — reuse the read-ahead / exec-clustering work; MP may already "work"
   with a big enough window. Consider SD-boot (local ext2, no NFS) to remove the
   NFS variable — an SD-boot MP client loads start.bsp from the fast local card.
3. If hard-stuck: gdb/QEMU or a source probe inside `Mod_LoadBrushModel` to find
   which BSP lump read hangs; compare the same `Mod_ForName(maps/start.bsp)` on
   the working single-player path (loopback) — SP loads the identical file, so the
   MP-specific factor (concurrent live UDP connection + `CL_KeepaliveMessage`
   network I/O during the load) is the prime suspect.

## ★★★ 2026-08-10 #68 FIXED — lwIP FIONBIO never enabled non-blocking sockets

Root cause found + fixed, HW-validated. The map-load "hang" was a **blocking
`recvfrom`**: `CL_KeepaliveMessage`'s `do { ret = CL_GetMessage(); } while (ret);`
drain loop parked in `recvfrom` waiting ~5s for the next stock `SV_SendNop`
unreliable nop (seq advanced ~1/5s, and there were **zero** `len=0` reads — the
signature of a blocking socket, not a stream). The client set the socket
non-blocking via `ioctlsocket(FIONBIO)`, which *returned success* but never took
effect.

**The bug (phoenix-rtos-lwip `port/sockets.c`):** `FIONBIO` is a write-only ioctl
(`_IOW`) — the flag is delivered in the IN payload. `ioctl_unpackEx` only fills
`out_data` when `IOC_OUT` is set, so for `FIONBIO` `out_data` is NULL and the flag
is in `in_data`. `socket_ioctl` passed `out_data` to `lwip_ioctl` for both
`FIONREAD` and `FIONBIO`; for `FIONBIO` that handed lwIP a NULL pointer → it read a
zero flag → left the socket **blocking**. So `FIONBIO` could *never* enable
non-blocking mode on any socket — masked everywhere data was always already
pending (handshakes, RPC), exposed only by polling an idle socket for
`EWOULDBLOCK`.

**Fix (lwip `fb8af75`):** split the cases — `FIONREAD` keeps `out_data`, `FIONBIO`
passes `in_data` (the actual flag). 9 lines. Benefits *every* non-blocking socket
consumer, not just Quake.

**HW validation (qmpfix cycle, netboot, 0 faults):** `dgrm Read len=0` now appears
**513×** (was 0 → socket genuinely non-blocking); the client loads **101/102**
models, hits `precache DONE`, advances `SignonReply` through **signon 4**, and is
**in-game** exchanging entity updates (unreliable seq 3000+). #68 (Quake MP hangs
at LOADING) is resolved.

**Full #68 chain, all fixed:** slist broadcast hang → skip-slist for direct IP;
lwIP `getnameinfo` OOB crash → bounds guard (pushed); **blocking `recvfrom` via
broken `FIONBIO` → this lwip fix**. The earlier lwIP-fragment-reassembly and
serverinfo-parse-desync theories were both refuted along the way (datagram
delivered whole; parser fine).

### Cleanup owed (next heartbeat)
- Fold the slist-skip into `tools/quakespasm-port/quakespasm-phoenix-port.patch`
  and strip all `PHXNET68` diag logs from `external/quakespasm`.
- The `phoenix-map.cfg` SP-boot branch in `pl_phoenix_main.c` is genuinely useful
  (single-player map boot) — keep it, drop only its `PHXNET68` label.
- Push lwip `fb8af75` to the org via the scrubbed cherry-pick flow (as with the
  getnameinfo fix — never raw-push the scrubbed lwip repo).
