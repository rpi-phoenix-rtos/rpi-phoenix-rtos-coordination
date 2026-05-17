# Temporary Fixes and Future Cleanup

This document is the registry of transitional shortcuts and workarounds
accepted during the Raspberry Pi 4 bring-up. Each item has a stable ID
(`TD-NN`) used to link from source code, commits, and future cleanup steps.

Ordering rule: once the Pi 4 boots to a usable state, every item here becomes
mandatory cleanup. Until then, progress on the boot path takes priority.

## Conventions

- **IDs are stable.** Never re-number. If an item is merged into another,
  add a `merged into TD-NN` note rather than deleting the entry.
- **Status** is one of:
  - `PENDING` — shortcut still active in source
  - `IN-PROGRESS` — cleanup step open against it
  - `RESOLVED` — cleanup committed and validated, record kept as history
  - `STALE` — doc entry described state that has since changed; entry
    superseded but kept for historical context
- **Linking from source.** Every transitional fix in upstream source should
  carry an inline marker: `TODO(TD-NN): <short hint>`. Grep for `TD-NN` to
  find all sites of a given shortcut.
- **Location snapshots may drift.** Line numbers in this file reflect state
  at the time the entry was written. Re-verify against current source before
  acting — the code changes faster than this doc.

## 2026-05-17 audit summary

A multi-month MMU+cache crash at `PC=0x400498` (EX=4, ESR=0x02000000) was
**resolved** by two armstub-level bug fixes:
1. The A72 erratum 1319367 workaround was being applied to an undefined
   sysreg (`S3_1_C15_C2_2`); moved to the canonical `CPUACTLR_EL1[46]`.
2. `L2CTLR_EL1 |= 0x22` (Data RAM Latency=3, Setup=1) was missing — every
   other working Pi 4 bare-metal stack programs this for BCM2711 silicon.

Both fixes are in `phoenix-rtos-project` commit `dde9bb5`. With these,
caches and MMU are operational, kernel boots through `_init.S` cleanly,
and user-space reaches the psh shell. Several TD items that were workarounds
for the cache-off era are now **STALE** or **RESOLVED**. See per-item notes.

---

## TD-01: SMP enable disabled on Cortex-A72

- **Status:** SPLIT — SMPEN at EL3 is RESOLVED via armstub; multi-core
  kernel bring-up is still PENDING.
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` line 361 has
  the original kernel-side SMPEN+CPUACTLR block commented out (just markers
  remain). The note at lines 362–377 explains the rationale: CPUACTLR /
  CPUECTLR trap from EL1 on A72, so they must be written at EL3.
- **EL3 SMPEN (RESOLVED 2026-05-17):** The Phoenix armstub
  (`_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`) sets
  CPUECTLR_EL1.SMPEN at EL3 before `eret` to EL2. Confirmed working
  end-to-end across multiple cold boots.
- **Multi-core kernel (PENDING):** Cores 1–3 are woken from the spin-table
  at PA 0xe0/0xe8/0xf0 only by the SMP-smoke probe in plo's
  `hal_smpBringupSecondaries`; they print one marker and park in WFE. The
  kernel does NOT use them — there's no per-CPU scheduler state, no IPI
  routing through the GIC, and no kernel-side wake/release protocol.
- **Risk accepted:** all kernel work runs on core 0. This may also be the
  dominant cause of the multi-minute "boot to psh prompt" slowness observed
  on real hardware — every IPC round-trip is serialized through one CPU.
- **Resolution requirements:**
  - Per-CPU scheduler state (run queues, current-thread, exception stacks)
  - IPI handlers wired through GICv2
  - Coordinated TLB+cache flush (broadcast invalidate already used)
  - Wake/release protocol for cores 1–3 (write a non-zero target into
    the armstub spin-table, sev)
  - Kernel awareness of CPU count from DTB `/cpus` node

## TD-02: Pre-MMU cache invalidation disabled

- **Status:** RESOLVED 2026-05-17.
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` line 563
  (`bl _inval_dcache_range` over the page-table region) and line 351 (the
  first `bl hal_cpuInvalDataCacheAll` at `_start`).
