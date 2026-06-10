# V3D direct power-on (PM + rpivid_asb) — ATTENDED draft, NOT wired into boot

**Status:** DRAFT / not-yet-implemented. Do **not** run unattended. See "Why attended" below.
**Goal:** wake the BCM2711 V3D 4.2 GPU so its HUB IDENT0 reads the live magic
`0x02443356` ("V3D"+gen) instead of the `0xdeadbeef` bus-error sentinel — Tier-3 of the
GLQuake roadmap (`~/.claude/plans/calm-wobbling-quill.md`, Phase 3).

## The decisive correction (vs the earlier mailbox approach)

On **BCM2711 (Pi 4) the V3D power domain + reset are NOT driven by the firmware property
mailbox genpd** (`raspberrypi,bcm2835-power`). They are driven by the PM block
(`brcm,bcm2711-pm`, the Linux `bcm2835-power` driver) poking the **PM** and **ASB**
registers directly. Proven from the decompiled `bcm2711-rpi-4-b.dtb`:

```
v3d@7ec00000 { compatible="brcm,2711-v3d"; status="disabled";
               power-domains=<0x49 1>; resets=<0x49 0>; }
/* phandle 0x49 = watchdog@7e100000, compatible "brcm,bcm2711-pm","brcm,bcm2835-pm-wdt" */
watchdog@7e100000 { reg=<0x7e100000 0x114,   /* "pm"          */
                         0x7e00a000 0x24,    /* "asb"         */
                         0x7ec11000 0x20>;   /* "rpivid_asb"  */
                    #power-domain-cells; #reset-cells; }
```

So V3D power/reset = the `bcm2835-power` driver's `bcm2835_asb_power_on(PM_GRAFX,
ASB_V3D_M_CTRL, ASB_V3D_S_CTRL, PM_V3DRSTN)`. The `dtoverlay=vc4-fkms-v3d` already added to
`config.txt` is *supposed* to make the firmware do this at boot — **STEP 0 is to check
whether it already did** before poking anything (see below).

## Register map (ARM physical, BCM2711 low-peri 0xfe000000 mapping)

| Block        | DTB (VC bus) | **ARM phys** | size  | role                          |
|--------------|--------------|--------------|-------|-------------------------------|
| PM           | 0x7e100000   | `0xfe100000` | 0x114 | power island + **watchdog**   |
| ASB (legacy) | 0x7e00a000   | `0xfe00a000` | 0x24  | ISP/H264 async bridges        |
| rpivid_asb   | 0x7ec11000   | `0xfec11000` | 0x20  | **V3D async bridge (BCM2711)**|
| V3D HUB      | 0x7ec00000   | `0xfec00000` | —     | IDENT0 @ +0x00 (scout reads)  |
| V3D CORE0    | —            | `0xfec04000` | —     | per-core regs                 |

**On BCM2711, V3D ASB ops target `rpivid_asb` (0xfec11000), NOT legacy ASB.** This is the
`ASB_BASE(is_v3d)` macro: `is_v3d && rpivid_asb ? rpivid_asb : asb`.

### Verified constants (verbatim from bcm2835-power.c, Anholt v4 + Saenz BCM2711 ASB patch)

```c
#define PM_PASSWORD   0x5a000000      /* every PM/ASB write ORs this in the top bits */
#define PM_GRAFX      0x10c           /* offset within PM block (0xfe100000) */
#define PM_POWUP      BIT(0)
#define PM_POWOK      BIT(1)
#define PM_ISPOW      BIT(2)
#define PM_MEMREP     BIT(3)
#define PM_MRDONE     BIT(4)
#define PM_ISFUNC     BIT(5)
#define PM_ENAB       BIT(12)

