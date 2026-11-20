# H.264 hardware decode through VCHIQ + MMAL: design note (L36 step 1)

Written 2026-11-18. **Documentation only.** No code, image or Pi was touched. This is step 1 of
`docs/inprogress/pi4-learnings-backlog-addendum-2026-11-17.md` §36, where a second OS port on the same board took
H.264 from nothing to a working player in a day. The note reverses the 2026-08-27 call "H.264 is a VCHIQ/firmware
wall (~27k LOC) → banked" (`docs/misc/2026-08-27-owner-decision-queue.md:41-47`). That estimate assumed we would
write the stack ourselves. Here we reuse three BSD-licensed parts and write only a server, a message shim and patches.

Paths are relative to `sources/` unless they start with `docs/`, `scripts/` or `.buildroot/`. "Inferred" means the
claim comes from reading code or docs; nothing was run.

## 1. Goal and non-goals

**Goal.** Hardware H.264 decode on the VideoCore through FFmpeg's `h264_mmal`:

- first in the `video_player` port (ffplay, `/bin/video-play`, gtk-video);
- then WebKit `<video>` (`webkit_wpe` USE video), which links the same FFmpeg.

The CPU decoder drops frames on 1080p H.264 (`docs/pi4-hardware-support-matrix.md:82`), and H.264 is what the demo
clips and most web video use.

**Non-goals:**

- rpivid/HEVC is unchanged (`phoenix-rtos-ports/video_player/files/rpivid/`, MMIO, no VCHIQ).
- No camera, ISP, encoder or dispmanx.
- No `vcsm` zero-copy, and no opaque `AV_PIX_FMT_MMAL` frames to the display. Frames return as I420 in system memory.
- No 4K. Inferred: the block is specified up to 1080p.
- No kernel driver: VCHIQ is a user-space server.

## 2. Licence check

The rules are in `LICENSING.md` and `docs/PHOENIX-RTOS-RPI4-CHANGES.md` ("Licensing"):

- no GPL code in Phoenix core repos;
- new drivers carry `%LICENSE%`;
- third-party code keeps its own licence.

Provenance:

- **(F)** FreeBSD `sys/contrib/vchiq`, from the local clone `external/freebsd-src` at `f492ef8318f5`. Public:
  https://github.com/freebsd/freebsd-src/tree/main/sys/contrib/vchiq
- **(U)** `raspberrypi/userland` master `a54a0dbb2b8d`. The tarball was fetched and scanned on 2026-11-18. The repo
  is archived, and the GitHub API reports `BSD-3-Clause`. https://github.com/raspberrypi/userland (`LICENCE`)
- **(L)** Linux: `external/linux`, a raspberrypi/linux clone at `57083d8cf8f0`.
- **(FF)** FFmpeg 6.1: the port's own `video_player/ffmpeg-6.1.tar.gz`.

| Files | Header (checked) | Use |
|---|---|---|
| (F) 30 of the 34 files: `vchiq_arm/*` except `vchiq_kmod.c`, plus `vchi/*` | Broadcom 2010-2014: **BSD-3 "ALTERNATIVELY" GPL-2**. Checked in all 30 | **Taken under BSD-3**, headers kept: `vchiq_core.[ch]`, `vchiq_util.[ch]`, `vchiq_connected.[ch]`, `vchiq_cfg.h`, `vchiq_if.h`, `vchiq_pagelist.h`, `vchiq_2835_arm.c` and `vchiq_arm.[ch]` (adapted). **Not needed:** `vchi/*`, `vchiq_shim.c`, `vchiq_kern_lib.c`, `vchiq_debugfs.*` (kernel-side clients) |
| (F) `vchiq_kmod.c`, `compat/vchi_bsd.[ch]` | FreeBSD glue, **BSD-2** | Not taken: kernel glue we replace. Read for the BELL0/BELL2 offsets and the bcm2711 `LONG_BULK_SPACE` flag |
| (F) `compat/list.h` | third-party **BSD-2** | Not taken |
| (U) `interface/{vchiq_arm,vcos,mmal,vchi,vctypes,vmcs_host}`, `vcinclude`, `helpers`, `vcfw`, `host_support`, `host_applications/linux/libs/bcm_host` | **BSD-3** (Broadcom Europe 2012, Raspberry Pi 2015). In the 243 files under `interface/` outside `khronos/` there are 0 GPL mentions and every file has a BSD header | **Taken.** Not built: `khronos/`, including 9 MIT-style OpenMAX headers; `containers/`; `libs/sm` (`user-vcsm`), whose `dma-buf.h` and `vc_sm_cma_ioctl.h` are **GPL**. `ENABLE_MMAL_VCSM` is left undefined |
| (FF) `libavcodec/mmaldec.c` | **LGPL-2.1+** | Enabled. In `configure`, `mmal` is in `HWACCEL_LIBRARY_LIST` (1948-1953), not in a nonfree or GPL list (4346-4350) |
| (L) `vc04_services/{vchiq-mmal,bcm2835-codec,vc-sm-cma}/*`, `vchiq_arm/vchiq_bus.[ch]`, `include/soc/bcm2835/raspberrypi-firmware.h` | **GPL-2.0** | **Behaviour reference only.** Two interface numbers are read from the last file: tag `VCHIQ_INIT` = 0x00048010, and clock ID 6 = H264 (`raspberrypi-firmware.h:154,166-171`). These are firmware ABI facts, not copied code |
| (L) `vchiq_arm/vchiq_core.c`, `vchiq_arm.c`, `vchiq_dev.c` | `GPL-2.0 OR BSD-3-Clause` | Not taken. We use the (F) copy |

