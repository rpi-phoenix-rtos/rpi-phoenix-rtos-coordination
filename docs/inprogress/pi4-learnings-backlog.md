# Pi 4 learnings backlog: what a second OS port on the same board taught us

Written 2026-11-01. **Documentation only: no Phoenix code, image or Pi was touched to write this.**

**Addendum 2026-11-17:** items 33–42 and status changes to 1–32 are in
[pi4-learnings-backlog-addendum-2026-11-17.md](pi4-learnings-backlog-addendum-2026-11-17.md).

In October 2026 a second operating-system port was brought up on the **same Raspberry Pi 4 Model B** (same
bench, same VL805/GENET/EMMC2/V3D/rpivid hardware, same VideoCore firmware). In two weeks it hit and measured
a large number of board-level problems. This file turns what it learned into **future tasks for Phoenix**,
after checking each against the Phoenix source and docs. Nothing here is an owner decision or a commitment:
it is a ranked pick-list. It is the only file of its kind; per the folder convention
([README.md](README.md)) it should move to `docs/misc/` when the week rolls over, with a pointer left in the
weekly log.

**How to read it.** Priority is High / Medium / Low, by expected value for Phoenix (correctness and
debuggability first, then speed, then features). Every task has: what was learned (with numbers),
why it may matter here (with Phoenix file or doc evidence), what to do, how to verify on the Pi, and an effort
guess (S = under a day, M = 1-3 days, L = a week or more). Phoenix paths are relative to `sources/`; Linux paths
to `external/linux` (rpi-6.18.y, `pcie-brcmstb.c` as in that tree). Evidence from the second port is described
by its measurement only; where a number comes from a different workload than Phoenix's, the caveat is stated.
Phoenix evidence was gathered by reading source and docs (nothing was run); items marked "inferred" or "not
measured" say so.

**Honest framing.** The second port's worst open problem, a whole-SoC freeze in the first minutes after the
Pi was off for a while, is **still unexplained there**: its own PCIe/xHCI, V3D, rpivid, cpufreq, thermal, WiFi,
Bluetooth, audio, SD and display drivers were each blocked in turn and the freeze still came. The PCIe/VL805
comparison below therefore lists *differences from Linux*, not proven causes. They are cheap parity items; do
not expect any one of them to be a cure.

**Phoenix does not use U-Boot** (firmware `start4.elf` loads `kernel8.img` = plo and the `loader.disk`
initramfs: `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt:6-9`). The second port's
U-Boot-only findings (its `usb start`, `pci enum`, autoboot countdown, EMMC2 command wait, GENET transmit-delay
and pixel-order patches) are therefore **not applicable as patches**; the transferable lessons are extracted
as tasks 1, 14 and 16.

**Workflow reminders for whoever picks a task up** (from `CLAUDE.md`): Pi cycles go through
`./scripts/pi-bench-lock.sh`; use `./scripts/test-cycle-bench.sh <N> <label>` for rates and
`./scripts/uart-summary.sh` to grade; `./scripts/check-label-free.sh <label>` before a series;
`./scripts/syntax-check.sh <repo> <file>` while a bench runs; after any *committed* core change rebuild with
`--scope core` (stale-core hazard). Kernel changes keep the rollback discipline (manifest per validated step).

## Index

