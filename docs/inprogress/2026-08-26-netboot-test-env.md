# Netboot manual-test environment (owner task 3) — 2026-08-26

Prepared for owner manual testing. Goal: a clean, current, **all-pushed (public)** netboot
environment with the X11 GPU fixes included.

## Confirmations (task-3 requirements)

- **No local-only code changes.** Every sibling repo + the coordination repo is
  `ahead-of-publish(org) = 0` (all committed source pushed to github.com/rpi-phoenix-rtos).
  Verified 2026-08-26; the last stray local commit (phoenix-rtos-filesystems `b017513`, an NFS
  debug-print removal) was pushed. Remaining uncommitted files are non-source only: build
  artifacts (bt binaries, gpu-libs, ttyprobe) and the never-push WiFi firmware blobs.
- **Cleanly built (core + X11 GPU).** The netboot boot image (`loader.disk`, kernel) was
  clean-rebuilt from committed source (`--scope core`, diagnostics off) and **boot-verified**.
  The X11 GPU stack (`Xphoenix-glamor-daemon`, `gl-x11-window-daemon`) was rebuilt from committed
  source (with the two glamor fix patches applied by `build-xserver-core.sh`) and boot-verified.
- **X11 GPU fixes included + verified** (Tier-0 item 3): red↔blue swap + vertical flip in
  windowed-GL content — both fixed (patches `tools/x11-port/patches/xorg-server-1.20.14-glamor-*.patch`,
  commits f18886e + 030eed1). HW-validated: the gl-x11-window pinwheel renders upright, centered,
  correct colors (evidence `docs/inprogress/evidence/2026-08-26-x11-FIXED-*.png`).

## What boots

- **Netboot** (card OUT of the Pi): TFTP kernel/loader from `.buildroot/.../rpi4b-bootfs`,
  NFS root = the `fsid=0` export **`/srv/phoenix-rpi4-nfs-gcc16`** (server 10.42.0.1).
- Bring the server up (host): `./scripts/netboot-server-up.sh`.

## How to launch the concurrent-GPU X desktop (the recent work)

At the `(psh)%` prompt after netboot takeover:

```
bash /gpu-x-gpudesk-repro.sh
```

This starts the V3D server (`/sbin/rpi4-v3d`), the glamor GPU-accelerated X server
(`Xphoenix-glamor-daemon`), then a `gpudesk` session: `twm` + a GPU-rendered GL window
(`gl-x11-window-daemon`, spinning pinwheel) + `xclock` + `xcalc`. All GPU work is serialized
through the one V3D daemon. HDMI shows the composited desktop.

(The helper `./scripts/test-cycle-psh-interact.sh --label t --inter-cmd-secs 8 --idle-secs 100 --
"bash /gpu-x-gpudesk-repro.sh"` drives a full power-cycle + capture from the host.)

## Scope note

This env is clean/current/all-pushed for the **core system + the X11 GPU desktop** (the recent
focus). The other showcase apps (Quake ×3, dillo, mc, …) in the export are from prior builds of
the same committed source (unchanged since). A full from-scratch `rebuild-rpi4b-fast.sh
--with-showcase` (or the Docker `--no-cache` clean-build gate) rebuilds *everything* fresh — the
comprehensive "cleanly built" gate — available on request; it is heavier (~30–60 min) and not run
here to avoid disrupting the verified env.

## Known separate issues (not part of this env's fixes)

- Cursor rendering (owner-noted small artifacts on mouse move) — deferred, separate from the
  windowed-GL orientation fix.
- Robustness follow-up: symmetric `glamor_download_boxes` Y-flip for screen-source
  CopyArea/GetImage (no visible issue today).
