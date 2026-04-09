# Pi 4 First Hardware Trial

This document is the focused operator checklist for the first real Raspberry Pi
4 boot of the current Phoenix image with:

- HDMI text console
- staged USB host
- intended USB-keyboard shell path

Use it together with
[manual-operator-instructions.md](/Users/witoldbolt/phoenix-rpi/docs/manual-operator-instructions.md),
not instead of it.

## Current Artifact

Use this image:

- [artifacts/rpi4b/rpi4b-sd.img](/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img)

Current SHA-256:

- `6d6b4d7dd84f237f3e8dab1764f8be34b29b4e4d46d6f92ad30aee1869a2acdc`

This image supersedes the earlier Pi 4 trial images that used the temporary
firmware-default low-placement experiment:

- no custom `armstub`
- no longer-coherent low `ADDR_PLO=0x200000` placement assumptions

This image now intentionally uses:

- `kernel_address=0x40080000`
- `boot_load_flags=0x1`
- `armstub=phoenix-armstub8-rpi4.bin`
- custom Pi 4 armstub EL3 preparation:
  - local timer control / prescaler
  - `CNTFRQ_EL0 = 54000000`
  - GIC group-1 setup through the ARM-visible aliases
- structured GPIO42 telemetry protocol:
  - the earlier one-off ACT-LED proofs were removed
  - the current image now emits numbered pulse groups separated by longer off
    gaps
  - the current checkpoint map is:
    - `1`: armstub primary-core entry
    - `2`: armstub after early timer / GIC preparation
    - `3`: armstub just before the fixed-address jump to `plo`
    - `4`: earliest generic AArch64 `plo` `_start`
    - `5`: `plo` EL3 path selected
    - `6`: `plo` EL2 path selected
    - `7`: `plo` EL1 path selected
    - `8`: `plo` `start_common`
    - `9`: `plo` core-0 branch to `_startc`
  - the goal of the next board trial is no longer “did one probe move?”
  - the goal is “what is the highest completed numbered checkpoint?”
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
- optional Ethernet

Do not assume UART visibility is available.

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
6. Power on the board.
7. Start a high-framerate close-up video before power-on and keep both LEDs in
   frame for at least 20 seconds.
8. Wait at least 60 seconds before classifying a silent result.
9. If text or prompt appears, try:
   - `help`
   - `ps`
   - `ls /`
10. Record the outcome using the template below.

## Expected Positive Signs

Any of these are useful:

- clearly separated numbered ACT-LED pulse groups
- a highest completed checkpoint that can be counted from the video
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
SHA256: e5f8662aca8c859464bed6c23e9742afd196bf1136a09f453e9c975e06b6441c
Board revision:
Display:
Keyboard:
Ethernet attached: yes/no

Observed class:
Power-on time observed:

HDMI result:
- no signal / brief flash / stable picture
- early panel seen: yes/no
- black text console seen: yes/no
- prompt seen: yes/no

ACT LED result:
- attach or summarize the LED video
- highest completed checkpoint group:
- final LED state after the pulse groups:

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

Count complete pulse groups separated by the longer off gaps.

- highest completed `1` only:
  armstub started but did not reach the post-setup armstub checkpoint
- highest completed `2`:
  armstub reached early timer / GIC setup but not the final pre-`plo` handoff
- highest completed `3`:
  armstub reached the final fixed-address jump point but `plo` stage `4` was
  not observed
- highest completed `4`:
  earliest generic AArch64 `plo` `_start` was entered
- highest completed `5`, `6`, or `7`:
  `plo` reached exception-level dispatch and selected EL3, EL2, or EL1
- highest completed `8`:
  `plo` reached `start_common`
- highest completed `9`:
  `plo` reached the core-0 branch to `_startc`

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
