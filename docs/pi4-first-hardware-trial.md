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

- `475d8d21cdc00d2c2fc79819fe02bdcc946b5ee75329b503198dda7ac16877c3`

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
2. Insert the microSD card into the Pi 4.
3. Connect HDMI.
4. Connect one USB keyboard directly to the Pi 4.
5. Optionally connect Ethernet.
6. Power on the board.
7. Wait at least 60 seconds before classifying a silent result.
8. If text or prompt appears, try:
   - `help`
   - `ps`
   - `ls /`
9. Record the outcome using the template below.

## Expected Positive Signs

Any of these are useful:

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
SHA256: 475d8d21cdc00d2c2fc79819fe02bdcc946b5ee75329b503198dda7ac16877c3
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

## Next-Agent Interpretation Rule

After the first board trial:

- do not jump straight into wide code changes
- classify the result first
- then choose the smallest next step that matches the observed class
