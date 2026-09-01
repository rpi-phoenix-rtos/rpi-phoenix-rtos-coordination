# Full-system SD-boot verification (2026-07-02)

Card: self-flashed full rootfs (X11, quake, editors, all ported SW) on the 64 GB SD, SD-booted
on the Pi via the Linux-netboot self-flash + `test-cycle-netboot.sh --sd-boot` (netboot server
down). Card stayed in the Pi throughout (no manual swap).

## Results

- **SD-boot: WORKS.** DDR50 engaged (`sdcard: UHS-I DDR50 @ 50 MHz DDR (1.8V)`), ext2 root
  mounted first-try, all drivers up (thermal/hwrng/**fb0**/gpio/audio0/usb/genet), psh reached,
  0 faults. `rpi4-sysinfo: devices: console+ ... gpio+ fb0+ audio0+ kbd0 mouse0`.
- **X11: FIXED + WORKS.** Root cause of the user's earlier failure: the `sd` variant did not
  launch `rpi4-fb`/`rpi4-gpio`/`rpi4-audio`/`rpi4-sysinfo` (gated `not in ['sd','nfsroot']` — a
  leftover "keep the sd path minimal" from the #120 bring-up). So `/dev/fb0` never existed →
  Xphoenix's fbdev backend failed `cannot open /dev/fb0: ENOENT` → "no screens found". **Fix:**
  changed the gate to `!= 'nfsroot'` (run on sd too, matching netboot) in
  `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`. Re-flashed the
  boot partition only (loader.disk, 64 MiB — ext2 untouched) via the Linux self-flash.
  **Verified:** `rpi4-fb ... registered /dev/fb0`, then `startx` → `[fbdev] /dev/fb0: 1920x1080
  bpp=32` (opened OK), Xphoenix up, kbd0+mouse0 active, and the HDMI snapshot shows **xeyes
  rendering** (artifacts/hdmi/*sdboot-x11*). X11 is confirmed on SD-boot.
- **Quake: still does NOT run on SD-boot — distinct from the fb0 issue, NOT yet fixed.** With
  `/dev/fb0` now present, `quake` (and `cd /usr/share/quake; quake`, `-basedir …`) launches but
  produces **zero UART output and no render**, and psh does not return (hangs early, no fault).
  This is unlike the known NFS-root state where GLQuake *rendered* demos (input was the only gate,
  kbd0-EBUSY). So there is a separate SD-boot-specific early hang in quake before its first
  stdout. Needs a focused GLQuake session: add early Sys_Printf/stderr flushing to find where it
  blocks (video/GL init? V3D power? fb0 scanout vs the pl011-tty fbcon owning the same surface?
  audio0 open?). Likely candidates: fb0/fbcon co-ownership (X explicitly disables fbcon via
  `[fbdev] HDMI console fbcon mode -> 1`; GLQuake may not), or the V3D scanout path. Deferred as a
  capstone item (#29-adjacent).

## Boot-config change committed?

Not yet — the `user.plo.yaml` gate change is in the working tree (sibling
phoenix-rtos-project), validated on HW. Commit pending (see session commits).

## How to re-run interactively

`./scripts/live-test-rpi4b.sh --sd-boot` (netboot server down → SD-boot; live UART + HDMI
preview; type on the USB keyboard). Added `--sd-boot` to that script this session.
