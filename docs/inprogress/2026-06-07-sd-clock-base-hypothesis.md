# SD #154 — prime hypothesis: 100 vs 200 MHz divider base (2× overclock)

**Date:** 2026-06-07. **Task:** #154 (SD HS50 read+write reliability).

## The reframe

The "50 MHz Data-CRC noise" (reads recover via retry, writes fail systematically
5/5 at some LBAs) is most likely **not** signal-integrity / tap-delay at all. It is
likely the card being clocked at **100 MHz**, double the intended 50 MHz, because of
a clock-divider base-frequency mismatch.

### Evidence

1. **Driver has zero sampling/tap/phase tuning.** `sdcard_configClockAndPower`
   (sdcard.c) only sets the SDHCI clock divisor + `HOST_CONTROL_HIGH_SPEED` bit.
2. **Divisor encoding (sdhost_defs.h):** `CLOCK_CONTROL_DIV_2 = 0x01<<8` → register
   field `N=1`. SDHCI divided-clock formula: `SDCLK = base / (2·N)`.
3. **Our driver:** mailbox `GET_CLOCK_RATE(clock id 12 = EMMC2)` returns
   `refclk = 100 MHz` (logged: `bcm2711-emmc: EMMC2 @0xfe340000 ... refclk=100000000 Hz`).
   `sdcard_calculateDivisor(100M, 50M)` → `DIV_2` → writes `N=1`. Correct **only if
   base is 100 MHz** (100/2 = 50). If base is 200 MHz, `N=1` → 200/2 = **100 MHz**.
4. **Firmware's own arasan/EMMC2 driver** (same controller @0xfe340000), recent
   `sdboot-sdstk3.log`:
   `arasan: ... C1: 0x000e0207 emmc: 200000000 actual: 50000000 div: 0x00000002 delay: 1`
   → base **200 MHz**, divider field **N=2** → 200/(2·2) = 50 MHz. The firmware uses
   N=2 where we use N=1. Either the base differs, or we are 2× off.
5. **Read/write asymmetry fits overclock:** at 100 MHz both directions are marginal;
   writes (card samples host output) fail deterministically, reads (host re-samples)
   occasionally latch OK and recover on retry.
6. **Media is healthy** — host (proper timing) writes+reads the systematically-failing
   LBA 137786 perfectly with two patterns + scratch LBA 100. So it is the Pi driver's
   clocking, not a bad block.

## Decisive diagnostic (built + flashed 2026-06-07)

`sdcard.c`, gated `#ifdef SDCARD_DIAG_CLOCKSWEEP` (**REMOVE before commit**), run once
at the end of `sdcard_initCard` after `sdcard_wideAndFast` (card in HS function + 4-bit):

- Dumps `CAPABILITIES` (offset 0x40) **base-clock field** (the authoritative on-chip
  base), plus `CTRL1`/`HOSTCTRL` as-left and the mailbox refclk.
- Sweeps divisor `N ∈ {1,2,4}` (`DIV_2/4/8`), holding HS bit + bus width constant, and
  at each does 8× scratch write→read-back→memcmp to **LBA 100 & 200** (unused MBR→p1
  gap), logging `writeOk/8` and `readbackMatch/8` + per-trial mismatches with `intr`.
- Tag `SDDIAG:` for easy grep. Restores `DIV_2` before returning.

### Expected outcomes

- **If base = 200 MHz (overclock hypothesis true):** `N=1` (our current "50 MHz")
  noisy/failing; `N=2` (our "25 MHz" label) **clean read+write**; `N=4` clean.
  CAPS base-clock field should read ~200. → Fix = correct the divider base (use CAPS or
  200 MHz) so "50 MHz" emits `N=2`. Clean, complete fix; no tap-delay needed. Also
  explains the earlier "25 MHz fails MBR" run (it cleared the HS bit / left card in HS
  function — a botched config, not proof that low clock fails).
- **If base = 100 MHz:** `N=1` is genuinely 50 MHz and still noisy → real
  signal-integrity; then pursue output drive-delay (writes) per advisor. CAPS ~100.

## Next

Card is in the Pi (shuttle), SD-boot, capture UART, grep `SDDIAG:`. Parallel research
(`docs/research/2026-06-07-bcm2711-emmc2-clock-base.md`) confirms base-clock truth from
Linux `sdhci-iproc` + firmware sources.
