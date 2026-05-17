# Phoenix-RTOS Raspberry Pi 4 OS Development Guide

This document captures everything we learned during the months-long
Pi 4 bring-up of Phoenix-RTOS. The audience is future agents (and
humans) working on this port or similar bare-metal Pi 4 efforts.

Status at the time of writing (2026-05-17):

- Phoenix-RTOS boots through plo → kernel → user-space → psh shell
  on real Pi 4 hardware (Model B 4GB confirmed).
- HDMI fbcon shows the psh prompt; serial console (UART) works.
- USB enumeration is partial (xhci HC init stalls; see [TD-10]).
- SMP, full 4GB RAM mapping, USB-HID keyboard, and a few cosmetic
  items are still open.
- The "fix that unblocked everything" was a two-line bug in the
  armstub at EL3 — not in the kernel cache code we'd been iterating
  on for months. See **Root-cause story** below.

## Boot pipeline (what runs in what order)

Pi 4 boot is a multi-stage handoff. Every stage can leave invisible
side effects in cache lines, registers, or DMA descriptors that bite
later stages. Always think about what state each stage's predecessor
left behind.

```
EEPROM  ───┐
           │  bootloader (Pi 4 EEPROM, recent firmware) selects
           │   boot device by `boot_order` (SD/USB-MSD/network/...)
           ▼
bootcode.bin  (Pi 4: lives in EEPROM, can be on FAT for fallback)
           │
           ▼
start4.elf  (the VideoCore VPU's firmware)
           │  reads config.txt, parses bcm2711-rpi-4-b.dtb,
           │  patches DTB with runtime info, loads:
           │    - armstub8 (or our phoenix-armstub8-rpi4.bin) at PA 0x0
           │    - kernel8.img at PA 0x80000 (overridable via
           │      `kernel_address=` in config.txt; Pi 4 default = 0x80000)
           │    - initramfs (we use `initramfs loader.disk 0x08000000`)
           │  sets up DRAM banks (one or two on Pi 4 4GB), writes
           │  the firmware's DTB pointer into the armstub's dtb_ptr32
           │  field (offset 0xf8 in the armstub blob), then branches
           │  the A72 core 0 to PA 0x0.
           ▼
armstub  (runs at EL3)
           │  Phoenix's `phoenix-armstub8-rpi4.S` does, in order:
           │    1. Init LOCAL_CONTROL prescaler
           │    2. Set CNTFRQ_EL0, CNTHCTL_EL2, CNTVOFF_EL2
           │    3. Set SCR_EL3 (NS + RW + HCE + RES1)
           │    4. CPUACTLR_EL1 |= bit 32 (A72 erratum 859971 fix)
           │                    + bit 46 (1319367 fix — fixed 2026-05-17)
           │                    + bits 55, 56 (L1D / load-pass-store
           │                                   prefetch disables)
           │    5. CPUECTLR_EL1: clear L2 prefetch bits, set IFETCH
           │                    distance bits 32-37
           │    6. CPUECTLR_EL1 |= bit 6 (SMPEN)
           │    7. L2CTLR_EL1 |= bit 1 (Data RAM Latency=3)
           │                  |= bit 5 (Data RAM Setup=1)
           │                  *** BCM2711-specific; added 2026-05-17 ***
           │    8. dsb sy + isb
           │    9. setup_gic (GICv2 distributor + CPU interfaces)
           │   10. SCTLR_EL2 = 0x30c50830 (RES1 baseline, caches off)
           │   11. SPSR_EL3 = mode=EL2H, masks DAIF
           │   12. ELR_EL3 = in_el2
           │   13. eret  (drops to EL2)
           │  At EL2: install vectors_el2, drop to EL1 by setting up
           │  spsr_el2 + elr_el2 + eret. For secondary cores, spin in
           │  WFE at `secondary_spin` until spin_cpu1/2/3 slots are
           │  written (currently used only by the SMP-smoke probe in
           │  plo's `hal_smpBringupSecondaries`).
           ▼
plo  (runs at EL1, caches OFF on Pi 4)
           │  `sources/plo/hal/aarch64/generic/_init.S` and `hal.c`
           │  initialise UART (PL011), HDMI mailbox + framebuffer,
           │  build initial page tables identity-mapping the entire
           │  ARM-visible DRAM as Normal-WB-Cacheable EXCEPT the 76 MB
           │  GPU reserve which stays Device; set SCTLR.M=1 only
           │  (caches stay OFF — `mmu_enable()` single-shot M|C|I
           │  hangs on A72 r0p3 + BCM2711 silicon, even with the
           │  armstub fix).
           │
           │  plo parses user.plo.yaml from initramfs; copies
           │  programs (kernel ELF, dummyfs, devfs, pl011-tty, mkdir,
           │  bind, usb, psh) to memory; computes a syspage describing
           │  memory maps + program table + DTB pointer.
           │
           │  `go!` enters `hal_cpuJump`: tear down MMU, invalidate
           │  D-cache (invalidate-only — plo's stores went straight
           │  to DDR, so no clean needed), `eret` to the kernel entry
           │  with x0=DTB? and x9=syspage PA.
           ▼
kernel  (runs at EL1, caches initially OFF then ON)
           │  `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`:
           │    - Sets VBAR_EL1 to a low-PA early-vector table.
           │    - Builds TTBR0 (LOW-PA identity map, four 1 GB blocks
           │      covering pkernel, executing-PC, syspage, PL011).
           │    - Builds TTBR1 (HIGH-VA kernel mapping at
           │      0xffffffffc0000000, 2 MB initially).
           │    - Copies syspage from plo's heap PA into kernel BSS.
           │    - Defensive `bl hal_cpuInvalDataCacheAll` (Phase Z).
           │    - Pre-flip ritual: `ic ialluis; dsb ish; tlbi vmalle1is;
           │      dsb ish; isb`.
           │    - SCTLR_EL1 |= M | C | I  (single-shot, Phase Z2).
           │    - Post-flip: `isb; ic iallu; dsb nsh; isb`.
           │    - First cacheable load: `ldr x3, =0xfe201018`
           │      (UART FR register address). *** This is the
           │      historical crash site PC=0x400498 before the armstub
           │      fix. ***
           │    - Walks syspage progs list (post-Phase Z3, no NC
           │      override).
           │    - `br x0` to `_core_0_virtual` (HIGH-VA address).
           │  At high VA: `_hal_init_c` (in `hal.c`) → schedulers,
           │  pmap_init, timer, GICv2 routing, kmalloc → spawn init
           │  thread → fork dummyfs-root, dummyfs, pl011-tty, mkdir,
           │  bind, usb, psh.
           ▼
user-space  (Phoenix processes)
           │  pl011-tty publishes /dev/tty0; usb daemon initialises
           │  PCIe + VL805 + xHCI; psh attaches to /dev/tty0; eventually
           │  emits the `(psh)% ` prompt.
```

