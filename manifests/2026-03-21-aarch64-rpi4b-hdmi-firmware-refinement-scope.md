# 2026-03-21: scope the smallest real-hardware-oriented Pi 4 HDMI refinement

## Scope

- Step: `STEP-0286`
- Goal: choose the smallest firmware- or operator-facing HDMI refinement for
  the first real Pi 4 board trial

## Inputs Reviewed

- current Pi 4 project firmware staging config:
  - `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`
- current project notes for the no-UART lab
- current Raspberry Pi documentation:
  - `config.txt`
  - legacy `config.txt` HDMI and framebuffer options

## Decision

The smallest next refinement should be:

- update the Pi 4 project `config.txt` to add:
  - `hdmi_force_hotplug=1`
  - `disable_overscan=1`

## Why This Is The Smallest Useful Change

- `hdmi_force_hotplug=1` is documented by Raspberry Pi as forcing HDMI output
  mode even if hotplug detection does not assert
- `disable_overscan=1` removes firmware-added overscan so the current
  upper-left `plo` marker is less likely to be cropped on a TV-like display
- both are firmware-stage settings, so they directly match the current goal:
  improve the first real-device HDMI trial without widening into runtime
  display work

## Why Not Use A Broader Safe-Mode Bundle Yet

Rejected for now:

- `hdmi_safe=1`

Reason:

- it is broader than needed
- it changes multiple settings at once
- it intentionally adds overscan, which works against the current marker
  visibility goal
- it is better kept as a later fallback if the narrower refinement still fails

## Out Of Scope

- fixed explicit `hdmi_group` / `hdmi_mode` selection
- runtime framebuffer console support
- real-hardware execution in this step

## Next Step

- implement `STEP-0287`: update the Pi 4 project `config.txt`, rebuild the Pi 4
  artifacts, verify the staged firmware config, and update the operator
  runbook with the new expected first-board HDMI behavior