**How this fits the `video_player` port.**

- The port stays `LGPL-2.1-or-later AND BSD-3-Clause` (`video_player/port.def.sh`). It already builds FFmpeg without
  GPL or nonfree parts and links ffplay statically, so `mmaldec.c` adds no new obligation.
- The new BSD-3 port `rpi_userland` adds one duty: binaries that link `libmmal_*`/`libvchiq_arm`/`libvcos` must ship
  its notice. The port installs `LICENCE`.
- The server's vendored Broadcom files stay BSD-3, and our own files carry `%LICENSE%`.

**Verdict: clear.** Everything that runs on Phoenix is BSD-3, or LGPL-2.1+ as FFmpeg already is. BSD-2 files are
used only as references. GPL files are used only for behaviour and interface numbers.

## 3. Architecture on Phoenix

```
 ffplay / WebKit ─ FFmpeg h264 wrapper ─ h264_mmal ─ libmmal (patched) ─ libvchiq (ioctl → msg shim)
                                                                           │ mtDevCtl, /dev/vchiq
 rpi4-vcmbox ◄─ VCHIQ_INIT, clock 6 ─ rpi4-vchiq server ─ slots + pagelists + bulk pool (< 1 GiB)
                                        │ interrupt(66): BELL0 0xfe00b840 · ring: BELL2 0xfe00b848
```

**Placement.**

- Server: `phoenix-rtos-devices/misc/rpi4-vchiq/`, started after `rpi4-vcmbox`. It holds the vendored (F) files and
  a new `vchiq_phoenix.h` that replaces `compat/vchi_bsd.h` (threads, semaphores, mutexes).
- Library port: `phoenix-rtos-ports/rpi_userland`. It builds `vcos` (pthreads), `vchiq_arm`, gencmd, MMAL
  core/util/vc client and a cut-down `bcm_host`.

**Slots.**

- Size: 64 + slot-zero slots of 4 KiB, plus 64 fragments of 128 B, about 280 KiB in total
  (`vchiq_2835_arm.c:56,66,204-206`).
- The server maps them once with `MAP_CONTIGUOUS | MAP_UNCACHED` and retries until the region ends below 1 GiB, as
  `kms_pool_init()` does (`video/rpi4-kms/kms_bo.c:59-104`). The VideoCore sees ARM RAM only through the DTB `/soc`
  `dma-ranges` alias `0xc0000000 → 0`, which is 1 GiB.
- The server sends `0xc0000000 | pa` with tag 0x00048010 through `vcmbox_call()` (`misc/rpi4-vcmbox/libvcmbox.h`).
  A response word of 0 means the firmware accepted it.
- **Decision:** use the property tag, not FreeBSD's mailbox channel 3 (`vchiq_2835_arm.c:267`). `rpi4-vcmbox` owns
  the only FIFO, and a second writer would race it (`libvcmbox.h:8-13`).
- **Decision:** use the `0xc0000000` alias, which both reference drivers pass. `rpi4-vcmbox` posts its own buffer by
  raw physical address (`rpi4-vcmbox.c:106`), so checkpoint 1 checks the alias.
- **Lifetime: the whole boot.** The firmware keeps the address and VCHIQ cannot be re-initialised. The slots are
  never freed, and after VCHIQ_INIT the server has no exit path: fatal errors park it. See R1.

**Doorbell.**