| # | Priority | Task | Effort |
|---|---|---|---|
| 1 | High | Cold-start reproducer: controlled power-off windows, long observation, temperature log | M |
| 2 | High | Console hardening: bounded UART wait + per-CPU heartbeat from IRQ context | S-M |
| 3 | High | Port the three Mesa V3D/V3DV fixes (correctness + 3.6x shader compile); ship a warmed shader cache | M |
| 4 | High | HEVC decode: pipeline the two rpivid phases | M-L |
| 5 | High | Gigabit path: find and cut the per-frame cost (raw TCP ~300 Mbit/s vs 918 elsewhere) | M-L |
| 6 | High | Power-cut test of the SD root (data left unwritten) | S |
| 7 | Medium | PCIe: reconcile the NOTIFY_XHCI_RESET policy with Linux (A/B) | S-M |
| 8 | Medium | PCIe: link bring-up parity with Linux (retry, CLKREQ#/SSC/4 s timeout, CRS, UBUS remap) | M |
| 9 | Medium | xHCI: TRB overfetch padding for the VL805 | S |
| 10 | Medium | EL0 exception hardening: fault loop with a blocked signal, sigreturn PSTATE | S |
| 11 | Medium | GIC IPI ordering and shared-priority race | S |
| 12 | Medium | Instruction-cache sync audit (clean to PoU, IDC/DIC) and exec cost | S |
| 13 | Medium | libphoenix string routines: replace byte loops with Arm Optimized Routines | S-M |
| 14 | Medium | Boot-time budget: EEPROM boot order, firmware delays, plo/kernel console cost | S |
| 15 | Medium | Console output through an IRQ-drained ring (boot time) | M |
| 16 | Medium | SD: log the DMA/PIO decision and bounce-buffer address; small-read latency | S |
| 17 | Medium | Clock policy proof: sample ARM/core/V3D clocks across the 2-minute mark | S |
| 18 | Medium | V3D server: cache maintenance outside the device lock | S-M |
| 19 | Medium | Physically contiguous allocation late in a session: metric and stress test | S-M |
| 20 | Medium | HDMI audio (MAI) driver | L |
| 21 | Medium | Random numbers: fast DRBG over the hardware RNG; kernel seed for TD-28 | S-M |
| 22 | Medium | Saved clock across reboots (no RTC) | S |
| 23 | Medium | USB mass storage: transfers above 64 KiB | S |
| 24 | Medium | Audit asserts with side effects and the shipped debug level | S |
| 25 | Medium | Hardware performance counters (PMU) driver and `pmustat`-style tool | M |
| 26 | Medium | Adaptive read-ahead for mapped files; cap any prefetch | M |
| 27 | Low | GNU hash, `-Bsymbolic-functions` and packed relocations in the dynamic loader | M |
| 28 | Low | Thread-stack reuse (measure first) | S-M |
| 29 | Low | EL0 `mrs` of ID registers: SIGILL or emulate; MDCR_EL2 for PMU/debug | S |
| 30 | Low | Wake IPI for idle CPUs and WFE event stream | S |
| 31 | Low | Bluetooth: route `rpi4-hci` through the mailbox server; acceptance tests for a future host stack | S |
| 32 | Low | Methodology checklist for the bench (documentation) | S |

Items the second port needed that Phoenix **already has** are in the table at the end.

---

## High

### 1. Cold-start reproducer: controlled power-off windows, long observation, temperature log

- **Learned.** The second port's freeze came only in the first minutes after the Pi had been **off 2-30 min**
  (not back to back). Quick boot-and-off series stayed cool and clean (0 of 44 overnight); 30-min-off trials
  froze about 1 in 3 for network boots and 2 of 3 for SD boots watched 240 s. Earlier "1 of 5" and "12 of 12
  clean" SD figures were **wrong**: the watch ended ~15 s into the kernel while freezes landed anywhere from 7 s
  to 133 s into it, so only 15-25 % of them could show. A hot Pi (SoC at 89-90 C) off a few minutes froze
  22 of 22 network boots while a bootloader step ran `usb start` (0 of 8 without it). A Linux control booted
  through the same path under full network load survived 4 of 4 trials. Each data point costs ~30 min, so
  the harness itself decides whether the answer ever arrives.
- **Why for Phoenix.** Phoenix's cycle scripts power the Pi off for only `RPI4B_POWER_SETTLE_SECS` = 3 s
  between cycles (`scripts/test-cycle-netboot.sh:34`); `--idle-secs` in `test-cycle-bench.sh` is a wait *after*
  boot, not an off window. No Phoenix log was ever taken after a long off. Phoenix's own history points the same
  way: `docs/done/2026-05-23-usb-bridge-degraded.md` (bridge poisoned after ~12 rapid cycles, recovered only
  after a long off), `docs/done/2026-05-23-usb-bridge-cooldown-curve.md` (about 30 min idle recovers it), and
  `docs/done/2026-06-17-unattended-work-log.md:678-689` (28 V3D render timeouts after back-to-back GPU boots,
  0 after a 30 min cold rest). The Phoenix C1 heap-corruption rate was also "a function of the bench schedule"
  (`docs/c1-heap-corruption.md`). None of this is proven to be the same phenomenon, but every Phoenix
  intermittent was measured under one schedule only.
- **Do.** Add `scripts/test-cycle-cold.sh <N> <label> --off-secs 1800 --capture-secs 300`: read the SoC
  temperature from `/dev/thermal` before powering off, power off, wait the window (`pi_power_off.sh`), boot,
  capture with timestamps, and probe liveness at ~200 s (Phoenix has no kernel-debugger prompt: use ICMP echo
  from lwip plus the task-2 heartbeat). Run interleaved arms (A and B alternating, never all A then all B), plus
  a back-to-back control after each cold trial. Always grade "alive at the end of a fixed window", never
  "reached prompt".
- **Verify.** Result table: boots, froze, last timestamped line, last heartbeat per CPU, temperature before.
  Needs 8-10 trials per arm to see a 1-in-3 effect (hours; run overnight under the bench lock). Baseline first
  with the unchanged image; any later fix needs the same arm length.
- **Effort.** M (script S, the overnight runs are the cost).

### 2. Console hardening: bounded UART wait + per-CPU heartbeat from IRQ context

- **Learned.** (a) A UART that stops transmitting stopped the whole system: the first CPU spun in the UART put-char routine
  holding the output lock and every other printing CPU spun behind it (panic prints the same way). Bounding
  the FIFO-room wait (~200 000 status reads, then skip until the FIFO drains) removed that failure class.
  (b) A heartbeat kernel setting: each CPU writes `HB<cpu> <s> pc <pc> <mode> thread <id>` to the UART from its **timer
  IRQ handler every 2 s, past the output queue and its lock**. In a freeze it showed all four CPUs stopped at
  the same instant, each last seen idle: not code a CPU ran. Without it the freeze looked like "output
  stopped in the PCI listing" while the CPUs had been further on (output was buffered). (c) A switch for
  fully **unbuffered** output is needed to locate a freeze at all.
- **Why for Phoenix.** The kernel console is synchronous with interrupts masked and the console lock held
  (`phoenix-rtos-kernel/hal/aarch64/generic/console.c:83-86,120-122`) and the FIFO-full wait is unbounded:
  `while (FR & TXFF)` at `hal/aarch64/generic/pl011.c:61-62` and `console.c:45`. A dead UART hangs every CPU
  that prints. There is no heartbeat, per-CPU watchdog or serial emergency key in the kernel (search under
  heartbeat/watchdog/hang/debugger/sysrq found only compile-time `MSG_SEND_WATCHDOG`/`SPAWN_WATCHDOG` printed from
  thread context, `proc/msg.c:591`, `proc/process.c:1469`). Phoenix's only freeze-locating tool is "the last
  UART line", which the second port showed is misleading once output is buffered or lines are cut
  mid-character.
- **Do.** (1) Bound the FIFO wait (give up after N reads, remember the state, retry on the next character).
  (2) Kernel option `heartbeat` (boot argument or build flag, default off): from each CPU's tick handler print
  one short line every 2 s with a direct, lock-free PL011 write (poll TXFF with a bound). (3) Document that
  heartbeat + unbuffered is the standard pair for a hang hunt. Keep it an option so it ships disabled
  (upstreamability rule in `CLAUDE.md`).
- **Verify.** QEMU: heartbeat lines from 4 CPUs every 2 s; Pi: same, plus an induced hang (spinlock held with
  IRQs masked on one CPU) shows that CPU's heartbeat stopping while the others continue. `syntax-check.sh`
  while a bench runs; `rebuild --scope core` before the cycle.
- **Effort.** S-M, kernel boot-risk (owner call per `AGENTS.md`).

### 3. Port the three Mesa V3D/V3DV fixes; ship a warmed shader cache

- **Learned.** Three bugs found while running a wgpu/Vulkan application on V3D 4.2, all in Mesa itself and
  present in Phoenix's Mesa 26.2.0 base: (i) the V3D NIR-to-QPU compiler lowered a **vector `mov` copying only
  its first channel** (wrong results, found as a failed compositing-blend test; 91/91 GPU tests pass after the
  fix); (ii) **liveness and copy propagation recomputed in linear passes** made large shaders compile **3.6x
  faster** with identical generated code (the application's pipelines 3.5x faster); (iii) **V3DV: a render
  pass's supertiles stored over pixels outside its render area**, losing data in tiled texture uploads (float
  documents). Also: with Mesa's on-disk cache enabled, one app's compositor pipelines went **41.3 s -> 0.26 s**
  on the second start; a libEGL without a build-ID note crashed in `v3d_disk_cache_init`.
- **Why for Phoenix.** Phoenix ships Mesa 26.2.0 with patches 0001-0018
  (`phoenix-rtos-ports/mesa_drm/patches/`; `ports/mesa_drm/port.def.sh:8`). A search of that directory finds none
  of the three fixes (no supertile, copy-prop, liveness or vir_ patch). Phoenix's first-start cost is large:
  vkQuake first present **77.4 s cold vs 2.8 s warm** with 117 cache files
  (`docs/gpu-new-lane/G4-shader-cache.md:34-43`), STK ~300 s, and the support-matrix V3DV row notes "~3 min
  compile before the first frame". Eviction of superseded cache entries is absent (`G4-shader-cache.md:45-49`).
- **Do.** (1) Re-implement the three fixes against Mesa 26.2.0 (the bugs are generic: `src/broadcom/compiler`
  and `src/broadcom/vulkan`) as patches 0019-0021; fork-only per owner policy. (2) Check `--build-id` on every
  static ICD/libEGL that links the cache code. (3) Add a warm-cache pre-population step to the image build for
  the showcase apps (the cache key includes the driver build-id, so it must be generated per Mesa build) and a
  size cap/eviction (the G4 open item).
- **Verify.** shader-db style comparison: generated QPU code identical before/after for (ii); a minimal test for
  (i) (vector mov of a vec4 through a phi) and for (iii) (tiled upload into a sub-rectangle); `vkquake` cold
  first-present time vs the 77.4 s baseline; `test-cycle-bench.sh` with `--ready-line 'present 30'`.
- **Effort.** M (patch porting S per fix; rebuild and gate cycles dominate).

### 4. HEVC decode: pipeline the two rpivid phases

- **Learned.** The rpivid HEVC block decodes in two phases: phase 1 writes coefficient/PU data into two scratch
  buffers, phase 2 reconstructs. Submitting picture N+1's phase 1 into a **second scratch set** while phase 2 of
  picture N runs (strict FIFO so references are complete; the driver owns and bounds-checks every buffer) took
  1080p 20 Mbit/s to **114.8 fps at 4.0 ms CPU per frame (0.46 core)**, 1080p Main 10 80.4 fps, 2160p 30 Mbit/s
  38.2 fps, all bit-exact against the CPU decoder (CPU decoder on 4 threads: 25.5 fps). Interface shape: a
  `SUBMIT` that queues and a `WAIT` that collects.
- **Why for Phoenix.** Phoenix's decode is synchronous: `rpivid_hw_decode`
  (`docs/gpu-new-lane/M10b-video-zero-copy.md:218`), no scratch-set pipelining in `rpivid_hevc.c`/`rpivid_hw.c`.
  Recorded: **1080p 52.8 fps, 18.9 ms/frame, 60 % of one core**, of which 4.5 ms is the block and 10.5 ms the
  uncached de-tile (`docs/gpu-new-lane/M10-hevc-hwaccel.md:60-65`); the 1080p60 PeerTube gate painted only
  ~28-33 fps (`M10b:599-604`). Caveat: Phoenix's figure includes its de-tile into a display buffer, the other
  number does not, so the gain is not a clean 2x; but the block itself idles for most of each frame.
- **Do.** Add the submit/wait interface and two scratch sets to `rpivid`, overlap phase 1 of N+1 with phase 2
  of N, keep FIFO ordering; move the de-tile off the uncached path (separate task if still dominant).
- **Verify.** The existing bit-exact check (all frames equal to the CPU decoder, as done for the 8610-frame
  reel), fps and CPU% on 1080p and 2160p clips, `video-play` full screen and the gtk-video window, 30-min soak.
- **Effort.** M-L.

### 5. Gigabit path: find and cut the per-frame cost

- **Learned.** On the same GENET hardware, the second port reached **918 Mbit/s receive and ~914 transmit** once
  (a) receive buffers were cacheable with a per-frame invalidate (250 -> 735 Mbit/s receive), (b) 256 distinct
  buffers, no status blocks, EEE and PHY auto-power-down off, and (c) the kernel's paranoid debug checks were
  turned down (the allocator and mutex debug level cost ~30 % and ~15 % of a TCP sender's kernel profile;
  upload 600 -> 914, download 660 -> 918 Mbit/s). Transmit was CPU-bound in the kernel, not checksum-bound.
- **Why for Phoenix.** (a) and (b) are **already done** (cacheable RX with `dc civac`, 256 unique buffers, EEE
  off: `phoenix-rtos-lwip/drivers/bcm-genet.c:102,810-860,932,943,953-956,1127,1153`). But Phoenix measures raw
  RX 37.5 MB/s (~300 Mbit/s), raw TX 21.7 MB/s, NFS read 29.9 MB/s, NFS write 19.7 MB/s, against **112 MB/s NFS
  read for Linux on the same board** (`docs/misc/gigabit-nfs-perf-decision.md:3`). That doc attributes the gap
  to per-frame mutex syscalls (~4.5 us per lock pair) and the lwip path, i.e. the microkernel's IPC per frame,
  with the TX copy only ~11 % of it. A 3x gap on the dev lane (NFS root) is the biggest measured throughput
  deficit on Phoenix. (Figures date from August; later work may have moved them.)
- **Do.** First profile with the `prof record` tooling (`docs/misc/2026-10-06-phoenix-profiler.md`) during a
  `lwiperf`-style transfer and tabulate per-frame costs by component; then batch: take N frames per wake-up from
  the RX ring, hand the stack one lock round-trip per batch, and check the kernel debug level (task 24).
  Target >= 60 MB/s raw RX.
- **Verify.** `lwiperf` raw RX/TX and NFS dd read/write before/after (n >= 3, median), 0 faults; confirm
  `GENET_RX_CACHEABLE` stays 1 and the GPU framebuffer is not corrupted under load (the old "Policy B"
  regression, `docs/pi4-hardware-support-matrix.md` row 1).
- **Effort.** M-L (profiling S).

### 6. Power-cut test of the SD root

- **Learned.** The page writer's wait timed out and went straight back to waiting, so a file's last modified
  pages (up to 1 MB) stayed in memory until a `sync`; a 111 kB file was not on the disk 60 s later, and a
  package installed just before power-off lost its last 163 kB. Fixed to write what there is after the timeout
  (on disk within 5 s).
- **Why for Phoenix.** The SD root defaults to `LIBCACHE_WRITE_THROUGH`
  (`phoenix-rtos-devices/storage/bcm2711-emmc/sdstorage_srv.c:303`, `sdstorage_dev.c:898`;
  `libcache/cache.c:301-308` flushes on write), so the mechanism differs and may be fine. But a Phoenix memory
  note records "power-cut, no sync: -2 inodes / -2 blocks" (`project_pi4_sd_fullspeed_state`), which a code
  reading could not reconcile (it may be a libext2 metadata path rather than libcache). The NFS root has no
  write delay but libnfs's stable-write mode was not verified (source not unpacked).
- **Do.** Write the test, then fix whatever it shows: a script that writes a 4 KiB, a 111 kB and a 2 MB file on
  the SD root, cuts power (`pi_power_off.sh`) at t = 1, 5 and 60 s, reboots, `e2fsck`s and compares.
- **Verify.** 3 trials per delay; expected: file complete and fsck clean at every delay >= 1 s with
  write-through.
- **Effort.** S (test); the fix is unknown until it runs.

---

## Medium

### 7. PCIe: reconcile the NOTIFY_XHCI_RESET policy with Linux (A/B)

- **Learned.** Linux sends the VideoCore `NOTIFY_XHCI_RESET` (tag 0x00030058, value 0x100000) **at every xHCI
  probe and every resume**, unconditionally, **after PCI enumeration** (the PCI core has already sized the
  VL805's BARs), then sleeps 200-1000 us: `external/linux/drivers/usb/host/xhci-pci.c:638` (probe), `:864`
  (resume), `drivers/reset/reset-raspberrypi.c:52-58`. The firmware-version dword at config 0x50 is read only to
  pick quirks (`xhci-pci.c:253-261`). The second port reads the VL805's config space well before its notify
  and still got the vendor ID, firmware 0x138c0 and a BAR in both early freezes, so the notify was not its cause.
- **Why for Phoenix.** Phoenix differs on three counts, and the stated reasons do not hold up: it **skips** the
  notify when config 0x50 is non-zero (`usb/xhci/bcm2711-pcie.c:922-923`); its comment says Linux's
  `rpi_firmware_init_vl805()` does this check (that function **does not exist** in the rpi-6.18.y tree: `rg` finds
  nothing, and the reset controller notifies unconditionally); and a memory note says "never re-issue
  NOTIFY_XHCI_RESET" citing firmware issue 1617 (an external report, not a Phoenix measurement; the same note's
  own bring-up list, `project_bcm2711_pcie_64bit_bug`:112, puts the notify after BAR assignment, while the code
  puts it before BAR0 programming, `bcm2711-pcie.c:941-947` vs `:993`). Phoenix also keeps BUS_MASTER off until
  after the notify (`:894-912`) and polls the vendor ID for 0x1106 up to 300 ms after it (`:962-977`), which is sound.
- **Do.** A documented-divergence cleanup plus one A/B: (a) fix the comment and the memory note to say what
  Linux really does; (b) arm B: always notify (once per boot), after BAR assignment, with the Linux 200-1000 us
  settle plus the existing vendor-ID poll; arm A: today's code.
- **Verify.** 20 cold boots per arm through task 1's script; criteria: enumeration success, `USBSTS` clean,
  `usb`-daemon error lines, time to the first HID event. Revert if arm B is not at least equal.
- **Effort.** S-M. No gain expected; the point is removing an unexplained divergence from the reference driver.

### 8. PCIe: link bring-up parity with Linux

Correction first: the hypothesis list this review started from said Phoenix samples link-up once after 100 ms
(`phoenix-rtos-devices/pcie/server/pcie.c:451-457`). That is true of the **standalone `pcie` daemon, which is no
longer spawned**: `usb/xhci/Makefile` links `bcm2711-pcie.c` into the `usb` daemon instead ("the standalone `pcie`
daemon is no longer spawned in user.plo.yaml for Pi 4"). The **live** code, `usb/xhci/bcm2711-pcie.c:497-529`,
waits 100 ms after PERST# release **and then polls link-up up to 50 x 2 ms (100 ms more)**, as Linux does, and logs
"link did not come up" (`:525-528`). So that suspected cause of Phoenix's old intermittent link-down is **already
fixed in the live path**. What remains, each a small difference from `pcie-brcmstb.c`:

- **Dead duplicate.** `pcie.c`'s BCM2711 path still holds the one-shot sample and a 200 us PERST# hold
  (`pcie.c:418,452-459`). Delete or sync it so nobody tests the wrong file (Low, S).
- **No retry on link-down.** After a failed poll `linkUp` stays false, no VL805 notify or BAR0 happens
  (`bcm2711-pcie.c:717-719`) and xhci gives up after 6 x 100 ms (`xhci.c:989-1024`). Add one full re-run of
  PERST# assert/release before giving up (S).
- **UBUS BAR2 remap.** `bcm2711-pcie.c:624-637` (USB-FIX-15) sets `UBUS_BAR2_CONFIG_REMAP` access-enable on
  BCM2711, and its comment says Linux "always sets this". Linux writes it **only for BCM7712**
  (`pcie-brcmstb.c:1355-1365`: "Most STB chips: Do nothing"). Phoenix's own ledger says it "cannot be the
  differential cause" (`docs/done/2026-05-30-linux-reference-inbound-dma-finding.md:19`). Run the A/B of removing
  it, then correct the comment either way (S).
- **CRS software visibility.** Phoenix sets RTCTL.CRSSVE whenever a PCIe capability exists
  (`bcm2711-pcie.c:670-673`); Linux sets it only if RTCAP advertises CRS visibility
  (`pcie-brcmstb.c:1862-1866`, and enables it before enumeration so the PCI core polls for a 0x0001 vendor ID).
  Gate it on RTCAP bit 0 (S). Phoenix's own post-notify vendor-ID poll already tolerates CRS.
- **CLKREQ#, SSC, internal bus timeout.** Linux chooses a CLKREQ# mode after link-up (Raspberry Pi OS:
  "default", L1 substates), enables spread-spectrum clocking via the PHY MDIO registers, and in that mode
  extends the internal bus timeout to 4 s (`RGR1_SW_INIT_1` minus 8). Phoenix touches only SERDES_IDDQ in
  `HARD_DEBUG` (`bcm2711-pcie.c:447-448`) and has no SSC and no timeout change; the 2026-09-26 re-diagnosis
  records "We skip `brcm_config_clkreq` and the RBUS timeout extension"
  (`docs/inprogress/2026-09-26-p2-serror-rediagnosis.md:55`). Interaction with SError: a stuck PCIe read currently
  ends after ~10.8 s with an SError (handled); a 4 s timeout would shorten that. The second port made all three
  **switchable** (default: CLKREQ# bits cleared, no SSC, no CRS) so each can be measured; the result is still
  pending there.
- **Do.** Land each as an opt-in boot parameter, default unchanged, and run task 1's cold arms for the ones that
  differ from today. **Verify.** per item above plus enumeration 20/20 cold boots (Phoenix baseline: 11/11 cold,
  `docs/pi4-hardware-support-matrix.md:67`). **Effort.** M overall.

### 9. xHCI: TRB overfetch padding for the VL805

- **Learned.** The VL805 **prefetches TRBs past where it stops** and may use those stale copies later; Linux
  flags this as `XHCI_TRB_OVERFETCH` and pads every endpoint ring and every TD's TRBs with **four unused TRBs**.
  In the second port the board's USB hub stopped answering in about **1 boot in 10 (59 of 623)** a few seconds
  after the HID driver started, every control transfer timing out until the next boot; the controller's dequeue
  pointer had run past the stop marker over entries of the ring's previous lap. After clearing completed entries
  and padding: **0 in 40** cold boots.
- **Why for Phoenix.** Phoenix's rings toggle the cycle bit properly on wrap (`usb/xhci/xhci.c:3010,3061`), so
  stale entries are harmless for the *validity* test, which probably explains why Phoenix did not see the second
  port's failure. But Phoenix has **no overfetch padding**: rings are single 4 KiB pages (256 TRBs, aligned 0x40
  only, `xhci.c:188-189,2825`) and completed TRBs are not cleared (`xhci.c:3038-3043`); a prefetch past the last
  TRB of a segment reads whatever follows the page. Phoenix's own comparison doc records "Phoenix applies none"
  (`docs/done/2026-06-27-xhci-linux-comparison.md:259-277`). Phoenix had its own history of enumeration failures
  (`-110`, `-19`) fixed by software ordering, so a residual rate is plausible but unmeasured.
- **Do.** Allocate each ring with four spare zeroed TRBs after the Link TRB (and leave a mapped page after the
  ring), clear a TD's entries on completion.
- **Verify.** `test-cycle-bench.sh 40 xhci-pad` cold boots with the stage table counting `usb` enumeration, hub
  and HID events; compare with a 40-boot baseline (the rate being sought is ~1 in 10 or lower).
- **Effort.** S.

### 10. EL0 exception hardening: blocked-signal fault loop and sigreturn PSTATE

- **Learned.** (a) A thread that faults while the signal is **blocked or ignored** (as in any fault inside a
  signal handler, which runs with its own signal blocked) returns to the faulting instruction and faults again
  millions of times a second: one program's server spent minutes in such a loop. Fix as Linux's
  `force_sig_fault()`: unblock the signal and reset its action to default before sending. (b) `sigreturn`
  restored the whole saved SPSR from user memory, so a program could return to EL1 or with interrupts masked by
  editing its signal frame; fix: take only NZCV, BTYPE, SSBS, DIT, TCO from the frame.
- **Why for Phoenix.** (a) The loop exists: a fault whose SIGSEGV/SIGILL is ignored or blocked just retries
  (FIXME at `phoenix-rtos-kernel/vm/map.c:1211-1216` and `proc/process.c:339-343`; ignored signals are dropped at
  `proc/threads.c:2048`, blocked ones stay pending at `:2091`). A WebKit/GTK process with a SIGSEGV handler is the
  realistic victim (100 % CPU forever instead of a crash report). (b) The plain sigreturn takes the whole `psr`
  from the user stack (`hal/aarch64/cpu.c:167`); the only check is the mode bits via `hal_cpuSupervisorMode`
  (`syscalls.c:1510`) and DAIF is not masked, so user code can return to EL0 with IRQs masked and no longer be
  preempted on that CPU. The ucontext path is already safe (`cpu.c:195`).
- **Do.** Apply the Linux rule in the fault-signal path; mask the plain sigreturn psr like the ucontext path.
- **Verify.** Two tests in `phoenix-rtos-tests`: SIGSEGV ignored/blocked then a NULL store, expect the process to
  die within 1 s with a diagnostic; a handler that sets DAIF.I in its frame, expect other threads still
  scheduled on that CPU. Run on QEMU and Pi.
- **Effort.** S.

### 11. GIC IPI ordering and shared-priority race

- **Learned.** The second port's GICv2 code lacked ordering and was fixed to Linux's: a **write barrier before
  sending an SGI** (the message goes to normal memory, the SGI is a device store that is not ordered after it), a
  **read barrier after acknowledging** (the acknowledge is a device read that does not order later normal
  loads), SGI targets from each CPU's **interface bit read from the banked target register** instead of
  `1 << cpu`, and every interrupt at one priority set **once** so enabling an interrupt no longer
  read-modify-writes the word it shares with three others (one of which may be active). Barrier before WFI too.
- **Why for Phoenix.** `phoenix-rtos-kernel/hal/aarch64/generic/interrupts_gicv2.c:486-490` writes `GICD_SGIR`
  and then does `dsb`: the barrier is **after**, not before; no barrier after the `GICC_IAR` read (`:151`);
  targets are `1U << cpu` (`:499`); priority is set for every IRQ at init (`:394-396`) **and again** in each
  `hal_interruptsSetHandler` (`:308`) as a byte read-modify-write of a 32-bit word under a per-IRQ (not
  per-word) lock (`:256-263`), so two CPUs registering neighbouring IRQs can race. In practice the single user
  (`threads.c:241`) stores its flag just before the SGI and the 4 CPUs map to bits 0-3, so this is a latent, not
  an observed, defect.
- **Do.** `dsb ishst` (or `dmb`) before `GICD_SGIR`, `dmb` after IAR, read `ITARGETSR0` for the mask, write
  priorities once at init and byte-wise (not RMW) afterwards, `dsb` before `wfi` in `hal_cpuHalt`.
- **Verify.** 4-CPU `cpuburn` soak with IPI-heavy load (thread wake-ups), 0 stalls over 30 min; QEMU smoke.
- **Effort.** S (kernel, boot-risk).

### 12. Instruction-cache sync audit and exec cost

- **Learned.** The second port's `arch_cpu_sync_icache()` computed the line size as `4 << (CTR_EL0 << 15)`
  instead of `4 << (CTR_EL0 & 0xf)`: the I-cache was invalidated every 4 bytes, 16x per 64-byte line, for every
  page mapped executable. It was **45 % of the time of starting a program**; fixed, with CTR_EL0.IDC and DIC
  honoured: fork+exec 7.8 -> 4.7 ms, posix_spawn 7.4 -> 3.4 ms.
- **Why for Phoenix.** The Phoenix line size is correct (`phoenix-rtos-kernel/hal/aarch64/cache.c:17-22`). But the
  function (`cache.c:25-37`) is called **per page, on every demand-faulted text page** from
  `_pmap_cacheOpAfterChange` (`pmap.c:339-342`), costing ~64 `ic ivau` + 2 DSB + 1 ISB each with no range
  batching; and the reading found **no `dc cvau` before the `ic ivau`** and no IDC/DIC test, so correctness for
  code written through a cached mapping depends on other paths having cleaned the lines (A72 has IDC=0).
- **Do.** Audit that every path that writes code (loader, relocation, JIT in WebKit) cleans to PoU first; honour
  IDC/DIC; consider `ic ialluis` once for a large range. Then measure process start.
- **Verify.** A loop of 1000 `vfork+exec` of a tiny binary, microseconds each, before/after; WebKit JIT and python
  `ctypes` smoke tests.
- **Effort.** S.

### 13. libphoenix string routines: replace byte loops with Arm Optimized Routines

- **Learned.** Vendoring Arm Optimized Routines (MIT OR Apache-2.0 WITH LLVM-exception) for memcpy, memmove,
  memset, memchr, memrchr, memcmp, strchr, strchrnul, strrchr, strcmp, strncmp, strlen, strnlen, strcpy, stpcpy
  took (generic C before): memcpy 1 KiB 5280 -> 11223 MB/s, memcpy from source+1 731 -> 9036, memset 4 KiB
  5805 -> 11598, strlen 4256 -> 8091, memchr 2908 -> 6419; a drawing benchmark 162 -> 181 frames/s. Verified by
  3.8 million checks against reference versions (every length up to 300, every alignment up to 16, strings
  ending at an unmapped page). The kernel keeps generic C (no SIMD in the kernel).
- **Why for Phoenix.** `libphoenix/arch/aarch64/memcpy.S` (AOR) and `memset.S` already exist, but `strlen`,
  `strcmp`, `strchr`, `memchr`, `memcmp` and friends are plain byte-wise C in `libphoenix/string/string.c`
  (`arch.h:23-25` selects only the three). Kernel `hal_strlen/strcmp/strncmp/strcpy/memcmp` are byte loops too
  (`hal/aarch64/string.c:18-124`), which is fine for the kernel. Phoenix has a host harness
  (`tools/libstring-hosttest`, over-read focused) and a past strncmp over-read fix, so the safety net exists.
- **Do.** Vendor the remaining AOR routines under `libphoenix/arch/aarch64/`, extend the host harness with the
  length x alignment x page-edge matrix, add a small MB/s benchmark to `phoenix-rtos-tests`.
- **Verify.** Host harness 0 mismatches; Pi MB/s before/after; run the libc suite and a python/sqlite smoke.
- **Effort.** S-M.

### 14. Boot-time budget: EEPROM boot order, firmware delays, plo/kernel console cost

- **Learned.** On the shared bench ~58 s of every boot was the **EEPROM's network attempt before the SD card**
  (boot order tries network first; the HDMI frame at 30 s said "Trying boot mode NETWORK"); a Pi that boots its
  card directly skips it. After the bootloader started, the card boot to desktop fell from 21.3 s to 14.5 s by
  cutting per-command waits, a chunk cache in the loader and the autoboot countdown, and a further 1.0 ->
  0.4-0.6 s came from silencing **52 of the loader's 85 serial lines** (5.7 KB at 115200 baud, written
  synchronously).
