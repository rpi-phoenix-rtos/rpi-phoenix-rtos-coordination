# Pi 4 learnings backlog, addendum (2026-11-17)

Written 2026-11-17. **Documentation only: no Phoenix code, image or Pi was touched to write this.**

This is an addendum to [pi4-learnings-backlog.md](pi4-learnings-backlog.md) (2026-11-01, tasks 1-32). It covers
only what the other OS port on the same board learned **after 2026-11-01**, plus the earlier tasks whose status
changed because of it. Numbering continues from 33. Format, priority and effort scales are those of the first file
(High / Medium / Low; S = under a day, M = 1-3 days, L = a week or more). Phoenix paths are relative to `sources/`
unless they start with `docs/`, `scripts/` or `.buildroot/` (those are relative to the coordination repo root). Evidence from the second port is cited as "second-port commit
`<hash>`" or by the topic of its write-up. Phoenix evidence comes from reading source and docs; nothing was run.
Items say "not checked" or "inferred" where that applies.

**The main news.** The cold-start whole-SoC freeze that the first file called "still unexplained" is **root-caused
and fixed**. It was a kernel read of **uninitialised DRAM**, not PCIe, xHCI, the GPU or the firmware. A
translation table the kernel installed in TTBR0 for kernel-only threads came from an early page allocator that
does not clear pages (second-port commit `d1a616afac`), so the table walker followed whatever the RAM held. A
warm restart left the previous boot's identical zeros in that page. A long power-off, or a short one on a hot
board, left garbage. The method that found it (task 33) costs minutes per trial instead of the 30-minute off
windows of task 1. Reading Phoenix with that lesson in mind turned up one concrete exposure, which task 33
describes.

## Index

| # | Priority | Task | Effort |
|---|---|---|---|
| 33 | High | Uninitialised memory: zero the kernel `.bss` in plo; RAM-poison provocation from plo | S-M |
| 34 | High | xHCI DMA must stay below 3 GiB even with a 4 GiB inbound window | S-M |
| 35 | High | SD writes in 512 KiB commands; find the rest of the write gap | M |
| 36 | High | H.264 hardware decode through VCHIQ + MMAL (the BSD-licensed route) | L |
| 37 | Medium | Restart and power-off through the BCM2711 PM watchdog | S |
| 38 | Medium | WiFi SDIO: stop busy-polling every data block | M |
| 39 | Medium | Mesa V3D: no BLAKE3 per shader-variant cache lookup | S |
| 40 | Low | Secondary-CPU start: one builder for the shared boot tables | S |
| 41 | Low | USB removal robustness: ghost SuperSpeed devices, hub that stops answering | S-M |
| 42 | Low | Scan-out planes: buffer reuse and unsynced-flip rules for the planned video plane | S (reference) |

Then: status changes to tasks 1-32, methodology additions, measured-small results (do not chase), and what Phoenix
already has.

---

## High

### 33. Uninitialised memory: zero the kernel `.bss` in plo; RAM-poison provocation from plo

- **Learned.** The freeze came only after the Pi had been off 2-30 min, or a few minutes when hot. Each cold
  trial cost ~36 min. The breakthrough was a bootloader step that **fills free RAM with pseudo-random data before
  the OS loads**. With it, **warm** boots froze **3 of 3** (warm boots before: 0 of 17+), in the same places as the
  cold freezes. The bisect zeroed halves of the **same** random stream, because a fresh fill restarts its sequence
  and the hang depended on the values. An automated halving reached **one 4 KiB page**: ~11 steps at ~9 min each
  for SD boots, and 14 steps at ~7 min for the network-boot form, which landed on a different page. A kernel
  debugger then showed what lived there. Both pages were the same object, the first early page the kernel took.
  Root cause: the empty TTBR0 table was never cleared (second-port commit `d1a616afac`). With it cleared,
  **10 of 10** cold boots after 30 min off answered the liveness probe at 200 s. Before the fix, up to 2 of 3 cold
  boots froze. Hardening landed alongside: the loader clears the tail of its PE header page, which
  a firmware need not load (`f593b2ae43`), and two bootloader patches copy the whole declared header and hand out
  cleared pages. The per-CPU heartbeat (task 2) told the story: three CPUs stuck early in the kernel, one still
  beating for 130 s. So this was not a stalled interconnect.