- The DTB node `mailbox@7e00b840` (`brcm,bcm2711-vchiq`, `reg <0x7e00b840 0x3c>`) has `interrupts <0 0x22 4>` on the
  GIC-400. That is SPI 34, so **INTID 66**, which matches the addendum. `interrupt()` takes INTIDs: rpivid uses
  `32u + 98u` (`rpivid_regs.h:105`).
- The ISR reads BELL0. The read clears it, and bit 2 means the VideoCore rang. The ISR then signals the slot-handler
  thread, which runs `remote_event_pollall()` (`vchiq_kmod.c:128-137`).
- To signal the VideoCore: store the event, then `dsb sy`, then write BELL2. Without that barrier,
  `docs/misc/2026-09-18-dma-barrier-audit.md` measured 2.9 % stale DMA fetches.
- The device page `0xfe00b000` also holds the property mailbox (`+0x880`), which `rpi4-vcmbox` maps. The two servers
  touch disjoint registers.

**IPC shim.**

- libphoenix `ioctl()` carries one flat buffer per call (`libphoenix/sys/ioctl.c`). The VCHIQ requests nest user
  pointers: `elements[]`, `msgbufs[]`, `buf` and bulk `data` (U `vchiq_ioctl.h:36-95`).
- So one libvchiq patch replaces the `ioctl(instance->fd, …)` sites in `vchiq_lib.c` (31 occurrences) with
  `vchiq_phx_call()`. That function marshals each request flat into `msg.i.data`/`msg.o.data`.
- The server keys clients by the `oid.id` it returns on `mtOpen`, like `rpi4-kms` (`kms_main.c:1925-1936`).

**Blocking calls and dying senders.** `AWAIT_COMPLETION` and blocking `DEQUEUE_MESSAGE` get deferred replies: the
server keeps the `rid` and answers from the slot-handler thread, as `rpi4-kms` (`kms_main.c:1957`) and
`rpi4-v3d-async` do. The kernel's rule decides the shape:

- A thread killed inside `msgSend` can abandon a received request only if **no payload window is mapped** into the
  server (`proc/msg.c:517-544`).
- A request whose payload is larger than `msg.i.raw`/`msg.o.raw` keeps the dying process alive until the server
  answers (`msg.c:342-447`). Until then its descriptors never close, so the server never sees `mtClose`.

**Decision:** split the deferred call in two.

1. A wait request with everything in `raw`, answered when completions are ready or after a **1 s timeout** (0 ready).
2. A non-blocking fetch that carries the data and is answered at once.

The completion thread in `vchiq_lib.c:1498` keeps its loop. A dead client's pending wait is detached by the kernel,
and the server's late `msgRespond` gets `-ENOENT`. Inferred: on exit, `posix_died()` → `posix_exit()` closes the
descriptors and `proc_close()` sends `mtClose` (`posix/posix.c:200-230,738,4093`). Checkpoint 1 tests this.

**32-bit messages on arm64.**

- The VCHIQ core is already 32-bit clean in (F): `REMOTE_EVENT_T.event` is a `uint32_t` offset
  (`vchiq_core.h:274-278`), and bulk `data` is a 32-bit pagelist address.
- MMAL is not. The firmware expects the 32-bit ARM layout of `mmal_vc_msgs.h`, which embeds host pointers
  (`control_service`, `waiter`, `client_component`, `client_context`, the `MMAL_BUFFER_HEADER_T` pointers; lines
  115-421). Upstream turns MMAL off on ARM64 (`CMakeLists.txt:11-17`), so **libmmal needs our patches**:
  - wire pointers become `uint32_t` handles, with a per-process handle table;
  - explicit 32-bit wire copies of the buffer header and the ES format;
  - explicit init of `pthread_once`/mutex objects that only Linux accepts zeroed;
  - the unlock-without-lock fix the addendum names;
  - `bcm_host_init()` cut down to VCHI and gencmd.
- We write these patches ourselves. The addendum's list is a checklist.
- A host harness compiles the original header for armv7 and ours for aarch64, and compares `sizeof`/`offsetof` of
  every wire structure. It follows the pattern of `tools/*-hosttest`.

**Bulk transfers.**

- `brcm,bcm2711-vchiq` selects the long pagelist format: page address `>> 4`, runs of up to 0x100 pages
  (`vchiq_2835_arm.c:80-84,528-535`). The pagelist itself must be below 1 GiB (`VCHIQ_ARM_ADDRESS`, `:59,341`).
- The VideoCore only ever sees **server-owned** memory: uncached pagelists, plus a cached contiguous bulk pool
  sub-allocated like `kms_bo.c`.
