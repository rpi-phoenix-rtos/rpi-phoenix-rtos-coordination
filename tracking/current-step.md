# Current Implementation Step

## Step: Diagnose `_hal_init` Stack/Data-Store Exception on Pi 4

**Status**: IN PROGRESS

**Date**: 2026-04-30

**Commits**: 
- pending: Pi 4 netboot/UART diagnostics around `_hal_init` stack/data-store exception
- d1996d8f: Fix infinite loop in entry relocation by using original_entries for loop condition
- aff01622: Add more detailed markers in entry loop to diagnose infinite loop
- 2f0b391f: Add detailed debug markers in map relocation loop to pinpoint crash location
- d609a196: Add Y marker at end of syspage_init() to confirm completion
- b62fe368: Add W and X debug markers to syspage_init() to diagnose crash point

**Note**: Broken commits (1bb7f806, 1c6a5267, 5e74c3c9) have been removed from history

## 2026-04-30 Update

Automated netboot/power/UART testing is now available and was used for the
current diagnostic loop:

```bash
./scripts/rebuild-rpi4b-fast.sh
./scripts/test-cycle-netboot.sh --label <label> --capture-secs <seconds>
```

Latest verified image in this step:

- SHA256: `cf46e5277d6b9bd7e24875b37b034c948911d5cf0624e2faa717f3d9c362115e`
- UART log: `artifacts/rpi4b-uart/rpi4b-uart-20260430-064643-netboot-exception-esr-halt-rerun.log`

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
- Firmware HDMI1 EDID warnings are expected when only HDMI0 is connected.
- `prepare-rpi4b-dtb.sh` now defaults to copying the final-form upstream DTB
  without decompiling/linting it; set `RPI4B_DTB_LINT=1` for explicit DTB audits.

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