- **Why for Phoenix.** Phoenix's only boot-phase breakdown is from June
  (`docs/done/2026-06-02-10-boot-consistency-study.md:82-92`: `plo_banner` -> `pcie_settled` 109.9 s, a constant
  165.9-177.5 s *before plo*; the pre-init PCIe register dump that cost 90-110 s has since been removed); no
  current table exists. The firmware prints "USB MSD stopped. Timeout: 25 seconds" (~23 s on netboot);
  `config.txt` has `boot_delay=0` and `uart_2ndstage=0` already (`config.txt:26,50-51`). Output costs ~87 us per
  character (`docs/knowledge/rpi4-os-development-guide.md:939-945`).
- **Do.** Measure power-on -> plo banner -> kernel -> `(psh)%` on SD and on netboot with timestamped capture,
  then check: EEPROM `BOOT_ORDER` (SD first vs network first), the 25 s USB MSD timeout (`BOOT_ORDER` without USB
  when unused), and plo's own prints (partition scan, memory map). Record the table in the weekly log.
- **Verify.** `capture-rpi4b-uart.sh --timestamp` on 5 boots per variant; median per phase.
- **Effort.** S (measure) plus small config changes.

### 15. Console output through an IRQ-drained ring (boot time)

- **Learned.** The kernel print routine wrote each line to the UART with interrupts disabled and waited for it (87 us per
  character). Putting output in a **16 KiB ring drained by the PL011 transmit interrupt**, with the debugger,
  panics and a full ring flushing directly (order kept), took boot volume -> desktop **5.1 -> 4.1 s**; an option
  keeps all output direct for hang hunts. Gotchas found on the way: an SPI routed to every CPU leaves the others
  the spurious ID 1023, which must not be reported (each report generated more output: a storm, boot 14-15.5 s);
  QEMU's PL011 keeps the RX interrupt raised until it is cleared in `UARTICR`.
