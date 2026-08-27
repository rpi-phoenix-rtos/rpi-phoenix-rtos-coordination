# SuperTuxKart (G-STK) — staged port plan for Phoenix-RTOS / Raspberry Pi 4

Date: 2026-08-27
Status: scoping done + **M0 COMPLETE** (enet, libogg, libvorbis, libsamplerate, harfbuzz) + **M1 DONE** (SDL2 already carries the phoenix /dev/audio0 backend — SDL_phoenixaudio) + **M2 COMPLETE — STK 1.4 cross-CONFIGURES clean** (§10) + **M3 COMPLETE — supertuxkart aarch64-phoenix static ELF LINKS with 0 undefined symbols** (§11). Next: M4 (runtime init on HW).
Target host: aarch64-phoenix cross, static linking, framework ports only.

---

## 0. TL;DR / verdict

SuperTuxKart **1.4** is portable to Phoenix/Pi4 and is **far smaller in scope than
previously assumed**, because modern `stk-code` **bundles almost every dependency
the earlier scoping flagged as "must-port"**: bullet, enet, angelscript, mcpp,
libsquish, its Irrlicht fork, the GE engine, and **MojoAL** (a self-contained
OpenAL implementation) all live in `stk-code/lib/` and are built in-tree.

The renderer question — the historical blocker — resolves in our favour:
- STK's `USE_GLES2` path (default ON for Linux/embedded) drives its Irrlicht fork's
  `CIrrDeviceSDL`, which creates the GL context with **`SDL_GL_CreateContext`
  requesting an OpenGL **ES 3.0** context** (`SDL_GL_CONTEXT_PROFILE_ES`,
  MAJOR=3/MINOR=0). This is **exactly the EGL-free SDL2 path already proven by
  yQuake2 gl3** (which negotiated ES 3.1 on our V3D). **No EGL/GLX/DRM is on the
  SDL device's path** (`CContextEGL` is only used by the Wayland device).
- We have **GLES 3.1 HW-proven** on V3D 4.2 and a full textured 3D game (yQuake2
  gl3) rendering through the identical SDL glue. STK requests ES 3.0 ⊂ ES 3.1.

**This supersedes the MASTER-RECONCILED-PLAN G-STK entry (2026-08-21) that called
for a Vulkan-only (`ge_vulkan`) port.** That entry predates the 2026-08-27 GLES3
proof and was written when our GL was believed to be 2.1-only. The GLES3/SP path
is now the recommended primary because (a) it reuses a proven, debugged pipeline
(yQuake2 gl3), (b) V3DV Vulkan currently carries an open scanout-striping bug STK
would inherit, and (c) `ge_vulkan` needs SPIR-V shader tooling (`lib/shaderc`) and
a WSI/swapchain-on-fb0 shim we don't have. **`ge_vulkan` remains a documented
fallback** if the SP/GLES3 path hits a wall.

The genuine **must-port** list collapses to the **audio decode chain** (libogg →
libvorbis → libsamplerate) plus exposing harfbuzz as a system-findable lib. **All
of these are now done** (enet, libogg, libvorbis, libsamplerate, harfbuzz — see
§9): **M0 is COMPLETE.**

Honest scale: still a **multi-week** effort, but SMALLER than first thought — M0 (deps)
✓, M2 (configure) ✓, and **M1 (SDL2 audio backend) is ALREADY DONE** (the SDL2 port
already ships the SDL_phoenixaudio /dev/audio0 driver — see §5; the earlier "dummy audio"
claim was wrong). Remaining long poles: (1) getting the large C++/Irrlicht/SP app to
compile+link statically (M3, IN PROGRESS), (2) **asset staging** (~650 MB compressed /
~900 MB–1 GB uncompressed — RAM-staging impossible; NFS-root for dev), and (3) **runtime
iteration** on the V3D GLES3 path (single-GPU-process, TFU tiling quirks, perf) to first
frame + playability, plus the owner-attended audio-output sign-off.

---

## 1. Target version

**SuperTuxKart 1.4** (`stk-code` tag `1.4`, `PROJECT_VERSION "1.4"`). Rationale:
- Uses the modern **SP (Shader Pipeline) + GE** renderer with a first-class
  **`USE_GLES2` / OpenGL ES 3.0** path (Irrlicht `COGLES2Driver` + ES3 shaders),
  which is what our V3D stack serves.
- Bundles all heavy deps in-tree (see §2), minimising new ports.
- Matches the `stk-assets` 1.4 release (asset/engine version lock-step).

Do **not** target legacy STK (0.8.x): that needs a bespoke Irrlicht
`COpenGLDriver`-on-scanout glue and would not reuse the proven SDL2+GLES3 path.

---

## 2. Dependency classification

Legend: **[P]** already ported (in the framework prefix) · **[B]** bundled in
`stk-code/lib/`, built in-tree, no port needed · **[M]** must-port.

