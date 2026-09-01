# Phoenix-RTOS Pi4 port — cleanup & upstreamability plan (2026-06-25)

**Goal:** bring the repo to a **clean, well-architected, upstreamable, publicly-showable** state
— a flagship "this port was built end-to-end by AI agents" artifact — with **no dirty
workarounds, no parked WIP rotting in the tree, no diagnostic cruft, and a sane architecture**
(applications in the ports project, drivers in the devices project, build-infra clearly separated).

This plan is the contract for getting there. Items are phased by value × safety. `[U]` = doable
unattended now (netboot/host-validated); `[U+T]` = needs the evening manual test; `[A]` = attended.

---

## Phase A — Tree hygiene: finalize or remove every uncommitted change

The working tree currently carries parked WIP that must each reach a decision: **finalize**
(clean commit), **remove** (delete + document why), or **document as a deliberately-parked branch**
(move off the working tree). No item may simply linger.

| Uncommitted item | Repo | Disposition | Notes |
|---|---|---|---|
| `_targets/Makefile.aarch64a72-generic` + `user.plo.yaml` (vkQuake swap) | devices/project | **TEMP — revert** | My active vkQuake HW-test swap; revert to flagship config when vkQuake work pauses (Phase C makes this a clean per-app toggle). |
| `misc/rpi4-gldraw/{Makefile,rpi4-gldraw.c}` | devices | **Decide: commit or drop** | A GL demo rung (glclear→gldraw→glcube). If kept, it belongs in the ports/demo area (Phase C), not devices/misc. If superseded by Quake, drop. |
| `storage/bcm2711-emmc/sdcard.c`, `sdstorage_dev.c` (#154 SD-write) | devices | **Finalize behind a branch/flag** | The SD-write-completion WIP (CMD13 poll). Validation is attended (SD swap). Either finish + commit (needs the evening SD test) or move to a tracked feature branch so the working tree is clean. Do NOT leave dangling. |
| `misc/rpi4-vkquake/rpi4-vkquake-stub.c` (untracked) | devices | **Commit** (Phase C: into ports) | The vkQuake bootable component. Currently untracked → a clean checkout loses it. Commit it (after Phase C, into the ports project, not devices/misc). |
| `port/wifi-fw-43455.{c,h}`, `wifi-nvram-43455.{c,h}` (untracked, #91) | lwip | **Move to a feature branch** | WiFi firmware blobs/WIP, #91 is JTAG-gated. These don't belong loose in the tree; park on a `wip/wifi-91` branch + remove from the working tree. |
| `artifacts/` (untracked) | lwip | **Remove + gitignore** | Build noise. Add to .gitignore. |
| `rootfs-overlay/` (untracked) | project | **Decide: commit or gitignore** | If it's the intended NFS/SD rootfs overlay content, commit it (it's part of the bootable system); if scratch, gitignore. |
| `tools/x11-port/src/`, launcher ELFs, build logs, `xkbcomp.warn` (coord, untracked) | coord | **gitignore** | Downloaded X sources + build outputs; never commit, add to .gitignore. |

**[U] now:** the gitignores (artifacts/, x11 build outputs, launcher ELFs), committing rpi4-vkquake-stub,
the vkQuake-swap revert. **[U+T]:** SD #154 finalize (evening SD test). **Decision items:** gldraw,
rootfs-overlay, wifi-branch — resolvable now with a documented call.

---

## Phase B — Technical-debt (TD-xx) resolution & honest documentation

`docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` tracks ~20 TD items. For an upstreamable
repo each must be: **resolved** (fix + remove marker + remove doc entry), or **reclassified** as a
documented *known limitation* with a clear rationale (not a "hack"). Categories:

- **Resolved — remove markers + entries:** TD-13 (user-mode silence, RESOLVED 2026-05-02),
  TD-16 (caches on since 2026-05-17), TD-19 (TLBI hardening validated). Audit the source for
  their `TODO(TD-xx)` markers and delete; trim the doc to a short "resolved" line.
- **Clean cleanups [U]:** TD-05 (UART debug-marker scaffolding — gate behind `RPI4_BOOT_MARKER`
  or delete; `_init.S` reindent), TD-14 residual (pl011-tty/klog two-owner — partly addressed by
  the #31 logging work; reconcile). Pure text/gating, netboot-smoke.
- **Genuine known-limitations — document, don't "fix" as a hack:** TD-01/TD-11 (SMP cpu0-only +
  the DAIF-mask spinlock — a real scope decision, document as "single-core scheduler; SMP is
  future work"), TD-09 (crossover cable — environmental, not code), TD-12 (plo 948 MiB clamp →
  ties to TD-06 DTB-driven memory). These are legitimate; phrase them as design notes, not TODOs.
- **Boot-critical hacks to actually resolve (the real "dirty" ones) [U+T]/[A]:** TD-04-hack-2/-3
  (the NC-mapping + fake `dtbEnd` syspage-corruption workaround in hal.c — the headline hack;
  resolving needs the BCM2711 plo→kernel handoff corruption root-caused, see TD-03/TD-06),
  TD-10 (SError masked early — unmask once the PCIe/USB external-abort source is fixed),
  TD-02 (pre-MMU cache inval), TD-20 (DC ZVA gate), TD-17/TD-18 (cache-hygiene/uncached-zone).
  These are the items that most make the port look "hacky"; each deserves a focused root-cause
  session. Where a true fix is HW-deep, document the limitation honestly with the evidence trail.

**This phase is the core of "no dirty workarounds."** Output: a source tree whose `TODO(TD-xx)`
markers are either gone (resolved) or point to a crisp known-limitation note — nothing that reads
as an unexplained kludge.

---

## Phase C — Architecture: applications out of the devices project (#12)

> **DONE 2026-06-26** (executed per `docs/done/2026-06-25-quake-to-ports-reorg-plan.md`,
> user-gated decisions): the apps moved to **`phoenix-rtos-project/_user/`** (NOT the literal
> `phoenix-rtos-ports` recipe repo — only `_user/` can host a bespoke libphoenix-linked binary AND
> bundle a 19 MB program into `loader.disk`; see the reorg plan §0). All six GPU bring-up
> diagnostics + the three GL demo rungs were **dropped** (hypotheses resolved; recoverable from git
> history) — NO GL demo kept. The prebuilt engine libs now live in **`tools/.gpu-libs/`** (gitignored
> `.a`s, tracked dir), replacing `/tmp`. `make -C "_user" all install` was wired into the rpi4b
> `b_build_project`. Verified `--scope core` clean, `rpi4-quake` still bundled in `loader.disk`, all
> dropped names absent. Commits: project `82f84a2`/`08e4e41`, devices `f988a76`/`a81767e`/`41d745b`,
> coord `9381401`. Items 1, 2, 4 below are realized; item 4's "`--scope core` auto-regenerates the
> libs" was deliberately **not** done (shelling the slow `build-*.py` out of the in-buildroot make
> breaks copy isolation) — instead each `_user` Makefile has a loud `$(error ...)` existence-check.

