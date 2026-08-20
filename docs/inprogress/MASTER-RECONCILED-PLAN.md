# Master Reconciled Plan — Phoenix-RTOS RPi4

**Date:** 2026-08-21. **Purpose:** single source of truth reconciling the owner's
directives (2026-08-07 → 2026-08-21), the 66-session progress log, all ~70 memory
entries, docs/inprogress, the tech-debt ledger, and the tools/→ports migration.
Built from three independent read-only sweeps + this synthesis. Owner asked: nothing
lost; progress by plan; no new topics until this list is done or explicitly decided
un-doable.

**Authoritative state** = this doc + `autonomous-plan.md` (per-turn log). `status.md`
(2026-08-12) and `tracking/current-step.md` (2026-06-09) are **stale** — superseded.

Legend: ✅ DONE-HW · 🧪 needs test/validation · 🔧 in-progress · ⏸ waiting-decision ·
📋 to-do · ⛔ blocked/can't-complete-unattended.

---

## A. DONE — HW-verified (banked; here for confidence, not action)

- ✅ **Boot/kernel:** 4-core SMP; caches on; EL0 cntvct/CNTKCTL; SError-masked boot; pool-thread stacks (#152); console P1/P2/P3; fbcon (teken); read-ahead exec speedup.
- ✅ **Drivers:** genet eth (Tier-5 IRQ RX, 8.5 MB/s); /dev/{fb0(read),gpio(read),hwrng,thermal,audio0}; EMMC2 SD (#119); SD ext2 root (#120); SD PIO-write completion (#154, 16/16); USB HID kbd+mouse (11/11 enumerate).
- ✅ **libphoenix:** libm C99 batch (+tests); dynamic-linking Phase A (dlopen/dlsym); libdbg B1/B2/B3 (incl. kernel EL1 backtrace); many port-driven fixes (malloc(0), vasprintf, envp-3rd-arg, floorl-family, openssl 64-bit bignum).
- ✅ **Ports (official, HW):** bash, coreutils (104/104 built; only stty skipped), lua 5.4.7, sqlite3, jq, redis, curl (mbedtls), Dillo (HTTP+HTTPS), busybox. CPython 3.14 (sqlite3, zlib, _ssl/HTTPS, _decimal, ctypes, .so-dlopen).
- ✅ **Graphics/games:** V3D Mesa GL; SDL2 (all 3 Quake engines de-Quaked onto it); Quakespasm Q1 (~40fps, MP #68), Quake2 (full 3D 1080p), Quake3 (renders q3dm1 gameplay), vkQuake (Vulkan/V3DV ~30fps); X11 (Xphoenix fbdev DDX + twm/JWM/WindowMaker + xeyes/xcalc/xedit/xterm/mc/nano); GPU-in-X-window (FBO+readback).
- ✅ **ML:** MNIST CNN (trained, 95.5%, bit-exact); llama2.c CPU (bit-identical).
- ✅ **Media:** ffmpeg SW decode (mjpeg+h264 bit-exact); 720p h264 from RAM disk on HDMI.
- ✅ **Networking:** WiFi WPA2 **control-plane** (associated + 4-way keyed); Bluetooth (/dev/hci0, HCI Inquiry); NFS-as-rootfs; NFS poll-stall + genet-RX + NFSv4-expiry fixes; RAM-staging (`ram-stage-play`).
- ✅ **Infra/process:** upstream sync (16 siblings, 2026-08-12 — *verify SHAs, see H*); kernel branch main→master cleanup; SDL de-Quake/relicense; 2nd code-review pass; clean-build reproducibility + release gate; Linux-Pi4 reference box; AXI-PMU reader (mechanism); netboot EEE + bootfs-wipe (lighttpd) fixes.

---

## B. TO-BE-TESTED / validation pending (built or fixed, not yet proven)

| Item | What's needed | Gate |
|---|---|---|
| 🧪 **coreutils `make check`** (owner task 2026-08-21) | build+run coreutils' own test suite on Phoenix; assess psh limits (bash port / host runner); fix libphoenix gaps | tractable now |
| 🧪 **xorg-libs (X11 Layer 1)** | validated standalone (24 libs build); confirm **in-framework** (`ports.yaml` + `--with-ports`) + a Pi smoke (twm/xeyes) | tractable now |
| 🧪 **AXI-PMU per-master attribution** (genet/V3D) | mechanism proven; per-master isolation UNVALIDATED | tractable |
| 🧪 **~20 libc/system test binaries on card** | staged, never run | needs Pi |
| 🧪 **SD SDMA-write path** (sdcard.c:1625, gated off) | flip + HW-validate; corruption risk — do NOT default-ON unvalidated | ⛔ HW-blocked (no card in rig) |
| 🧪 **Audio audible sign-off** | driver HW-verified; needs owner + headphones | owner-attended |
| 🧪 **X11 interactive keypress** (DDX kbd/pointer → /dev/kbd0,mouse0) | needs owner physically at the board | owner-attended |
| 🧪 **/dev/gpio outputs** | read path done; outputs need attended bench rig | owner-attended |

---

## C. IN-PROGRESS (active, tractable unattended — the near-term queue)

1. 🔧 **X11 → ports migration** (owner directive; hybrid layered model). Spec: `x11-ports-migration-spec.md`.
   - Done: `xorg-libs` (Layer 1, 24 libs). **Next:** validate in-framework → `xorg-fonts` (Layer 2: freetype/fontconfig/pixman/cairo/pango/harfbuzz/fribidi/libpng/jpeg/libXft) → `xorg-server` (Layer 3: Xorg + Xphoenix DDX) → **rewire `xterm`+`windowmaker` off `/tmp`** (they still bootstrap from `/tmp/x11-phoenix`+`/tmp/wmaker-deps`) → add app ports (twm/jwm/xcalc/xclock/xeyes/xedit/xlogo/oclock/ico/xbill/startx) → runtime-asset staging.
2. 🔧 **Move remaining `tools/` ports** (owner directive — see §G for the full ledger). Biggest: `tools/ports/` bundle (glib2, dillo, fltk, mc, nano, ncurses, libffi, libiconv), `ffmpeg-port`, `python-port`, the 4 game ports; `v3d-driver-port` placement is ⏸ (see §E).
3. 🔧 **lwip TCP gateway bug** (Pi→internet fetch). Root-caused (S60, wire-level): a valid SYN-ACK for a `SYN_SENT` PCB whose peer is via the default gateway is never ACKed → handshake never completes. **Next: read lwip `tcp_in.c`/`ip4_input`, apply fix** (do NOT re-confirm the symptom). Reconcile vs E3 "Dillo browsed live internet" claim (see reconciliation notes).
4. 🔧 **Revisit ports' unfinished parts** (owner A20, ongoing) — deferred features: Redis persistence, bash `-i` interactive, SQLite WAL/multi-proc lock, CPython curses/TLS1.3, ffmpeg demux/audio/player.
5. 🔧 **Polish + perf** (A21, ongoing).

---

## D. TO-DO (not started, tractable unattended)

- 📋 **wpa_supplicant port upgrade** (A10) — port is old; upgrade + use (WiFi join currently via custom tools/wifi-probe, not wpa_supplicant).
- 📋 **zsh** (part of A18 "full bash/zsh CLI") — never attempted (bash done).
- 📋 **CNN on GPU** (A8, the owner's real ML target) — CNN is CPU; wire conv/matmul onto the working (bit-exact) V3D CSD compute path.
- 📋 **Mesa 26.2.0 rebase** (A25) — rebase our patches onto the released version (was rc/beta). *Couples with the mesa-publish decision, §E.*
- 📋 **qemu 11.1 host toolchain** (A23 / TD-07/08) — update host qemu; re-test boot under qemu+gdbstub.
- 📋 **TD-Eth-LinkIRQ** — route PHY INT_B to GIC SPI (or accept MDIO-poll as the portable answer).
- 📋 **Propose-own impressive feature** (A32) — arguably satisfied by RAM-staging + the ports ecosystem; can propose a fresh one if wanted.
- 📋 **Journey article** (`docs/AI-DRIVEN-PORT-JOURNEY.md`) — draft exists; owner review/finalize.

---

## E. WAITING FOR OWNER DECISION (blocks or shapes work)

1. ⏸ **`v3d-driver-port` placement** — it's a GPU driver/lib (ported Mesa). Move to `phoenix-rtos-ports` (like SDL2) **or** `phoenix-rtos-devices`? Determines the migration target.
2. ⏸ **Mesa publish model** — patch-series vs full fork; couples with the Mesa 26.2.0 rebase (D).
3. ⏸ **jq/libphoenix `malloc(0)`→non-NULL** contract change — glibc-compat, already shipped and relied on by ports. Accept permanently, or revert to strict-NULL + per-port shims?
4. ⏸ **XFce vs LXQt vs stay-WindowMaker** (A14) — full XFce is blocked on porting **glib's GIO** (multi-week); LXQt is Qt (different large blocker); WindowMaker/twm already work. Which target?
5. ⏸ **DRI/DRM true GPU concurrency** (A15) — V3D 4.2 is a **single-context device** → true multi-client GPU is HW-structurally-blocked; the FBO+readback path is today's ceiling. Build the GPU-arbiter daemon (M0–M5, multi-week, still can't give true concurrency) or accept the ceiling?
6. ⏸ **ffmpeg VideoCore HW h264 decode** (A16) — a multi-week VideoCore-codec driver. Attempt, or keep SW decode (works) + shelve?
7. ⏸ **WiFi data-plane / radio-as-transport** (A9/A17) — BANKED at the fw-opaque SDPCM wall ("TX reaches fw not air; don't blind-code"). Abandon as can't-complete-unattended, or invest (needs deeper fw RE / possibly JTAG)? WPA3 unreached.
8. ⏸ **Upstream-readiness bugs B1–B14** (`project_rpi4_upstream_review`) + **kernel upstream sync Batch 3** — documented, NOT applied; need attended review (B4 SMP-gate breaks other arches). Schedule an attended pass?
9. ⏸ **Publication/licensing** — fork relocation, GPLv2 choice, WiFi-subtree history scrub, first GitHub release sequencing. Owner-owned.
10. ⏸ **gcc 16.2.0 rebase** (A19) — owner labeled "future/big idea". Confirm defer.
11. ⏸ **Genuine-tool boundary** — confirm `cnn-mnist`, `llama2-port`, `sdl2-port` (demos), and the probe/bench tools STAY in `tools/` (not ports). (My classification in §G.)

---

## F. CAN'T-COMPLETE unattended / structurally blocked (owner: abandon or invest)

- ⛔ **WiFi data-plane** — fw-opaque wall (see E7).
- ⛔ **True multi-client GPU / real GLX-DRI** — V3D 4.2 single-context HW limit (see E5).
- ⛔ **SuperTuxKart** — modern needs GL3.3/GLES3 (our port is GL2.1); legacy = multi-week Irrlicht spike. Deferred.
- ⛔ **TD-10 / SError root cause** — live PCIe/VL805 external-abort behind JTAG/masked-SError wall.
- ⛔ **SD SDMA-write validation** — no card in the Pi/host rig.
- ⛔ **ffmpeg HW-decode** unless E6 says invest.

---

## G. Ports migration tracker (tools/ → phoenix-rtos-ports)

**Fully migrated + tools/ copy retired:** sqlite3, jq, redis, coreutils, lua, bash. ✅

**Genuine tools — STAY in tools/ (not ports):** axi-pmu, bt-probe, wifi-probe, dbg-probe, dlopen-poc, nfs-bench, ram-stage, stress, v3d-shader-tool, demo-apps, cnn-mnist, llama2-port, sdl2-port(demos only). *(confirm — §E11)*

**Still a port, to move:**
| Source | Target | Difficulty |
|---|---|---|
| `tools/x11-port` (fonts+server+apps) | xorg-fonts/xorg-server + app ports | 🔧 in progress (Layer 1 done) |
| `tools/ports/glib2` | phoenix-rtos-ports/glib2 | hard (blocks XFce via GIO) |
| `tools/ports/dillo` | ports/dillo | medium |
| `tools/ports/{fltk,mc,nano,ncurses,libffi,libiconv}` | ports/ each | medium (ncurses/libffi are deps of others) |
| `tools/ffmpeg-port` (libav*.a recipe) | ports/ffmpeg (players stay as demos) | medium-hard |
| `tools/python-port` (CPython 3.14) | ports/cpython (or python3) | large (bespoke build.sh) |
| `tools/v3d-driver-port` | ⏸ ports vs devices (E1) | very hard |
| `tools/{quakespasm,quake3,vkquake,yquake2}-port` | ports/ each (GPL glue) | medium; depends on v3d/SDL2 moving first + game-data staging |

**Not-yet-self-contained official ports:** `xterm`, `windowmaker` still read `/tmp/x11-phoenix`+`/tmp/wmaker-deps` — fixed only once Layers 2/3 land.

---

## H. Loose ends / hygiene (do-not-lose)

- **Un-pushed commit:** Quake3 ports fix `e498158` committed locally but **NOT pushed to org** — push (after verifying it's the QVM-exec fix that made q3dm1 render).
- **Verify upstream-sync SHAs:** the 2026-08-12 "16 siblings synced" claim supersedes an earlier "Batch 3 banked as unsafe" note — confirm against actual sibling SHAs.
- **Stale docs to update/retire:** `tracking/current-step.md` (2026-06-09), `status.md` (2026-08-12); several WiFi/SD/B2 `docs/inprogress/*` say "parked/not-validated" but memory reports resolved.
- **Tech-debt open subset:** TD-06 (DTB single-IRQ-ctrl, 1/2/8 GiB unvalidated), TD-07/08 (qemu), TD-10 (SError, HW), TD-15-remaining (**plo syspage map hardcoded — mis-maps 2/8 GiB boards**), **TD-19 (doc says `dsb;isb` but source `hal_tlbInval*` ends `dsb ish` only — pre-publish reconcile, do not edit unattended)**, TD-20 (dc zva off, perf-only), TD-Eth-LinkIRQ, TD-Git-Branches(#128).
- **Untracked `_user/ext2conc/` repro** — closed-hypothesis tool, recommend drop (owner OK).
- **NFS first-lookup-after-psh transient ENOENT (#156)** — kernel root-resolver race, parked (low impact, retry works).

---

## Reconciliation conflicts resolved (against newest evidence)

1. **Pi live-internet:** memory E3 says "Dillo browses live HTTPS via NAT"; S59–S60 have wire-level proof the gatewayed TCP handshake fails NOW. → **Authoritative: internet-via-gateway is BROKEN (lwip bug, §C3).** The E3 claim is stale/regressed or was narrower; re-verify after the fix.
2. **Quake3 QVM-exec:** port doc says "banked/blocked"; log+comparison show q3dm1 **renders full gameplay (all 3 VMs)**. → **Authoritative: Quake3 renders (DONE-HW);** the fix exists as commit `e498158` — just not pushed (§H).
3. **SD #154 / B2 backtrace / WiFi-associated:** docs say not-validated/parked; memory says HW-verified. → **memory is newer = authoritative (DONE-HW).** SDMA-*write* (distinct DMA path) stays HW-blocked.
4. **USB #121 free-list:** "do NOT mark root-caused" caveat stands, but de-facto fixed (0/15 boots, 11/11 HID). → treat as DONE-HW-mitigated, keep the caveat.

---

## Decisions I need from you (blocking §E) — quick reference
1 v3d-driver-port: ports or devices? · 2 Mesa: patch-series or fork? (+26.2.0 rebase) · 3 jq malloc(0)→non-NULL: keep? · 4 XFce(port GIO) vs LXQt vs stay-WindowMaker? · 5 DRI/DRM arbiter (won't give true concurrency) or accept FBO ceiling? · 6 ffmpeg HW-decode: attempt or shelve? · 7 WiFi data-plane: abandon or invest? · 8 upstream B1–B14 + kernel-sync attended pass: schedule? · 9 publication/licensing calls · 10 gcc 16.2.0: confirm defer · 11 confirm genuine-tool boundary (§G).