| Dependency | Class | Notes |
|---|---|---|
| SDL2 | **[P]** | `libSDL2.a` present; EGL-free GLES-capable glue proven (yQuake2 gl3). Needs an **audio backend** added (see §5) for MojoAL. |
| zlib | **[P]** | present |
| libpng | **[P]** | present |
| libjpeg (turbo) | **[P]** | present; STK `find_package(JPEG REQUIRED)` |
| freetype | **[P]** | `libfreetype.a` present; `find_package(Freetype)` |
| curl | **[P]** | present; `find_package(CURL REQUIRED)` (online/news/addons) |
| mbedtls / openssl | **[P]** | `libmbedcrypto/mbedtls/mbedx509` present |
| sqlite3 | **[P]** | present; `USE_SQLITE3` (server stats/ban list; falls back OFF if absent) |
| **enet** | **[P] (this work)** | `USE_SYSTEM_ENET` ON by default → `pkg_check_modules(ENET libenet>=1.3.4)`. Ported here (1.3.18) **with a hand-emitted `libenet.pc`** so pkg-config resolves it. **Caveat:** the system-enet branch is gated on `UNIX AND NOT APPLE` (CMakeLists L230), which is **FALSE under `CMAKE_SYSTEM_NAME=Generic`** — see the M2 trap below. Bundled copy is the fallback / used when `USE_IPV6`. |
| bullet (physics) | **[B]** | `lib/bullet` |
| angelscript (scripting) | **[B]** | `lib/angelscript`; `USE_SYSTEM_ANGELSCRIPT` default OFF |
| mcpp (preprocessor) | **[B]** | `lib/mcpp`; `find_library(MCPP)` **falls back to bundled** when absent |
| libsquish (DXT) | **[B]** | `lib/libsquish`; `USE_SYSTEM_SQUISH` **falls back to bundled** when absent |
| MojoAL (OpenAL impl) | **[B]** | `lib/mojoal`; selected with **`-DUSE_MOJOAL=ON`** → avoids porting OpenAL-soft entirely |
| Irrlicht (STK fork) | **[B]** | `lib/irrlicht`; `CIrrDeviceSDL` + `COGLES2Driver` = our GLES3 path |
| GE (graphics engine) | **[B]** | `lib/graphics_engine`; GL 2D + Vulkan backend |
| sheenbidi / tinygettext / graphics_utils / simd_wrapper / dnsc | **[B]** | all `lib/*`, in-tree |
| wiiuse | **[B]** | `lib/wiiuse`; disable with `-DUSE_WIIUSE=0` (no wiimotes on Pi) |
| shaderc | **[B]** | `lib/shaderc`; only needed for the Vulkan renderer → skip on GLES3 path |
| **libogg** | **[M] → [P] (this work)** | Ported here (1.3.5). Base for libvorbis. **Not bundled.** |
| **libvorbis / vorbisfile** | **[M] → [P] (this work)** | Ported here (1.3.7). `find_package(OggVorbis REQUIRED)`. **Not bundled.** Needed libogg (done); FindOgg cache-var pin. |
| **libsamplerate** | **[M] → [P] (this work)** | Ported here (0.2.2). Required **only when `USE_MOJOAL=ON`** (pitch handling). No hard deps. |
| **harfbuzz** | **[M]/(expose) → [P] (this work)** | Exposed here (2.6.7 framework port, freetype backend). `find_library(HARFBUZZ REQUIRED)` + `<hb.h>`/`hb-ft.h` now resolve in the prefix. |
| GLEW | **n/a** | Not needed — in-process Mesa V3D GL/GLES provides entrypoints. |
| EGL / GLX / DRM / KMS | **n/a / avoid** | SDL device path avoids all of these. |

**Key correction vs the task's initial assumption:** bullet, angelscript, mcpp,
libsquish, and OpenAL do **not** need standalone ports — STK builds them itself.
Porting mcpp/libsquish standalone (a task suggestion) would be wasted effort.

### There is no "graphics without sound" build
The only switch that removes sound is `SERVER_ONLY`, which **also removes all
graphics**. A graphical STK therefore hard-requires the audio decode chain at link
time (`-DENABLE_SOUND` is added unconditionally in the non-server branch). So
libvorbis + (MojoAL|OpenAL) + libsamplerate are on the **critical path even to
render a single frame** — they cannot be deferred with a stub.

---

## 3. Must-port order & effort (smallest/cleanest first)

1. **libogg 1.3.5** — DONE (this work). ~2 files, CMake, no deps, clean. Effort: trivial.
2. **enet 1.3.18** — DONE (this work). 8 files, CMake, one portability patch. Effort: trivial. *(Bonus: enet is a reusable general port; consumed by STK via `USE_SYSTEM_ENET`.)*
3. **libvorbis 1.3.7** — DONE (this work). Depends on libogg (`depends="libogg"`). CMake, static. Provides `libvorbis.a` + `libvorbisfile.a` + `libvorbisenc.a`. `alloca` resolved via gcc builtin (no autotools `config.h`); no `clock` gap surfaced.
4. **libsamplerate 0.2.2** — DONE (this work). For `USE_MOJOAL=ON`. CMake, static, no hard deps (examples/tests + SndFile/FFT off).
5. **harfbuzz (expose) 2.6.7** — DONE (this work). Added a `harfbuzz` framework port driving upstream's CMake build (freetype backend ON; glib/icu/cairo/subset off); `find_library(HARFBUZZ)` + `<hb.h>` + `hb-ft.h` now resolve in the prefix.

