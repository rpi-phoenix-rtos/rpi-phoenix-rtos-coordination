# Work order: stop shipping 1.2 MB/frame through the X socket (a mini-DRI3 for this port)

**Status: steps 0 and 2 DONE and verified on hardware. Steps 3-4 (X server, client) not started.** Everything below is verified against the
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

1. **Cross-process BO reference — ALREADY WORKS, and now TESTED (`tools/boshare-probe`).**
   Measured on hardware: `parent created handle=4 pa=0x29420000` → `child MMAP_BO -> pa=0x29420000
   SAME` → `parent read child's pattern: MATCH (0/65536 bytes wrong)`. A second process maps a handle
   it did not create, gets the same physical pages, and writes are visible **both** directions. The daemon's BO handles are **global**, not
   per-client: `v3d_gpu.c` keeps one `W.bos[]` array, `handle == slot + 1` (`:705`), and
   `pbo_get()` (`:431`) looks the handle up in that table with no client scoping. So the X server
   can already name a BO the GL client created. `V3D_RPC_MMAP_BO` already returns its physical
   address. **No daemon change needed.**
2. **Mesa import — ALREADY EXISTS, and its daemon-side prerequisites are now TESTED.**
   `v3d_bo_open_handle()` makes exactly **one** daemon call, `DRM_IOCTL_V3D_GET_BO_OFFSET` — no
   GEM_OPEN, no dmabuf — and then asserts the offset is non-zero. Verified for a *foreign* handle:
   `child GET_BO_OFFSET(handle=2) rc=0 gpuva=0x2491000 (parent gpuva=0x2491000) SAME+NONZERO`. The
   CPU map it needs later is `MMAP_BO`, also verified. So this function should work here unchanged. `v3d_resource_from_handle()`
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

## Ruled out: MIT-SHM, the standard shortcut

Before designing a channel, the obvious question is whether X's own shared-memory
extension would capture most of the win with no new protocol —  `XShmPutImage`
would remove exactly the term that hurts (`put`, 65.8 ms of a 105 ms frame) while
leaving the server's upload alone. **It is not available here**, on two counts:

* libphoenix has no `sys/shm.h` — no SysV `shmget`/`shmat`.
* `MITSHM` is `/* #undef */` in the X server's generated `include/xorg-server.h`,
  i.e. the extension is not compiled in.

The fd-passing variant (`ShmAttachFd` + `memfd_create`) is closer to reachable —
the kernel does have `posix/fdpass.c` — but it still needs `memfd_create` and the
extension enabled, which is more work than the BO path that is already proven.

## The channel: an X property read from the DDX

The remaining design question for step 3 was how the client names its buffer to the
server. A full X extension is the "proper" answer and is heavy. The cheap and
legitimate alternative: the client sets a property (`_PHOENIX_V3D_BO` =
`{handle, w, h, stride}`) on its own window, and the DDX — which runs inside the
server process — reads it with `dixLookupProperty()`. That is core dix API, needs
no protocol extension, and solves the binding problem the launcher-argv idea could
not: it associates the buffer with a specific *window* at runtime.

Note what the server then has to do with it. Without a compositor a window's pixels
live in the screen pixmap, so the server cannot simply adopt the client's buffer as
the window's storage; it must **blit** from the shared texture into the window's
region of the screen pixmap on damage. That is a GPU-to-GPU blit rather than a
1.2 MB CPU round trip, which is exactly the win — but it is real work in the
glamor/DDX layer, and it is where the demo-critical risk sits.

## Steps, in dependency order

**The whole server-side chain is now proven in isolation** (`tools/v3d-driver-port/gl_bo_import.c`,
HW): `daemon handle → resource_from_handle → st_context_teximage → GL texture`, with the imported
texture's FBO **complete** and a GL readback matching the pattern written through the raw physical
mapping, `0/16384 bytes wrong`, **no R/B swap**. Since `glamor_set_pixmap_texture()` takes exactly a
texture name, step 3 is reduced to that one call plus a way to carry `{handle, w, h, stride}`.

Two findings worth keeping: an imported buffer is **renderable** (FBO complete), not merely
samplable — so the server could render into a client's buffer; and the absence of an R/B swap here
says this stack's documented BGRA/RGBA seam lives in glamor's own render path, not in the import.


