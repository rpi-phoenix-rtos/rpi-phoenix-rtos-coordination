# M2 — display server `rpi4-kms`, Stage A (firmware planes / firmware panning)

Milestone M2 of the [new-lane plan](PLAN.md), from the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §4.2, §4.4,
§4.5, §4.7 and §5 (M2). It builds on [E3](E3-firmware-planes-vblank.md) (firmware plane wire format,
SMI vblank, scan-out range; **run on the Pi 2026-09-26 21:04, build 9 — results folded in §9.1**), [E5](E5-deferred-reply.md) (deferred
replies, blocking `read()` event delivery, `poll()` quantisation), [E1](E1-vm-object-export.md)
(`memExport`/`memUnexport`, merged), [M1](M1-async-render-server.md) (the render server's fence page
and memrefs) and [E7](E7-drm-userspace-build.md) (what libdrm/Mesa need from a KMS node).

**Status (2026-09-26, late evening):** design + compiling skeleton + test tool, **no M2 Pi cycle
yet**. E3 has run: SMI vblank, `SET_PLANE` for our own buffers and the scan-out range are all
answered (§9.1), so **Stage A is viable as designed and the `plane` backend is the default**;
`pan` stays as the fallback.
Code in [`tools/gpu-lane/kms/`](../../tools/gpu-lane/kms/) (coord repo; nothing in `sources/`),
`-Wall -Wextra -Werror` clean. The old lane is untouched: `rpi4-fb`, fbcon, the winsys and the
games keep working as they do. Nothing is committed; the server is not started by any boot script.

Evidence tags as in the research doc: **[read]** = read in source at the cited place;
**[measured]** = measured on this hardware (source cited); **[inferred]** = reasoning, not verified.

---

## 0. Decisions at a glance