- **Why for Phoenix.** The exact defect does not exist here: the empty user table is cleared
  (`phoenix-rtos-kernel/hal/aarch64/pmap.c:950-952`), and so is the boot TTBR0 scratch page (`_init.S:621-623`).
  The class does exist. **Nothing found by reading clears the kernel's `.bss`.**
  - plo's `kernel` command copies only `p_filesz` of each segment (`plo/cmds/kernel.c:111-120`). The `.bss`
    segment of the current build has FileSiz 0 and MemSiz 0x2ce60 (`readelf -l` of
    `.buildroot/_build/aarch64a72-generic-rpi4b/prog.stripped/phoenix-aarch64a72-generic.elf`).
  - No clear was found in `hal/aarch64/_init.S` or in plo's `syspage_entryAdd()` (`plo/syspage.c:339+`).
  - The code shows the authors knew for one variable: `nCpusStarted` is zeroed through a flag in the code section
    because it "starts out uninitialized" (`_init.S:625-637`).
  - Another variable is not protected. `hal_smpPrimaryReady` is a `.bss` word
    (`hal/aarch64/interrupts_gicv2.c:431`), first written at `main.c:196`. Secondaries spin on it being non-zero
    (`_init.S:811-823`), and the comment there says that passing that gate early "races primary's vm subsystem and
    hangs the boot". A garbage non-zero word would open the gate at once.

  Phoenix's cycles power the board off for only 3 s (`scripts/test-cycle-netboot.sh:34`), and there is no software
  restart (task 37). Note that retention would not hide this particular word: the previous boot leaves it at 1
  (`main.c:196`), so retained DRAM would open the gate early on every 3-s-off boot. Since boots are reliable today,
  one of three things holds: something not found clears the word (the firmware, say), DRAM has decayed by the time
  the kernel runs, or the gate is not load-bearing. Which one is unknown. The plo clear below is right in all three
  cases: an ELF loader must zero `.bss`.
- **Do.**
  0. A one-line probe that prints `hal_smpPrimaryReady` (and a few other `.bss` words) at kernel entry, over 5 boots
     after the usual 3 s off. This settles the trilemma above.
  1. In plo, zero `[p_filesz, p_memsz)` of every loaded segment, as any ELF loader must, and check the program
     loader path the same way.
  2. Add a debug-only plo command (off by default) that fills all free DRAM with a seeded pattern before the kernel
     and programs are loaded. Exclude plo itself, the firmware-reserved range and the boot image.
  3. If boots then fail, bisect by zeroing halves of the same seeded stream.
  4. Grep the kernel for other `.bss` state read before it is written.