- **Why for Phoenix.** The Phoenix console is synchronous (task 2 evidence) and boot logs are 500+ lines;
  Phoenix already handles spurious 1023 silently (`interrupts_gicv2.c:154-156`). Do task 2 first; this builds on it.
- **Do.** TX ring + interrupt, direct-output switch (the heartbeat/hang-hunt mode of task 2), flush on panic.
- **Verify.** Boot time A/B (3 interleaved boots each), identical log content, crash dumps still complete.
- **Effort.** M, kernel.

### 16. SD: log the DMA/PIO decision and bounce-buffer address; small-read latency

- **Learned.** The EMMC2's DMA window on this SoC revision is the **first GB**; a buffer above it needs a bounce,
  and whether a transfer's memory landed above 1 GB varied by boot, which made identical network sessions read
  the card at 15 MB/s vs 42-45 on SD boots. Raising the bounce size from 16 kB to 128 kB made unaligned uncached
  64 KiB reads 4.5 -> 2.1 ms and 256 KiB 16.5 -> 7.3 ms. One ADMA2 command for all segments (instead of one per
  vector) took raw reads 32.3 -> 42.6 MB/s. A read costs ~0.5 ms fixed + transfer (4 KiB 0.59 ms, 64 KiB 1.8,
  1 MiB 23 = 45 MB/s). Spinning up to 50 us for command-complete before sleeping helped the (WiFi) SDIO path
  ~14 -> ~25 Mbit/s up.