- **What changed:** Both cache-maintenance call sites are now LIVE in
  source, not commented out. The Phase Z work (kernel-side, 2026-05-17)
  also added a second defensive `bl hal_cpuInvalDataCacheAll` immediately
  before the M|C|I SCTLR write. With caches operational (armstub fix), the
  pre-MMU invalidation is no longer a source of hangs.
- **Doc was stale on this** — entry described "commented out" but the call
  site has been live since the Phase Z2 work.

## TD-03: Syspage copy / BSS mapping shortcut

- **Status:** STALE — needs re-audit against current `_init.S`.
- **Original concern:** the early MMU page tables didn't cover the BSS
  region where the syspage was being copied; access to the copied virtual
  syspage was unreliable.
- **What changed:** The Phase Z3 work (2026-05-17) deleted the TD-04 NC
  override on `_hal_syspageCopied`. The kernel now reads the syspage via
  the normal cacheable mapping after the M|C|I flip; with armstub fixes
  in place, this works correctly. Map coverage for BSS is no longer a
  workaround surface.
- **Action:** verify that the canonical syspage copy path is used
  end-to-end and remove the `_init.S` markers / probes that targeted the
  old broken path. Merge into TD-05 cleanup.

## TD-04: BCM2711-specific syspage corruption at the plo→kernel handoff

- **Status:** ROOT CAUSE IDENTIFIED 2026-05-17 — NC workaround REMOVED;
  underlying defect was in the armstub, not in the syspage path.
- **Original analysis (2026-04-29):** Real-Pi-4 silicon corrupted bytes
  at PA `_hal_syspageCopied + 0x310` per-boot-randomized; the cache-off
  plo's writes seemed to never reach the kernel's view of DDR.
- **Cache-layer workaround (now reverted, was in tree 2026-04-29 →
  2026-05-17):** Re-mapped `_hal_syspageCopied`'s page as Normal
  Non-Cacheable in TTBR1 TTL3 so kernel reads bypassed the A72 D-cache.
  Empirically masked the symptom.
- **Real root cause (2026-05-17):** Multi-agent investigation found two
  unrelated armstub-level bugs:
  - **1319367 erratum workaround on undefined sysreg.** The armstub
    wrote `CPUACTLR2_EL1[0]` with `CPUACTLR2_EL1` aliased to
    `S3_1_C15_C2_2` — not a documented A72 system register. ATF and
    Phoenix's own `docs/plans/a72-errata-sweep.md` document the canonical
    fix as `CPUACTLR_EL1[46] = 1`. The undefined-sysreg write either
    trapped silently or hit a reserved register, corrupting nearby
    state. With the fix, the A72 erratum is actually mitigated.
  - **Missing `L2CTLR_EL1 |= 0x22` BCM2711 RAM-timing setup.** The
    BCM2711's Cortex-A72 cluster L2 cache at 1.5 GHz requires Data RAM
    Latency=3 cycles + Setup=1 cycle (bits 1 and 5). The Cortex-A72 TRM
    default of 2 cycles is too tight for BCM2711 silicon; first cacheable
    D-side fill after `SCTLR.C=1` returned corrupt data. The canonical
    raspberrypi/tools armstub8, Trusted Firmware-A, and Circle all
    program this. Phoenix did not, until 2026-05-17.
  - **Both fixes land in** `phoenix-rtos-project` commit `dde9bb5`
    (`phoenix-armstub8-rpi4.S`). Two-boot reproducibility verified.
- **Where the NC workaround was (now removed):** `_init.S` TTL3 override
  block (Phase Z3 deleted it). The companion post-copy
  `_clean_inval_dcache_range` (Phase Z4 deleted) and the single-shot
  M|C|I (Phase Z2 added) all moved together.
- **Companion hacks status:**

### TD-04-hack-1: SKIP the program-relocation loop in `syspage_init()`

- **Status:** UNVERIFIED — needs re-test now that caches work.
- **Where:** `sources/phoenix-rtos-kernel/syspage.c` (no `TODO(TD-04-hack-1)`
  marker remains in current source — `grep` returns 0 matches; the skip
  may have been removed in a prior cleanup).
- **Action:** verify the prog-reloc loop is restored and the heisenbug
  hang is gone. Add a regression test that walks the progs list.

