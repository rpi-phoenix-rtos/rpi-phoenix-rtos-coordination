# Knowledge Status — Pi 4 First-Boot Readiness

Written 2026-04-23. Consolidates source-code truth (Phoenix) with reference-OS
evidence (Circle, rust-raspberrypi-os-tutorials, rpi4-osdev, rpi4-bare-metal)
to confirm we have what we need to close TD-04 and reach first boot. Flags
gaps explicitly. Refresh before each phase boundary; facts decay with the
tree.

Authoritative companions:

- `docs/inprogress/status.md` — current boot markers, active blocker.
- `docs/done/roadmap-first-boot.md` — phase plan.
- `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` — TD-01..TD-06.
- `docs/knowledge/raspberry-pi-4.md` — board constants.

## 1. Confirmed Phoenix boot path (source-level)

All line numbers relative to
`sources/phoenix-rtos-kernel/` unless noted.

### 1.1 `_init.S` marker legend (single source of truth)

| Marker | Line | Event                                                |
| ------ | ---- | ---------------------------------------------------- |
| `Z`    | 174  | Debug: EL check                                      |
| `K`    | 178  | Entry; interrupts masked                             |
| `L`    | 224  | FPU/SIMD + perf monitors done                        |
| `M`    | 246  | Pre-MMU, start of setup                              |
| `X1`   | 250  | TCR_EL1 programming begins                           |
| `X2`   | 291  | TTL1 mapped (syspage + kernel + PL011)               |
| `X3`   | 323  | MMU enabled, I-cache invalidated                     |
| `N`    | 415  | First post-MMU virtual-address instruction           |
| `Y`    | 419  | About to jump into virtual code                      |
| `O`    | 422  | Inside virtual code; syspage copy starts             |
| `P`    | 448  | Syspage copy complete                                |
| `S`    | 457  | Vector table installed                               |
| `T`    | 468  | TTBR0 set                                            |
| `U`    | 477  | Kernel stack set                                     |
| `Z`    | 480  | Final asm marker                                     |
| `b`    | 483  | Branch into `main()`                                 |

### 1.2 `main()` + `syspage_init()` — C-level markers

From `main.c:108+` and `syspage.c:186-297`:

- `F` at `main.c:196` — entering `syspage_init()`.
- `G..n` inside `syspage_init()` — map relocation (lines 218–276).
- `o` at `syspage.c` (near line 279) — entering the program loop.
- **Hang observed at `o`**: inside the program relocation do/while at
  lines 279–291, before returning to `main()`.

Post-return order: `_hal_init()` → `hal_consolePrint()` → `_usrv_init()` →
`_vm_init()` → `_proc_init()` → `_syscalls_init()` →
`proc_start(main_initthr)`.

### 1.3 Syspage copy and `relOffs`

- `_init.S:425–431` copies syspage from the physical address PLO passed in
  (`x9`) to `VADDR_SYSPAGE` (alias of `_hal_syspageCopied`).
- `relOffs` is stored as `virtual - physical`.
- Relocation helper (`hal/aarch64/hal.c:38-41`):

  ```c
  void *hal_syspageRelocate(void *data)
  {
      return ((u8 *)data + relOffs);
  }
  ```

  No NULL tolerance. No bounds check. Caller must gate on NULL itself.

### 1.4 Known relocation bug in the map loop

`syspage.c:242, 248` relocates `map->entries` twice:

```c
map->entries = hal_syspageRelocate(map->entries);   // line 242
...
mapent_t *original_entries = map->entries;          // saves relocated-once
map->entries = hal_syspageRelocate(map->entries);   // line 248 — BUG
```

The loop terminates against `original_entries`, so map traversal still
works. But the list *head* (`map->entries`) now points one `relOffs` too
far. If any later code dereferences `map->entries`, it reads garbage —
which may include the memory that backs `progs`. **Strong candidate for
the marker-`o` hang cause.**

### 1.5 Program relocation loop

`syspage.c:279-291`:

```c
if (syspage_common.syspage->progs != NULL) {
    syspage_common.syspage->progs = hal_syspageRelocate(syspage_common.syspage->progs);
    prog = syspage_common.syspage->progs;
    do {
        prog->next  = hal_syspageRelocate(prog->next);
        prog->prev  = hal_syspageRelocate(prog->prev);
        prog->dmaps = hal_syspageRelocate(prog->dmaps);
        prog->imaps = hal_syspageRelocate(prog->imaps);
        prog->argv  = hal_syspageRelocate(prog->argv);
        prog = prog->next;
    } while (prog != syspage_common.syspage->progs);
}
```

Issues visible on inspection:

1. `dmaps`, `imaps`, `argv` are passed through `hal_syspageRelocate` even
   when NULL — producing `relOffs` as a pointer (not NULL). For the
   dummyfs-root blob, all three are NULL (`plo/cmds/blob.c:104-107`). If
   later code doesn't check NULL-after-shift, this bites in Phase B–E,
   not Phase A.
2. The loop has no cycle guard. If the 11-entry `progs` list (confirmed
   by build log: `user.plo`, kernel, dtb, dummyfs-root, dummyfs,
   pl011-tty, mkdir, bind, pcie, usb, psh) has any `next` pointer not
   properly relocated by PLO, the loop hangs.
3. Inner-shareable cache attributes may matter here if SMP is ever
   enabled (TD-01). Today, SMP is off, so this is a later concern.

### 1.6 Console path

- DTB lookup: `hal/aarch64/dtb.c:703-730` `dtb_getConsoleSerial()` walks
  `/chosen/stdout-path`.
- Console init: `hal/aarch64/generic/console.c:76-89` calls
  `hal_pl011Init(base)` which `_pmap_halMapDevice`s the PL011 and programs
  IBRD=26, FBRD=3 (48 MHz → 115200 baud).
- Early markers (`uart_putc`, `uart_putc_virt`) use hard-coded PL011
  register offsets, **bypassing** the Phoenix console subsystem. They
  will remain available during Phase B; Phase C replaces them.

### 1.7 First-user-process path

- `main.c:181` → `proc_start(main_initthr, NULL, "init")`.
- `main_initthr` iterates `syspage_progList()`; programs whose `argv`
  starts with `'X'` are spawned via `proc_syspageSpawn()`
  (`proc/process.c:1303-1306`).
- `proc_syspageSpawn` calls `proc_spawn(VM_OBJ_PHYSMEM, ...)` — the ELF
  parse path is skipped for the outer object but `process_load()` still
  parses an ELF header from the mapped physical range. So executable
  syspage progs (`psh`, `mkdir`, …) must remain valid ELF; dummyfs-root
  is a raw blob and isn't launched — dummyfs parses it at runtime.

### 1.8 `vm/map.c:763` FIXME

```c
else { /* FIXME disabled until memory objects are created for syspage progs */
    p = amap_page(map, e->amap, e->object, paddr, e->aoffs + offs, eoffs, prot);
}
```

Today the code falls through to direct physical mapping. Consequence:
fork from a syspage-launched process shares physical pages. Good enough
for Phase E's "first process reaches main()". Becomes blocking only when
the workload actually forks (e.g., psh → children). Keep in Phase E
scope notes.

## 2. Reference-OS facts we will lean on

### 2.1 Cortex-A72 SMP enable (TD-01 context)

Circle (Pi 4+): does **not** touch `CPUECTLR_EL1.SMPEN` explicitly. SMP
is already on from firmware when the kernel runs. Secondaries are woken
via spin-table — `CleanDataCache()` then write each core's entry into
`ARM_SPIN_TABLE_BASE` then `sev`.

Implication: our current commented-out SMPEN write in `_init.S:226-234`
is the right default for now. If we need secondaries later, follow the
spin-table pattern and leave SMPEN alone.

### 2.2 Pre-MMU cache maintenance (TD-02 context)

- Circle: uses set/way `dc isw` with MMU off, wraps in `DSB SY`.
  Precondition: SCTLR_EL1.C and .I must be 0 during set/way.
- rust-raspberrypi-os-tutorials: skips dcache ops entirely before MMU
  enable, relies on atomic SCTLR write.

Implication: our current approach (MMU enable without pre-invalidate) is
closer to the rust model and is already working through marker `N`. If a
later phase needs explicit invalidation, use set/way with caches
confirmed off, not VA-based `dc ivac`.

### 2.3 GIC-400 (Phase C)

- `GICD_BASE = 0xff841000`, `GICC_BASE = 0xff842000` (matches
  `docs/knowledge/raspberry-pi-4.md`).
- Distributor init (from Circle): disable + clear-pending +
  clear-active for all SPI lines, priority default, target = core 0,
  level-triggered, then set `GICD_CTLR.Enable`.
- CPU interface: `GICC_PMR = 0xF0`, `GICC_CTLR.Enable`.
- Generic-timer interrupt is PPI #30 — hardwired to each core's CPU
  interface, no `ITARGETSR` routing.

### 2.4 PL011 for the full console (Phase C)

- Pi 4 firmware provides a 48 MHz ref clock. IBRD=26/FBRD=3 → 115200 —
  already what Phoenix writes. No mailbox call needed from the kernel.
- GPIO 14/15 must be ALT0 before LCRH. The early UART markers already
  assume this is pre-configured by firmware/plo; verify on first Phase C
  rebuild.

### 2.5 ARM generic timer (Phase C/D)