- **Why for Phoenix.** Phoenix already uses **ADMA2 in both directions, a 128 KiB bounce and DDR50** (raw read
  ~38 MB/s: `storage/bcm2711-emmc/sdcard.c:77,96,249,310-352`; `docs/PHOENIX-RTOS-RPI4-CHANGES.md:1197`). Open
  points: the bounce buffer must land below 1 GiB for DMA or the driver **silently falls back to PIO** (12 MB/s vs
  38): `useDma = (dmaBufferPhys < 0x40000000)` at `sdcard.c:270,290`, relying on the early allocation being low
  (`:264`), and the kernel has no placement control (`kms_bo.c:8`); the command-complete wait checks the status
  once then sleeps on a 1 ms `condWait` (`sdcard.c:385-403`), no short spin; every miss transfers a full 128 KiB
  line (`sdstorage_dev.c:52`, `SDCARD_MAX_TRANSFER`), so a 4 KiB random read costs a 128 KiB transfer.
- **Do.** (1) Log the buffer PA and `useDma` at every boot and make a PIO fallback a loud line plus a counter;
  (2) add a short (<= 50 us) completion spin before sleeping and measure; (3) measure 4 KiB random read latency
  and IOPS vs 128 KiB lines (consider smaller lines for metadata).
- **Verify.** 10 boots, count PIO fallbacks (expect 0), raw read MB/s, 4 KiB random-read ms.
- **Effort.** S.

### 17. Clock policy proof: sample ARM/core/V3D clocks across the 2-minute mark

