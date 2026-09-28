# M9 — lower-resolution fullscreen for the games (scaled KMS modes)

Owner goal (2026-09-28): a fullscreen game can run at a **lower resolution for more fps** and the
display goes back to the normal resolution when the game ends or leaves fullscreen. STK is 11.9 fps at
1920×1080 and GPU-bound (E2c), so fewer pixels should help almost linearly, up to the CPU floor.

Status: **implemented and host-tested; Pi cycles pre-registered (§6), not run.** New server
`/bin/rpi4-kms-g9` (g8 + scaled modes; g8's foreign-`/kmsbuf` alias is kept), probe `/bin/kmstest-g9`,
launcher `/bin/game-res`. No SDL patch and no game relink was needed (§3).

## 1. SET_PLANE scaling: the evidence

**The firmware plane takes separate source and destination rectangles, and nothing on the Linux side
limits the ratio.**

- `struct set_plane` (`external/linux/drivers/gpu/drm/vc4/vc4_firmware_kms.c:64–96`, the layout our
  `fw_plane_t` mirrors, `_Static_assert` 60 bytes): `src_x/y/w/h` are u32 **16.16** in framebuffer
  pixels, `dst_x/y` s16 and `dst_w/h` u16 in screen pixels, independent fields.
- `vc4_plane_to_mb()` (`:531–576`) copies `state->src_w/h → src_w/h` and `state->crtc_w/h → dst_w/h`
  with no equality check. `vc4_fkms_plane_atomic_check()` (`:678–689`) only calls it: fkms never calls
  `drm_atomic_helper_check_plane_state()`, so there is no min/max scale and any rectangle pair goes to
  the firmware.
- `vc4_fkms_margins_adj()` (`:450–492`) rescales **dst only** (`dst_w = dst_w * adjhdisplay /
  hdisplay`, src untouched) to implement the overscan margins: fkms relies on the firmware scaling
  `src → dst` on every plane.
- The full KMS driver shows what the HVS itself does with a ratio (`vc4_plane.c`):
  `vc4_get_scaling_mode()` (`:329–341`) uses the polyphase filter (PPF) when `3·dst ≥ 2·src`, else
  TPZ; `drm_atomic_helper_check_plane_state(…, 1, INT_MAX, …)` (`:593–594`) allows any up/down scale.
  Every M9 mode is an **upscale** (1.2×–2.25×) → PPF, the good filter. [inferred: the firmware makes the
  same choice; it is closed]
- Limits that apply: the line buffer. A vertically scaled RGB plane needs `src_w × 16 / 4` LBM words on
  BCM2711 (`__vc4_lbm_size`, `vc4_plane.c:755–800`); the HVS5 LBM is 60 k words
  (`vc4_hvs.c:1664–1667`). 1600 px → 6 400 words, so one scaled primary plus a scaled cursor is far
  inside it. Alignment: none beyond what `plane_check` already enforces (the pool's 64-byte pitch;
  1280/1600/960/1440/1024/800/640 × 4 bytes are all multiples of 64).
- Not proven on the bench yet: E3 used `src == dst` only. `m9a` (§6.1) is the first scaled SET_PLANE on
  this Pi.
- Linux fkms itself changes the **HDMI timing** for a lower mode (`vc4_crtc_mode_set_nofb` → firmware
  `SET_TIMING`, `:928–969`). M9 does not: it is the panel-fitter design (i915/amdgpu "scaling mode:
  Full aspect"), with no monitor resync and no TV "mode change" delay, and the firmware's own HDMI
  setup is never touched.

**Plain plane scaling** (`drmModeSetPlane` with `crtc_w/h ≠ src_w/h`) was already served by g8:
`plane_check` has no ratio rule, `plane_apply` passes both rectangles, and libdrm-phoenix turns
SETPLANE into a one-plane atomic commit (`drm_phoenix_kms.c ioc_setplane`). M9 adds a host-test row and
the `m9a` planescale step; compositors can use it as is.

## 2. Design as built (`tools/gpu-lane/kms/`, build `out-g9`)

**Mode list** (`kms_modes.h`, pure functions, host-tested). The connector lists the native mode first
(PREFERRED|DRIVER), then every lower mode that is smaller than the screen both ways: **1600×900,
1440×1080, 1280×720, 1024×768, 960×540, 800×600, 640×480** (8 modes for the 1920×1080 bench monitor).
Their timings are nominal: `mode_fallback`'s blanking and a clock that gives the **native refresh**,
and `vrefresh` = the native one. That matters: quakespasm and vkQuake accept a fullscreen size only if
it is an SDL display mode with `refresh_rate == vid_refreshrate` (60). The list is derived on every
GET_CONNECTOR from the native mode, so a refresh re-measure (`kms_mode_set_refresh`) moves all modes.
`-M native` restores g8's single-mode list (rollback / A/B switch).

**Setting a mode.** SET_CRTC (`mode_hdisplay/vdisplay`, what libdrm-phoenix `ioc_setcrtc` sends) and
atomic `MODE_ID` accept any listed size; anything else is `-EINVAL`, as before. No wire change:
`kms_proto.h` is untouched (and its ports copy stays in sync). The CRTC then has a *user mode*
(`umode_w/h`, 0 = native) and a **fit** (`kms_fit`): scale = min(screen/mode) per axis, image centred.
Clients see the lower mode everywhere: GET_CRTC (size + modeinfo), the CRTC's `MODE_ID` property (a
server blob holding the lower mode), HELLO's `width/height`.

**Scanning out.** Plane state stays in mode coordinates (what the client sent; GET_PLANE/properties
unchanged). `plane_apply` maps only `dst` through the fit (`kms_fit_rect`: both edges mapped, size =
their difference) and `plane_check` refuses a mapped rectangle outside the firmware's s16/u16 range.
All planes are mapped, the cursor included (a panel fitter scales the whole pipe). In the native mode
the fit is the identity and the SET_PLANE values are byte-for-byte g8's.

| mode | on screen | bars | |
|---|---|---|---|
| 1600×900 | 1920×1080 +0+0 | none | 1.2× |
| 1280×720 | 1920×1080 +0+0 | none | 1.5× |
| 960×540 | 1920×1080 +0+0 | none | 2× |
| 1440×1080 | 1440×1080 +240+0 | left/right | 1× |
| 1024×768, 800×600, 640×480 | 1440×1080 +240+0 | left/right | 1.40625×, 1.8×, 2.25× |

A mode change re-applies the planes the commit does not touch (their screen rectangles move). The
firmware framebuffer (the console, layer −127) would show in the pillarbox bars, so while a mode with
bars shows a primary plane the server blanks it (`FRAMEBUFFER_BLANK`, E3 stack 3/4; the `-B` path)
and unblanks when the bars go. 16:9 modes need no blank.

**Restore — a lower mode never outlives its client or its picture.** It goes back to native when:
1. **the primary plane goes off**: SetCrtc `fb 0` (SDL KMSDRM's `DestroySurfaces`, which restores
   `original_mode` with the console `buffer_id` 0), atomic `ACTIVE 0`, a commit that leaves the primary
   empty, RMFB of the shown framebuffer (DRM disables the CRTC), and a client's death (`planes_off`);
2. **the client that set it closes its descriptor**, whoever's planes are on screen;
3. a client sets the native mode.
Another client's close does not revert a mode it did not set. This sits in front of the existing console
path: `client_close → planes_off → (mode back to native) → console_update` → `console handover
enable` (with `-C`) and the unblank, so the console comes back at native. Nothing is needed in `-R`:
modes are server state and the HDMI timing never changed. Each change prints one line, e.g.
`KMS mode crtc=0 1280x720 scaled client=3 why=commit screen=1920x1080+0+0 bars=0` and
`KMS mode crtc=0 1920x1080 native client=0 why=primary_off …`. The ready line ends `modes=8 scaler=fit`.

Files: `kms_modes.h` (new), `kms.h` (CRTC fields, `-M`), `kms_main.c` (connector list, lookups,
`commit()` with the new mode, the restore rules, blank-on-bars, GET_CRTC/HELLO/MODE_ID), `kms_backend.c`
(`plane_check`/`plane_apply` map dst), `kmstest.c` (`modes`, `modes-leave`, `crtc`).

## 3. SDL 2.30.12 KMSDRM and the games — no patch, no relink

- **SDL picks the mode from the window size.** `KMSDRM_CreateWindow` takes the closest listed mode to
  `window->windowed.w/h` (`SDL_kmsdrmvideo.c:1588–1600`, `KMSDRM_GetClosestDisplayMode` `:505–529` =
  the smallest mode ≥ the request); for `SDL_WINDOW_FULLSCREEN` SDL_video.c then calls
  `KMSDRM_SetDisplayMode` (`:1393–1421`), which stores `fullscreen_mode`. `KMSDRM_CreateSurfaces`
  creates the **GBM surface at the mode's size** (`:1216–1237`), and the first swap does
  `drmModeSetCrtc(…, &dispdata->mode)` (`SDL_kmsdrmopengles.c:164–175`; patch 0009 left it intact) →
  libdrm-phoenix sends `mode_hdisplay/vdisplay` → g9 sets the scaled mode. So a game that asks for
  1280×720 fullscreen gets the 1280×720 mode, renders 1280×720 and is scaled.
- `SDL_WINDOW_FULLSCREEN_DESKTOP` keeps the desktop (native) mode: SDL's semantics, as on Linux.
- On exit `KMSDRM_DestroySurfaces` restores `original_mode` with the console buffer (`:1102–1110`) →
  restore rule 1; a crash → rule 2.
- Vulkan: `KMSDRM_Vulkan_CreateSurface` picks the `VK_KHR_display` mode whose visible region equals the
  window size (`SDL_kmsdrmvulkan.c:357–400`); v3dv's display WSI then modesets it — the same SET_CRTC
  path.
- **SDL "windowed" on KMSDRM is also a mode switch** (to the closest mode to the window size). So
  "goes windowed" returns to native only if the window is desktop-sized or the game uses
  FULLSCREEN_DESKTOP; a real window on the desktop is M8 (Wayland).

Hence **no new SDL patch and no relinked game**: `stk-drm`, `quakespasm-drm`, `quake2/3-drm` and
`vkquake-drm` are used as staged. What each engine needs is on its command line (`/bin/game-res`, §4).

## 4. Per-game knobs — `/bin/game-res <game> [WxH] [args…]` (or `export GAME_RES=WxH`)

`tools/gpu-lane/m9-res/game-res.c` builds the command line and execs the existing launcher/engine
(printing it as `game-res: <game> WxH -> exec …`). Read from each engine's source:

| game | exec | why |
|---|---|---|
| `stk` | `/bin/stk-drm --screensize=WxH …` | the launcher drops its own `--screensize=1920x1080` when one is given and keeps `--fullscreen` (`stk-launcher.c user_overrides`, `:162–182, 222–225`); Irrlicht GL asks for `SDL_WINDOW_FULLSCREEN` (`CIrrDeviceSDL.cpp:428–437`). The seeded `scale_rtts_factor=0.75` stays: the 3D renders at **0.75 × the window** (§6.2) |
| `qs` | `/usr/bin/quakespasm-drm -width W -height H -fullscreen` | fullscreen needs an exact SDL display mode at `vid_refreshrate` 60 (`gl_vidsdl.c VID_ValidMode → VID_SDL2_GetDisplayMode`); `vid_desktopfullscreen` stays 0 |
| `q2` | `/usr/bin/quake2-drm +set vid_fullscreen 1 +set r_mode -1 +set r_customwidth W +set r_customheight H` | the launcher's `vid_fullscreen 2` is `SDL_WINDOW_FULLSCREEN_DESKTOP` (`glimp_sdl2.c:512–518`): **no mode switch**; `+set`s run in order (`cmdparser.c Cbuf_AddEarlyCommands`), the later wins |
| `q3` | `/usr/bin/quake3-drm +set r_fullscreen 1 +set r_mode -1 +set r_modeFullscreen -1 +set r_customwidth W +set r_customheight H` | quake3e's `r_modeFullscreen` defaults to `-2` (desktop) and **overrides `r_mode` when fullscreen** (`cl_main.c CL_GetModeInfo`: `if (fullscreen && *modeFS) mode = atoi(modeFS)`) |
| `vkq` | `/usr/bin/vkquake-drm -basedir /usr/share/quake -width W -height H -fullscreen +r_rtshadows 0 +map start` | vkQuake takes the **first** `-width` (`COM_CheckParm`), so it cannot be appended to `vkq-drm`'s `-width 1920`; game-res repeats vkq-drm's arguments with the size replaced |

Without a size and without `GAME_RES`, game-res passes an explicit 1920×1080. A size that is not a
listed mode is passed through with a warning (SDL then picks the closest larger mode; quakespasm falls
back to its config). Host dry-run test in `m9-res/build.sh` (9/9 PASS: every game's exact argv,
`GAME_RES`, default, unknown game refused).

**Notes for the P1 default-launcher work** (the migration agent):
- The yquake2 and quake3e launchers' current arguments are *desktop*-fullscreen: they stay native under
  g9 by construction (good for defaults). A resolution option in a new default launcher must set
  `vid_fullscreen 1` (q2) and `r_modeFullscreen -1` (q3), or it silently does nothing.
- vkq-drm hard-codes `-width 1920 -height 1080`: a size knob must replace them, not append.
- quakespasm-drm's default comes from the staged `id1/config.cfg` (`vid_width 1920`, `vid_height 1080`,
  `vid_fullscreen 1` = exclusive): native under g9. **Hazard:** quakespasm writes `config.cfg` at a
  menu Quit, so a 1280×720 session that quits normally persists `vid_width 1280` on the NFS root, and
  every later default start is 1280×720. The migrated default should pass `-width 1920 -height 1080`
  (or `-current`) explicitly. (The cycles never quit it.)