- `CNTFRQ_EL0` is firmware-set on Pi 4 (54 MHz per `raspberry-pi-4.md`).
  Read it, don't write it.
- Tick programming: `CNTP_CVAL_EL0 = CNTPCT_EL0 + (CNTFRQ / HZ)`, then
  `CNTP_CTL_EL0 = 1`.
- Pi 4 firmware has a history of shipping `CNTFRQ = 0`. Phoenix must
  assert non-zero at _hal_timerInit and halt loudly if it is — silent
  zero-divide is easier to chase than silent hang.

## 3. Primary hypothesis for the marker-`o` hang

Ranked by evidence, not just plausibility:

1. **Map-loop double-relocation corrupts `progs`-adjacent memory**
   (§1.4). After the map loop finishes, `map->entries` points one
   `relOffs` past the real list head. Whatever sits at that offset gets
   clobbered on any write through that alias. If `progs` storage is
   near the map region, the program loop then traverses a corrupted
   ring. — **Try first.** Fix: write to `map->entries` once (the
   corrected pattern already exists in the saved `original_entries`
   variable).
2. **One of `prog->next` is not properly relocated by `plo`** — i.e.,
   the list-head pointer-dance in `plo/syspage.c` leaves one node with
   a pre-relocation `next`, so the kernel relocates it twice. Test by
   adding a post-relocation self-check (bounded iteration count before
   calling the loop terminated).
3. **`prog->argv`/`imaps`/`dmaps` NULL + `relOffs` produces a pointer
   that later code dereferences** — unlikely during Phase A (the loop
   itself doesn't deref these), but watch for it in Phase E.

Phase A discipline: try (1) first with one markered rebuild. Do not
pre-emptively rewrite the loop.

## 4. Tools we have, in order of preference

1. **QEMU gdbstub** on a non-Pi target (`aarch64-virt` or similar). Will
   not reproduce the bug directly (Pi 4 emulation isn't in the current
   build), but can exercise the syspage logic with synthetic input. Use
   to verify (1) before touching hardware.
2. **Real hardware UART markers.** Current mechanism. Keep additions
   bounded (≤3 new markers per rebuild). Every new marker must be
   justified in writing.
3. **`_early_vector_table`** was restored recently; if the hang is an
   unreported exception rather than a true spin, we will see output
   from it. Check whether it's wired to print before assuming spin.

## 5. Gaps to close before code changes

- [ ] Confirm whether `_early_vector_table` prints anything today — if
      yes, the hang is spin; if not, the hang could still be a masked
      exception. Quick grep-level check, no rebuild needed.
- [ ] Snapshot the 11-entry `progs` list layout (addresses, not just
      sizes) from a post-plo run so we can check whether the next-pointer
      drift hypothesis is testable without a full kernel run.
- [ ] Sanity-check that our `VADDR_SYSPAGE` and `relOffs` math matches
      what PLO expects. The map loop is reported working — if `relOffs`
      were wrong, map traversal would have hung earlier, so this is
      low-risk but worth one page of verification.
- [ ] No test harness exists for syspage relocation. Not a blocker for
      first boot; note for post-boot cleanup (TD-04 resolution).

## 6. Open questions the docs do not settle

1. **Is dummyfs-root's presence in the progs list enough for psh to
   start, or does dummyfs need `app`-style map entries?** Agent 3's
   read says blob progs get NULL imaps/dmaps; dummyfs server mmaps the
   raw range at runtime. Confirm on first boot, not in advance.
2. **Which PL011 instance is wired to the GPIO header on the user's
   board?** Phoenix's `stdout-path` points at `serial0`, but aliases
   vary between Pi 4B revisions. A mismatch here silently kills the
   console — fallback is the early raw-UART markers. Resolve in
   Phase C on the bench.
3. **Does the RPi4 firmware revision on the test board reliably set
   `CNTFRQ_EL0` non-zero?** Agent 2 flagged this as historically
   unreliable. Worth a `mrs` + print on first boot.

## 7. What this confirms for the roadmap

- Phase A is well-scoped with a specific first hypothesis (§3.1).
- Phase B needs a BSS-zeroing audit; current `_init.S` only zeros
  `PMAP_COMMON_SCRATCH_PAGE`. If BSS isn't zeroed by PLO, TD-03 bites
  earlier than expected.
- Phase C's PL011/GIC/timer sequences are all well-referenced; no new
  research needed before coding.
- Phase E has one known follow-up (the `vm/map.c:763` FIXME) which may
  need its own step.

## 8. What's intentionally out of scope for first boot

- SMP beyond core 0 (TD-01).
- Fork semantics on syspage-launched processes (`vm/map.c:763`).
- USB, PCIe, NIC, real storage. Present in the `progs` list but none
  are launched on first boot.
- Pi 5 (different SoC, different GIC, different boot stub).
