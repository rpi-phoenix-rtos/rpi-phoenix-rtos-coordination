# Pi 4 First Hardware Trial

This document is the focused operator checklist for the first real Raspberry Pi
4 boot of the current Phoenix image with:

- HDMI text console
- staged USB host
- intended USB-keyboard shell path

Use it together with
[manual-operator-instructions.md](/Users/witoldbolt/phoenix-rpi/docs/manual-operator-instructions.md),
not instead of it.

Current strong recommendation:

- when a USB-TTL cable is available, use UART as the primary observability lane
- the old structured GPIO42 Phoenix telemetry is no longer part of the current
  stabilized image
- LED video is now optional auxiliary evidence, not the primary decode channel

## Current Artifact

Use this image:

- [artifacts/rpi4b/rpi4b-sd.img](/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img)

Current SHA-256:

- `eff8ca6193da33baeeb5af6c7fee3deefbd6a6243388b5cc708544bab2dd210e`

This image supersedes the earlier Pi 4 trial images that still halted in the
late custom armstub seam on an empty `kernel_entry32` slot.

This image now intentionally uses:

- `armstub=phoenix-armstub8-rpi4.bin`
- a relocatable `kernel8.img` trampoline instead of a raw direct copy of the
  high-linked `plo` image
- low-memory firmware-visible placement:
  - `kernel8.img` loaded by firmware to `0x00200000`
  - `loader.disk` loaded by firmware to `0x08000000`
- firmware UART options:
  - `enable_uart=1`
  - `uart_2ndstage=1`
  - `init_uart_baud=115200`
- kernel PL011 hardcoding at `115200`
- no legacy Phoenix GPIO42 stage-code telemetry in:
  - `armstub`
  - earliest `plo`
  - kernel `_start`
  - `dummyfs`

Historical note:

- the detailed GPIO42 stage-code map and the dual-profile `firmware` /
  `postswitch` serial workflow described later in this file are preserved as
  historical context from earlier diagnostic images
- they are not the primary expectation for the current stabilized image
- custom Pi 4 armstub EL3 preparation:
  - local timer control / prescaler
  - `CNTFRQ_EL0 = 54000000`
  - GIC group-1 setup through the ARM-visible aliases
- handoff hardening at the stage-`3 -> 5` seam:
  - the late custom armstub path now restores the Raspberry Pi firmware
    contract instead of dereferencing the target image before the branch
  - the armstub now reads:
    - `dtb_ptr32` into the temporary DTB handoff register
    - `kernel_entry32` into the branch target register
  - if `dtb_ptr32 == 0`, the armstub now falls back to the original entry
    `x0` value for the DTB pointer
  - if `kernel_entry32 == 0`, the armstub now falls back to the real observed
    firmware relocation target `0x80000` instead of halting
  - after the armstub jumps to the effective kernel entry, the new trampoline
    now emits:
    - `TR0`
    - `TR1`
    - `TR2`
    - `TR3`
  - that trampoline now copies the embedded `plo` payload to `0x40080000`,
    preserves the DTB pointer in `x0`, and only then branches to the real
    high-linked `plo`
  - the trampoline now intentionally keeps the firmware-programmed post-switch
    PL011 rate instead of reprogramming UART again
  - the armstub now keeps only `dsb sy; isb` immediately before the final
    branch
  - the firmware DTB pointer is now preserved into earliest generic `plo` and
    stored in `hal_firmwareDtb` at `start_common`
  - the seam-stage codes are still emitted twice with an extra long gap to
    make the next phone-video decode less ambiguous
  - earliest `plo` entry still uses:
    - stage `5` inline in tiny veneer at raw branch target
    - stage `6` inline at first instruction of old generic `_start` body
- compact GPIO42 telemetry protocol:
  - the earlier one-off ACT-LED proofs and later count-based pulse groups were
    removed
  - each stage now emits:
    - one sync pulse
    - then `5` fixed-width bits, MSB first
    - short on-time = `0`
    - long on-time = `1`
    - then one longer off gap before the next stage
  - this keeps the protocol denser than counting long pulse groups while still
    staying decodable from a high-framerate video
  - the current checkpoint map is:
    - `1` / `00001`: armstub primary-core entry
    - `2` / `00010`: armstub after early timer / GIC preparation
    - `3` / `00011`: armstub just before the firmware-slot jump to `plo`
    - `23` / `10111`: late seam entered
    - `24` / `11000`: `dtb_ptr32` loaded
    - `25` / `11001`: `kernel_entry32` loaded
    - `26` / `11010`: `kernel_entry32` was nonzero
    - `29` / `11101`: DTB fallback to entry `x0`
    - `30` / `11110`: kernel-entry fallback to `0x80000`
    - `4` / `00100`: armstub branch imminent after firmware-slot handoff prep
    - `5` / `00101`: earliest Pi 4 entry veneer at branch target
    - `6` / `00110`: first instruction of old generic `_start` body
    - `7` / `00111`: after clearing `x0..x7`
    - `8` / `01000`: after clearing `x8..x15`
    - `9` / `01001`: after clearing `x16..x23`
    - `10` / `01010`: after clearing `x24..x30`
    - `11` / `01011`: after `dsb sy` / `isb`
    - `12` / `01100`: after `mrs currentEL`
    - `13` / `01101`: `start_el3`
    - `14` / `01110`: `start_el2`
    - `15` / `01111`: `start_el1`
    - `16` / `10000`: EL3 path complete, before `start_common`
    - `17` / `10001`: EL2 path complete, before `start_common`
    - `18` / `10010`: EL1 path complete, before `start_common`
    - `19` / `10011`: `start_common`
    - `20` / `10100`: after stack initialization
    - `0` / `00000`: EL2 exception trap during the seam
  - the goal of the next board trial is to identify the highest completed
    stage code, not to count approximate pulse envelopes
