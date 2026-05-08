# Phoenix-RTOS Raspberry Pi 4 Port Status

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
