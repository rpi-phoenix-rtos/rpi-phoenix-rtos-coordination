# Work order: replace the FlipY size gate with a real scanout predicate (post-demo)

**Status: NOT STARTED — deliberately deferred.** Everything below is analysis; no code changed.
Deferred because it touches the flip path of *every* GL app plus the X desktop, and the demo
goal is currently met with a cut, verified image. Do this when a demo is not imminent, in one
focused go, with the hardware soak listed at the end.

## The defect

`external/mesa/src/mesa/state_tracker/st_atom_framebuffer.c:127-140` forces `Y_0_TOP` when

```c
st->state.fb_orientation == Y_0_BOTTOM && fb->Width >= 1024 && fb->Height >= 768
```

The intent is "this is the full-screen HDMI **scanout-backed** FBO, so render upright straight
into the scanout surface". It tests **size**, not scanout-ness. So *any* offscreen FBO ≥1024×768
is wrongly flipped. Two known victims, each carrying a compensating hack:

* Quake II's 1920×1080 underwater post-process FBO.
* glamor's 1920×1080 X screen pixmap (a plain GL texture — glamor presents via `glReadPixels`
  into a shadow it `write()`s to `/dev/fb0`, so it is **not** scanout-backed).

Because the workaround is per-app, **every future GL port with a large offscreen FBO will render
upside down until someone adds another hunk.** That is the reason to fix it properly.

## The trap that makes the obvious fix wrong

`v3d_bufmgr.c:183` sets `bo->scanout` from the *requested* `create_flags`, which came from the
same size gate. The winsys arbitrates the claim in `ioc_create_bo`
(`sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c:885-897`) and records the
truth per-BO at `winsys:977` — but it writes back only `handle` and `offset` (`winsys:1006-1007`)
and **never clears `c->flags`**. So Quake II's FBO requests scanout, is *refused*, and Mesa still
believes `bo->scanout == true`.

**Any fix built on `rsc->bo->scanout` as it stands tags both resources and fixes nothing.**
Making "honored" distinguishable from "requested" is the load-bearing edit.

Also dead on Phoenix: `struct v3d_resource`'s upstream `renderonly_scanout *scanout`
(`v3d_resource.h:54`) is always NULL — every context passes `ro = NULL`
(`sdl_phoenix_glctx.c:236`, `gl_x11_window.c:213`, `glamor_phoenix_ctx.c:161`, …), so both
renderonly branches are unreachable. And `v3d_phoenix_peek_next_scanout()`
(`winsys:771-775`) is a one-shot valid only between `set_next_scanout()` and the next
`create_bo`, is not per-resource, and is called only from the **Vulkan** paths
(`pl_phoenix_vk_vid.c:588`, `v3dv_harness.c:318`) — not usable here.

## Edit set (PIPE_BIND_SCANOUT marker route)

1. **`v3d_phoenix_winsys.c`** ~:920-922, the claim-refused branch (where `W.next_scanout = 0`
   already happens): also `c->flags &= ~0x2u;`. Safe by construction — the shim runs in-process
   on Mesa's own struct, which Mesa already reads back at `v3d_bufmgr.c:180-181`.
2. **`v3d_bufmgr.c:183`**: set `bo->scanout` from the *returned* `create.flags`, not the pre-call
   local. **Leave `:141` alone** — the BO-cache lookup must keep using the pre-ioctl value.
3. **`v3d_resource.c`** after a successful `v3d_resource_bo_alloc()` (~:970): 
   `if (rsc->bo->scanout) prsc->bind |= PIPE_BIND_SCANOUT;`. Setting it *post*-creation means
   neither `:907` nor `:956` sees it (both test `tmpl->bind`), so it is a pure marker.
   Keep the size gate at `:141-143` as the *request* heuristic.
4. **`st_atom_framebuffer.c`**: delete the `:127-140` block; re-insert **after** the cbufs trim at
   `:192-196`, testing
   `framebuffer.nr_cbufs > 0 && framebuffer.cbufs[0].texture && (framebuffer.cbufs[0].texture->bind & PIPE_BIND_SCANOUT)`.
   The trim already drops NULL/GL_NONE cbufs, so depth-only FBOs need no extra guard, and nothing
   between :127 and :196 consumes `fb_orientation`.
5. **Revert all five compensators as part of the same change** — they are correct only while the
   size gate is:
   * `external/yquake2/src/client/refresh/gl3/gl3_draw.c:389-402` — Quake II un-flip, **unconditional**
     (its own comment at :396 admits it mis-flips at small viewsizes and names this fix).
   * `tools/x11-port/glamor-shim/glamor_phoenix_ctx.c:236` (`PHX_READBACK_FLIP_Y 1`) + block `:271-300`.
   * `glamor_transfer.c` upload flip — 21.1.24 `:82`/`:116-124`, 1.20.14 `:90`/`:124-132`.
   * `glamor_transfer.c` download flip — 21.1.24 `:201`/`:235-242`, 1.20.14 `:209`/`:243-250`.
   * the two `tools/x11-port/patches/xorg-server-*-glamor-screen-upload-yflip.patch` files.

