# SMP Pi 4 secondary bring-up — research summary (2026-05-23)

## Canonical Pi 4 armstub layout (raspberrypi/tools/armstubs/armstub8.S)

- `_start` at PA 0x0 — entry point for every core that the firmware
  brings into the armstub at boot.
- After mpidr-based routing:
  - cpu0 → primary path → reads `kernel_entry32` (PA 0xfc) and
    `dtb_ptr32` (PA 0xf8), then `br x4 = kernel_entry32`.
  - cpu1/2/3 → `secondary_spin` loop, polling `spin_cpu1/2/3`
    (PA 0xe0/0xe8/0xf0) for a non-zero value to branch to.
- Spin-table addresses (the OS's `cpu-release-addr` from the DT):
  - PA 0xd8: spin_cpu0 (8 bytes)
  - PA 0xe0: spin_cpu1 (8 bytes)
  - PA 0xe8: spin_cpu2 (8 bytes)
  - PA 0xf0: spin_cpu3 (8 bytes, **overlapped** with stub_magic
    0x5afe570b in the canonical armstub — confirmed in upstream
    source, our Phoenix armstub mirrors this)
  - PA 0xf4: stub_version
  - PA 0xf8: dtb_ptr32
  - PA 0xfc: kernel_entry32
  - PA 0x100: start of armstub init code

## What our Phoenix armstub does

Identical layout. `_start` routes all cores through start_late →
EL3 init → drop to EL2 → in_el2 → cbz x6 to primary/secondary
path. Should behave the same as the canonical armstub.

## What our UART markers measured

Multi-byte unique markers (`<Z<digit>` at in_el2 entry,
`>Y<digit>` post-WFE) showed that across 3 cold-boot cycles:

  - Run 1: cpu3 only ('<Z3')
  - Run 2: nothing
  - Run 3: cpu1 only ('<Z1')

cpu0 NEVER fired `<Z0` — primary doesn't run armstub at all.
cpu2 fired zero times across all three runs.

Two hypotheses for what's happening:

### Hypothesis A: Firmware brings up cores asynchronously

The Pi 4 firmware (start4.elf) only powers up cpu0 by default and
boots it through the armstub. Secondaries stay powered-down at
VC4-level reset. Some firmware versions DO bring up secondaries
into the armstub, but our test results suggest the version we're
running brings up at most ONE secondary per boot, non-deterministically.

If this is right, we need to power up cpu1/2/3 ourselves via:
- **PSCI CPU_ON via smc** — would require adding an EL3 secure
  monitor to the armstub that implements PSCI. Lots of work.
- **BCM2711 ARM_LOCAL_MAILBOX_3 SET writes** at PA 0x4c000080 +
  cpu_id*0x10. Direct VC4 power-up registers. Documented in
  BCM2711 SoC docs but not in the public BCM2711 ARM-side
  peripherals datasheet — Linux upstream brcm pcie + smp drivers
  reference these registers indirectly through the BCM2711
  ARM_LOCAL DT node.

### Hypothesis B: Firmware brings them up at PSCI boot-protocol address (not armstub _start)

The Pi 4 firmware might support a PSCI boot-protocol where
secondaries are kept in WFI/WFE at firmware-managed memory until
an OS-driven smc CPU_ON call. In that case the armstub never
sees them at all and our spin-table dance is pointless on Pi 4.

Linux upstream's `arch/arm64/boot/dts/broadcom/bcm2711.dtsi`
uses `enable-method = "spin-table"` and `cpu-release-addr = 0xd8`
(the armstub spin_cpu0+cpu_id*8 path). So Linux DOES rely on
the armstub spin-table mechanism. But Linux is documented to
work on Pi 4 SMP, so the armstub-spin-table path must be
working — for them.

The difference: Linux is started by start4.elf at PA 0x80000,
NOT through an armstub at 0x0. The Linux kernel itself does the
spin-table dance (its `__cpu_method_smp_init` reads
cpu-release-addr from DT and writes the secondary entry address
there).

For Phoenix, we're inserting the armstub and have its _start at
PA 0x0. The firmware loads the armstub at PA 0x0, then loads
the kernel image (`kernel8.img` — our relocator + plo payload)
at PA 0x80000 and starts cpu0 there. The relocator copies plo
to 0x200000 and jumps to plo._start, which eventually loads the
Phoenix kernel.

Secondaries — based on the canonical Pi 4 firmware behavior —
are supposed to be brought up to PA 0x0 (armstub _start), where
they enter the secondary_spin loop and wait. THAT is what our
markers should have showed, if firmware did its part.

## Action plan

The next SMP push has to start with one diagnostic question:
**does the Pi 4 firmware actually bring up cpu1/2/3 to PA 0x0
on cold boot?**

Two ways to find out:

1. Add a "any-core entered armstub _start" marker — single byte
   at the very first instruction. If only cpu0 prints it,
   firmware doesn't bring up secondaries.

2. Read the BCM2711 ARM_LOCAL MAILBOX registers (PA 0x4c000080
   onwards) from primary at boot — these reflect per-core
   power state. If cpu1/2/3 are powered down, we have our
   answer.

Then, depending on what we find:
  - If firmware brings up secondaries: figure out why our
    armstub variant loses them between PA 0x0 and `in_el2`.
  - If firmware doesn't: add a kernel-driven power-up sequence
    that pokes ARM_LOCAL MAILBOX SET registers to start
    cpu1/2/3 from a known kernel-side entry. This is the same
    approach Circle and bare-metal Pi 4 kernels use.

## Status

Investigation captured. No code change this session — the
BCM2711 USB bridge is also stuck in poison state which
constrains hardware iteration to one test per ≥30 min cooldown.
Resuming next session with the diagnostic plan above.