After these five, **every STK dependency is satisfied** (ported or bundled). **M0 is COMPLETE.**

### Not to be ported (bundled — confirmed by reading `stk-code/CMakeLists.txt`)
bullet, angelscript, mcpp, libsquish, MojoAL, Irrlicht, GE, sheenbidi,
tinygettext, wiiuse, shaderc, dnsc, graphics_utils, simd_wrapper.

---

## 4. Renderer strategy (GLES3 via SDL2 — primary)

- Build STK with **`-DUSE_GLES2=ON`** (default for Linux non-Apple). This selects
  the Irrlicht `COGLES2Driver` + STK **SP** renderer + ES3 GLSL shaders.
- Context path: `CIrrDeviceSDL::createWindow` → `SDL_GL_SetAttribute(PROFILE_MASK,
  ES)`, `MAJOR=3/MINOR=0` → `SDL_GL_CreateContext`. Our SDL2 phoenix GL glue
  already honours the ES request (`profile_mask=0x4 → OpenGL ES`, proven with
  yQuake2 gl3 → ES 3.1 Mesa 26.2.0). **No new context code expected.**
- Present path: our in-process `/dev/fb0` present + triple-buffered scanout FBOs
  resolve to fb (the yQuake2 gl3 mechanism). STK renders to the default framebuffer
  → SDL swap → our glue blits/scans out.
- **Shader risk to watch:** STK's SP shaders are `#version 300 es` / possibly some
  `310 es` compute or SSBO usage. We have ES 3.1 (compute + SSBO capable in
  Mesa/V3D). Verify STK's `central_settings` GLES minimum matches; some SP features
  (bindless-ish texture arrays, image load/store) may need feature-gating or the
  "legacy device" (`--hardware-skinning=0`, lower graphics preset).
- **TFU tiling:** the V3D TFU "VERTICAL-MISMATCH" texture class (seen in yQuake2 gl3
  / q3dm7) may surface on STK textures; the NPOT-mip render-fallback fix
  (`project_v3d_npot_mip_fix`) and mipmap-off lever are available. Expect iteration.

**Vulkan (`ge_vulkan`) fallback:** only if the SP/GLES3 path proves infeasible.
Requires `lib/shaderc` (SPIR-V), a V3DV WSI/swapchain-to-fb0 shim (we have none —
vkQuake used a custom present), and inherits the open V3DV scanout-striping bug.
Higher risk; documented, not primary.

---

## 5. Audio strategy