## Boot artifacts and layout

Files on the FAT boot partition (or netboot TFTP root):

```
bootcode.bin                — Pi 4: optional (EEPROM has it). Recovery only.
start4.elf                  — VPU firmware. ~2.3 MB. Reads config.txt.
fixup4.dat                  — companion to start4.elf. ~5 KB.
start4cd.elf / fixup4cd.dat — "compact" firmware (~850 KB). Disables
                              codecs / 3D / unused init. Useful for
                              minimum-footprint experiments.
bcm2711-rpi-4-b.dtb         — DeviceTree blob. ~56 KB on disk; the
                              firmware patches it at boot to add
                              memory bank info, MAC address, etc.
overlays/<name>.dtbo        — DT overlays applied by firmware.
phoenix-armstub8-rpi4.bin   — Phoenix's custom armstub (~4 KB).
                              Replaces the canonical raspberrypi/tools
                              armstub8.bin via `armstub=` in config.txt.
config.txt                  — firmware configuration. See "config.txt"
                              section below.
kernel8.img                 — Phoenix's plo+reloc stub (~60 KB). The
                              firmware loads this to 0x80000 (Pi 4
                              default for 64-bit). On launch the
                              reloc stub copies plo to PA 0x200000
                              and branches there.
loader.disk                 — Phoenix initramfs containing the kernel
                              ELF, dummyfs, etc. Loaded by firmware to
                              0x08000000 via `initramfs loader.disk`.
```

