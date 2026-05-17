# Current Implementation Step

## BREAKTHROUGH 2026-05-17: Pi 4 boots through to psh shell

**Status**: ✅ FIRST FULL BOOT achieved on real Pi 4 hardware. The
multi-month MMU+cache crash at `PC=0x400498` (EX=4, ESR=0x02000000)
is RESOLVED.

**Manifest**: `manifests/2026-05-17-pi4-first-boot-to-psh.md`
**UART proof**: `artifacts/rpi4b-uart/rpi4b-uart-20260517-010011-netboot-armstub-1319367-and-L2CTLR-fix.log`

## Root cause (two convergent armstub bugs)

Multi-agent investigation found the actual root cause was in the
**armstub at EL3, not in the kernel cache code** the project had
been iterating on for weeks. Two independent bugs:

### Bug 1: 1319367 erratum workaround targeted an undefined sysreg

Phoenix's own `docs/plans/a72-errata-sweep.md` line 98 documents
the canonical Cortex-A72 1319367 fix as `CPUACTLR_EL1[46] = 1`
(DIS_HW_PAGE_AGGREGATION), per TF-A `cortex_a72.S
errata_a72_1319367_wa`. The armstub instead wrote
`CPUACTLR2_EL1[0]` with `CPUACTLR2_EL1` aliased to `S3_1_C15_C2_2` —
**that encoding is NOT a documented A72 system register**. Writing
to an undefined impl-defined sysreg can trap silently OR hit
whatever physical reg lives at that encoding, silently corrupting
state. The actual 1319367 mitigation never got applied.

### Bug 2: missing BCM2711 L2 RAM timing setup

Every other working Pi 4 bare-metal stack writes `L2CTLR_EL1 |= 0x22`
at EL3 (Data RAM Latency = 3 cycles, Data RAM Setup = 1 cycle).
Phoenix's armstub did not. The BCM2711 silicon's 1 MB Cortex-A72
cluster L2 at 1.5 GHz requires this; without it, the FIRST cacheable
D-side fill after `SCTLR.C=1` can return corrupt data.

Cross-referenced against:
- raspberrypi/tools/armstubs/armstub8.S (canonical)
- TF-A plat/rpi/common/aarch64/plat_helpers.S
- Circle boot/armstub/armstub8.S

### Why this is at EL3 not the kernel

Both CPUACTLR and L2CTLR access traps from EL1 on A72 r0p3. The
kernel cannot apply these itself — only EL3 code (armstub) can.
That's why every kernel-side cache-enable variant we tried (M-only,
M|C|I single-shot, deferred I-cache, set/way pre-flush, etc.) had
no effect: the actual bug was upstream.

## Fix commits

| Repo | SHA | What |
|---|---|---|
| phoenix-rtos-project | `dde9bb5` | armstub: 1319367 register fix + L2CTLR_EL1\|=0x22 |
| phoenix-rtos-kernel | `72242a05` | _init.S: single-shot M\|C\|I + drop TD-04 NC + drop post-copy flush; main.c: redundant icache-enable wrapped #if 0 |
| phoenix-rtos-devices | `b5cc6b0` | USB: merge BCM2711 PCIe bridge init into xhci library (canonical single-process Phoenix pattern) |
| phoenix-rtos-project | `fb771c4` | drop standalone pcie daemon from user.plo.yaml |
| plo | `cf98b18` | hal.c: document Phase Z1 cache-on hang; M-only baseline retained |

## UART evidence (post-fix)

- Armstub markers: `1` (859971 + 1319367) → `4` (L2 prefetch policy)
  → `2` (SMPEN) → **`5` (L2CTLR timing — new)** → `A`/`S0` (EL3 final
  steps). All print cleanly on real hardware.
- Kernel markers: `X1` → `X2` → `X3` → `X4` → `X5` all print past the
  previous crash point at PC=0x400498.
- Kernel banner: "Phoenix-RTOS microkernel v. 3.3.1 rev. ######## +0"
- Scheduler runs; init thread spawns dummyfs-root, dummyfs/devfs,
  pl011-tty, mkdir, bind, usb, psh.
- `fbcon: ok` — framebuffer console up.
- `pcie: linkUp=1 rcMode=1` — PCIe link trained.
- `pcie: 01:00.0 ven=1106 dev=3483 cls=0c0330` — VL805 USB host
  controller enumerated.
- `pcie: VL805 BAR0 programmed lo=f8000004` — BAR0 mapped to 0xf8000000.
- `pcie: diag-outbound caplen=20 ver=0100 hcsparams1=05000420 (KEPT)`
  — **xHCI capability registers readable** (no more 0xdead pattern;
  the USB merge work eliminated the cross-process bridge race).
- `psh: tty open`, `open: console enter` — psh shell waiting on
  pl011-tty for user input.
- Zero fault patterns in either of two consecutive UART captures.

## What's NOT yet working (next-phase /loop goals)

| Goal | Status | What's needed |
|---|---|---|
| cache | ✅ kernel reaches all targets via M\|C\|I single-shot | (done) |
| SMP | ⏳ cores 1-3 spinning in armstub WFE | kernel SMP bring-up |
| full RAM | ⏳ kernel sees only 948 MB low bank | map the 3008 MB high bank (above the GPU hole at 0x40000000) |
| USB | ⏳ VL805 enumerated; no HID driver | usb-hub init + USB-HID class driver + key-event plumbing |
| HDMI | ⏳ fbcon up; no scrolling text yet | text-rendering finalisation + scrollback or alternate output path |

## Open hygiene items

- `psh prompt` not yet seen — UART capture window is 90 s; either
  extend the window or send keystrokes via psh-interact to get the
  shell prompt and confirm interactive session works.
- plo `hal.c` Phase Z1 cache-on still parked at M-only baseline —
  may now work with the armstub fix; re-test deferred (kernel boot
  works as-is).
- Pi 4 `_init.S` shared with zynqmp has diverged enough that
  aarch64a53 builds no longer compile (Agent #8 finding); may want
  to factor rpi4b-specific code into `hal/aarch64/rpi4b/` if we want
  to keep both targets buildable.
- Phoenix research docs in `docs/research/` contain stale claims
  about "BCM2711 has no SLC" — Agent #9 found the datasheet page 5
  explicitly mentions a 1 MB system L2; needs reconciliation.

## Rollback safety

- Snapshot manifest: `manifests/2026-05-17-pi4-first-boot-to-psh.md`
- Restore via: `scripts/restore-integration-state.sh manifests/2026-05-17-pi4-first-boot-to-psh.md`
- All sibling repos clean (no uncommitted state) at snapshot time.

## Next focus

Pick the highest-leverage `/loop` goal to advance: SMP bring-up
unlocks 4-core operation, full RAM mapping unlocks process memory
headroom, USB-HID gets the keyboard working (already enumerated),
HDMI text-rendering polishes the user-visible output. Cache is no
longer the blocker.