**★ CORRECTION 2026-08-27: M1 (SDL2 audio backend) is ALREADY DONE — the earlier
"our SDL2 is dummy audio" claim below was WRONG.** The SDL2 port already ships a full
phoenix `/dev/audio0` audio driver: `sources/phoenix-rtos-ports/sdl2/overlay/src/audio/
phoenix/SDL_phoenixaudio.{c,h}` + patch `0006-cmake-phoenix-audio-driver` (SDL_AUDIO_DRIVER_
PHOENIX gates it + registers `&PHOENIXAUDIO_bootstrap` in SDL's driver list). VERIFIED in the
current `libSDL2.a`: `PHOENIXAUDIO_bootstrap` (defined) + `PHOENIXAUDIO_{Init,OpenDevice,
GetDeviceBuf,PlayDevice,WaitDevice,CloseDevice}` all present (full SDL AudioBootStrap). A smoke
test exists (`tools/sdl2-port/sdl2-audiotest.c` — opens the default device = the phoenix driver,
440Hz sine via the callback→conversion→GetDeviceBuf path). So MojoAL's `SDL_OpenAudioDevice`
will get the PHOENIX driver → /dev/audio0 with NO new work. Remaining: only the runtime
audio-OUTPUT sign-off (owner-attended, needs headphones — the /dev/audio0 driver itself is
HW-verified per project_pi4_audio_driver). **⇒ the "single biggest audio task" is a non-issue;
STK audio is satisfied by the existing SDL2 backend + bundled MojoAL + ported libvorbis/
libsamplerate.**

- **Use MojoAL** (`-DUSE_MOJOAL=ON`), bundled, single-file OpenAL over SDL2's audio device
  → the existing PHOENIXAUDIO driver → /dev/audio0.
- MojoAL requires **`libsamplerate`** (pitch — ported) + the SDL2 audio backend (DONE, above).
- Decode: **libvorbis/vorbisfile** (ported). [Superseded stale note kept for history:]
  ~~Our SDL2 is dummy audio; a phoenix SDL2 audio driver is the single biggest audio task.~~
- **Sequencing option:** because there is no graphics-without-sound build, the
  audio chain must link before first render. If the SDL2 phoenix audio driver
  slips, a **temporary local patch** to STK's `SFXManager`/`MusicManager` init to
  tolerate a no-op audio device (open MojoAL against SDL dummy audio) can unblock a
  first *visual* frame while real audio lands — but the decode libs still must link.

---

## 6. Asset strategy

- Full 1.4 game assets ≈ **652 MB compressed** (`SuperTuxKart-1.4-linux-arm64.tar.xz`),
  ≈ **900 MB–1 GB uncompressed** (`stk-assets` + `stk-code/data`). `stk-code/data`
  alone (shaders/GUI/descriptors, ships with the source) is **46 MB**.
- **RAM-staging is impossible** (reference: 50 MB → /tmp took 11.5 s; ~1 GB is out).
- **Recommended:**
  - **Dev:** serve assets from the **NFS root** (host holds `stk-assets` + `data`;
    Pi runs `/` on NFS — proven, `project_nfs_rootfs_feasibility`). Slow first-load
    but zero staging. Use the read-ahead clustering perf work already landed.
  - **Later / standalone:** **SD ext2 root** (`project_pi4_sdroot_120`) with the
    asset tree on card.
  - **Lightweight first bring-up:** the **1.4 APK asset set is only 168 MB**
    (mobile-reduced) — a candidate minimal data set to reach "renders a track"
    faster before committing the full 1 GB.

---

## 7. Milestones

| # | Milestone | Gate / evidence |
|---|---|---|
| M0 | Deps ported | **COMPLETE** ✅ — enet ✅, libogg ✅, libvorbis ✅, libsamplerate ✅, harfbuzz-exposed ✅ all build clean in the framework prefix via `scripts/build-port.sh` (see §9). Every STK dependency is now ported or bundled. (M0 = deps only; the SDL2 `/dev/audio0` backend is M1, not part of this gate.) |
| M1 | SDL2 audio backend | ✅ **DONE (found 2026-08-27)** — SDL_phoenixaudio driver already in libSDL2.a (PHOENIXAUDIO_bootstrap + full OpenDevice/PlayDevice/…); MojoAL will use it. Runtime audio-out sign-off owner-attended. |
| M2 | STK configures | **COMPLETE** ✅ — `cmake` (cross toolchain file, `-DUSE_GLES2=ON -DUSE_MOJOAL=ON -DUSE_WIIUSE=0 -DCHECK_ASSETS=OFF -DBUILD_RECORDER=OFF -DUSE_DNS_C=ON -DUSE_CRYPTO_OPENSSL=OFF`) reaches `Configuring done` + `Generating done`, all find_package satisfied from the shared ports prefix, no host leakage; bundled `mcpp` compiles with the cross toolchain. Trap resolution + full detail in §10. |

> **M2 TRAP — `CMAKE_SYSTEM_NAME` and STK's `UNIX`-gated defaults.** Our standard
> cross pattern (sdl2/libjpeg/enet/libogg ports) sets `CMAKE_SYSTEM_NAME=Generic`,
> under which CMake's **`UNIX` variable is FALSE**. But several STK defaults are
> gated on `UNIX AND NOT APPLE`: the `USE_GLES2=ON` default (CMakeLists L90/94) and
> the `USE_SYSTEM_ENET` system-enet branch (L230). Under `Generic` these blocks do
> **not** fire — STK would default to desktop GL and silently use bundled enet.
> Fixes at STK-configure time: **pass the UNIX-gated options explicitly**
> (`-DUSE_GLES2=ON`, and if system enet is wanted, adjust the L230 guard or accept
> bundled enet), **or** configure STK specifically with `CMAKE_SYSTEM_NAME=Linux`
> so `UNIX` is true (evaluate side-effects: host-probe leakage the `Generic` setting
> was avoiding). This is a real trap for whoever executes M2; it does not affect the
> already-landed enet/libogg ports, which build under `Generic` correctly.
| M3 | STK links | **COMPLETE** ✅ — static `supertuxkart` aarch64-phoenix ELF (45.7 MB, EXEC, no PT_INTERP), **0 undefined symbols**, via `scripts/build-port.sh supertuxkart`. All STK src + bundled Irrlicht/GE/bullet/angelscript/mojoal/shaderc compile on the GLES2/SP path; GLES entrypoints resolve against `tools/.gpu-libs/libGL-phoenix.a`+`libv3d-phoenix.a` in the group-link. Detail + categorized gap fixes in §11. |
| M4 | STK inits headless-ish | binary starts, loads config/assets over NFS, creates the SDL window + **ES 3.0 GL context** (UART/log evidence), no early fault |
| M5 | Renders a frame | main menu / a track renders to `/dev/fb0` on HDMI (HDMI snapshot evidence) — the "first frame" prize |
| M6 | Playable | a race runs at usable fps with input (USB kbd/gamepad via SDL) + audio |

Milestones are gated on the Pi resource (owner-held); M0–M3 are host-side.

---

## 8. Risks / honest multi-week assessment

- **Biggest unknown = SP/GLES3 shader compatibility at runtime** on V3D (feature
  use, TFU tiling, single-GPU-process constraints). yQuake2 gl3 de-risks the
  context + basic pipeline but STK's SP is far heavier (deferred-ish shading,
  shadow maps, many shader variants). Expect real iteration; a lower graphics
  preset / feature-gating is the likely first-playable config.