- An SDL app that runs *windowed* at a listed size (e.g. 800×600, 640×480) now gets that mode, scaled,
  instead of a 1920×1080 surface. None of the migrated defaults does this (checked above); a new
  client that does will look lower-res until given a desktop-sized window.
- Rollback: `rpi4-kms-g9 -M native` = g8's mode list and refusal, everything else identical.

## 5. Host tests — `tools/gpu-lane/kms/hosttest/run.sh` (native gcc + ASan/UBSan, ~10 s)

`mode_test.c` runs **the real server**: it `#include`s `kms_main.c` (its handlers are static) and links
`kms_backend.c` and `kms_bo.c`, from host copies whose only change replaces the aarch64 instructions
(`cntvct`/`cntfrq` reads, `dsb`; `run.sh` checks each substitution changes exactly one line and no
`__asm__` is left). Stand-ins: the firmware (a mailbox that records every SET_PLANE value), the vblank
thread (the test calls `kms_on_vblank`), the Phoenix calls (`hosttest/shim/`). The test only speaks the
wire protocol (`handle_raw`) and reads what reached the firmware, so it builds unchanged against g8.

Rows (96): the mode list (8 modes, native first + PREFERRED, all 7 lower ones, none larger than the
screen, each lower mode 60 Hz by `vrefresh` and by `clock/htotal/vtotal`); SetCrtc 1280×720 → SET_PLANE
src 1280×720, dst 0,0 1920×1080, fb width/height/pitch; GET_CRTC and MODE_ID 1280×720; no blank; a page
flip keeps the scaling; a 64×64 cursor at 100,100 → 150,150 96×96; SetCrtc 1024×768 → dst 240,0
1440×1080, the cursor re-applied at 381,141 90×90 (2 SET_PLANE calls), fb blanked; **the client closes →
primary and cursor unset, unblanked, the next client sees 1920×1080, no fb, MODE_ID native**; native
SetCrtc = identity; 1600×900 then **SetCrtc fb 0 + native mode → native**; 960×540 then **RMFB → native**;
atomic MODE_ID 1440×1080 → dst 240,0 1440×1080; MODE_ID 1366×768 → `-EINVAL`; TEST_ONLY with a mode →
nothing changes, no firmware call; **ACTIVE 0 → native**; SetPlane 640×360 → full screen at native
(plane scaling); an unrelated client's close keeps the mode, **its setter's close → native**; `bos_live`
0 at the end.

