# BCM2711 EMMC2 (Pi 4 microSD) SDHCI clock-base resolution

Date: 2026-06-07. Read-only research. Resolves the clock-base ambiguity in
`phoenix-rtos-devices/storage/bcm2711-emmc/` (`sdcard.c`, `bcm2711-sdio.c`).

## (a) VERDICT

**The SDHCI divider-input base clock on Pi 4 / BCM2711 EMMC2 is 100 MHz** (the
firmware `emmc2` leaf-clock rate on a stock, non-overclocked Pi 4 — exactly what
mailbox `GET_CLOCK_RATE(id 12)` returns: `100000000`).

**The Phoenix driver is CORRECT — it does NOT silently double to 100 MHz.**
To target 50 MHz against a 100 MHz base it selects `CLOCK_CONTROL_DIV_2`
(register field value = `0x01`), and SDCLK = 100 MHz / (2·1) = **50 MHz**. This
is bit-identical to what mainline Linux writes (see (c)). Do **not** "fix" the
divider to field value 2 — that would give 100/(2·2) = 25 MHz and would be the
real bug.

## (b) SDHCI divider formula (confirmed)

8/10-bit "divided clock mode", SDCLK Frequency Select field = register value N:
- N = 0  → SDCLK = base (divide by 1)
- N > 0  → SDCLK = base / (2·N)

So register field `0x01` = divide-by-2, `0x02` = divide-by-4, `0x04` =
divide-by-8 … This matches the Phoenix `sdhost_defs.h` encoding exactly:
`CLOCK_CONTROL_DIV_2 = 0x01<<8` ("divide by 2"), `DIV_4 = 0x02<<8`
("divide by 4"), …, `DIV_256 = 0x80<<8`. The `<<8` positions the value into
CONTROL1[15:8] (SDCLK Frequency Select). Confirmed against Linux
`sdhci.c::sdhci_calc_clk`, which computes an even `real_div` then writes the
field as `div >>= 1` (i.e. field = real_div/2 ⇒ SDCLK = base/real_div =
base/(2·field)).

## (c) What Linux does (base + speed cap + HS50 delay/tuning)

- **Base clock source.** `drivers/mmc/host/sdhci-iproc.c` sets
  `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN` on BCM2711, so the SDHCI core **ignores
  the CAPABILITIES base-clock field** and instead takes `host->max_clk` from the
  clk framework — the `emmc2` clock (`clocks = <&clocks BCM2711_CLOCK_EMMC2>`
  in `bcm2711.dtsi`), the same firmware-managed source as mailbox clock id 12.
  On a stock Pi 4 that rate is **100 MHz**. So Linux `max_clk = 100 MHz`.
- **Divider.** `sdhci_calc_clk(100 MHz, 50 MHz)` → `real_div = 2`, register
  field = `2 >> 1 = 1`. Same value the Phoenix driver writes.
- **bcm2711 ops** (`sdhci_iproc_bcm2711_ops`): `set_clock = sdhci_set_clock`,
  `set_power = sdhci_set_power_and_bus_voltage`,
  `get_max_clock = sdhci_iproc_get_max_clock`,
  `get_min_clock = sdhci_iproc_bcm2711_get_min_clock` (floors min at 200 kHz to
  avoid a controller hang when core-clock ≫ bus-clock). `bcm2711_data`:
  `.mmc_caps = MMC_CAP_3_3V_DDR`. Platform quirk:
  `SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12`.
- **Speed cap.** No explicit Hz cap beyond what the card/bus mode allows; the
  microSD slot runs HS (3.3V) up to 50 MHz. There is **no** vendor sample/drive
  delay-line programming for HS50 in this driver.

### HS50 delay / tuning register — NO

Mainline Linux does **NOT** program any input-sample-delay or output-drive
delay register for HS50 (high-speed, 3.3V) on bcm2711 emmc2. There is **no**
`platform_execute_tuning` / delay-line write in `sdhci_iproc_bcm2711_ops`;
stock `sdhci_set_clock` is used. Input-sample tuning applies only to HS200/
SDR104 (1.8V UHS), which this controller path does not use for the microSD
slot. (The firmware "arasan" driver's `delay: 1` is its own private setting,
not replicated by Linux and not required for HS50.)