- Pi 4 `plo` GIC base aliases:
  - `0xff841000`
  - `0xff842000`

Do not reuse older on-card `config.txt` edits. Reflash the whole image instead.

## Hardware Setup

Attach:

- Raspberry Pi 4 Model B
- microSD card flashed with the image above
- HDMI display
- USB keyboard
- USB-TTL adapter if available
- optional Ethernet

Current UART wiring:

- adapter RX -> Pi Pin 8 / `GPIO14` / `TXD0`
- adapter TX -> Pi Pin 10 / `GPIO15` / `RXD0`
- adapter GND -> Pi Pin 6 / `GND`
- 3.3 V TTL only

## Execution Checklist

1. Flash the SD image using the existing macOS workflow from
   [manual-operator-instructions.md](/Users/witoldbolt/phoenix-rpi/docs/manual-operator-instructions.md).
   If you want a prefilled report file first, run:
   - [scripts/create-rpi4b-first-trial-report.sh](/Users/witoldbolt/phoenix-rpi/scripts/create-rpi4b-first-trial-report.sh)
   Before writing the card, run:
   - [scripts/verify-rpi4b-sdimg.sh](/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh)
   If helpful, print the exact flash commands with:
   - [scripts/print-rpi4b-macos-flash-commands.sh](/Users/witoldbolt/phoenix-rpi/scripts/print-rpi4b-macos-flash-commands.sh) `diskN`
2. Insert the microSD card into the Pi 4.
3. Connect HDMI.
4. Connect one USB keyboard directly to the Pi 4.
5. Optionally connect Ethernet.
6. If a USB-TTL cable is available, start UART capture before power-on:
   - [capture-rpi4b-uart.sh](/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh) `--list`
   - firmware-side evidence:
     [capture-rpi4b-uart.sh](/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh) `--profile firmware --device /dev/cu.usbserial-XXXX --label pi4-firmware`
   - current higher-value post-switch run for the active kernel-entry boundary:
     [capture-rpi4b-uart.sh](/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh) `--profile postswitch --device /dev/cu.usbserial-XXXX --label pi4-postswitch`
   - if you want earlier EEPROM messages than `uart_2ndstage`, enable
     `BOOT_UART=1` in the Raspberry Pi EEPROM on a known-good Raspberry Pi OS
     card first
7. Power on the board.
8. Start a high-framerate close-up video before power-on and keep both LEDs in
   frame for at least 60 seconds.
   If convenient, record 90 seconds so the full compact `1..26` seam plus the
   later `plo` stages still
   fits even if the board progresses farther than expected.
   The current host-side decode workflow is:
   - [scripts/analyze-rpi4-actled-video.py](/Users/witoldbolt/phoenix-rpi/scripts/analyze-rpi4-actled-video.py)
   - [scripts/interpret-rpi4-actled-analysis.py](/Users/witoldbolt/phoenix-rpi/scripts/interpret-rpi4-actled-analysis.py)
   - [scripts/rpi4_actled_probe_layout.py](/Users/witoldbolt/phoenix-rpi/scripts/rpi4_actled_probe_layout.py)
   Recommended use:
   - `scripts/analyze-rpi4-actled-video.py --pretty /path/to/IMG_xxxx.mov > /tmp/pi4-led.json`
   - `scripts/interpret-rpi4-actled-analysis.py /tmp/pi4-led.json`
   Interpretation rule:
   - the initial ACT LED chatter during firmware SD-card reads is not Phoenix
     telemetry
   - ignore that preamble unless it becomes part of a later valid contiguous
     Phoenix stage run
9. Wait at least 90 seconds before classifying a silent result on the current
   firmware-entry-contract image.
10. After the trial, summarize the UART log if one was captured:
   - [summarize-rpi4b-uart-log.py](/Users/witoldbolt/phoenix-rpi/scripts/summarize-rpi4b-uart-log.py) `/path/to/log`
   - if a firmware-profile log stops at the PL011 baud-switch line, the helper
     now tells you to rerun with `--profile postswitch`
11. If text or prompt appears, try:
   - `help`
   - `ps`
   - `ls /`
12. Record the outcome using the template below.

## Expected Positive Signs

Any of these are useful:

- early EEPROM bootloader UART output
- firmware second-stage UART output
- later Phoenix `plo`, kernel, or shell UART output
- trampoline UART breadcrumbs `TR0..TR3`
- clearly separated ACT-LED stage-code bursts
- a highest completed checkpoint code that can be decoded from the video
- visible top-left early panel from `plo`
- black background with white text
- readable Phoenix boot output
- visible `(psh)%` prompt
- keyboard input changing the prompt state
- successful `help` output

## Failure Classes

Use one primary class:

- `firmware-load`
  no visible HDMI output and no sign of repeated later-stage behavior
- `early-boot`
  only the early panel appears, or visible output stops before runtime text
- `runtime-no-input`
  runtime text or prompt appears, but keyboard input has no visible effect
- `runtime-shell`
  prompt appears and at least one command works
- `reboot-loop`
  repeated visible restart pattern
- `unknown`
  ambiguous result

## Result Template

Copy this block into the next report or chat message:

```text
Pi 4 first hardware trial
Image: artifacts/rpi4b/rpi4b-sd.img
SHA256: 4d9daf70168d6990e7525d0c0accda4a8a1ffed0a5fe62432aab4dcff8e70217
Board revision:
Display:
Keyboard:
Ethernet attached: yes/no
USB-TTL adapter:
Serial device path:
BOOT_UART enabled in EEPROM: yes/no/unknown

Observed class:
Power-on time observed:

UART result:
- connected: yes/no
- capture log path:
- earliest visible output:
- latest visible output:
- summary helper result:

HDMI result:
- no signal / brief flash / stable picture
- early panel seen: yes/no
- black text console seen: yes/no
- prompt seen: yes/no

ACT LED result:
- attach or summarize the LED video
- highest completed checkpoint code:
- final LED state after the last visible stage burst:

Keyboard result:
- no visible effect / partial / full
- keys tried:

Command results:
- help:
- ps:
- ls /:

LED / reboot behavior:

Additional notes:
```

## Current LED Telemetry Interpretation Rule

Decode each stage burst as:

- one sync pulse
- then `5` bits MSB-first
- short on-time = `0`
- long on-time = `1`

Current stage meanings:

- highest completed `1`, `2`, or `3`:
  still in the custom armstub path before generic `plo` runs
- highest completed `4`:
  armstub completed the firmware-slot handoff prep and is about to branch
- highest completed `23`, `24`, `25`, or `26`:
  use the firmware-slot seam map to pinpoint the exact failing instruction
  band before the branch
- highest completed `24`:
  the armstub loaded `dtb_ptr32`, so the next live boundary is the
  `kernel_entry32` read itself
- highest completed `25`:
  the armstub loaded `kernel_entry32`, so the next live boundary is the
  nonzero-check or fallback-selection path
- highest completed `26`:
  the armstub saw a nonzero `kernel_entry32`, so the next live boundary is the
  final branch-preparation path into `plo`
- highest completed `29`:
  the armstub fell back to the original entry `x0` for the DTB pointer
- highest completed `30`:
  the armstub fell back to `0x80000` for the kernel entry and the next live
  boundary is the final branch into the relocatable trampoline
- highest completed `0`:
  the armstub took an EL2 exception during the dense late seam
- highest completed `5`:
  earliest Pi 4 `plo` entry veneer at the branch target was entered
- highest completed `6`:
  first instruction of old generic `plo _start` body was reached
- highest completed `7`, `8`, `9`, or `10`:
  failure is inside the general-purpose register-clearing block
- highest completed `11`:
  failure is after `dsb sy` / `isb` but before or during `mrs currentEL`
- highest completed `12`:
  `currentEL` was sampled but the chosen EL-path body was not reached
- highest completed `13`, `14`, or `15`:
  `plo` selected EL3, EL2, or EL1 respectively
- highest completed `16`, `17`, or `18`:
  the chosen EL-path body ran to its pre-`start_common` boundary
- highest completed `19`:
  `plo` reached `start_common`
- highest completed `20`:
  `plo` passed stack setup
- highest completed `21`:
  `plo` reached the core-0 branch to `_startc`
- highest completed `22`:
  `plo` reached the unexpected-EL trap path

## Next-Agent Interpretation Rule

After the first board trial:

- do not jump straight into wide code changes
- classify the result first
- then choose the smallest next step that matches the observed class

## Next Step By Observed Class

- `firmware-load`
  Recheck SD-image flashing, firmware file presence, and visible power-on
  behavior before touching Phoenix runtime code.
- `early-boot`
  Focus the next step on the earliest visible transition that failed:
  firmware, `plo`, or kernel-to-runtime handoff.
- `runtime-no-input`
  Keep the next step on the Pi 4 PCIe plus VL805 plus xHCI keyboard path, not
  on the HDMI text path.
- `runtime-shell`
  Treat the bring-up goal as met and move to small follow-up interaction or
  stability tests.
- `reboot-loop`
  Focus first on the repeating visible phase and on capturing more precise boot
  timing before changing multiple subsystems.
- `unknown`
  Improve observation first, then choose the smallest subsystem-specific step.
