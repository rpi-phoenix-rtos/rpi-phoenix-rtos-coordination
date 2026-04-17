# Current Step

## Metadata

- Step ID: `STEP-0490`
- Title: Await the next Pi 4 retry on the restored clock-stabilization image
- Status: `ready`
- Date: `2026-04-17`
- Milestone / phase: `Phase 1`

## Objective

- verify whether restoring the temporary Pi 4 firmware clock settings recovers
  the lost late-boot observability on real hardware
- check whether the April 17 cleanup-image regression was primarily a serial /
  visibility regression rather than a deeper pre-userspace boot regression
- gather the next real evidence before making broader userspace or DTB changes

## Scope

In scope:
- one new real-device Pi 4 retry on the restored-clock image
- HDMI observation
- UART capture
- fallback `postswitch` capture if the firmware still overrides the restored
  configuration

Out of scope:
- reintroducing legacy GPIO42 stage telemetry
- broad manual HDMI tracing rollback before the next real result
- unrelated pre-`plo` boot changes

## Acceptance Criteria

- the restored-clock image is flashed and tried on real hardware
- the retry clearly answers one of:
  - `115200` UART becomes readable past the old firmware cut-off
  - the firmware still switches baud and a `postswitch` capture is required
  - HDMI regains the earlier text visibility
  - the board still stops at the same brown three-square panel
- the next blocker is classified from that real evidence instead of from stale
  cleanup-image assumptions

## Validation Plan

- flash `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- capture UART first with:
  - `./scripts/capture-rpi4b-uart.sh --profile firmware --device /dev/cu.usbserial-XXXX --label pi4-firmware`
- if the log still ends at the firmware baud-switch line, rerun with:
  - `./scripts/capture-rpi4b-uart.sh --profile postswitch --device /dev/cu.usbserial-XXXX --label pi4-postswitch`
- summarize with:
  - `./scripts/summarize-rpi4b-uart-log.py /path/to/log`
- capture an HDMI screenshot or photo if the screen changes

## Rollback / Baseline

- restored-clock test image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `60e0aac62028e25c6f409839103e9cc500231855b8542eb579ea29db4f7e2fd7`)
- prior cleaned-image baseline:
  `eff8ca6193da33baeeb5af6c7fee3deefbd6a6243388b5cc708544bab2dd210e`
- restoration step recorded in:
  `manifests/2026-04-17-pi4-uart-clock-restoration.md`

## Notes

- the April 17 cleanup-image retry still reached the `plo` kernel-jump panel
  but falsified the “plain `115200` primary lane” assumption
- the restored test image now reintroduces:
  - `force_turbo=1`
  - `core_freq=250`
  in the Pi 4 `config.txt`
- QEMU remains non-regression green on the restored image:
  - Pi 4 shell smoke: pass
  - Pi 4 HDMI smoke: pass