- **Verify.** 10 boots with the fill and 10 without, before and after fix (1), graded "alive at 240 s"
  (heartbeat or ICMP), not "reached prompt". Then 10 cold boots (30 min off, task 1's harness), all alive.
  `--scope core` rebuild; a manifest for the plo and kernel SHAs.
- **Effort.** S for the fix and the fill command; the runs are M. Boot-path change (owner rule on kernel risk).

### 34. xHCI DMA must stay below 3 GiB even with a 4 GiB inbound window

- **Learned.** The other port programs the PCIe inbound window as Phoenix does: RC_BAR2 4 GiB at PCIe address 0,
  BAR1 and BAR3 off, SCB0 size code 17 (4 GiB) in MISC_CTRL. Its USB disk driver still allowed buffers up to 4 GiB. A test on the 4 GB bench Pi
  took all but 320 MiB of free memory, then read a USB stick into every 1 MiB slice. **866 of 3392 slices came
  back wrong**, all in the last-allocated (highest) memory. The buffers kept their prefill pattern, with **no read
  error and no USB error**. The next write ended in xHCI **"Host System Error"**: the controller was dead and the
  stick unreadable until power-off. After the fix (bounce anything above 0xc0000000; second-port commit
  `61fe84cd93`): **0 of 3392 and 0 of 3520** wrong, 62-65 MB/s. The limit matches the device tree's `dma-ranges`
  (3 GiB). A lightly loaded session never handed the controller memory that high, so this shows up only under
  memory pressure.
- **Why for Phoenix.** Phoenix programs the same 4 GiB window (`phoenix-rtos-devices/usb/xhci/bcm2711-pcie.c:808,
  1196,1272`; SCB0 4 GiB at `:463-464`). The USB host's DMA memory has no placement control:
  - the uncached pool is `mmap(MAP_CONTIGUOUS | MAP_UNCACHED)` (`phoenix-rtos-usb/usb/mem.c:112-113`);
  - rings, contexts and scratchpads come from it (`xhci.c:1210-1336`);
  - payloads are staged through it (`usb/drv.c:506-523`);
  - the kernel allocator has no placement flag (`phoenix-rtos-kernel/include/mman.h:18-26`,
    `phoenix-rtos-kernel/vm/page.c`).

  Phoenix's own guide gives the large window as a safe alternative to staying low
  (`docs/knowledge/rpi4-os-development-guide.md:360-363`). The measurement above, on the same board model with the
  same window, says it is not. Phoenix runs on a 4 GB Pi. Whether its pool ever lands above 3 GiB is **not
  measured**.
- **Do.**
  1. Keep every USB DMA allocation below 0xc0000000. Use the retry loop `rpi4-kms` already uses for its below-1 GiB
     pool (`video/rpi4-kms/kms_bo.c:7-9`) or a kernel placement flag.
  2. Log the highest physical address ever handed to the controller.
  3. Correct the guide.
- **Verify.** A memory-pressure test before and after: allocate and touch all but ~300 MB, then read and write
  1 GiB on a stick with per-sector LBA tags and SHA-256 checks. Expect 0 wrong sectors and 0 HSE. Run it again
  under the GPU showcase load.
- **Effort.** S-M. Without the fix, the failure is silent data corruption and then a dead controller.

### 35. SD writes in 512 KiB commands; find the rest of the write gap

- **Learned.** These measurements used the same card on the same board.
  - **Linux** issues 512 KiB per command and writes **32.7-33.9 MB/s**. Limited to 128 KiB per command, Linux
    writes **23 MB/s**.
  - **The other port's driver** sent at most 128 KiB per command, for two reasons: 32 DMA segments of mostly single
    pages, and 128 KiB bounce buffers (this board's EMMC2 reaches only the first GiB, so most buffers bounce). It
    wrote raw at 24.3 MB/s.
  - **With 128 segments and 512 KiB bounce buffers** (second-port commits `b04df67eaf`, `265d2b03dd`): raw writes
    **24.3 → 35.6 MB/s** (median, above Linux), 1 MiB reads 35 → 43-44 MB/s, 128 KiB requests 1-2 MB/s slower.
  - **The file layer** also capped requests at 32 vectors (`72ea4b2993`, `afd7f0590c`). Lifting it took
    file-system writes of 1 GiB + fsync from 31.3 to 34.2 MB/s and 128 KiB writes from 19.8 to 30.9 MB/s.
  - **Checks.** SHA-256 matched 13 of 13 across both OSes, and power cuts behaved as before.
  - **A trap on the way.** The I/O layer saved a vector length in a **16-bit field**, so a 128 KiB bounce vector
    came back as 0. Writes hung on another controller until this was fixed (`3a0be104e3`).
- **Why for Phoenix.** The driver caps every command at 128 KiB: `SDCARD_MAX_TRANSFER` (`storage/bcm2711-emmc/
  sdcard.h:21`), a 32-page guard on the contiguous staging buffer (`sdcard.c:245-249`), and 128 KiB cache lines
  (`sdstorage_dev.c:49-52`). End-to-end `dd` writes measure **12.7-12.9 MB/s**. The support matrix's SD row already
  names "raising `SDCARD_MAX_TRANSFER`" as the next lever (`docs/pi4-hardware-support-matrix.md:71`). Caveat:
  Phoenix's figure goes through ext2 and libcache, while 24.3 → 35.6 is raw device writes. So only part of
  Phoenix's gap is command size. The rest is unmeasured: write-through per request, libext2, IPC.
- **Do.**
  1. Measure raw device writes at 128 KiB on a scratch partition next to the ext2 `dd`, to split the gap.
  2. Raise to 512 KiB: ADMA2 descriptors, a 512 KiB contiguous staging buffer below 1 GiB (task 16's placement
     check applies), and matching cache lines.
  3. Audit every length and count field on the path for 16-bit or 128 KiB assumptions.
- **Verify.** Raw and ext2 write MB/s before and after (n ≥ 3, median). SHA-256 of written data read back by Linux
  in the flash lane. `e2fsck` clean. Task 6's power-cut test. Boot time unchanged.
- **Effort.** M.

### 36. H.264 hardware decode through VCHIQ + MMAL (the BSD-licensed route)

- **Learned.** The other port took H.264 from nothing to a working player path in one day (2026-11-05, 08:49 to
  22:12):
  - **Kernel side.** FreeBSD's `sys/contrib/vchiq` core (Broadcom files dual BSD-3/GPL-2, used under BSD-3; the OS
    glue BSD-2) became a kernel driver with the Linux/FreeBSD ioctl ABI. Broadcom's archived `raspberrypi/userland`
    `libvchiq`/`libmmal`/VCOS (BSD-3) runs on it with seven patches:
    - zero-initialised `pthread_once`/mutex objects that only Linux accepts;
    - 64-bit cleanups;
    - the VideoCore's **32-bit message layouts on arm64**, with 32-bit handles for host pointers (37 structures
      checked against the 32-bit original);
    - an unlock-without-lock bug.
  - **Player side.** FFmpeg's `h264_mmal`, with 2 extra buffers instead of 10, because 10 overflowed the
    VideoCore's 76 MB.
  - **Bring-up facts.** The ARM side allocates the slot memory (below 1 GiB, uncached, never freed afterwards) and
    passes its bus address with mailbox tag `VCHIQ_INIT` 0x00048010. The doorbell is GIC interrupt 66. A bulk echo
    of 1 MiB takes 7.3 ms round trip.
  - **Decode speed.** 720p 111 fps and 1080p 53 fps at ~8 % of one core, at the H.264 block's post-boot 250 MHz.
    Holding the block at 500 MHz (firmware clock 6, while a decoder is open): **171 / 79 fps**. The CPU decoder on
    4 threads gives 137 / 66 fps for 15-18× the CPU.
  - **Bit-exactness.** 900 of 900 frames equal FFmpeg's, except a firmware quirk: output samples 1-15 come out one
    lower.
  - **In the player.** 1080p30 needs 88 % of a core instead of 159 %. For 1080p60, 24 fps are shown, where the
    CPU decoder collapses.
  - **One crash fixed.** A client killed with bulk transfers queued crashed the kernel until its buffers passed to
    kernel ownership. The VideoCore still writes them.
  - **Licensing.** Linux's `vchiq-mmal`, `bcm2835-codec` and `vc-sm-cma` are GPL-only and were used only as
    behaviour references.
