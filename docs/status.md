# Phoenix-RTOS Raspberry Pi 4 Port Status

## Current Status: 2026-05-28 — USB PoC architecture proven mechanically; new finding: rig is flaky

Headline updates since 2026-05-27:

- **USB PoC integration: mechanically complete and committed.** The Phoenix
  USB host stack now builds into the `lwip-port` process via 6 thin shim
  files under `port/usb-embed/`. Boot-time `usb;--bridge-only` daemon does
  ONLY the BCM2711 PCIe bridge bring-up and parks (commits `28bc814`,
  `29f8cea`, `cfe8aec`, `600081d`); lwip-port-embedded then drives the
  controller in DRIVE_ONLY mode (commit `77f31bb`).
- **CRITICAL correction (2026-05-28):** the "X diag rig works ⇒ rig is a
  reliable baseline" narrative was statistically invalid. Multi-trial
  benches this session show:
    - Rig (`diag_format_xhci_bringup`): **2/4 trials succeed** (~50%)
    - PoC full bring-up: 0/8 trials
    - PoC DRIVE_ONLY: 0/4 trials
    - PoC MaxSlotsEn=1: 0/2 trials
    - PoC contig scratchpad: 0/4 trials
    - PoC no-bridge-mmap: 0/3 trials
  The rig has its own ~50% bridge-flakiness signature (`pre USBSTS=0x00`
  on failure indicates bridge MMIO reads return 0). The PoC's 0% is real
  divergence beyond the same flakiness mechanism.