| build | result |
|---|---|
| g9 (working tree) | **96/96 PASS** |
| g8 source (`f90f74b4a`, = `/bin/rpi4-kms-g8`) — negative control | **FAIL, 41/96** (nmodes 1, every lower SetCrtc/MODE_ID `-EINVAL`, so no scaled plane and no restore rows; the native, SetPlane-scaling and unrelated-close rows pass on g8 too) |
| g9 with `-DMODE_TEST_NATIVE_ONLY` (`-M native`) — control | **FAIL**, like g8 |
| existing G7 scan-out rules + control, alias test (60/60) + g7 control | unchanged, PASS |

`kms_modes.h` arithmetic cases (all exact): 1280×720 → 1.5×, 960×540 → 2×, 1600×900 → 1.2×,
1440×1080 → +240, 1024×768 → 1440×1080 at +240.

## 6. Build, staging and pre-registered Pi cycles

```
tools/gpu-lane/kms/build.sh --poll-notify --out out-g9      # the g8 recipe; -Werror, 0 warnings
tools/gpu-lane/kms/hosttest/run.sh                          # §5
tools/gpu-lane/m9-res/build.sh                              # host dry run 9/9, then the Phoenix ELF
strings -a tools/gpu-lane/kms/out-g9/rpi4-kms | grep -c 'modes=%u scaler=%s'   # 1 (g8: 0)
```
Built 2026-09-28 08:55 against the tree sysroot `libphoenix.a` `83c07cf81b47e3f8` (08:33; a later core
rebuild moves it — re-run `build.sh` before staging a rebuilt copy). Staged on the live fsid=0 export
`/srv/phoenix-rpi4-nfs-gcc16` (`/etc/exports.d/phoenix-rpi4.exports`), each path checked absent, then
`sudo -n install -m 755` and `cmp`:

