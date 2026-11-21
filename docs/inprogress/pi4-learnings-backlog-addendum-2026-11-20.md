# Pi 4 learnings backlog, addendum 2 (2026-11-20)

Written 2026-11-20. **Documentation only: no Phoenix code, image or Pi was touched to write this.**

Continues [pi4-learnings-backlog.md](pi4-learnings-backlog.md) (tasks 1-32) and
[the 2026-11-17 addendum](pi4-learnings-backlog-addendum-2026-11-17.md) (tasks 33-42); numbering continues from 43.
Same scales (High / Medium / Low; S = under a day, M = 1-3 days, L = a week or more). The findings were measured
by the other OS port on the same board in its last two days of video work, and relayed on 2026-11-20. Phoenix
evidence below comes from reading our source; nothing was run.

**Where Phoenix stands.** `rpi4-kms` drives the firmware's `SET_PLANE` for **RGB scan-out only**: the plane
backend fills `num_planes = 1` and never sets `color_encoding` (`video/rpi4-kms/kms_backend.c:170`, `:331`), and
`kms_scanout.h:58` refuses Broadcom UIF/SAND/T-tiled modifiers. Hardware-decoded HEVC reaches the screen either
through the CPU (SAND → planar, then ffplay) or, in the browser, through the GPU (M10b path B: a SAND blit,
3.9 ms/frame). So tasks 43-46 are the plan for a **YUV video plane**, which [M10b](../gpu-new-lane/M10b-video-zero-copy.md)
lists as "not started: the players"; 47 is a cheap browser A/B; 48 feeds task 36 (H.264).

## Index