- **SDL2 phoenix audio driver** is new Phoenix work (not a port); non-trivial.
- **C++ scale:** a large C++ app statically linked under libphoenix/libstdc++.
  De-risked in kind by Dillo (FLTK/C++) but STK is much larger; expect link-surface
  gaps (locale, threads, exceptions, `<filesystem>`).
- **Asset I/O perf** over NFS: first-load of ~1 GB will be slow; read-ahead
  clustering helps but load times will be long during bring-up.
- **Vulkan fallback** carries its own WSI + striping-bug tax if the GLES3 path fails.

Realistic estimate: **several weeks** of focused work across host-side deps
(days), the SDL audio backend (days), configure+link (days–a week of gap-chasing),
then **open-ended runtime iteration** to first frame and playability.

---

## 9. Work landed with this plan

Committed to the `phoenix-rtos-ports` sibling (`master`, not pushed):

- **`enet` 1.3.18** — `e4e3b68`. `sources/phoenix-rtos-ports/enet/`
  (`port.def.sh` + `patches/0001-phoenix-socket-const-fallbacks.patch`).
  All 8 CMake feature probes resolve against libphoenix; installs a `libenet.pc`
  (`pkg-config --modversion libenet` → 1.3.18); cross link-smoke
  (`enet_initialize`/`host_create`/`host_service`) → static aarch64 ELF, 0
  undefined symbols. Discoverable by STK's `USE_SYSTEM_ENET` **subject to the M2
  Generic-vs-UNIX trap above**.
- **`libogg` 1.3.5** — `21e954d`. `sources/phoenix-rtos-ports/libogg/`
  (`port.def.sh`, no patches). `libogg.a` + `include/ogg/` + `ogg.pc`; correct
  aarch64 int typedefs in generated `config_types.h`. Base for the next dep
  (libvorbis).
- **`libvorbis` 1.3.7** — `9b655a5`. `sources/phoenix-rtos-ports/libvorbis/`
  (`port.def.sh`, no patches). `depends="libogg"`. `libvorbis.a` +
  `libvorbisfile.a` + `libvorbisenc.a` + `include/vorbis/` + three `.pc`.
  **libogg find mechanism:** `find_package(Ogg REQUIRED)` satisfied by
  pre-seeding the bundled `FindOgg.cmake` cache vars `OGG_INCLUDE_DIR` +
  `OGG_LIBRARY` from `${PORT_DEP_libogg}` (the shared build prefix); FindOgg
  honours pre-set cache vars and short-circuits pkg-config, and the version-less
  find can't fail on the empty `OGG_VERSION_STRING`. `alloca()` → gcc builtin
  (CMake path never defines `HAVE_CONFIG_H`). Cross link-smoke
  (`vorbis_info_init` + `ov_open_callbacks`) → static aarch64 ELF, 0 undefined.
- **`libsamplerate` 0.2.2** — `5b65151`. `sources/phoenix-rtos-ports/libsamplerate/`
  (`port.def.sh`, no patches). No hard deps (`LIBSAMPLERATE_EXAMPLES=OFF`,
  `BUILD_TESTING=OFF` → no SndFile/FFTW/ALSA). `libsamplerate.a` +
  `include/samplerate.h` + `samplerate.pc`. Cross link-smoke (`src_new` +
  `src_simple`) → static aarch64 ELF, 0 undefined. Needed for `USE_MOJOAL=ON`.
