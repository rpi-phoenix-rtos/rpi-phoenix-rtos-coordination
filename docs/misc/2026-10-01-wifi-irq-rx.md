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

## Result

(pending — after the C9 build)