| source | staged as | sha256 |
|---|---|---|
| `tools/gpu-lane/kms/out-g9/rpi4-kms` (1 086 872 B) | `/bin/rpi4-kms-g9` | `c3dc69dbecb75000db5955b59721488a35a753cd8d2c8cfd5c9127ddd155ce51` |
| `tools/gpu-lane/kms/out-g9/kmstest` (888 536 B) | `/bin/kmstest-g9` | `bb322f088fa4f3ad2c8bfe8209c9a714d56403e2dac7a14897ed340dcd797c03` |
| `tools/gpu-lane/m9-res/out/game-res` (991 368 B) | `/bin/game-res` | `4237b85abfc1757466609acb5e16d5351d3260d89922b87b9d78bfedd30b6f7f` |

Nothing else was staged or changed; `/bin/rpi4-kms-g8`, the games and every launcher are as before.

Common to all cycles: netboot image with core_freq=500 (build ≥ 17); single-owner rule (no old-lane GPU
app, X or `rpi4-v3d` in the boot); the game binaries of MIGRATION §6 (stk-drm `ea3a5667004c793b`,
supertuxkart-drm `71ac4f58a678dc20`). Before the first cycle: `cmp` the three staged files against
`out-g9` (a stale copy is the likeliest false result). ~1.3 % UART line corruption — re-read, don't
count; EL0 dumps print twice. HDMI: only snapshots after the probe's/game's own banner.

