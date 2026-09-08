# Known-good X server binaries

`Xphoenix-glamor-daemon.mesa-e4be116324` — sha256 `8e003ab45ef41009…`, built 2026-09-08 02:12.
Renders the Window Maker desktop with **0 faults**; re-verified on hardware repeatedly on
2026-09-08/09. This is what the demo and the showcase reel run.

## ⚠️ Save the archives too, not just the binary

This directory holds a binary that **cannot be rebuilt**. Every attempt to relink the glamor X
server against the current `tools/.gpu-libs/*.a` produces a server that starts, brings up all its
clients, and then shows a grey or black screen with GPU faults — and the cause could not be found,
because the archives this binary was linked against were overwritten in place by later rebuilds.
Three source suspects were cleared by experiment (the DDX, the Mesa `u_vbuf` guard, the winsys), so
the difference is in the build, and there is nothing left to diff it against.

Full account: `docs/misc/2026-09-08-glamor-screen-mirror-and-gl-window-rate.md` §13–§14.

**So when promoting an X daemon to known-good, copy the archives beside it:**

```
cp -p tools/.gpu-libs/libv3d-phoenix.a  artifacts/x11/known-good/libv3d-phoenix.a.<tag>
cp -p tools/.gpu-libs/libGL-phoenix.a   artifacts/x11/known-good/libGL-phoenix.a.<tag>
cp -p tools/.gpu-libs/libv3dv-phoenix.a artifacts/x11/known-good/libv3dv-phoenix.a.<tag>
```

They are ~40 MB together, which is cheap against the cost of a known-good binary that can never
take another change.

Note the same archives render **all four Quakes and SuperTuxKart correctly** — the breakage is
specific to glamor's use of the driver (2D, many small draws, render-to-texture), not a general v3d
regression.