#define ASB_V3D_S_CTRL 0x08           /* offset within rpivid_asb (0xfec11000) */
#define ASB_V3D_M_CTRL 0x0c
#define ASB_REQ_STOP   BIT(0)
#define ASB_ACK        BIT(1)
/* ASB write: writel(PM_PASSWORD | val, ASB_BASE + reg) */
```

### ⚠ Constants to CONFIRM against the canonical bcm2835-power.c before running

These were not captured verbatim this session (fetch routes were rate-limited); the next
session MUST read them out of `drivers/soc/bcm/bcm2835-power.c` (raspberrypi/linux rpi-6.6.y)
before poking, because a wrong write here is into the PM/watchdog block:

- `PM_INRUSH_SHIFT`, `PM_INRUSH_MASK`, `PM_INRUSH_3_5_MA .. PM_INRUSH_20_MA` (the POWUP inrush
  current-ramp loop — likely shift 13, 2-bit mask, values 0..3, but VERIFY).
- `PM_V3DRSTN` — the V3D reset-deassert bit in PM_GRAFX (the `resets=<0x49 0>` line). VERIFY.
- The exact body/ordering of `bcm2835_asb_power_on()` (does it deassert PM_V3DRSTN + set
  PM_ENAB before or after the two asb_enable calls?). VERIFY.

## Procedure (verbatim power_on + the asb wrapper)

```c
/* PM island bring-up — verbatim from bcm2835_power_power_on(pd, PM_GRAFX) */
if (PM_READ(PM_GRAFX) & PM_POWUP) return 0;            /* already up */
for (inrush = PM_INRUSH_3_5_MA; inrush <= PM_INRUSH_20_MA; inrush++) {
    PM_WRITE(PM_GRAFX, (PM_READ(PM_GRAFX) & ~PM_INRUSH_MASK)
                       | (inrush << PM_INRUSH_SHIFT) | PM_POWUP);
    /* poll PM_POWOK, ~3000ns budget per inrush step */
}
/* if !POWOK -> -ETIMEDOUT (this is the classic "Timeout waiting for grafx power OK") */
PM_WRITE(PM_GRAFX, PM_READ(PM_GRAFX) | PM_ISPOW);
PM_WRITE(PM_GRAFX, PM_READ(PM_GRAFX) | PM_MEMREP);     /* poll PM_MRDONE, ~1000ns */
PM_WRITE(PM_GRAFX, PM_READ(PM_GRAFX) | PM_ISFUNC);

/* then the ASB wrapper (bcm2835_asb_power_on tail) — VERIFY ordering: */
asb_enable(ASB_V3D_M_CTRL, is_v3d=1);  /* clear ASB_REQ_STOP, poll ACK->0, ~1000ns */
asb_enable(ASB_V3D_S_CTRL, is_v3d=1);
/* deassert V3D reset (PM_V3DRSTN) + PM_ENAB as the canonical driver does */

/* asb_enable(reg): writel(PM_PASSWORD | (readl(base+reg) & ~ASB_REQ_STOP), base+reg);
 *                  while (readl(base+reg) & ASB_ACK) { cpu_relax; timeout 1000ns; } */
```

After this, re-read `*(volatile u32*)(0xfec00000)` (HUB IDENT0): success == `0x02443356`.

## STEP 0 (do this FIRST, it may be all that's needed)

The `vc4-fkms-v3d` overlay is already in `config.txt`. Before implementing any PM/ASB poke,
just boot **netboot** (the existing `rpi4-v3d-scout` already runs there and dumps HUB IDENT)
and read the scout's IDENT0 line from UART. **If it already reads `0x02443356`, the firmware
powered V3D via the overlay and the entire direct-poke path is unnecessary** — go straight to
Tier-4 (the V3D triangle). Only if it still reads `0xdeadbeef` do you need the sequence above.

## Why this is ATTENDED, not unattended (do NOT put this in the boot path)

1. The `rpi4-v3d-scout` runs on **every** boot. A wrong PM/ASB write that hangs the SoC turns
   into a **persistent boot-hang loop**; if the session ends mid-hang the Pi is stuck
   power-cycling into a hanging image (the "cannot-silently-regress" rule, `feedback_unattended_scoping`).
2. The PM block (`0xfe100000`) holds the **watchdog/reset** controller — it is NOT covered by
   the scout's V3D-MMIO abort gating (which only protects `0xfec00000`). A stray write there is
   genuinely dangerous, not abort-isolated.
3. Recovery requires reverting the scout + rebuild + reboot — fine with a human watching the
   UART, not safe to attempt headless overnight.

**Attended run recipe:** keep the poke behind a diag-udp command (e.g. extend the scout to act
on a UDP trigger) OR a one-shot build flag, so the *default* boot never pokes PM; arm it only
with the UART in view; bound every poll with a spin-cap (never infinite); have the Meross
power-cycle ready. Confirm the verify-flagged constants from canonical source first.

## Provenance
- Sequence/constants: Linux `drivers/soc/bcm/bcm2835-power.c` — Eric Anholt "Add support for
  power domains under a new binding" (patchwork.kernel.org 20181212235150.6491-4) + Nicolas
  Saenz Julienne "Add support for BCM2711's RPiVid ASB" (patchwork 20210217114811.22069-9,
  the `rpivid_asb` / `ASB_BASE(is_v3d)` addition).
- ARM bases: decompiled `bcm2711-rpi-4-b.dtb` v3d/watchdog nodes (this session).