The capstone applications + GPU demos currently live in **`phoenix-rtos-devices/misc/`** — wrong:
they are not device drivers. Present in devices/misc: `rpi4-quake`, `rpi4-vkquake`, `rpi4-v3d-mesa`,
`rpi4-v3d-scout`, `rpi4-v3d-stalltest`, `rpi4-v3dv-tier0` (+ the gl* demos). Reorg:

1. **Applications → the ports project.** Quake (`rpi4-quake`) + vkQuake (`rpi4-vkquake`) are
   ports/applications; move them under `phoenix-rtos-ports` (or a dedicated apps area the project
   build supports), with their build pulling the prebuilt engine libs. This is the user's explicit
   "Quake not in rtos-devices but in ports" ask.
2. **GPU bring-up harnesses → a clearly-labeled diagnostics/demo area** (not devices/misc): the
   v3d-scout/stalltest/tier0/mesa + gl* demos are dev/bring-up tools, not drivers. Either a
   `tools/`-side test target or a ports demo group, behind a default-off switch.
3. **The real driver** that *does* belong in devices: only the GPU **kernel/power** bits (V3D
   power-on, the fb0 driver, vcmbox, thermal, hwrng, gpio, audio) stay in devices. The Mesa/V3DV
   userspace stack is a port, not a driver.