- **Why for Phoenix.** Phoenix banked hardware H.264 as "a VCHIQ/firmware wall (~27k LOC, WiFi-scale)"
  (`docs/misc/2026-08-27-owner-decision-queue.md:41-47`). The matrix says H.264 has no MMIO decoder and 1080p H.264
  on the CPU drops frames (`docs/pi4-hardware-support-matrix.md:82`). The BSD route above removes most of that
  cost: a reused core, an unchanged userland library and an FFmpeg decoder that already exists. Phoenix's HEVC path
  (rpivid) stays as it is. In Phoenix, VCHIQ would be a user-space server:
  - slots from a contiguous below-1 GiB pool, as `kms_bo.c` does;
  - the doorbell through `interrupt()`;
  - `VCHIQ_INIT` through `rpi4-vcmbox`;
  - an ioctl shim over message passing for `libvchiq`.

  `gpu_mem` is 128 here (`phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt:60`), not 76, but
  the triple framebuffer takes part of it. Size the decoder's buffer count against what is left. Effort is
  inferred: no Phoenix code exists.
- **Do.**
  1. Write a design note (server and IPC shape, dying-client rule, licence check against
     `project_prepublication_licensing`).
  2. A `GCMD version` round trip.
  3. `libmmal` H.264 decode to memory, with an MD5 check against FFmpeg.
  4. `h264_mmal` in `video_player` and the WebKit media path, with the H.264 clock held while a decoder is open and
     the CPU decoder as fallback.
- **Verify.** Bit-exact frames, allowing for the sub-black quirk. fps and CPU at 720p and 1080p. Kill a client
  mid-decode 10 times with 0 faults. A 30-min play with flat memory. The firmware version recorded.
- **Effort.** L.

---

## Medium

### 37. Restart and power-off through the BCM2711 PM watchdog

- **Learned.** Started through U-Boot, the other port's board had no PSCI: no `/psci` node, and the secondaries
  start from a spin table. Restart and power-off go through the power-management block's watchdog (second-port
  commit `0b22f22f5b`; the register facts come from NetBSD's and FreeBSD's BSD-2 drivers and Linux's
  `bcm2835_wdt`, with no code copied):
  - **Reboot** starts the watchdog with a ten-tick timeout and a full-reset configuration.
  - **Power-off** does the same with "partition 63" in the reset-status register, which the firmware takes as
    "halt".

  Results: restart **11 of 11** (back in the bootloader in 28-32 s), power-off **4 of 4** (UART silent, HDMI
  black). Also learned: a failed PSCI call may clobber x1-x17 (SMCCC 1.0), which matters if a PSCI path is ever
  added.