Memory layout on Pi 4 4GB (confirmed 2026-05-17 from firmware boot
screen):

```
0x00000000 - 0x000000ff      armstub area (firmware writes spin-table
                             here too)
0x00000100 - 0x000fffff      free for early use
0x00080000 - 0x000fffff      kernel8.img landed here (default
                             kernel_address)
0x00200000 - 0x003fffff      plo runtime (2 MB)
0x00400000 - 0x006fffff      kernel image runtime (low PA)
0x00700000 - 0x07ffffff      free general DRAM (low bank)
0x08000000 - 0x0Bffffff      initramfs (loader.disk)
0x0C000000 - 0x3b3fffff      free general DRAM (low bank, contiguous)
0x3b400000 - 0x3fffffff       76 MB GPU reserve (VC4 framebuffer + heap)
0x40000000 - 0xfbffffff      3008 MB ARM DRAM (high bank — NOT YET
                             EXPOSED to Phoenix; see TD-06)
0xfc000000 - 0xfeffffff      BCM2711 peripherals (low-peripheral mode)
0xff800000 - 0xff8fffff      ARM-local controllers (GIC, etc.)
```

## config.txt knobs that matter

Current Phoenix `config.txt`:

```
arm_64bit=1                    # boot ARM cores in AArch64
armstub=phoenix-armstub8-rpi4.bin
kernel=kernel8.img
initramfs loader.disk 0x08000000
enable_uart=1
uart_2ndstage=1                # firmware logs to UART after handoff
init_uart_baud=115200
init_uart_clock=48000000
dtoverlay=miniuart-bt          # route Bluetooth to mini-UART, free
                               # PL011 for our debug console
force_turbo=1                  # required when core_freq is pinned for
                               # miniuart-bt (so UART clock stays
                               # at 48 MHz)
core_freq=250                  # 250 MHz core clock — keeps the
                               # PL011 divisor stable for miniuart-bt
gpu_mem=76                     # 76 MB GPU reserve (minimum that
                               # still supports HDMI + mailbox + fb)
hdmi_force_hotplug=1
disable_overscan=1
disable_splash=1
```

Things to know:
- `disable_commandline_tags=2` is **undocumented**; values 0/1 are the
  documented set. We don't currently use it.
- `total_mem=` can only CAP memory, not enlarge. The high bank exposure
  on 4 GB Pi happens via DTB nodes added by firmware, not via this
  config knob.
- `start_file=start4cd.elf` + `fixup_file=fixup4cd.dat` switches to
  the compact firmware. Worth trying if VC4 interference is suspected.
- `dtparam=audio=off` disables HDMI audio DMA path.

## BCM2711 hardware specifics

Things that are different from a generic ARMv8-A board.

### Cortex-A72 r0p3

- L1I + L1D 48 KB each, L2 1 MB cluster-private cache.
- **L2 RAM timing must be programmed by firmware** for BCM2711's
  1.5 GHz silicon. `L2CTLR_EL1 |= 0x22` (Data RAM Latency=3 cycles,
  Setup=1 cycle). Without this, the first cacheable D-side fill after
  `SCTLR.C=1` returns corrupt data. This is set at EL3 in the armstub.
