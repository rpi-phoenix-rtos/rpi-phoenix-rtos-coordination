# Pi 4 UART Clock Restoration After Cleanup-Image Regression

Date: `2026-04-17`

## Summary

The April 17 cleanup-image retry proved two things at once:

- real hardware still reaches the `plo` HDMI kernel-jump panel
- the current tracker assumption that plain `115200` is once again a stable
  default UART lane is false

The next bounded fix therefore restores the temporary Pi 4 firmware clock
settings that were active during the earlier HDMI-text milestone:

- `force_turbo=1`
- `core_freq=250`

This is intentionally a debug-oriented observability restoration step, not a
claim that the underlying late-boot blocker is solved.

## Triggering Evidence

### Real hardware UART

Source:

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-173902.log`

Important lines:

- `Loaded 'loader.disk' to 0x8000000 size 0x314cd0`
- `Loaded 'kernel8.img' to 0x200000 size 0xd5e8`
- `Kernel relocated to 0x80000`
- `Device tree loaded to 0x2eff1e00 (size 0xe1f2)`
- `uart: Set PL011 baud rate to 103448.300000 Hz`
- `uart: Baud rate change done...`

Interpretation:

- the cleanup image still boots through the same real firmware path
- the firmware-side UART override is still active on real hardware
- the repository tracker had become too optimistic about `115200` being
  restored end-to-end

### Real hardware HDMI

Source:

- `/var/folders/jt/_gyk57f575q5gl68ltg0_y6w0000gn/T/TemporaryItems/NSIRD_screencaptureui_3WsBjZ/Screenshot 2026-04-17 at 17.40.08.png`

Observed screen:

- brown background
- top-left progress panel
- all three squares lit

Interpretation:

- this is the same `video_markKernelJump()` boundary already recorded on
  `2026-04-12`
- the April 17 result is not a regression back to the old pre-`plo` seam

## Git-History Review

The visible “HDMI text” period coincided with the following temporary
visibility/stability changes:

- `phoenix-rtos-project b6dab61`
  - `project/rpi4b: stabilize UART clock in config.txt`
- `phoenix-rtos-devices 993a8b6`
  - `rpi4b: add HDMI mirroring and userspace heartbeat LED`
- `phoenix-rtos-filesystems f3f90bb`
  - `dummyfs: add HDMI tracing for initialization milestones`

The later cleanup / stabilization series removed both the temporary HDMI
tracing and the temporary clock settings:

- `phoenix-rtos-project 06144ef`
- `phoenix-rtos-devices 540e25b`
- `phoenix-rtos-devices f0f97ae`
- `phoenix-rtos-filesystems 4ad91e3`
- `phoenix-rtos-filesystems 1ae1cbf`

Strongest conclusion:

- the current board result is partly a visibility regression
- restoring the clock settings is the narrowest next move before reintroducing
  broader HDMI-only tracing

## Implemented Change

Updated:

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`

Source repository:

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project`
  - commit: `bdbc261`

Restored:

- `force_turbo=1`
- `core_freq=250`

## Validation

- `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`
  - result: pass
- Pi 4 shell smoke:
  - result: pass
- Pi 4 HDMI smoke:
  - result: pass
- canonical export:
  - result: pass
- FAT-aware SD-image verification:
  - result: pass

Observed QEMU non-regression markers:

- `call: exec go!`
- `go: enter`
- `hal: jump exit el1`
- `A3`
- `KLMconsole: pl011 init done`

## Warnings Surfaced

Real warnings from the April 17 hardware log:

- `[sdcard] vl805.bin not found`
- `[sdcard] pieeprom.upd not found`
- `Failed to open command line file 'cmdline.txt'`
- `gpioman_get_pin_num: pin DISPLAY_DSI_PORT not defined`
- `hdmi_get_state is deprecated`

These warnings were not ignored. They remain real, but they do not match the
current boundary because the board still reaches the `plo` kernel-jump panel.

Process warning during this session:

- two expected manifest filenames from earlier mixed AI sessions were missing;
  that confirmed more tracker drift, not a source-tree build problem

## Exported Image

- path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `60e0aac62028e25c6f409839103e9cc500231855b8542eb579ea29db4f7e2fd7`

## Next Step

- run the next real Pi 4 retry on image `60e0aac6...`
- start with `--profile firmware`
- if the firmware still switches baud, rerun with `--profile postswitch`
- only if that still fails should the next step widen back into explicit
  post-`plo` HDMI breadcrumbs or firmware-DTB-specific kernel probes
