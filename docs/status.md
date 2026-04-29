# Phoenix-RTOS Raspberry Pi 4 Port Status

## Current Status: 2026-04-29 — TD-04 NC-dest fix LANDED + three documented hacks reach `_hal_init` entry

**Latest milestone:** the BCM2711 cache-coherency anomaly at the
plo→kernel handoff is closed at the syspage-copy layer. Re-mapped
`_hal_syspageCopied` as Normal Non-Cacheable in TTBR1 TTL3 (MAIR
slot 1, AttrIndx=1) and switched the kernel-side syspage copy to
write through the high-VA NC mapping. Verified: three bit-identical
real-Pi-4 runs walk the entire map-entry list (11 entries) cleanly,
exit `syspage_init()` via the natural `entry == original_entries`
terminator, and reach `_hal_init()` (kernel commit `cff18d49`).

**Second class of bug exposed**, NOT covered by the NC-dest fix:
attempts to drive `_hal_init()` further surface a Heisenbug-class
hang (code-layout-sensitive, shifts every rebuild). Three documented
hacks (TD-04-hack-{1,2,3} in
`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`, kernel commit
`59c58644`) push past it to reach the `_hal_init()` entry sequence
on real hardware:

- `TD-04-hack-1`: SKIP `syspage_init()`'s prog-reloc loop.
- `TD-04-hack-2`: inline localization probes inside `_hal_init()`.
- `TD-04-hack-3`: fake `dtbEnd = dtbStart + 0x10000`.

Every hack carries a `TODO(TD-04-hack-N)` marker in the source plus
a full TD entry with risk and resolution requirements.

**Last-working marker chain** (with hacks applied, three consecutive
identical real-Pi-4 runs):

```
NYOPSTUZbcdeF123GHIJKs{...}p{...}r{...}q{...}VWXabcdefgB{...}T{...}O{...}
h{...}ijR{...}kl × 11 (loop terminates) lmnPYf  [+ inconsistent H/4/5/6/...]
```

— past `o` (the 2026-04-19 stuck point), past `Y` (syspage_init
return), past `f` (main.c marker before `_hal_init()`), into the
`_hal_init()` entry sequence. The H/4/5/6/F/S/r/D/s/E/etc. markers
inside `_hal_init` are present in the binary but fire inconsistently
across rebuilds — that's the Heisenbug.

**Active step:** stand up QEMU + gdbstub introspection (TD-07
prerequisite QEMU upgrade, then TD-08) to validate `_hal_init()`
logic against gdb-visible MMU/cache/TLB state on the unhacked
source, then try broader real-hardware fixes (extend NC mapping to
all kernel BSS, OR full inner-shareable D-cache clean+invalidate at
`_hal_init()` entry, OR VPU quiesce via mailbox). Per-marker probing
on real hardware has hit a wall — the markers themselves shift the
layout. Full plan in `tracking/current-step.md`.

**Project rule adopted this session:** every new diagnostic probe
must be tested in QEMU first, then on real Pi 4, with both outputs
diffed in the active step's tracker. Codified in `AGENTS.md` and
`docs/testing-automation.md`. The QEMU comparison is what reframed
"iter-8 hang in syspage_init" as a real-hardware-only cache-coherency
problem rather than a code bug.

## 2026-04-19 milestone (still valid): Map Relocation Completed

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