# Known-good X server binaries (kept on disk, NOT in git — see D6 on ELF bloat)

| file | Mesa | state |
|---|---|---|
| `Xphoenix-glamor-daemon.mesa-aa916f2f06` | current (`aa916f2f06`) | **current known-good.** 0 faults, desktop renders + animates (mean 102, std 68), verified 2026-09-09 |
| `Xphoenix-glamor-daemon.mesa-e4be116324` | older | previous known-good, 0 faults |
| `libv3d-phoenix.a.mesa-aa916f2f06`, `libGL-…`, `libv3dv-…` | current | the archives the current binary was linked against |

The archives are saved **beside** the binary now, so a known-good state can always be
rebuilt and diffed. Not doing that cost a session: see
`docs/misc/2026-09-08-glamor-screen-mirror-and-gl-window-rate.md` §13–§15.

## ⚠️ There are TWO GPU X servers. Build the right one.

```
build-xfbdev.sh --glamor         -> Xphoenix-glamor          IN-PROCESS winsys
build-xfbdev.sh --glamor-daemon  -> Xphoenix-glamor-daemon   /dev/v3d-srv CLIENT
```

`startx_gpu` starts the `rpi4-v3d` daemon, so it needs the **`-daemon`** build. Staging the
in-process build into that slot means **two processes driving V3D**, which presents as MMU
violations, GPU wedges and a grey or black screen — and looks exactly like a Mesa or driver
regression. The two binaries are within 440 bytes of each other and the filenames differ by one
word, so the mistake is invisible.

`build-xfbdev.sh` now asserts the linked backend matches the requested target
(`v3d_cli_bo` present for `--glamor-daemon`, absent for `--glamor`) and fails the build
otherwise, so this cannot recur silently.