- **One root-cause class FIXED, one new one OPENED:**
    - FIXED: VL805 retains internal CRCR latch across HCRST. Boot-time
      daemon programming CRCR + exit caused the next process's HCRST +
      CRCR write to be silently ignored, producing CCE events whose
      `parameter` field referenced the FIRST process's cmd-ring PA.
      Splitting bridge bring-up from controller drive (BRIDGE_ONLY +
      DRIVE_ONLY) eliminated this class of failure. ERDP readback at
      cmdExec timeout now matches `eventRingPhys` exactly.
    - OPEN: the residual 0% PoC pass rate (vs rig's 50%) — single-variable
      experiments matching the rig (MaxSlotsEn, scratchpad layout, no
      bridge re-init, no leaked bridge mmap) all gave 0%. Remaining
      hypothesis: cumulative side effect of `usb_init`'s pre-xhci_init
      allocations (mutex pool, driver registration, hub_init) vs the
      rig's clean-slate mmap sequence.
- Memories updated: `pi4-xhci-crcr-stale-after-hcrst` (new),
  `usb-dma-write-loss` (rig-is-flaky correction).

USB parked at this point pending a fresh angle (kernel-side
instrumentation, or literally rewriting `xhci_init`'s allocator path to
mirror the rig's mmap sequence). SMP + GENET still fully working as
before; WiFi unchanged at firmware-execution gate.

**External research consulted 2026-05-28** validated that BOTH the USB
and WiFi walls have known hardware-level corollaries in mainline Linux:

- **USB VL805**: Linux issue #5060 + RPi forums document per-board
  silicon variability on Pi 4; some boards need a hardware RC delay
  between 3.3V and nPONRST. Mainline Linux also hits VL805 reliability
  issues. Our ~50% rig flakiness is at least partly silicon. See memory
  [[vl805-known-silicon-flakiness]].
- **WiFi BCM43455 firmware execution**: OpenWrt #23069 + multiple
  forum reports — "HT Avail timeout" before firmware download, software
  recovery (module reload, unbind/bind) FAILS, only PSU power-cycle
  restores operation. Matches our exact symptom (HT_AVAIL never set
  after CR4 release). Hardware-level reset limitation, not Phoenix bug.

Two software improvements this iteration after the research:
- `bcm2711_pcie_initVL805` DRIVE_ONLY path now returns `-ENODEV` if
  the PCIe link doesn't come up in 10 s (was silently returning EOK).
- `xhci_programCommandSpace` register-write order now matches the
  proven-working 'X' diag rig exactly (CONFIG first, LO-then-HI for
  64-bit pointers).

Neither fixes the residual 0% PoC pass rate but both narrow the
diagnostic surface for future investigations.

---

## Previous Status: 2026-05-27 — SMP fully working; USB + WiFi at deep, well-characterized walls

Subsystem reality (verified this session; see manifest
`2026-05-27-smp-4core-verified-usb-ngnrne-wifi-pmufix.md`):

- **SMP: FULL 4-core scheduling WORKS.** D-8 (deferred CNTV) + D-9
  (secondary CNTV re-arm) are live (kernel `af171987`); all 4 A72s arm
  CNTV and take timer ticks (`smp: tick+15s cpu1=7…` rising), boot reaches
  `(psh)%`. The old "cpu0-only / CNTV-breaks-primary" notes were a
  test-timing artifact and are OBSOLETE. Remaining SMP work is
  hardening/soak, not a fix.
- **Boot / GENET networking / HDMI fbcon: working.** Pings 5/5 ~0.9 ms RTT;
  psh interactive.
- **USB (VL805 xHCI): WALLED — in-place fixes EXHAUSTED.** usb-hcd reaches
  RUNNING/CRR=1 but the controller posts ZERO events (inbound DMA writes
  never land); the lwip-port process does an identical bring-up and DOES
  get events. After exhaustive elimination (scratchpad, ERSTBA order,
  IMAN/IMOD, USBCMD INTE|HSEE `65a17ec`, MMIO nGnRnE `05ba58b`, buffer
  flags, cache, timing, full HCRST re-init, bridge re-settle, and a
  decisive PROOF that the failing+working DMA buffers are interleaved in
  the SAME physical region) the difference is purely the *process context*
  of the bring-up — with NO SMMU to explain it. Next: a cross-process
  architecture (drive the controller from a process distinct from the
  bridge bring-up, mirroring the working lwip path) or maintainer input.
  See memory `usb-dma-write-loss`.
- **WiFi (BCM43455c0 SDIO): MAJOR progress, now at the firmware-execution
  gate.** Research overturned the earlier "host must force HT" dead-end:
  brcmfmac releases the CR4 on ALP only and the *firmware* brings up HT
  itself. Rewrote the bring-up accordingly (`fd4551f`) → CR4 release is now
  textbook-correct (IoCtrl pre=0x21/post=0x01). Verified correct: firmware
  load (64/64), rstvec @ backplane 0 (read-back MATCH, `a87b891`), NVRAM
  trailer (`0xfe5001af`, matches WHD), cold reset, F2 handshake (`cdd9b23`).
  Yet firmware still does not execute (no HT, no F2-ready, CARD_INTR=0) —
  a deep gate that mirrors known Linux/OpenWrt 43455-on-Pi4 HT-timeout
  issues. New reusable tool: `scripts/diag-udp-probe.sh` (`ffb3f08`).
  See memory `bcm43455-chip-id`.

Active branches: kernel `agent/rpi4-program-reloc`; lwip
`agent/rpi4-genet`; devices `codex/upstream-sync-20260516`.

---

## Previous Status: 2026-05-25 — SMP Phase E HARDENED (idle + saturation endpoints both pass)

### Headline (2026-05-25)

- **SMP Phase E saturation harden.** Added a `b` (burn) sub-command
  to the diag-udp responder that spawns 4 busy-loop threads in the
  lwip-port for 10 s. Mid-burn probe: each burner accumulates ~4.9 s
  of cpuTime in 5.2 s wall-clock (94% of a CPU each); sum = 3.77×
  wall-clock — definitive saturation-side proof that all 4 cores
  pick up a CPU-bound thread. Combined with the earlier idle
  measurement (sum ≈ 4× wall-clock at idle), Phase E now passes at
  both endpoints. Manifest `2026-05-25-smp-phase-e-saturation.md`,
  lwip head `b750d7e`.
- **SMP Phase E validated.** Per-CPU idle thread cpuTime measured via
  the new `t` diag query: 4 `[idle]` threads each accumulate cpuTime
  at ~99.9% of wall-clock; sum ≈ 4× wall-clock proves 4 independent
  schedulers run on 4 cores. Closes task #29 with a definitive
  measurement-based pass. Manifest
  `2026-05-25-smp-phase-e-validated.md`, lwip head `f5687ad`.

- **Tier 5c: net-routed observability.** A new lwip-port UDP diag
  responder on port 9999 surfaces per-netif counters on demand,
  bypassing the post-fbcon UART silence. The bcm-genet driver hooks
  it via a new optional `netif_driver_t.stats` callback; the
  loopback netif (no `netif_alloc` wrapper) is skipped via
  `NETIF_FLAG_ETHARP`. Validated: counters increment by +101 after
  100 pings + 1 cold probe. `echo q | nc -u -w 1 10.42.0.99 9999`
  is now the project's go-to runtime-state probe. Resolves
  `TD-Eth-Stats`. Manifest `2026-05-25-eth-tier5c-diag-udp.md`,
  lwip `b261265`.

- **Tier 5b: real MAC + PROMISC off.** `bcm-genet.c` now
  calls the BCM2835 mailbox property channel with `GET_BOARD_MAC`
  (tag `0x10003`) at init. Validated: host ARP shows
  `dc:a6:32:3c:dd:f1` (real Raspberry Pi OUI `dc:a6:32`) for
  `10.42.0.99`. PROMISC is now off (the LAA fallback remains for
  diagnostic mode if the mailbox fails). Still 5/5 pings, RTT
  0.66–1.42 ms. Resolves `TD-Eth-MAC` and `TD-Eth-Promisc`.
  Manifest `2026-05-25-eth-tier5b-mailbox-mac.md`, lwip `79bd607`.
- **Ethernet Tier 5 productionization done.** GENET v5 driver now runs
  RX off `INTRL2_0_RX_DMA_DONE` (GIC SPI 157 = abs IRQ 189) via a
  Phoenix-pattern `interrupt()` + cond-driven service thread, replacing
  the 10 ms polling loop. Per-RX diagnostic printf removed. Host pings:
  `5/5, RTT 0.612–1.173 ms (avg 0.916 ms)` — ~8× faster than Tier 4's
  3.7–16.8 ms avg 7.4 ms. Manifest `2026-05-25-eth-tier5-irq-rx.md`,
  lwip `789be33` on `agent/rpi4-genet`.
- TX completion stays polled (single-slot synchronous, ~12 µs at 1 Gbps
  — IRQ overhead would dominate). Link state stays on the 1 Hz MDIO
  poll thread — the BCM54213PE PHY's INT_B pin isn't routed to a usable
  GIC SPI on the Pi 4 board. Both are tracked in
  `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`.

### Earlier headline (2026-05-24)

- **All 4 Cortex-A72 cpus take timer ticks at the SYSTICK_INTERVAL
  cadence.** Heartbeat dump from `main_initthr` 15 s after spawn
  loop:
  `smp: tick+15s cpu0=15242 cpu1=5038 cpu2=5038 cpu3=5038`.
  Kernel `af171987`. Manifest `2026-05-23-d9-validated-4cpu-ticks.md`.
- **Boot reaches `(psh)%` prompt.** Primary completes hal_init,
  vm/proc/syscalls init, syspage spawn (dummyfs, devfs, pl011-tty,
  usb, psh), fbcon brings up HDMI console.
- **USB cap-probe success path**: progresses past xhci_capProbe
  (HCIVERSION=0x0100) into xhci runtime layer. usb-hcd reports
  rc=-110 (ETIMEDOUT) on the path where cap-probe succeeds — a
  layer beyond the prior rc=-19 (-ENODEV from poison reads). xhci
  event-ring-before-R/S ordering fix shipped (devices `3696617`).
- **USB hardening commits**: HCRST 250 ms (devices `002324e`),
  cap-probe per-attempt logging (`5598799`), stderr unbuffered
  (usb `f7dea42`), runStateSelftest removed (`229ee55`), BAR1
  inbound disabled (`72f30ad`), MISC_CTRL bits matched to Linux
  (`9bf0ede`), post-mailbox + post-HCRST + pre-R/S bridge
  resettle (`fccc7ed` + `8d11e99` + `541f48e`), DSB SY around
  R/S=1 and doorbell (`70e8dc5` + `9ad2eab`), VSR1 LE endian
  for inbound BAR2 (`3470c54`), HSE/timeout reg dumps (`68260a8`
  + `763f1cc` + `ceb71d8`).
- **USB status: statistical**. Boot mode A (~1/3): cap-probe
  poisoned (rc=-19, no xhci output). Mode B (~1/3): cap-probe
  OK, R/S=1 sets USBSTS.HSE (rc=-19, "controller error state
  after run", USBSTS=0x15). Mode C (~1/3): R/S=1 clean,
  USBSTS=0x10, but NOOP cmd timeout (CRCR_LO=0, event[0]=0,
  rc=-110). Bridge inbound DMA state appears to be the root
  variability — needs deeper investigation (post-PERST timing,
  full SCB1/SCB2 setup, MSI registers).
- **Plo cold-boot diagnostics**: Cd/Ci/Id/Ii/Md markers between
  release-2 and kernel _start (plo `b31c50d`); kernel `_start`
  emits 'K' as first instruction (kernel `92331abe`). Plo
  switched to set/way D-cache invalidation (plo `54bf7c3`).
- **Test infra**: ffmpeg HDMI grab wrapped in 5 s timeout
  (`070c63b`) so v4l2 device contention doesn't hang the cycle.

### Pre-2026-05-24 (kept for diff value)

- **SMP Phase D investigation paused**. UART markers proved
  Pi 4 firmware does NOT reliably deliver secondaries into
  the armstub on cold boot (cpu0 never reaches in_el2,
  cpu1/2/3 reach it 0-or-1 times per boot, non-deterministic).
  The standard armstub spin-table protocol can't be the
  primary SMP wake mechanism on Pi 4. Resolving needs PSCI
  CPU_ON (EL3 secure monitor in the armstub) or a
  BCM2711 ARM_LOCAL_MAILBOX poke.

  **2026-05-24 update:** the "secondaries don't enter armstub"
  signal was diagnosed wrong — Pi 4 firmware DOES release all
  4 cores into the armstub spin-table reliably. The earlier
  data was a UART-routing artifact (cpu1/2/3 emit at the same
  PL011 device and were getting interleaved/lost during the
  cold-boot uart_reinit_115200 window). With the proper
  deferred-CNTV (D-8) + per-CPU CNTV re-arm (D-9) protocol,
  full 4-core scheduling works.

- **NUM_CPUS reverted to 1** (kernel `be058044`) for stable

### TL;DR — what changed since 2026-05-22

- **SMP Phase D investigation paused**. UART markers proved
  Pi 4 firmware does NOT reliably deliver secondaries into
  the armstub on cold boot (cpu0 never reaches in_el2,
  cpu1/2/3 reach it 0-or-1 times per boot, non-deterministic).
  The standard armstub spin-table protocol can't be the
  primary SMP wake mechanism on Pi 4. Resolving needs PSCI
  CPU_ON (EL3 secure monitor in the armstub) or a
  BCM2711 ARM_LOCAL_MAILBOX poke.
- **NUM_CPUS reverted to 1** (kernel `be058044`) for stable
  single-core baseline. Secondaries park in DAIF-masked WFI
  at the top of `_other_core_virtual` (kernel `7a6017ed`) so
  the assembly-side cbnz can't route them into C helpers that
  would race primary's GIC setup.
- **USB BAR-size bug fixed** (devices `2ac680e`).
  XHCI_MAP_SIZE 64 KiB → 4 KiB matches VL805's actual BAR0.
  Cross-OS verified (FreeBSD bcm2838_xhci, Linux xhci_pci_setup,
  Circle USBStandardHub, NetBSD xhci_pci_attach all use 4 KiB).
  Net effect: `rc=-19` (poison reads, primary failure mode for
  weeks) is replaced by `rc=-110` (reset timeout, secondary
  symptom) on most cold boots.
- **HCH guard before HCRST** (devices `b1ae732`).
  `xhci_reset` now reads USBSTS.HCH and halts the controller
  if it isn't already halted, per xHCI spec 5.4.1.
- **50 ms post-reprogram settling** (devices `0619a7f`).
  After re-arming the BCM2711 outbound window post mailbox
  notify, wait 50 ms for bridge translation to stabilise.
- **Cap-probe in-loop retry** (devices `4795266`).
  6× 100 ms retries if cap-space reads return the poison
  pattern (caplength=0xad / version=0xdead).
- **Public `bcm2711_pcie_resettleOutboundWindow()`**
  (devices `77fe93a`). Exported function that re-arms the
  bridge translation without re-running the full bring-up.
  Available for `xhci_reset` to call between HCRST write
  and the bit-clear wait, but not currently invoked from
  there because the bridge re-degrades faster than I can
  measure a code effect under rapid test cadence.

### Hardware constraint discovered this session

The BCM2711 PCIe bridge accumulates internal degradation across
rapid boot cycles. Empirical curve:

  - 30 min idle after previous-session degraded state →
    1st test post-idle: `rc=-110` (bridge healthy)
  - 3 back-to-back cycles in quick succession →
    `rc=-19` (bridge re-degraded)

Implication for USB iteration: **at most one test per build, with
a ≥30 min idle gap between builds**. The previous "run 3 cycles
to measure rates" pattern fights the hardware. See
`docs/notes/2026-05-23-usb-bridge-cooldown-curve.md`.

### Updated baseline

Single-core boot is stable: psh prompt reached every cycle, no
fault patterns, K120 keyboard visible to Pi 4 firmware
(VID 046d PID c31c) in pre-handoff DEV scan.

Most-recent integration snapshot:
`manifests/2026-05-23-post-marker-cleanup-bridge-stuck-rc19.md`.

### Marker cleanup this session

Stripped 186 lines of Phase-D diagnostic UART markers from
`_init.S` (kernel `774d188d`), armstub (project `833f654`),
and `secondary_smoke_entry` (plo `0722997`). Boot UART output
is now noise-free. The x9 reload trampoline at el1_entry
(kernel `979e05c0`) and the per-CPU stack fix in
`_set_up_vbar_and_stacks` are kept for future SMP work.

### TD list snapshot

- **TD-13-spawn-cap**: verified inactive across 232 captured
  logs (the cap at 32 iters never fires). Documented in
  `docs/notes/2026-05-23-td13-spawn-cap-analysis.md` with the
  smallest-experiment plan for an eventual root-cause attempt.

### Original 2026-05-22 status follows below (kept for reference)

## Previous Status: 2026-05-22 (SMP Phase A on; NUM_CPUS=4 active; Phase C step 2 PPI enable in place; secondaries wake on timer ticks)

Three milestones since the morning's 5/5 stability baseline:

1. **SMP Phase A on by default** (project `cf9fbbc`). `PLO_SMP_ENABLE=1`
   in the rpi4b board config. Cores 1-3 wake via the armstub spin-
   table two-stage release, branch into kernel `_start`, fall
   through MPIDR check to `_other_core_trap`, and park in the WFI
   loop in `_other_core_virtual` after their per-CPU VBAR / GIC /
   cpuInit. 3/3 boot cycles reach `(psh)%`; the previous Phase B
   re-entry pathology is silent now that the kernel marker prints
   have been stripped.

   **Follow-up A: NUM_CPUS bumped to 4** in
   `hal/aarch64/generic/config.h` (kernel `fb9669f4`). Activates
   the real-SMP code paths: LDAXR/STXR spinlocks, 4-bit GIC mask,
   per-CPU scheduler `current[]` array. 5/5 boot stability holds
   (manifest `2026-05-21-smp4-5of5-verified.md`).

   **Follow-up B: Phase C step 2 — per-CPU timer PPI enable**
   (kernel `fbfe3a3f`). Each secondary's `_hal_interruptsInitPerCPU`
   now flips its own banked bit in `GICD_ISENABLER0` for the
   architectural timer's PPI (with a spin-wait on
   `hal_timerIrq()` to ride out the boot-order race against
   primary's `_hal_timerInit`). Cores 1-3 should now exit their
   WFI loops on each timer tick and call into `threads_schedule`;
   `threads_init` already creates one idle thread per CPU at
   MAX_PRIO. 3/3 boot stability holds.

   What's still missing for "real" SMP scheduling: a way to
   visually confirm secondaries pick up the idle thread (the
   one-shot print in `threads_idlethr` was added then reverted
   because the post-fbcon UART backlog swallows late prints), and
   eventually a CPU-affinity hint or load-balancing pass so
   real worker threads actually distribute across CPUs.

2. **BCM2711 PCIe bridge bisected end-to-end**. Two distinct bugs
   identified and fixed (devices `c94be27`, `1ccfcea`, `cef62e1`):
   - Reading PCI_VENDOR_ID at bus=1, dev=1, fn=0 tore down the
     outbound translation. Fixed by capping per-bus device sweep at
     1 on every bus (PCIe Express is point-to-point).
   - `cfgio.destroy()` munmapping the bridge config window also
     poisoned the outbound translation. Worked around by leaking
     the bridge mapping until process exit.
   - `bcm2711NotifyXhciReset`'s mailbox mmap was failing silently
     because `RPI_MAILBOX_BASE_ADDRESS=0xfe00b880` isn't page-aligned.
     Page-align fix lets firmware-notify actually fire.

   With all PCIe-side bugs fixed, Codex-guided diag #2 confirmed
   `MISC_CPU_2_PCIE_MEM_WIN0_LO` reads back as `0xf8000000`
   (our programmed value) — bridge HW state is intact. **The
   remaining 0xdead poison xhci sees is a CPU-side pmap bug in the
   Phoenix kernel.** USB-HCD ops->init still fails (rc=-19) and
   cannot be fixed without kernel-side work. See
   `docs/notes/2026-05-21-pcie-bridge-ageing-codex.md`.

Authoritative baseline:
`manifests/2026-05-21-smp-phase-a-stable.md` (image SHA
`ebc1f77d302c9cc67cc97b3142bccbcd0a8390ed9d9172ba21047117ac25768f`,
kernel `2690736b`, plo `0ee44df`, devices `cef62e1`, project
`cf9fbbc`).

The 5/5 morning baseline (`manifests/2026-05-21-stability-5of5.md`)
is still the single-core reference for the pre-SMP regression
window. Today's added work hasn't disturbed any boot path; if
problems surface with SMP secondaries online, that manifest is the
clean fallback target for `restore-integration-state.sh`.

### What changed today

- **PCIe link / VL805 firmware settle now polled** (devices `5a833e0`).
  Fixed 100 ms PERST + 200 ms VL805 sleeps replaced with bounded
  polling (2 ms / 5 ms granularity). Typical wins: 70 ms + 195 ms.
- **xhci register-bit waits tightened** (devices `905b4f8`).
  `xhci_waitOpBits` and `xhci_portWaitBits` now do a 1 ms tight 50 µs
  burst before falling back to the original 1 ms cadence. Worst-case
  timeouts unchanged.
- **pl011-tty createTty0 retry budget 30 → 5** (devices `486b0a5`).
  /dev/tty0 has been non-fatal since TD-14, so when devfs lookup is
  slow we are better off giving up quickly and letting create_dev()
  handle /dev/console.
- **test-cycle-netboot default capture-secs 90 → 360 s** (coord
  `6b3c390`). Empirically the post-fbcon (psh)% prompt can land 60–300 s
  after `fbcon: ok` depending on TD-14 IPC variance; 360 s gives a
  comfortable margin.
- **EEPROM-prep one-shot SD image documented** (coord `56b5a8d`).
  `scripts/prepare-pi-eeprom-netboot.sh` regenerates the image and the
  operator can apply the ~30 s firmware-time win on next physical
  access — see manual-operator-instructions §9.1.

### Linux dev host bring-up: still nominal

Pi 4 boots end-to-end on this Linux box: build → power-cycle → UART
capture (picocom --logfile path) → analyze. Test cycle takes HDMI
snapshots every 25 s during the cycle and a final frame before
pi_power_off, useful when post-`fbcon: ok` UART output is interleaved.

### Open work

- **SMP Phase A still gated off** (PLO_SMP_ENABLE undefined). Phase A
  was previously known-working end-to-end (cores 1-3 woke and reached
  kernel `_other_core_trap`) but produced ~100 spurious re-entries per
  boot via an unidentified exception path; reverted pending root-cause
  investigation. The plo `secondary_smoke_entry → secondary_park`
  smoke is still on by default.
- **USB-HCD `ops->init` failing** with `oerr=-2` on real Pi 4 BCM2711
  PCIe path. Boot survives (USB isn't on the critical path to psh) but
  no USB devices are usable. Root cause unknown; PCIe init code now
  polls properly but xhci itself returns ENOENT somewhere in init.
- **TD-14 IPC slowness**: lookup("devfs") round trips are bimodal
  (~11 ms steady state, but rare outliers up to 43 s documented in
  TEMPORARY-FIXES). Drives the post-fbcon `(psh)%` latency variance.
  Today's `pl011-tty createTty0` 30 → 5 retry tightening narrows the
  damage; a real fix would need kernel proc_send investigation.
- **UART timestamping**: tio 3.9 with `--timestamp` block-buffers even
  with `stdbuf -oL`; picocom + ts(1) drops slow lines past the first
  kilobyte. Infrastructure in place (`--timestamp` flag) but default is
  off until the buffering issue is resolved.

## Previous Status: 2026-05-20 (Linux dev host operational; Pi 4 boots to fast `(psh)%` with caches enabled and 4 GB DRAM)

The Pi 4 port is now well past initial bring-up. Real hardware boots
reliably to an interactive `(psh)%` prompt in roughly 55–60 s after
power-on, with caches enabled end-to-end, both DRAM banks visible to
the kernel, the HDMI framebuffer console (`fbcon: ok`) up, and the
shell responsive enough that `Ctrl-C` cleanly exits the `pm` builtin
(`phoenix-rtos-utils b188911`, 2026-05-19).

Authoritative baseline: `manifests/2026-05-19-td12-stable-plus-pm-sigint.md`
(image SHA `8b3fc0b049a8`, kernel `c8a81d5e`, plo `6a5dfdd`, project
`dde9bb5`, devices `3899d38`, utils `b188911`, libphoenix `bd61195`).

### The 2026-05-17 cache breakthrough — what actually unblocked things

The pre-2026-05-17 status entries below describe months of cache-enable
attempts that all failed at the first cacheable load after `SCTLR.C=1`.
The root cause turned out to be **two missing pieces in the armstub**,
not the BCM2711 SLC, not Cortex-A72 prefetch behaviour, and not the
A72 erratum 1319367 workaround Phoenix already had:

1. **`L2CTLR_EL1 |= 0x22`** — BCM2711 requires the A72 cluster L2 cache
   RAM to use 3-cycle data latency (bit 0) and 1-cycle setup (bit 5).
   Without this, the very first cacheable D-side fill after `SCTLR.C=1`
   returns corrupt data. Every other Pi 4 bare-metal stack (TF-A
   `plat/rpi`, Circle, the canonical raspberrypi/tools armstub) sets
   these bits. Phoenix did not.
2. **Erratum 1319367 register-encoding fix** — Phoenix had been writing
   `S3_1_C15_C2_2` (`CPUACTLR2_EL1[0]`), which is not the documented A72
   sysreg encoding. The correct workaround per TF-A and Phoenix's own
   `docs/plans/a72-errata-sweep.md` is `CPUACTLR_EL1[46]=1`
   (`DIS_HW_PAGE_AGGREGATION`).

Both landed in project commit `dde9bb5` ("rpi4b/armstub: fix 1319367
register encoding + add L2CTLR_EL1 timing"). Once that was in place,
kernel commit `72242a05` ("kernel Phase Z works once armstub is fixed")
collapsed the entire deferred-cache-enable scaffolding back to a
single `SCTLR_EL1.M | C | I` write in `el1_entry` (see
`hal/aarch64/_init.S:609-613`). Dead helpers and ~250 lines of TD-04/
TD-15/TD-16 markers were stripped over the following days (kernel
`dccd0aee`, `5a2d3a77`, `6c65616f`; plo `c988e6a`, `568f4cf`,
`6a5dfdd`).

### Current verified behaviour on real Pi 4

| Subsystem | Status | Evidence |
|---|---|---|
| armstub → plo → kernel handoff | working with caches on | manifest `2026-05-17-armstub-1319367-and-L2CTLR-fix` |
| `SCTLR_EL1.M\|C\|I` | enabled in `el1_entry` (single shot) | `hal/aarch64/_init.S:609-613` |
| 4 GB DRAM | both banks visible (`pmap: nBanks=2`, 948 MB + 3008 MB = 3956 MB) | manifest `2026-05-17-pi4-full-4gb-ram-unlocked`; plo `84ffbea` reads firmware-patched DTB from PA 0xf8 |
| 8 user processes | spawn (bind, dummyfs, dummyfs-root, mkdir, pl011-tty, psh, usb, mkrootfs) | `2026-05-19-td12-stable-plus-pm-sigint` |
| `(psh)%` prompt | reached in ~146-line UART log | `2026-05-18-td12-pass4-speed-bundle-psh-prompt-fast` |
| `fbcon: ok` (HDMI text) | up | linux-host-bootstrap.md |
| `pm` interruptible | yes (`Ctrl-C` exits cleanly) | utils `b188911` |
| PL011 RX latency | ~6 ms (one TX batch per `pl011_thr` iteration) | devices `3899d38` |
| pcie + xhci | merged into single usb daemon process (eliminates cross-process bridge-state race) | project `fb771c4`, devices `b5cc6b0` (new `bcm2711-pcie.c`, 1027 lines) |
| devfs fast-path | restored after Pass-4 cleanup regression | kernel `c8a81d5e` |

### Open work (per linux-host-bootstrap.md "Open work items")

- **USB keyboard interactive verification** — code chain fully wired
  (xhci HC in usb daemon, `libusbdrv-usbkbd` built into the a72
  target via phoenix-rtos-build `f05f148`); needs a runbook walk with
  a USB keyboard physically attached on real Pi 4 to confirm
  `/dev/kbd0` materialises and keypresses reach psh. Runbook:
  `docs/interactive-verification-runbook.md`.
- **fbcon prompt-indent rendering glitch** — cosmetic; needs live
  instrumentation of `pl011_fbcon_putc` to capture the bytes between
  command output and the next prompt.
- **SMP** — cores 1-3 wake from the armstub spin-table and park in
  WFE; not actively dispatched. TD-01 still pending. Stage 3 of the
  cache/RAM/SMP roadmap.

### Linux dev host bring-up (2026-05-20)

Project is now portable to a dedicated Linux x86-64 development host
without the macOS+Lima VM. Build/test pipeline was migrated across
five commits (`8d352d1` … `b845a39`):

- `scripts/rebuild-rpi4b-fast.sh`, `assemble-rpi4b-*`,
  `export-rpi4b-*`, `verify-rpi4b-sdimg.sh`,
  `prepare-rpi4b-dtb.sh` — OS-aware dispatch (macOS keeps Lima, Linux
  runs directly on host).
- `scripts/netboot-server-{up,down}.sh`, `vm-netboot-server.sh` — Linux
  path invokes dnsmasq directly on the host's dedicated USB-Ethernet
  NIC (no socket_vmnet, no Lima bridge).
- `scripts/capture-rpi4b-uart.sh` — Linux device autodetection prefers
  persistent `/dev/serial/by-id/*` symlinks.
- `scripts/build-phoenix-toolchain-linux.sh` — new wrapper that builds
  the aarch64-phoenix toolchain into `$repo/.toolchain/aarch64-phoenix`.
- `scripts/bootstrap-linux-host.sh` + `docs/linux-host-bootstrap.md` —
  the on-ramp for fresh Ubuntu 24.04+ x86-64 hosts.

2026-05-20 follow-ups (this session):
- `scripts/pi_power_on.sh` / `scripts/pi_power_off.sh` — replaced the
  macOS-only `shortcuts run "Gniazdko..."` calls with an OS-aware
  dispatch: Darwin keeps Apple Home, Linux drives the Meross outdoor
  plug via `/home/houp/meross-plug/plug.py`. Verified end-to-end
  against the actual plug.
- `scripts/netboot-bridge-recover.sh` — Linux branch added (no VM to
  restart; bounces dnsmasq + re-ups the NIC).
- Repo-relative path defaults in `capture-rpi4b-uart.sh`,
  `uart-list.sh`, `uart-summary.sh`, `git-siblings.sh`,
  `snapshot-integration-state.sh`, `restore-integration-state.sh`,
  `psh-interact.py` — replaced hard-coded `/Users/witoldbolt/…`
  defaults with `$(dirname $0)/..` so the helpers work from any clone
  location.

The full netboot test cycle on this Linux host is not yet smoke-tested
end-to-end against real hardware; that's the next step.

### Doc-vs-code drift was significant

The sections below this one (last updated 2026-05-16) describe a state
where caches were still parked and USB+keyboard were still blocked at
xhci capProbe ENODEV. Those entries are kept for history but **do not
reflect the current code state.** The 2026-05-17 → 2026-05-19
manifests and the recent sibling commit messages are authoritative.

---

## Previous Status: 2026-05-16 (upstream-synced kernel; cache C-3 WIP continues)

The 2026-05-16 upstream sync is complete across the Phoenix sibling
repositories. `phoenix-rtos-kernel` was the only repository requiring manual
merge work; its dirty cache/MMU diagnostic state was checkpointed first, then
`origin/master` was merged into `agent/rpi4-program-reloc` as kernel commit
`2193fc4b`. The `proc/name.c` conflict was resolved by keeping upstream port
register/unregister and process-destroy API changes while preserving the local
TD-14/devfs lookup diagnostics.

Post-merge validation:

* `git diff --check` in `sources/phoenix-rtos-kernel`: clean.
* `./scripts/rebuild-rpi4b-fast.sh`: passed after cleaning stale Pi-target
  `libphoenix` build output in the disposable VM buildroot.
* Verified image SHA256:
  `242d495bd67079b8e566735c506839e68a9d39d5f112afa5426d31449c883ffa`.

Warning handled: the first post-merge fast rebuild failed at link time with a
`portRegister` multiple-definition error from stale generated `libphoenix`
syscall objects. This was not a source conflict; it was a stale incremental
artifact after the upstream syscall rename to `sys_portRegister`. Future
fast-rebuild automation should clean dependent libphoenix output when upstream
syscall headers or generated syscall names change.

Cache state after sync: the branch still contains C-3 diagnostic WIP, not a
boot-correct shipping cache configuration. I-cache-only enable is now deferred
until `main_initthr()` after `_usrv_start()`. The most useful result on
2026-05-16 was kernel commit `43635eca`, which strengthened the deferred
I-cache helper to clean+invalidate high-VA kernel text to PoC (`dc civac`)
before I-cache invalidation and `SCTLR.I=1`. That moved the real-Pi boundary
from the first post-I-cache `lib_printf()`/console path to inside
`posix_init()`:

```
iurstxy2z0I main_initthr: icache enabled ... abcd main_initthr: syspage listed ... ef
```

Temporary `posix_init()` markers then changed layout enough to regress the stop
back to `ab`, so that probe was reverted. Latest rebuilt image after the revert:
`artifacts/rpi4b/rpi4b-sd.img` SHA256
`710b10f76654ea65cd9ba5224fa0c798a8042e16ae09f630c37459dfdad88aee`.

D-cache remains parked until the I-cache-only path is stable and no longer
layout-sensitive.

## Previous Status: 2026-05-15 late (kernel M-only baseline + hygiene fixes; cache enable parked AGAIN after C-3u/c3v/c3w/c3z)

> **Retraction note (2026-05-15 morning).** The 2026-05-14 "M|C|I enabled on
> real Pi 4" milestone has been retracted. Bisect on 2026-05-15 morning showed
> the three cache-enable commits (3d9c5574, 3b63677f, f2b7c62f) silently broke
> user-process execution — every spawned user process exited before its own
> code could run, even though the kernel logged successful spawns. The shipped
> milestone criterion ("no fault, reaches `proc_reap` idle") was necessary but
> not sufficient. All three cache commits were reverted; the kernel returned
> to M-only baseline. Full retraction analysis is in the commit message of
> `phoenix-rtos-kernel 5bff6ebb`.

### 2026-05-15 C-3 second pass + late prefetch/SLC-bypass tests (parked)

After the retraction, a clean Linux-`__cpu_setup`-style cache enable plan was
designed (`docs/research/2026-05-15-cache-enable-c-approach-design.md`) and
re-attempted in steps C-1 through C-7. C-1 (PT pre-MMU construction) and C-2
(TTBR0 cacheable low-PA aliases) landed cleanly and verified
M-only-correct. C-3 (single-shot SCTLR.M|C|I write) was attempted in **20
variants** (c3a–c3t) covering single-shot vs staged enable, cacheable vs
NC walker mappings, deferred enable from `main.c` after `_hal_init`,
post-enable VA-range invalidates, double set/way invalidate, and split
D-only vs I-only enable. Later the session also tested c3u/c3v/c3w/c3z:
L1D prefetch disable, stronger A72 prefetch-disable sweep, and an
Outer-NC MAIR slot for normal kernel mappings / TTBR0 low-PA RAM aliases.
**Every variant fails** either at the first
post-MMU walker read (cacheable-walks case) or at the first post-SCTLR.C=1
cacheable data access (deferred-enable case).

The leading remaining hypothesis is the **BCM2711 SLC (system L2 cache)** —
ARM cache-maintenance ops only invalidate the A72 cluster's caches; the
SLC sits between the cluster and DDR and may hold firmware-era dirty lines
that surface as stale reads when SCTLR.C=1 enables data caching. However,
the simple MAIR Outer-NC bypass test did not move the stop point, so the
next cache-enable attempt should be either a real BCM2711 SLC maintenance
sequence or an assembly-only post-enable probe that never returns to C until
several known data reads have been proven. Full matrix and planned
strategies are in the research doc.

Latest failed real-Pi cache logs:

* `artifacts/rpi4b-uart/rpi4b-uart-20260515-184812-netboot-c3u-l1d-prefetch-disable.log`
* `artifacts/rpi4b-uart/rpi4b-uart-20260515-185112-netboot-c3vw-a72-prefetch-disable-sweep.log`
* `artifacts/rpi4b-uart/rpi4b-uart-20260515-185538-netboot-c3z-outer-nc-kernel-map.log`

### Locked-in shipping configuration (boot-correct on real Pi 4)

* armstub: A72 erratum 859971 + 1319367 + SMPEN at EL3 reset
* plo: M-only, teardown `dc ivac` (discard firmware lines, don't write back)
* kernel: M-only, `dc isw` (not `cisw`) for cold-start cache invalidate,
  `_hal_init` asm cleaned up (TD-04-hack-2 probe stores removed)
* 4 GB DRAM unlocked via `ddrh` map for chunk 2 in syspage
* HDMI: framebuffer console up (fbcon spawned)
* SMP smoke: cores 1-3 wake from armstub spin-table, park in WFE
* Userspace: all 8 expected processes spawn (bind / dummyfs /
  dummyfs-root / mkdir / pcie / pl011-tty / psh / usb), psh reaches
  interactive prompt

Verified manifest: `manifests/2026-05-15-cache-hygiene-fixes-m-only.md`.

### Kernel branch state

`agent/rpi4-program-reloc` has commit `f7fe6b39` "deferred cache-enable
helpers (C-3 work-in-progress)" on top of the boot-correct state, plus
late diagnostic deltas for c3v/c3w/c3z. The branch adds
`hal_cpuEnableDCache` and `hal_cpuEnableICache` asm helpers and a
late-enable call site in `main.c`. **The call site is intentionally left
enabled** as WIP state for cache diagnosis and is not boot-correct. To
return the kernel to boot-correct, either revert the C-3 WIP commits or
comment out the late cache-enable call in `main.c`.

---

## Previous Status: 2026-05-14 (MMU + D-cache + I-cache enabled on real Pi 4; TLBI hardening validated) — RETRACTED

### What changed

The cache-enable boundary moved substantially forward. The current real Pi 4
image runs with `SCTLR_EL1.M|C|I` enabled in the kernel, reaches the full
configured userspace spawn set, and remains alive through the UART capture
window:

```
main: spawned dummyfs-root (2)
main: spawned dummyfs (3)
main: spawned pl011-tty (4)
main: spawned mkdir (5)
main: spawned bind (6)
main: spawned pcie (7)
main: spawned usb (8)
main: spawned psh (9)
main: spawn loop done, entering proc_reap idle
```

Latest validated real-Pi run:

* image SHA256: `fb4493c1e1bed1ed7fd5752d8d5657846f97d1da63eaab5fe0239089c07eabac`
* UART log: `artifacts/rpi4b-uart/rpi4b-uart-20260514-154015-netboot-tlbi-fix-stable-cache-policy.log`
* no `Exception`, `Data Abort`, `panic`, or `fault` matches in the captured
  Phoenix path
* current restored export from the same source state after reverting the failed
  zone retry: `artifacts/rpi4b/rpi4b-sd.img` SHA256
  `2dee8c288549751641e4e4052feb2044b9fc208522f915c07f4165c30eb11651`

Previous validated real-Pi run for the same cache policy before TLBI
hardening:

* image SHA256: `b36d2e7fe4d2ec78728c816fd191d2bce0678be2e00adcca621ac71e0461dfec`
* UART log: `artifacts/rpi4b-uart/rpi4b-uart-20260514-094723-netboot-restored-cacheable-user-data-zone-uncached.log`
* no `Exception`, `Data Abort`, `panic`, or `fault` matches after the controlled reboot

### New cache boundary

The successful configuration is now materially narrower than the first
cache-enable workaround:

* `vm/amap.c`: temporary `amap` copy/zero mappings are cacheable again.
  `amap_page()` invalidates the cacheable source alias for firmware-loaded
  object pages before copying, and invalidates freshly allocated destination
  pages before first zero/copy reuse.
* `proc/process.c`: writable ELF `PT_LOAD` mappings and explicit BSS-tail
  mappings are cacheable again.
* `hal/aarch64/aarch64.h` and `hal/aarch64/pmap.c`: TLBI operations now issue
  the post-TLBI ISB, and invalid-to-valid L3 PTE creation invalidates the VA
  after the descriptor is visible. This keeps runtime PTE creation
  architecturally ordered with D-cache enabled.
* `hal/aarch64/_init.S`: kernel flips `SCTLR_EL1.M|C|I`; some early bootstrap
  mappings and pmap metadata remain non-cacheable as a separate cleanup
  boundary.
* `vm/zone.c`: zone allocator backing pages still need `MAP_UNCACHED`. A
  negative-control test making them cacheable faulted in `_vm_zalloc()` while
  spawning `dummyfs-root`, with a garbage free-list pointer. Invalidating the
  cacheable zone backing range before free-list initialization did not fix it;
  invalidate-plus-flush after free-list initialization regressed even earlier
  in `main_initthr`.

Two negative controls were important:

* Re-enabling `SCTLR_EL1.I` on top of D-cache succeeded on real hardware.
* Restoring `amap` temporary mappings to cacheable immediately regressed to
  repeated EL0 Data Aborts in `dummyfs` `_atexit_init()` / `memset`, even while
  writable user ELF mappings remained uncached. The failing log is
  `artifacts/rpi4b-uart/rpi4b-uart-20260514-091158-netboot-icache-dcache-cacheable-amap-writable-uncached.log`.
* Adding targeted `amap_page()` invalidation for object-source and fresh
  destination pages then made both cacheable `amap` aliases and cacheable
  writable ELF data pass on real Pi.
* Making `_page_sbrk()` dynamic kernel heap pages cacheable while leaving zones
  uncached did not fault, but failed to complete page scanning within 180s
  (`artifacts/rpi4b-uart/rpi4b-uart-20260514-152547-netboot-cacheable-page-sbrk-zone-uncached-long.log`).
  Making both initial and dynamic kernel heap mappings cacheable regressed
  further, even with TLBI hardening
  (`artifacts/rpi4b-uart/rpi4b-uart-20260514-153601-netboot-cacheable-kheap-tlbi-fix-zone-uncached.log`).
  These heap-cacheability attempts are rejected for now; kernel heap/pmap
  bootstrap metadata remain non-cacheable.
* Retrying cacheable zone backing pages after TLBI hardening still failed in
  `_vm_zalloc()` while spawning `dummyfs-root`
  (`artifacts/rpi4b-uart/rpi4b-uart-20260514-154825-netboot-cacheable-zone-after-tlbi-fix.log`).
  TLBI ordering is therefore not the root cause of the zone free-list
  corruption.

Working hypothesis: the real bug was stale cache lines becoming visible through
new cacheable aliases when Phoenix reused freshly allocated application pages
or read firmware-loaded object pages through `amap` temporary mappings. The
current fix is still AArch64-specific and should be generalized before
upstreaming, but it no longer keeps writable user data globally uncached.

### Validation caveats and warnings

* `./scripts/rebuild-rpi4b-fast.sh` continued to warn that core repos are
  dirty. `phoenix-rtos-kernel` contains the active cache work; the dirty
  `phoenix-rtos-devices` XHCI change predates this step and was not touched.
* The fast build regenerated the missing Pi 4 DTB by copying the official
  firmware blob from `external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb`.
  No dtc lint was run unless `RPI4B_DTB_LINT=1` is set.
* Netboot still commonly misses first DHCP and recovers by restarting the Lima
  bridge VM. Lima prints broken-pipe / closed-socket errors during shutdown;
  recovery succeeded and the Pi then booted from TFTP.
* `scripts/qemu-shell-smoke.sh rpi4b` timed out in QEMU 10.2.2 at early marker
  `A3` and did not reach the kernel banner. Treat real Pi UART as authoritative
  for this step; QEMU rpi4b remains a separate emulator discrepancy.
* A failed cacheable-`amap` test produced continuous UART exception spam; the
  watchdog did not stop it cleanly, so the test process had to be killed from
  the host. The test harness should learn to terminate spammy sessions more
  reliably.

### Immediate next step

Harden and clean up the cacheable-data fix:

1. Audit shared-anonymous COW source handling; current invalidation is only for
   object-backed sources and freshly allocated destinations.
2. Diagnose why cacheable kernel heap mappings stall during page scanning
   before retrying `_page_sbrk()` or bootstrap heap cacheability. Do not repeat
   the rejected direct cacheable-heap tests without a materially different
   hypothesis.
3. Diagnose zone allocator page cache hygiene before retrying cacheable
   `vm/zone.c`; direct `MAP_NONE` regressed in
   `artifacts/rpi4b-uart/rpi4b-uart-20260514-093855-netboot-cacheable-zone-backed-pages.log`,
   and invalidate-before-init regressed in
   `artifacts/rpi4b-uart/rpi4b-uart-20260514-094326-netboot-cacheable-zone-inval-before-init.log`.
   Invalidate-plus-flush also failed in
   `artifacts/rpi4b-uart/rpi4b-uart-20260514-095141-netboot-cacheable-zone-inval-flush-free-list.log`.
4. Remove or gate the temporary cache-bring-up UART/debug probes once the cache
   policy is stable enough for the next subsystem step.

## Current Status: 2026-05-13 (cache enable parked at walker boundary; baseline still boots to psh)

### TL;DR of today's session

* Applied A72 erratum **1319367** workaround in the armstub
  (`phoenix-rtos-project a27bc07`). Closes the speculative-AT
  hazard. Baseline boots cleanly with it; UART markers now show
  `1 3 2` (859971 / 1319367 / SMPEN) at EL3 reset.
* Ran iter-7 through iter-12 of kernel D-cache enable. All failed
  with the same translation-fault-L3 at `FAR=0xffffffffc0001890`.
* iter-12-diag (image `b597c1f7…1e743807`) proved the walker uses
  a memory path **distinct from the D-cache view**: kernel reads
  TTL3[0]/[1] correctly via `ldr` but the walker reports them as
  invalid. Cache maintenance and ordering changes can't bridge
  this gap. Full forensics in
  [docs/research/2026-05-13-iter-11-12-cache-walker-finding.md](research/2026-05-13-iter-11-12-cache-walker-finding.md).
* Cache enable parked. Worktree reverted to functional baseline.
  Locked-in image: SHA `2e8ed7dd…af69b592d` (current rebuild) /
  `c6fb8ab9…bacf1ead` (today's earlier baseline manifest).

### What still works (locked-in baseline)

* plo + kernel + userspace boot end-to-end to `(psh)%` prompt.
* 4 GB DRAM unlocked (`ddrh` map for chunk 2 in syspage).
* HDMI shows psh prompt on display.
* SMP smoke: cores 1-3 wake via spin-table, print marker, WFE.
* armstub A72 errata 859971 + 1319367 applied at EL3 reset.

### What's still blocked

* Cache enable (D and I) — see "walker uses separate path" finding.
* USB+keyboard — PCIe VL805 enumeration reads vendor/device/BARs
  as all zero. Separate from cache. Likely needs the mailbox
  VL805 reset to land before kernel config-space reads.
* Full SMP (cores 1-3 currently park in WFE after the smoke marker).

---

## 2026-05-12 (Previous milestone — kernel boots to userspace)

### What just landed

Three commits, all driven by the multi-subagent finding that Circle, rust-raspberrypi-OS-tutorials, and NetBSD evbarm all follow the same canonical AArch64 boot recipe: drop to EL1 BEFORE enabling MMU, do zero pre-MMU cache maintenance, single SCTLR write — and Phoenix-RTOS plo was doing essentially the opposite.

| Repo | Commit | Summary |
|---|---|---|
| plo | `9357e0b` | `_init.S start_el2` minimal EL2 setup then `eret` to `start_el1`; remove `dc isw` set/way loop; slot-E handler uses `_EL1` regs; staged SCTLR.M-only enable; drawSignal re-enabled |
| kernel | `94905263` | `_init.S` Step 5 cache enable: single SCTLR.M write (no C, no I — TODOs) at EL1 with proper barrier ritual |
| project | `fa4181b` | armstub `CPUACTLR_EL1[32]` (A72 errata 859971 DIS_INSTR_PREFETCH) reordered to write BEFORE SMPEN — TF-A says writes after SMPEN may be silently dropped on r0p3 |

Runtime verified: read `CPUACTLR_EL1` at EL1 after armstub→EL2→EL1 ERETs — bit 32 sticks.

### Boot reaches USERSPACE for the first time

Real Pi 4, image SHA256 `8524f4108a5b317d0f8f2302bba5af7f45e95d557c62710157ccb93623b0fc7e`, 180 s UART capture:

```
arm_loader: Starting ARM with 948MB
12 A S 0 TR0..TR3
hal: console_init done -> mem: pre/post-init/map/iallu
mem: post-read-sctlr -> mem: pre/post-sctlr-M -> mem: post-enable
hal: hal_memoryInit done -> timer_init done
video: pre-fbInit / mbox call / post-fbInit
draw: enter / pre-bg-fill / post-bg-fill / post-dcacheClean   <- HDMI ORANGE RECTANGLE renders
td16: arm_freq Hz = 0x59682f00                                <- 1.5 GHz
hal: video_init done -> entry EL1 -> init complete
Phoenix-RTOS loader v. 1.21 rev: unknown
hal: Cortex-A72 Generic
cmd: Executing pre-init script
... all apps loaded (kernel, dummyfs, pl011-tty, mkdir, bind,
    pcie, usb, psh) ...
call: exec go! -> hal: jump exit el1
        |
        v   plo erets to EL1 with caches off
        v
probe: pre-jump read#1 / no diff (DDR stable)
probe[0x310..328]= valid syspage values
A1 ZK[LSTUMV X1 X2 X3 X4 X5 N!YOPSTUZbcd
td15:OK                                                       <- TD-15 probe PASSED
td16:cf=0337f980 dt=0000000000872ba4                           <- ARM counter measurement
main: hal init done
Phoenix-RTOS microkernel v. 3.3.1 rev. ######## +0           <- KERNEL BANNER
vm: enter -> page init done                                   <- VM SUBSYSTEM
threads: ready queued / context created (repeats)             <- THREADS SUBSYSTEM
console: pl011 init done                                       <- CONSOLE DRIVER
pcie: linkUp=1 rcMode=1                                        <- PCIe LINK UP
pcie: 00:00.0 ven=14e4 dev=2711 cls=060400 hdr=01             <- BCM2711 PCIe bridge
pcie: 01:00.0 ven=1106 dev=3483 cls=0c0330 hdr=00             <- VIA VL805 USB
pcie: VL805 cmd 0000->0006 rb=0006                            <- VL805 BAR PROGRAMMED
pcie: bcm2711NotifyXhciReset enter
fbcon: ok                                                      <- KERNEL FRAMEBUFFER
xhci: capProbe iter pre / ENOSYS (ok) / post capProbe
xhci: pre reset
psh: tty open / isatty done / ready                            <- PHOENIX SHELL
... threads continuing past capture window ...
```

The capture window (180 s) was not long enough to see if psh reaches a prompt. Need a longer capture or interactive UART to confirm shell prompt.

### Open: cache-enable TODOs

Both items below are real cache-related faults that we haven't root-caused, but they no longer block the boot. The MMU-only baseline is correct ARM-architectural behavior (ARM ARM B2.4.4: SCTLR.{C,I}=0 ⇒ Non-cacheable per-access) so the cost is only boot-time performance.

* **TD-plo-dcache** — `SCTLR.C=1` (D-cache enable) causes a wild-pointer-deref fault inside plo's `exec kernel ram0` command path (EC=0x22 PC-alignment with FAR holding garbage). Reproduced at both EL2 and EL1; staged vs single-shot SCTLR doesn't matter; pre-MMU `dc ivac` doesn't help; 859971 workaround is confirmed effective.
* **TD-plo-icache** — `SCTLR.I=1` (I-cache enable) causes a "first I-fetch after SCTLR.I=1 returns garbage" fault (EC=0x00 Unknown/Undefined Instruction). 859971 confirmed effective; no other A72 workaround we've tried fixes it.

Both are likely a single missing A72 bit or BCM2711-specific quirk we haven't located. Not blocking userspace bring-up.

### Sibling commits + manifest

* `manifests/2026-05-12-mmu-only-clean-handoff.md` (yesterday's milestone)
* New manifest pending: `manifests/2026-05-12-kernel-boots-to-userspace.md`

## Current Status: 2026-05-12 (BREAKTHROUGH — 4GB unlocked, plo->kernel handoff clean, kernel past prior cache-hang to NEW frontier)

### Root cause finally identified

A four-subagent investigation (transcripts in
`docs/research/2026-05-12-vpu-l2-deep-dive.md`) converged on a real,
citation-backed root cause for every cache-related fault we've been
chasing for the past two weeks: **BCM2711 has a separate 1 MB "system
L2" cache used by the VPU/GPU that sits on a different SoC fabric than
the A72 cluster's L1+L2**. start4.elf primes that cache before
handoff. The system L2 is NOT a participant in the A72's inner-
shareable broadcast domain, so A72-side cache maintenance ops cannot
reach it:

* `dc isw` (set/way) — local to A72 cluster caches only; ARM ARM
  B2.2.6 + DEN0024 explicitly say set/way is "for cache power-down
  only, NOT for coherency" and Linux/U-Boot/FreeBSD never use it on
  the boot path.
* `dc ivac` / `ic ialluis` (VA-based IS-broadcast) — broadcast in
  the IS domain; the BCM2711 system L2 is outside that domain.

The architectural fix is a mailbox call to VC4 asking it to clean its
own system L2 (tag TBD — TD-plo-icache in
`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`). Until that lands, both
plo and the kernel run with caches off (SCTLR.M=1, C=0, I=0).
Performance is acceptable — these are short-lived boot paths.

### What now works (real Pi 4, image SHA256 89bc66be...)

```
arm_loader: Starting ARM with 948MB
12 A S 0 TR0..TR3                           <- armstub + plo relocator
hal: console_init done
mem: pre-init/post-init/post-map            <- mmu_init + 4GB cacheable remap
mem: post-dc-ivac                           <- per-VA dc ivac over plo image
mem: post-tlbi
mem: pre-sctlr-write-M / post-sctlr-write-M <- M-only, set_sctlr ritual
mem: post-enable
hal: hal_memoryInit done -> timer_init done
video: pre-fbInit / fb-init / mailbox call  <- full mailbox property call success
video: post-fbInit / post-drawSignal        <- (drawSignal skipped, see below)
video: post-armFreq                          <- td16: arm_freq Hz = 0x59682f00 (1.5 GHz!)
hal: video_init done -> entry EL2 -> init complete
[1mPhoenix-RTOS loader v. 1.21 rev: unknown[0m
hal: Cortex-A72 Generic
cmd: Executing pre-init script
call: opened user.plo on ram0
call: magic ok user.plo
call: exec alias -r phoenix-aarch64a72-generic.elf 0x1000 0x26148
call: exec kernel ram0
... all apps loaded (kernel, system.dtb, dummyfs-root, dummyfs,
    pl011-tty, mkdir, bind, pcie, usb, psh) ...
call: exec go!
go: enter -> devs done -> hal done -> jump
hal: jump entry -> jump irq off -> jump exit el1   <- plo erets to EL1
probe: pre-jump read#1
probe: no diff (DDR stable)                         <- DDR coherent across handoff
probe[0x310]=0000000000000000                       <- KERNEL READS SYSPAGE CLEANLY
probe[0x318]=00000000002173a8
probe[0x320]=0000000000217288
probe[0x328]=0047800000000002
td15: probe write start / done @ pa=0x02000000
A2 ZK[LSTUMV X1 X2 X3 X4 X5                         <- KERNEL RELOCATOR + cache enable
L                                                   <- partial early exception print
```

The post-X5 'L' is the kernel's `_early_exception_common` handler
printing the beginning of "ELR=" but the rest of the dump (the actual
ELR/FAR/ESR hex) doesn't appear — the handler itself hits a recursive
fault because it uses `PL011_TTY_EARLY_VADDR` which requires the
device TTL3 mapping that isn't set up yet at X5. This is a NEW
frontier — we have never reached this point before. The cache-
corruption arc that ate the prior two weeks is genuinely resolved.

### Active TODOs (carry forward)

* **TD-plo-icache** — implement VC4 mailbox call to flush VPU system
  L2, then re-enable SCTLR.M|C|I in both plo and kernel. Long-term.
* **Next frontier: post-X5 kernel fault** — the kernel sets up TTL2
  for the kernel image after X5 but before installing the real
  device mapping for PL011. An exception in that window leaves the
  early handler unable to print. Two paths: (a) install the early
  device mapping earlier; (b) add a marker right before/after the
  TTL2 setup loop to localise which instruction faults.
* **TD-plo-drawsignal** — VC4-allocated framebuffer sits in 76 MB
  GPU reserve which plo keeps Device-nGnRE; bg-fill is too slow for
  meaningful HDMI output. Real fix: cache the framebuffer mapping
  AFTER the VC4 L2-flush mailbox lands.

### Sibling commits

* `phoenix-rtos-project 2285fb8` — config.txt: gpu_mem=76 lock
* `plo e2e6ae1` — aarch64a72-generic .ldt: SIZE_DDR 948MB -> 3.94GB + GPU-hole carve-out
* `plo 7bbe1f2` — hal_memoryInit: Linux set_sctlr ritual + per-VA dc ivac + MMU-only
* `kernel a551692c` — _init.S Step 5: MMU-only enable (skip C and I)

## Current Status: 2026-05-11 (EL2 fault localised — EC=0x00 sync abort after plo MMU+caches enable; root cause hypotheses ranked)

### What works as of today

- armstub (EL3) → plo (EL2) handoff with A72 errata 859971 + SMPEN applied (commit `0f6be40`, `phoenix-armstub8-rpi4.S`)
- Path A: plo `mmu.c` / `cache.c` dispatch on `currentEL` at runtime — zynqmp unchanged, rpi4b uses `*_EL2` bank (committed in `plo codex/common-aarch64-platform-makefiles e90bdcd`)
- TCR_EL2 fixup in `mmu_setTranslationRegs`: bit 23 (EL1's EPD1) cleared, IPS placed at bits 18:16 (EL2 layout) instead of 34:32 (EL1 layout)
- **Staged SCTLR_EL2 writes succeed**: M, then M|I, then M|I|C each followed by ISB. All three completion markers print.
- plo MMU + I-cache + D-cache active at EL2 — `mem: post-enable` prints
- `hal_init` continues through `timer_init` cleanly post-MMU
- `video_init` enters, `hal_memset` on DRAM buffer at 0x02000000 works, `hal_dcacheClean` works, first MMIO read of mailbox status at 0xfe00b898 returns sensible `0x40000000` (mbox_empty=1, mbox_full=0)
- 2–3 more PL011 `hal_consolePrint` calls succeed past the mailbox status read

### Where the boot hangs and what we measured

A **synchronous exception at VBAR_EL2 + 0x200** (Current EL with SPx, Sync) fires after the `mbox: skip-wait` print. Our instrumented diag vector (replaces the EL3-only `_exceptions_dispatch` so we don't recursively trap) prints:

```
E
ESR=0000000002000000   <- EC = 0x00 (Unknown), IL = 1
ELR=0000000000202600   <- inside video_mailboxCall
FAR=0000000000000000   <- not set (consistent with EC=0x00)
SPR=00000000600003c9   <- EL2h, NZ set, DAIF all set
SCT=0000000030c5183d   <- SCTLR_EL2: M|C|I=1, SA=1, all RES1 forced
```

Disasm at the ELR:

```
2025fc: adrp x0, 20c000      <- last 4 bytes of cache line 0x2025c0-0x2025ff
202600: add  x0, x0, #0x718  <- FIRST instruction of cache line 0x202600-0x20263f  *** ELR_EL2 ***
202604: bl   hal_consolePrint
```

The fault PC is exactly at a 64-byte cache-line boundary. The instruction (`add x0, x0, #0x718`, encoded `0x911c6000`) cannot architecturally raise EC=0x00 on its own. So we are observing either (a) the I-cache fetching different bits than what is in RAM, or (b) an unrouted/CONSTRAINED-UNPREDICTABLE event being miscategorised by the CPU as Unknown.

Plo's `hal_consolePrint` is a leaf (no SP push); `video_td16PrintHex32` uses a properly 32-byte-aligned stack frame. SP alignment is ruled out as the cause.

### Ranked hypotheses (synthesis of 6 subagent reports)

**HIGH probability**

1. **HCR_EL2 missing AMO/IMO/FMO**. Plo sets `HCR_EL2 = (1<<31) | (1<<29)` = `RW=1, ATA=1` only. With `AMO=0`, a physical SError targets EL1, but plo runs at EL2 with no EL1 active → CONSTRAINED-UNPREDICTABLE → can surface as a fault that the CPU reports with EC=0x00. U-Boot's `start.S` sets AMO and unmasks SError early; Linux's `init_el2_hcr` macro routes phys SError/IRQ/FIQ to EL2 in nVHE mode. — *fix: HCR_EL2 = `(1<<31) | (1<<29) | (1<<5) | (1<<4) | (1<<3)` (RW, ATA, AMO, IMO, FMO).*
2. **I-cache aliasing post-SCTLR.I=1 — `dc isw` is not coherency-grade**. ARM ARM D5.10.2 explicitly says set/way ops are for power-down, NOT for I/D coherency. Multiple Pi 4 reports (forum t=345466, OSv #1100) of "ELR at a sane instruction, EC=Unknown" were resolved by adding per-cache-line `dc cvau` + `ic ivau` over the .text range, NOT by `ic iallu` once. Our plo only does `ic iallu` once (before SCTLR.I=1) and `dc isw` once early; if any speculative I-fetch ran into a stale L2 line between SCTLR.I=1 and the fault, the result is garbage decoded as undef. The fault PC being at a fresh 64-byte cache-line fill is consistent. — *fix: after the M|I|C SCTLR write, do `ic iallu; dsb ish; isb`, and ideally walk plo's `[__text_start, __etext)` issuing `dc cvau` + `ic ivau` per cache line.*
3. **MDCR_EL2 / HSTR_EL2 left at UNKNOWN reset values**. Linux's `init_el2_state` always writes MDCR_EL2 and HSTR_EL2=0 to disable debug-related and CP15 traps. Plo never touches them. An UNKNOWN value could trap a speculative debug uop, surfacing as EC=0x00 from EL2's perspective. — *fix: `msr mdcr_el2, xzr; msr hstr_el2, xzr`.*

**MED probability**

4. **VPIDR_EL2 / VMPIDR_EL2 UNKNOWN**. Linux mirrors `MIDR_EL1 → VPIDR_EL2` and `MPIDR_EL1 → VMPIDR_EL2`. Doesn't directly cause faults, but EL1 code reading MIDR/MPIDR will get garbage if we ever drop to EL1.
5. **SCTLR_EL2 baseline `0x30c00838` is missing RES1 bit 18 in the *write*** (HW forces it to 1 on read-back, so we observe `0x30c5083d` post-OR). Architecturally writing 0 to a RES1 bit is CONSTRAINED UNPREDICTABLE prior to ARMv8.2. Use `0x30c50838` instead.
6. **CPTR_EL2 = 0 instead of 0x33ff**. On A72 the RES1 bits force these on read, so semantically equivalent — but mainline's value is the documented-correct one.

**LOW probability (ruled out or weak)**

- A72 erratum 859971 (already applied at EL3 by `phoenix-armstub8-rpi4.S`)
- A72 erratum 855872 (A53-specific, doesn't apply)
- SP alignment (hal_consolePrint is leaf; video_td16PrintHex32 aligned)
- MMU translation/permission fault (FAR=0 rules these out; would be EC=0x21/0x25)

### Next on-device cycle (planned, ready to land when Pi is available)

Single bundled patch addressing items 1–5:

- `plo hal/aarch64/generic/_init.S` `start_el2`:
  - Mask DAIF defensively (`msr daifSet, #0xf`) before any state writes
  - Set MDCR_EL2 = 0, HSTR_EL2 = 0
  - Mirror MIDR_EL1 → VPIDR_EL2, MPIDR_EL1 → VMPIDR_EL2
  - HCR_EL2 = `(1<<31) | (1<<29) | (1<<5) | (1<<4) | (1<<3)` (RW | ATA | AMO | IMO | FMO)
  - SCTLR_EL2 baseline = `0x30c50838` (full A72 RES1 mask, no functional bits)
  - CPTR_EL2 = `0x33ff` (explicit, matches mainline)

- `plo hal/aarch64/generic/hal.c` `hal_memoryInit` after the M|I|C SCTLR write:
  - Add `ic iallu; dsb ish; isb`
  - (Optional, if previous still faults) Loop `dc cvau`+`ic ivau` over plo's `[__text_start, __etext)`

Already-built diag infrastructure stays (richer slot-E dump including TTBR0/TCR/MAIR and the 4-byte instruction word at `[ELR_EL2]`) so even if the new patch shifts the fault, we get richer evidence in one cycle.

### Sources (subagent research, today)

- Linux `arch/arm64/include/asm/el2_setup.h` (init_el2_state, init_el2_hcr)
- U-Boot `arch/arm/cpu/armv8/start.S` and `cache_v8.c`
- TF-A `lib/cpus/aarch64/cortex_a72.S` (859971, 1319367)
- Cortex-A72 errata notice ARM-EPM-012079 (relevant items: 832075, 855873, 859971)
- ARM ARM DDI 0487 §B2.7.2, §D5.10.2, §D13.2.36 (ESR_EL2 EC decode)
- Pi forum t=345466, t=331894; OSv issue 1100; Zyngier idmap patch series
- Circle armstub8.S, rust-rpi-tutorials, NetBSD locore_el2.S — all drop EL2→EL1 before MMU; plo is uniquely under-tested by staying at EL2

### Diagnostic state checked in but not committed yet

- `plo/hal/aarch64/generic/_init.S` — diag vector tags (A..P) at each VBAR_EL2 slot + `_slot_e_dump` handler reading ESR/ELR/FAR/SPSR/SCTLR/TTBR0/TCR/MAIR + instruction-word at ELR
- `plo/hal/aarch64/generic/hal.c` — staged SCTLR_EL2 writes with per-stage markers, hal_init reordered (console_init before hal_memoryInit), `#include "../mmu.h"`
- `plo/hal/aarch64/mmu.c` — TCR_EL2 fixup, EL-aware sysreg dispatch, pre/post-flip cache barrier ritual in `mmu_enable`
- `plo/hal/aarch64/generic/video.c` — instrumentation markers + diagnostic mailbox status probe
- See `manifests/2026-05-11-plo-el2-fault-diagnostic-state.md` for tested image SHA and uncommitted diff list.

## Current Status: 2026-05-10 (Path A plo MMU/cache EL-aware landed; Step 3 still hangs pending diagnostic instrumentation)

After Steps 1+2 landed on 2026-05-08, Step 3 (plo runs with
MMU+caches ON) was attempted and revealed the structural blocker:
plo's `mmu.c` writes `*_EL3` sysregs but rpi4b plo runs at EL2
(armstub `eret`s to `EL2h`), so the writes trap. The
`docs/plans/plo-el2-mmu-fix.md` plan recommended Path A: generalise
`mmu.c` + `cache.c` to dispatch on `currentEL` at runtime.

**Path A committed**: `plo codex/common-aarch64-platform-makefiles e90bdcd`.
Validated on real Pi 4 (image `89645ec6`) — baseline boots through
`fbcon: ok` cleanly. For zynqmp the new dispatch resolves to the
prior `*_EL3` sequence (no functional change).

Step 3 retested on top of Path A still hangs plo before its
banner. Remaining suspects: TCR_EL2 layout edge cases, the
`sctlr_el2` baseline from the armstub, or another sysreg access
path not covered.

**Next**: Step 7 (early-boot diagnostic instrumentation —
`docs/plans/early-boot-diagnostic-instrumentation.md`) must land
before Step 3 can be debugged. The instrumentation patch is
already designed end-to-end with macros, header file, diffs.

Manifest: `manifests/2026-05-10-pathA-plo-mmu-cache-el-aware.md`.

## Current Status: 2026-05-08 (Round-3 deep-research + Steps 1-2 of canonical-idiom alignment validated; cache enable still blocked on plo-side Step 3)

After Phase A and Phase B both failed in early May, the project ran
a 10-agent deep-research wave (FreeBSD / NetBSD / OpenBSD / seL4 +
Genode / ARM ARM authoritative / BCM2711 firmware hand-off / plo-kernel
handoff / bare-metal Pi 4 forum / diagnostic techniques / Phoenix-port-
conventions audit). Output: `docs/research/round3-*.md` (10 briefs)
plus `docs/research/round3-cache-enable-synthesis.md` integrating
everything.

**Headline finding**: rpi4b deviates from the canonical Phoenix
A-class plo→kernel handoff on four independent dimensions vs
imx6ull/zynq7000/zynqmp:

1. plo cache state — canonical: MMU+caches ON; rpi4b: caches OFF
2. plo flush range — canonical: full DDR; rpi4b: heap only
3. kernel `_start` `dc isw` — canonical: called; rpi4b: function
   exists, never called
4. SCTLR flip — canonical: M|C|I single write; rpi4b: M only

The synthesis proposes a 7-step ordered fix sequence. Steps 1-4 are
individually no-ops in the current caches-off boot (low-risk per
step); Step 5 is the actual cache enable.

**Validated and committed in this session**:

- **Kernel Step 1** (`phoenix-rtos-kernel agent/rpi4-program-reloc 763b210a`):
  call `hal_cpuInvalDataCacheAll` at `_start`; switch
  `tlbi vmalle1 → tlbi vmalle1is`. Boots through `psh: readcmd`.

- **plo Step 2** (`plo codex/common-aarch64-platform-makefiles c4f6dda`):
  extend `hal_cpuJump` civac range from heap-only to full DDR.
  Boots through `psh: readcmd`.

- **Armstub A72 erratum 859971** (`phoenix-rtos-project master 0f6be40`):
  apply `CPUACTLR_EL1[32] DIS_INSTR_PREFETCH` at EL3 (the kernel-side
  attempt traps from EL1 on A72 r0p3; ATF applies it at EL3).
  Critical bit-number correction: 859971 is bit 32, not bit 47.

**Step 5 attempted but failed** (image 0cec8e0d): single SCTLR
M|C|I write with full canonical barrier ritual. Same X3 hang as
previous Phase B — boot reaches the pre-flip fence but hangs at
the SCTLR write itself. Steps 1+2 are no-ops in plo's caches-off
configuration so they don't change Phase B outcome.

**Step 3 (plo MMU+caches ON, mirroring zynqmp) is the missing
prerequisite.** Patch drafted in
`docs/plans/canonical-idiom-step2-step3-plo-patches.md` for
application in the next session.

Reverted to known-good baseline (image 78c072f3); kernel boots to
`psh: readcmd` cleanly with Steps 1+2 in. Step 5 diff preserved at
`docs/plans/canonical-idiom-step5-mci-flip.md`.

Manifest: `manifests/2026-05-08-round3-step12-validated.md`.

## Current Status: 2026-05-06 (Stage 4 phase 2 in progress)

**Stage 4 phase 2 (USB keyboard via VL805 xHCI): three working fixes
landed; xhci_init reaches `xhci_allocCommandSpace` before failing.**

Manifest: `manifests/2026-05-06-stage4-phase2-vl805-bme-fix.md`.
Sibling commits:
- `phoenix-rtos-devices` master `16fb9b9` — VL805 BAR0 program +
  PCI command-register BME/MSE fix + reorder + xhci_init poll-retry
  around capProbe + diagnostic instrumentation across xhci_init.
- `phoenix-rtos-usb` master `2a35b16` — `debug()` milestone markers
  around hcd_init in `usb/usb.c` and `usb/hcd.c`.

What now reliably works on real Pi 4 (image `acf01e80`, label
`stage4-phase2-cmd-pre-mailbox`):
- pcie daemon programs VL805 BAR0 = `0xf8000004`.
- pcie daemon enables MSE+BME on VL805 BEFORE the firmware mailbox
  notify (correct order; reverse order leaves controller dead).
- xhci_init: capProbe succeeds within a couple of retry-loop
  iterations once BAR is programmed; reset succeeds; validateRuntime
  passes with sane caps (5 ports, 32 slots, 4 interrupters, AC64,
  32-byte contexts).

What's still failing:
- `xhci_allocCommandSpace` returns non-zero with no `fprintf(stderr,...)`
  reaching UART. Most likely: alignment check fails because Phoenix's
  `usb_allocAligned` aligns the virtual address but the `va2pa`
  physical address doesn't satisfy `XHCI_DCBAA_ALIGN`. Image
  `df2b1cf6` (built but not yet test-cycled — Pi cycle was
  stopped to commit progress) adds debug() prints inside each branch
  of allocCommandSpace plus a value-dump of the va/phys pairs after
  va2pa. Next cycle will identify which branch.

Stage 4 phase 1 (HDMI text console) remains fully validated.
Diagnostic `debug()` instrumentation is intentionally still present
in xhci.c, hcd.c, usb.c, pcie.c, usbkbd.c — to be removed once
phase 2 closes (per AGENTS.md).

## Strategic trajectory (2026-05-04)

The user named four crucial milestones for a working Pi 4 system:
caches enabled, 4 GiB DRAM unlocked with correct GPU memory partitioning,
SMP across all four Cortex-A72 cores, and HDMI text console + USB
keyboard input. Dependency analysis put the four into a stage order:

| Stage | Goal | Prereq |
|---|---|---|
| 1 | Caches via Linux `__enable_mmu` shape | none (architectural refactor) |
| 2 | 4 GiB DRAM + GPU memory partition (TD-15 phases 2-6) | Stage 1 (fast iteration + meaningful DMA cacheability) |
| 3 | SMP cores 1-3 | Stage 1 (LDXR/STXR exclusive monitor across cores requires IS-shareable cacheable memory) |
| 4 | HDMI text console + USB keyboard input | Stages 1-3 |

Single source of truth: **`docs/roadmap-cache-ram-smp.md`**.
Active step: Stage 1, see `tracking/current-step.md`.

The 5-iteration cache-enable investigation captured 2026-05-04 is
conclusive: every late post-MMU cache enable produces a walk-time
translation fault on real Pi 4 (most plausibly because A72
speculatively populates D-cache lines while D-cache is "off" and
those lines persist past `dc civac`). The architecturally-correct
fix is to restructure boot so that *all* page-table and syspage
writes happen with the MMU off, then a single atomic
`SCTLR_EL1.M | C | I` flip, then no memory writes until `b main`.

## Previous Current Status: 2026-05-04

**TD-16 page-table visibility step passed on QEMU and real Pi 4.** Kernel
`5e727dcc` restores the early `_inval_dcache_range` over
`PMAP_COMMON_KERNEL_TTL2 .. PMAP_COMMON_STACK` before the first
`SCTLR_EL1.M` write. This follows the Linux/FreeBSD early-MMU shape for page
tables populated with the MMU off. It still does not enable I-cache or
D-cache, so the Pi remains slow; the TD-16 loop stayed at
`dt≈0x883e**`.

Validation:
- Rebuild/export image SHA256:
  `0f6dc1a9e8254d9c42f41d6ee308eff074a9a6a2e0810cc1fa25044d9c260115`.
- QEMU Pi 4 smoke reaches `(psh)% help`.
- Generic AArch64 QEMU smoke reaches `(psh)% help`.
- Real Pi 4 netboot reaches `(psh)%`; log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-221342-netboot-td16-early-pt-inval.log`.

Warnings observed:
- Build/export: no compiler, linker, DTB, packaging, or image verification
  warnings; helper reported verification OK.
- Real Pi firmware/netboot: expected SD/USB boot misses before network
  fallback, missing per-MAC TFTP files before root `config.txt`, missing
  `cmdline.txt`, and HDMI1 EDID/DSI messages while HDMI0 is active.
- UART helper selected `picocom` for this run and printed
  `STDIN is not a TTY`; capture still completed and the log is valid.

Next TD-16 step: with aliases narrowed and early page-table invalidation
restored, compare the exact remaining `SCTLR_EL1` transition against Linux
`__cpu_setup` / `__enable_mmu`. The next implementation attempt should either
enable `M|C|I` together in the early transition with fault-register capture, or
first add a no-call early exception dump if the current handler is still too
fragile to diagnose cache-enable faults.

Follow-up caution: a no-call early exception-dump rewrite was tried after this
step and rejected. It passed QEMU but the real Pi run timed out at
`psh: readcmd` without reaching `(psh)%`; source was reverted and no kernel
commit was made. Do not retry that diagnostic shape without first proving it
with a controlled QEMU exception or gdbstub-backed fault capture.

## Previous Current Status: 2026-05-03 night

**TD-16 alias cleanup progressed without regressing boot.** Kernel
`d52f6c3a` drops the temporary TTBR0 low identity map immediately after the
syspage copy and its post-copy cache maintenance, and removes the obsolete
E2 source/destination syspage byte-dump probe block. The system still runs
cache-disabled, so this is not a speed fix yet; it is a prerequisite for a
safe later `SCTLR_EL1.M | C | I` transition.

Validation:
- Rebuild/export image SHA256:
  `c82fa3be79c9a13f35c72a8717e97adfb6d5d7cb719ea31ebb1c7586bdae15b9`.
- QEMU Pi 4 smoke reaches `(psh)% help`.
- Generic AArch64 QEMU smoke reaches `(psh)% help`. First generic smoke
  timed out once after the log had already reached `psh: tty open`; an
  immediate rerun passed. Treat this as a timing warning, not a functional
  regression, unless it repeats.
- Real Pi 4 netboot reaches `(psh)%`; log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-214816-netboot-td16-early-ttbr0-drop.log`.

Warnings observed:
- Build/export: no compiler, linker, DTB, packaging, or image verification
  warnings; helper reported verification OK.
- Real Pi firmware: expected netboot-path messages only (`sdcard` misses
  before network fallback, missing `cmdline.txt`, HDMI1 EDID/DSI messages
  while HDMI0 is active). No new Phoenix runtime fault.

Next TD-16 step: audit the remaining bootstrap alias boundary and page-table
maintenance before attempting caches again. The target remains a
Linux/FreeBSD-shaped early MMU transition that enables M, C, and I together,
not another late I-cache-only placement.

## Previous Current Status: 2026-05-02 night

**TD-14 now reaches the real Pi 4 UART shell prompt.** The latest tested
netboot image reaches `psh: readcmd` and prints `(psh)%` on UART. The
remaining near-term work is cleanup and interactive command validation, not
another early-boot wall.

Landed sibling commits:
- kernel `60703368` — fixes relative `proc_portLookup("devfs")` payload
  slicing, adds a direct stored OID for the well-known `devfs` namespace,
  and keeps a bounded TD-14 `proc_send("devfs")` timing probe.
- devices `63f1d438` — PL011 tty now supports minimal char-device
  stat/attr messages and registers a direct `/dev/console` namespace alias
  while TD-14 bind/devfs lookup latency is unresolved.
- devices `3ee4702` — `TIOCSPGRP` now stores the requested foreground
  process-group ID directly instead of treating it as a PID and calling
  `getpgid()` inside the tty server.
- libphoenix `3c76bba` — temporary `/dev/console` open tracing and a narrow
  TD-14 fast path that avoids a second full `resolve_path()` walk.
- utils `da2f541` — psh early TD-14 probes use the `debug()` syscall, not
  inherited stdio that can block before `/dev/console` is open.

Validation:
- QEMU Pi 4 smoke reaches `(psh)% help`.
- Real Pi netboot image SHA256:
  `d219efa27dd617ea171465f601742427ca1c96f3d505fb3979a1c7a27d0c520e`.
- Real Pi UART log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260502-220314-netboot-td14-readcmd-long.log`.

Key hardware evidence:

```text
open: console sys_open done
psh: tty isatty
psh: tty isatty done
psh: tty ready
psh: tcsetpgrp
psh: tcsetpgrp done
psh: readcmd
(psh)%
```

The previous run without the direct `devfs` OID showed `proc_send("devfs")`
round trips varying from ~1 ms to ~43 s while root dummyfs returned
`-ENOENT`. That proved the repeated root-query path was both wrong and
expensive on Pi 4. The direct OID removes that wall. The later shell path
then exposed two issues: `TIOCSPGRP` used the wrong semantic argument in
libtty, and duplicated `/dev/console` canonicalization made startup extremely
slow under the debug-heavy image.

Next step: strip or gate the remaining TD-04/TD-14 boot probes enough to
reduce UART backlog, then run an interactive UART smoke (`help`, `ps`, `ls
/dev`, `dmesg`) on the real Pi. Keep the direct `devfs` and console-alias
workarounds documented until the canonical namespace path is fast and stable.

## 2026-05-03 TD-16 cache investigation update

The current bottleneck is confirmed as cache-disabled execution, not CPU
clocking: firmware reports ARM at 1.5 GHz and `CNTFRQ_EL0` is 54 MHz, but
the kernel's pre-cache TD-16 nop loop takes `dt≈0x089xxxxx`.

Two late I-cache-only placements were tested and rejected:

- End of `_hal_init_c()` made the second TD-16 loop fast
  (`td16b:dt=0x126ee`) but real Pi hung before `_usrv_init()` returned.
- `_hal_start()` avoided that earlier boundary but hung immediately after
  `main_initthr: enter`.

Therefore no SCTLR cache-enable path is currently active. The only code
change kept is kernel `1a4eb297`, a safer
`hal_cpuInvalDataCacheAll()` implementation that
uses CLIDR/CCSIDR correctly, clean+invalidates by set/way, and adds the
required barriers. Next TD-16 work should remove mixed cacheable/NC aliases
in the bootstrap mappings before retrying D-cache enable.

External OS comparison now makes the target sequence clear: Linux arm64,
FreeBSD arm64, and Circle all turn caches on as part of the early MMU/memory
map transition, not as a late C-level speed knob. Phoenix should converge on
the same model: alias-safe bootstrap mappings, page-table cache maintenance,
then `SCTLR_EL1.M | C | I` together before normal kernel execution.

First alias-cleanup step landed in kernel `7f7684c4`: temporary TTBR0 RAM
identity block descriptors now use Normal Non-Cacheable attributes instead
of Normal cacheable. This does not enable caches and does not make the boot
faster yet, but it removes one direct conflict with the existing NC
`_hal_syspageCopied` and `PMAP_COMMON_STACK` TTBR1 mappings.

Validation:
- Rebuild/export image SHA256:
  `f6e77484512867c68f880923687342ec510469b61b59d09d4fb22be935a9795c`.
- QEMU Pi 4 smoke reaches `(psh)% help`.
- Generic AArch64 QEMU smoke reaches `(psh)% help`.
- Real Pi 4 netboot reaches `(psh)%`; log:
  `artifacts/rpi4b-uart/rpi4b-uart-20260503-213203-netboot-td16-ttbr0-nc-blocks.log`.

## Current Status: 2026-05-02 late-evening

**TD-13 closed. TD-14 narrowed to: kernel `proc_portLookup` IPC is
materially slower on Pi 4 silicon than QEMU.** Three workarounds
landed and are validated by QEMU smoke; on real hardware they give
incremental progress but do not yet produce a `(psh)%` prompt
within reasonable capture windows.

Strategy A (probe-strip) — landed:
- libphoenix `master` @ `43e050d` — strip TD-13/TD-14 debug() trace
  probes (resolve_path, _readlink_abs, safe_lookup, open, crt0,
  _libc_init); also reverts the TD-14-stat-skip workaround.
- devices `master` @ `3a3ee35` — strip TD13_DBG macro + all calls,
  drop poolthr `debug("poolthr enter")`.
- utils `master` @ `ff9fd9d` — revert psh probe commit.
- Effect on QEMU: `(psh)%` reaches at log line 264 (was 454+ with
  probes — boot is materially faster end-to-end).

Strategy B (ttyopen non-fatal) — landed:
- utils `master` @ `b25b0f8` — `psh_run()` continues with inherited
  STDIN/STDOUT/STDERR (kernel klog port) when the
  `psh_ttyopen("/dev/console")` retry budget exhausts. Logs a
  warning and proceeds. Restores fatal path once IPC is fast.

Pi 4 reality with both A and B applied (600 s capture window:
`artifacts/rpi4b-uart/rpi4b-uart-20260502-202654-netboot-td14-long-600.log`):

```text
main: spawned dummyfs-root (2)
main: spawned dummyfs (3)
main: spawned pl011-tty (4)
main: spawned mkdir (5)
main: spawned bind (7)            [pid 6 was unused / interleaved]
main: spawned pcie (8)
main: spawned usb (9)
main: spawned psh (10)
main: spawn loop done, entering proc_reap idle
threads: psh user scheduled
[≈ 12 minutes elapsed]
pl011-tty: started
pl011-tty: register tty0
pl011-tty: tty0 lookup
pl011-tty: tty0 lookup retry      [×2 — at i=0 and i=9]
name: devfs root query             [×15]
[no "tty0 lookup ok", no "console ready", no "psh: root ready"]
[no "(psh)%"]
```

Interpretation: in 12 minutes of Phoenix runtime, pl011-tty's
`createTty0` retry loop made fewer than ~10 iterations. That's
~60 s per `lookup("devfs")` round-trip on Pi 4 vs <1 ms on QEMU.
psh likewise stuck in its `while (lookup("/", ...) < 0)` loop. The
underlying issue is the kernel's `proc_portLookup` → `proc_send` to
the root server is slow on real silicon.

QEMU still reaches `(psh)% help` interactively in every smoke run.
Image SHA256 (post-A-B): `ff79d79d4ab5b6e4d407eda2ea6dcae256dc55fea0333970d31c634e510fc5df`.

What's still pending (next session):

- **Strategy C** — Replace busy-poll `lookup()` retry loops with a
  kernel-side name-ready notification primitive (condvar that fires
  when dcache acquires the requested name). Removes both the
  `usleep` jitter and the per-retry IPC cost.
- **Strategy D** — Root-cause why each `proc_send` round-trip is so
  slow on Pi 4. Plausible candidates:
  - VideoCore VI / GPU mailbox traffic interfering with kernel
    memory (same class as TD-04 syspage corruption).
  - Slow timer/interrupt latency on the boot core (TD-11
    single-core spinlocks may serialize too much).
  - dummyfs-root's poolthr scheduling latency under contention.

Latest verified images:

- Integration manifest: `manifests/2026-05-02-td14-strategy-ab-checkpoint.md`
- Kernel: `agent/rpi4-program-reloc` @ `37fcc58e` (unchanged from TD-13)
- Devices: `master` @ `3a3ee35`
- Utils: `master` @ `b25b0f8`
- Libphoenix: `master` @ `43e050d`
- Image SHA256: `ff79d79d4ab5b6e4d407eda2ea6dcae256dc55fea0333970d31c634e510fc5df`

Reference logs:
- 600 s no-prompt: `artifacts/rpi4b-uart/rpi4b-uart-20260502-202654-netboot-td14-long-600.log`
- 240 s no-prompt: `artifacts/rpi4b-uart/rpi4b-uart-20260502-202114-netboot-td14-ttyopen-nonfatal.log`
- 240 s post-strip baseline: `artifacts/rpi4b-uart/rpi4b-uart-20260502-201349-netboot-td14-stripped-probes.log`

## Previous Status: 2026-05-02 evening

**TD-13 closed. TD-14 reframed: not a single hang point but a
constellation of TD-04-class IPC fragility on Pi 4.** Each cycle the
wall moves to a different point along the pl011-tty / psh path:
sometimes psh's `resolve_path("/dev/console")` takes 100+ s but
completes; sometimes pl011-tty's `lookup("devfs")` retry loop iterates
~10 times then hits capture cutoff; sometimes pl011-tty hangs even
earlier between `pl011_configure done` and the next bare
`pl011_writeRaw`. Same kernel image, same namespace logic — the
silicon-only difference is materially slower IPC, occasionally
indefinite stalls.

QEMU still reaches `(psh)% help` interactively in every smoke run.

Latest verified images:

- Integration manifest: `manifests/2026-05-02-td14-tty0-nonfatal-checkpoint.md`
- devices `master` @ `8b80f4c` — TD-14-tty0-nonfatal (createTty0 non-fatal,
  retries 50→30 so we fall through to create_dev's portRegister fallback)
- utils `master` @ `0cafa08` — TD-14-psh-retry (PSH_TTYOPEN_RETRIES 20→200)
- libphoenix `master` @ `47034f8` — TD-14 resolve_path trace + oid port/id
- kernel `agent/rpi4-program-reloc` @ `37fcc58e` — TD-13 fixes (unchanged)
- QEMU image SHA256: `1124cb2876d3ce0d09dd5ec3645450c13fbbdb83a244ae90c316e0a8cc1e3a5f`

Reference logs:
- Worked once on Pi 4 (resolve completes, `oid port=3 id=2` printed,
  `abspath_ok` reached): `artifacts/rpi4b-uart/rpi4b-uart-20260501-225856-netboot-td14-oid-trace2.log`
- Most recent Pi 4 run hung early in pl011-tty:
  `artifacts/rpi4b-uart/rpi4b-uart-20260502-195556-netboot-td14-tty0-nonfatal-clean.log`

Things that did NOT work (recorded so we don't try them again):
- Reordering the syspage list to put pl011-tty after `bind devfs /dev`
  → bind caches /dev state at mount time and never refreshes; lookups
  for /dev/console miss for every later consumer. Reverted.
- A 2 s `usleep` at the top of pl011-tty `main()` to let dummyfs settle
  → same QEMU breakage as the reorder. Reverted.
- Adding raw-byte register/lookup name traces in kernel `proc/name.c`
  → broke QEMU (probe in `name_traceRegister` re-entered through a held
  spinlock). Stashed in the kernel repo, not part of HEAD.

## Previous Status: 2026-05-02 (morning)

**TD-13 closed. New active blocker is TD-14 (`/dev/console` open hang in
`resolve_path`).** On real Pi 4 the kernel + libphoenix + psh now run cleanly
through `main: spawned psh (10)` → `threads: psh user scheduled` → libc init
→ psh `main` → `pshapp: tty loop enter` → `pshapp: ttyopen attempt` →
`open: console enter` → `open: console stat skipped` →
`open: console resolve enter`, then the UART falls silent. The wall is
inside libphoenix `resolve_path("/dev/console", NULL, 1, 1)`, which makes
sys_lookup IPC round-trips to the namespace servers (bind / devfs / pl011-
tty). One of those round-trips hangs on hardware. QEMU still reaches
`(psh)% help`, so the path works in software.

Reference log (morning):
`artifacts/rpi4b-uart/rpi4b-uart-20260501-220933-netboot-console-open-skip-stat.log`

## Previous Status: 2026-05-01

**Previous blocker**: TD-13 `proc_mutexCreate` hang was fixed by avoiding
exclusive-access atomics on the current single-core AArch64 target. The noisy
TD-13 syscall/mutex/EL0 probes have now been removed and
`syscalls_phMutexCreate()` again validates both user pointers with
`vm_mapBelongs()`. The real Pi still does not show a clean shell prompt; the
latest hardware log reaches `threads: psh user scheduled`, so the next
boundary is post-`psh` startup, console/stdin/stdout, devfs/tty open, or a
later syscall.

Latest verified image:

- Integration manifest: `manifests/2026-05-01-td13-clean-probes.md`
- Kernel: `agent/rpi4-program-reloc` @ `37fcc58e` (TD-13 probe cleanup plus
  restored `phMutexCreate` validation on top of single-core AArch64 atomic
  fallback)
- Devices: `master` @ `8984455` (TD-13 pl011-tty progress markers)
- UART log: `artifacts/rpi4b-uart/rpi4b-uart-20260501-214225-netboot-td13-clean-probes.log`
- QEMU smoke: still reaches `(psh)% help` interactively.
- Image SHA256: `03e1988da8390512df2737d8efaa9b994725cd9873e465f318910af5e1ea6f0d`

Real-device boundary on `td13-clean-probes.log`:

```text
dummyfs-root              → main: spawned dummyfs-root (2)
dummyfs                   → dummyfs: root initialized
pl011-tty                 → pl011-tty: init: libtty_init ok / pl011_configure done
bind/devfs                → name: devfs cache hit
usb                       → threads: timer irq
psh                       → main: spawned psh (10)
post-spawn                → threads: psh user scheduled
[silence]
```

Key result:

- The added `a..f` probes proved the pre-fix wall was inside
  `resource_put(p, &mutex->resource)`, which is just
  `lib_atomicDecrement(&r->refs)`.
- `lib_atomicIncrement/Decrement` now use a DAIF-masked plain update only for
  `defined(__aarch64__) && NUM_CPUS == 1`, matching the already validated
  single-core spinlock strategy. Multicore AArch64 and other architectures
  keep the existing `__atomic_*` builtins.
- With that fix, real hardware progresses through `M12abcdef3K`, initializes
  `dummyfs` and `pl011-tty`, spawns through `psh`, and schedules `psh`.
- Cleanup validation on 2026-05-01 removed `sNN`, `M123EK`, `a..f`, `*15`,
  and `>` probes and restored `vm_mapBelongs()` in `phMutexCreate`; QEMU still
  reaches `(psh)% help`, and real Pi still reaches `threads: psh user scheduled`.

Next target:

- Instrument the next smallest post-`psh` boundary with readable console logs:
  `psh` startup, fd/devfs lookup, tty open, and first blocking read/write.
  Avoid broad single-byte probes unless GDB/QEMU or normal console logs cannot
  answer the question.

Tool/process warnings observed in this session:

- One first netboot attempt used too short a `--capture-secs=35` window and
  ended before firmware reached DHCP; use 100+ seconds for netboot captures.
- After a day of unplug/replug, the first DHCP attempt failed and bridge
  recovery restarted the Lima VM/socket_vmnet path. A subsequent cold cycle
  DHCPed successfully.
- The latest real run emitted many firmware `xHC-CMD err: 19/36 type: 11`
  lines while probing USB before falling through to network boot. This happens
  before Phoenix loads and did not block netboot, but it should not be ignored
  if USB boot/probing behavior becomes relevant.
- The capture helper used `picocom` and ended with watchdog `SIGTERM`
  (`exit 143`), expected for timed captures.
- On the latest run, the first DHCP attempt again required the documented
  Lima/socket_vmnet bridge recovery. After recovery, DHCP/TFTP/netboot worked.

## Previous Status: 2026-04-30

**Previous blocker**: Pi 4 now reaches user-space process creation and spawns
all syspage programs through `psh`, then stays silent before a shell prompt.

Latest verified image:

- SHA256: `3dc62d31c1469955ee462f7a0279cc4f570e7fcb57d71fc50ceb2686e1aec447`
- UART log: `artifacts/rpi4b-uart/rpi4b-uart-20260430-214456-netboot-spawn-names.log`
- QEMU smoke: `./scripts/qemu-shell-smoke.sh rpi4b` reaches `psh help`

Latest real-device boundary:

```text
main: spawned dummyfs-root (2)
main: spawned dummyfs (3)
main: spawned pl011-tty (4)
main: spawned mkdir (5)
main: spawned bind (6)
main: spawned pcie (7)
main: spawned usb (8)
main: spawned psh (9)
```

The latest hardware log contains no `Exception`, no `SError`, and no
instruction abort. This closes the previous `_hal_init`, scheduler entry, and
SError-flood blockers.

Validated fixes in this step:

- AArch64 single-core spinlocks use DAIF save/IRQ-FIQ mask/restore instead of
  exclusive byte atomics when `NUM_CPUS == 1`.
- SError remains masked in synthetic thread contexts, syscall/exception C
  dispatch, IRQ dispatch, and direct `hal_jmp()` userspace entry.
- `main()` enters the first scheduled context before enabling timer IRQs in the
  bootstrap context.

Next target:

- Diagnose post-spawn user-service execution and console/TTY handoff. Confirm
  whether `psh` is scheduled on hardware, whether `pl011-tty` registers the
  expected device, and whether shell output is blocked waiting on `/dev`.

Memory/GPU note:

- The target board is physically 4GB, but current firmware/boot config reports
  `MEM GPU: 76 ARM: 948 TOTAL: 1024`; PLO also clamps usable DDR to about
  948MiB. This lowers immediate GPU-overlap risk but is temporary.
- Final memory support must derive usable RAM and reserved regions from the
  firmware-mutated DTB (`/memory`, `/reserved-memory`, `/memreserve/`,
  `dma-ranges`) instead of hardcoding either 1GB or 4GB.

Tool/process warnings observed:

- Firmware logs still show expected missing `recover4.elf`/`recovery.elf`,
  HDMI1 EDID failures, `DISPLAY_DSI_PORT` warnings, and missing `cmdline.txt`.
  HDMI1 EDID is safe because HDMI0 is used; the others are firmware/boot-media
  noise unless correlated with a Phoenix failure.
- One test attempt failed before boot because `picocom` could not lock
  `/dev/cu.usbserial-201310`. `test-cycle-netboot.sh` now aborts if UART
  capture exits before Pi power-on.
- A VM restart removed `/tmp/rpi4b-dtb`; rebuild restored it by copying the
  official final-form Raspberry Pi firmware DTB without dtc decompile lint.

## Previous Status: 2026-04-19

**🎉 MAJOR MILESTONE: Map Relocation Completed!**

The Raspberry Pi 4 port has achieved a massive breakthrough! The system now successfully completes all map relocation in syspage initialization and reaches the program relocation phase.

### Current Boot Progress

**Boot Stage**: Program Relocation Entry ✅

**Last Working Markers**: `NYOPSTUZbcdeFGVWXabcdefgmklmno`
- `N`: MMU enable preparation
- `Y`: MMU enable complete  
- `O`: Entered virtual memory code
- `P`: Syspage copy setup complete
- `S`: Vector table setup
- `T`: TTBR0 setup
- `U`: Stack setup
- `Z`: About to enter main()
- `b`: About to branch to main()
- `c`: Main function entry
- `d`: Main function executing
- `e`: Before syspage_init()
- `F`: syspage_init() entry
- `G`: After hal_syspageAddr() call
- `V`: Syspage pointer is valid
- `W`: About to access syspage->maps
- `X`: syspage->maps is not NULL
- `a`: After maps relocation
- `b`: In map loop
- `c`, `d`, `e`: Map field relocations
- `f`: Entries not NULL
- `g`: After entries relocation
- `m`: Skipping entry relocation (workaround)
- `k`: Before map next
- `l`: After map next
- `m`: End of map loop
- `n`: End of map relocation section
- `o`: Starting program relocation ✅ **NEW MILESTONE!**

### Recent Achievements

#### 🎉 Fixed Syspage Access Crash (2026-04-19)
**Problem**: Kernel crashed in syspage_init() when accessing syspage->maps after MMU enable
**Root Cause**: Syspage was copied to BSS region, but BSS was not mapped in MMU page tables
**Solution**: Temporary fix to skip syspage copy and use original syspage directly
**Result**: Kernel now progresses from syspage_init() crash to HAL initialization entry

#### 🎉 Fixed UART Corruption (2026-04-19)
**Problem**: Severe UART corruption after MMU enable prevented reliable debugging
**Root Cause**: Using physical UART addresses after MMU enable instead of virtual addresses
**Solution**: Replaced physical UART calls with virtual address macro
**Result**: Clean UART output throughout boot process

### Technical Details

**Current Image**: 
- SHA256: `bb7861c314ca675eeee1f98e7744df29c123efa0533f3d007bc0c49b5d469531`
- Date: 2026-04-19
- Commits: 10+ commits in phoenix-rtos-kernel with comprehensive debugging

**UART Log**: `artifacts/rpi4b-uart/rpi4b-uart-20260419-104437.log`

### What's Working

✅ **Early Boot Sequence**
- UART initialization
- CPU register setup (SCTLR_EL1, CPACR_EL1)
- Cache invalidation
- SMP enable for Cortex-A72
- MMU setup and enable
- Virtual memory transition
- System page access
- Vector table setup
- Stack setup

✅ **Memory Management**
- Physical to virtual address translation
- Early MMU page tables
- Kernel space mapping
- Syspage access (using original location)

✅ **Debug Infrastructure**
- Comprehensive debug markers throughout boot
- Virtual UART access after MMU enable
- Clean UART output throughout boot
- Strategic marker placement for issue isolation

✅ **Kernel Initialization**
- Main function entry and execution
- Syspage initialization (maps section complete)
- Map relocation (all map entries processed)
- Progress to program relocation phase

### Known Issues

⚠️ **Temporary Fixes**
- Syspage copy operation skipped (using original syspage directly)
- BSS region not properly mapped in MMU page tables
- Entry relocation skipped to avoid circular list issues
- Technical debt: need to restore proper syspage copy and entry relocation

⚠️ **Current Blocking Issue**
- System hangs at program relocation phase (marker `o`)
- Likely circular linked list issue similar to entry relocation
- Need to add debugging or implement workaround for program relocation

### Next Steps

1. **Immediate Priority**
   - Debug program relocation hang at marker `o`
   - Add strategic debug markers to identify exact failure point
   - Consider temporary workaround to skip program loop
   - Goal: Reach marker `Y` (end of syspage_init())

2. **Short Term Goals**
   - Complete kernel initialization sequence
   - Achieve console output and logging
   - Test basic device drivers
   - Reach user-space entry point

3. **Technical Debt Resolution**
   - Implement proper MMU mapping for BSS/data region
   - Restore syspage copy operation
   - Ensure all memory regions properly mapped

4. **Long Term Goals**
   - Full device driver support
   - Networking stack
   - Filesystem support
   - Multi-core SMP

### Progress Timeline

- **2026-04-19**: Fixed infinite loop in entry relocation, completed map relocation
- **2026-04-19**: Fixed syspage access crash, reached HAL initialization
- **2026-04-19**: Fixed UART corruption, reached kernel entry point
- **2026-04-18**: Inlined critical setup functions, progressed to NYOPSTUZb
- **2026-04-17**: Separated MMU/cache enable, progressed to NYO
- **2026-04-16**: Fixed CPACR_EL1 FPU setup, progressed to X3

### How to Help

**Testing**:
```bash
# Build and test the current image
cd /Users/witoldbolt/phoenix-rpi
./scripts/rebuild-rpi4b-fast.sh
./scripts/capture-rpi4b-uart.sh

# Analyze results
python3 scripts/summarize-rpi4b-uart-log.py artifacts/rpi4b-uart/latest.log
```

**Development Focus**:
- Program relocation debugging and completion
- Syspage initialization finalization
- Reaching HAL initialization entry point
- Proper BSS region MMU mapping

### Risks and Challenges

- **Cortex-A72 Specific Issues**: Memory ordering, cache coherence
- **MMU Configuration**: Page table setup, memory attributes
- **Memory Mapping**: BSS/data region mapping needed
- **Technical Debt**: Temporary fixes need proper solutions

### Success Criteria

✅ **Achieved Milestones**:
- Clean UART output after MMU enable
- Reach kernel entry point
- Syspage access working
- Progress to HAL initialization
- Reliable debug markers throughout boot

🔄 **Next Targets**:
- HAL initialization completion
- Console output and logging
- Device driver initialization
- User-space entry

**Status**: Active development, major progress, on track for first complete boot!

*Last Updated: 2026-04-19*
