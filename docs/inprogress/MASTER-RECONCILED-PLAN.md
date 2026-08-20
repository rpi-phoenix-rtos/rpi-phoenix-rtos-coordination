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

## 0. EXECUTION PRIORITY QUEUE (my decision — work top-down; owner may reorder)

**Rationale:** finalize what the owner's live HW test just proved un-usable FIRST
(a "done" port you can't actually run isn't done), then the standing X11 migration
directive, then other tractable finalization; decision-gated (§E) and can't-complete
(§F) items are parked pending owner input. Owner retests are BATCHED (I signal).

**TIER 1 — finalization (near-term, tractable; finish first):**
1. **P1 — quake2 + quake3 runnable** (A2.1/A2.2): DONE for quake3 (menu renders); finish quake2 render check. → owner retest.
2. **P2 — bash interactive shell** (A2.5) — **OWNER-ATTENDED** (moved from unattended): the psh-interact harness injects input line-at-a-time (psh model) and can't sustain a persistent interactive stream, so a child shell sees EOF under automation — NOT autonomously verifiable. Candidate `pty-run` helper built+deployed (`pty-run bash`); psh hands the tty to children correctly (runfile.c), so the residual is a readline/job-control/tty interaction needing a live terminal to diagnose. → owner tries plain `bash` AND `pty-run bash` at the real UART and reports.
3. **P3 — X11 ports migration** (C1) — **SIMPLIFIED by E4** (desktops paused → no glib2/pango yet): validate `xorg-libs` in-framework → `xorg-fonts` **glib-free tier** (freetype/fontconfig/expat/libpng/jpeg/libXft/cairo) → `xorg-server`/Xphoenix → rewire `xterm`/`windowmaker` off `/tmp` → app ports.
4. **P4 — coreutils `make check`** on Phoenix (owner-scheduled): prove correctness.
5. **P5 — strerror POSIX descriptions** (A2.4): analysis first, then wire the drafted fix + test.
6. **P6 — lwip TCP gateway bug** (C3): fix Pi→internet handshake (root-caused).
7. **P7 — vkQuake hang + input** (A2.3): characterize backtrace; input half owner-attended.
8. **P8 — move remaining `tools/` ports** (§G): v3d-driver-port→**devices** (E1), ffmpeg, python, games, `tools/ports/` bundle (dillo/fltk/mc/nano/ncurses/libffi/libiconv; **glib2 deferred** with E4).
9. **P9 — small to-do** (§D): **Mesa patch-series rebase onto released 26.2.0** (from rc1; keep patches in-repo, pin the release tag — E2), wpa_supplicant upgrade, qemu 11.1. (zsh DROPPED — bash is enough. CNN-on-GPU RULED OUT — see §F.)

10. **P-DOCS — sync the user-facing GitHub docs with recent developments** (§I) — README.md, docs/KNOWN-ISSUES.md, TUTORIAL.md, TUTORIAL-NETBOOT.md, docs/BUILD.md, docs/inprogress/pi4-hardware-support-matrix.md, docs/HARDWARE.md. Also a **pre-publish gate**. Best run as a pass AFTER P1–P3 settle (to avoid re-churn), + refreshed whenever a big feature lands. Known-stale inventory captured in §I.

**TIER 2 — big greenlit goals (E-decisions; multi-cycle; the major thrusts, begin interleaving as Tier-1 wins bank):**
10. **G-GPU — Linux/RPi-OS GPU parity** (E5): GL-windowed apps under X + video-in-a-window + HW-accelerated X11 on V3D 4.2 — study how RPi-OS does it (DRI/DRM/kmsro/glamor/modesetting or Wayland) and replicate the capability level.
11. **G-WIFI — WiFi data-plane** (E7): keep debugging with host WiFi tools + Linux-Pi4 netboot reference (compare SDPCM/data path); then WPA3.
12. **G-FFMPEG-HW — VideoCore h264 HW decode driver** (E6).
13. **G-GCC — gcc 16.2.0 rebase** (E10).
14. **G-UPSTREAM — attended B1–B14 pass** (E8): re-verify relevance first (SMP likely already resolved), then apply what's live.
- **Parked:** §F (structurally blocked) + §E11 (tool-boundary, owner deciding later).

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

