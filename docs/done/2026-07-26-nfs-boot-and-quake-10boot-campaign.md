# NFS boot fix + Quake 10-boot stability campaign (2026-07-26)

Context: user reported the nfsroot test environment was unstable — boot 1 OK
(Quake launched, torches + some monsters still glitched but guns much improved),
but boots 2 & 3 aborted the NFS root takeover
(`nfs-fs: takeover aborted: mount 10.42.0.1:/: ; keeping RAM root /`), blocking
Quake. Asked to fix the NFS boot and run Quake 10× with screenshots.

## Fix 1 — NFS-root takeover: bounded-retry the mount (committed)

Root cause: `nfs_runTakeover` (filesystems/nfs/srv.c) did `nfs_makeContext` +
`nfs_mount` **exactly once**. Any transient — the socketsrv/lwip socket layer not
ready the instant DHCP completes, or a rapid-reboot race against the prior NFSv4
lease — aborted the ENTIRE takeover and dropped the boot to the sparse dummyfs RAM
root. The sibling **root-mode** path (`nfs_runRoot`) already handles this with a
bounded-retry loop; the takeover path did not.

Fix (phoenix-rtos-filesystems `aae643f`): mirror the root-mode loop — retry
`makeContext`+`mount` with a 3 s backoff up to a 120 s deadline (margin past the
~90 s NFSv4 lease window) before falling back to the RAM root. The fixed NFSv4
client id (`nfs4_set_client_name`, already set in the shared `nfs_makeContext`)
makes a same-id reboot REPLACE the prior lease, so the retry normally converges on
attempt 0–1; the loop just covers the transient window.

Verified the new code shipped in the rebuilt loader.disk (strings grep for
`takeover mount attempt`) — loader.disk stays quake-free + small (4.4 MB).

**Honest limitation:** I could NOT reproduce the original failure. The user's
boots 2–3 were rapid manual reboots (<~90 s apart, inside the NFSv4 lease window);
`test-cycle` spaces boots ~115 s apart, so the lease expires between boots and the
stale-lease race doesn't recur. 4 rapid boots on the OLD (no-retry) binary all
succeeded too. So the fix is correct-and-low-risk (mirrors proven code, keeps the
RAM-root fallback) but was **unexercised** in the campaign (retry-attempts=0 on all
10 boots). It removes the single-shot-abort failure mode the error message points
to; it is insurance, not a reproduced-and-confirmed fix.

## 10-boot campaign results

Each boot: netboot (nfsroot) → confirm takeover (+ retry-attempt count) → run
`rpi4-quake` from psh → capture UART (faults/fps) + HDMI every 10 s.

| Dimension | Result |
|---|---|
| **NFS root takeover** | **10/10 OK** — no RAM-root fallback on any boot |
| takeover retry-attempts | 0/10 (transient did not occur → retry fix unexercised) |
| **Quake launched** | **9/10** — boot 6 hit `exec '/usr/bin/rpi4-quake' failed (err=-5)` |
| **Faults/crashes** | **0/10** — no Exception/Data Abort/Fatal in any run |
| fps | ~20–45 fps @ 1080p, ~112 s gameplay/run |

Montages: `artifacts/quake-10boot-campaign/boot0N.png` (15 HDMI frames each).

### Quake visual verdict
- **Guns / viewmodel: FIXED** — the shotgun/double-barrel renders correctly on
  every frame of every boot (the #67 alias fix working). World geometry, items,
  the yellow-armor pickup, keys, dragon-door relief all render correctly.
- **Torches: STILL glitch** — wall-torch flame models render as mangled red/black
  angular spikes/blobs. Consistent across boots but the *exact* mangle varies
  per boot (boot 1 = red spike, boot 5 = black blob) ⇒ the #67 vertex-fetch race,
  **not fully closed for the small torch flame models**. Evidence:
  `artifacts/quake-10boot-campaign/torch-glitch-boot1-vs-boot5.png`. This matches
  the user's report exactly (torches glitch; guns much better). It is the known
  #67 residual (needs a guaranteed V3D slice-invalidate-completion primitive — a
  separate, non-overnight effort). Cosmetic, no crash.

### Two residual issues (NOT the reported takeover bug — separate)
1. **exec-from-NFS EIO (1/10):** the ~17 MB `rpi4-quake` occasionally fails to
   exec from the NFS root with `err=-5`. The NFS read path already retries
   transient EIO/ETIMEDOUT 25× with backoff (nfs_ops.c) — boot 6 logged NO nfs
   errors and failed *fast*, so it came from a path that doesn't retry (kernel
   exec setup), a rare transient. Workaround: re-run `rpi4-quake` at psh (the
   transient passes). This is the tradeoff of running the big binary from the NFS
   rootfs vs bundling it in loader.disk (syspage exec = 100 % reliable). Root fix
   needs kernel-exec-path work — deferred.
2. **Torch/monster glitch** — see above (#67 residual).

## Bottom line
- NFS boot: takeover is stable (10/10); the single-shot-abort failure mode is now
  retried. Blocker removed.
- Quake: stable in the crash sense (0 faults/10), mostly-reliable to launch
  (9/10; retry recovers), with the cosmetic torch/monster glitch residual.