- **Learned.** About **2 minutes after power-on the firmware drops the ARM from 1500 to 600 MHz and the core clock
  from 500 to 200 MHz for good** (59 C, not throttled) unless the OS asks for a rate (Linux's cpufreq does).
  Everything measured after 2 minutes ran at 40 % CPU clock: QuakeSpasm 1080p 19-27 -> 30-43 fps when fixed, WiFi
  upload 26.5 -> 37.4 Mbit/s; at 200 MHz core, HEVC lost a fifth of its rate. With turbo on, the firmware answers
  every ARM rate request with the maximum.
- **Why for Phoenix.** Phoenix pins `force_turbo=1` and `core_freq=500` in the generated `config.txt:45-46`, with a
  comment that it also holds the ARM at maximum, so the drop should not occur **(inferred; not measured)**: there
  is no ARM `SET_CLOCK_RATE` anywhere (only EMMC and rpivid), no clock logging over time, and `/dev/thermal`
  reports only temperature and throttle bits. Every Phoenix benchmark past 2 minutes depends on this being true;
  it also costs idle power and heat (force_turbo disables DVFS), relevant to task 1's heat log.
- **Do.** Add measured ARM/core/V3D clocks (`GET_CLOCK_MEASURED`) to the thermal device and sample every 5 s
  through a 5-minute idle and a 5-minute `cpuburn` boot.
- **Verify.** Clocks flat at 1500/500 across 0-300 s; if not, this becomes High and the benchmark baselines are suspect.
- **Effort.** S.

### 18. V3D server: cache maintenance outside the device lock

- **Learned.** Cache maintenance of imported frame memory (a walk of 8 MB at 1080p, 3-4 ms) ran **holding the
  device lock** that the job worker needs to finish one job and start the next, so the GPU idled twice per frame.
  Walking with a reference on the buffer instead, plus one sync per buffer (not per frame) and the present blit
  inside the frame's own job: glmark2 233 -> 304 windowed, 64 -> 81 at 1080p full screen.
- **Why for Phoenix.** The same shape exists: V3D cache and TLB maintenance, including the bounded L2T flush spin
  (up to 1M reads, `phoenix-rtos-devices/gpu/rpi4-v3d-async/v3da_hw.c:287`), runs with `srv.lock` held
  (`v3da_jobs.c:9,354-375,1173-1178`), so the event and dispatch threads serialise on it. **Not measured** as a stall.
- **Do.** Instrument lock hold time in the cache paths first; if > 0.5 ms per frame, hold a reference instead of
  the lock.
- **Verify.** GPU busy % and fps in the 1280x720 desktop-window QuakeSpasm (56 fps baseline) and the X GL window
  (60.00 fps vsync); 0 faults.
- **Effort.** S-M.

### 19. Physically contiguous allocation late in a session

- **Learned.** The display driver allocated a new **physically contiguous buffer below 1 GB** for every smaller
  video mode; after Firefox and other games a 2 MB run was no longer free (`B_NO_MEMORY`, 3 of 3 full smoke runs,
  never early in a session). Fix: allocate once at driver start and reuse.
- **Why for Phoenix.** Scan-out is already one pool reserved at server start (`rpi4-kms/kms_bo.c:7-9,59-99`), so
  the display case is covered. But `MAP_CONTIGUOUS` for GPU buffers and USB DMA is a **runtime buddy allocation**
  (`vm/object.c:1024-1035`, power-of-two rounding) with mobility grouping and reclaim as mitigation
  (`vm/page.c:28-49,223-240`), and `meminfo()` does not report the largest free block
  (`docs/misc/2026-10-09-c17-oom-options.md:39`). The only recorded failure (C17) was true OOM, not proven
  fragmentation.
- **Do.** Add "largest free block per order" to `meminfo`, then a stress test: browser + game cycles, then allocate
  2/4/16 MB contiguous every minute and log.
- **Verify.** 2-hour soak; report the smallest largest-free-block seen.
- **Effort.** S-M.

### 20. HDMI audio (MAI) driver

- **Learned.** HDMI audio works under the firmware-owned display with direct MAI programming: **legacy DMA
  channel 4, DREQ 10, into MAI_DATA at bus 0x7ef2001c**, IEC 60958 subframes built by the driver (the MAI wants
  subframes, not raw PCM), N/CTS computed from the **measured** pixel clock, an Audio InfoFrame in a free packet
  slot. Facts that settled the open points: the HSM clock (firmware clock 13) reports a **set rate of 0 but
  measures 150 MHz**; the firmware uses packet slots 0, 2 and 4 (slot 4 holds its own SPD InfoFrame), so the Audio
  InfoFrame goes in **slot 1**; the firmware does not program audio itself; 48 kHz stereo streamed at exactly
  48000.000 Hz and was recorded back through the capture card (1 kHz tone at the level played, no glitch over
  20 s, 0 underruns). Limits: the device appears only if a monitor taking audio is connected at boot; no
  hot-plug or mode-change following; HDMI0 only.
- **Why for Phoenix.** No HDMI audio exists: `docs/USER-GUIDE.md:689` says "HDMI audio is not supported", and the
  support-matrix row titled "PWM / I2S / HDMI" only has PWM. Phoenix drives the same firmware planes, and the PWM
  driver already uses legacy DMA (`audio/rpi4-audio`), so the DMA model transfers. The video demos and games are
  silent on a monitor's speakers; F4 (no audible sign-off for PWM) is separate.
- **Do.** Implement `rpi4-hdmi-audio` on the same DMA ring scheme, 48 kHz stereo first.
- **Verify.** Record the HDMI output (capture card) while playing a 1 kHz tone: frequency, level, 0 underruns over
  60 s; ffplay with sound; the SDL audio backend selects it when present.
- **Effort.** L.

### 21. Random numbers: fast DRBG over the hardware RNG; kernel seed for TD-28

- **Learned.** Every 32 bytes read reseeded a pool that yielded 257 times under a lock: 16 bytes took 145 us,
  `/dev/urandom` gave 2.8 MB/s, and every `getentropy()`, TLS handshake and program start paid it. A ChaCha20
  keystream keyed from the pool with fast key erasure (stirred every 1 MiB, every 60 s, after a write, and once 64
  bytes of hardware entropy arrived) gives 2.3 us per read, 149 MB/s.
- **Why for Phoenix.** Phoenix's kernel exposes no random bytes (no `getrandom` syscall, no AT_RANDOM); the path is
  userland only: `rpi4-hwrng` -> `/dev/hwrng` -> posixsrv `/dev/urandom` -> libphoenix `getrandom/getentropy`
  (`project_pi4_hwrng_driver` note). The hwrng FIFO wait is a bounded busy-poll without yield
  (`misc/rpi4-hwrng/rpi4-hwrng.c:49,96-99`) at ~65 kB/s, and **TD-28** is open: the stack-protector guard is seeded
  from a time/counter/pid mix because urandom is unusable before `main`
  (`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md:513-525`).
- **Do.** (1) posixsrv: ChaCha20 DRBG seeded and periodically reseeded from `/dev/hwrng`, so urandom reads never
  hit the RNG FIFO; (2) a kernel-supplied random word at process creation (or a syscall) to close TD-28.
- **Verify.** `dd if=/dev/urandom bs=1M count=16` MB/s before/after; TLS handshake time (curl HTTPS); a program
  prints its stack-guard value across 20 boots (distinct).
- **Effort.** S-M.

### 22. Saved clock across reboots (no RTC)

- **Learned.** A launch job restores the clock forward to the last saved time (modification time of a file), saves
  it every 5 minutes and at shutdown, never moves it backward; without it every boot started in 1970 until NTP,
  files got 1970 dates and HTTPS reported certificates "not yet valid".
- **Why for Phoenix.** No persisted clock exists; `ntpclient -w 90` runs once, fire-and-forget, from psh
  (`psh/pshapp/pshapp.c:1629-1672`), and an earlier clock step before the shell was reverted because it caused
  no-prompt boots (0/134 -> 5/88, `user.plo.yaml:205-211`). Without a network, timestamps and TLS stay wrong
  (`ntpclient.c:419-422`).
- **Do.** A small post-boot job (not on the psh critical path) doing the same forward-only restore.
- **Verify.** Reboot without network: date within 5 min of the shutdown; `ls -l` of new files; HTTPS check.
- **Effort.** S.

### 23. USB mass storage: transfers above 64 KiB

- **Learned.** A USB 3 stick: raw reads 62 -> 69.5 MB/s (128 kB) and 78 -> 87.5 MB/s (1 MB) with 256 DMA segments
  per transfer; 97 MB/s at 1 MiB per command after fixing TDs to cover only the current fragment.
- **Why for Phoenix.** `UMASS_MAX_IO` is 64 KiB (`phoenix-rtos-devices/usb/umass.c:79`) and measured raw read is
  59.1 MB/s, write 18.2 (`docs/pi4-hardware-support-matrix.md:69`). Phoenix builds one TD of up to 34 chained TRBs
  (`xhci.c:215,2913-3043`), so larger commands are plausible.
- **Do.** Raise to 128 KiB, then 256 KiB / 1 MiB while watching for babble/transaction errors.
- **Verify.** Raw `dd` read/write MB/s per size, `e2fsck` clean, 0 xHCI errors over 1 GB.
- **Effort.** S.

### 24. Audit asserts with side effects and the shipped debug level

- **Learned.** `ASSERT()` calls with side effects (fetching a signal handler's address; fetching a device node and
  its driver, 9 places) would have used garbage at a lower debug level; found only by building at level 1.
  Separately, the paranoid level cost ~30 % of a TCP sender's kernel profile in the allocator and ~15 % in mutexes.
- **Why for Phoenix.** `DEBUG=1` drops `-DNDEBUG` and enables `LIB_ASSERT` plus the kstack/ustack canary check
  (`phoenix-rtos-build/Makefile.common:37-42`, `proc/threads.c:912`); **which value the shipped image uses was not
  found**. A release build compiles asserts out, so any assert argument with a side effect changes behaviour.
- **Do.** `rg -n 'LIB_ASSERT\(|assert\('` over kernel, devices and servers and review each call's argument for side
  effects (calls, `++`, assignments); document the shipped level and its cost in the weekly log; add a build at the
  opposite level to the checks.
- **Verify.** Both builds boot to psh and pass the showcase gate; note the throughput delta (feeds task 5).
- **Effort.** S.

### 25. Hardware performance counters (PMU) driver and `pmustat`-style tool

- **Learned.** A system-wide PMUv3 driver programmed the cycle counter and the Cortex-A72's 6 event counters and
  sampled them every 200 ms into 64-bit totals (32-bit counters wrap in under a second); a `perf stat -a`-like
  command gave cycles, instructions, L1D/L2 refills, branch misses, IPC and rates, per CPU and per mode. Needs
  PMCCFILTR cleared and `MDCR_EL2` programmed at the EL2 drop (its reset value is partly unknown).
- **Why for Phoenix.** Phoenix has a sampling profiler (`perf/trace.c`, `prof record/report`) but no hardware
  counters; PMCR is enabled and the cycle counter read only in `test/vm.c` (`hal/aarch64/cpu.c:388-390`,
  `_init.S:365-371`); plo zeroes `MDCR_EL2` (`plo/hal/aarch64/generic/_init.S:208`); PMUSERENR is unset. The
  owner's profiling goal (W42 item 6c) would gain miss rates and IPC.
- **Do.** A `/dev/pmu`-style driver plus a psh command; program MDCR_EL2 (HPMN = PMCR_EL0.N, no traps).
- **Verify.** IPC of a memcpy loop vs a pointer-chase; cycles consistent with 1500 MHz.
- **Effort.** M.

### 26. Adaptive read-ahead for mapped files; cap any prefetch

- **Learned.** After two in-order package-heap reads, one page-aligned 256 KiB request brought the next chunks
  (scattered reads unchanged): all of a 100 MB package from the SD card 9.0 -> 5.35 s. Opposite lesson: mapping a
  file started an asynchronous read of up to 10 MB; the first program to map a 30 MB data library had its page
  faults delayed behind it (1.2 s to `main`, 0.6 s after capping at 1 MB).
- **Why for Phoenix.** The kernel's file-fault read-ahead is a fixed 16 pages (`vm/object.c:716-760,960-982`),
  libcache lines are 128 KiB, NFS read max is 1 MiB; there is no sequential detection and no prefetch on map.
- **Do.** Detect in-order faults per object and grow the cluster to 256 KiB (aligned); never prefetch at map time
  beyond 1 MB.
- **Verify.** Cold start time of a large binary (WebKit, Quake) from SD and from NFS, `dd` of a mapped file; no
  regression on scattered-access workloads.
- **Effort.** M.

---

## Low

### 27. GNU hash, `-Bsymbolic-functions` and packed relocations in the dynamic loader
- **Learned.** DT_GNU_HASH with a Bloom filter turned most "symbol not in this image" lookups into one test: GUI
  app start 95 -> 80 ms and 120 -> 81 ms for two apps. `-Bsymbolic-functions` made calls to a library's own
  exports relative relocations (7372 of 8953 in the largest library), 12.7k lookups instead of 23k per terminal
  start, 10-20 % sooner. DT_RELR cut one library's relocation table 12 MB -> 0.25 MB (dlopen warm 29.5 -> 16 ms;
  start of a big app only -1.5 %).