## A2. OWNER HARDWARE-USABILITY FINDINGS (2026-08-21 — live test, UART log 20260820-212714-live-test.log)

Owner ran the ports on real HW. "Finalizing a port = being able to actually run it." Findings folded in:

1. ✅/🔧 **quake2 (yQuake2)** — LAUNCHER LANDED (`quake2` = yquake2 -datadir /usr/share/quake2): now finds baseq2/pak0.pak (1106 files), loads ref_gl1 on V3D, fully initializes (no more crash-to-shell) — the crash-to-shell the owner hit is FIXED. VISUAL RENDER of the auto-demo still black even at 95s: the log shows it doing only ~12 TFU texture copies in the window (~2.5s EACH) — so the bottleneck is the **slow TFU/texture-load path** (NFS asset reads + the per-copy L2T/TMU cache-flush epilogue in v3d_phoenix_winsys.c), NOT the TFU-vcheck diagnostic (which is gated `tfu_n<=12`, low-impact — checked). NEXT (separate GPU/perf sub-task, not a quick fix): either RAM-stage the quake2 data (ram-stage-play pattern, like the fast-load path) OR profile/speed-up the TFU-copy epilogue. quake1/vkQuake avoid this because their data is baked at /usr/share/quake and they load differently. WAS: does not run — — starts, but game DATA not found: it searches CWD (`/usr/bin/baseq2`) and dies at `GetPCXPalette: Couldn't load pics/colormap.pcx` → back to shell (no render). Data IS staged at `/usr/share/quake2/baseq2/pak0.pak`. ROOT CAUSE: basedir not set. FIX: launcher/basedir → `/usr/share/quake2` (yQuake2 `-datadir`). [in progress]
2. ✅ **quake3 (quake3e)** — LAUNCHER LANDED + HW-VERIFIED (`quake3` = quake3e +set fs_basepath /usr/share/quake3 +set fs_game demoq3): main menu RENDERS on HDMI (FS_Startup: 1390 files in 2 pk3 files; QVM runs via RWX-mmap fallback). WAS: does not run — — `pak0.pk3 is missing`; search path is CWD (`/usr/bin` or `/`). Data IS staged at `/usr/share/quake3/demoq3/{pak0,pak1}.pk3`. ROOT CAUSE: fs_basepath not set. FIX: launcher → `+set fs_basepath /usr/share/quake3 +set fs_game demoq3`. [in progress]
   - Both mirror why q1/vkQuake "just work": those bake `basedir=/usr/share/quake` (log: `vkquake: found /usr/share/quake/id1/pak0.pak`). q2/q3 need the same via `rpi4-quake2`/`rpi4-quake3` launchers (convention = `rpi4-quake`/`rpi4-vkquake`).
