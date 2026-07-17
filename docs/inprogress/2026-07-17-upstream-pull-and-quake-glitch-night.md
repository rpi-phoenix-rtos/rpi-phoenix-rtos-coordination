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

**libphoenix conflict RESOLUTION (worked out 2026-07-17, ready to apply in phase 2):**
All 3 conflicts resolve by taking UPSTREAM — upstream added real implementations that
supersede our fork's stubs/additions:
- `include/math.h` → **accept upstream's delete** (`git rm include/math.h`). The RTOS-1132
  libm move relocated everything to `libm/phoenix/` + `libm/libmcs/`; libmcs PROVIDES
  hypot/hypotf/hypotl (`libm/libmcs/libm/mathd/hypotd.c`, `mathf/hypotf.c`) and the new
  `libm/libmcs/libm/include/math.h` DECLARES them — so our fork's hypot() addition (6e2b929)
  is now redundant.
- `posix/stubs.c` → **`git checkout --theirs`**. Upstream moved getgr* to a new real
  `unistd/grp.c` and ADDED getgid/getegid/getgroups/setgroups/wctomb/flock/ulimit to stubs.c;
  keeping ours would duplicate grp.c's getgr* (link error). We only lose a deprecated `gets`
  stub (verify nothing links it; re-add if the build complains).
- `unistd/pwd.c` → **`git checkout --theirs`**. Upstream's pwd.c is a superset (getpwnam/uid,
  getpwnam_r/uid_r, getpwent/setpwent/endpwent). RUNTIME RISK to check: upstream's getpwnam
  behavior vs our fork's (X11/Quake call it for the home dir) — confirm it doesn't return NULL
  where consumers expect a synthetic entry.
Recipe: `git -C sources/libphoenix merge origin/master; git rm include/math.h;
git checkout --theirs posix/stubs.c unistd/pwd.c; git add -A; git commit --no-edit`.
This ALSO fixes the socklen_t build failure (the merged posix-socket.h + merged libphoenix
agree once both are on upstream).

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

---

## UPDATE (2026-07-17, session 2) — both tasks advanced

### (b) UPSTREAM PULL — COMPLETE + HW-VALIDATED
Applied the libphoenix recipe (math.h delete, stubs.c/pwd.c take-theirs) + resolved the
follow-on link clash: upstream's libmcs now defines the C99 float-math fns our GL math shim
provided (tanf/acosf/powf/expf/...), causing multiple-definition. Fixed by making the shim
defs WEAK (coord addafb4). ALL 16 siblings now merged (usb 8e8316f keeps our #121 mem.c
guards; libphoenix 58c87dd; +14 clean). The full merged tree **builds, boots to (psh), and
Quake renders correctly on HW** (netboot: TRIPLE-BUFFER scanout, brick world+torch+weapon+
ammo+HUD all correct). Recorded: manifests/2026-07-17-upstream-merged-validated.md.
Notes: (1) intermittent NFS exec -5 flake still bites rpi4-quake launch (retry). (2) SD card
NOT reflashed — still the pre-upstream flicker-fixed build the user validated; build+flash the
`sd` variant to move the upstream integration onto the card. (3) RUNTIME item to verify:
upstream's getpwnam/getpwent behavior vs consumers (X11/Quake home-dir lookups).

### (a) CAPTURE-READBACK FIX — WIP, buffer-index unresolved
Root cause confirmed (glReadPixels reads the FBO resource's fresh CPU mapping = noise).
Implemented v3d_phoenix_scanout_readback (mmap fb PA, MAP_SHARED required) + glctx wrapper +
gl_screen.c hook (coord 3667ff4, quakespasm c533d16). The mmap reads real fb content, BUT
neither g_back (render buffer) nor g_displayed (presented buffer) yields the clean frame —
both come back noise (g_back once showed the 2D console overlay). So glctx's buffer index
does NOT map to the winsys physical buffer (scanout_pa + k*bytes) as assumed. NEXT (needs 1
build+boot): dump all 3 buffers to separate files in one capture to identify which holds the
frame; fix the index (or track the firmware SET_VIRTUAL_OFFSET pan directly). The glitch does
not reliably reproduce in the deterministic demo anyway, so this tooling is the prerequisite,
not the fix. No functional regression (HDMI display correct; capture was already noise).