**Step 0 — DONE.** Cross-process sharing verified by `tools/boshare-probe`: same `pa`, same non-zero
`gpuva`, coherent both directions (pieces 1 and 2 above). Everything the daemon must supply for the
import is confirmed on hardware.

**Step 1 is dropped as written.** It proposed a `libv3d-client` wrapper whose verification was
"two processes, same handle, compare bytes" — which the probe already did through the raw RPC. A
wrapper with no caller adds nothing; write it when step 3 needs it.

**Step 2 — LANDED, mesa `3b339c93a07`.** `WINSYS_HANDLE_TYPE_SHARED` now routes, under
`__phoenix__`, to a new `v3d_bo_open_phoenix_handle()` that supplies the mutex
`v3d_bo_open_handle()` requires. Size comes from `whandle->stride * tmpl->height0` — the region the
caller intends to use — since the daemon's size is unreachable through the DRM ioctl surface
(`drm_v3d_mmap_bo` has no size field) and `winsys_handle` has none either; a larger real BO only
makes the offset-overflow check stricter, which is the safe direction.

**Step 2 is EXERCISED and PASSES** (`tools/v3d-driver-port/gl_bo_import.c`): a BO created outside
Mesa through the raw daemon RPC, filled through its `MAP_PHYSMEM` view, imported with
`resource_from_handle(TYPE_SHARED)`, then mapped *through the pipe context* and compared —
`mapped stride=256 (raw stride=256)`, `compare MATCH (0/16384 bytes wrong)`, 0 faults. The stride
agreeing matters as much as the bytes: `v3d_setup_slices()` accepted the caller's stride for an
imported linear resource instead of recomputing a different layout, which is what step 3 will rely
on. Build it with `GL_SMOKE_SRC=gl_bo_import.c python3 tools/v3d-driver-port/build-gl-smoke-daemon.py`
(daemon flavour only — the in-process winsys would fight the daemon for the GPU).

**Earlier note, now superseded:** Nothing on this port calls `resource_from_handle`, and the case it
repurposes was unreachable anyway (no flink names). Regression-checked rather than assumed: archives
rebuild clean (driver 29/0, core 333/0, gl 325/0) and the glamor desktop is unchanged on HW — 0
faults, colours (77,79,110), mirror MAD 75.55, 10.76 fps.

**➡ Next: exercise it.** A headless daemon-client GL harness that creates a BO through the raw RPC,
imports it via `pscreen->resource_from_handle` with `TYPE_SHARED`, textures from it and reads it
back, comparing against the pattern written through the raw mapping. `tools/v3d-driver-port/`
already has headless GL harnesses (`gl_es_smoke.c`, `gl_frontend_smoke.c`) and
`tools/x11-port/glamor-shim/glamor_phoenix_ctx.c` is a working example of bringing up an st context
as a *daemon* client — between them the harness is mostly assembly rather than new ground. Do that
before step 3, so the import is proven in isolation rather than debugged inside the X server.

**Original framing of step 2, kept for the record:** one *additive* case in
`v3d_resource_from_handle`'s `whandle->type` switch (`v3d_resource.c:1050`, currently only `SHARED`
→ `v3d_bo_open_name` and `FD` → `v3d_bo_open_dmabuf`, neither available here) routing a Phoenix
handle type straight to `v3d_bo_open_handle`. Two details to get right:
  - **`v3d_bo_open_handle` needs a `size`, and `winsys_handle` has no size field** (type, layer,
    plane, handle, stride, array_stride, image_stride, offset, modifier, format). `MMAP_BO` already
    returns the BO size, so the natural source is the client, not the template.
  - The caller must hold `screen->bo_handles_mutex` — `v3d_bo_open_handle` unlocks it at `done:`.
    The existing two cases do this; a new one must too.
  Being additive, it cannot affect the `SHARED`/`FD` paths the five games never take either.

1. `libv3d-client.c`: `phoenix_v3d_open_handle(handle)` → `MMAP_BO`, `mmap(MAP_PHYSMEM, pa)`,
   register in the existing handle table. Verifiable alone: two processes, same handle, compare
   bytes.
