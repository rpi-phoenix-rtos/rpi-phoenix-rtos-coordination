# Current Implementation Step

## Active step (2026-05-14): kernel MMU + D-cache + I-cache enabled; cache-policy cleanup active

Real Pi 4 now validates with kernel `SCTLR_EL1.M|C|I` enabled. The current
stable image reaches all configured userspace spawns and then idles in
`proc_reap`:

* image SHA256: `68b36c20f66401078bf1c9271f9405d965ed31af94da1e6ea02ea4a608442054`
* UART log: `artifacts/rpi4b-uart/rpi4b-uart-20260514-091513-netboot-stable-mmu-dcache-icache-uncached-amap-writable.log`
* terminal milestone: `main: spawn loop done, entering proc_reap idle`

The active implementation step is no longer "can the caches turn on?" but
"replace the temporary uncached data-path mappings with precise cache
maintenance while keeping `SCTLR_EL1.C|I` enabled."

Current required temporary cache policy:

* `vm/amap.c`: `amap_map()` must use `MAP_UNCACHED` for page copy/zero
  temporary mappings. Negative control `icache-dcache-cacheable-amap-writable-uncached`
  regressed immediately to EL0 Data Abort in `dummyfs` `_atexit_init()` /
  `memset`.
* `proc/process.c`: writable ELF `PT_LOAD` mappings are currently
  `MAP_UNCACHED`, including explicit BSS-tail mappings. This avoided the
  previous corruption of `atexit_common.head` and allowed all user programs to
  spawn under D-cache.
* `hal/aarch64/_init.S`: kernel flips `SCTLR_EL1.M|C|I`; early table-walk attrs
  remain non-cacheable and bootstrap pmap/common metadata remains NC.

Next actions, in order:

1. Preserve this stable boot state and do not remove the two uncached data-path
   workarounds until a replacement cache-maintenance point is tested.
2. Add targeted cache hygiene for freshly allocated app pages and/or COW
   object-to-anon copy so writable ELF mappings can become cacheable again.
3. Validate on real Pi after each narrowing step. QEMU rpi4b timed out at
   marker `A3` for this image and is not authoritative for the current cache
   boundary.
4. Once writable ELF mappings pass as cacheable, retry cacheable `amap`
   temporary mappings.

## Active step (2026-05-13): cache enable parked, baseline reliable

The cache-enable investigation reached a definitive empirical
boundary today and is parked. The locked-in shipping configuration
remains the Pi 4 baseline:

* armstub: A72 erratum 859971 + **1319367** (CPUACTLR2_EL1[0]=1) +
  SMPEN, applied at EL3 reset (`phoenix-rtos-project a27bc07`).
* plo: M-only (MMU on, caches off). Slow but reliable.
* kernel: M-only. Reliably reaches `(psh)%` prompt on real Pi 4.
* 4 GB DRAM unlocked via the `ddrh` map for chunk 2 in syspage
  (`phoenix-rtos-project 42b2db5`).
* HDMI fully functional — psh prompt visible on connected display.
* SMP smoke: cores 1-3 wake from the armstub spin-table, print
  alive marker, park in WFE.

Last validated image SHA: see `manifests/2026-05-13-armstub-1319367-final.md`.

## Cache enable investigation (parked)

Today's session ran **12 numbered iterations** plus diagnostics. The
critical finding from iter-12-diag (`b597c1f7…1e743807`):

```
T0:0000000000400703    ← kernel reads TTL3[0] correctly post-MMU+C=1
T1:0000000000401703    ← kernel reads TTL3[1] correctly post-MMU+C=1
FAR=ffffffffc0001890   ← but the WALKER reports TTL3[1] as invalid
```

The CPU's data cache returns correct PT entries to a regular `ldr`
instruction, AND the walker still hits translation-fault-L3 when
walking the same entries. The walker uses a memory path distinct
from the regular D-cache view that no `dc ivac` / `dc civac` /
`tlbi vmalle1is` ordering we tried clears.

Full forensic log + recipe table for iter-7…iter-12 in
[docs/research/2026-05-13-iter-11-12-cache-walker-finding.md](../docs/research/2026-05-13-iter-11-12-cache-walker-finding.md).

## What blocks USB+keyboard

Looking at the boot log, the kernel's PCIe driver enumerates the
VL805 device on bus 1 but reads vendor/device IDs as `0000`, and
all six BARs as raw zero. This is a SEPARATE issue from cache —
the firmware-to-kernel PCIe state handoff and/or the VL805 mailbox
reset is not yet wiring the device for kernel access. Worth its
own session.

## Next session — what to try

Highest-signal options (mostly listed in the research note):

1. **Compare against rust-raspberrypi-OS-tutorials ch15/16** on the
   same hardware. They run M|C|I on this CPU successfully. Their
   TCR field set + `__cpu_setup` barrier ordering is the reference.
2. **Try TCR `SH1/IRGN1/ORGN1 = 0`** (force walker to always read
   from DRAM, no cache view). Slow but architecturally robust.
3. **Linear-clean each PT entry with `dsb sy + dc cvac` after every
   `str`** — pushes lines all the way to PoC rather than just to
   D-cache.
4. **Single MSR with M|C|I and zero instructions of speculative
   surface** between SCTLR prep and write (no UART prints, only
   `nop`s) — let the architecture handle it as a single transition.
5. **Investigate PCIe/USB independently** — they don't need cache to
   work; the BAR-zero / vendor-ID-zero readback suggests a firmware
   handoff state issue (mailbox reset of VL805 may need to land
   BEFORE config space reads).

## Subordinate items

* TD-01 …TD-16, TD-plo-dcache, TD-plo-icache, TD-15-mboxprobe,
  TD-04 — see `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` for the
  current ledger; the cache TDs (TD-16, TD-plo-dcache, TD-plo-icache)
  remain open.
