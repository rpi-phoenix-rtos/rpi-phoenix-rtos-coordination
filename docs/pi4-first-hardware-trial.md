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

- `6b349fe6c2afe11ea0fdeb5d9fc874eb5ae1b990ee83d42c48f10662445875e8`

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
- handoff hardening at the stage-`3 -> 4` seam:
  - the primary armstub path no longer clears `x0..x3` before the fixed-address
    branch
  - the armstub now inserts `dsb sy; ic iallu; dsb sy; isb` immediately before
    the branch to `0x40080000`
  - the armstub now also verifies a deliberate `plo` entry signature at
    `0x40080000 + 0x4` before branching
    - stage `4`: signature verified
    - stage `31`: signature mismatch, halt before branch
  - the armstub now also carries a dense late seam ladder:
    - stage `23`: late seam entered after stage `3`
    - stage `24`: fixed target address loaded
    - stage `25`: first signature word read
    - stage `26`: second signature word read
    - stage `27`: first expected signature constant loaded
    - stage `28`: first compare passed
    - stage `29`: second expected signature constant loaded
    - stage `30`: second compare passed
    - stage `0`: EL2 exception trap during the seam
  - fixed-address Pi 4 entry now uses:
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
    - `3` / `00011`: armstub just before the fixed-address jump to `plo`
    - `23` / `10111`: late seam entered
    - `24` / `11000`: fixed target address loaded
    - `25` / `11001`: first signature word read
    - `26` / `11010`: second signature word read
    - `27` / `11011`: first expected signature constant loaded
    - `28` / `11100`: first compare passed
    - `29` / `11101`: second expected signature constant loaded
    - `30` / `11110`: second compare passed
    - `4` / `00100`: armstub verified `plo` signature at `0x40080000`
    - `5` / `00101`: fixed-address Pi 4 entry veneer at branch target
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
    - `21` / `10101`: core-0 branch to `_startc`
    - `22` / `10110`: unexpected-EL trap path
    - `31` / `11111`: armstub signature mismatch before branch, hard halt
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
   frame for at least 60 seconds.
   If convenient, record 90 seconds so the full compact `1..21` sequence still
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
8. Wait at least 60 seconds before classifying a silent result.
9. If text or prompt appears, try:
   - `help`
   - `ps`
   - `ls /`
10. Record the outcome using the template below.

## Expected Positive Signs

Any of these are useful:

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
SHA256: 6b349fe6c2afe11ea0fdeb5d9fc874eb5ae1b990ee83d42c48f10662445875e8
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
  armstub verified the expected `plo` signature before branching
- highest completed `23`, `24`, `25`, `26`, `27`, `28`, `29`, or `30`:
  use the dense late armstub seam map to pinpoint the exact failing
  instruction band before the branch
- highest completed `31`:
  armstub did not find the expected signature at `0x40080000` and halted
  before the branch
- highest completed `0`:
  the armstub took an EL2 exception during the dense late seam
- highest completed `5`:
  fixed-address Pi 4 entry veneer at raw branch target was entered
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