2. Mesa: a Phoenix `winsys_handle` type routed to `v3d_bo_open_handle`. Verifiable alone: import a
   BO in process B and `glReadPixels` it.
3. X server: accept `{handle, geometry}`, import, `glamor_set_pixmap_texture()` onto a pixmap,
   composite that pixmap into the window on damage.
4. Client: drop `glReadPixels`/`pack`/`XPutImage`; send the handle once and damage per frame.

## ⚠ Step 4 as written is not implementable, and the ceiling is lower than it looks

Two findings from orienting on the actual files (2026-09-09), before any code was
written. Both change whether this work is worth doing, so they belong above the
implementation notes rather than inside them.

**1. "The client sends the handle" presupposes an export path that does not exist.**
Step 2 gave this port `resource_from_handle` (import). There is no
`resource_get_handle` (export), so a GL client has no way to ask Mesa for the
daemon handle behind a texture it allocated. The way around it is to invert the
allocation: the **client** creates the BO through the raw daemon RPC
(`V3D_RPC_CREATE_BO`), imports it into its own Mesa with `resource_from_handle`,
binds it as a GL texture with `st_context_teximage` and renders into it — so it
knows the handle because it minted it. The server imports the same handle by the
same route. `tools/v3d-driver-port/gl_bo_import.c` already proves every link of
that chain on HW (FBO **complete**, readback `0/16384 bytes wrong`, no R/B swap),
and it proves it for *both* sides, because both sides do the identical import.

The cost of that inversion is the part to be honest about: it makes
`gl_x11_window.c` an **st/gallium** client rather than a GL client, the way
`tools/x11-port/glamor-shim/glamor_phoenix_ctx.c` is. That is a rewrite of the
demo's GL client, not the "drop three calls" that step 4 describes.

**2. The present survives buffer sharing, so the ceiling is ~1.7x, not ~5x.**
Measured on the shipped image: 96.0 ms/frame = draw 2.7 + read 12.5 + pack 6.7 +
put 58.0. Buffer sharing removes read + pack + put, which reads as 96 -> 22 ms.
It does not, because the DDX still has to present the changed rows to
`/dev/fb0`, and the fitted present cost is **1.74 ms + 0.07 ms/row**
(`2026-09-08-glamor-screen-mirror-and-gl-window-rate.md`). A 480-row window is
~35 ms of present per frame either way, and today that cost is hidden inside the
58 ms `put`. So the realistic floor is **~55-60 ms/frame (~17 fps)**, from 96 ms
(~10.4 fps).

That is a real improvement and the design is sound. But it is a rewrite of the two
components the demo depends on — the GL client and the X server's damage path — for
~1.7x, on a bench that cannot test SD boot, against an image the owner has not yet
flashed. **Owner decision, logged in the weekly log's section 1.** Everything needed
to resume is in this file; nothing is half-applied.

## Risk, and the rule for doing it

This touches the **demo-critical** X server, and the current build is verified and gated
(image `7f3de597…`, all six apps). So: implement the shared path as an **addition**, with the
existing `XPutImage` path intact and selected by default until the new one is proven — a failure to
import must fall back, not break the desktop. Keep `gl-x11-window-daemon` able to run either way so
an A/B is one argument, not a rebuild.

Verification is the set that has converged for X work: 0 faults · colours `(77,79,110)` · mirror
check MAD > 40 · fan coverage across four frames inside the 23–54% baseline · **and repeated runs**,
because the run-to-run spread on this stack is ±4.5% (`docs/misc/2026-09-08-glamor-screen-mirror-and-gl-window-rate.md` §30).

## Operational note (cost one Pi cycle)

Staging a probe into the NFS export with `sudo cp` leaves it **root-owned**, and the next
`netboot-server-up` rsync then fails with `failed to set permissions ... Operation not permitted`
and the binary does not run — the symptom is a command that produces *zero* output, which reads
like a crash. Copy as the normal user, or `chown houp:houp` afterwards. Also: `rpi4-v3d` is not a
psh-friendly foreground command; bring the daemon up with `startx_gpu --quit-after N wmaker`, which
tears down X but deliberately leaves the daemon running for reuse.