- A72 erratum **1319367** (speculative AT during context switch) —
  workaround is `CPUACTLR_EL1[46] = 1`. **NOT** `CPUACTLR2_EL1[0]`;
  that encoding (`S3_1_C15_C2_2`) is undefined on A72 and writing it
  silently corrupts adjacent state. (This was Phoenix's months-long
  invisible bug.)
- A72 erratum **859971** (speculative I-fetch into XN page) —
  workaround is `CPUACTLR_EL1[32] = 1` (DIS_INSTR_PREFETCH).
- `CPUACTLR_EL1` (S3_1_C15_C2_0) and `CPUECTLR_EL1` (S3_1_C15_C2_1)
  **trap from EL1** on A72 r0p3 unless `ACTLR_EL2.CPUACTLR/EL2_CPUECTLR`
  grants access. Phoenix applies all such writes at EL3 in the armstub.
- SCTLR_EL1 RES1 bits: 11, 18, 22, 28, 29 (some are RES0 on later
  revisions). Phoenix's current baseline `0x30c0c938` may be missing
  some — historical comments say `0x30d4d938` is the strict baseline.
  Cleanup candidate.

### BCM2711 SoC quirks

- **L2 cache controller is the A72 cluster L2**. The phrase "1 MB
  system L2 cache" in the BCM2711 ARM Peripherals datasheet is a
  copy-paste artefact from the BCM2835 era; there is NO separate
  system L2 outside the A72 cluster on BCM2711.
- **PCIe bridge**: single-lane Gen 2 controller at `0xfd500000`,
  with an outbound window mapping CPU PA `0x600000000` to PCIe
  bus address `0xf8000000`. The VL805 USB host controller (BAR0)
  lives behind it on bus 1.
- **PCIe bridge translation is per-process / per-mmap state**.
  Splitting "PCIe init" and "xHCI driver" into separate Phoenix
  processes hits a race where the second process's mmap returns
  0xdead-pattern. **Solution:** single-process bus owner (the
  Phoenix-canonical pattern; see TD-11).
- **GENET MAC** at `0xfd580000`. Phoenix has no GENET driver yet;
  netboot DMA may leave the RX ring armed at handoff (suspected
  but not confirmed corruption vector).
- **VideoCore VC4/VPU**: the SoC's boot CPU. Runs `start4.elf`.
  After handoff, VC4 keeps running (HDMI scanout, mailbox responses,
  possibly other DMA). Treat the GPU reserve (0x3b400000-0x3fffffff)
  as Device memory and DO NOT cache-alias.
- **DRAM low-peripheral mode**: peripherals at 0xfc000000-0xffffffff,
  ARM-visible DRAM in two banks: 948 MB at 0x00000000 and 3008 MB
  at 0x40000000.

### Pi 4 4GB DRAM topology

- Confirmed by firmware boot screen ("Raspberry Pi 4 Model B - 4GB")
  and by Linux booting on this same board.
- Firmware reports per-bank: low bank arm-visible size in its boot
  log ("MEM GPU:76 ARM:948 TOTAL:1024"). TOTAL=1024 is LOW-BANK
  total only; the high bank is announced separately, typically as
  a second `/memory@40000000` DTB node.
- **Phoenix currently sees only the low bank** (Goal 3 / TD-06).

## Reading the boot trace

Phoenix's boot UART output is dense with markers. Quick decoder:

| Source | Marker | Meaning |
|---|---|---|
| firmware | `MESS:HH:MM:SS.xxxxxx:...` | start4.elf log lines |
| firmware | `MEM GPU:NN ARM:NN TOTAL:NN` | low-bank-only memory split |
| firmware | `arm_loader: Starting ARM with NNMB` | low-bank ARM size |
| armstub | `1` | erratum 859971 + 1319367 workarounds applied |
| armstub | `4` | L2 prefetch policy applied |
| armstub | `2` | SMPEN applied |
| armstub | `5` | L2CTLR_EL1 BCM2711 timing applied (2026-05-17+) |
| armstub | `A`,`S0` | EL3 final steps + GIC + drop-to-EL2 |
| plo | `hal: console_init done` | PL011 + console layer up |
| plo | `mem: pre/post-init` | mmu_init done |
| plo | `mem: pre/post-iallu` | I-cache + TLB invalidate |
| plo | `mem: pre/post-sctlr-M` | SCTLR.M=1 applied (caches stay off) |
| plo | `draw: pre/post-bg-fill` | HDMI framebuffer cleared (blue) |
| plo | `1M2hHLmkECcv` | (varies) plo banner sequence |
| plo | `Phoenix-RTOS loader v. 1.21` | banner |
| plo | `call: exec ...` | parsing user.plo.yaml |
| plo | `go: enter ... go: jumphal` | jumping to kernel |
| plo | `hal: jump irq off`, `hal: jump exit el1` | cache+MMU teardown done |
| kernel | `A1` | _start entry (low PA) |
| kernel | `ZK[LSTUMV` | TTBR0/TTBR1 build progress markers |
| kernel | `X1`,`X2`,`X3`,`X4`,`X5` | early SCTLR-flip stages (Phase Z) |
| kernel | `N!YOPSTUZbcdtd15:OK` | post-flip checkpoint sequence |
| kernel | `td16:cf=...` | tag-16 syspage checkpoint |
| kernel | `eF123GHIJKs{...}` | syspage_init probes (TD-04 era) |
| kernel | `T{...}O{...}h{...}` | program-reloc map walk |
| kernel | `lmnPp01234567ZYfhRI` | _hal_init enter |
| kernel | `Phoenix-RTOS microkernel v. 3.3.1` | banner |
| kernel | `vm: ...` | VM init |
| kernel | `threads: ...` | scheduler events |
| kernel | `main: spawn ...` | spawning user-space processes |
| userspace | `name: registered ...` | dummyfs/devfs registration |
| userspace | `pl011-tty: tty0 lookup` | pl011-tty serving |
| userspace | `usb-daemon: ...` | usb daemon |
| userspace | `xhci-pcie: ...` | xhci HC init |
| userspace | `pcie: ...` | PCIe scan / VL805 program |
| userspace | `psh: ...` | shell startup |
| userspace | `(psh)% ` | interactive prompt |
| exception | `EX=N ESR=... ELR=... FAR=...` | early-vector dump |

## Root-cause story: the "PC=0x400498" crash

For weeks the kernel crashed at `PC=0x400498` with
`ESR=0x02000000, FAR=0x0` immediately after the SCTLR M|C|I write.
We tried EVERY cache-enable variant — M-only, M|C, M|C|I single-shot,
staged enable, deferred enable, pre-flip set/way invalidate,
post-flip dsb variants — and they ALL crashed at the same spot.

The fault site at disassembly time was:

```
0x484:  msr sctlr_el1, x0     <-- enable M|C|I
0x488:  isb
0x48c:  ic iallu
0x490:  dsb nsh
0x494:  isb
0x498:  ldr x3, =0xfe201018   <-- FIRST cacheable LOAD after enable
```

The fault appears at the first cacheable D-side load following the
SCTLR write. The literal-pool load fetches a constant via PC-relative
addressing — a perfectly ordinary instruction that worked thousands of
times before with caches off.

Multi-agent investigation found **two armstub bugs** at EL3 (which is
why no amount of kernel-side experimentation helped):

