# 2026-07-17 overnight: upstream pull + Quake model-glitch hunt

Two tasks requested (user asleep): (a) fix per-boot-varying Quake model glitches
(black/skewed triangles on weapons/torches/monsters); (b) pull upstream from all
phoenix-rtos sibling repos and be up-to-date with mainline.

Rollback point before any of this: manifest
`manifests/2026-07-16-flicker-vcmbox-fixed-knowngood.md`
(`restore-integration-state.sh` that file to revert everything).

## (b) Upstream pull — status

Scouted divergence + conflicts (`git merge-tree`) before merging. Result:

**MERGED cleanly (15 repos), each a separate merge commit:**
phoenix-rtos-build, corelibs, devices, doc, filesystems, hostutils, kernel,
lwip, ports, posixsrv, project, tests, utils, plo, **phoenix-rtos-usb**
(usb had 1 conflict, usb/mem.c: kept OURS — the #121 heap-corruption guards
usb_chunkSane/usb_bufSane are strictly more defensive than upstream's loop
refactor; upstream's other 5 usb commits merged clean).

**DEFERRED (needs attended resolution):**
- **libphoenix** (43 behind, 39 ahead) — upstream `d0a2884` "libm: Move
  libphoenix math library implementation" (JIRA RTOS-1132) is a MAJOR libm
  overhaul: deletes include/math.h + complex.h + math/consts.h, relocates
  math/*.c → libm/phoenix/, integrates the third-party **libmcs**. It
  modify/delete-conflicts with our fork's include/math.h (added hypot/hypotf).
  Also add/add conflicts in posix/stubs.c (getgr* family) + unistd/pwd.c
  (getpw* reentrant). Resolving blind overnight on a library Mesa+Quake link
  against heavily = too risky (silent math-precision/linkage breakage). LEFT
  AT KNOWN-GOOD. Resolution plan: adopt upstream's libmcs layout, re-home our
  hypot() (or confirm libmcs provides it), take upstream's real getpw/getgr
  impls over our stubs, then full rebuild + Quake math re-validation.

**ABI-coupling check (new kernel + deferred old libphoenix):** the 42 merged
kernel upstream commits are additive/hardening (futimens/poll validation,
lseek fix, `schedInfo` syscall ADDED, SMP priority inheritance) — no
ABI-breaking syscall removals, so old libphoenix stays compatible. rpi4-vcmbox
(the flicker fix's dependency) is UNTOUCHED by upstream.

Validation: rebuild `--scope core` + netboot (boot + Quake render) — see below.

## (a) Quake model-glitch hunt — BLOCKER FOUND

The visual-regression harness (deterministic demo capture → TCP sink → host
llvmpipe reference → SSIM/blacktex compare) is **currently blind**: the Pi's
`scr_capture` TGA frames come back as **full-frame RGB noise** (montage left
panel), while HDMI shows the scene rendering correctly. Root cause: since the
render-to-scanout rework, `gl_screen.c` capture does `qsv3d_bind_fbo()` +
`glReadPixels`, but the GPU tile-STOREs to the aliased scanout PA while
glReadPixels reads the FBO resource's own (never-written) BO → noise. HDMI is
correct because the flip displays the scanout PA. So capture must read the
scanout buffer's CPU mapping (via a winsys accessor) instead of glReadPixels.
(Also fixed en route: host reference build — guarded the Phoenix-only
`qsv3d_bind_fbo` with `#ifdef QSS_PHOENIX` in gl_screen.c + added -DQSS_PHOENIX
to the Pi build; host now builds + captures 120 frames.)

Plan once capture is fixed: capture 2-3 Pi boots + diff Pi-boot-A vs Pi-boot-B
at identical frame indices (same engine, r_particles 0, fixed timestep → any
pixel diff = pure non-determinism, localizes the unstable model). Then
root-cause (candidate: v3d_bo_from_cache reuse not re-zeroed → stale vertex
data). [[project_pi4_quake_flicker_vcmbox]] [[project_quakespasm_port]]

### Does the glitch reproduce in the deterministic demo? — NO (this boot)

Cheap check before investing in the capture fix: eyeballed a spread of the 2702
CORRECT boot-A HDMI frames (the display output is fine; only the TGA capture is
noise). f_0650 (demon-face relief), f_1000 (weapon+corridor), f_1700 (gibs),
f_2100 (**a live zombie — correct geometry + blood texture — plus a wall torch,
both clean**): dynamic models render CORRECTLY across the sampled demo. The
per-boot-varying glitch did NOT manifest in this boot's deterministic demo
(consistent with the 2026-06-15 harness finding that demo playback matches host).

**Conclusion for the hunt:** the glitch is intermittent / non-deterministic /
possibly interactive-or-angle-specific — a single deterministic-demo boot does
not reliably surface it. Localizing it needs the FIXED TGA harness run across
several boots, diffed Pi-vs-Pi to catch the rare non-deterministic divergence.
That chain (fix capture → multi-boot capture → diff → localize → root-cause →
fix → validate) is a multi-cycle next-session task, not tonight. NOT a
regression from the flicker fix — the flicker fix + known-good state are intact.

### Capture-fix design (next session, ready to implement)

The winsys has all scanout state (scanout_pa / pa2 / pa3 / phys_h / bytes). Add
`v3d_phoenix_scanout_readback(dst, buf, bytes)` that mmaps the fb PA
(MAP_PHYSMEM|MAP_UNCACHED) for buffer `buf` and memcpy's it; a glctx wrapper
passes g_back; gl_screen.c capture uses it (under QSS_PHOENIX) instead of
glReadPixels, converting scanout 32bpp → TGA BGR with a Y-flip. Match the
scanout pixel order from plo video.c / rpi4-fb (don't guess). Advisor's cheaper
alternative: a capture-only toggle to the DRAM-fallback FBO path
(g_resolve==0 / g_render_fbo) where glReadPixels worked pre-2026-06-21 — since
the leading cause (stale vertex-BO reuse) is geometry (upstream of present),
it'd still show in blit-resolve capture, sidestepping the format work.