- **Why for Phoenix.** `hal_cpuReboot()` halts in place: "No reboot yet on BCM2711 … no EL3 PSCI monitor is
  present" (`phoenix-rtos-kernel/hal/aarch64/generic/generic.c:141-149`), although `pctl_reboot` exists
  (`:48-53`). Phoenix ships its own armstub at EL3 (`phoenix-armstub8-rpi4.S`), so a PSCI service is possible, but
  the watchdog is far less work. A software restart also gives the bench warm boots without the smart plug, which
  task 33's provocation runs need.
- **Do.** Implement the watchdog reset and halt behind `hal_cpuReboot()` and a power-off `platformctl`, and wire
  psh `reboot`/`poweroff`.
- **Verify.** 10 restarts in a row, each reaching `(psh)%`. 5 power-offs (UART silent, HDMI black, the plug's
  current reading idle). The reset reason visible on the next boot.
- **Effort.** S.

### 38. WiFi SDIO: stop busy-polling every data block

- **Learned.** The other port's SDIO path polled the present-state register for every block and spun for transfer
  complete. During TCP transfers, spinning and the time reads it loops on were **37-58 % of the busy CPU**. The fix
  (second-port commit `6018671453`):
  - blocks of 256 bytes or more are moved by the interrupt handler, which acknowledges the latched bit first and
    then drains every ready block;
  - the caller sleeps until transfer complete;
  - a 1 ms fallback moves a missed block and counts it (0 events in 13 boots);
  - smaller blocks, such as the 64-byte firmware-download blocks, stay polled.

  Result: CPU cycles per MiB **−31 to −34 %** (226/183 → 151/126 M, up/down) and busy CPU 19.6/15.9 → 11.6/8.9 %.
  Throughput was unchanged at equal clock (49/46 Mbit/s); the 2.4 GHz access point is the limit there.
- **Why for Phoenix.** The frame path calls the polled block helpers (`wifi/rpi4-wifi/rpi4-wifi.c:2118-2154` →
  `diag_sdioCmd53Read/Write`, `:838-1010`, `:1105+`). Each waits through `diag_sdhciWaitSet()`, which spins
  `SDHCI_SPIN_ITERS` register reads and then polls with a timer (`:735-760`). The card interrupt exists
  (`:5678`), but it signals frames, not data blocks. Phoenix moves more data (5 GHz TX 6.47 / RX 7.10 MB/s), so the
  share may be larger. **Not measured.**
- **Do.**
  1. `prof record` during a WiFi `lwiperf` run to get the spin share.
  2. If it is above ~20 %, move ≥ 256-byte blocks in the interrupt path. A Phoenix `interrupt()` handler runs in
     interrupt context; that it can safely drain the FIFO there is **inferred**, check it first.
- **Verify.** TX/RX MB/s and CPU busy % (n ≥ 3 each), 0 CMD53 errors, WiFi bring-up 5 of 5.
- **Effort.** M.

### 39. Mesa V3D: no BLAKE3 per shader-variant cache lookup

- **Learned.** `cache_hash()` in the V3D gallium driver ran BLAKE3 over the variant key and the stored digest on
  **every hash-table lookup**, that is, on every draw that changes state. It cost **5.1 % of a 3D game's menu
  CPU**. The fix seeds `_mesa_hash_data_with_seed(key)` with `_mesa_hash_data(digest)` and leaves equality
  unchanged (second-port Mesa commit `ebf1ee1ff1b`). Upstream Mesa main still had the BLAKE3 at the time.
- **Why for Phoenix.** The same code is in Phoenix's Mesa 26.2.0:
  `.buildroot/_build/aarch64a72-generic-rpi4b/port-sources/mesa_drm-26.2.0/mesa-26.2.0/src/gallium/drivers/v3d/
  v3d_program.c:968-975`. This joins task 3's three V3D fixes.
- **Do.** Add it as a `mesa_drm` patch next to task 3's.
- **Verify.** `prof record` of a GL game menu with no `blake3_*` symbols. fps of the showcase GL apps unchanged or
  better. No rendering difference (same-scene screenshots).
- **Effort.** S.

---

## Low

### 40. Secondary-CPU start: one builder for the shared boot tables

- **Learned.** Secondary CPUs start with their caches off, and two bugs came from that window:
  - The boot CPU had zeroed the secondaries' stacks with caches on and only *cleaned* them. A compiler change kept
    a value on the stack across caches-on, so the secondary read back a stale zero. A second "CPU 0" was the
    result: 2 panics in ~13 boots, then 0 in 24 after clean+invalidate plus re-deriving the CPU index from MPIDR
    (second-port commit `eb4bdd0ea4`).
  - The spin-table release address had to be cleaned to the point of coherency (`b00f6a9170`).
