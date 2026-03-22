# 2026-03-22: Pi 4 first manual HDMI plus USB-keyboard trial scope

## Scope

Close `STEP-0395` by making the next move explicit:

- use the current exported SD image on the real Raspberry Pi 4 board
- treat the next stronger validation lane as manual hardware execution
- avoid further speculative code work until board evidence exists

## Outcome

The current pre-hardware state is sufficient for a first real-device trial:

- HDMI text-console path present
- USB-host path staged on the Pi 4 image
- exported SD-card image present with recorded checksum

The next bounded improvement should not be new runtime code. It should be a
structured operator-facing trial checklist and result template so the first
board run produces actionable evidence.