3. ⛔/🔧 **vkQuake hangs after the main menu** — found data + rendered the menu, then WATCHDOG tick #1 (15s) with a backtrace; NO keyboard/mouse response. Two coupled problems: (a) input not wired (SDL/console input on Phoenix — owner-attended territory), (b) a genuine post-menu hang (the known HW-marginal binner wedge did NOT self-recover, or an input-wait deadlock). HARDEST; characterize backtrace, fix later.
4. 📋 **`cat: file: ENOENT` should read `No such file or directory` — SCHEDULED (not reactive-fixed).** cat's ENOENT for non-existent files is correct behavior, BUT libphoenix `strerror()` deliberately returns errno *names* ("ENOENT") not POSIX descriptions (string/strerror.c header: "errno names") — so EVERY error message from EVERY program is unhelpful. This is a general, high-leverage libphoenix quality bug. Owner directive: do NOT blindly fix — ANALYZE first (it's a system-wide behavior change: check nothing in Phoenix/tests depends on strerror returning the NAME; confirm POSIX-text is the desired contract; consider strsignal too). A draft `sources/libphoenix/string/errno.desc` (NAME->POSIX text, 78 entries) is already written as a head-start + the generator change is scoped (Makefile errno.list join; tab.inc `read num name` already handles multi-word). Add the mandatory libphoenix test when done. See memory project_strerror_posix_descriptions.
5. 🔧/🧪 **bash self-exits immediately** — OWNER-ATTENDED. psh execs children with the tty handed over (runfile.c vfork+execv+tcsetpgrp) and psh reads its own tty fine, but a child shell EOF-exits even with no input pending. The automated harness injects input line-at-a-time (psh model), so it CANNOT sustain/verify a persistent interactive shell — needs a human at a real terminal. Candidate fix `pty-run` (tools/pty-run, /dev/ptmx bridge) built+deployed; owner to try `bash` and `pty-run bash` at the UART. Residual = readline/job-control/tty interaction, diagnosable only at a live terminal.

**Priority (my call):** q2/q3 launchers FIRST (clear, high-value, matches owner expectation) → verify cat + game render on HW → bash-tty bridge → vkQuake hang+input (hardest, owner-attended input). Owner retests when I signal.

## B. TO-BE-TESTED / validation pending (built or fixed, not yet proven)

| Item | What's needed | Gate |
|---|---|---|
| 🧪 **coreutils `make check`** (owner task 2026-08-21) | build+run coreutils' own test suite on Phoenix; assess psh limits (bash port / host runner); fix libphoenix gaps | tractable now |
| 🧪 **xorg-libs (X11 Layer 1)** | validated standalone (24 libs build); confirm **in-framework** (`ports.yaml` + `--with-ports`) + a Pi smoke (twm/xeyes) | tractable now |
| 🧪 **AXI-PMU per-master attribution** (genet/V3D) | mechanism proven; per-master isolation UNVALIDATED | tractable |
| 🧪 **~20 libc/system test binaries on card** | staged, never run | needs Pi |
| ⏳ **SD SDMA-write path** (sdcard.c:1625, gated off) | flip + HW-validate; corruption risk — do NOT default-ON unvalidated | **POSTPONED** — owner will supply a card + trigger the SD image/boot/perf pass |
| 🧪 **Audio audible sign-off** | driver HW-verified; needs owner + headphones | owner-attended |
| 🧪 **X11 interactive keypress** (DDX kbd/pointer → /dev/kbd0,mouse0) | needs owner physically at the board | owner-attended |
| 🧪 **/dev/gpio outputs** | read path done; outputs need attended bench rig | owner-attended |

---

## C. IN-PROGRESS (active, tractable unattended — the near-term queue)

1. 🔧 **X11 → ports migration** (owner directive; hybrid layered model). Spec: `x11-ports-migration-spec.md`.
   - Done: `xorg-libs` (Layer 1, 24 libs). **Next:** validate in-framework → `xorg-fonts` (Layer 2: freetype/fontconfig/pixman/cairo/pango/harfbuzz/fribidi/libpng/jpeg/libXft) → `xorg-server` (Layer 3: Xorg + Xphoenix DDX) → **rewire `xterm`+`windowmaker` off `/tmp`** (they still bootstrap from `/tmp/x11-phoenix`+`/tmp/wmaker-deps`) → add app ports (twm/jwm/xcalc/xclock/xeyes/xedit/xlogo/oclock/ico/xbill/startx) → runtime-asset staging.