1. **Erratum 1319367 was written to the wrong system register.**
   Phoenix's `phoenix-armstub8-rpi4.S` had:
   ```
   #define CPUACTLR2_EL1   S3_1_C15_C2_2
   #define CPUACTLR2_EL1_ERRATA_1319367_WA  BIT(0)
   ...
   mrs x0, CPUACTLR2_EL1
   orr x0, x0, #CPUACTLR2_EL1_ERRATA_1319367_WA
   msr CPUACTLR2_EL1, x0
   ```
   But `S3_1_C15_C2_2` is **not a documented A72 system register**.
   Phoenix's own `docs/plans/a72-errata-sweep.md` line 98 — and ARM
   Trusted Firmware's `cortex_a72.S errata_a72_1319367_wa` — document
   the correct fix as `CPUACTLR_EL1[46] = 1` (DIS_HW_PAGE_AGGREGATION,
   same register as 859971, different bit).

   Writing to an undefined impl-defined sysreg can:
   - trap silently (no effect), or
   - hit whatever physical reg happens to live at that encoding,
     silently corrupting state.

   Either way the actual erratum mitigation never got applied; an
   AT-instruction speculation hazard plus possibly some corrupted
   state was on the table.

2. **`L2CTLR_EL1 |= 0x22` was never programmed.** The BCM2711's 1MB
   A72 cluster L2 at 1.5 GHz needs Data RAM Latency = 3 cycles
   (`L2CTLR_EL1[2:0] = 1`) and Data RAM Setup = 1 cycle (`L2CTLR_EL1[5] = 1`).
   The A72 TRM default of 2 cycles assumes timing tighter than what
   BCM2711 silicon can meet. **Every other working Pi 4 bare-metal
   stack programs this**:
   - raspberrypi/tools `armstubs/armstub8.S`
   - ARM Trusted Firmware `plat/rpi/common/aarch64/plat_helpers.S`
   - Circle `boot/armstub/armstub8.S`

   Without this, the first cacheable D-side fill after `SCTLR.C=1`
   returns corrupt data.

**Fix:** `phoenix-rtos-project` commit `dde9bb5` (2026-05-17). Two
small armstub edits:
- Replaced the bogus `CPUACTLR2_EL1[0]` write with a `CPUACTLR_EL1[46]`
  bit added to the existing `CPUACTLR_EL1` RMW.
- Added the canonical `L2CTLR_EL1 |= 0x22` write after SMPEN, before
  the eret to EL2.

Two-boot reproducibility verified on real Pi 4 hardware.

**Lesson for future agents:** before re-iterating on kernel-side
SCTLR sequences for a new ARMv8-A platform, cross-check the EL3 setup
against:
- The canonical vendor armstub (raspberrypi/tools, Xilinx FSBL, etc.).
- ARM Trusted Firmware's per-CPU `cpu_reset_func` and per-platform
  `plat_reset_handler`.
- The platform-specific TRM section "Initialisation requirements".

Different SoCs need different EL3 setup. Same kernel code can be
correct on one platform and crash on another because of EL3-only
setup that doesn't survive any kernel-side change.

## The USB merge

Phoenix originally had two separate processes for the BCM2711 USB
path: a `pcie` daemon (PCIe bridge bring-up) and the `xhci` driver
(USB host controller). Each opened its own mmap of the outbound
window. The second mmap returned **0xdead-pattern reads** — the
BCM2711 PCIe bridge translation state is per-process / per-mmap.

This was solved (2026-05-17) by folding bridge bring-up into the
xhci library as `bcm2711_pcie_initVL805()`, called from `xhci_init`
before any HC mmap. Single process, single mmap, no race. This
matches the canonical Phoenix-RTOS pattern for every other supported
board (imx6ull, imxrt106x/117x, ia32).

Commits: `phoenix-rtos-devices b5cc6b0`, `phoenix-rtos-project fb771c4`.

## Open issues snapshot

See `TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` for the full list. Short
version:

- **TD-06**: kernel `dtb.c` parses only the first `/memory*` node,
  blocking Goal 3 (4 GB RAM). Needs multi-bank support.
- **TD-10**: xhci HC init stalls after VL805 BAR readback; the
  silence point is somewhere between `bcm2711_pcie_initVL805`
  returning and the next debug print. Likely in `pcie_scanBus`
  iterating empty slots OR libtty back-pressure.
- **TD-12**: boot to psh prompt takes minutes even with caches on.
  Strongest candidate: single-core (no SMP) means every IPC round-
  trip serializes; libtty mirrors each byte to HDMI; cumulative
  cost is measured in minutes.
