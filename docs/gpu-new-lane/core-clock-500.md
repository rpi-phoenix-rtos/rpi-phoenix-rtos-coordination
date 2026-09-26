# Adopting `core_freq=500` — dependency census, changes, gate

Status: **code ready on branches, Pi gate not run** (2026-09-26).
Origin: [E2b step 1](E2b-v3d-render-slowness.md#result--step-1-core-clock-ab-queue5-build-9-shipped-stk-warm-cache-interleaved):
`core_freq=500` gives SuperTuxKart **+13 %** (7.43 → 8.39 fps, 4 interleaved trials, 0 exceptions).

## Question

`config.txt` pins the VideoCore core (VPU/bus) clock at 250 MHz (`force_turbo=1`, `core_freq=250`).
That pin dates from April (`manifests/2026-04-17-pi4-uart-clock-restoration.md`, "temporary … for
UART stability", from before the PL011 console had its own clock through `init_uart_clock=48000000`).
The Pi 4 default, which Raspberry Pi OS uses, is 500 MHz. Which Phoenix code assumes or derives
from the core clock, and does any of it break at 500?

## Branches (not merged, not pushed)

| repo | worktree | branch | commit |
|---|---|---|---|
| phoenix-rtos-devices | `/home/houp/.claude/jobs/c8f1289c/tmp/wt-phoenix-rtos-devices-coreclk` | `gpu-lane/core-clock` | `27db916` rpi4-wifi: read the SDHCI base clock from the firmware |
| phoenix-rtos-project | `/home/houp/.claude/jobs/c8f1289c/tmp/wt-phoenix-rtos-project-coreclk` | `gpu-lane/core-clock` | `e70e124` rpi4b: run the VideoCore core clock at 500 MHz |

The two are independent. The devices change works at either core clock, so it can merge first.

## Firmware clock ids (for reference)

From `external/linux/include/soc/bcm2835/raspberrypi-firmware.h`: 1 = EMMC, 2 = UART, 3 = ARM,
**4 = CORE**, 5 = V3D, 11 = HEVC, 12 = EMMC2. Mailbox tags: `GET_CLOCK_RATE` 0x00030002,
`GET_CLOCK_RATE_MEASURED` 0x00030047, `SET_CLOCK_RATE` 0x00038002.

## Census

Method: `rg --no-ignore` (plain `grep -r` from the coord root skips `sources/`) over every sibling
under `sources/` and over `tools/`. Searched for `250000000`, `250 MHz`, `core_clk`, `CLOCK_CORE`,
clock id 4, `core_freq`, every mailbox clock tag, `AUX_MU_BAUD` and the AUX/SPI/BSC/PWM/SDHOST/Arasan
MMIO bases (0xfe215000, 0xfe204000, 0xfe804000, 0xfe20c000/c800, 0xfe202000, 0xfe300000). Every
Pi 4 driver (`rg -l rpi4|bcm2711|bcm2835`) was also read for clock or divisor code.

Which hardware runs on the core clock (Linux DT, `external/linux/arch/arm/boot/dts/broadcom/`):
the AUX block (mini-UART, SPI1/2), SPI0, the BSC/I2C controllers, the legacy SDHOST
(`mmc@7e202000`), plus the HVS/DMA fabric. The Arasan SDHCI used for WiFi (`mmc@7e300000`) is
**not** on it. Its DT clock is `BCM2835_CLOCK_EMMC`, a CPRMAN peripheral clock whose parent mux
includes `pllc_per`, the same PLL that drives the core clock (`drivers/clk/bcm/clk-bcm2835.c`).

| # | Consumer | Location | How it gets its clock | Verdict |
|---|---|---|---|---|
| 1 | **rpi4-wifi**, Arasan SDHCI divisor (400 kHz enumeration, 25 MHz data) | `sources/phoenix-rtos-devices/wifi/rpi4-wifi/rpi4-wifi.c:284` (before the fix) | **hard-coded `base_hz = 250000000u`** | **FIXED** (`27db916`). It now queries `GET_CLOCK_RATE(EMMC=1)` through `/dev/vcmbox`, logs the result, and falls back to 250 MHz only if the query fails. This is not clock 4, even though the task named clock 4: the Arasan runs on the EMMC clock, so querying CORE would have halved the WiFi bus clock if EMMC stays at 250 while core goes to 500. Whether the firmware moves EMMC with core_freq can't be settled from source. The new log line answers it on the first gate run. |
| 2 | **rpi4-hci**, mini-UART `AUX_MU_BAUD` (Bluetooth) | `sources/phoenix-rtos-devices/bt/rpi4-hci/rpi4-hci.c:534-543` | `GET_CLOCK_RATE(CORE=4)` **once at start**, fallback 500 MHz | **OK, no change.** Divisor = core/(8·115200) − 1: 250 MHz → 270 (seen on HW, `rpi4b-uart-20260810-073804-btdaemon.log:205`), 500 MHz → **541**. Both give 115 313 baud, +0.1 %. It needs a **fixed** core clock because the divisor is computed once (see force_turbo). |
| 3 | **tools/bt-probe** (coord repo), the same mini-UART divisor | `tools/bt-probe/bt-probe.c:343` | `GET_CLOCK_RATE(CORE=4)` once, fallback | OK, no change. |
| 4 | **tools/wifi-probe** (coord repo), the Arasan divisor | `tools/wifi-probe/wifi-probe.c:228` | **hard-coded 250 MHz**, the same code rpi4-wifi was lifted from | **Not changed.** It is a coord-repo tool (no commits there in this task) and superseded by rpi4-wifi. Follow-up: port the same fix, or retire it. Harmless unless someone runs the probe at a core clock that moves EMMC. |
| 5 | bcm2711-emmc (SD card, EMMC2) | `sources/phoenix-rtos-devices/storage/bcm2711-emmc/bcm2711-sdio.c:82-105` | `GET_CLOCK_RATE(EMMC2=12)` via libvcmbox, SET to 100 MHz if 0 | Independent of core. Already queries. |
| 6 | pl011-tty (console) | `pl011-tty.c:678`, `PL011_TTY_CLOCK 48000000u` from `_projects/aarch64a72-generic-rpi4b/board_config.h:34` | UART clock pinned by `init_uart_clock=48000000` | Independent. |
| 7 | kernel early console | `sources/phoenix-rtos-kernel/hal/aarch64/pl011.c:48-52` (IBRD 26 / FBRD 3), `hal/aarch64/_init.S:76-84` (guarded on 48 MHz) | 48 MHz UART clock: 48e6/(16·115200) = 26.042 → IBRD 26, FBRD round(0.042·64) = 3 | Independent. |
| 8 | armstub / kernel8 relocator early UART | `phoenix-armstub8-rpi4.S:148-156`, `phoenix-kernel8-reloc.S:31-38` (IBRD 26 / FBRD 3); `cntfrq` = 54 MHz oscillator (`phoenix-armstub8-rpi4.S:41,257`) | 48 MHz UART clock, 54 MHz crystal | Independent. |
| 9 | plo console | `sources/plo/hal/aarch64/generic/console.c` | inherits the firmware/armstub PL011 setup, programs no divisor | Independent. |
| 10 | plo / rpi4-fb framebuffer | `sources/plo/hal/aarch64/generic/video.c` | firmware framebuffer tags only | Independent. The HVS runs on the core clock but the firmware owns it. Faster is fine; Pi OS runs 1080p60 at 500. |
| 11 | rpi4-audio (PWM1) | `audio/rpi4-audio/rpi4-audio.c:121-131,249-270` | CPRMAN PWM clock from **`CM_SRC_OSC` (54 MHz)**, DIVI 2 | Independent. Confirmed: the core500 trial T4 logged `clk BUSY (CM_PWMCTL=0x00000091, ~44117 Hz) … /dev/audio0 ready`. The pwm-write/pwm-dma probes in `tools/` also use OSC. |
| 12 | V3D (old lane + gpu-lane) | `gpu/rpi4-v3d/v3d_gpu.c`, `mesa/v3d_phoenix_power.c`, `tools/gpu-lane/v3d-async/v3da_hw.c` | V3D clock id 5 (state/rate only) | Independent. The faster fabric is the point of the change. `tools/gpu-lane/stkprof/e2b-winsys.patch:66` has a now-stale comment ("config.txt pins core_freq=250"). It is only a comment. |
| 13 | rpivid HEVC | `tools/hevc-decode/hevc-m1.c:144-153` | sets its own HEVC clock (id 11) | Independent. |
| 14 | GENET (wired Ethernet, NFS root) | `sources/phoenix-rtos-lwip/drivers/bcm-genet.c:634` | "MDIO clock divides the bridge clock automatically", no divisor in software | Independent. Exercised at core 500 in the E2b trials (netboot + NFS root). |
| 15 | xHCI / PCIe (VL805, USB) | `usb/xhci/bcm2711-pcie.c` | PCIe refclk, no core-clock divisor | Independent. |
| 16 | thermal, hwrng, gpio, sysinfo, vcmbox, klogd | `sensors/rpi4-thermal`, `misc/rpi4-*`, `gpio/rpi4-gpio` | no clock divisors | Independent. |
| 17 | SPI / I2C / legacy SDHOST | none | — | **No Phoenix consumer.** Nothing maps 0xfe204000 (SPI0), 0xfe205000 / 0xfe804000 (BSC0/1) or 0xfe202000 (SDHOST). All
of these are `BCM2835_CLOCK_VPU` in `bcm283x.dtsi`. A future SPI/I2C driver must query `GET_CLOCK_RATE(4)`. |
| 18 | Non-Pi 250 MHz hits | `hal/armv7r/tda4vm/timer.c:33`, `plo/hal/*/zynqmp/zynqmp.c` comments, `onfi-4.c` tRST timings, `sparcv8leon-gr740-mini/board_config.h`, `phoenix-rtos-utils/benchmarks/{dup_close,high_load}/main.c:47,55` (a 250 MHz cycle-counter assumption from another board) | — | Not Pi core-clock consumers. |
| 19 | coord scripts / `.claude` | `scripts/` | no `core_clk` / `core_freq` / 250 MHz matches | No gate script greps for the old value. |

**Summary:** one real hard-coded consumer (rpi4-wifi, fixed; it was on the EMMC clock, not the core
clock), one coord-repo copy of it (wifi-probe, follow-up), and one core-clock consumer that already
queries the rate (rpi4-hci / bt-probe) but needs the rate to stay constant.

## force_turbo=1 — kept

1. **The mini-UART needs a fixed core clock.** rpi4-hci computes `AUX_MU_BAUD` once at start.
   Without `force_turbo` the Pi 4 firmware scales the core clock at runtime between `core_freq_min`
   and `core_freq`, and Bluetooth drifts off baud mid-session. The firmware pins the core clock by
   itself only when the mini-UART is the *primary* UART. With `miniuart-bt` the PL011 is primary, so
   `force_turbo` is the only thing holding the core clock still. (Upstream: "`miniuart-bt` requires a
   fixed VPU core clock such as `force_turbo=1` or `core_freq=250`", see
   `docs/knowledge/source-artifacts.md:46`.)
2. **The +13 % was measured with it set.** Removing it changes two things at once.
3. **ARM behaviour is unchanged.** `force_turbo` also holds the ARM at its maximum (1500 MHz), as it
   has since April. Dropping it would bring ARM DVFS, which is a separate change with its own timing
   risks.

An alternative, `core_freq_min=500` without `force_turbo`, would fix the core clock and let the ARM
scale. It is not taken here: it is a different experiment, and the gain is not measured.

## Changes

* **devices `27db916`**: `rpi4-wifi.c` gets `diag_sdhciBaseHz()`, which queries once
  (`vcmbox_call(GET_CLOCK_RATE, …{1, 0}…)`), caches the value, prints
  `rpi4-wifi: SDHCI base clock (EMMC) = N Hz` (or `GET_CLOCK_RATE(EMMC) failed; assuming 250000000 Hz`),
  and `diag_sdhciSetClockKHz()` divides that value. `build-standalone.sh` compiles
  `misc/rpi4-vcmbox/libvcmbox.c` in (the `tools/hevc-decode/build-hevc-m1.sh` pattern;
  `VCMBOX_DIR` can be overridden). The WL_ON power-cycle still drives the mailbox FIFO directly. That
  path already existed and is out of scope; it should move to libvcmbox as well.
  Validation: `build-standalone.sh` with every input overridden and `NFSROOT=/nonexistent-skip`
  built with 0 warnings and 0 undefined symbols. `-fsyntax-only -Werror -Wall -Wextra` passes.
  `strings` shows both new messages. Nothing was staged.
* **project `e70e124`**: `config.txt` `core_freq=250` → `core_freq=500`, with a comment giving the
  measurement, the date, the retired April rationale, and why `force_turbo=1` stays.

Coord docs that still describe the 250 MHz pin (update after the merge, not done here):
`docs/knowledge/manual-operator-instructions.md:726-727`, `docs/knowledge/testing-automation.md:144-145`,
`docs/knowledge/rpi4-os-development-guide.md:874,1162-1163`, and
`docs/knowledge/bluetooth-bcm43455-non-linux.md:82`. That last line is wrong for Phoenix in any
case: BT is on the mini-UART, and a fixed clock, not 250, is what's required.

## Residual risks

* **The EMMC clock may follow core_freq.** If it does, a *stale* rpi4-wifi binary (built before
  `27db916`) would clock the SDIO bus at 2× (800 kHz enumeration, 50 MHz data, without high-speed
  mode negotiated). **The devices change must be staged before, or with, the config change.** The
  gate checks for the new log line.
* **Thermal throttling** can still lower clocks under `force_turbo` at the soft limit. If it touched
  the core clock, the mini-UART would drift. This risk existed at 250 as well. It is now slightly
  more likely because the core draws a bit more power. Watch `/dev/throttled` in long BT sessions.
* **Coverage not exercised by E2b T2/T4.** Those two core-500 boots were netboot + NFS root, GENET,
  PL011 console, V3D, HDMI, audio bring-up and STK, with 0 exceptions. They did **not** run WiFi,
  Bluetooth, the SD card lane (EMMC2 is independent by the table, but untested at 500), or audible
  audio. The gate covers WiFi and BT.
* rpi4-hci and rpi4-wifi still drive the mailbox FIFO directly for GPIO power (pre-existing; the
  libvcmbox rule is to go through the server).
* tools/wifi-probe keeps the 250 MHz hard-code (row 4).

## Pre-registered Pi gate (coordinator runs; not run by this agent)

**Preparation (host):**

1. Merge devices `gpu-lane/core-clock` first, stage it, and run G0 below. Then merge project
   `gpu-lane/core-clock` to `master` and rebuild the image.
   A config-only change is picked up by `--scope auto`. Check that
   `grep -c '^core_freq=500' .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/config.txt`
   prints 1 and `grep -c '^force_turbo=1'` prints 1.
2. Stage WiFi: `sources/phoenix-rtos-devices/wifi/rpi4-wifi/build-standalone.sh`. Gate:
   `strings -a /srv/phoenix-rpi4-nfs-gcc16/bin/rpi4-wifi | grep -c 'SDHCI base clock (EMMC)'` ≥ 1.
3. Stage BT: `sources/phoenix-rtos-devices/bt/rpi4-hci/build-standalone.sh`. It stages `rpi4-hci`,
   `btctl` and `/etc/bluetooth/BCM4345C0.hcd`. None of the three is in the live export today.
4. `./scripts/radio-ap-up.sh` and the AP-side tcpdump from `wifi-runtime-test.sh`
   (`tcpdump -i wlp3s0 -n -l -e -v -tttt 'arp or icmp or port 67 or port 68'`).

**Cycle G0: new rpi4-wifi at the current 250 MHz** (devices merged, project NOT yet merged). This
separates the untested WiFi binary from the clock change. Run the `wifi-runtime-test.sh` sequence
once and read the `rpi4-wifi: SDHCI base clock (EMMC) = N Hz` line. Expect N = 250000000 and the
usual AP-side DISCOVER..ACK + ICMP. It shows the vcmbox path works from the WiFi daemon, so G1 then
measures only the config change. N ≠ 250 MHz at core 250 would be news in itself: it would mean the
old hard-code was already wrong.

**Cycle G1: radios + console** (`test-cycle-psh-interact.sh --label core500-g1 --inter-cmd-secs 8
--idle-secs 60 --max-cmd-secs 160`). The psh commands, in order:

```
ls /dev
rpi4-hci
btctl scan
rpi4-wifi &
wifi status
wifi connect PhoenixNet phoenixpi2026
wifi status
ping -c 5 10.43.0.1
wifi stats
```

(`rpi4-wifi &` is copied verbatim from the `wifirt1-d2` PASS. rpi4-wifi and rpi4-hci daemonize
themselves by fork + SIGUSR1, so the prompt returns either way. Two differences from `wifirt1-d2`:
(a) that run moved the lab `/etc/wifi.conf` aside first. If the conf is left in place, the netif may
join by itself before `wifi connect`, so either move it aside the same way (with the EXIT-trap
restore) or grade P5 without regard to order. Any DISCOVER..ACK + ICMP 5/5 from 10.43.0.89 counts.
(b) BT is started before WiFi on purpose. Running the two daemons one after the other keeps their
direct-FIFO mailbox start-ups from overlapping.)

**Cycle G2: GPU** (the queue5 method, warm cache, a boot with no GPU-driver change):

```
stk --track=hacienda --numkarts=4 --profile-laps=2
```

with `--max-cmd-secs 440`, graded as queue5 graded it:
`grep -ao 'flipstat [0-9]* frames in [0-9]* ms = [0-9.]* fps' <log>`, keeping windows with
fps > 3, taking the mean. Two trials.

**PASS requires all of these:**

| # | Check | Source |
|---|---|---|
| P1 | Boot reaches `(psh)%`. `uart-summary.sh` stage table all ✓, including the lwip line. No garbled console lines beyond the known ~1.3 % UART corruption baseline. | UART log |
| P2 | `rpi4-hci: core_clk=500000000 Hz -> AUX_MU_BAUD=541` (proves the firmware applied 500) | UART log |
| P3 | `HCI_RESET ok`, `patchram 323/323 records acked`, `BD_ADDR dc:a6:32:3c:dd:f5`, `registered /dev/hci0`, then `btctl`: `Inquiry Complete (status=0x00)` (0 sightings is acceptable; the 250 MHz reference also saw 0) | UART log |
| P4 | `rpi4-wifi: SDHCI base clock (EMMC) = N Hz` is present. **Record N.** | UART log |
| P5 | AP side: DHCP DISCOVER..ACK from `dc:a6:32:3c:dd:f3`, then ICMP echo 5/5 from 10.43.0.89 (graded on the AP capture, not on Pi text) | tcpdump |
| P6 | `rpi4-audio: … clk BUSY … /dev/audio0 ready` (bring-up only; audible check out of scope) | UART log |
| P7 | STK gameplay mean ≥ **8.2 fps** in both G2 trials | flipstat |
| P8 | 0 `Exception #` lines in G1 and G2 | UART logs |

**What each outcome means:**

* All pass → adopt: record a manifest and update the coord docs listed above.
* P4 N = 250000000 → the EMMC clock does not follow core_freq. The old hard-code was right by
  coincidence, and the fix remains correct.
* P4 N = 500000000 (or anything ≠ 250 MHz) → the old binary would have mis-clocked WiFi. The fix
  was necessary. Note this in the manifest.
* P4 shows `GET_CLOCK_RATE(EMMC) failed` → vcmbox was unreachable from rpi4-wifi. The driver runs on
  the 250 MHz fallback, so WiFi at core 500 is unproven. Fix the vcmbox lookup before adopting.
* P2 shows core_clk ≠ 500000000 → the firmware did not apply the config (wrong bootfs, or the
  firmware clamped it). The run is void.
* P3 fails with P2 correct → a mini-UART problem at 541. Re-run the same binary at core_freq=250 to
  separate the clock from BT flakiness before blaming the clock.
* P5 fails with P4 correct → run the 250 MHz baseline once (the `setssid=-100` join flake is 1 in 4;
  the netif retries). Only a failure that reproduces at 500 and not at 250 blocks.
* P7 < 8.2 with P8 clean → the gain didn't hold on the committed config. Compare with T2/T4
  (8.39, 8.39) before re-deciding.
* P1 fails → revert the project commit (the config is the only core-clock change), rebuild, and
  investigate.

Rollback: revert project `e70e124` (config only). Devices `27db916` is safe at either clock and can
stay.