## (d) CAPABILITIES register (offset 0x40) base-clock field on this controller

The "Base Clock Frequency For SD Clock" field (SDHCI CAPABILITIES, offset 0x40)
is **unreliable on bcm2711 emmc2** — which is exactly why Linux marks
`SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN` and reads the rate from the clk framework
instead. The Phoenix driver already makes the matching choice: its comment in
`sdcard_configClockAndPower()` notes the Capabilities reg "doesn't have to be
implemented" and it instead learns `refclkFrequency` from the mailbox in
`sdio_platformConfigure()`. That is the correct strategy; do not switch to
reading the CAPABILITIES base-clock field.

## Reconciling the firmware arasan log

`arasan: ... emmc: 200000000 actual: 50000000 div: 0x00000002 ... target:
50000000 delay: 1`

This is internally consistent under the **same** formula: 200 MHz / (2·2) =
50 MHz. It records that the *firmware transiently ran the emmc2 leaf clock at
200 MHz and used register field N=2* to reach 50 MHz. It does **not** mean the
divider base is "really 200" at Phoenix runtime: the base feeding the divider
is whatever the current emmc2 leaf-clock rate is, and `GET_CLOCK_RATE(12)`
returns that **actual** rate (100 MHz for us — not a parent PLL). (100 MHz, N=1)
and (200 MHz, N=2) are two correct ways to reach 50 MHz. So `emmc: 200000000`
is the firmware's own contemporaneous base, independent of the mailbox value we
read; both produce 50 MHz on the SD bus.

## Implication for the observed CRC noise

The Data-CRC symptom (reads recover via retry, **writes fail systematically**)
is **not** explained by a doubled clock: a uniform 100 MHz SD clock would corrupt
reads too. A doubling bug would also produce a symmetric failure. The asymmetry
points at the **write path** (cf. tracking notes: CMD18 multi-block bug,
single-block CMD24/CMD18 perf) or signal integrity at 50 MHz — not the clock
base. The clock base is correct at 100 MHz; the divider is correct at N=1.

## Citations

- Linux `sdhci-iproc.c` (bcm2711 data/ops, `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN`):
  https://github.com/torvalds/linux/blob/master/drivers/mmc/host/sdhci-iproc.c
- Linux `sdhci.c::sdhci_calc_clk` (Version-3.00 even-divisor, `div >>= 1`,
  SDCLK = base/(2·field)):
  https://github.com/torvalds/linux/blob/master/drivers/mmc/host/sdhci.c
- LKML — "mmc: sdhci-iproc: Set SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN on BCM2711"
  ("the controller doesn't seem to pick-up on clock changes" / cap base broken):
  https://www.spinics.net/lists/arm-kernel/msg915451.html
  http://mail.spinics.net/lists/linux-mmc/msg65588.html
- LKML — "mmc: sdhci-iproc: Cap min clock frequency on BCM2711" (200 kHz floor):
  https://lore.kernel.org/all/1628334401-6577-5-git-send-email-stefan.wahren@i2se.com/
- `bcm2711.dtsi` emmc2 node (`compatible "brcm,bcm2711-emmc2"`,
  `clocks = <&clocks BCM2711_CLOCK_EMMC2>`):
  https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm/boot/dts/broadcom/bcm2711.dtsi
- vcgencmd / firmware emmc2 clock = 100 MHz on stock Pi 4 (clock-id mailbox):
  https://forums.raspberrypi.com/viewtopic.php?t=245733
- SDHCI SDCLK Frequency Select field encoding (base/(2·N), N>0; base for N=0),
  representative SDHCI-compatible reference:
  https://onlinedocs.microchip.com/oxy/GUID-A52628F4-6F6F-4C77-80CB-113A0C62DB75-en-US-10/GUID-4688762D-FD02-40D0-AC32-95CDBA02BCF2.html