2. 🔧 **Move remaining `tools/` ports** (owner directive — see §G for the full ledger). Biggest: `tools/ports/` bundle (glib2, dillo, fltk, mc, nano, ncurses, libffi, libiconv), `ffmpeg-port`, `python-port`, the 4 game ports; `v3d-driver-port` placement is ⏸ (see §E).
3. 🔧 **lwip TCP gateway bug** (Pi→internet fetch). S60 wire-level: valid SYN-ACK for a gatewayed `SYN_SENT` PCB is never ACKed. **P6 source-read (2026-08-21, host-only):** the GENERIC lwip logic is CORRECT — `tcp_in.c:798-815` handles SYN_SENT→SYN-ACK with `tcp_ack_now()`, and `ip4_input_accept` gates on DEST (the Pi's IP) not SOURCE, so lwip does NOT filter an off-subnet source (1.1.1.1). ⇒ the drop is NOT in generic lwip TCP; candidates = (a) genet RX not delivering the pkt into lwip, (b) a Phoenix lwip patch, (c) the outbound ACK's ARP/route. NEEDS on-Pi instrumentation (tcp/ip drop counters or a probe in ip4_input_accept + the SYN_SENT case) — can't localize by reading alone. ALSO re-test the E3 "Dillo browsed live internet via this NAT" claim: if it now works, S59/60 may have had a config/ARP issue, not a standing bug.
4. 🔧 **Revisit ports' unfinished parts** (owner A20, ongoing) — deferred features: Redis persistence, bash `-i` interactive, SQLite WAL/multi-proc lock, CPython curses/TLS1.3, ffmpeg demux/audio/player.
5. 🔧 **Polish + perf** (A21, ongoing).

---

## D. TO-DO (not started, tractable unattended)

- 📋 **wpa_supplicant port upgrade** (A10) — port is old; upgrade + use (WiFi join currently via custom tools/wifi-probe, not wpa_supplicant).
- ~~zsh~~ — **DROPPED** (owner: bash is enough, we don't do zsh).
- ⛔ **CNN on GPU (A8) → RULED OUT with the current approach — see §F.** (was mis-listed as a to-do; it's investigated + shelved, not fresh work.)
- 📋 **Mesa patch-series rebase onto released 26.2.0** (A25/E2) — move our local patches from 26.2.0-rc1 to the released 26.2.0 tag; keep patches in-repo; pin the single release tag/tarball as reference; NO full fork.
- 📋 **qemu 11.1 host toolchain** (A23 / TD-07/08) — update host qemu; re-test boot under qemu+gdbstub.
- 📋 **TD-Eth-LinkIRQ** — route PHY INT_B to GIC SPI (or accept MDIO-poll as the portable answer).
- 📋 **Propose-own impressive feature** (A32) — arguably satisfied by RAM-staging + the ports ecosystem; can propose a fresh one if wanted.
- 📋 **Journey article** (`docs/AI-DRIVEN-PORT-JOURNEY.md`) — draft exists; owner review/finalize.

---

## E. OWNER DECISIONS — RESOLVED (2026-08-21)

1. ✅ **v3d-driver-port placement → `phoenix-rtos-devices`** (if feasible). Migration target set (§G, P8).
2. ✅ **Mesa → USE the released 26.2.0, maintained as a LOCAL PATCH-SERIES (never a full fork).** Rebase our local patches (currently against 26.2.0-**rc1** git-671c4f08c9) onto the **released 26.2.0 tag**; keep the patches in our repo; reference the upstream release tarball / specific release tag (a single stable tag — we do NOT support multiple versions). Mesa's tree is VERY BIG, so no vendored fork. ⇒ this is an ACTIVE task (P9), NOT dropped.
3. ✅ **jq/libphoenix `malloc(0)`→non-NULL → ACCEPT permanently, don't revert.** Settled; glibc-compat stays.
4. ✅ **Desktop environments → PAUSE XFce/LXQt, stay on WindowMaker** (revisit later). ⇒ glib2/GIO + pango/harfbuzz/fribidi (GTK text stack) DEFERRED with it. **Simplifies P3:** xorg-fonts only needs the glib-free tier (freetype/fontconfig/expat/libpng/jpeg/libXft/cairo) to carry the *current* WindowMaker+Xft stack.
5. 🎯 **GPU capability → AIM FOR RPi-OS/Linux PARITY on Pi4** (NEW ACTIVE GOAL, not "accept the ceiling"). Owner: Linux on the same V3D 4.2 does GL-windowed apps under X + video-in-a-window + HW-accelerated X11/Wayland — we should reach a similar capability level. Investigate HOW RPi-OS achieves this (DRI/DRM, kmsro, X modesetting/glamor, or Wayland) on single-context V3D and replicate. (Reframes A15/F.)
6. 🎯 **ffmpeg → BUILD the VideoCore HW h264 decode driver** (greenlit, multi-week). (A16.)
7. 🎯 **WiFi data-plane → KEEP DEBUGGING** (owner confident it's solvable with the current setup). Use host-side WiFi tools + the **Linux-Pi4 netboot reference** far more extensively for comparison/analysis of the SDPCM/data path. WPA3 after. (Un-parks A9/A17.)
8. 🔶 **Upstream B1–B14 + kernel sync → schedule an ATTENDED pass, but FIRST re-verify relevance** — owner believes the SMP items (e.g. B4) were already solved before the org publish. Action: audit B1–B14 against current code; likely several are stale/done.
9. ✅ **Publication/licensing → largely DONE** (we're on the GitHub org with forks; continue that model). Only open item: **WiFi code must stay unpublished / scrubbed** (existing lwip filtered-publish discipline). No fork-relocation/release-sequencing work needed.
10. 🎯 **gcc 16.2.0 rebase → INVEST** (owner: "a big achievement to be proud of"). Greenlit as a real multi-cycle goal. (A19.)
11. ⏸ **Genuine-tool boundary → owner will answer later; follow my classification (§G) for now.**

---

## F. STRUCTURALLY BLOCKED / HW-gated (still can't-complete)

- ⛔ **ML on GPU (CNN / matmul) — INVESTIGATED, NO SPEEDUP (not a HW block, a perf reality).** The V3D CSD GPU-compute matmul was brought up and is numerically **bit-exact**, but it is **dispatch/bandwidth-bound → ~6.6× SLOWER than the A72 CPU** (measured on the llama2 arc, session ~S22). CNN's conv/matmul would inherit the same slowdown, so CNN-on-GPU shows no win. The only remaining lever (tiled-GEMM) was **ruled out by the advisor** and owner-gated ("do NOT optimization-grind"). ⇒ RULED OUT unless a fundamentally different GPU-compute formulation emerges (possibly via the E5 GPU-parity work). CNN itself runs CPU-side, HW-verified 95.5%. Memory: `project_ml_inference_llama2`, `project_cnn_mnist`.
- ⛔ **SuperTuxKart** — modern needs GL3.3/GLES3 (our port is GL2.1); legacy = multi-week Irrlicht spike. Deferred.
- ⛔ **TD-10 / SError root cause** — live PCIe/VL805 external-abort behind JTAG/masked-SError wall.
- ⏳ **SD SDMA-write validation — POSTPONED (not blocked).** Owner will provide an SD card later, then trigger: rebuild a full SD-card image → boot the system from SD → focus on SD performance (incl. flipping the SDMA-write gate at sdcard.c:1625 + HW-validating it). Owner-triggered; resume when the card is in.
- ~~WiFi data-plane~~ → **owner says INVEST (E7)** — moved to active goals.
- ~~True multi-client GPU / DRI~~ → **owner wants Linux-parity (E5)** — moved to active goals (investigate RPi-OS approach; may not need *true* concurrency to match Linux's user-visible capability).
- ~~ffmpeg HW-decode~~ → **owner says BUILD it (E6)** — moved to active goals.

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

## I. USER-FACING DOCS SYNC (P-DOCS) — keep the GitHub-browsable docs current

Owner task: the main docs a visitor sees on the coord repo's GitHub page must stay
in-line with the system's real state. **Doc set:** `README.md`, `docs/KNOWN-ISSUES.md`,
`TUTORIAL.md`, `TUTORIAL-NETBOOT.md`, `docs/BUILD.md`,
`docs/inprogress/pi4-hardware-support-matrix.md`, `docs/HARDWARE.md` (+ `LICENSING.md`,
`CONTRIBUTING.md` as needed). Run as a deliberate pass once P1–P3 settle, then refresh
per big feature; also the pre-publish gate. **Known-stale inventory (found 2026-08-21):**

- **Bluetooth mislabeled ⬜"Not started"** in README Capabilities + hardware-matrix — WRONG. BT is **functional at the driver level**: `/dev/hci0`, patchram 323/323 → real BD_ADDR → HCI Inquiry completes (`project_bluetooth_bringup`). Correct to "🟡 driver up (HCI inquiry); no host stack yet."
- **Dillo "HTTP only / no HTTPS"** in TUTORIAL §6.3 + §7 and KNOWN-ISSUES #70 — WRONG. Dillo **browses the live HTTPS internet** (CA-verified TLSv1.2, E2/E3 done). Update to HTTPS-works (via host NAT + ntpclient cert-clock).
- **Missing the whole ports/language ecosystem** in README/TUTORIAL/matrix: coreutils 9.5 (**104 tools**), CPython **3.14** (sqlite3/_ssl-HTTPS/_decimal/ctypes/.so-dlopen), **redis** 7.2, **sqlite3**, **jq**, **bash** 5.2, **Lua** 5.4.7. TUTORIAL only shows micropython/lua. Add a "CLI tools & languages" section.
- **quake2 / quake3 now launchable** via the new `quake2`/`quake3` commands (data-path launchers) — quake3 renders the menu; add to TUTORIAL + matrix (matrix has Q3 "VM-exec banked" which is superseded — it renders). quake2 render still slow (note honestly).
- **WiFi framing**: README/KNOWN-ISSUES/matrix say ⛔"blocked/not-supported"; per E7 it's "control-plane up (WPA2 associated+keyed), data-plane under active debugging" — soften from "abandoned" to "in progress," but keep "don't rely on it yet."
- **vkQuake**: matrix/README say "renders clean"; add the post-menu **hang + no-input** finding (owner HW test) as a known issue.
- **New known-issues to add**: bash EOF-exits without an interactive tty (getty→pts pending); `strerror()` prints errno NAMES not POSIX text (scheduled); quake2 slow TFU/NFS texture load.
- **Dates**: KNOWN-ISSUES (2026-08-05) + matrix (2026-08-06) headers are stale; refresh.
- **HARDWARE.md / BUILD.md**: largely current (lab-rig + build path); minor — add the ports/languages to BUILD's showcase description. TUTORIAL-NETBOOT §8 only stages quake1 data; add quake2/quake3 data staging.
- **Do NOT** over-claim: keep WiFi/vkQuake-hang/quake2-render honest; the docs' value is accuracy.

---

## H. Loose ends / hygiene (do-not-lose)

- **~~Un-pushed commit e498158~~ (CORRECTED 2026-08-21):** e498158 is an sdl2 window-events commit and is ALREADY on publish/master — agent misidentified it; no action. Real hygiene check ran: coord + all siblings are pushed EXCEPT phoenix-rtos-lwip, which carries 5 local commits (FIONBIO/#68, getnameinfo OOB, genet header, poll-readiness wakeup, comment) ahead of publish/master — this is the EXPECTED lwip state (lwip publishes via a filtered cherry-pick onto a scrubbed tip to keep the WiFi subtree out; NEVER raw-push). Not lost work.
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
