# Current Implementation Step

## Step: Diagnose `proc_mutexCreate` Hang on Pi 4 (TD-13)

**Status**: IN PROGRESS

**Date**: 2026-05-01

**Manifest**: `manifests/2026-05-01-td13-mtxbypass-checkpoint.md`

**Sibling commits at this checkpoint**:
- kernel `agent/rpi4-program-reloc` @ `39c81236` — TD-13 syscall + phMutexCreate instrumentation (s16 trace, M/1/2/3/E/K markers, TD-13-mtxbypass)
- kernel `agent/rpi4-program-reloc` @ `c5c21c6e` — `>` pre-eret marker, EC probe, spawn-cap (TD-13-spawn-cap)
- devices `master` @ `8984455` — pl011-tty TD13_DBG progress markers via `debug()`

## 2026-05-01 Update — TD-13 narrowed

The previous "user processes are silent after eret" framing has been
replaced by hard data:

1. Every user process that gets dispatched (eret, `>` marker) does
   take a real EL0-source synchronous exception with EC=0x15
   (AArch64 SVC) — `*15` probe.
2. The first (and only) syscall any user process makes is #16 =
   `phMutexCreate`, called from libphoenix `_errno_init`'s
   `mutexCreate(&errno_common.lock)` — `s16` probe.
3. Inside `syscalls_phMutexCreate`, every process reaches the
   `proc_mutexCreate(attr)` call (markers `M`, `1`, `2` — the
   last only printed because TD-13-mtxbypass skips the
   `vm_mapBelongs` validation), and **none** ever returns from
   it (no `3`, no `K`, no `E`).
4. `dummyfs` (pid 3) never even reaches the SVC — it has no `>`,
   `*`, `s`, or `M` event in the log. Either it never runs, or
   it crashes between eret and its first SVC. Out of scope for
   the immediate next probe.

So the wall is one of these four calls inside `proc_mutexCreate`
(`sources/phoenix-rtos-kernel/proc/mutex.c:51-80`):

```text
mutex = vm_kmalloc(sizeof(*mutex));   /* candidate a */
id    = resource_alloc(p, &mutex->resource); /* candidate b */
proc_lockInit(&mutex->lock, attr, "user.mutex"); /* candidate c */
resource_put(p, &mutex->resource);    /* candidate d */
```

`vm_kmalloc` is the front-runner suspect (TD-04-class: heap
free-list traversal touching uncached/stale memory after the
syspage handoff). `proc_lockInit` is a close second (initializes a
spinlock — see TD-11 single-core spinlock).

## Next action

Add `a/b/c/d` UART markers between the four calls in
`proc_mutexCreate` (same single-byte-PL011-write idiom as the
existing TD-13 ladder), self-cap to ~32 events, run a single
netboot test cycle (~60s wait + 30s capture). Decision tree:

- See `Ma` only → `vm_kmalloc` is the wall.
- See `Mab` only → `resource_alloc` is the wall.
- See `Mabc` only → `proc_lockInit` is the wall.
- See `Mabcd` → returns from `proc_mutexCreate` but something
  later in `syscalls_phMutexCreate` (or the path back through
  `syscalls_dispatch` / exception return) hangs — distinct
  problem, fall back to context dump.

## Build/test commands

```bash
./scripts/rebuild-rpi4b-fast.sh
./scripts/test-cycle-netboot.sh --label td13-mutex-substep --capture-secs 30 --dhcp-wait-secs 60
python3 scripts/summarize-rpi4b-uart-log.py artifacts/rpi4b-uart/<latest>.log
```

## Historical commits leading here

- pending: Pi 4 SError masking, single-core spinlock fix, and post-spawn diagnostics
- d1996d8f: Fix infinite loop in entry relocation by using original_entries for loop condition
- aff01622: Add more detailed markers in entry loop to diagnose infinite loop
- 2f0b391f: Add detailed debug markers in map relocation loop to pinpoint crash location
- d609a196: Add Y marker at end of syspage_init() to confirm completion
- b62fe368: Add W and X debug markers to syspage_init() to diagnose crash point

**Note**: Broken commits (1bb7f806, 1c6a5267, 5e74c3c9) have been removed from history

## 2026-04-30 Update

Latest hardware result:

- Image SHA256: `3dc62d31c1469955ee462f7a0279cc4f570e7fcb57d71fc50ceb2686e1aec447`
- UART log: `artifacts/rpi4b-uart/rpi4b-uart-20260430-214456-netboot-spawn-names.log`
- QEMU smoke reaches `psh help`
- Real Pi reaches `main: spawned psh (9)`
- No `Exception`, `SError`, or instruction abort appears in the latest real-device log

Confirmed fixes:

- Single-core AArch64 spinlocks no longer depend on exclusive byte atomics.
- SError is kept masked across thread creation, IRQ dispatch, syscall/exception
  C dispatch, and direct `hal_jmp()` user entry.
- First scheduling happens before timer IRQs are enabled in the bootstrap
  context.

Current boundary:

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

Next action: instrument or debug post-spawn scheduling and console handoff.
The immediate question is whether `psh` is scheduled on hardware and whether
the shell is blocked waiting for `pl011-tty`/`devfs`/`/dev`.

Automated netboot/power/UART testing is now available and was used for the
current diagnostic loop:

```bash
./scripts/rebuild-rpi4b-fast.sh
./scripts/test-cycle-netboot.sh --label <label> --capture-secs <seconds>
```

Latest verified image in this step:

- SHA256: `07d40d5e1a197f4c3e763ac3368f475d774a7f1596cb9452073133673a970032`
- UART log: `artifacts/rpi4b-uart/rpi4b-uart-20260430-150524-netboot-nic-refresh-baseline.log`

Current marker boundary:

```text
... kllmnPYfhR
```

Meaning:

- `Y`: `syspage_init()` completed.
- `f`: `main()` reached the point immediately before `_hal_init()`.
- `h`: assembly `_hal_init` wrapper was entered before the C prologue.
- `R`: wrapper reset `SP` to the initial kernel stack.
- No later `q/w/u/W` marker appears, so the first direct data-store probe after
  `R` does not complete.

An inline exception-vector marker confirmed that the CPU takes an exception
after `R`; the normal exception path then faults recursively because it saves
state on the same suspect stack. A later attempt to print a clean ESR marker and
halt did not produce a clean ESR in the final capture, so the current confirmed
fact is exception-after-`R`, not yet a decoded syndrome.

Tool/process warnings observed during this step:

- First netboot DHCP attempt often times out; `test-cycle-netboot.sh` recovers
  by restarting the Lima/socket_vmnet bridge and then DHCP succeeds.
- `tio` has no timed capture mode in this setup; the canonical helper falls
  back to `picocom`.
- `picocom` can briefly leave `/dev/cu.usbserial-201310` locked after a capture;
  verify with `lsof /dev/cu.usbserial-201310` before rerunning if capture fails.
- `picocom --exit-after` did not terminate cleanly when the DUT emitted a very
  large stream of blank lines during failed `X3` experiments; the stuck capture
  had to be killed so the test-cycle trap could power the Pi off.
- The QEMU smoke helper previously returned success after timing out before a
  shell prompt; it has been tightened so `expect` timeout/eof paths exit
  nonzero.
- After that fix, `./scripts/qemu-shell-smoke.sh rpi4b` correctly exits `124`.
  The QEMU log reaches `STUZb!e{86000005}`. Decoding `ESR_EL1=0x86000005`
  gives an instruction abort from the same exception level with a level-1
  translation fault. That is strong evidence that QEMU is exposing the
  low-PC/invalid-TTBR0 problem immediately after the branch to `main`, while
  real Pi 4 hardware continues further only because stale translations or
  cached instruction state mask the bug for longer.
- Firmware HDMI1 EDID warnings are expected when only HDMI0 is connected.
- `prepare-rpi4b-dtb.sh` now defaults to copying the final-form upstream DTB
  without decompiling/linting it; set `RPI4B_DTB_LINT=1` for explicit DTB audits.

Failed experiments on 2026-04-30, both reverted before the baseline rebuild:

- Keeping the bootstrap `TTBR0_EL1` identity map active in the primary path
  regressed real hardware from `...YfhR` to `A2 ... X1 X2 X3`.
- Loading the normal exception vector base through the linked high VA also
  regressed real hardware from `...YfhR` to `A2 ... X1 X2 X3`.

Current conclusion: the real board still cannot reliably fetch instructions or
exception vectors through the TTBR1 higher-half kernel mapping. The next useful
work item is not more local `_hal_init` probing, but a focused rebuild of the
MMU transition so it matches the Linux/bare-metal pattern: keep identity fetch
valid, prove the higher-half executable mapping, branch once to the linked high
VA, then retire the identity path.

### Major Achievement
The system has successfully completed all map relocation in syspage initialization! This represents a massive milestone in the Raspberry Pi 4 bring-up process.

### Current Progress
**Last Working Markers**: `NYOPSTUZbcdeFGVWXabcdefgmklmno`

The system now:
- ✅ Completes early boot sequence
- ✅ Sets up MMU and virtual memory
- ✅ Enters syspage_init() successfully
- ✅ Validates syspage pointer
- ✅ Processes all map entries with relocation
- ✅ Reaches program relocation phase (marker `o`)

### Current Blocking Issue
The system hangs at marker `o` (starting program relocation). This is likely due to a circular linked list issue similar to what was encountered in entry relocation.

### Next Actions

#### Immediate (Current Blocker)
1. **Add strategic debug markers** around `hal_syspageRelocate(syspage_common.syspage->progs)` call:
   - Check if `syspage_common.syspage->progs` is NULL before calling `hal_syspageRelocate`
   - Add markers before and after the `hal_syspageRelocate` call
   - Test with enhanced debugging to identify exact failure point

2. **Determine root cause** of the hang at marker `o`:
   - NULL pointer dereference
   - Infinite loop in `hal_syspageRelocate`
   - Circular linked list in program entries
   - Memory access violation

3. **Implement targeted fix** based on diagnosis:
   - If circular list: Implement proper validation and termination
   - If NULL pointer: Add proper NULL check and error handling
   - If memory issue: Fix memory mapping or access pattern

#### Short-term (After Unblocking)
1. **Reach marker `Y`**: Complete syspage_init() successfully
2. **Reach marker `f`**: Enter _hal_init() successfully
3. **Complete syspage initialization** and enter HAL initialization phase

#### Medium-term (Next Milestones)
1. **Fix SMP enable** for multi-core support
2. **Re-enable syspage copy** with proper BSS mapping
3. **Implement proper cache management** for Cortex-A72
4. **Consolidate debug infrastructure** into configurable system

### Technical Details
- **Current Image SHA256**: `fecdc6b78fc1d55e4198ad27e34eee53dd866be6497894a11b64c7184344ccab`
- **UART Log**: `artifacts/rpi4b-uart/rpi4b-uart-20260419-104437.log`
- **Test Command**: `./scripts/capture-rpi4b-uart.sh`

### Success Criteria
- ✅ System reaches marker `o` (starting program relocation)
- ⏳ System reaches marker `Y` (end of syspage_init())
- ⏳ System reaches marker `f` (before _hal_init())
- ⏳ System completes syspage initialization and enters HAL initialization

### Rollback Information
Previous working commit: `b0a93572` (reverted problematic debug marker before TLB invalidation)
