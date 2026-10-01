# F1: interrupt-driven WiFi receive — design and pre-registered Pi check

Register row: [KNOWN-ISSUES F1](../KNOWN-ISSUES.md). Branch `f1-wifi-irq`:

| Repo | Commit | Change |
|---|---|---|
| phoenix-rtos-devices | `caa9b6c` | `storage/bcm2711-emmc/sdcard.c`: the SD handler returns -1 ("not mine") unless its own status shows an enabled interrupt pending. |
| phoenix-rtos-devices | `d4ab87e` | `wifi/rpi4-wifi/{rpi4-wifi,wifi}.c`: SDIO card interrupt plus a `/dev/wifiirq` waiter. |
| phoenix-rtos-lwip | `a7f63a4` | `drivers/wifi43455.c`: drain until empty, then sleep on `/dev/wifiirq`; bounded fallback to the paced poll. |

## Findings (offline)

**The line really is shared.** EMMC2, the SD card (`bcm2711.dtsi:429`), and the WiFi SDHCI controller (`bcm2711.dtsi:1141`, `bcm2711-rpi-ds.dtsi:188`) are both on GIC SPI 126, level-high. That is IRQ 158 in Phoenix.

**The kernel already supports shared lines:**
- Handlers sit in a circular list in registration order, and every one is called on each interrupt (`hal/aarch64/interrupts_gicv2.c:129-135`).
- A return ≥ 0 wakes that handler's condition variable, and a negative return means "not mine" (`proc/userintr.c:87-90`).
- A wakeup that arrives before the wait is remembered, not lost (`proc/threads.c:1454`, `:1252`).
- Every IRQ is level-triggered, so each handler must silence its own controller.

**The SD handler problem.** `sdhost_isr` returned 0 for every interrupt, so a WiFi interrupt would wake the SD command waiter early. That waiter reads the early wake as a timeout and resets the CMD/DAT lines under an in-flight command. Today this is harmless only because the WiFi controller never signals: in 156 logs `CARD_INTR=0`, and its signal enable is never written.

## Design

**The interrupt is armed only while a waiter sleeps.**

- A `/dev/wifiirq` read first re-arms card-interrupt detection (SDHCI §2.2.18).
- If the line is already up, the read returns at once. Otherwise it enables the signal, sleeps up to 10 ms, then disables the signal.
- It returns 1 byte for an interrupt and 0 for a timeout.
- With no client, the WiFi controller never drives IRQ 158, exactly as today.

**The chip interrupt mask is the frame indication only (`hostintmask=0x40`).**

- The next `/dev/wifidata` read clears the indication before draining, in brcmfmac's order.
- It also skips up to 16 non-data frames, so a 0 really means the queue is empty.

**lwip falls back to the poll for the rest of the boot if:**
- `/dev/wifiirq` is missing, or a read fails;
- 64 interrupts in a row find no frame (a stuck line);
- at least 32 frames, and more than the number of interrupts, are found only after timeouts (the interrupt is not firing).

**A/B switches:** `pollrx` on the daemon command line, or `wifi rxpoll` at run time.

## Risks

1. **SD regression.** The change sits in the root-filesystem path on SD boots, and the SD handler change prints no string. The SD-boot gate below is the only behavioural proof.
2. **Interrupt storm.** If `rpi4-wifi` dies inside a ≤10 ms armed window and a frame then arrives, line 158 stays asserted and nothing masks it. On SD boot that is a hard hang. The follow-up is in the kernel: after N interrupts that every handler declines, mask the line at the GIC and log it.
3. **Cost on the shared line.** Every IRQ 158 now runs both handlers. The `declined=` counter measures how often.
4. **Unproven on this chip:**
   - that byte-wide SDIO writes clear the chip's interrupt status;
   - how the controller holds CARD_INT.

   The lwip fallback bounds both.

## Pi check (pre-registered 2026-10-01)

### 1. Build proof

Build with `--scope core` and both branches merged. In `loader.disk`, `strings -a … | grep -c 'RX interrupt on IRQ'` must be ≥ 1, and the same for `'RX mode: '`. Master prints 0 for both.

### 2. Netboot, AP up, `/etc/wifi.conf` staged

PASS needs all of:
- `rpi4-wifi: RX interrupt on IRQ 158 …` is printed. Record its boot `status=` and `signal=`; a non-zero `signal=` is a finding in itself.
- `lwip: wifi43455: … RX mode: irq (/dev/wifiirq)`, then the join and a DHCP lease.
- No `RX mode: poll (` line anywhere.
- `wifi stats` shows `WIFISTATS rxirq mode=irq`, with wakes + level > 0, `acks` > 0, `acks_noframe` ≪ `acks`, and `ack_errs=0`.

### 3. Same-boot A/B