| Question | Decision |
|---|---|
| Names | server binary `rpi4-kms`; card node **`/dev/kms`** (Stage A protocol is private); buffer namespace **`/kmsbuf`** on a second port; test tool `kmstest`. M3 adds `/dev/dri/card0` (`create_dev` creates the `dri` directory itself, `libphoenix/unistd/file.c:576-620` [read]) once the protocol carries marshalled DRM ioctls. Never `/dev/fb0`. `grep create_dev` in the devices tree shows no `kms`/`dri` names [read]. |
| Process | one process: **dispatch thread** (card port, prio 3), **buffer-namespace thread** (`/kmsbuf` port, prio 4), **vblank thread** (prio 1; IRQ-driven or polling, same code path), SMI ISR. One server mutex. Self-detaching (fork + pipe handshake, the `rpi4-v3d-async` pattern; psh has no `&`); `-f` foreground. |
| Vblank | SMI interrupt, GIC SPI 112 = **Phoenix IRQ 144** (E3: 60.01 Hz, jitter 59 µs, 0 spurious), ISR = MMIO ack + timestamp only. Auto-picked at start with fallbacks **irq → HVS frame counter (polled) → firmware `SET_VSYNC` → timer**; the winner is logged; a source silent for 250 ms or storming (> 2000 entries/s) falls back at run time. |
| Object model | one connector (HDMI-A) → one encoder (TMDS) → one CRTC for display 0; planes defined by the backend (pan: primary; plane: primary + cursor, `-o n` overlays). DRM ids, DRM property names and DRM value ranges (§3). |
| Buffers | **one contiguous pool** reserved at start (`MAP_CONTIGUOUS|MAP_UNCACHED`, placement retried until it ends **below 1 GiB — E6: the firmware scans nothing above**; 32 MiB for plane), sub-allocated exact-size (page granular), each BO **`memExport`ed** under `{buffer port, handle}` → client `open("/kmsbuf/<h>")` + `mmap(MAP_UNCACHED)`, zero-copy. Pan backend's flippable BOs are firmware-fb slots 1..n-1 handed out by **physical address** (firmware memory is `MAP_PHYSMEM`, which `memExport` refuses, E1 §2). |
| Commits | atomic (flattened per-plane states), legacy `PAGE_FLIP`, `SETCRTC` (current mode only). **One commit in flight per CRTC** (`-EBUSY`, as DRM). Applied at commit time when in-fences have signalled; **completes at the vblank after the firmware's update point** (E3 latch: the firmware writes pending plane state ~1.6 ms before each vblank; a call returning later than frame − 2 ms after a vblank is scanned one vblank later); `FLIP_COMPLETE` event to the committing file; outgoing framebuffer released only then. |
| Events | DRM wire layout (`struct drm_event_vblank` / `drm_event_crtc_sequence`, 32 B) read from the card fd; per-open queue (the `mtOpen` id); blocking `read()` parks in the server (bounded 2 s → `-EAGAIN`); `O_NONBLOCK` honoured. |
| In-fences | `v3da_fence_t` checked against `rpi4-v3d-async`'s fence page with one load (`-G` maps it) — **no IPC on the flip path**; if unsignalled the commit waits, polled every 500 µs (`-g`) and at each vblank. |
| Implicit sync | designed, not built in Stage A (§8): a per-flip "last-writer fence of this buffer" query to `rpi4-v3d-async` (~30–45 µs, E5) once the render server can import kms BOs (M3). |
| Backends | **plane** (firmware `SET_PLANE`, 60-byte value buffer through the `/dev/vcmbox` large-buffer call; **default** since E3: 60 flips/s, 0 missed, p50 100 µs) and **pan** (firmware-framebuffer panning — the old lane's mechanism; `-b pan`, fallback). |
| Console | fbcon keeps drawing into fb slot 0, which the server never hands out; pan shows slot ≥ 1, planes stack above the fb layer. Optional `-C` = `FBCONSETMODE(DISABLED)` while a plane shows (side effect: pl011-tty releases `/dev/kbd0`); optional `-B` = `FRAMEBUFFER_BLANK` the fb while the primary shows (E3 `stack` 3/4 round-trips). Display restored on client close/death, `DBG_QUIT`, SIGTERM, and by `rpi4-kms -R`. |
| fbdev emulation | **deferred** (§10.4). `rpi4-fb` keeps serving `/dev/fb0` = slot 0. |

---

## 1. Placement and coexistence with the old lane

* **Code:** `tools/gpu-lane/kms/` (coord repo), the M1 part-1 pattern: the devices `Makefile` includes
  every sub-`Makefile` below it, and an untracked file in a sibling flips `rebuild-rpi4b-fast.sh
  --scope auto` to `core` (M1 §1.1 [read]). Moves to `sources/phoenix-rtos-devices/video/rpi4-kms/`
  with a `Makefile` at the M2 gate.
* **Exclusive with every old-lane full-screen app** (any game, SDL2, vkQuake, the glamor X server):
  the in-process winsys aliases fb slots 1/2 as scan-out BOs and pans through the **raw mailbox FIFO**
  (`gpu/rpi4-v3d/mesa/v3d_phoenix_power.c:627`, E3 §3 [read]), which also races `/dev/vcmbox`. Guard:
  the server refuses to start when the firmware fb is not panned to y = 0 (someone else is flipping),
  `-F` overrides. Like M1, no M2 cycle runs a game or X.
* **Not a conflict:** `rpi4-fb` (`/dev/fb0`) and fbcon only read/write slot 0 and never pan
  (`video/rpi4-fb/rpi4-fb.c` [read]); the kdrive X server's `/dev/fb0` writes land in slot 0 too.
  While kms shows slot ≥ 1 (pan) or a plane above the fb (plane), their output is simply invisible.
* **Mailbox:** every firmware call goes through `/dev/vcmbox` (`libvcmbox`, the project rule); the
  server refuses to start without it.

## 2. Process and threads

```
   clients (kmstest now; libdrm-phoenix in M3)
   open("/dev/kms") -> mtOpen -> client id        read(fd) <- DRM events (parked until ready)
   ioctl(fd, HELLO); msgSend {port, id} raw ops   open("/kmsbuf/<h>") + mmap(MAP_UNCACHED) = BO pages
            |                                                 |
 +----------v------------------- rpi4-kms --------------------v-----------------------------+
 | dispatch thread (prio 3)        vblank thread (prio 1)           /kmsbuf thread (prio 4)  |
 |  msgRecv(card port) ->           wait: IRQ cond | HVS poll |      mtLookup/mtOpen/atMode   |
 |  lock -> op -> maybe apply       SET_VSYNC | timer                (refuses atSize, E1)     |
 |  (mailbox) / park -> unlock      -> lock -> complete armed flip                            |
 |  -> msgRespond                   -> apply fence-ready commit -> fire vblank events         |
 |                                  -> answer parked reads/waits (after unlock)               |
 |  ------------------ srv.lock (clients, BOs, fbs, CRTC state, parked) ------------------   |
 | SMI ISR (EL1, our pmap): ack SMICS / SMIDSW0/1, stamp cntvct, head++ -> cond             |
 +--------------------------------------------------------------------------------------------+
   MMIO: SMI 0xfe600000 (rw, 1 page), HVS 0xfe400000 (read-only witness + frame counter)
   firmware: /dev/vcmbox (display query, SET_VIRTUAL_OFFSET, SET_PLANE via XL, SET_VSYNC)
   render server (optional -G): /dev/v3d-async HELLO -> fence page mapped read-only, cached
```

* Parked requests follow E5's rules: one owner, claimed (removed) under the lock before anyone
  responds, answered **after** the lock is dropped (`proc_respond` reschedules after every response,
  E5 §1(a)), bounded (2 s), raw-only for `WAIT_VBLANK` (no payload window); a parked `read()` holds
  its small `o.data` window until answered (one per client). The port-death kernel fix (build 9) fails
  parked clients with `-EINVAL` if the server dies.
* The ISR (`kms_vblank.c:smi_isr`) disassembles to straight-line code, no calls (checked with
  `objdump`; built `-mno-outline-atomics`).

## 3. KMS object model

### 3.1 Objects and ids

| Object | Id | Stage A |
|---|---|---|
| connector | `0x20 + i` | HDMI-A, `type_id` 1, connected, 1 mode (the firmware's current mode), `mm_width/height` from EDID bytes 21/22, EDID blob when `GET_EDID_BLOCK_DISPLAY` (tag `0x00030023`, 136-byte value: block, display, 128 B; `raspberrypi-firmware.h:76`, `vc4_firmware_kms.c:122-127,1446-1457` [read]) returns a valid header |
| encoder | `0x30 + i` | TMDS, `possible_crtcs = 1` |
| CRTC | `0x40 + i` | display 0 only (`GET_NUM_DISPLAYS` logged; a second display is not driven in Stage A) |
| plane | `0x50 + 8·crtc + p` | p = firmware plane index: 0 primary, 1..6 overlay, 7 cursor (`vc4_firmware_kms.c:1868-1877`, E3 §1.2 [read]) |
| framebuffer | from `0x1000` | single plane, linear, 32 bpp |
| blob | from `0x10000` | MODE_ID, EDID, IN_FORMATS (server-owned); client MODE blobs |

Mode: `GET_DISPLAY_TIMING` (36-byte `struct set_timings`) → `drm_mode_modeinfo` (clock, h/v timings,
`vrefresh`, sync polarity from `TIMINGS_FLAGS_{H,V}_SYNC_POS`/`INTERLACE`, `vc4_firmware_kms.c:155-173`
[read]); refresh = clock/(htotal·vtotal). **On the bench firmware (`0x69fe27b1`) the tag answers all
zeros** (E3 `info timing idx=0 id=2 … clock_khz=0`) [measured], so the server **synthesizes** the mode:
the fb geometry (1920×1080), CEA-861 VIC 16 blanking (2200×1125) for 1080p, else ~10 %/4 % blanking,
and a pixel clock recomputed from the vblank rate the thread measures over its first 120 vblanks
(E3: 60.01 Hz), so `clock/(htotal·vtotal)` and `vrefresh` agree for clients that derive one from the
other. The blanking numbers are not the link's [inferred]; Stage A never programs them. Why the tag
answers zeros (overlay state? request layout?) is open (Q7). `GET_DISPLAY_CFG` answers
`max_pixel_clock 300 MHz` for both displays; `GET_NUM_DISPLAYS` = 1, display id 2 (HDMI0).

### 3.2 What the consumers need, and what Stage A answers

Consumer requirements: **[read]** where cited, else **[inferred]** from upstream knowledge.

| Item | Mesa kmsro/GBM/EGL | Vulkan `VK_KHR_display` | SDL2 KMSDRM | Xorg modesetting | Weston DRM | Stage A |
|---|---|---|---|---|---|---|
| `VERSION` name | `"vc4"` pairs with render node `"v3d"` (`pipe_loader_drm.c:394,397` [read]) | — | — | — | — | `KMS_DRIVER_NAME "vc4"` (M3 answers VERSION) |
| `CREATE_DUMB` / `DESTROY_DUMB` / PRIME export | yes (`renderonly.c:59,80,108` [read]) | swapchain images (research §4.7) | via GBM | yes | yes | ✅ (PRIME = memref + library `open()`) |
| `MAP_DUMB` | GBM dumb map (E7 §3.5) | — | `gbm_bo_write` | shadow fb | yes | ✅ (memref, not an mmap offset) |
| `GETRESOURCES`/`GETCONNECTOR`/`GETENCODER`/`GETCRTC` | kmscube | yes (`wsi_common_display.c` [read]) | yes (SDL2 grep [read]) | yes | yes | ✅ |
| `GETPLANERESOURCES`/`GETPLANE` | atomic apps | yes | optional | cursor/overlays | yes | ✅ (universal-planes rule honoured) |
| properties: plane `type`, `FB_ID`, `CRTC_ID`, `SRC_*`, `CRTC_*`, `IN_FORMATS` | kmscube atomic | all [read: `wsi_common_display.c:1694,2357,2982-2991`] | `ObjectGetProperties` [read] | [inferred] | [inferred] | ✅ |
| plane `IN_FENCE_FD`, CRTC `OUT_FENCE_PTR` | kmscube atomic, EGL sync | — | — | — | yes [inferred] | property ✅; value path = M3 (§8) |
| CRTC `MODE_ID`, `ACTIVE`; connector `CRTC_ID` | kmscube atomic | yes [read `:2929-2931`] | — | atomic mode only | yes | ✅ (current mode only) |
| connector `EDID`, `DPMS`, `link-status`, `non-desktop` | — | `EDID` [read `:411`] | — | yes [inferred] | yes [inferred] | ✅ (DPMS accepted, not acted on) |
| `GAMMA_LUT`/`DEGAMMA_LUT`/`CTM`, `Colorspace`, `VRR_ENABLED`/`vrr_capable`, HDR | — | optional, cleared if present [read `:2937-2941,400,419-421`] | `CrtcGetGamma/SetGamma` [read] | gamma optional | optional | absent (gamma_size 0) except `VRR_ENABLED` = 0 |
| plane `zpos`, `alpha`, `rotation` | — | — | — | — | yes [inferred] | plane backend only |
| `ADDFB`/`ADDFB2(+modifiers)`, `RMFB` | yes | `AddFB2WithModifiers` [read `:1805`] | both [read] | yes | yes | ✅ ADDFB2 linear; ADDFB = M3 library shim |
| `PAGE_FLIP` + `FLIP_COMPLETE` event | via EGL/GBM apps | — | yes [read] | yes | legacy path | ✅ |
| `ATOMIC` (`TEST_ONLY`, `NONBLOCK`, `ALLOW_MODESET`, `PAGE_FLIP_EVENT`) | kmscube | yes [read `:2911-2995`] | — | optional | yes (plane assignment by TEST_ONLY) | ✅ flattened (§11) |
| `SETCRTC` | — | — | yes [read] | yes | legacy | ✅ current mode only |
| `CURSOR`/`CURSOR2`/`MOVECURSOR` legacy | — | — | yes [read] | yes | — | M3 library → cursor plane (plane backend) |
| `WAIT_VBLANK`, `CRTC_GET/QUEUE_SEQUENCE` | — | `drmCrtcQueueSequence` [read] | — | yes [inferred] | — | ✅ |
| `SET/DROP_MASTER`, `AUTH_MAGIC` | — | — | yes [read] | yes | yes | accepted no-ops |
| caps | — | — | `drmGetCap` [read] | many | many | `DUMB_BUFFER` 1, `PRIME` 3, `TIMESTAMP_MONOTONIC` 1, `CRTC_IN_VBLANK_EVENT` 1, `ADDFB2_MODIFIERS` 1, `CURSOR_W/H` 64, `DUMB_PREFERRED_DEPTH` 24, `ASYNC_PAGE_FLIP` 0, `PAGE_FLIP_TARGET` 0, `SYNCOBJ` 0 (render node) |
| client caps | — | `UNIVERSAL_PLANES`, `ATOMIC` [read] | — | `UNIVERSAL_PLANES` | both | `UNIVERSAL_PLANES`, `ATOMIC`, `ASPECT_RATIO` accepted; `STEREO_3D`, `WRITEBACK` refused |
| `poll()` on the card fd | — | yes | yes | yes | yes | works but **quantised to 20 ms** until the kernel `block_ms` change (E5 §5 item 1–2) — M4/M6 dependency |

Formats: plane backend `XRGB8888` is **confirmed** (E3 snapshots: `VC_IMAGE_XRGB8888` with u32
`0x00RRGGBB` shows the colour bars in the right order). Pan = the firmware fb's own format; plo sets pixel order 1 and fbcon found that R sits in the
**low** byte (`pl011-tty.c` palette comment [read]) → **`XBGR8888`** [inferred mapping; the first
cycle's snapshot checks it]. Plane backend = `XRGB8888` (`VC_IMAGE_XRGB8888` 44) and `ARGB8888` (43)
(BSD-3 `vc_image_types.h`, E3 §1.1). Mesa GBM defaults to XRGB8888, so **pan-mode GBM clients need
either XBGR8888 support in GBM/v3d (it has it for scan-out [inferred]) or the plane backend** — an M3
item.

## 4. Buffers

### 4.1 The pool (plane backend; non-scanout BOs in pan mode)

* One `mmap(MAP_ANONYMOUS|MAP_CONTIGUOUS|MAP_UNCACHED)` at start: 32 MiB default for plane (three
  1080p XRGB buffers = 23.7 MiB fit; `vm_objectContiguous` rounds to a power of two anyway, E3 §1.4),
  4 MiB for pan. **Placement:** the buddy allocator has none, so the server takes up to 16 pool-sized
  blocks until one ends at or below **1 GiB** and returns the rejects. **E6 settled the limit:** a raw
  PA of `0xf8000000` went into the display list as `0xf8000000` = bus alias 3 of `0x38000000` (the
  firmware keeps only the low 30 bits; `0x38000000` is the VideoCore memory base, `info vc_mem`) and
  the snapshot shows near-black noise instead of the cyan/black checker [measured]; the same buffer
  shape below 1 GiB is correct with either convention. `kms_bus_addr` therefore refuses any scan-out
  range ending above 1 GiB for both conventions (`-m` can only lower the limit). Availability is not a
  problem: E3 `contig` got a single 256 MiB block at `0x10000000` and 24 of 32 8 MiB chunks below
  1 GiB on a freshly booted 4 GB board [measured]. Returning the rejects (safe since the E1 §6 object-tree fix, kernel `d0fb0ca9`). Contiguity is checked with
  `va2pa` of the first and last page. A PA is never truncated: out-of-convention buffers are refused
  (`kms_bus_addr`).
* **Sub-allocation:** exact size (pitch × height, pitch rounded to 64 B [inferred safe for the HVS],
  page-rounded), lowest-fit among the live pool BOs. No power-of-two rounding per buffer (the §1.4
  waste of today's per-BO `MAP_CONTIGUOUS`).
* **Export:** `memExport({buf_port, handle}, va, size)` **before** the handle is returned (E1: an
  early importer would create a shadow object); the namespace refuses `atSize` for the same reason.
  One memory type per export: the pool is uncached (Normal-NC = write-combine), so clients **must**
  `mmap(..., MAP_UNCACHED, fd, 0)`; any other type fails with `-EINVAL` (E1 `vm_objectMapCheck`;
  `kmstest pool` checks it). The buffer namespace is its own port because the card port answers
  `mtOpen` with a per-open id, which would rewrite a buffer descriptor's oid (E1 §1 [read]).
* **Access:** DRM semantics (a dumb BO is private until PRIME-exported). Stage A logs, once, an open by
  a process other than the owner that was not PRIME-exported, and allows it — the pid the kernel
  stamps on a path-resolution message (`proc/msg.c:386` [read]) has not been observed on hardware
  yet. Enforce at the M2 gate.
* **Zeroing:** every BO is zeroed at hand-out (contiguous pages are not zeroed; Linux zeroes dumb BOs).
* **Lifetime (the C1 rule, research §4.9):** BO refs = the owner's handle + one per framebuffer; FB
  refs = the user's (until `RMFB` or death) + one per plane showing it or about to. A flip takes a
  reference on the incoming FB at commit and drops the outgoing one **when the flip completes at a
  vblank**, so scanned memory is never freed early. `memUnexport` on the last reference withdraws the
  name at once; a client mapping that outlives it keeps the pages alive through the window's
  reference on the pool — so a stale client write lands in pool memory, never in memory the kernel
  recycles. **Residual:** the server cannot see whether a client still maps a withdrawn window, so a
  pool range can be reused while a buggy client still writes it (another client's buffer, not a
  kernel structure). HVS scan-out is read-only DMA: a freed scanned page shows garbage, never
  corrupts memory.

### 4.2 Pan-mode flippable BOs

A mode-sized, 32-bpp `CREATE_DUMB` without `KMS_DUMB_POOL` gets firmware-fb slot 1..n-1
(n = virtual height / height = 3 on the bench: `virt_h 3240`, E3 §1.4 [measured]). The memref is
`KMS_MEM_PHYS` (map `MAP_PHYSMEM|MAP_UNCACHED`, as `rpi4-fb` and the M1 memrefs do). Slot 0 is never
handed out (fbcon, `/dev/fb0`). Two slots = exactly double buffering; a third mode-sized request falls
back to the pool (mappable, not scannable by pan: its flip gets `-EINVAL`).

## 5. Commits

* **Forms:** `ATOMIC` (flattened: a header in `i.raw` + `kms_atomic_plane_t[n]` in `i.data`, each a
  complete plane state incl. an in-fence), `PAGE_FLIP` (primary plane, raw-only, carries an in-fence),
  `SETCRTC` (full-screen primary, or fb 0 = all planes off / console). Stage A cannot change the
  firmware's mode: a `MODE_ID` or `SETCRTC` mode that is not the current one is `-EINVAL`.
* **Validation:** the plane exists in this backend; the FB belongs to the caller (Stage A); the
  backend's `check` (pan: an fb slot, full screen, no scaling; plane: format, 16-bit limits, bus
  address reachable, source rectangle inside the FB, rotation subset). `TEST_ONLY` stops here.
* **One commit in flight per CRTC:** a commit while one is pending (fence-waiting or armed) gets
  `-EBUSY`, like DRM's page flip. Clients flip again on the event.
* **Apply:** if every in-fence has signalled (fence page load), the backend's firmware calls run
  immediately in the dispatch thread (one `SET_VIRTUAL_OFFSET`, or one `SET_PLANE` per plane in the
  commit); else the commit waits and the vblank thread re-checks every `-g` µs (500) and at each
  vblank. The reply says whether it was applied and the mailbox latency.
* **Completion model (E3-measured for `SET_PLANE`):** `SET_PLANE` does **not** block (p50 100 µs;
  `flipmax` issued 12 443 calls/s) and does **not** write the hardware list synchronously: a call made
  right after the vblank IRQ appears in the HVS display list **15.04 ms later** (48/48 samples,
  p50 15 041 µs, max 15 051 µs) [measured] — the firmware writes pending plane state at a fixed point
  ≈ 1.6 ms before the next vblank, and the HVS scans it from that vblank. So at arm time the server
  computes the commit's **target vblank**: `seq + 1` if the last mailbox call returned more than
  `-L` µs (default 2000) before the expected next vblank, else `seq + 2`. At the target vblank the new
  state becomes current, the outgoing FBs are unreferenced and `FLIP_COMPLETE` (sequence, timestamp,
  `crtc_id`, user data) is queued. If the vblank thread has not yet processed an ISR vblank when the
  commit arms, `seq` and the stamp are one frame stale and the rule errs **late, never early** (a
  frame-late event costs a frame; an early one would let the client draw into a scanned buffer).
  E3's `plane result` confirms the model end to end: flipping right after each IRQ gave 1201 flips in
  20 s, `missed=0`. Pan uses the same rule [inferred: its update point was not measured; the old
  lane's flicker-free double buffering after the single-buffer fix is the evidence that it latches at
  vblank, memory `project_pi4_quake_flicker_vcmbox`].
* **Errors after acceptance:** DRM has no channel; the event still fires (no client waits forever),
  the first five failures are logged (`KMS apply FAIL`) and counted (`apply_errors`).
* **Blocking vs `NONBLOCK`:** Stage A treats every commit as non-blocking; libdrm-phoenix (M3) waits
  for the event when a caller did not ask for `NONBLOCK` [design].
* **Async flips:** `DRM_CAP_ASYNC_PAGE_FLIP = 0` in Stage A. E3 `flipmax` (12 443 non-blocking
  `SET_PLANE`/s, `missed=0`, HVS still at 59–60 Hz) shows the firmware coalesces to "last write before
  the update point wins": a "mailbox"-style present (replace the pending buffer without waiting) is
  possible, true tearing flips are not. It also means the 44 vs 141 fps fkms/KMS gap (research §4.5)
  is **not** flip latency (E4-lite).

## 6. Vblank

* **ISR** (`smi_isr`): read `SMICS`; if no INT bits → clear `SMICS`, count spurious, do not wake;
  else `SMICS = 0`, read `SMIDSW0`; old firmware (no `0xabcd` signature) = one event for all displays;
  new: bit 0 of `SMIDSW0`/`SMIDSW1` = display 0/1, acked by writing `0xabcd0000`; for display 0 stamp
  `cntvct` into a 64-entry ring and bump `head`. Exactly the convention E3 §1.2 documents
  (`vc4_firmware_kms.c:1225-1273` [read]); `SMICS = 0` before `interrupt()` as Linux does.
* **Sequence:** `crtc.seq += vblanks since the last wake` (ISR head delta, HVS 6-bit counter delta, or
  1), so missed wake-ups still count frames.
* **Timestamps:** `cntvct` → `CLOCK_MONOTONIC`-shaped ns through one calibration pair taken at start
  [inferred: Phoenix's monotonic clock and `cntvct` do not drift apart measurably over a test; the
  `event_delivery_us` column shows any offset]. HVS source: stamp = when the change was seen
  (`usleep(250)` granularity, likely ~1 ms [inferred]); `SET_VSYNC`: stamp = call return; timer: not
  synchronised to the display at all — flips complete on the timer's edges, so **tearing and a wrong
  completion time are possible**; logged as such.
* **`SET_VSYNC` fallback** holds `/dev/vcmbox` for up to a frame per call; thermal and our own flip
  calls wait behind it. Last resort, never on the flip path otherwise.
* **Waits and events:** blocking `WAIT_VBLANK` (relative/absolute) parks raw-only and is answered at
  the target vblank; the `_DRM_VBLANK_EVENT` form and `CRTC_QUEUE_SEQUENCE` (`RELATIVE`,
  `NEXT_ON_MISS`) queue an event; `CRTC_GET_SEQUENCE` returns the count and last timestamp.

## 7. Events and `read()`

* Wire format = libdrm's MIT `drm.h`: `struct drm_event {type, length}` + `drm_event_vblank`
  (user data, `tv_sec`, `tv_usec`, sequence, `crtc_id`) or `drm_event_crtc_sequence` (user data,
  `time_ns`, sequence), both 32 bytes (`_Static_assert`ed). `drmHandleEvent` reads the fd and parses
  these unchanged.
* Per-open queue (64 events) keyed by the `mtOpen` id; overflow drops and counts.
* `read()`: whole events up to the buffer size; empty + `O_NONBLOCK` (`i.io.mode`, the file's status
  flags, `proc/name.c:629` [read]) → `-EAGAIN`; empty + blocking → parked until an event, **at most
  2 s, then `-EAGAIN`** (a parked Phoenix client cannot be interrupted, E5 condition 1; callers loop).
  E5 measured this path at **19.9 µs** wake p50.
* `poll()`: `atPollStatus` answers readiness; no `block_ms` (E5: the kernel only passes it for
  `ftInetSocket`), so a poller sees events 0–20 ms late. Stage A clients block in `read()`.

## 8. Fences and implicit sync

* **In-fences (explicit):** a commit carries `v3da_fence_t` (slot, queue, gen, seqno). With `-G` the
  server is a client of `rpi4-v3d-async` (HELLO → fence page memref, mapped read-only **cached**, the
  only memory type any mapping of that page may use, E5 §1(d)). Signalled ⇔ `slot.gen` matches and
  `completed[queue] ≥ seqno` (acquire loads). A reassigned slot (client gone) or a server that set
  `V3DA_FP_EXITED` counts as signalled — the display never wedges on a dead renderer. Without `-G` a
  commit with an in-fence is `-ENODEV`. **No IPC on the flip path.**
* **`IN_FENCE_FD` (M3):** libdrm-phoenix turns a sync-file fd into the fence it wraps
  (`v3da` `SYNCOBJ_IMPORT` is the emulation M1 already has) and puts it in the plane state
  [design].
* **Implicit sync (Xorg modesetting/glamor flips carry no fence, research §4.5):** the kms server
  needs "the last GPU write to this scan-out buffer". Stage A cannot ask yet: `rpi4-v3d-async` does
  not import kms BOs (`BO_IMPORT` is `-ENOSYS`, M1 §10). Design for M3, in order of preference:
  (1) additive v3da op `BO_LAST_FENCE(import handle)` → the fence, then gate on the fence page like an
  explicit fence (one ~30–45 µs query per flip, E5; zero waiting IPC); (2) the E1-exported "resv page"
  (per exported buffer last-writer fence, research §4.5) to remove even that query — only on E1, never
  on `MAP_PHYSMEM`. `BO_WAIT` with `timeout_ms = 0` already exists as a non-parking poll (returns
  `-ETIMEDOUT` while busy, `v3da_sched.c:340-343` [read]) and is the fallback.

## 9. Plane backends

Interface (`kms.h: kms_backend_t`): `init` (sets the plane mask), `formats`, `check`, `apply`
(returns the mailbox status and latency; `fb == NULL` disables), `restore`.

| | plane (default) | pan (`-b pan`, fallback) |
|---|---|---|
| mechanism | `SET_PLANE` (tag `0x48015`, 60 B) via `vcmbox_callXL`; unset = display + plane id only | `SET_VIRTUAL_OFFSET(0, slot·height)`; checks the firmware's answer equals the request |
| planes | primary + cursor (+ `-o n` overlays) | primary |
| buffers | pool BOs (OID memref) | fb slots 1..n-1 (PHYS memref) |
| scaling / alpha / rotation | firmware scaling, per-plane alpha, rotate-180 / reflect-x/y (transform bits 1/16/17) | none |
| layer | primary at layer 0 (the fb answers `GET_LAYER` −127; E3's primary at 0 covered it), overlays by zpos, cursor on top | — |
| bus address | raw PA (default; firmware ORs in alias 0x8) or `-c` = `0xC0000000|PA`; both work, both **below 1 GiB only** | — |
| needs | `/dev/vcmbox` XL (`79a4212`) + a pool | fb `virt_h ≥ 2·height` |
| restore | unset every plane it touched (+ unblank with `-B`) | pan to 0 |

### 9.1 E3 results (Pi, build 9, `artifacts/rpi4b-uart/rpi4b-uart-20260926-210421-e3-kms.log` + `artifacts/hdmi/20260926-2104..2109-e3-kms-*.png`) and what they decide

| E3 line | Result [measured] | M2 decision |
|---|---|---|
| `vcmbox xl=1 … setplane_transport=xl` | the large-buffer call is in build 9 | plane backend + EDID usable |
| `display … num=1 id=2 timing=0 fb_pa=0x3d3b2000 virt_h=3240 fb_layer=-127(fw)` | one display (HDMI0, id 2); `GET_DISPLAY_TIMING` unanswered; fb PA differs per boot (`0x3d3fd000` on another) | synthesized mode (§3.1); never hard-code the fb PA |
| `vblank poll … smi_events=61 … gic_pending_samples=28`, `vblank irq count=601 hz=60.01 jitter_us=59 min_us=16665 max_us=16668 spurious=0`, `mbox_vsync … hz_est=60`, `verdict irq=yes poll=yes hvs=yes` | the firmware raises SMI with the new per-display protocol (`dsw0=0xabcd0001`) **with the vc4-fkms-v3d overlay present**; IRQ 144 is delivered; all three sources work | vblank `irq` as designed; `hvs`/`fwvsync`/`timer` remain fallbacks. Whether SMI needs the overlay is still Cycle B's question (the bench keeps the overlay, so not blocking) |
| `stack step=1..5` | green plane 1 and blue plane 0 (layer −127) both enter the list next to the fb (`entries 2→3`); `FRAMEBUFFER_BLANK` removes the fb (`fb_in_dlist=0`) and unblank restores it; unsetting leaves the fb alone (`entries=1`); `restore done fb_in_dlist=1` | planes **add** to the fb element (Q1 answered: the fb is a separate bottom element); fb blank/unblank is a usable handover (`-B`, off by default until an M2 cycle watches it) |
| `plane first … first_lat_us=92 primary_in_dlist=1 primary_word=0xac000000 overlay_in_dlist=1 layer=0` | our pool-style buffers (`0x2c000000`, `0x2c800000`) in the list; the firmware turned raw PA into alias 0x8 | raw PA default |
| `plane latch samples=48 notseen=0 latch_us_p50=15041 max=15051` | fixed firmware update point ≈ 1.6 ms before vblank | target-vblank completion rule + `-L 2000` guard (§5) |
| `plane result secs=20 flips=1201 flips_per_s=60 … missed=0 setplane_us_p50=100 p99=2074 max=5828 overlay_us_p50=75 p99=95 errors=0` | primary + moving overlay every frame, no miss; two calls per frame ≈ 0.2 ms of 16.7 ms | **Stage A viable** (E3 §5 criteria 1–3 met; p99 2.07 ms is at the 2 ms line — rare mailbox contention, harmless under the 14 ms budget); overlays can be exposed (`-o`) |
| `flipmax result flips_per_s=12443 … setplane_us_p50=78 p99=112 max=6832` | `SET_PLANE` never blocks to vblank | commits apply in the dispatch thread; mailbox-style present possible, no tearing flips (§5) |
| `range try where=fb1 conv=c0|00 … in_dlist=1` | firmware fb memory scans with both conventions | control OK |
| `range try where=lo conv=c0 … dlist_word=0xec000000`, `conv=00 … 0xac000000` + snapshot (blue/yellow checker correct) | our memory below 1 GiB scans with both | pool below 1 GiB, either convention |
| `range try where=hi conv=00 pa=0xf8000000 … dlist_word=0xf8000000` + snapshot `…-210827-…` (near-black noise, not the cyan/black checker) | **above 1 GiB is NOT scannable**: the firmware keeps the low 30 bits (→ `0x38000000`, VC memory) | `kms_bus_addr` refuses > 1 GiB for both conventions; pool placement below 1 GiB is mandatory (was already the default) |
| `-b c0 plane 8`: `flips=481 flips_per_s=60 missed=0 p99=1431` + snapshot `…-210845-…` (colour bars + checker + magenta box correct) | the `0xC0000000` alias is equally good | keep raw default (Linux parity), `-c` available |
| `contig single mib=256 ok=1 pa=0x10000000`, `chunk8 total=32 below_1g=24 1g_to_4g=8` | plenty of contiguous memory below 1 GiB early in a boot | a 32 MiB pool is cheap; larger pools (YUV video, M3 triple buffering) feasible |

So of the pre-registered E3 §5 table: "Stage A viable as designed" holds (1–4 all met), plus two rows
that change details: "`range hi` shows other memory → pool below 1 GiB" and a latch that is neither
≈ 0 nor ≈ one frame but a fixed update point (handled by the target-vblank rule).

**Pre-registered mapping (written before E3 ran; kept for the record — the §9.1 rows are what applied):**

| E3 result | M2 consequence |
|---|---|
| `SET_PLANE` of our buffers in the HVS list and visible, `plane result` at refresh, p99 ≤ 2 ms | plane backend becomes the default at the M2 gate; overlays exposed if two planes per frame fit |
| `SET_PLANE` accepted only for firmware memory (`range fb1` works, `range lo` does not) | pan stays the default; plane backend limited to fb-slot buffers (possible: `check` accepts slot BOs), overlays via Stage B |
| `SET_PLANE` not honoured at all | pan only; Stage B (native HVS lists) for planes |
| only `c0` works | `-c` becomes the default convention; pool placement below 1 GiB mandatory |
| `range hi` works | `-m hi` default; pool may sit anywhere below 4 GiB |
| SMI IRQ at refresh | vblank `irq` (as designed) |
| no SMI event, HVS frames advance | `hvs` source (auto-picked); Cycle B asks whether the overlay gates SMI |
| `plane latch` ≈ one frame | move `SET_PLANE` to a worker thread; completion = call return |
| `flipmax` ≫ refresh | `ASYNC_PAGE_FLIP` can become 1 for the plane backend |
| fb blank/unblank round-trips (`stack` 3/4) | console handover by `FRAMEBUFFER_BLANK` becomes an option beside `FBCONSETMODE` |

## 10. Console, old-lane `/dev/fb0`, crashes

1. **fbcon** draws into slot 0 of the firmware fb (plo's graphmode PA). Pan shows slot ≥ 1, so the
   console is off-screen but keeps its content; planes stack above the fb layer. Nothing has to stop
   fbcon for Stage A to be correct. With `-C` the server additionally calls `FBCONSETMODE(DISABLED)`
   on `/dev/tty0` (else `/dev/console`) while any plane shows and `ENABLED` when none does — the
   existing DOS-style switch the SDL2 glue uses (`pl011-tty.c:464-514`, `SDL_phoenixvideo.c:86-96`
   [read]); fbcon then renders into its shadow and blits it back on enable. **Side effect:** DISABLED
   also makes pl011-tty release `/dev/kbd0` to the app (`kbdReleased`), so `-C` is off by default.
   With the plane backend the fb stays a separate element **under** the primary (E3 `stack`), so the
   HVS still fetches it every frame; `-B` blanks it while the primary shows and unblanks on the way
   out (E3 steps 3/4 proved the round trip) — the way Linux fkms treats it [inferred saving: one
   1080p32 layer ≈ 0.5 GB/s of DRAM reads].
2. **Restore paths:** a client's close/death takes its planes off (console back); `SETCRTC(fb 0)` does
   the same on request; `DBG_QUIT`/SIGTERM restore (unset planes / unblank / pan 0 / fbcon on) before
   exit; `rpi4-kms -R` does it after a crash (like `kmsprobe restore`, which E3 ran twice cleanly).
3. **Server crash:** pan mode: the display stays panned to a slot (firmware memory, never freed —
   harmless, stale picture) until `-R`. Plane mode: planes keep pointing at pool pages that the kernel
   frees at exit → the HVS scans recycled memory (garbage on screen, read-only, no corruption) until
   `-R` [inferred]. M3 needs a supervisor or a boot-script `-R` on server death.
4. **fbdev emulation (deferred):** `/dev/fb0` semantics (read/write + `RPI4FB_GETMODE`) on the primary
   plane, so old clients keep working during migration. Needs a decision on who owns `/dev/fb0` (a
   second `create_dev("fb0")` fails while `rpi4-fb` runs); the natural step is at migration: kms
   registers `fb0` itself when started instead of `rpi4-fb`, backed by a pool BO shown on the primary
   plane while no KMS client is active.
5. **What happens to the old lane's `rpi4-fb` when kms owns the display:** it keeps running and serving
   slot 0. Its readers see the console image, its writers draw into an invisible slot. No corruption
   either way. It is retired at migration (item 4).

## 11. Client protocol

Header: [`tools/gpu-lane/kms/kms_proto.h`](../../tools/gpu-lane/kms/kms_proto.h). Transport as
`rpi4-v3d-async` (M1 §10): `open("/dev/kms")` → per-open client id; `ioctl(fd, KMS_IOC_HELLO)`
(proto, client id, backend, vblank source, buffer port, mode, refresh); then raw `mtDevCtl` to
`{port, id}` with `{magic "KMS1", op, flags, u[48]}` in `i.raw` and `{err, op, u[56]}` in `o.raw`;
arrays in `i.data`/`o.data` (keep page-aligned, E5); events by `read()`.

| Op | DRM ioctl it serves (M3 marshalling) | Payload |
|---|---|---|
| `GET_CAP`, `SET_CLIENT_CAP` | `GET_CAP`, `SET_CLIENT_CAP` | raw |
| `SET_MASTER`/`DROP_MASTER`/`AUTH_MAGIC` | same | raw, no-ops |
| `GET_RESOURCES` | `MODE_GETRESOURCES` (+ `drmIsKMS`) | raw; FB ids in `o.data` |
| `GET_CONNECTOR` | `MODE_GETCONNECTOR` | raw; `drm_mode_modeinfo[]` in `o.data` |
| `GET_ENCODER`, `GET_CRTC` | `MODE_GETENCODER`, `MODE_GETCRTC` | raw (+ mode in `o.data`) |
| `SET_CRTC` | `MODE_SETCRTC` | raw |
| `GET_PLANE_RESOURCES`, `GET_PLANE` | same | raw |
| `GET_PROPERTIES`, `GET_PROPERTY`, `GET_BLOB`, `CREATE_BLOB`, `DESTROY_BLOB` | `MODE_OBJ_GETPROPERTIES`, `MODE_GETPROPERTY`, `MODE_GETPROPBLOB`, `MODE_CREATEPROPBLOB`, `MODE_DESTROYPROPBLOB` | arrays in `o.data`/`i.data` |
| `CREATE_DUMB`, `MAP_DUMB`, `DESTROY_DUMB` | same (`GEM_CLOSE` too) | raw; memref instead of an mmap offset |
| `PRIME_EXPORT` | `PRIME_HANDLE_TO_FD` | raw → memref; the library `open()`s `/kmsbuf/<h>` |
| `ADDFB2`, `RMFB` | `MODE_ADDFB2` (`ADDFB` shimmed), `MODE_RMFB` | raw |
| `PAGE_FLIP` | `MODE_PAGE_FLIP` | raw (+ in-fence) |
| `ATOMIC` | `MODE_ATOMIC` flattened: libdrm-phoenix groups the obj/prop/value arrays into complete per-plane states + CRTC `MODE_ID`/`ACTIVE` | `kms_atomic_plane_t[]` in `i.data` |
| `WAIT_VBLANK`, `CRTC_GET_SEQUENCE`, `CRTC_QUEUE_SEQUENCE` | same | raw (blocking form parks) |
| `DBG_BO_CHECKSUM`, `DBG_STATS`, `DBG_QUIT` | — | raw |

M3 items on the library side: `VERSION` (`"vc4"`), `GET_UNIQUE`, the E7 device-identity seam
(`drmGetDevices2`, `S_IFCHR` from `mtGetAttr` — answered), legacy cursor → cursor plane, `ADDFB` →
`ADDFB2`, `OUT_FENCE_PTR` written client-side, `PRIME_FD_TO_HANDLE` (import a foreign buffer:
a new op in Stage B/M3), `DIRTYFB` (no-op).

## 12. The skeleton

| File | What |
|---|---|
| `kms_proto.h` | wire protocol, DRM layouts (events, modeinfo, caps, flags, fourcc) |
| `kms.h` | internal state, backend interface |
| `kms_main.c` | args, detach, ports, dispatch, clients, event queues, parked requests, commit engine, properties, `-R` restore, HELLO |
| `kms_fw.c` | `/dev/vcmbox`: display count/id/timing, EDID, fb geometry, pan, console switch, bus-address convention |
| `kms_backend.c` | pan + plane backends |
| `kms_bo.c` | pool, dumb BOs + `memExport`, framebuffers, blobs, the `/kmsbuf` namespace thread |
| `kms_vblank.c` | SMI ISR, HVS frame counter, source probe + fallback, the vblank thread, refresh measurement |
| `kmstest.c` | `info`, `pool`, `flip` (+ `-p` negative test), `vblank`, `stats`, `quit`, `all` |
| `build.sh` | standalone build into `out/` |

**Build** (writes only `tools/gpu-lane/kms/out/`; checks that the sysroot libphoenix has
`memExport` and libvcmbox has `vcmbox_callXL`):

```
tools/gpu-lane/kms/build.sh          # -> out/rpi4-kms, out/kmstest
```

Equivalent to: `aarch64-phoenix-gcc -O2 -g -std=gnu11 -Wall -Wextra -Werror -mno-outline-atomics
-mcpu=cortex-a72 -mstrict-align --sysroot=$S/ -B$S/lib/ -I tools/gpu-lane/kms
-I sources/phoenix-rtos-devices/misc/rpi4-vcmbox -I tools/gpu-lane/v3d-async` over the five server
files + `libvcmbox.c`, and `kmstest.c` alone.

**Implemented:** everything in §5–§9 for both backends — plane (default, kmsprobe's E3-proven wire
format) and pan (`-b pan`); the target-vblank completion rule; synthesized mode; `-B` fb blank; pool + export + namespace; vblank sources +
fallback; events + parked reads; properties/blobs; in-fences (with `-G`). **Not implemented:**
implicit sync (§8), fbdev emulation, a second display, mode setting, `PRIME` import, legacy cursor
ops (library side), `/dev/dri/card0`.

## 13. Pre-registered first Pi test (one netboot cycle)

**Question:** does the server take the display through the firmware, flip two CPU-drawn buffers at
the display's refresh with vblank-timed events, export pool BOs zero-copy, give the console back —
first with the (now default) **plane** backend, then with the **pan** fallback in the same boot?

**Preconditions:** netboot lane, current image (build ≥ 9: E1 export + port-death + vcmbox XL; the
same image E3 ran on); no other Pi cycle; **no GPU app, no X, no SDL program** in the command list;
`rpi4-fb` and fbcon run as always (not a conflict, §1); `rpi4-v3d-async` not started (no `-G`).

**Build + stage (coordinator):**

```
tools/gpu-lane/kms/build.sh
EXPORT=$(awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports)
sudo cp tools/gpu-lane/kms/out/rpi4-kms tools/gpu-lane/kms/out/kmstest "$EXPORT/bin/"
```

**One cycle** (Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label m2-kms-a --idle-secs 8 --max-cmd-secs 120 \
    --hdmi-dense-on 'KMSTEST flip start' -- \
    "/bin/rpi4-kms" \
    "/bin/kmstest info" \
    "/bin/kmstest pool" \
    "/bin/kmstest -n 600 -H 6 flip" \
    "/bin/kmstest vblank" \
    "/bin/kmstest stats" \
    "/bin/kmstest quit" \
    "/bin/rpi4-kms -b pan" \
    "/bin/kmstest -n 300 -H 6 flip" \
    "/bin/kmstest -p flip" \
    "/bin/kmstest stats" \
    "/bin/kmstest quit" \
    "/bin/rpi4-kms -R"
```

**Harness notes:** `rpi4-kms` returns to the prompt once ready (`KMS srv detached pid=`); every
`kmstest` mode prints a line at least every 2 s (`tick`); the longest command (plane `flip`: 600 flips
≈ 10 s + 6 s hold + draw) is far below `--max-cmd-secs`; no `--stamp` (breaks tag grepping). HDMI
snapshots go dense (5 s) from the first `KMSTEST flip start`. Wall clock ≈ netboot 60–150 s +
13 × (8 s idle + ~5 s) + ~40 s of test ≈ 5–6 min (within the 10-min Bash cap; if it has to be split,
cut after the first `quit` and run the pan half as a second cycle). Grade from the tagged lines only:

```
grep -a -E '^(KMS|KMSTEST) ' artifacts/rpi4b-uart/rpi4b-uart-*-m2-kms-a.log
./scripts/uart-summary.sh m2-kms-a        # stage table + fault counts
```

Allow for ~1.3 % UART line corruption (re-read a garbled line, don't count it); EL0 fault dumps print
twice. The fb PA changes per boot (`0x3d3b2000` in E3, `0x3d3fd000` on 2026-09-26 18:xx): predictions
below are written relative to the `fb_pa` of this cycle's `KMS fw` line.

**Predictions (per line) and what each alternative means — plane half:**

| Line | Predicted | If instead… |
|---|---|---|
| `KMS fw rev=0x69fe27b1 xl=1 displays=1 fb_pa=<P> fb=1920x1080 pitch=7680 virt_h=3240 slots=3 yoff=0 pixel_order=1 fmt=XB24 fb_layer=-127(fw)` | as listed (E3 `display`/`info` lines) | `xl=0`: stale core, the image lacks vcmbox-xl → the plane server refuses (`backend=plane FAIL`), run the pan half only. `yoff≠0`: refuses (`FAIL … panned`): something flipped earlier in the boot. |
| `KMS mode crtc=0 fw_id=2 1920x1080 clock_khz=148500 h=1920/2008/2052/2200 v=1080/1084/1089/1125 vrefresh=60 refresh_mhz=60000 from_fw=0 edid=… mm=…` | `from_fw=0` (E3: the timing tag answers zeros) with the synthesized CEA timing; `edid=yes` [inferred — EDID never queried before] | `from_fw=1`: the tag answered this time — fine, and Q7 closes. `edid=no`: tag/layout wrong or none; only mm size missing. |
| `KMS pool pa=… size_mib=32 tries=1–3 below_1g=1` | as listed (E3 `contig`: 32 MiB at `0x2c000000`, 24/32 chunks below 1 GiB) | `pool FAIL`: 32 MiB below 1 GiB unavailable → plane server exits (`return 5`); retry with `-p 16`. |
| `KMS vblank src=irq probe_ms=…` | `irq`, `probe_ms` ≤ 20 (E3: 60.01 Hz via IRQ 144) | `hvs`/`fwvsync`/`timer`: the ISR registration differs from kmsprobe's — compare `smi_mapped`/`irq=144`; the flip verdict is then about the protocol, not vsync. |
| `KMS srv ready dev=/dev/kms buf=/kmsbuf backend=plane planes=0x81 vblank_src=irq mode=1920x1080 refresh_mhz=60000 xl=1 pool=1 pool_mib=32 … bus=raw … guard_us=2000` + `KMS srv detached pid=` | once each | no `ready`: read the preceding `KMS srv FAIL` line. |
| `KMS vblank measured src=irq n=120 … hz=60.0xx` | 60.00–60.02 Hz (E3 60.01) | far off: the synthesized mode is off by that much (`refresh_mhz` follows the measurement). |
| `KMSTEST connect … backend=plane …` | client 1, 1920x1080 | `connect FAIL open`: device not registered. |
| `KMSTEST info caps dumb=1 prime=3 mono=1 async=0 cursor_w=64 modifiers=1 crtc_in_ev=1 depth=24` | as listed | rc in place of a value: protocol bug in that op. |
| `KMSTEST info connector … type=11 connection=1 … modes=1 … fw_display_id=2`, `info mode name=1920x1080 clock_khz=1485xx …` (148524 once the 60.01 Hz measurement has updated the synthesized mode and its MODE_ID blob) | as listed | — |
| `KMSTEST info planes n=2`, `info plane id=0x50 … type=1 … fmt0=XR24 fmt1=AR24`, `info plane id=0x57 … type=2` | primary + cursor | `n≠2`: universal-planes handling. |
| `KMSTEST info props plane … type=1 FB_ID=0 … IN_FORMATS=<id> zpos=0 alpha=65535 rotation=1`, `props crtc … ACTIVE=1 MODE_ID=<id> …`, `props connector … CRTC_ID=64 DPMS=0 EDID=… link-status=0 non-desktop=0`, `info in_formats rc=0 len=56 version=1 nfmt=2 fmt0=XR24 nmod=1`, `info result fails=0 verdict=PASS` | as listed | any rc≠0: fix before M3 marshals onto it. |
| `KMSTEST pool create … kind=2 …`, `pool map ok=1 err=0`, `pool zeroed=1`, `pool checksum rc=0 … match=1`, `pool memtype_refused=1 err=-22`, `pool destroy rc=0 name_gone=1 err=-2`, `pool result fails=0 verdict=PASS` | as listed — **the first zero-copy export of a server BO to a client** | `map ok=0 err=-2`: namespace lookup; `err=-13`/`-1`: access; `err=-22`: memtype mismatch. `match=0`: different pages (shadow object, E1 §3) — blocker. `memtype_refused=0`: E1 enforcement bypassed — blocker. `name_gone=0`: `memUnexport` did not withdraw the name. A `KMS srv note: /kmsbuf/… opened by pid X, owner pid Y` line: the path-resolution pid is not the client's — informs R7. |
| `KMSTEST flip buf=0 … kind=oid addr=<h> pitch=7680 size=8294400 map=1`, `buf=1 …` | two pool BOs | `map=0`: as `pool map` above. |
| `KMSTEST flip draw buf=… us=` | 10–60 ms per 1080p buffer [inferred, uncached stores] | ≫ 100 ms: Normal-NC stores not write-combined — matters for CPU-drawn clients only. |
| `KMSTEST flip test_only rc=0`, `flip ebusy_check rc=0 second_rc=-16 attempts=1 ebusy=1` | as listed | `ebusy=0` with `second_rc=0` on 3 attempts: the one-in-flight rule is broken. |
| `KMSTEST flip hist frames<0.75=0 on_time=… 1.5=… 2=… 3=… >3.5=…` | `on_time` ≥ 99 % of 599 | many `2`: flips land a frame late → the guard/target rule is too conservative for this client's timing (commits arriving later than frame − 2 ms after the vblank) or the vblank thread lags; many `<0.75`: vblank double counting. |
| `KMSTEST flip latency commit_to_event_us p50=… apply_us p50=… p99=… event_delivery_us p50=…` | commit→event p50 ≈ 16 ms (commit right after the previous event, completion at the next vblank); `apply_us` p50 ≈ 100, p99 ≤ 2100 (E3 `setplane_us`); `event_delivery_us` p50 < 200 (E5 read 20 µs + vblank-thread hop) | `apply_us` p99 > 8 ms: mailbox contention — frames will be missed. delivery ≫ 1 ms: vblank thread starved or the mono calibration is off (compare `interval_us`). |
| `KMSTEST flip interval_us p1=… p50=16666 p99=… max=… frame_us=16666` | p50 ≈ 16666, p1/p99 within ±200 µs (E3 IRQ jitter 59 µs) | wide spread: ISR/cond wake jitter or missed frames (then `missed>0`). |
| `KMSTEST flip result flips=600/600 fps=60.0x refresh_mhz=600xx … missed=… errors=0 … ebusy=1 rate_ok=1 verdict=PASS` | as listed, `missed` ≤ 6 (E3: 0 in 1201) | `errors>0`: read the `flip error`/`event_bad` lines (−16 = one-in-flight violated, −22/−34 = backend check). |
| HDMI snapshots during the plane `flip` | full-screen bars **white, yellow, cyan, green, magenta, red, blue, dark grey** left→right, grey ramp at the bottom, the checker marker **left or right** (alternating), a magenta box moving along the bottom third; during the hold one steady frame; no console text | console text visible under/over the image: layer order wrong; tearing/flicker: completion reported early; wrong colours: impossible per E3 unless the format mapping regressed. |
| `KMSTEST flip restore set_crtc0 rc=0` + next snapshot | the console again (planes unset, fb element still in the list) | display left on a test frame: see `KMS apply FAIL`. |
| `KMSTEST vblank wait n=59 errs=0 interval_us p1≈16600 p50≈16666 p99≈16730`, `vblank queue_sequence events=10/10`, `vblank result verdict=PASS` | as listed | `errs>0` with −11: parked waits expired → vblank thread not delivering. |
| `KMSTEST stats … applied=602 completed=602 fence_deferred=0 … apply_errors=0 dropped=0 bos=0 exports=0` | `applied == completed` ≈ 602 (EBUSY probe 1 + 600 + restore 1; +2 if the probe raced a vblank); no leaks | `bos`/`exports` > 0: leak on RMFB/DESTROY/close. |
| `KMSTEST quit rc=0`, `KMS srv exit flips_completed=… restored=1` | once | no exit line: quit path hung. |

**Pan half:**

| Line | Predicted | If instead… |
|---|---|---|
| `KMS srv ready … backend=pan planes=0x01 … pool_mib=4 slots=3 fmt=XB24 …` (no `srv note: /dev/kms left by a dead server`) | the second server registers because the first deregistered on quit | `srv note … reclaiming` then `ready`: the quit path did not deregister but reclaim works; `FAIL could not create /dev/kms`: names outlive the server and reclaim failed — fix before M3. |
| `KMSTEST flip buf=0 … kind=phys addr=<P+0x7e9000>`, `buf=1 … addr=<P+0xfd2000>` | fb slots 1 and 2 | `kind=oid`: slots not handed out (geometry mismatch) → the flips fail −22. |
| `KMSTEST flip result flips=300/300 fps=60.0x … missed≤3 errors=0 ebusy=1 verdict=PASS`, `hist on_time` ≥ 99 %, `apply_us p50` 100–300 (the ~0.15 ms flip-mailbox budget, research §4.5) | as listed — pan's update point is unmeasured, so this is the first check of the same target rule on panning | many `2`: panning latches later than `SET_PLANE` — raise `-L` for pan. `<0.75`/tearing in snapshots: pan applies immediately mid-frame (not at vblank) — pan completion must become "next vblank after a vblank-aligned apply". |
| HDMI snapshots during the pan `flip` | the same picture as the plane half **with correct colours** | red↔blue swapped (bars white, cyan, yellow, green, magenta, blue, red): the pan-format inference XBGR8888 (§3.2) is wrong — flip the mapping. |
| `KMSTEST flip result negative_test=pool_on_pan addfb_rc=0 flip_rc=-22 (expect -22) verdict=PASS` | as listed, screen unchanged | `flip_rc=0`: pan accepted a non-slot buffer — blocker. |
| `KMSTEST stats … applied≈302 … apply_errors=0 bos=0 exports=0`, `KMSTEST quit rc=0`, `KMS srv exit … restored=1` | as listed | — |
| `KMS restore done planes_unset=8 pan0_rc=0 unblank_rc=0 console_rc=0` | the final `-R` finds nothing to undo and leaves the console | rc≠0: the restore tool itself is broken — needed after any crash. |
| fault dumps (`uart-summary.sh`) | 0 kernel, 0 EL0 | any fault: `addr2line` the PC first (`out/rpi4-kms` is unstripped). |

**What the cycle decides:** PASS on the plane half = M2 Stage A works as designed (default backend);
the pan half checks the fallback and the name lifecycle. A `pool` failure blocks M3 (kmsro dumb BOs
are exactly these), not the pan flip path. Not exercised: in-fences (`-G`; needs the M1 server and a
GPU client — the M2→M1 integration cycle), `-B`, `-C`, overlays.

## 14. Open questions and risks

| # | Question / risk | Handling |
|---|---|---|
| Q1 | Firmware framebuffer vs our planes | **Answered by E3:** planes are added next to the fb element (fb at layer −127, `entries 2→3`); a primary at layer 0 covers it; blank/unblank round-trips. The fb is still fetched under an opaque primary → `-B` (default off until an M2 cycle watches it). |
| Q2 | fbcon still drawing into the firmware fb | harmless in both backends (slot 0 under the primary / not panned to); `-C` stops it at the cost of the keyboard release; costs fbcon's CPU time (and, without `-B`, the HVS fetch of the fb layer). |
| Q3 | Old lane's `rpi4-fb` when kms owns the display | keeps serving slot 0 (invisible, no corruption). fbdev emulation moves into kms at migration (§10.4). |
| Q4 | Completion model | **E3:** fixed firmware update point ≈ 1.6 ms before vblank → target-vblank rule with a 2 ms guard (§5). Pan's point unmeasured → this cycle's pan `hist`. |
| Q7 | Why does `GET_DISPLAY_TIMING` answer zeros on firmware `0x69fe27b1`? (request layout, or only filled after an fkms mode set?) | synthesized mode meanwhile (§3.1); matters only for real mode setting (not in Stage A) and for exact refresh reporting (measured instead). |
| Q8 | Does SMI vblank (and `SET_PLANE`) depend on `dtoverlay=vc4-fkms-v3d`? | E3 ran with the overlay (the bench needs it for V3D anyway); E3 Cycle B answers it if the overlay ever has to go. |
| Q5 | Pan format XBGR8888 | this cycle's snapshot. |
| Q6 | Path-resolution pid on `/kmsbuf` opens | this cycle's `srv note` line (or its absence). |
| R1 | **C1 exposure**: Stage A adds one mailbox transaction per flip (pan: the same rate the old lane's games already do; plane: one `SET_PLANE` per plane per frame through the XL path) | all traffic on `/dev/vcmbox`'s fixed bounce buffer; watch the C1 rate when M2 runs with games (research §4.9). |
| R2 | Server crash in plane mode leaves planes scanning freed pages | read-only DMA (garbage, no corruption); `rpi4-kms -R`; M3 supervisor. |
| R3 | Pool range reuse while a buggy client still maps a withdrawn window | stays inside the pool (never kernel-recycled memory); a quarantine by age is possible if it ever matters. |
| R4 | `poll()` quantised to 20 ms | kernel `block_ms` widening + device readiness wake-up (E5 §5 items 1–2) before M4/M6. |
| R5 | `SET_VSYNC` fallback blocks `/dev/vcmbox` a frame per call | last resort only; logged. |
| R6 | HVS source stamping granularity (~1 ms) and timer source not synchronised | only fallbacks now (E3: IRQ works); completions may be a frame early/late under them; the source is logged. |
| R10 | Names outlive a crashed server (devfs node, kernel port name) | clean exits deregister; start-up reclaims a name whose owner does not answer (a recycled port id answering would block the reclaim — safe direction). |
| R11 | Scan-out limited to the low 1 GiB (E6) | pool below 1 GiB (32 MiB is easy, E3 `contig`); imported buffers from other servers (M3: V3D render targets, HEVC frames) must also come from below 1 GiB — an M3 allocation constraint for `rpi4-v3d-async` scan-out BOs. |
| R7 | Access control on `/kmsbuf` logged, not enforced | enforce at the M2 gate once Q6 is known. |
| R8 | Stage A exposes one display | `GET_NUM_DISPLAYS` is logged; a second CRTC needs per-display SMI bits (the ISR already acks both) and fw display ids — M2 follow-up. |
| R9 | The firmware plane API is fkms-era and deprecated for the Pi 5 (research §4.5) | pinned firmware; Stage B (native HVS) is the long-term path. |

## Result

*(to be filled after the §13 cycle: log path, snapshot paths, the tagged lines, the rows that
applied, and the M2 decision)*