### TD-04-hack-2: localization probes inside `_hal_init()`

- **Status:** STILL ACTIVE (cleanup candidate).
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/hal.c:97–164` — H, 4,
  5, 6, F/S, r, D, s, E, 7, 8, 9, a, b, c, d, e markers via the TTBR1
  early-UART pointer.
- **Why kept:** Their "Heisenbug insurance" role was a side effect of the
  cache-off-era bug. With caches working, the markers are pure diagnostic
  and can be stripped.
- **Action:** strip or gate behind a debug flag; merge into TD-05.

### TD-04-hack-3: fake `dtbEnd = dtbStart + 0x10000`

- **Status:** STILL ACTIVE.
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/hal.c:142–154`.
- **Why kept:** `dtb->end` read hung the kernel on real Pi 4 in the
  cache-off era. With caches operational this may no longer reproduce.
- **Action:** **test removal in next cycle**. Replace
  `dtbEnd = dtbStart + 0x10000;` with `dtbEnd = dtb->end;`. If boot
  still reaches psh tty open, the heisenbug is gone and we can mark
  resolved. If it hangs, escalate.

## TD-05: UART debug-marker scaffolding

- **Status:** STILL PENDING (pervasive cleanup).
- **Where:** `hal/aarch64/_init.S`, `syspage.c`, `main.c`, `hal.c`, and
  related boot-path files.
- **What's there:** dozens of `uart_putc` and direct UART-pointer stores
  producing the `NYOPSTUZbcdeFGVWXabcdefgmklmno` progress trace, plus the
  Phase Z markers `X1`–`X5` (kernel SCTLR pre/post), plus the armstub
  `1`/`2`/`4`/`5`/`A`/`S0` markers.
- **Risk accepted:** noise + boot time + the markers themselves are part
  of why the boot output stream is so dense.
- **Resolution requirements:**
  - Replace ad hoc markers with a compile-time-gated debug macro
    (`RPI4_BOOT_MARKER(c)`).
  - Decide what minimum subset stays in tree for ongoing diagnostics
    (e.g., armstub `1/2/4/5` for "EL3 path complete", kernel `X1–X5` for
    "post-SCTLR flip").

## TD-06: DTB handling assumptions

- **Status:** PENDING — and now actively blocking Goal 3 (full 4 GB RAM).
- **Where:** `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c`.
- **What was done:** early parsing assumes a single `/memory@*` node with
  one `reg` tuple, no `/reserved-memory` handling beyond a static list, and
  fixed assumptions about address-cells / size-cells.
- **NEW (2026-05-17):** the Pi 4 4GB hardware reports two memory banks
  (low 948 MB at 0x00000000 and high 3008 MB at 0x40000000). Linux sees
  both. Our kernel sees only the low bank because either (a) the static
  on-disk DTB has only `memory@0` and the firmware-patched runtime DTB
  also adds `memory@40000000` but we're not receiving the patched version,
  or (b) our `dtb_parseMemory` doesn't handle a multi-bank tuple.
- **Action plan:**
  - Confirm what `/memory*` nodes exist in the runtime DTB by adding a
    one-shot debug dump in `_dtb_init`.
  - If the runtime DTB has the high bank but our parser misses it, fix the
    parser (likely needs to scan ALL `/memory@*` siblings, not just the
    first one).
  - If the runtime DTB has only the low bank, look at `config.txt`
    options or firmware version to expose the high bank.
- **Resolution requirements:**
  - Drive memory layout entirely from the actual DTB.
  - Validate required nodes at parse time and fail with a useful message.
  - Plan for Pi 4B variants (1/2/4/8 GB), Pi 5, and Pi CM4 carriers.

---

## TD-07: Update QEMU inside the phoenix-dev VM to a current stable