`PIPE_BIND_SCANOUT` is safe as the marker: the only sites testing it are `v3d_resource.c:907`
(same outcome — the Phoenix gate already forces `should_tile = false` at :933-936) and `:956`
(unreachable, `screen->ro == NULL`). `PIPE_BIND_DISPLAY_TARGET` is never tested anywhere and is
set only for `rb->Name == 0`, so it is not a usable marker.

## Hardware verification required (this is why it is not a quick change)

* **BO-cache exposure.** After edit #2 a requested-but-refused BO reports `scanout == false` and
  so becomes **cache-eligible** on free where it previously bypassed the cache
  (`v3d_bufmgr.c:294`). Correct in principle, but this is the reuse path with a documented
  corruption history (`winsys:963-969`, `:269`). **Soak Q2 underwater + Q3.**
* **Double buffering must stay per-resource.** `W.scanout_double` grants 2-3 buffers
  (`winsys:679-687`, `:885-895`), so two full-screen RTs can legitimately be scanout-backed and
  both need `Y_0_TOP`. Do not collapse the test to a single global.
* **Allocation order is load-bearing — possible latent bug.** With the claim arbitration as
  written, a 1920×1080 offscreen FBO created *before* the present RTs would steal scanout buffer
  0. Today the uniform size gate masks the symptom. **Trace Q2's FBO creation order** (not yet
  established).
* **Verifying Quake II underwater needs a diagnostic re-added.** The `gl3_forceunderwater` cvar
  was removed after the stopgap landed, so there is currently no way to reach the underwater path
  without swimming. Re-add it temporarily, verify, remove it again.
* Re-verify all six demo components (5 games + the GPU X desktop), not just Quake II.

## Alternative route

`GL_MESA_framebuffer_flip_y`: set `fb->FlipY` on the scanout FBO from each present layer and
delete the gate entirely — skips edits #3-4 and additionally fixes the residual
render/readback inconsistency (`st_cb_readpixels.c:502/517/532` still reports `Y_0_BOTTOM` for a
scanout RT rendered as `Y_0_TOP`), which the marker route leaves in place. It is what
`gl3_draw.c:398-401` itself recommends. Costs a change in every present layer, and it was not
confirmed that these in-process contexts actually advertise the extension
(`extensions_table.h:397` gates it at GL 4.3 / ES 3.0).

---

## ⚠️ ARCHITECTURE CORRECTION (2026-09-08) — read before doing edit #1

Edit #1 below ("`c->flags &= ~0x2u;` in the winsys refusal branch, safe by construction because
Mesa reads the same struct back") is only half true, because **the V3D stack is split into two
paths** and they behave differently. Measured from boot logs:

| component | path | evidence in the UART log |
| --- | --- | --- |
| GPU X desktop (`startx_gpu`) | **daemon** `/dev/v3d-srv` | `v3d-srv` present, `v3d-winsys` absent |
| games (SuperTuxKart, vkQuake, …) | **in-process winsys** | `v3d-winsys` present, `v3d-srv` absent |

Consequences for the plan:

* **Games (in-process winsys):** Mesa calls the winsys directly on its own
  `struct drm_v3d_create_bo`, so an in-place `c->flags &= ~0x2u;` *is* visible to
  `v3d_bufmgr.c`. Edit #1 works as written here.
* **X desktop (daemon):** it does not use the winsys at all — the arbitration lives in the
  daemon's own `v3d_gpu.c` `ioc_create_bo`. And the client marshals over RPC:
  `libv3d-client.c` sends `req.flags = c->flags` and receives a `v3d_rpc_resp_t` carrying only
  `handle`/`pa`/`size`/`gpuva`, then never writes back to `c->flags`. **`v3d_rpc_resp_t` has no
  flags field**, so a refusal cannot reach Mesa. Making the honored-flag visible on this path
  needs an RPC protocol change (add `flags` to the response, return it from the daemon, write it
  back in the client) — three files and a struct used by every GPU client, not the one-liner the
  plan implies.

So edit #1 must be done **twice**, once per path, and the daemon half is a protocol change. Budget
for that before starting.

Independently established while checking this: the BO-cache concern this document raised for
edit #2 is **not** a real risk. `v3d_bufmgr.c:141` uses the *local* request flag for the cache
lookup, which is correct and must stay (the outcome is unknown at that point), and the free-side
guard's own comment gives the reason as "scanout aliases a fixed framebuffer PA" — which a
*refused* BO does not. Caching a refused, plain-DRAM BO is correct by that invariant.
