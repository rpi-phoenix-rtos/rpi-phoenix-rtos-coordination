# WiFi #91 — BCM43455 firmware-execution gate: investigation & next steps

> **STATUS (2026-06-26): still PARKED — no change.** WiFi #91 remains at the firmware-
> execution gate (fw downloaded + CR4 released, fw does not run). Strongest lead is still
> the SOCRAM-tail readback / NVRAM-trailer-token issue. No work since 2026-06-04; resuming
> needs a focused datasheet/JTAG session. The loose WiFi WIP files in lwip
> (`port/wifi-fw-43455.*`, `wifi-nvram-43455.*`) are flagged by the cleanup plan (Phase A)
> to move to a `wip/wifi-91` branch.

**Status (2026-06-04):** firmware is downloaded correctly and the CR4 is released,
but the firmware **does not execute**. This note consolidates the elimination
work so a focused datasheet/JTAG session can start from the frontier instead of
reconstructing it.

All work is via the diag-UDP responder handler **`'G'`** =
`diag_format_sdio_fwrelease()` in
`sources/phoenix-rtos-lwip/port/diag-udp.c:4372-4990`. Trigger it on a booted Pi
with `./scripts/test-cycle-netboot.sh --label wifi-G --probe G --capture-secs 180`
(card-out, fresh PSU boot), then read `artifacts/diag-udp/*wifi-G*-probe.txt`.

## The decisive result

A **wide post-release image change-scan** (committed lwip `745b3a2`) reads 64 bytes
at 6 points spread across the full 643 KB loaded image (`+0x2000, +0x10000,
+0x30000, +0x60000, +0x90000, +0x9C000`) and compares to the source blob:

```
image-scan post vs fw (changed bytes/64 @ +off): +0x02000=0 +0x10000=0 +0x30000=0 +0x60000=0 +0x90000=0 +0x9C000=0
  -> 0/6 points changed => no memory writes anywhere -- fw genuinely not running
```

A running CR4 writes its data/BSS/stack *somewhere* in 643 KB. **0/6 changed ⇒ the
firmware is genuinely not executing — this is NOT an observability artifact.** It
also confirms (via read-back) that the full image is correctly resident in memory.

## Eliminated causes (with evidence)

1. **NVRAM placement/format.** Token `af 01 50 fe` = LE `0xfe5001af` → low16 `0x01af`
   (len 431), high16 `0xfe50` (= `~len`) — a valid Broadcom length-magic, placed at
   `0x237940 + 1728 - 4 = 0x237FFC` = RAM-top. The `SOCRAM-tail rc=-4` seen in the
   reply is a **diag-readback artifact** (that read used `block_size=16`; F1 block
   size is 64, so non-64 reads return `-EIO`), not an on-chip problem.
2. **CR4 release sequence.** Source-verified vs upstream brcmfmac `chip.c`:
   `brcmf_chip_cr4_set_active` → `brcmf_chip_resetcore(core, ARMCR4_BCMA_IOCTL_CPUHALT=0x20, 0, 0)`.
   The AI coredisable+resetcore emits `IOCTL=0x23 → RESET_CTL=0x01 → IOCTL=0x03 →
   RESET_CTL=0 (poll) → IOCTL=0x01` — **exactly** our `diag-udp.c:4670-4683`.
   IoCtrl readback `pre=0x21 / post=0x01` are sensible CR4 values ⇒ the CR4 wrapper
   base `0x18100000` is right.
3. **Host-side HT clock.** Already tried+reverted (code comment `diag-udp.c:4502-4519`,
   it spun: CHIPCLKCSR stuck `0x50/0x00` = OpenWrt #23069). Source-confirmed:
   `brcmf_sdio_download_firmware` downloads with `bus->alp_only=true` (so `CLK_AVAIL`
   requests **ALP**, not HT, during the fw window); HT comes up only *after* the
   firmware executes and requests it. Our `HT_clk_csr=0x48` (ALP only) is a *symptom*
   of fw-not-running, not the cause. **Do not re-attempt host-side HT.**
4. **Observability / SDIOD mailbox base.** Ruled out by the decisive scan above —
   the fw isn't running at all, so the `0x18005000` SDIOD-base hypothesis is moot
   for now (it only matters once fw executes).
5. **Firmware-image write correctness.** The scan read-back shows the source bytes
   are correctly resident at all 6 points across the image (no SBADDR-window
   corruption).

## Confirmed correct vs brcmfmac (source-verified)

- Download ordering: `clkctl(ALP)` → fw to `rambase 0x198000` → nvram to
  `rambase+ramsize-varsz = 0x237940` → `set_active(rstvec)`.
- `rstvec = le32(fw->data) = 0xb83ef198`; `brcmf_sdio_buscore_activate` writes it to
  **chip address 0** (after clearing the SDIO-core intstatus — see candidate B).
- `0xb83ef198` (bytes `98 f1 3e b8`) decodes as a valid **Thumb-2 `B.W`** (32-bit
  branch) instruction — i.e. a sensible reset vector to execute at address 0.

## Frontier: why won't the CR4 execute?

Everything in the documented brcmfmac sequence matches, yet the core doesn't run.
Ranked remaining candidates (need datasheet/JTAG):

- **A. Reset-fetch address mismatch (most likely).** Does the CR4 fetch its reset
  vector from the *backplane* address 0 we wrote, or from its TCM-mapped address 0
  (which may correspond to `rambase 0x198000` on the backplane)? If the CR4 boots
  from its own address 0 mapped to `0x198000`, our separate write to backplane
  address 0 is irrelevant — but note fw[0] (the same branch) is *also* at `0x198000`,
  so that alone wouldn't break it. Needs the 4345/43455 backplane-vs-core address
  map from the datasheet.
- **B. Missing activate step.** `brcmf_sdio_buscore_activate` clears the SDIO-core
  `intstatus` (`0xFFFFFFFF`) *before* writing rstvec. Our diag may skip this. Low
  probability (it's interrupt cleanup, not an execution gate) but a cheap, source-
  grounded thing to add.
- **C. ARM-vs-Thumb reset state.** Cortex-R4 resets into ARM state unless TEINIT/
  CFGTE selects Thumb. The vector is Thumb; if the core boots ARM it executes
  garbage and faults. (Works on Linux for the same silicon, so probably not — but
  verify the 43455's reset T-state.)
- **D. A core clock/PMU resource** the CR4 needs that POR doesn't provide.

## Recommended next experiments

1. **Trivial-program test (decisive, safe, diag-only).** Instead of the real fw,
   write a tiny hand-crafted CR4 routine that increments a counter at a known RAM
   address, place it at *both* address 0 and `0x198000`, release the CR4, and scan
   the counter. If it increments ⇒ the CR4 executes (and from which base) — isolates
   A and the release itself from the firmware. If not ⇒ the *release* is the
   problem, not the firmware. (Author both an ARM and a Thumb variant to also settle
   candidate C.)
2. Add the `intstatus = 0xFFFFFFFF` clear before the rstvec write (candidate B) —
   one line, source-grounded.
3. Obtain the BCM4345/43455 backplane address map (datasheet) to settle candidate A,
   or attach JTAG to read the CR4 PC/CPSR directly after release.

The diag fw-release code lives in the lwip-port process, so all of these are
**low-risk** to the working USB stack. See task #91 and memory
`project_wifi_fw_exec_gate_91`.