- **Status:** STALE — QEMU 10.2.2 is installed (verified 2026-05-17 by
  Agent #8 A53-baseline run); current enough for Pi 4 boot up to the
  point where physical hardware diverges.
- **Action:** mark RESOLVED unless a newer version is needed for a
  specific feature (e.g. raspi4b with full GIC / xHCI / GENET models).

## TD-08: Re-test boot under QEMU + gdbstub for in-flight introspection

- **Status:** PARTIALLY EXERCISED — Agent #8 ran the A53/zynqmp build
  under QEMU + gdb to capture SCTLR/TCR/MAIR/TTBR0/TTBR1/VBAR/SP register
  state at post-SCTLR-flip; current `_init.S` is now rpi4b-specific so a
  paired A53/A72 build no longer compiles from the same tree.
- **Action:** kept open for future Pi 4 specific QEMU work
  (`raspi4b` machine); update when the machine model in QEMU 11+ supports
  enough peripherals to validate post-cache boot in QEMU.

## TD-09: Replace en7 crossover cable with an unmanaged ethernet switch

- **Status:** PENDING — user has the switch but is missing its PSU.
- **Operational note:** `test-cycle-netboot.sh` has built-in bridge
  recovery (`netboot-bridge-recover.sh`) that handles the wedge. The
  separate `test-cycle-psh-interact.sh` does NOT — it uses `exec python3`
  so its EXIT trap can't fire, and it consistently wedges netboot. **Use
  only `test-cycle-netboot.sh` until that script is fixed (or the
  unmanaged switch is installed, which eliminates the trigger entirely).**

## TD-10: USB stack stall after VL805 BAR programming (NEW)

- **Status:** PENDING (new issue, post-armstub-fix discovery).
- **Where:** `sources/phoenix-rtos-devices/usb/xhci/bcm2711-pcie.c` and
  `xhci.c`. The usb daemon's xhci HC init enters
  `bcm2711_pcie_initVL805`, reads VL805's 6 BARs (last log line:
  `pcie: BAR5 raw=00000000`), then NO further debug output is emitted
  even in 300-second UART captures.
- **What works:** VL805 enumerates correctly under firmware (confirmed by
  Pi boot menu using a connected USB keyboard for SPACE/2 input). The xhci
  capability registers are readable from our diag-outbound mmap
  (`caplen=20 ver=0100 hcsparams1=05000420`). So the bus electrical path
  + bridge translation works.
- **What stalls:** Either `pcie_scanBus` iterating empty dev slots 1–31
  on the recursive bus, `cfgio.destroy` munmap, or libtty back-pressure
  silently dropping subsequent debug output.
- **Knock-on effect:** `usbkbd` never enumerates the HID keyboard; pl011-
  tty's `pl011_kbdthr` retries `open(/dev/kbd0)` every 500 ms forever.
- **Resolution requirements:**
  - Add `debug()` traces between each scanBus iteration and after
    cfgio.destroy to localize the silence point.
  - Replace the 200 ms VL805 firmware-settle `usleep(200000)` with a
    Vendor-ID polling loop (already noted as a TODO in the comment at
    `bcm2711-pcie.c:797`).

## TD-11: USB merge (pcie+xhci into one process) (NEW)

- **Status:** RESOLVED 2026-05-17 via `phoenix-rtos-devices` commit
  `b5cc6b0`.
- **What was done:** Folded BCM2711 PCIe bridge bring-up + VL805 BAR
  programming into the xhci library as `bcm2711_pcie_initVL805()`.
  Eliminated the cross-process bridge translation race that previously
  caused 0xdead-pattern reads. Standalone `pcie` daemon dropped from
  `user.plo.yaml` (commit `fb771c4`).
- **Reference:** see "BCM2711 PCIe bridge translation" in
  `docs/rpi4-os-development-guide.md`.

## TD-12: Boot speed (NEW)