- **Copy path** (checkpoint 2):
  - Transmit data (compressed NAL units) arrives in `msg.i.data` and is copied into the pool before the reply.
  - Received data gets `dc civac` after the bulk completes (as in `rpivid_hw.c:479-494`) and is copied out in the
    fetch reply.
- **Export path** (checkpoint 3):
  - MMAL output buffers are allocated by the server and mapped into the client with `memExport()`
    (`libphoenix/include/sys/mman.h:105-121`), under `/vchiqbuf/<h>` like `/kmsbuf`.
  - A bulk into such a buffer travels as `{handle, offset, len}`. The server does the cache maintenance through its
    own alias, which works because the A72 data cache is PIPT.
  - FFmpeg needs `mmal_port_pool_create()` instead of `mmal_pool_create()` (`mmaldec.c:340`).

**Dying client.**

- The VideoCore may still write a killed client's buffers, so a client never owns DMA memory. Its `memExport`
  mapping is a second mapping, and the pages live until the server unexports them (`mman.h`).
- On `mtClose` the server:
  - closes the client's services;
  - moves every buffer with a queued or active bulk to a **quarantine list**;
  - frees nothing until each bulk reports done or aborted and the VideoCore acknowledges the service close.
- If the VideoCore never acknowledges, the buffers leak and the leak is logged. It is never a write into memory the
  kernel has already reused.

**Clock.**

- Firmware clock 6 (H264). The addendum gives 250 MHz after boot and 500 MHz maximum.
- The server counts open `mmal` services, which is one per MMAL process.
- When the count goes 0→1, it saves the current rate and sets the maximum (`GET_MAX_CLOCK_RATE`/`SET_CLOCK_RATE`, as
  `rpivid_hw.c:187-210` does for clock 11).
- When the count goes 1→0, it restores the saved rate. A dead client's `mtClose` also counts, so no client can keep
  the clock raised.

**GPU memory.**

- `gpu_mem=128` (`phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt:60`). The same value is in the
  built bootfs.
- The bootfs ships the full `start4.elf` (2.3 MB, no `start_file`). The cut-down firmware has no codecs.

| Item | MB | Basis |
|---|---|---|
| Triple framebuffer 3 × 1920×1080×4 | 24.9 | computed (`config.txt:53-59`) |
| 1080p I420 frame (1920×1088×1.5) | 3.13 | computed |
| ARM-side output pool: max(min, recommended) + `extra_buffers` (FFmpeg default 10) | ≥ 34 | `mmaldec.c:338-339,821` |
| ARM-side input pool: max(min, 20) × max(min, 512 KiB) | ≈ 10 | `mmaldec.c:420-423` |
| Firmware-side copies of these pools, plus reference frames (≈ 4-5 frames at 1080p) | ≈ 13-16 + pools | inferred: the VideoCore allocation is not visible in our code |
| 10 extra buffers overflowed 76 MB; 2 fitted | — | measured by the second port (addendum §36) |
| Free relocatable heap | ? | **to measure** in checkpoint 1: `get_mem reloc_total`/`reloc` over GCMD |

Sum for 1080p with `extra_buffers=2` and 8 input buffers, inferred: 24.9 (framebuffer) + about 16 (reference frames)
+ about 9.4 (3 output buffers) + 4 (input) ≈ **54 MB** of the 128 MB, before the firmware's own footprint.

**Decision:**

- `video_player` sets `extra_buffers=2` and 8 input buffers.
- The wrapper decoder (§4, checkpoint 3) allows **one hardware decoder per board**: a process-wide count plus a
  server refusal of a second `mmal` client. A second stream goes to the CPU.
- These limits are revisited after the checkpoint-1 measurement.

## 4. Work plan

Pi steps run under `./scripts/pi-bench-lock.sh` with `test-cycle-psh-interact.sh` (or the `rpi4-run` recipe) and
`--capture-secs` ≥ 180. Core changes are rebuilt with `--scope core`. Every checkpoint ends with a manifest.