| # | Priority | Task | Effort |
|---|---|---|---|
| 43 | Medium | YUV video plane for the players: SAND straight to the firmware plane (NV12_COL128, below 1 GiB) | M |
| 44 | Medium | Plane colour encoding from the stream's tag, not from the picture height | S |
| 45 | Medium | A boot-time reserved pool below 1 GiB for scan-out pictures | S-M |
| 46 | Low | Browser video on a YUV plane with a colour key; no window redraw when only the video changed | L |
| 47 | Medium | Build Skia (WPE's CPU raster) with clang and compare | S |
| 48 | — | H.264 via VCHIQ + MMAL: five operational limits (folded into task 36) | reference |
| 49 | Low | Bench: an agent's ssh helper must check it reached its own Pi session | S |
| 50-57 | — | Part 2: suggestions for our open issues D5, C18, P3, P1, C17, C5, L36, F2 (table at the end) | per item |
| 58 | Medium | Idle timer interrupts ~6 200/s → hundreds: GENET/KMS waits, PL011 RX IRQ, xHCI event interrupt, tickless secondaries | S each, M for tickless |

## Medium

### 43. YUV video plane for the players: SAND straight to the firmware plane

**Measured by the second port** (HEVC on the hardware block, a media-player-style plane path, no CPU SAND →
planar, every frame shown, output bit-exact against the CPU decoder):

| HEVC | CPU de-tile + RGB path | SAND on the plane |
|---|---|---|
| 1080p30 | 23 % of a core | 4.4-6.5 % |
| 1080p30 Main 10 | 44 % | 5-6 % |
| 720p | 12 % | 3-4 % |
| 4K (with task 45's pool) | 78.6 %, ~360 frames dropped | 5.7-7 %, 0 dropped |

**Two hardware conditions.**
- **Layout.** The firmware accepts SAND only when luma and chroma share the column layout: **NV12_COL128**, one
  buffer, column stride C × 128, chroma at row H16 inside each 128-byte column. Separate per-plane SAND buffers
  are refused. Check what `hevc_rpivid` writes (`sources/phoenix-rtos-ports/video_player/files/rpivid/src`) before
  wiring `ADDFB2` NV12 + SAND128 into `rpi4-kms`.
- **Address.** The display fetches only the low 1 GiB (30 address bits); we know this already for RGB
  (`kms_scanout.h:27`, `kms_fw.c:128-130`). The HEVC block can write anywhere (M10b round 4), but a picture meant for
  scan-out must be allocated below 1 GiB, which leads to task 45.

**Phoenix work.** Accept the SAND128 modifier for NV12 in `kms_scanout.h`, fill `num_planes`/`planes[]`/`is_vu`
for it in the plane backend, let ffplay (SDL KMSDRM) or a small player hand the decoder's buffer over as a dma-buf
(M10b §3 already shares the buffers), and keep the CPU path as the fallback.
**Verify on the Pi:** `video-play` 720p/1080p/Main 10 CPU % against today's, frames shown = frames decoded,
same-scene screenshot against the CPU path.

### 44. Plane colour encoding from the stream's tag

**Measured by the second port**, probing the HVS through the firmware's `SET_PLANE` `color_encoding` byte:

| value | matrix and range |
|---|---|
| 1 | BT.601 limited |
| 2 | BT.709 limited |
| 3 | BT.601 **full** |
| 9 | BT.2020 |
| 0 and every other value | BT.709 (limited) |

There is no full-range BT.709 and no full-range BT.2020: convert those to limited range first, or leave them on
the GPU path. BT.2020 on the plane was up to 11 code values off a software reference, so the second port keeps it
off the plane. Passing the stream's real tag gave 11 test clips (SD/HD, 601/709, 601-tagged HD, full range, HEVC
8/10-bit) within ±3 of the RGB path. Guessing by height (709 from 720 lines up), which is what they did before,
gave a hue shift on 601-tagged HD.

**Phoenix work:** with task 43, map `AVFrame.colorspace`/`color_range` to this byte and fall back to the GPU path
for the two combinations the plane cannot show. **Verify:** a 601-tagged HD clip, same-scene against the RGB path.

### 45. A boot-time reserved pool below 1 GiB for scan-out pictures

**Measured by the second port:** a lowest-address-first allocator plus pinned pages scattered through low memory
left **no free contiguous 12.7 MiB below 1 GiB after ~20 browser runs**, so even 1080p fell back from the plane.
Retries and compaction did not help; a pool reserved at boot did. Sizes: **192 MiB** holds 4K 8-bit (~190 MB of
pictures); 4K Main 10 needs ~255 MiB. With it, 4K HEVC went to 5.7-7 % of a core with 0 frames dropped, and 1080p
stayed on the plane after heavy use.

**Trap:** never return a picture's memory to the pool while any other mapping of it exists. Otherwise the display
scans out reused memory, and another client can read the next stream's frames.

**Phoenix today:** `rpi4-kms` already has a scan-out pool below 1 GiB for RGB buffers (`srv.pool_ok`,
`kms_backend.c:189`; "our pool buffers scanned below 1 GiB", `:23`). Size it for pictures too (or add a second
pool), and make the free path wait until the last mapping is gone (the same rule as task 42's buffer reuse).
**Verify:** 20 consecutive 1080p plays, then a 1080p play still on the plane.

### 47. Build Skia with clang and compare

**Measured by the second port:** Firefox's CPU compositor (SWGL) built by clang 21 instead of GCC 16 composited
in 14 % less CPU at 720p and 25 % less at 1080p, and 1080p dropped frames fell from 33 % to 5 %: GCC misses its
vector code paths. **Phoenix:** WPE composites with Skia's CPU raster (M10b status, build 60), and Skia upstream
states GCC builds are much slower (its raster pipeline's vector code is tuned for clang). Building Skia alone with
clang is a cheap A/B: the browser's scroll/video benchmarks, CPU % and dropped frames, same pages. Needs a clang
cross toolchain for aarch64-phoenix first (not checked whether ours has one; inferred effort S if it does).

## Low

### 46. Browser video on a YUV plane with a colour key

**Measured by the second port:** instead of a GPU blit (our M10b path B), the browser put the decoded video on a
YUV plane behind a colour key. Per 10 s of 720p, compositor CPU went 8.2 s → 0.9 s, and 1080p start-up drops
120 → ~33. The biggest single win was **not redrawing the window when only the video changed**: the browser engine
marked the video rectangle dirty every frame, and excluding it took the window-system copies to 0.
Rules they needed: translucent content over the video (captions, controls) is drawn on the CPU/GPU instead; only a
fully visible, still rectangle goes on the plane (none while scrolling or partly hidden); the plane is released
when the tab is hidden or the window minimised; screenshots show black there.
**Phoenix:** depends on 43-45 and on a compositor that can place a plane; the "do not mark the video rectangle
dirty" part may pay off on path B too, and is worth measuring first.

### 49. Bench: an agent's ssh helper must check it reached its own Pi session

The second port's ssh helper once reached the other agent's running Pi session because its own session start had
been refused. It now checks the session label first. Phoenix's cycles use the UART and the bench lock, not ssh, but
any helper that logs in over the network should check the boot's label (for example `/etc/build-versions` or a
per-cycle token) before acting.

## Reference

### 48. H.264 via VCHIQ + MMAL: five operational limits (folded into task 36)

Measured by the second port on the BSD-licensed route (FreeBSD's vchiq + Broadcom's BSD-3 libmmal/VCOS); recorded
in [the L36 design note](../misc/2026-11-18-h264-vchiq-mmal-design.md) §"Field notes 2026-11-20":
1. Build VCOS with `NDEBUG`: otherwise `vcos_assert` calls `abort()` inside the client.
2. A second decoder fails with `ENOSPC` at the default `gpu_mem=76` (one 1080p decoder fits), and its error flood
   can wedge the firmware's MMAL service until reboot: allow one H.264 decoder system-wide (a file lock) and fall
   back to software.
3. The decoder manages ~69 fps at 1080p, so 1080p60 dropped 216-538 frames, while software decode played 1080p60
   with 0-11 of ~1500 dropped at ~2 cores: choose by pixel rate (above 1080p at 45 fps → CPU).
4. A decode and an encode at once need `gpu_mem=128`.
5. `h264_mmal`'s `initial_qp > 0` silently disables the encoder's rate control.

## Part 2: suggestions for our open issues (relayed 2026-11-20 late)

The same port matched our `docs/KNOWN-ISSUES.md` against what worked on its side. These are suggestions, each to
be checked against our source before acting; "checked" below means read in our tree on 2026-11-20.

| # | Our issue | Suggestion | Status in our tree |
|---|---|---|---|
| 50 | D5 (VL805 `0xdead` window, waits) | (1) read vendor and the VL805 firmware version at config **0x50**; (2) non-zero → skip `NOTIFY_XHCI_RESET` (no reset, no race, no wait); (3) else notify, then poll the vendor ID (5 ms × 60) instead of a fixed sleep; (4) **after a notify, reprogram the root port's inbound window**, which the firmware's handling of the request can disturb | ✅ checked: (1)-(3) are already in `usb/xhci/bcm2711-pcie.c:914-964` (USB-FIX-2 skip on 0x50, vendor-ID poll capped at 300 ms). (4) not done; and the late-`mmap`-reads-`0xdead` pmap workaround may come from a later notify or reset: find out whether a second notify ever happens (log every notify) |
| 51 | C18 (labwc cannot import ffplay's dma-buf after a resize) | The shared buffer's lifetime follows its **references**, not its exporter: export = an object that holds a BO reference, bound to the fd; import = a new BO reference from that object; the exporter's withdrawal drops only its own reference; free at 0. Two instances on their side: picture memory back to a pool while another process still mapped it; a process killed during async DMA freeing memory before its fds closed (release DMA and import references at process death first) | ✅ checked 2026-11-20: **does not apply** — our render server already ties a BO to every reference, the dma-buf fd included, and none of the failing runs reached a server-side rejection; C18 is an fd that does not name a buffer file at labwc's lookup (details and leads in KNOWN-ISSUES C18) |
| 52 | P3 (`dc zva` disabled in `hal_memset`) | Arm Optimized Routines' memset (MIT OR Apache-2.0 WITH LLVM-exception) checks DCZID_EL0 at run time and uses `dc zva` for large zeroing; stable on this Pi across their releases. The one hazard: `dc zva` on Device or non-cacheable memory faults or hangs. Split: AOR for normal memory; a plain store loop for MMIO and uncached DMA buffers | not checked: grep `hal_memset` callers on DMA/uncached/device mappings first (fits our recorded hang) |
| 53 | P1 (2/8 GB boards mis-mapped) | The firmware patches the DTB `/memory` node with the real banks (e.g. 0-0x3b400000, 0x40000000-0xfc000000, 0x100000000+ on 4/8 GB): plo can take the map from there. Mailbox GET_ARM_MEMORY gives only the first bank, GET_VC_MEMORY the carve-out. (Their port has also run only a 4 GB board) | not checked |
| 54 | C17 (OOM leaves the desktop unusable) | Graded low-memory states (normal / note / warning / critical) from free pages and free memory; every cache registers a handler that shrinks at each step (block cache, file cache, VM caches, slab), plus a small reserve so teardown and close messages still get memory. For us: a kernel low-memory level that servers subscribe to (V3D BO pool: trim harder at "warning"; shmsrv; the file cache) | design idea |
| 55 | C5 (PWM DMA parked waiting for DREQ) | Read the DTB's `brcm,dma-channel-mask` before claiming channel 5 and refuse if the firmware has not freed it; if the firmware used channel 5 on some boots, a parked channel would look exactly like ours | ✅ done on branch devices `audio-dma-mask` `63c7bf78` (via the firmware's `GET_DMA_CHANNELS` mask, logged every start; Pi pending) |
| 56 | L36 (H.264 via VCHIQ + MMAL) | Beyond task 48: VCHIQ slot memory below 1 GiB; raise the firmware's H.264 clock while decoding (500 MHz, release after; idle 250); MMAL zero-copy off at first (copy out); the one-decoder lock and NDEBUG from day one. Steps 1-3 (device node, libmmal, decoder) took them ~4 days | design input (also in the L36 note) |
| 57 | F2 (no Bluetooth host stack) | NetBSD's `sys/netbt` (HCI, L2CAP, RFCOMM, SCO, `bthidev` for keyboards/mice; BSD-2) as a BSD-licensed host stack on top of our working HCI transport | idea |
| — | F1 (WiFi throughput) | Calibration: 49 up / 46 down Mbit/s (~6 MB/s) on the same 2.4 GHz access point; interrupt-moved data blocks cut CPU per MiB by a third without changing the rate. The AP and band matter more than further bus work | matches our 2026-11-20 A/B (5.1 MB/s, CPU −50 %) |
| — | P7 (no UTF-8 in libphoenix) | musl's `mbrtowc`/`wcrtomb`/`wcwidth` (MIT) as the compact reference | idea |

## 58. Idle timer interrupts: ~6 200/s here against ~355/s on the other port (read 2026-11-21)

Measured idle (per-CPU heartbeat, after the xHCI fix of 11-19): CPU 0 ~3 180/s, CPUs 1-3 ~1 001/s each. The other
port measures ~355/s summed over 4 CPUs at idle (99.5 % idle). Reading our kernel (no rate below is measured):

- **CPUs 1-3: a fixed 1 ms tick** (`SYSTICK_INTERVAL 1000`, `hal/aarch64/arch/cpu.h:62`; re-armed flat on every
  interrupt, `proc/threads.c:313-325`, because CNTV is level-triggered). It serves round-robin preemption and is the
  only way an idle secondary picks up new ready work: there is no reschedule IPI (`arch/interrupts.h:28`), and the
  idle loop's low-power hook is off on aarch64 (`hal_cpuLowPowerAvail()` returns 0, `cpu.c:585-588`). CPU time
  accounting does not depend on the tick.
- **CPU 0: no separate tick**, one interrupt per distinct sleeper deadline (capped at 1 ms; `threads.c:158-174`,
  `:328-343`). Its ~3 200/s are the boot services' polling, by reading:

  | source | wait | est. /s |
  |---|---|---|
  | `pl011-tty` RX/TX thread (polled, no IRQ) | `usleep(1000)`, `pl011-tty.c:1131` | ~1 000 |
  | xHCI interrupt-IN poller (keyboard, mouse) | `usleep(1000)`, `xhci.c:831`, `:874` | ~1 000 |
  | GENET IRQ thread RX backstop | `condWait` 2 ms, `bcm-genet.c:199`, `:1319` | ~500 |
  | KMS vblank thread (60 Hz, ~8 waits per frame) | ≤ 2 ms, `kms_vblank.c:304`, `:502` | ~450-540 |
  | pl011 keyboard bridge | `usleep(8000)`, `pl011-tty.c:74`, `:1183` | ~125 |
  | v3d-async event thread backstop | 10 ms, `v3da_sched.c:43` | ~100 |
  | WiFi (link-state / idle polls) | 10-100 ms | 10-100 (not verified) |

**Order of work** (cheap and local first; each verified with the heartbeat build, idle, 20 boots):
1. ✅ branches 2026-11-21 (lwip `genet-idle-poll` `3606550`, devices `kms-vblank-wait` `e5b8d3c3`): GENET backstop 2 ms → 50 ms when idle (the IRQ is the primary path) and the KMS vblank waits (sleep to the next vblank,
   not in 2 ms slices): ~1 000/s, small, low risk.
2. ✅ branch 2026-11-21 (devices `pl011-rx-irq` `92afcd22`, project `c0a5025`): PL011 RX interrupt instead of the 1 ms poll thread (the UART has one; the console path must stay safe for panics):
   ~1 000/s.
3. xHCI: wait on the event-ring interrupt instead of sleeping 1 ms per idle interrupt-IN pipe: ~1 000/s, larger.
4. Tickless idle on CPUs 1-3: implement `hal_cpuLowPower()` (mask the timer, mark the CPU idle, `wfi`), add a
   reschedule SGI sent by `_readyAdd()` to an idle CPU, keep a 100 ms-1 s backstop so a lost IPI costs at most that
   and the heartbeat still reports; ~150 lines, low-medium risk (global ready queue; CPU 0 keeps its interrupts):
   ~3 000/s.