- **Why / state.** Phoenix's loader reads **DT_HASH only** and errors without it
  (`libphoenix/dl/dl.c:116,213,574,589-590`); no GNU hash, Bloom, RELR; TLS in shared objects is unsupported
  (`dl.c:38`), so the second port's TLS fast path (35 -> 17.4 ns per access) has no Phoenix equivalent. Impact is
  limited to `dlopen` users (python extensions); Mesa and the apps are static.
- **Do / verify / effort.** Add GNU hash lookup and the link flags if dlopen start time matters; measure
  `python -c "import ssl, sqlite3"`. M.

### 28. Thread-stack reuse (measure first)
- **Learned.** A team keeping up to 4 exited threads' stacks for new threads of the same size: create+join of an
  empty thread 75 -> 49.5 us (CPU 67 -> 41), with 64 KiB of stack used 193 -> 49 us.
- **Why / state.** Phoenix `mmap`s and `munmap`s the stack on every `pthread_create`
  (`libphoenix/pthread/pthread.c:379,513,567`); kernel stacks are `vm_kmalloc` per thread (`threads.c:431-435`);
  no create+join microsecond figure is recorded.
- **Do / verify / effort.** Measure create+join over 100k threads; if > 100 us, add a small per-process cache. S-M.

### 29. EL0 `mrs` of ID registers and MDCR_EL2
- **Learned.** Emulators and runtimes read CPU features with `MRS`; on an ARMv8.0 core the read is an undefined
  instruction. Linux's documented behaviour (`cpu-feature-registers.rst`): pass MIDR_EL1 and the feature
  registers, MPIDR as a uniprocessor, others zero.
- **Why / state.** Phoenix sends EC 0x18 (trapped MSR/MRS) to `process_exception`, which dumps and posts
  **SIGKILL** (`proc/process.c:309-324`), not SIGILL (EC 0x00 gives SIGILL, `:327`); a library probing with `mrs`
  is killed instead of falling back. MDCR_EL2 is untouched by the kernel and zeroed by plo (see task 25).
- **Do / verify / effort.** Deliver SIGILL for EC 0x18 at minimum, emulate MIDR_EL1 and the ID registers as Linux
  does; test with a program that reads `ID_AA64ISAR0_EL1` under a SIGILL handler. S.

### 30. Wake IPI for idle CPUs and WFE event stream
- **Learned.** The generic timer's event stream (every ~152 us) bounds every WFE; with it plus test-before-exchange
  and WFE spin-waits, a Cortex-A72 LL/SC livelock (polling a line another CPU updates atomically stalled both CPUs
  for 10 ms to over a minute, ~400 stalls in a 10-minute soak) disappeared.
- **Why / state.** Phoenix already follows the pattern (LL/SC lock with test-before-exchange and `wfe`,
  `hal/aarch64/spinlock.c:58-72`, `hal.c:65-77`; no polling of exclusively-updated lines found), but
  CNTKCTL_EL1.EVNTEN is not set (`_init.S:334` writes 3), so a WFE with no release event never times out. Also an
  observation (not from the other port): there is **no wake IPI**, so a thread made runnable for an idle CPU waits
  for that CPU's next 1 ms tick (`proc/threads.c:241,311-323`), which matters for an IPC-heavy microkernel.