- **`harfbuzz` 2.6.7** — `fca5715`. `sources/phoenix-rtos-ports/harfbuzz/`
  (`port.def.sh`, no patches). Drives upstream's **CMake** build (not the
  autotools path `tools/x11-port` uses). `depends="xorg_fonts"` (sole freetype
  provider; it builds freetype harfbuzz-less → no cycle). `HB_HAVE_FREETYPE=ON`,
  glib/icu/graphite2/cairo/subset/utils OFF. `find_package(Freetype)` pinned via
  its FindFreetype cache vars (`FREETYPE_LIBRARY` +
  `FREETYPE_INCLUDE_DIR_ft2build/_freetype2`) at `${PORT_DEP_xorg_fonts}` to
  defeat the `Generic`-mode fallthrough to host `/usr` freetype. C++ toolchain
  driven explicitly (`CROSS g++`, CFLAGS reused as CXXFLAGS — the fltk pattern,
  since `reset_env` exports no CXXFLAGS). Installs `libharfbuzz.a` +
  `include/harfbuzz/*.h` (incl. `hb-ft.h`) + hand-emitted `harfbuzz.pc`. C++
  cross link-smoke (`hb_buffer_create` + `hb_ft_font_create_referenced`, the
  hb-ft bridge STK's font manager is expected to use) → static aarch64 ELF,
  0 undefined.

All five build clean via `scripts/build-port.sh <name>`. **M0 (all STK deps
ported/available) is COMPLETE.** Remaining STK gates (M1 SDL2 audio backend
onward) are unaffected by this work.

---

## 10. M2 landed — STK 1.4 cross-configures clean

Committed to the `phoenix-rtos-ports` sibling (`master`, not pushed):
`sources/phoenix-rtos-ports/supertuxkart/` — `port.def.sh`,
`aarch64-phoenix.cmake` (toolchain file), `patches/0001..0003`.

**Verdict: `cmake` configure + generate COMPLETE** (`Configuring done` /
`Generating done`, exit 0) for `aarch64a72-generic-rpi4b` via
`scripts/build-port.sh supertuxkart`. The bundled `mcpp` lib also compiles with
the cross toolchain (configure sanity smoke). The full executable link is **M3**
and was deliberately not attempted.

### Source pin
GitHub tag-1.4 archive `stk-code-1.4.tar.gz` (the auto-generated tarball, which
bundles `lib/*` and `data/` in-tree — tag 1.4 has no `.gitmodules`):
size `32646035`, sha256 `40ff14ce0e1fde05fa9f427bfe1f75917a6f4efbf2c1a86421a7f794d05189b9`.
`b_port_download`'s `(filename, orig_filename)` array form saves the remote
`1.4.tar.gz` under the descriptive local name.

### Generic-vs-UNIX trap — resolution
Kept **`CMAKE_SYSTEM_NAME=Generic`** (every sibling port's choice; avoids
host-`/usr` leakage and lets `FindOggVorbis`'s manual else-branch be pinned)
and dealt with the UNIX-gated fallout explicitly:
- **`-DUSE_GLES2=ON`** passed explicitly (the arm/aarch64 auto-default is
  UNIX-gated → would not fire under Generic → desktop GL).
- **Bundled enet** (`-DUSE_SYSTEM_ENET=OFF`): the system-enet branch is both
  UNIX-gated *and* skipped when `USE_IPV6=ON` (the default), so the ported enet
  is not consumed by this config — as anticipated in §2.
- **Three configure-only portability patches** (none touch runtime code):
  - `0001` — STK's `cmake/FindFreetype.cmake` else-branch calls
    `pkg_check_modules(freetype2)`, but under Generic the UNIX-gated
    `include(FindPkgConfig)` never ran, so that command is undefined → configure
    abort. Route Generic through the existing manual find branch.
  - `0002` — STK forces policy `CMP0043 OLD`; host **cmake 4.2.3** removed OLD
    support for it (hard error). Gate the `cmake_policy` on cmake < 4.0.
  - `0003` — bundled `lib/shaderc/third_party/spirv-tools` `FATAL_ERROR`s on any
    unknown `CMAKE_SYSTEM_NAME` ("platform 'Generic' is not supported"). Add a
    Generic branch treating it as Linux (mirrors STK's own NintendoSwitch
    addition; `SPIRV_LINUX` only selects ANSI colours). shaderc/glslang/
    spirv-tools/spirv-headers then configure.
- `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (mandatory under cmake 4.x for STK's
  `cmake_minimum_required(2.8.4)`).

### find_package / find_library — all resolved, none blocked
Toolchain file confines finds to the shared ports prefix + phoenix sysroot
(`CMAKE_FIND_ROOT_PATH_MODE_{LIBRARY,INCLUDE,PACKAGE}=ONLY`, `PROGRAM=NEVER` so
host python3 is found for SPIRV-Tools). Every dep cache var was pinned to the
shared prefix; verified in the configure log:
- Found **JPEG** (v62), **ZLIB** (1.2.11), **PNG** (1.6.40) — Irrlicht's
  `find_package(... REQUIRED)`.
- Found **OggVorbis** (manual branch, all 6 `OGGVORBIS_*` vars incl. vorbisenc).
- **Freetype** (patched module) + **HARFBUZZ** (`include/` so `<harfbuzz/hb.h>`
  resolves, matching STK's `src/font`), **SDL2**, **libsamplerate** (MojoAL),
  **sqlite3**, **CURL** — all "Use system …" from the shared prefix.
- **MbedTLS** selected for crypto (`-DUSE_CRYPTO_OPENSSL=OFF` + pinned
  `MBEDTLS_INCLUDE_DIRS`/`MBEDCRYPTO_LIBRARY`), avoiding `find_package(OpenSSL)`
  (openssl is a `conflicts`/versioned port, not in the shared prefix).
- **Generate-step NOTFOUND landmines** pre-empted (these fail at *generate*, not
  configure, when a `*-NOTFOUND` reaches `target_link_libraries`):
  `PTHREAD_LIBRARY` pinned to `sysroot/lib/libpthread.a`; `LIBRESOLV_LIBRARY`
  removed by `-DUSE_DNS_C=ON` (bundled resolver — STK's designed fallback);
  vorbisenc pinned.
- Bundled fallbacks (no system lib, as intended): **mcpp**, **libsquish**,
  **shaderc**, **angelscript**, **MojoAL**, **bullet**, **Irrlicht**, **GE**,
  **sheenbidi**, **tinygettext**, **wiiuse** (off). `graphics_engine` emits one
  non-fatal warning (no system `astcenc` → ASTC support off).

### GL / GLES wiring — configure needs nothing; M3 link plan
STK's bundled Irrlicht and GE do **not** `find_package(OpenGL/OpenGLES/EGL/X11)`
at configure. The renderer is selected purely by preprocessor defines: with
`USE_GLES2=ON` STK adds `-DUSE_GLES2 -D_IRR_COMPILE_WITH_OGLES2_
-DNO_IRR_COMPILE_WITH_OPENGL_`, so `COGLES2Driver.cpp` / GE `gl.c` reference GLES
entrypoints (`glActiveTexture`, `glCreateProgram`, …) that remain **unresolved
in the static libs until the final executable link**. So configure resolves no
GL lib, and none is needed at M2.

**M3 GL-link plan (reuse the yQuake2 gl3 mechanism):** link the `supertuxkart`
ELF against our in-process ported Mesa —
`tools/.gpu-libs/libGL-phoenix.a` (17 MB) + `libv3d-phoenix.a` (18 MB), both
present — using the proven group-link so the GLES symbols resolve:
`-Wl,--start-group libSDL2.a libGL-phoenix.a libv3d-phoenix.a -Wl,--end-group`,
plus the SDL2 phoenix GL-context glue (`sdl_phoenix_glctx.c` /
`sdl_phoenix_glstubs.c` under the sdl2 port's glue dir) that services the ES 3.0
context request (`SDL_GL_CONTEXT_PROFILE_ES`, MAJOR=3/MINOR=0) STK's
`CIrrDeviceSDL` makes — identical to the yQuake2 gl3 path that negotiated ES 3.1
on our V3D. Injection point: extend the `supertuxkart` target's link (a small
STK `target_link_libraries` patch or `-DCMAKE_EXE_LINKER_FLAGS`) in the port's
M3 `p_build`. See `sources/phoenix-rtos-ports/yquake2/port.def.sh` for the exact
flag/glue recipe to copy.

### Known remaining M3 (link) risks — NOT blockers for M2
- The GLES entrypoint resolution above (needs the group-link + glue).
- Large C++ static link under libphoenix/libstdc++ (locale, threads,
  exceptions, `<filesystem>`) — de-risked in kind by Dillo but STK is larger.
- GE compiles Vulkan sources (`ge_vulkan_*`) + bundled `vulkan.c` even on the
  GLES path; they must at least compile/link (runtime unused on SP/GLES3).
- `-DUSE_MOJOAL=ON` links MojoAL against SDL2 audio → needs the **M1** SDL2
  `/dev/audio0` backend before a graphical build is actually runnable (there is
  no graphics-without-sound build; §2/§5).

### Reproduce (M2)
`scripts/build-port.sh supertuxkart` — configure only was the M2 gate. The
`p_build` has since been extended to build+link (M3, §11).

---

## 11. M3 landed — supertuxkart aarch64-phoenix ELF links (0 undefined)

Committed to `phoenix-rtos-ports` (`master`, not pushed): extended
`supertuxkart/port.def.sh` `p_build` (configure → `make` → glue compile →
group-link → install), the toolchain file, `stk_phoenix_compat.h`, and patches
`0004`–`0010`.

**Verdict:** the full game — all 431 STK `src/*.cpp` objects + every bundled lib
(Irrlicht/GE/bullet/angelscript/mojoal/sheenbidi/tinygettext/shaderc/glslang/
SPIRV-Tools/libsquish/mcpp/dnsc/enet) — compiles on the GLES2/SP path, and the
`supertuxkart` executable **links to a static aarch64 ELF with 0 undefined
symbols** (`readelf`: ELF64 EXEC AArch64, statically linked, no PT_INTERP,
8 MB PT_GNU_STACK; 45.7 MB unstripped). Installs `/usr/bin/supertuxkart`.

### How GLES was wired (the prize mechanism)
STK's Irrlicht/GE select the renderer purely by preprocessor define
(`USE_GLES2=ON` → `-D_IRR_COMPILE_WITH_OGLES2_ -DNO_IRR_COMPILE_WITH_OPENGL_`)
and never `find_package` a GL lib, so the GLES entrypoints stay unresolved in
the static libs until the executable link — exactly the yQuake2 gl3 situation.
Two additions carry it:
- **Headers:** `-I${repo}/external/mesa/include` folded into the build CFLAGS so
  Irrlicht's `<GLES2/gl2.h>` / `<GLES3/gl3.h>` resolve everywhere (mesa/include
  ships GLES2/GLES3/KHR headers; nothing shadows libc).
- **Link:** CMake's own link of the target necessarily fails (GLES undefined), so
  `p_build` relinks from CMake's computed object/lib list
  (`CMakeFiles/supertuxkart.dir/link.txt`) with the two SDL2 phoenix GL-glue
  objects (`sdl_phoenix_glctx.c` = ES-3.0-context→Mesa/V3D winsys bridge +
  `/dev/fb0` present; `sdl_phoenix_glstubs.c`) compiled against the Mesa internal
  headers, plus a trailing `-Wl,--start-group libSDL2.a libGL-phoenix.a
  libv3d-phoenix.a libz.a libogg.a libvorbis.a libvorbisfile.a libvorbisenc.a
  -Wl,--end-group`. Identical seam to yQuake2 gl3 (libSDL2.a exports undefined
  `PHOENIX_GL_*`/`phxgl_*` that the glue provides). **M1 is already satisfied** —
  SDL2 carries the `/dev/audio0` backend (`SDL_phoenixaudio`), so MojoAL links
  against real audio, not dummy.

### Categorized gap fixes (all Phoenix libc/libstdc++ gaps; none change renderer)
Compile-surface (bundled 3rd-party):
- **GLES headers absent** → `-I mesa/include` (above).
- **BSD socket consts** `AF_MAX`/`SOMAXCONN`/`MSG_TRUNC` (dnsc, bundled enet) →
  force-included `stk_phoenix_compat.h` (macro-only).
- **simde `<fenv.h>`** — Phoenix's header is a poison-pill `#error` stub; simde
  auto-includes it in two blocks → patch `0004` skips both on `__phoenix__`
  (uses simde's non-fenv rounding fallback).
- **VMA `aligned_alloc`/`posix_memalign` absent** → patch `0005` adds a
  `__phoenix__` base-stashing malloc for `vma_aligned_alloc/free` (VMA is on the
  GLES path but its Vulkan allocator is never called at runtime).
- **glslang OSDependent** — under `CMAKE_SYSTEM_NAME=Generic` the `OSDependent/
  Unix` subdir wasn't built → bare unprovided `-lOSDependent`; patch `0007` adds
  Generic to the gate. Its `ossource.cpp` then needed `<semaphore.h>` (absent,
  and unused) dropped + thread-cleanup routed through the Android/Fuchsia path
  (no `pthread_setcanceltype`/`PTHREAD_CANCEL_*` on Phoenix) → patch `0008`.
- **spirv-tools timers** — `struct rusage` lacks `ru_maxrss/ru_minflt/ru_majflt`
  and there's no `CLOCK_PROCESS_CPUTIME_ID` → patch `0003` leaves
  `SPIRV_TIMER_ENABLED` off in the Generic branch.
- **`-std=gnu17` in C++ flags** — the framework CFLAGS carries a C-only `-std`
  that becomes fatal under bundled sub-projects' `-Werror`; the toolchain file
  now strips it from `CMAKE_CXX_FLAGS` only.

Compile-surface (STK's own src):
- **`swprintf` absent from libphoenix** (wchar.h has zero printf family;
  `libc.a` exports none) — Irrlicht + STK call it → patch `0006` adds a
  self-contained shim in `irrTypes.h` (numeric + wide-`%s`; the CZipReader sites
  pass `wchar_t*` to `%s`, handled as wide). **Real libphoenix gap — noted for a
  future proper `swprintf`/`vswprintf`.**
- **`std::wstringstream` absent** — Phoenix libstdc++ has no wide iostreams;
  only `spinner_widget.cpp` used it → patch `0009` formats via a narrow stream
  widened through `stringw`. **Real libstdc++ gap.** (`std::wstring` itself
  works; only the wide *streams* are missing.)
- **`LC_MESSAGES` absent** from Phoenix `locale.h` (has `LC_ALL`..`LC_TIME`
  only) → patch `0010` routes `translation.cpp` through its existing Windows
  `LC_CTYPE` branch.

Link-surface:
- **GLES/EGL entrypoints** → the group-link above (resolved, 0 left).
- **ogg/vorbis/zlib ordering** — CMake lists these as raw file paths in
  producer-first order, so single-pass `ld` can't back-resolve
  vorbisfile→vorbis→ogg / zlib (214 undefined) → added to the trailing group
  (resolved, 0 left).
- **No libstdc++ hole** surfaced — exceptions/threads/`<filesystem>`/locale all
  resolved from the Phoenix libstdc++ + libphoenix already in the link.

### Residual libc/libstdc++ gaps to fix upstream (tracked, not blocking M3)
- libphoenix: `swprintf`/`vswprintf` (wide-char printf family) — shimmed locally.
- libphoenix libstdc++: wide iostreams (`std::wstringstream` etc.) — avoided.
- libphoenix: no `aligned_alloc`/`posix_memalign` — VMA-local workaround.
These are the honest "fix in libphoenix later" items; the ELF links without them.

### Reproduce (M3)
`scripts/build-port.sh supertuxkart` → `/usr/bin/supertuxkart` (static aarch64
ELF). Requires the GL stack artifacts present: `tools/.gpu-libs/libGL-phoenix.a`
+ `libv3d-phoenix.a`, `external/mesa/{src,include}`, `/tmp/mesa-v3d-build/src`
(built by `tools/v3d-driver-port/build-gl-phoenix.py` + `build-v3d-phoenix.py`);
`p_build` fails loud if any is missing. Not attempted: M4 runtime bring-up on
HW (owner-gated Pi resource) and the ~1 GB `stk-assets` staging (§6).