- **Why for Phoenix.** Both are already handled:
  - plo cleans each release slot with `dc cvac` (`plo/hal/aarch64/generic/hal.c:392-417`);
  - secondaries set SP only after the M|C|I write (`_init.S:588-619`);
  - the CPU index comes from MPIDR (`:642-644`).

  One new observation from reading: every core, secondaries included, zeroes and refills the shared boot tables
  with caches off (`_init.S:391-393`, `:453-459`: "race-free same-write convergence"). A core that arrives late
  can zero `KERNEL_TTL2` while core 0 already translates through it. The window is narrow and the problem has not
  been observed.
- **Do.** Let only the primary build the shared tables; secondaries wait on a cleaned flag before their MMU
  write.
- **Verify.** 50 boots with 4 CPUs up (`NUM_CPUS` lines), plus task 33's poison runs.
- **Effort.** S.

### 41. USB removal robustness: ghost SuperSpeed devices, hub that stops answering

- **Learned.** A tool that sends hub-class PORT_POWER requests replayed a field stall:
  - Clearing power on the VL805's SuperSpeed root port made the stick reappear on the USB 2 side, but the
    SuperSpeed device was **never reported gone**. It stayed as a "ghost" whose every command failed ("USB
    transaction" errors, 5 s control timeouts), retried about every 5.5 s.
  - A single ghost made each media-check round take ~25 s for all disks.
  - After repeated power cycles the VL805's internal USB 2 hub stopped answering port-status requests until the
    next boot.
  - A teardown that deleted an I/O worker while holding the lock that worker needed deadlocked (second-port commit
    `d3cc8e3fd1`; the most consistent explanation of 1 stall in ~2170 boots, not reproduced).
- **Why for Phoenix.** The teardown lesson is already applied: `umass` tears a device down outside its lock after
  an analogous self-deadlock (`storage/umass/umass.c:1475-1515`). The ghost and hub behaviour are hardware and
  stack interactions. Phoenix's root-port status handling (`phoenix-rtos-usb/usb/hub.c`) was **not checked**.
- **Do.** A port-power test tool, then make sure a SuperSpeed port that loses power reports a disconnect, and that
  a device whose every command fails is dropped after N failures.
- **Verify.** Power-cycle the port 20 times during `dd`: no stuck server, the stick back each time, other USB
  devices unaffected.
- **Effort.** S-M.

### 42. Scan-out planes: buffer reuse and unsynced-flip rules for the planned video plane

- **Learned** (full-screen GL and video on firmware planes, from the other port's display work):
  - A buffer is rendered into again only after a vblank at least 3 ms past its flip.
  - In unsynced mode the newest waiting frame replaces the pending one ("mailbox"), with one flip per vblank:
    130 fps rendered instead of a cap at 60, and 0 missed in 1200 synced frames.
  - Scan-out pages must be cleaned before they are mapped write-combining.
  - Known gaps: a YCbCr422 plane shows black, and an overlay stays black after a mode change until it is shown
    again.
  - SAND128 NV12 on a plane works only experimentally.
  - Gains on a plane were small (−2 to +3 % fps), so vsync, not speed, is the point.
- **Why for Phoenix.** Phoenix already has firmware planes, SMI vblank and a reserved pool (table at the end of
  the first file), and it cleans uncached pages at mapping time (`hal/aarch64/pmap.c:325-339`). Path C of the
  zero-copy video plan (an HVS plane scanning SAND out, `docs/gpu-new-lane/M10b-video-zero-copy.md:55`) is where
  these rules apply.
- **Do / Verify / Effort.** Use them as acceptance points when path C starts; no work before that. S.

---

## Status changes to tasks 1-32

| Task | Change |
|---|---|
| "Honest framing" | **Superseded.** The cold-start freeze was uninitialised DRAM read through an uncleared TTBR0 table (second-port commit `d1a616afac`). Confirmed with 10 of 10 cold boots alive. |
| 1 Cold-start reproducer | **Demoted to confirmation.** Task 33's poison-and-bisect is the primary method: minutes per warm trial instead of ~36 min per cold one. Keep the 30-min-off series as the final gate. |
| 2 Heartbeat | **Strengthened.** The heartbeat plus unbuffered serial showed three CPUs stuck and one beating for 130 s. That pointed away from the interconnect and towards software reading garbage. |
| 3 Mesa fixes | Unchanged, plus task 39. |
| 6 Power-cut test | Nothing new for Phoenix. The other port's upstream also landed a page-writer waiter fix next to its own fix; Phoenix's write-through SD path differs. |
| 7, 8 PCIe parity | **Lose their cold-start motivation.** The freeze was not PCIe. The other port made CLKREQ#, SSC and CRS-visibility switchable and left them off. Keep as Linux-parity cleanups and drop both to **Low**. |
| 10 Fault with a blocked signal | **Confirmed by others.** The other port's upstream made the same fix independently (unblock and reset to default before sending). Its kernel test ends both cases by SIGSEGV. Phoenix's FIXMEs are still at `vm/map.c:1212` and `proc/process.c:340`. |
| 16 SD bounce placement | Extended by task 35: the staging buffer must still end below 1 GiB when it grows to 512 KiB. |
| 17 Clock policy | Add: "fresh boot" benchmarks run during the firmware's ~2 min boot-turbo window and are not comparable with later ones; log the clock with every benchmark. If `force_turbo` is ever replaced by DVFS, a per-CPU-load governor left the shared clock at 750-1200 MHz for a fork/exec chain spread over four cores (fork+exec 4.6 → 6.6 ms). The fix: also go to maximum when summed active time reaches 55 % of one CPU. Phoenix's HEVC clock request already exists (`video_player/files/rpivid/src/rpivid_hw.c:187-206`). |
| 20 HDMI audio | Unchanged. |
| 23 USB > 64 KiB | **Expected gain lowered.** The other port's 62-97 MB/s USB figures had been measured on corrupt data (missing cache maintenance for USB payloads; see the Already-done table). On correct data: reads 60-81 MB/s, writes 49 MB/s. Linux reads the same stick at 43 MB/s with 256 KiB commands. Re-measure Phoenix's 59.1 MB/s with a data check first; Phoenix copies through uncached buffers, so it is probably right (**inferred**). |
| 26 Read-ahead | **Phoenix is at parity.** The other port added a 64 KiB aligned read-around for file faults (a big-application cold start 14.05 → 8.36 s; mapped files 8-10 → 23-32 MB/s). It excluded its compressed package file system, where the same read-around cost +0.49 s. Phoenix already fetches a 16-page cluster forward (`vm/object.c:716-760,960-992`). Only "grow for in-order faults" remains, open in both. Drop to **Low**. |
| 27 Dynamic loader | Add a measurement: DT_RELR shrank a 12 MB relocation table to 0.25 MB and cut a cold start by 1.85 s from a slow-fault file system (−0.75 % warm; for a packaged copy ~0.3-0.5 s, derived). It still matters only for Phoenix's `dlopen` users. |
| 28 Thread-stack reuse | Add tests if it is ever built. Reuse exposed two latent bugs in the other port: a signal pending at a new thread's first return to user mode (second-port commit `5d9132a49c`), and a lock waiter that missed its hand-off after an interrupted wait (`2bf2f773ba`). Phoenix's locks are futex-style without hand-off, so the second bug does not apply. |
| 14, 15 Boot time | New figures for the comparison: on the shared bench the EEPROM still spends ~58 s before the bootloader (network tried first), then bootloader → desktop ≈ 6.4 s (4 card boots). Nothing new to do beyond the existing tasks. |
| "Already done": SMP placement | The other port's scheduler fix for busy threads stuck on one core predates 2026-11-01 and is in the first file's table. Nothing new. |
| (new check) Signal pending at a new thread's first user entry | The other port delivered no pending signal on a new thread's first return to user mode, only at its first exception (second-port commit `5d9132a49c`). **Not checked** for Phoenix: confirm that the first switch of a new thread to EL0 runs the same signal check as a syscall or exception return. S. |
| 30 Wake IPI | **Strengthened.** The other port measured that a woken high-priority thread does not displace lower-priority work on another core. Phoenix, without a wake IPI, waits up to a 1 ms tick for the same case. |
| 32 Methodology | See the additions below. |
| Others | Nothing new. |

## Methodology additions

1. **Poison memory, then bisect by zeroing parts of one stream** (task 33). Values matter, so never compare
   fills that restart a generator.
2. **Never trust a whole-system profile before checking its attribution.** The other port's system profiler
   dropped every program started after it: 65 % of a session's busy ticks were "unknown", because a 128-byte event
   buffer had no room for a 64-bit pointer (second-port commit `f6c52282df`). Check Phoenix's `prof report`
   unknown share before ranking anything.
3. **Every throughput figure needs a data check.** The other port reported 78-90 MB/s USB reads for weeks while
   every read returned different data.
4. **Log the clock.** The firmware runs at maximum for ~2 min after power-on, so "fresh" and "later" numbers
   differ by up to 1.4× for reasons unrelated to the change.
5. **Screen savers and DPMS hide windows.** Windowed benchmarks taken behind a blanked screen were 13-19 % faster.
   Bench images must disable blanking (**not checked** for Phoenix's X and labwc sessions).
6. **Kill by process ID, never by `ps` column position.** A script killed an argument, not the game, so "after
   quit" rows were measured with the game still running.
7. **Do base-rate arithmetic before quoting a clean bench.** One stall in ~2170 archived boots means 40 clean
   boots prove nothing; say so.
8. **Never mount the bench card read-write from a test.** A "mount all volumes" call in a session mounted the
   owner's SD card for a minute.

## Measured small or negative (do not chase without new evidence)

| Idea | Result on the same board |
|---|---|
| LTO of the whole userland | Games, GPU, launch and Python ±1-4 %, against +17 % build time. Not adopted. |
| GCC 13.3 → 16.2 | Neutral overall; two first launches 15-25 % faster. (Phoenix: already on 16.2.) |
| V3D jobs on concurrent per-unit queues | +1-3 % (a GL game 79.1 → 80.0 fps; glmark2 within a few %). |
| Holding the ARM at 1500 MHz during WiFi transfers | +0.6 % up, +1.1 % down: within boot-to-boot spread. |
| Trace-based prefetch of a large application | Pre-reading the whole package (5.2 s) costs more than it saves (3.2 s). |
| Read-around on a compressed, chunked file system | +0.49 s cold start: the aligned window spans two chunks. |
| GL on a scan-out plane instead of a copy | −2 to +3 % fps; vsync is the gain, not speed. |

## Already done in Phoenix (no task)

| Learning (second-port commit) | Phoenix evidence |
|---|---|
| The empty TTBR0 table must be cleared (`d1a616afac`) | `hal/aarch64/pmap.c:950-952` (memset, then installed by `_pmap_switchUserOut`, `:440-455`); the boot scratch TTBR0 page is zeroed, `_init.S:621-623,756-769` |
| Spin-table release address cleaned to PoC (`b00f6a9170`) | `plo/hal/aarch64/generic/hal.c:392-417` (`dc cvac` per slot, then `sev`) |
| No caches-off stack use by secondaries (`eb4bdd0ea4`) | SP set after M\|C\|I, `_init.S:588-619`; index from MPIDR, `:642-644` (see task 40 for the table race) |
| USB payload coherency on a non-snooping VL805 (`eabad7a921`, `b90cb3f0d4`) | Payloads copied through uncached buffers (`phoenix-rtos-usb/usb/drv.c:506-523,773`); stale lines evicted at pool creation (`usb/mem.c:112-133`) |
| Clean before mapping memory uncached or write-combining (`3286ff573d`) | Kernel cleans and invalidates on every Normal-NC mapping, `hal/aarch64/pmap.c:325-339` |
| Strong compare-and-swap on LL/SC (a weak CAS lost 53.7 % of increments with outline atomics off) | Phoenix builds with `-mno-outline-atomics` (`phoenix-rtos-build/target/aarch64.mk:20`), the exposed configuration, but every `__atomic_compare_exchange_n` found passes weak = 0 (`libphoenix/sys/ulock-internal.h:55`, `pthread/pthread.c:1357-1582`, `pthread/sem_unnamed.c:84-219`). Rule for new code and ports: no weak CAS outside a retry loop. |
| Lock waiter missing a hand-off after an interrupted wait (`2bf2f773ba`) | Not applicable: futex-style locks re-check the word, no hand-off (`libphoenix/sys/ulock-internal.h:52-90`) |
| Device teardown must not wait for a worker while holding its lock (`d3cc8e3fd1`) | `storage/umass/umass.c:1475-1515` (teardown outside the lock, after an analogous self-deadlock) |
| Hold the video block's clock at maximum while decoding | HEVC: `video_player/files/rpivid/src/rpivid_hw.c:187-206` |
| File-fault clustering (`a38b19efab`) | 16-page cluster per fault, `vm/object.c:716-760,960-992` (see task 26) |
| Firmware planes, SMI vblank, reserved scan-out pool | First file's table; `video/rpi4-kms/kms_bo.c:7-9`, `kms_vblank.c` |

Not applicable to Phoenix by design: the other port's loader and bootloader fixes themselves (PE header page, EFI
page clearing; Phoenix boots plo, not UEFI), its file-system journal and vnode fixes, its static-TLS block for
shared images (inferred: Phoenix's showcase programs are static, and its loader does not support TLS in shared
objects, first file task 27, `libphoenix/dl/dl.c:38`), its compressed-package read-ahead, and its
cross-architecture regression checks.