| # | Checkpoint | Contents | Effort (inferred) | Pi verification |
|---|---|---|---|---|
| 1 | **`GCMD version` round trip** | Server: slots, VCHIQ_INIT, ISR, doorbell, services, the wait/fetch shim. `rpi_userland`: vcos, libvchiq, gencmd. A minimal `vcgencmd` | 5-7 d | `vcgencmd version` prints the firmware build, which goes into the manifest. `get_mem reloc_total`/`reloc` fill the budget table. 100 connect/close loops, 0 faults. A `vcgencmd` killed while it waits: the server logs `mtClose` and the process is gone. The slot PA is logged below 1 GiB |
| 2 | **MMAL decode to memory, MD5 against FFmpeg** | libmmal arm64 patches + layout harness, the bulk copy path. `mmal-h264-check`, modelled on `hevc-rpivid-check` (`video_player/files/rpivid/check/`) | 5-7 d | Demo clips, 720p and 1080p, 900+ frames: per-frame MD5 against the CPU `h264` decoder. A frame that differs counts as a pass only if every differing sample has `ref ∈ [1,15]` and `hw = ref − 1`, the firmware's sub-black quirk. The excused count is printed, along with fps and CPU at 250 and 500 MHz |
| 3 | **`h264_mmal` in `video_player`** | `--enable-mmal` and `h264_mmal` in `files/components.sh`. A wrapper decoder registered first, like `hevc_rpivid` (patch 1001), because `mmaldec.c` has no CPU fallback: it takes MMAL for 8-bit 4:2:0 ≤ 1920×1088 when `/dev/vchiq` is up and the slot is free, otherwise native `h264`. The export path, the clock hold | 3-5 d | 1080p30 under 100 % of one core (CPU decoder: 159 %); 1080p60 ≥ 24 fps shown. Kill ffplay in mid-decode 10×: 0 faults, the quarantine drains, the clock returns to the boot rate. 30-min play with flat memory. `FFMPEG_MMAL=0` forces the CPU |
| 4 | **WebKit media path** | WebKit's FFmpeg player (`webkit_wpe/patches/webkit-video/0030`) finds decoders through `avcodec_find_decoder`, as it found `hevc_rpivid`. Rebuild USE video against the new `ffmpeg/` prefix. MSE (0032) later | 1-2 d | An H.264 `<video>` page and an HLS stream play on HDMI, and the log names the decoder. A second video on the page decodes on the CPU. Closing the tab mid-play: 0 faults |

Total: about 3-4 weeks, L as in the addendum.

## 5. Risks and open questions

- **R1. A server crash corrupts memory.** The firmware writes the slots and in-flight bulk pages for the rest of the
  boot. If `rpi4-vchiq` dies, the kernel reuses pages the VideoCore still writes. This is the top risk.
  - Mitigation: no exit path, fatal errors park the server, and the code stays small.
  - Experiment for checkpoint 1: take the slots from firmware memory (`ALLOCATE_MEMORY`/`LOCK_MEMORY`), mapped with
    `MAP_PHYSMEM` as the `rpi4-kms` "pan" backend maps firmware framebuffers. Such pages never return to the Phoenix
    allocator. Not known: whether the firmware accepts slots in its own heap.
- **R2. VideoCore memory may be short** (the relocatable heap is unmeasured). Fallback: `gpu_mem=160`.
- **R3. A missed 32-bit MMAL field** fails silently in the firmware. The harness checks layout, not pointer semantics,
  so checkpoint 2 runs with MMAL logging on.
- **R4. Firmware drift.** MMAL behaviour, including the sub-black quirk, depends on `start4.elf`. Record
  `vcgencmd version` in each manifest.
- **R5. The archived userland.** Our patches are permanent. Pin the archive sha256 in `port.def.sh`.
- **R6. Clock and heat.** 500 MHz raises SoC power. Watch `/dev/throttled` in the 30-minute run. Whether the firmware
  lowers the rate by itself is not known.
- **Q1.** Do 2 extra output buffers survive seeks and 1080p60 bursts? Measure in checkpoint 3.

## Field notes 2026-11-20 (measured on the same board by the other OS port's VCHIQ + MMAL work)

These change the design's operating rules, not its structure:

1. **Build VCOS with `NDEBUG`.** Otherwise `vcos_assert` calls `abort()` inside the client process.
2. **One H.264 decoder system-wide.** At the default `gpu_mem=76` one 1080p decoder fits; a second fails with
   `ENOSPC`, and its error flood can wedge the firmware's MMAL service until reboot. Serialise with a file lock and
   fall back to the software decoder.
3. **Choose by pixel rate.** The firmware decoder manages ~69 fps at 1080p, so 1080p60 dropped 216-538 frames,
   while the software decoder played 1080p60 with 0-11 of ~1500 dropped at ~2 cores. Rule: above 1080p at 45 fps,
   decode on the CPU.
4. **`gpu_mem=128`** is needed for a decode and an encode at once.
5. **Encoder:** `h264_mmal`'s `initial_qp > 0` silently disables rate control.

Tracked as task 48 of [the 2026-11-20 learnings addendum](../inprogress/pi4-learnings-backlog-addendum-2026-11-20.md).