- **Do / verify / effort.** Set EVNTEN with a divider for ~150 us; consider an SGI on enqueue to an idle CPU;
  measure cross-CPU message ping-pong latency before/after (`test-msg-*`). S.

### 31. Bluetooth: mailbox hygiene; acceptance tests for a future host stack
- **Learned.** The second port serialises every firmware mailbox call under one mutex; Phoenix itself lost a
  `SET_CLOCK_STATE` answer to a concurrent direct-FIFO client (about 9 % of boots, 0 of 85 after moving to the
  mailbox server). On the other side, a remote device's L2CAP response to an unknown request panicked a kernel
  Bluetooth stack (2 of 2 when a laptop unpaired); PIN pairing from BlueZ and a discovery tool are the acceptance tests.
- **Why / state.** All Phoenix users go through `rpi4-vcmbox` **except** `bt/rpi4-hci/rpi4-hci.c:99-150`, which
  drives the FIFO directly with unbounded spins. Bluetooth userspace is not in the image (F2), so it is dormant.
- **Do / verify / effort.** Move `rpi4-hci` onto `libvcmbox`; if a host stack is ever added, test
  unknown-identifier responses and BlueZ PIN pairing. S.

### 32. Methodology checklist for the bench (documentation)
See the next section; turn it into a short `docs/knowledge/` note after task 1 proves it. S.

---

## Methodology: what made the second port's investigation work

For `docs/knowledge/` after task 1; each line was learned the hard way (rates quoted wrongly, hours lost):

1. **Observation window first.** Fix the window before quoting a rate; it must cover the whole interval in which
   the event can land (here 2-133 s into the kernel). "SD 1 of 5" watched 15 s was wrong; "alive at 240 s" is a result.
2. **Liveness probe, not "reached prompt".** A freeze after the prompt scores as a pass. Probe at the end.
3. **Per-CPU heartbeat from IRQ context plus unbuffered serial** (task 2) to tell "SoC stopped" from "a thread
   hung"; the CPUs were all idle in every captured freeze.
4. **Controlled off-windows** (30 min), interleaved arms, a back-to-back control after each cold trial, and an
   independent reference (Linux on the same board and path under the same load).
5. **Identical boot path for A/B** (network vs SD differ); change one thing; rebuild stale cores (`--scope core`);
   label hygiene (`check-label-free.sh`).
6. **Block, do not guess.** Boot-time block lists of drivers (one at a time, then in groups) ruled out ten
   candidate drivers in a day; keep each variant's block list in the log.
7. **A scripted run with a missing argument must not start a cycle** (one did, and waited for the bench lock);
   waiter loops must not `pgrep -f` their own command line (13 stale waiters piled up); keep one shared bench lock
   for all projects on the board.
8. **Measure with two tools** and ask an object its name before counting objects (profiler + PMU + timestamps).
9. **Record the temperature** before and after every trial; the hot-then-briefly-off condition mattered.

## Already done in Phoenix (no task)

| Learning | Phoenix evidence |
|---|---|
| GENET cacheable RX with per-frame invalidate, 256 unique buffers, no status blocks, EEE/APD off | `phoenix-rtos-lwip/drivers/bcm-genet.c:102,810-860,932,943,953-956,1127` |
| ADMA2 for SD both directions; 128 KiB bounce; DDR50 1.8 V; plain W1C interrupt acknowledge; 32-bit register access; CRC/ADMA errors end the transfer | `storage/bcm2711-emmc/sdcard.c:77,96,249,310-352,439,444,982-1005`; `docs/pi4-hardware-support-matrix.md:71` |
| WiFi SDIO card interrupt with poll fallback, BLKSIZE+BLKCNT in one 32-bit write, 41.67 MHz bus, PIO lost-edge fix with WL_ON retry; throughput already above the second port's (5 GHz: TX 6.47 / RX 7.10 MB/s vs its 37/46 Mbit/s on 2.4 GHz) | `wifi/rpi4-wifi/rpi4-wifi.c:864,5066-5098,5570-5617`; `docs/KNOWN-ISSUES.md` F1. A short pre-sleep spin was tried and reverted for no benefit (`rpi4-wifi.c:4031`). |
| xHCI CRCR stale after HCRST (one daemon owns bridge + controller) | `usb/xhci/xhci.c:1934-1989`; `docs/done/usb-fix-ledger.md:72` |
| PERST# held >= 100 ms; live link-up poll; ASPM off; non-snoop fixes; bus 0 restricted to dev 0 / fn 0; link-up gate before bus-1 config | `usb/xhci/bcm2711-pcie.c:438-444,497-529,689-695,709-719` |
| Scan-out pool reserved at server start below 1 GiB; firmware planes with `SET_PLANE`; vblank from the SMI interrupt (60.00 Hz, 0 missed flips); hardware cursor | `rpi4-kms/kms_bo.c:7-9,59-99`, `kms_backend.c:15-23`, `kms_vblank.c:6-17` |
| Mailbox calls serialised through one server; V3D clock race fixed (0 of 85) | `misc/rpi4-vcmbox/rpi4-vcmbox.c`; `project_v3d_clock_race_mmio_hang` note |
| ARM held at maximum by `force_turbo=1` / `core_freq=500` (verify: task 17) | `_projects/aarch64a72-generic-rpi4b/config.txt:45-46` |
| Mesa on-disk shader cache (27x on second start); NPOT mipmap fallback; HEVC on rpivid (bit-exact) | `docs/gpu-new-lane/G4-shader-cache.md`; mesa patches 0004, 0018; `M10-hevc-hwaccel.md` |
| `poll()` leaves the caller's `events` alone | kernel `posix/posix.c:3326-3387` |
| User write-only mapping is user-readable (AArch64 AP bits cannot express write-only) | `vm/map.c:600-607`, `pmap.c:534-540` |
| CNTKCTL_EL1 so EL0 can read counters | `hal/aarch64/_init.S:334`; `project_pi4_el0_cntvct_cntkctl` note |
| memcpy/memmove/memset from Arm Optimized Routines (userland); I-cache line size computed correctly | `libphoenix/arch/aarch64/*.S`; `hal/aarch64/cache.c:17-22` |
| Spurious interrupt ID 1023 handled silently; spinlock and scheduler lock use test-before-exchange + WFE | `interrupts_gicv2.c:154-156`; `hal/aarch64/spinlock.c:58-72` |
| Sampling profiler has no per-thread armed timer to leak (samples from each CPU's own tick) | `perf/trace.c:367-460` |
| SMP placement: one global ready queue, so two busy threads cannot sit on one CPU while others idle | `proc/threads.c:62,252-262,767-933` |
| PCIe SError no longer fed by pre-init register dumps; unmasked as upstream | `docs/inprogress/2026-09-26-p2-serror-rediagnosis.md` |
| IPv6 off, so ICMPv6 error and DNS-wait problems cannot occur; IPv4 ICMP unreachable is sent | `rpi4b/lwip/lwipopts.h:32`, `lwip/src/core/udp.c:419` |
| Shared bench lock and heavy-build memory guard | `scripts/pi-bench-lock.sh`, `scripts/heavy-build.sh` |

Not applicable to Phoenix by design: the second port's loader/FDT `ranges` translation fixes, remote-disk network
boot, its package file system, window-server copies, x86-emulation memory-ordering and MAP_SHARED-after-mprotect
fixes (Phoenix's P21 `MAP_SHARED` is deferred by the owner), debugger back end, Bluetooth stack fixes, and its
packed-relocation work for a browser. Phoenix's 2/8 GB board mapping gap (TD-06, TD-15, `docs/KNOWN-ISSUES.md` P1)
is untouched by anything above.

**Stale Phoenix notes found while checking** (fix when touched, no task needed): memory note
`project_pi4_genet_rx_perf` still calls cacheable RX "UNVIABLE" (the August round and the current default
`GENET_RX_CACHEABLE=1` supersede it); `project_ffmpeg_hw_decode_scope` and `project_v3d_shader_disk_cache`
describe an August scope / a stub, not the current decoder and cache; the USB-FIX-15 and
`rpi_firmware_init_vl805` comments discussed in tasks 7-8 attribute behaviour to Linux that the checked Linux tree
does not have.
