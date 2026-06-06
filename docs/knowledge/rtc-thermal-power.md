# Forward-Research Brief: RTC, Thermal, Watchdog & Power Management on Pi 4 (BCM2711)

Scope: what Phoenix-RTOS will need, where it lives in the SoC, what Linux does, and the minimum-viable path to a working RPi4 boot. All numeric values are quoted from primary sources cited inline.

---

## 1. RTC

The Pi 4 has **no on-SoC RTC** in the BCM2711. Time is lost across reboots unless an external chip is fitted on the I2C bus, or NTP is used after networking comes up. This was a deliberate cost decision and persisted across all 2-, 3-, and 4-series boards. The Pi 5 changed this: an RTC was added inside the Renesas DA9091 PMIC (not in BCM2712 or RP1) and is exposed to Linux as a separate device with optional supercap/coin-cell backup ([Pi 5 introduction](https://www.raspberrypi.com/news/introducing-raspberry-pi-5/), [RPi5 RTC Specs forum](https://forums.raspberrypi.com/viewtopic.php?t=356991)).

The dominant external RTC on Pi 4 HATs is the **NXP PCF8523**, a low-power I2C CMOS RTC sold by Adafruit, Pimoroni and clones ([Adafruit PiRTC PCF8523](https://www.adafruit.com/product/3386)). Linux drives it via `drivers/rtc/rtc-pcf8523.c`; the DT binding is a standard I2C child node with `compatible = "nxp,pcf8523"`. Other common options on RPi HATs are DS1307 (cheaper, less stable) and DS3231 (TCXO, more accurate).

For Phoenix-RTOS the practical path is:
- **Phase 1 (current bring-up)**: defer RTC entirely. Treat wall-clock time as unset until userspace runs SNTP over Ethernet. The kernel's monotonic timer (ARM generic timer, already required for SMP startup) is unaffected.
- **Phase 2**: add a PCF8523 driver behind the existing I2C stack when one is integrated. The driver is small (~300 lines in Linux) because PCF8523 is a flat register file at I2C address 0x68.

NTP-as-RTC is the standard pattern on RTC-less embedded Linux boards and is the only zero-hardware option.

---

## 2. Thermal

BCM2711 has on-die thermal sensors in the **AVS_TMON** block (Adaptive Voltage Scaling, Thermal Monitor). Two interfaces are available:

**a) Mailbox (firmware) path** — read-only, simple. Tag `0x00030006 GET_TEMPERATURE` returns SoC temperature in millidegrees Celsius; divide by 1000 for °C. The buffer layout is well documented: `[size, 0, 0x30006, 8, 4, sensor_id, value, padding]`, with success indicated by `buf[1] == 0x80000000` and `buf[4] == 0x80000008` ([RPi firmware Mailbox Property Interface wiki](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface), [RPi 4 Working Mailbox example](https://forums.raspberrypi.com/viewtopic.php?t=250591)). A companion tag `0x0003000a GET_MAX_TEMPERATURE` returns the firmware's configured trip ceiling.

**b) Direct AVS_TMON MMIO** — read/write, supports trip-point interrupts and an emergency over-temperature chip-reset line. This is what `drivers/thermal/broadcom/bcm2711_thermal.c` and `drivers/thermal/broadcom/brcmstb_thermal.c` use; the original AVS TMON driver is documented in patchwork submissions ([brcmstb AVS TMON v3 patchset](https://patchwork.kernel.org/patch/9873047/)). DT compatible is `brcm,avs-tmon-bcm2838`. Linux pairs this with `drivers/clk/bcm/clk-raspberrypi.c`, which ramps clocks down via firmware property tags when the trip is hit.

Throttle policy on Pi 4 (per Raspberry Pi documentation):
- **Soft limit** (default 60 °C, settable via `temp_soft_limit` in `config.txt`): A76 drops from 1.5 GHz toward lower P-states; voltage trimmed slightly.
- **80 – 85 °C**: progressive ARM throttle.
- **85 °C**: ARM **and** GPU throttle, frequency steps down through 1500 / 1000 / 750 / 600 MHz ([thermal testing Raspberry Pi 4](https://www.raspberrypi.com/news/thermal-testing-raspberry-pi-4/), [frequency-management.md](https://www.raspberrypi.org/documentation/hardware/raspberrypi/frequency-management.md)).
- **Hard trip in AVS_TMON**: a chip-level reset line — independent of OS — fires if temp exceeds the trip (firmware default ~115 °C).

For Phoenix-RTOS the minimum viable approach is the **mailbox read path**: a periodic poll (1 Hz) of tag 0x00030006 exposed as `/dev/temp` or via the existing telemetry surface. No trip configuration, no clock ramping; the firmware-level hard trip still protects the silicon. Direct AVS_TMON support can come later if the OS needs to react to thresholds itself.

---

## 3. Watchdog

The PM (Power Manager) block on BCM2711 lives at the low-peripherals base **`0xFE100000`** (legacy alias `0x7E100000`); it bundles the watchdog, soft-reset and partition-select state machine. DT compatible chain is `"brcm,bcm2711-pm", "brcm,bcm2835-pm"` with fallback `"brcm,bcm2835-pm-wdt"` ([brcm,bcm2835-pm.txt binding](https://www.kernel.org/doc/Documentation/devicetree/bindings/soc/bcm/brcm,bcm2835-pm.txt), [bcm2711.dtsi rpi-6.6.y](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm/boot/dts/broadcom/bcm2711.dtsi)).

Driver: `drivers/watchdog/bcm2835_wdt.c` ([torvalds/linux master](https://github.com/torvalds/linux/blob/master/drivers/watchdog/bcm2835_wdt.c)). Key offsets and magic numbers used by Linux:

- `PM_RSTC` at offset `0x1c` — reset control.
- `PM_RSTS` at offset `0x20` — reset status, also used as a halt/partition channel.
- `PM_WDOG` at offset `0x24` — countdown register.
- `PM_PASSWORD = 0x5a000000` — top byte must be set on every write.
- `PM_RSTC_WRCFG_CLR = 0xffffffcf`, `PM_RSTC_WRCFG_FULL_RESET = 0x00000020`.
- `PM_RSTS_RASPBERRYPI_HALT = 0x555`, `PM_RSTS_PARTITION_CLR = 0xfffffaaa`.

Reboot sequence (Linux): write `PM_PASSWORD | <ticks>` to `PM_WDOG`, then `(PM_PASSWORD | (rstc & PM_RSTC_WRCFG_CLR) | PM_RSTC_WRCFG_FULL_RESET)` to `PM_RSTC`. Ten ticks ≈ 150 µs is sufficient ([RPi watchdog forum thread](https://forums.raspberrypi.com/viewtopic.php?t=353094)).

Halt/poweroff sequence: read `PM_RSTS`, OR with `PM_PASSWORD | PM_RSTS_RASPBERRYPI_HALT`, write back, then trigger the reset above. The firmware reads `PM_RSTS` on reboot to choose the boot partition; partition 63 (`0x3f`) is the magic value the firmware interprets as "halt" rather than "reboot" — this is why the same hardware path covers both ([Patchwork bcm2835 poweroff patch](https://patchwork.kernel.org/project/linux-arm-kernel/patch/1433845305-17329-2-git-send-email-noralf@tronnes.org/)).

For Phoenix-RTOS this is **the** essential of these four; right now the test harness power-cycles via a relay, which is slow and can race the build script. A one-page driver mapping `0xFE100000 + {0x1c, 0x20, 0x24}` and exposing a `reboot()` syscall hook is in scope for the next bring-up step. Same code unlocks `poweroff()` for free.

The same block is what the RPi bootloader's `BOOT_WATCHDOG_TIMEOUT=N` config uses, so behavior is consistent with the firmware-level boot watchdog already present on the SoC.

---

## 4. Power management

The BCM2711's main PMU is owned by the VPU (VideoCore). The Cortex-A72 cluster cannot independently gate most peripheral power domains; it must ask the firmware over the mailbox.

- **Reboot**: use the watchdog path in §3 (this is what Linux does on bcm2835 too — same call site).
- **Power-off**: two routes. Linux uses the `PM_RSTS` magic-partition trick (§3). An alternative is mailbox tag **`0x00028001 SET_POWER_STATE`** ([Mailbox property interface wiki](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface)). Tag `0x00028001` is per-device (SDCARD/USB/DSI/V3D) — it is **not** a system-wide off — so for system shutdown the partition-magic approach is the right one. The set-power-state tag is mostly useful later for selectively gating USB and SD ([Issue #1894 firmware](https://github.com/raspberrypi/firmware/issues/1894)).
- **Throttle/under-voltage status**: mailbox tag `0x00030046 GET_THROTTLED`. Bit 0 = under-voltage active; bit 1 = ARM cap active; bit 2 = throttling active; bit 3 = soft-limit active; bits 16 – 19 mirror these as sticky historical flags. The MxL7704 PMIC sets the under-voltage flag when input drops below ~4.63 V ([RPi forum on undervoltage throttling](https://forums.raspberrypi.com/viewtopic.php?t=313358), [pi forum #245584](https://forums.raspberrypi.com/viewtopic.php?t=245584)). Polling this is cheap and worth surfacing in OS telemetry — under-voltage is the single most common cause of weird Pi failures in the field.
- **CPU frequency control**: also via firmware mailbox (`GET_CLOCK_RATE`, `SET_CLOCK_RATE`, IDs include `ARM_CLK_ID = 3`). Out of scope for first boot, but the same mailbox driver covers it.

---

## 5. DT bindings + Linux driver paths (summary)

- External RTC (PCF8523): `nxp,pcf8523` — `drivers/rtc/rtc-pcf8523.c`
- Thermal (BCM2711 AVS): `brcm,bcm2711-thermal` / `brcm,avs-tmon-bcm2838` — `drivers/thermal/broadcom/bcm2711_thermal.c`, `drivers/thermal/broadcom/brcmstb_thermal.c`
- Throttle/clock ramp: firmware-mediated — `drivers/clk/bcm/clk-raspberrypi.c`
- Watchdog / PM: `brcm,bcm2711-pm`, `brcm,bcm2835-pm`, `brcm,bcm2835-pm-wdt` — `drivers/watchdog/bcm2835_wdt.c`
- Mailbox (consumed by all of the above): `brcm,bcm2835-mbox` — `drivers/mailbox/bcm2835-mailbox.c` and `drivers/firmware/raspberrypi.c`

---

## 6. Phoenix-RTOS path to working

Recommended ordering, smallest-cost first:

1. **Watchdog / reboot / poweroff** — implement the `0xFE100000` PM_RSTC+PM_WDOG+PM_RSTS driver. ~80 LOC. Replaces relay-based test harness power-cycle and unblocks proper crash-recovery semantics. **Highest priority** — this is the only one with operational impact today.
2. **Thermal read via mailbox** — once the mailbox driver from `_hal_init` is exercised for any other reason, expose tag 0x00030006 as a 1 Hz reading. Optional GET_THROTTLED (0x00030046) on the same channel for under-voltage telemetry. ~50 LOC over the mailbox primitive.
3. **RTC — defer.** No on-SoC RTC; require Ethernet + SNTP for wall-clock time. Add PCF8523 I2C driver only when a HAT is present, after I2C support lands.
4. **Power-off** — nice-to-have, identical code path to reboot (writes `PM_RSTS` halt magic instead of triggering reset). Add as a follow-up to #1.

Firmware-level hard-trip thermal protection (~115 °C) is already active on every BCM2711 regardless of OS, so deferring software thermal management is safe.

---

## 7. Known quirks

- **Clock-domain mapping**: there is no public canonical table mapping every BCM2711 power-domain ID to a peripheral. The community-maintained list comes from Linux's `pmdomain/raspberrypi` driver and forum reverse-engineering. Tag `0x00028001` is documented as deprecated/limited — newer power-domain tags supersede it ([Issue #1905 firmware](https://github.com/raspberrypi/firmware/issues/1905)).
- **Under-voltage detection bit**: the PMIC flag is sticky. Once tripped it stays in the historical-bits region (bits 16 – 19) until cleared. Phoenix-RTOS telemetry should treat the latched bits as "since boot" not "currently active".
- **Thermal trip causing soft-reset**: AVS_TMON's emergency over-temp line is wired into the chip reset and is independent of OS reads. If the SoC ever reboots unexpectedly with no log, check `PM_RSTS` for the over-temp cause bits before chasing software bugs.
- **Watchdog vs bootloader boot watchdog**: the same `PM_WDOG` is used by `BOOT_WATCHDOG_TIMEOUT=N` in the bootloader config ([rpi-eeprom release notes](https://github.com/raspberrypi/rpi-eeprom/blob/master/firmware-2711/release-notes.md)). Stomping the register from the OS while a boot watchdog is armed is fine — the OS having booted is itself the event the bootloader was waiting for.
- **Pi 5 differs sharply**: PMIC-hosted RTC, RP1 owns most peripherals, watchdog moved. None of the Pi 4 register layouts above apply to BCM2712.

---

## 8. Open questions

1. Does our currently-loaded mailbox driver (the one TD-04 work touches) already implement the property-interface envelope, or only the lower-level FIFO? The thermal/throttle reads only need the property layer; reboot does not need mailbox at all.
2. What is the precise timing of `PM_WDOG` ticks on BCM2711? Linux assumes the PM clock is the same on bcm2835/bcm2711, but timing has not been verified for our specific firmware blob.
3. Should we expose the under-voltage bit directly to userspace, or fold it into a generic system-health surface? Decision affects API stability.
4. When PCF8523 support eventually lands, should it be wired through Phoenix-RTOS's existing `realtime` device-class abstraction, or as a flat I2C driver? Need to check what other ports do.

---

**Prose word count**: ~1300 words (excluding citation labels, URLs and the §5 source-path list); within the 800 – 1500 target.
**File path**: `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/docs/knowledge/rtc-thermal-power.md`