### 6.1 `m9a-modes` — the mode list, three scaled modes, restore, plane scaling (≈ 4 min)

**Question:** does the firmware scale a SET_PLANE with `src ≠ dst` to the full screen (1280×720,
960×540) and to a centred 1440×1080 with black bars (1024×768), and does every way back reach native?

```
./scripts/test-cycle-psh-interact.sh --label m9a-modes --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 120 --ready-line 'KMS srv detached|KMSTEST done' --ready-extra-secs 4 \
    --hdmi-dense-on 'KMSTEST cmd modes start' -- \
    "/bin/rpi4-kms-g9 -C" \
    "/bin/kmstest-g9 -H 8 modes" \
    "/bin/kmstest-g9 modes-leave" \
    "/bin/kmstest-g9 crtc" \
    "/bin/kmstest-g9 stats"
```

| # | line / observation | predicted | if instead… |
|---|---|---|---|
| 1 | `KMS srv ready … modes=8 scaler=fit` | g9 runs | `modes=` absent: g8 or older staged (`cmp`) |
| 2 | `KMSTEST modes list rc=0 n=8`, `mode i=0 1920x1080 … preferred`, then 1600x900, 1440x1080, 1280x720, 1024x768, 960x540, 800x600, 640x480, each `vrefresh=60 clock_hz=60` | as host test | n=1: `-M native` given or g8 |
| 3 | `KMS mode crtc=0 1280x720 scaled … screen=1920x1080+0+0 bars=0`, `KMSTEST modes set 1280x720 rc=0 crtc=1280x720 fb=<F> ok=1` | yes | `rc=-22`: mode not accepted (row 1) |
| 4 | **HDMI during the 1280×720 hold: the blue card fills the screen** — white border on all four edges, red TL / green TR / blue BL / yellow BR squares, a **round** white ring, grid lines sharp-ish (1.5× PPF) | **firmware scales src→dst** | card in the top-left 1280×720 only: the firmware ignored dst_w/h (then scaling needs the HVS dlist directly — a finding); garbage/stripes: pitch or LBM problem; ring an ellipse: aspect wrong (fit bug — compare the `screen=` line) |
| 5 | `KMS console handover disable rc=0` at the first plane | yes (`-C`) | — |
| 6 | 1024×768: `KMS mode … 1024x768 scaled … screen=1440x1080+240+0 bars=1`, `KMS fb blank=1 rc=0 (scaled mode bars)`; **HDMI: the green card centred, 240-px black bars left and right, full height, ring round** | yes | console text in the bars: the blank did not take (`rc≠0`) — record; the card stretched to full width: dst ignored as in row 4 |
| 7 | between modes: `KMS mode crtc=0 1920x1080 native … why=primary_off` (the probe's RMFB of the shown card) and `KMS fb blank=0` after 1024×768 | yes | no native line: restore rule 1 (RMFB) broken |
| 8 | 960×540: the red card fills the screen (2×) | yes | as row 4 |
| 9 | `KMSTEST modes restore rc=0 crtc=1920x1080 fb=0 native=1` (SetCrtc fb 0 + native, SDL's exit path) | yes | `native=0`: rule 1 (SetCrtc fb 0) broken |
| 10 | `KMSTEST modes planescale 640x360->1920x1080 rc=0 … ok=1`; **HDMI: the purple card full screen (3×), ring round** | plane scaling at native works | card 640×360 in a corner: as row 4 |
| 11 | `KMSTEST modes result fails=0 verdict=PASS` | PASS | |
| 12 | `modes-leave set 1280x720 rc=0 crtc=1280x720 -- exiting without a restore`, then **`KMS mode crtc=0 1920x1080 native … why=primary_off`** and `KMS srv client <n> closed planes_off=1`, `console handover enable rc=0` | the close restores | the mode survives the client: rule 2 broken (the next row fails too) |
| 13 | `KMSTEST crtc rc=0 mode=1920x1080 fb=0`; **HDMI: the text console at native** | yes | `mode=1280x720`: FAIL |
| 14 | `KMSTEST stats … apply_errors=0 … bos=0 exports=0` | 0 errors, nothing leaked | `apply_errors>0`: the firmware refused a SET_PLANE (the first 5 are logged as `apply FAIL … rc=`) |
| 15 | faults | 0 | addr2line `out-g9/rpi4-kms` |

**Decides:** rows 4/6/8/10 = the firmware scaler works for every M9 case (the design's one unproven
premise). Rows 7/9/12/13 = all three restore paths on hardware.

### 6.2 `m9b-stk-res` — STK at 1920×1080, 1600×900, 1280×720, 960×540 (4 cycles, ≈ 10–13 min each)

**Question:** how much fps does each lower mode give STK, and does it render correctly scaled?

Four cycles, run **detached** (each exceeds the 600 s Bash cap, as `mig-all-stk`), labels
`m9b-stk-{720,540,900,1080}` in that order (the headline first; 1080 is the same-server control against
`stkdrm-1` 11.89 / `mig-all-stk`). Only the resolution changes between them:

```
./scripts/test-cycle-psh-interact.sh --label m9b-stk-720 --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 440 --ready-line 'V3DA srv detached|KMS srv detached|profile: Number of frames|KMSTEST done' \
    --ready-extra-secs 45 --hdmi-dense-on 'stk-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-g9 -G" \
    "/bin/game-res stk 1280x720 --track=hacienda --numkarts=4 --profile-laps=2" \
    "/bin/kmstest-g9 crtc"
```
(`m9b-stk-540`: `960x540`; `m9b-stk-900`: `1600x900`; `m9b-stk-1080`: `1920x1080`.) The trailing
`kmstest-g9 crtc` is the restore check after STK's own exit (m9c's row 3, per resolution);
`--ready-extra-secs 45` (not mig-all's 30) leaves STK's ~30 s teardown room before it is typed.

**Metric** (the M1/M3 STK rule, unchanged): the `stk-drm flipstat … = X fps` lines over **gameplay
windows only** — fps > 3 in the contiguous race run, first and last race window dropped, none with a
wedge/`TIMEOUT`/reject line; trial fps = mean of the per-window fps; ≥ 10 windows or void. Never grade by
STK's `profile: … Average FPS` (simulation frames). `./scripts/flipstat-summary.sh --seq <label>`.

**Prediction.** New lane at 1080p: 11.89 fps = 84 ms/frame, GPU-bound (Pi OS: V3D busy 97 %, render
83 ms/frame at the same settings, E2c). The render phase scales with the **deferred RTT pixels** (E2b:
~66–78 ms/Mpx), and the RTTs are 0.75 × the window (`scale_rtts_factor`). CPU∥GPU overlap on this lane,
CPU ≈ 40 ms/frame (E2) ⇒ a **floor near 25 fps**:

| mode | RTT | RTT Mpx | GPU ≈ 71 ms/Mpx | predicted fps | band |
|---|---|---|---|---|---|
| 1920×1080 | 1440×810 | 1.17 | 83 ms | **11.9** | 11.5–12.5 (= stkdrm-1) |
| 1600×900 | 1200×675 | 0.81 | 58 ms | **17** | 15–18.5 |
| 1280×720 | 960×540 | 0.52 | 37 ms | **22** (GPU ≈ CPU) | 19–25 |
| 960×540 | 720×405 | 0.29 | 21 ms | **24** (CPU-bound) | 20–27 |

The informative outcome is the **plateau**: 960×540 ≤ 1.15 × the 1280×720 fps. Linear-in-pixels
(the owner's hypothesis) would give ~27 and ~48 fps; a 960×540 result near 45 would mean the CPU term
is far smaller than E2's 40 ms on this lane (a finding worth a profile).

| # | line / observation | predicted | if instead… |
|---|---|---|---|
| 1 | `KMS srv ready … modes=8 scaler=fit`; `game-res: stk 1280x720 -> exec /bin/stk-drm --screensize=1280x720 --track=…` | yes | — |
| 2 | `stk-drm: DATADIR=…` then `stk-drm: new GPU lane …` | yes | `Resolution 1280x720 has been blacklisted`: STK's blacklist (none seeded) |
| 3 | **`KMS mode crtc=0 1280x720 scaled client=<n> why=commit screen=1920x1080+0+0 bars=0`** (possibly after one native SetCrtc while SDL recreates the surface) | yes | no scaled line and `first swap … 1920x1080`: SDL did not switch — the window was FULLSCREEN_DESKTOP or the mode list did not reach SDL (`DEBUG:` KMSDRM lines) |
| 4 | `stk-drm: first swap … window 1280x720 drawable 1280x720` | yes | 1920x1080: as row 3 |
| 5 | `DEBUG: New DRM FB … 1280x720`; `V3DA srv import … pages≈901` (1280·720·4 B + Mesa's scan-out padding; 1080p gave 2026) | yes | pages≈2026: a 1080p buffer |
| 6 | **fps** (metric above) | table | ≤ 13 at 720p: the GPU time did not fall — check `V3DA srv qstat` render ms/job against the 1080p control |
| 7 | `KMS srv flipstat client=…` | mostly `vbl1`, `deferred≈flips` | — |
| 8 | **HDMI** after the banner: the lit hacienda race **filling the screen**, upright, HUD and STK's FPS counter legible (softer than 1080p), no console text, no tearing, no garbage at the edges | yes | top-left quarter only: dst not applied (m9a row 4); **3D upside down while the HUD is upright**: the old-lane Mesa Y-flip size gate (`FBO ≥ 1024×768`, stk-launcher.c:109–116) exists on this Mesa too — it is not in `mesa-drm/patches`, so predicted absent, a finding if seen |
| 9 | exit: `profile: Number of frames …`, `KMS mode crtc=0 1920x1080 native … why=primary_off`, `KMS srv client <n> closed`, then `KMSTEST crtc rc=0 mode=1920x1080 fb=0` | the game's exit restores | `mode=1280x720`: FAIL (m9c) |
| 10 | `V3DA srv qstat` err/wedges/rej; faults | 0/0/0; 0 (except the known libphoenix `fclose(stdout)` exit fault if that fix is not in the image) | addr2line the unstripped `supertuxkart-drm` |

`m9b-stk-540` and `-900`: the same rows with their sizes (`pages≈` 507 for 960×540, 1407 for
1600×900); `-1080`: no `KMS mode … scaled` line at all (native requested), fps = control.

**Decides:** the fps table is the owner's answer (which mode to offer by default); rows 8–9 = scaled
fullscreen is correct and never outlives the game.

### 6.3 `m9c-restore` — after the game, the console and the next game get native (≈ 11–12 min, detached)

**Question:** after a game in a lower mode exits by itself, is the next thing on HDMI — the console, a
probe, and a default-resolution game — at the native mode?

```
./scripts/test-cycle-psh-interact.sh --label m9c-restore --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 420 --ready-line 'V3DA srv detached|KMS srv detached|profile: Number of frames|KMSTEST done|quakespasm-drm: first swap' \
    --ready-extra-secs 45 --hdmi-dense-on 'stk-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-g9 -G -C" \
    "/bin/game-res stk 960x540 --track=hacienda --numkarts=1 --profile-laps=1" \
    "/bin/kmstest-g9 crtc" \
    "/bin/kmstest-g9 modes-leave" \
    "/bin/kmstest-g9 crtc" \
    "/usr/bin/quakespasm-drm"
```

| # | line / observation | predicted | if instead… |
|---|---|---|---|
| 1 | STK: `KMS mode … 960x540 scaled`, `first swap … 960x540`, `KMS console handover disable rc=0`; HDMI: the race full screen | yes | as m9b |
| 2 | STK exits: `profile: Number of frames`, **`KMS mode crtc=0 1920x1080 native … why=primary_off`**, `KMS console handover enable rc=0`, `KMS srv client <n> closed` | SDL's own SetCrtc(fb 0, original mode) or the close | no native line before `client closed`: FAIL |
| 3 | `KMSTEST crtc rc=0 mode=1920x1080 fb=0`; **HDMI (tick after the race): the text console, native-size glyphs**, no frozen race frame | yes | a frozen 960×540 frame: the plane was not unset (compare `planes_off=`) |
| 4 | `modes-leave` → `KMS mode … native … why=primary_off` at its close; `KMSTEST crtc … mode=1920x1080 fb=0` | the close path | as m9a row 12 |
| 5 | `quakespasm-drm: new GPU lane`, **no `KMS mode … scaled` line**, `quakespasm-drm: first swap … window 1920x1080 drawable 1920x1080`; HDMI: the attract demo at full resolution | the next default game is native (its `config.cfg`: 1920×1080 fullscreen) | a scaled line or 960×540: a mode leaked from the previous client — FAIL |
| 6 | faults; `V3DA srv qstat` | 0; 0/0/0 | addr2line |

**Decides:** "return to the normal resolution when the game ends" on hardware, by the game's own exit
(rule 1) and by a client that just closes (rule 2), plus "the next game is unaffected".

## 7. Open points

- The firmware scaler on this Pi (m9a is the first `src ≠ dst` SET_PLANE). If it ignores dst sizes, the
  fallback is the HVS display list (what full KMS does), a larger step.
- Scaling filter quality is the firmware's choice (not settable through SET_PLANE); a "nearest"
  option would need that too.
- FRAMEBUFFER_BLANK is only used for modes with bars; `-B` still blanks at every mode.
- A desktop compositor that later wants per-output scaling can use plain plane scaling today; an
  output-scale property (`scaling mode`) is not exposed (no client asks for it yet).
- Known small gaps: the lower mode's `MODE_ID` blob is written at each mode change, so a later refresh
  re-measure updates the native blob but not it (sizes are what clients compare); if clients hold all
  16 blob slots, `MODE_ID` reads the native blob while the CRTC is scaled; the host test has no
  "fence-deferred commit + mode change" row (SDL's SET_CRTC carries no fence, so it does not occur in
  the M9 paths).
- Side observations (2026-09-28): `/usr/bin/vkquake-drm` on the export is `53eb25309585a3d4` (re-staged
  04:22), not MIGRATION §6's `22755bb450b09e0f` — check before grading any vkq cycle.
  `scripts/check-gpu-lane-ports-sync.sh` is clean after g9 (kms/ has no ports copy besides
  `kms_proto.h`, which is unchanged).