- **Status:** PENDING — investigation open.
- **Observed:** with caches operational, kernel boot to `psh: tty open`
  takes >>90 s of wall time. The first `psh: readcmd` (start of the
  shell's main loop) does not appear in even 300 s captures. The HDMI
  prompt does eventually appear (user-confirmed).
- **Candidates** (in order of plausibility):
  - **No SMP** — every IPC round-trip serializes through core 0. Phoenix
    has many short message exchanges in the boot path (psh ↔ pl011-tty,
    usb daemon ↔ kernel, etc.). With one CPU + libtty cross-process
    serialization + fbcon mirror per byte, the cumulative cost is
    measured in minutes. TD-01 multi-core would directly attack this.
  - **fbcon mirror cost** — `pl011_thr` mirrors every UART TX byte to
    HDMI; each `drawChar` is 128 stores; each scroll is a 3 MB memmove
    over uncached framebuffer memory. Per-byte cost is ~10× UART cost.
  - **libtty buffer back-pressure** — multi-process writers backlog the
    libtty TX queue; `tcsetattr(TCSAFLUSH)` in `psh_readcmd` waits for
    the entire queue to drain before changing terminal mode.
  - **`pl011_kbdthr` 500 ms retry forever** — open(/dev/kbd0) fails
    until usbkbd attaches (which is blocked by TD-10).
  - **`usb_hostLookup` 1 s retry** — only matters across processes
    (usbkbd is in-process so doesn't apply here).
- **Diagnostic in flight:** added debug checkpoints `pre-calloc cmdhist`,
  `post-calloc cmdhist`, `pre-loop`, `readcmd-pre-malloc/tcsetattr/...`,
  `prompt-written` in `psh/pshapp/pshapp.c`. Next test cycle will tell us
  exactly which library call is slow.
- **Resolution requirements:**
  - Add wall-clock timestamps to UART capture (e.g. picocom + `ts` from
    moreutils or a small awk filter).
  - Run the checkpoint-instrumented psh and capture > 400 s.
  - If `tcsetattr` is slow, audit libtty's TCSAFLUSH path.
  - If checkpoints space out evenly, the system-wide IPC cost is the
    issue and TD-01 SMP becomes the high-leverage fix.

---

## Priority Ladder

**Blocks "first Pi 4 boots to a useful state" milestone:**
- TD-10 (USB stack stall blocks keyboard input)
- TD-12 (boot speed — slow prompt makes interactive use painful)

**Blocks effective debugging:**
- TD-09 (netboot loop reliability — `test-cycle-netboot.sh` self-heals
  but `test-cycle-psh-interact.sh` wedges; switch eliminates trigger)

**Blocks upstream-ready quality:**
- TD-05 (debug-marker strip/gate — many markers from cache-off era)
- TD-04-hack-2, TD-04-hack-3 (cache-off-era hacks, candidates for
  removal now)
- TD-01b (multi-core kernel SMP, also helps boot speed)

**Hardware completeness:**
- TD-06 (DTB robustness; specifically required for 4 GB RAM exposure —
  goal 3 of `/loop`)

**Historical / resolved:**
- TD-01a SMPEN at EL3 — RESOLVED via armstub
- TD-02 pre-MMU cache invalidation — STALE (doc was out of date; call
  site is live)
- TD-04 NC syspage workaround — superseded by armstub fix; NC override
  removed
- TD-07 QEMU — STALE (10.2.2 is current enough)
- TD-11 USB pcie+xhci merge — RESOLVED via `b5cc6b0`

## Tracking Checklist

| ID | Status | Blocker? |
| --- | --- | --- |
| TD-01a | RESOLVED | SMPEN at EL3 |
| TD-01b | PENDING | multi-core kernel work (likely affects TD-12) |
| TD-02 | RESOLVED (doc was stale) | n/a |
| TD-03 | STALE (re-audit) | upstream quality |
| TD-04 | RESOLVED via armstub | n/a |
| TD-04-hack-1 | UNVERIFIED | cleanup |
| TD-04-hack-2 | STILL ACTIVE | cleanup (low risk to remove) |
| TD-04-hack-3 | STILL ACTIVE | cleanup (likely safe to remove) |
| TD-05 | PENDING | upstream quality |
| TD-06 | PENDING | full RAM (4 GB exposure) |
| TD-07 | STALE | n/a |
| TD-08 | PARTIALLY EXERCISED | future debugging |
| TD-09 | PENDING | netboot reliability |
| TD-10 | PENDING (NEW) | USB enumeration / keyboard |
| TD-11 | RESOLVED (NEW) | n/a |
| TD-12 | PENDING (NEW) | boot speed |

When resolving an item:

1. Create a `tracking/current-step.md` scoped to that single ID.
2. Remove the corresponding `TODO(TD-NN):` marker(s) from upstream source.
3. Commit the upstream repo change and snapshot an integration manifest.
4. Flip the status to `RESOLVED` in this file with the commit SHA and date.