1. Run the 09-30 recipe (3): ping, `wifi-perf.py`, `wifi stats`.
2. Run `wifi rxpoll`, then repeat (3).
3. Compare `WIFIPERF-MEDIAN` TX/RX and the growth of `rx_misses` per MB moved.

Prediction: throughput at or above TX 3.6 / RX 3.3 MB/s, and far fewer misses in irq mode.

### 4. SD-boot regression gate

Boot from the card with WiFi joined. Run the `rpi4-storage-test` write/verify loop, with e2fsck, at the same time as `wifi-perf.py`. PASS needs all of:
- e2fsck clean;
- no SD `cmd timeout`, `cmd error` or `intr_status=` lines;
- WiFi still `mode=irq` with `ack_errs=0`;
- `declined` grew.

### 5. Link loss and rejoin

The 09-30 recipe (4), unchanged.

## Storm guard (kernel, ships with this)

These ship together with F1 in build 14:
- kernel branch `irq-unclaimed-guard`: `56267884` makes `userintr_dispatch` report a declining user handler as -1, and `b3040484` adds the guard in `hal/aarch64/interrupts_gicv2.c`;
- tests branch `irq-unclaimed-test`, `8176308`: the `irq-unclaimed` program.

**What the guard does.** It counts deliveries of an SPI that no handler claimed and that left the line still pending. After 100 000 in a row it masks the SPI and prints one line on the UART only: `interrupts: IRQ %u unclaimed %u times in a row, masked`. Any claim resets the count, as does a delivery that leaves the line low. Registering a handler resets it too and re-enables the line. The threshold matches Linux's `spurious.c` 99 900 / 100 000, but here the run must be unbroken, which is stricter.

**Trade-off.** Masking line 158 also silences the SD card until `rpi4-wifi` registers again. On SD boot, a dead daemon therefore becomes a root-filesystem stall instead of a hard hang.

**Deterministic test (`irq-unclaimed`, SPI 223, unwired).** The handler re-pends itself via GICD_ISPENDR, which emulates a held level line.

PASS needs these lines, in order:
- `A declined+released: fired=200000 enabled=1`
- `B claimed+held: fired=200000 enabled=1`
- exactly one `interrupts: IRQ 223 unclaimed 100000 times in a row, masked`
- `C declined+held: fired=100000 enabled=0 pending=1`
- `D re-register: fired=2 fired2=2 enabled=1`
- `IRQ-UNCLAIMED: PASS`

On a master kernel the expected result is `C … fired=300000 enabled=1` and `IRQ-UNCLAIMED: FAIL C`. That is what shows the test can fail.

**No false positives.** `interrupts: IRQ` must appear 0 times across every gate boot.

## Result

### Build 15, netboot, 2026-10-01 (`g15-wifi`, `g15-irqtest`)

**Build proof:** `RX interrupt on IRQ` 1, `RX mode: ` 2, `times in a row, masked` 1, `/bin/irq-unclaimed` present.

**Interrupt RX, (2) PASS.**
- Printed `RX interrupt on IRQ 158 (SDIO card interrupt, hostintmask=0x40); controller enables at boot: status=0x37ff003f signal=0x00000000`. The boot `signal=` is 0, so there is no hazard left by the firmware.
- Then `RX mode: irq (/dev/wifiirq)`, the join, and `STATUS joined=1`. No `RX mode: poll (` appeared before the deliberate switch.
- `WIFISTATS rxirq` showed `waits=21969 wakes=109 level=86 timeouts=21774`, and `isr claimed=109 declined=0 acks=194 acks_noframe=0 ack_errs=0 drained=2275`.

**Same-boot A/B, (3).**

| Mode | `WIFIPERF-MEDIAN` TX | RX |
|---|---|---|
| irq | **4.00 MB/s** (3.81–4.02) | **3.79 MB/s** (3.53–3.81) |
| poll, after `wifi rxpoll` | 3.56 MB/s (3.48–3.64) | 3.33 MB/s (3.19–3.36) |

The poll figures sit at the 09-30 baseline. Interrupt RX is **+12 % TX and +14 % RX**, with no overlap between the ranges. The prediction was "at or above the baseline", so this is PASS.

`wifi rxpoll` printed `RX mode: poll (the daemon switched the interrupt off; irq wakes=195 timeouts=28792 empty=77 missed=0)`.

**Storm guard (`irq-unclaimed`) PASS**, exactly the pre-registered sequence:
- A `fired=200000 enabled=1`
- B `fired=200000 enabled=1`
- one `interrupts: IRQ 223 unclaimed 100000 times in a row, masked`
- C `fired=100000 enabled=0 pending=1`
- D `fired=2 fired2=2 enabled=1`
- `IRQ-UNCLAIMED: PASS`

**Still to do:**
- the SD-boot regression gate (4): SD write/verify plus e2fsck under `wifi-perf`;
- the link-loss check (5);
- the showcase gate for build 15, with 0 `interrupts: IRQ` lines across all its boots.
