# Work order: stop shipping 1.2 MB/frame through the X socket (a mini-DRI3 for this port)

**Status: NOT STARTED — feasibility spike only.** Everything below is verified against the
sources named; no code changed. Written because the verdict changed: this was recorded three
times as "a real project, not a tuning pass", and the spike shows **three of the four pieces
already exist**.

## What it fixes, and why nothing cheaper will

The owner's standing complaint about the GPU X desktop is that the 3D window animates slowly.
`gl-x11-window-daemon` renders to an FBO, `glReadPixels` into its own memory, then `XPutImage`
**1.2 MB back to the server every frame**, which uploads it into the screen texture:
GPU → CPU → socket → CPU → GPU, per frame.

Measured client frame (105 ms total, ~9.5 fps): `draw 3.1 · read 13.0 · pack 6.6 · put 65.8`.
**Everything except `draw` is that round trip.** Every cheaper avenue has now been measured and
closed:

| tried | result |
|---|---|
| AF_UNIX ring 64 kB → 256 kB | +12%; throughput flat at ~12 MB/s, ring is not the constraint |
| poller thundering herd | refuted — 6 clients → 2 left the gap unchanged (2.839 → 2.785 ms) |
| cached alias for the readback | refuted — uncached DRAM is 1001 MB/s, only 1.3× off cached |
| deferring the present during a transfer | no measurable effect (inside the ±4.5% spread) |
| a `glReadPixels` direct-map fast path | worth ~+20%, and it touches a path all five games use |

Removing the transfer is worth `put + read + pack` ≈ **85 ms of a 105 ms frame**.

## The four pieces — three already exist

1. **Cross-process BO reference — ALREADY WORKS.** The daemon's BO handles are **global**, not
   per-client: `v3d_gpu.c` keeps one `W.bos[]` array, `handle == slot + 1` (`:705`), and
   `pbo_get()` (`:431`) looks the handle up in that table with no client scoping. So the X server
   can already name a BO the GL client created. `V3D_RPC_MMAP_BO` already returns its physical
   address. **No daemon change needed.**
2. **Mesa import — ALREADY EXISTS.** `v3d_resource_from_handle()`
   (`v3d_resource.c:1009`, wired at `:1278`) builds a `pipe_resource` from a `winsys_handle`, and
   `v3d_bo_open_handle(screen, handle, size)` (`v3d_bufmgr.c:339`) builds the `v3d_bo`. Both are
   upstream code we already compile. `DRM_FORMAT_MOD_LINEAR` is accepted, which is what we want —
   the Phoenix gate already forces full-screen RTs to RASTER.
3. **Server-side pixmap wrap — ALREADY EXISTS.** `glamor_set_pixmap_texture(pixmap, texture)` is
   `_X_EXPORT`ed from `glamor/glamor.h:113`. (`glamor_pixmap_from_fd(s)` also exists but needs
   dma-buf, which Phoenix has no equivalent of — do **not** go that way.)
4. **The gap: a Phoenix `winsys_handle` flavour, and a side channel.**
   - `v3d_resource_from_handle` switches on `whandle->type` — `SHARED` (GEM_OPEN by name) and `FD`
     (dmabuf). Neither exists here. Add a Phoenix type that passes the daemon handle straight to
     `v3d_bo_open_handle`, and teach `libv3d-client.c` to register an *externally created* handle
     by calling `MMAP_BO` for its `pa` and mapping it — the client already does exactly that for
     its own BOs at `CREATE_BO` (`:348`/`:408`), so this is the same code on a different trigger.
   - The client must tell the server `{handle, width, height, stride, format}` once, then send
     **damage** per frame instead of pixels.

## Steps, in dependency order

1. `libv3d-client.c`: `phoenix_v3d_open_handle(handle)` → `MMAP_BO`, `mmap(MAP_PHYSMEM, pa)`,
   register in the existing handle table. Verifiable alone: two processes, same handle, compare
   bytes.
2. Mesa: a Phoenix `winsys_handle` type routed to `v3d_bo_open_handle`. Verifiable alone: import a
   BO in process B and `glReadPixels` it.
3. X server: accept `{handle, geometry}`, import, `glamor_set_pixmap_texture()` onto a pixmap,
   composite that pixmap into the window on damage.
4. Client: drop `glReadPixels`/`pack`/`XPutImage`; send the handle once and damage per frame.

## Risk, and the rule for doing it

This touches the **demo-critical** X server, and the current build is verified and gated
(image `7f3de597…`, all six apps). So: implement the shared path as an **addition**, with the
existing `XPutImage` path intact and selected by default until the new one is proven — a failure to
import must fall back, not break the desktop. Keep `gl-x11-window-daemon` able to run either way so
an A/B is one argument, not a rebuild.

Verification is the set that has converged for X work: 0 faults · colours `(77,79,110)` · mirror
check MAD > 40 · fan coverage across four frames inside the 23–54% baseline · **and repeated runs**,
because the run-to-run spread on this stack is ±4.5% (`docs/misc/2026-09-08-glamor-screen-mirror-and-gl-window-rate.md` §30).