- **TD-01b**: kernel SMP integration (cores 1-3 currently park in
  WFE after armstub setup).
- **TD-04-hack-2/3**: cache-off-era heisenbug hacks (marker stores
  inside `_hal_init`, fake `dtbEnd`). Likely safe to remove now.
- **TD-05**: pervasive UART markers from the cache-off-era.

## Tooling and operational notes

### Test cycles

- `scripts/rebuild-rpi4b-fast.sh` — rebuild + export SD image.
- `scripts/test-cycle-netboot.sh` — power-cycle Pi + UART capture +
  bridge auto-recovery on DHCP timeout. **This is the reliable
  cycle.**
- `scripts/test-cycle-psh-interact.sh` — power-cycle + send commands
  via psh. **DO NOT USE** until fixed: it uses `exec python3` so
  the EXIT trap can't fire, and consistently wedges the en7 ↔ lima1
  socket_vmnet bridge. Use the netboot script with a longer capture
  (`--capture-secs 300`) to observe interactive output instead.
- `scripts/netboot-bridge-recover.sh` — manual bridge recovery
  (Pi must stay powered ON throughout).

### Logs and analysis

- UART logs land in `artifacts/rpi4b-uart/rpi4b-uart-YYYYMMDD-HHMMSS-<label>.log`.
- `scripts/uart-summary.sh [label|path]` — structured stage decoder.
- `scripts/uart-list.sh [N] [label]` — list most recent logs.
- `manifests/<date>-<slug>.md` — known-good integration manifests.
  Restore via `scripts/restore-integration-state.sh <manifest>`.

### VM / build host

- Lima VM `phoenix-dev` hosts: aarch64-phoenix toolchain in
  `/home/witoldbolt.guest/phoenix-toolchains/`; build root in
  `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/`;
  QEMU 10.2.2 in `/home/witoldbolt.guest/tools/qemu-10.2.2/`.
- dnsmasq runs in the VM on the `lima1` interface (10.42.0.1/24).
  TFTP root is the buildroot's `_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/`.

### Documentation discipline

- `docs/research/` accumulates per-topic deep dives.
- `docs/plans/` is for forward-looking work plans.
- `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` is the TD registry.
- `tracking/current-step.md` is a single-active-step working note;
  reset when starting a new step.
- `tracking/step-history.md` is an append-only log of completed
  steps.

## Pointers for future agents

If you're starting fresh on this port or a similar Pi 4 bring-up:

1. **Read this guide + AGENTS.md + tracking/current-step.md first.**
2. **Always check the armstub / EL3 setup before iterating on kernel
   SCTLR/cache code.** Phoenix lost weeks to this exact trap.
3. **Use `test-cycle-netboot.sh` for every test cycle.** Avoid the
   psh-interact script until TD-09 is fixed.
4. **Read the firmware UART log lines** (`MESS:HH:MM:...`) — they
   tell you which boot device was tried, what DRAM the firmware
   sees, etc.
5. **When stuck, check what Linux / Circle / U-Boot / TF-A do** for
   the same SoC. Anything every other working stack does and we
   don't is a candidate root cause.
6. **Single-process bus owners are the Phoenix-RTOS canonical
   pattern**, especially for SoC-level subsystems like PCIe + xHCI.
   Cross-process splits hit subtle hardware-state races.
7. **The Pi 4 4 GB DTB has TWO `/memory@*` nodes.** Make sure your
   parser handles both. (TD-06.)
8. **The Pi 4's GPU keeps running after handoff.** Treat the GPU
   reserve as Device memory; don't alias-cache it.
9. **Plo's `mmu_enable()` single-shot M|C|I hangs at the MSR on
   A72 r0p3 + BCM2711** even with the armstub fix. Plo stays at
   `SCTLR.M=1` only; the kernel does the full M|C|I enable.