4. **Build-artifact homes.** The GPU libs (`/tmp/lib{v3d,GL,vkquake,v3dv}-phoenix.a`) are built to
   `/tmp` and get **cleared mid-session** (this caused two flagship-rebuild failures). Give them a
   stable build-output dir under the port tree + make the engine-app builds depend on them properly
   (so `--scope core` regenerates them, not a manual prebuild). This removes a whole class of
   "stale/missing lib" fragility.
5. **`tools/` consolidation.** `tools/{v3d-driver-port,vkquake-port,quakespasm-port,x11-port,
   demo-apps,v3d-shader-tool}` is the port build-infra. Decide its long-term home: keep as the
   coordination repo's build-infra (documented), or fold the per-port build scripts into the ports
   project's recipes. The patches (below) are the durable artifacts.

**Sequencing:** C is moderately invasive (build-system paths). Do it in a dedicated `--scope core`
+ netboot-validated pass, one move at a time (Quake first — it's the flagship), each verified to
still bundle + boot. **[U]** but careful; this is whole-tree-touching so it must run solo (no other
subagent editing the devices Makefile / plo / build-core concurrently).

---

## Phase D — Upstreamability

1. **Drop diagnostic patches.** `tools/x11-port/patches/libXt-1.3.0-alloc-diag.patch` is
   diagnostic-only (the malloc0 root-cause is the real fix, via the configure flag). Remove it +
   its auto-apply. Keep the genuine port patches (`libxcb-1.16-phoenix.patch`,
   `libICE-1.1.1-phoenix.patch`, the mesa/vkquake/quakespasm port patches).
2. **Vendored-clone patches → upstream-or-vendor decision.** `mesa-phoenix-port.patch`,
   `vkquake-phoenix-port.patch`, `quakespasm-phoenix-port.patch` modify gitignored upstream clones.
   For a showable repo: (a) ensure each applies cleanly onto a pinned upstream base (already the
   convention), (b) document the base SHA + apply steps, (c) flag which changes are
   Phoenix-portability (upstreamable to those projects) vs Phoenix-specific shims.
3. **Sibling-repo PR-readiness.** The kernel/devices/lwip/libphoenix/etc. changes are the actual
   Phoenix port. They're on `master` tracking `origin` (phoenix-rtos upstream), not pushed. Prepare
   them for upstream: the 2026-06-06 review (project_rpi4_upstream_review) already removed ~6.3k
   lines of diagnostics; finish that — remove remaining disproved-diagnostic code, stale comments,
   and the catalogued USB `TODO(#129)` log-spam (reviewed pass, multi-boot-validated).
4. **Coord repo.** This repo (docs/scripts/tracking) is the orchestration record — keep it clean +
   navigable; it's part of the "AI built this" story.

---

## Phase E — The "built by AI agents" showcase

A top-level narrative so the achievement is legible to outsiders: what was built (full Pi4 bring-up
— SMP-aware kernel, GENET, USB HID, SD/NFS root, HDMI, V3D GPU + GLQuake, Vulkan/V3DV + vkQuake,
X11 desktop, a userland of ports), that it was authored entirely by AI agents, and how to build +
run it. A `SHOWCASE.md` / refreshed top-level README + a short "how the agents worked" note. The
extensive `docs/` are the evidence trail.

---

## Suggested execution order (this session, unattended-first)

1. **[U] Phase A gitignores + commit rpi4-vkquake-stub + revert the vkQuake swap** when vkQuake
   pauses — cheap, immediate tree hygiene.
2. **[U] Phase D.1 drop the diagnostic libXt patch** — trivial publishability win.
3. **[U] Phase B resolved-marker cleanup** (TD-13/16/19 markers + doc) — safe, netboot-smoke.
4. **[U] Phase C.1 Quake → ports** (the flagship reorg, the user's headline ask) — careful, solo,
   netboot-validated; then vkQuake, then the demos; then C.4 build-artifact homes.
5. **[U] Phase D.3 finish the diagnostic-removal review pass** (multi-boot-validated for USB).
6. **[U+T] Phase A SD #154 + Phase B boot-critical hacks** — evening manual test where needed.
7. **[U] Phase E showcase** — once the tree is clean.

vkQuake completion (the in-flight render-resource bring-up) continues in parallel; once it renders,
its component lands in the ports project per Phase C.1.
</content>
