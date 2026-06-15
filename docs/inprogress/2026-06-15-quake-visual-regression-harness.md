# Quake Pi4-vs-host visual regression harness — design + status (2026-06-15)

Goal: systematic, automated, **unattended** comparison of Quake's visual output
on Pi4 (V3D) vs a host reference (llvmpipe software GL), to localise the small
visual bugs (objects/parts rendering black instead of textured; broken text).
Not pixel-perfect — the deliverable is clear, per-frame **evaluation criteria**
for what's still wrong.

## Approach

**Determinism via fixed timestep.** `host_framerate <dt>` (host.c, pre-existing)
forces a constant frametime per rendered frame regardless of wall-clock, so a
demo advances deterministically. With `r_particles 0` (cross-arch `rand()` —
libphoenix vs glibc — would otherwise desync particles), **frame N shows the
same demo moment on every machine**. The same demo (demo1...) on both → directly
comparable frame sets.

**Capture mode (DONE, committed external/quakespasm in gl_screen.c).** New cvars:
`scr_capture` (dump every Nth in-game demo frame), `scr_capture_max` (auto-quit
after N), `scr_capture_dir` (output dir). `SCR_CaptureTick` (SCR_UpdateScreen,
pre-swap) dumps the composed frame to `cap_NNNN.tga` (self-contained 24-bit TGA
writer — no libpng, no com_gamedir dep). Driven by an `id1/autoexec.cfg`:
```
host_framerate 0.05
r_particles 0
scr_capture_dir "/nfstest/id1"   # Pi: straight onto the NFS export
scr_capture 5
scr_capture_max 120
```
**Validated on Pi**: 120 shots, demo timestamps exactly 0.25 s apart (5 frames ×
0.05), clean auto-quit — fully deterministic.

## Transport: Pi → host

Pi writes `cap_NNNN.tga` to `/nfstest/id1` = `/srv/phoenix-rpi4-nfs/id1` on the
host (the host reads them directly — no separate copy). The host capture run
writes to a separate local dir to avoid collision.

**BLOCKER (open): NFS large-write bug.** A large file write to the NFS export
stalls after the first 4 KB block and floods `getservbyport: not implemented`.
Root: libnfs binds a privileged source port (socket.c:1515, ports 512–1023); a
write-triggered **reconnect** re-binds and loops over the range (the prior port
is busy). Reads are unaffected (only 2× getservbyport at mount). The connection
breaks *during* the write — leading hypothesis is the WRITE RPC failing/timing
out (caches-off slow write path) → libnfs autoreconnect → reserved-port flood.
This is a real NFS-write-stability gap (also affects saves/screenshots), worth
fixing for the broader NFS work. Alternatives if the write fix is deep: a TCP
sink (Pi sends TGAs to a host `nc`/python listener, bypassing the NFS write).

## Host reference run (planned)

Host can build quakespasm (SDL2 2.32.10 + llvmpipe `swrast_dri.so` present).
Headless options: `Xvfb` (needs `sudo apt install xvfb`) + `LIBGL_ALWAYS_SOFTWARE=1`,
or SDL `offscreen`/EGL surfaceless. Build the same capture-enabled tree, run the
same demo + autoexec (no `scr_capture_dir` → local id1), collect `cap_*.tga`.

## Comparison script (planned, host Python via uv venv)

Per frame pair `(pi_i, host_i)`: load TGAs → numpy; metrics:
- **SSIM** + mean-abs-error (overall similarity).
- **Black-texture signature**: pixels near-black on Pi but textured on host →
  count + bounding boxes (the "object rendered black" bug). The headline metric.
- **HUD/text region** cropped + compared separately (the text-rendering bug).
Output: per-frame CSV + a montage (side-by-side + diff heatmap) of the worst
pairs. Evaluation criterion = #black-texture pixels and text-region SSIM trend.

## Status / next

- [x] Deterministic capture mode (committed, Pi-validated).
- [ ] Unblock transport: fix the NFS large-write (preferred — NFS stability) OR
      TCP sink.
- [ ] Host headless capture run (Xvfb/EGL + llvmpipe).
- [ ] Comparison script + first report.
