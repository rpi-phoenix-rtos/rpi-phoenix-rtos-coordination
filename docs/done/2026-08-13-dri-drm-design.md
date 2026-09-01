# DRI/DRM for V3D 4.2 on Phoenix/RPi4 — multi-app GPU design (owner #3)

Design only (no implementation). Source-cited from external/linux (upstream V3D DRM =
authoritative HW model) + tools/v3d-driver-port (the current Phoenix in-process port).
Advisor-gated: do NOT implement yet — this doc resolves the scoping fork + hands the
owner a clean decision. [[project_pi4_v3d_scout]] [[project_x11_gpu_windowed_feasibility]]

## DECISIVE FINDING: V3D 4.2 (Pi4) is a SINGLE-CONTEXT device — true concurrency is HW-BLOCKED
- **One core.** NCORES=1 (HUB_IDENT1=0x000e1124; test_ident_decode.c:34 + v3d_regs.h:44-45). Winsys only ever
  uses core0 (v3d_phoenix_winsys.c:211,266). No second core to give a second client.
- **One submit interface.** A job launches by writing CT0QBA/CT0QEA (bin) + CT1QBA/CT1QEA (render);
  "writing the end register starts the job" (v3d_regs.h:321-325; v3d_sched.c:266-267). Phoenix mirrors it
  (v3d_phoenix_winsys.c:978-980). The "multiple queues" (BIN/RENDER/TFU/CSD…) are FUNCTIONAL UNITS that
  pipeline ONE client's stream, not client lanes. Linux runs one job at a time per queue (credit_limit=1,
  v3d_sched.c:858) and FIFO-schedules clients (v3d_sched.c:8-16).
- **One page table, no isolation.** Single shared PT / one 4GB VA space "by design" (v3d_drv.h:141-143;
  v3d_mmu.c:12-15,84). The only isolation mechanism (GMP, 128KB masking) is **unimplemented even upstream**
  (v3d_mmu.c:16-18). A hang resets the WHOLE device for all clients (v3d_sched.c:719-744).

**⇒ "multiple apps at the same time" on this silicon = serialized time-slicing that LOOKS concurrent (several
X windows updating in turn), NOT hardware-concurrent execution.** This is exactly what Linux v3d does.
Serialization buys turn-taking, NOT memory protection (all BOs share one PT → any client can scribble any
other's BO; one hang kills all). TRUE concurrent DRI is not "harder" — it's HW-impossible here.

## Today (the problem): in-process, single-app-by-construction
drmIoctl forwards to `phoenix_v3d_ioctl` INSIDE the app (v3d_libdrm_shim.c:4-8); SUBMIT_CL is synchronous
(busy-wait FLDONE/FRDONE), fences pre-signaled, PRIME stubbed "not supported (single client)". Each GPU app
mmaps the SAME physical V3D registers + writes MMU_PT_PA_BASE with its OWN PT (v3d_phoenix_winsys.c:278,794).
**Two GPU apps at once today = fight over registers + MMU base = mutual corruption.** That's what this solves.

## RECOMMENDATION: `v3d-server` daemon, job-granularity FIFO arbitration, copy-first compositing
The Linux model in Phoenix's driver-server idiom (like rpi4-thermal/rpi4-wifi/lwip-netif). One server process
OWNS the V3D MMIO + the single PT; clients never touch registers. Verbs (msgport, mirroring the DRM ioctls the
winsys already implements): CREATE_BO/MMAP_BO/SUBMIT_CL/WAIT/GET_PARAM/CLOSE_BO. The server body = the current
winsys (v3d_phoenix_winsys.c ~2.5k lines) lifted almost verbatim (register mmap, PT ownership, va_alloc:462,
submit ioc_submit_cl:888) into a server + a dispatch loop. **Mesa gallium v3d + libGL UNTOUCHED** — only
`phoenix_v3d_ioctl` (the sole drmIoctl target, v3d_libdrm_shim.c:4) becomes a thin IPC client stub.
- **Lease granularity: per-job/frame (COARSE-job)** = interleaved multi-window. Keep **COARSE-app (whole-GPU
  exclusive lease = today's behavior, ZERO regression)** as the built-in fallback/scope-floor.
- **Compositing transport: COPY-FIRST** (glReadPixels → X protocol → DDX blit to /dev/fb0, gl_x11_window.c:266-285
  generalized to N clients). Zero new kernel facility. Shared-BO/zero-copy is a later optimization needing a
  peer-process physmem-share primitive (unverified) + /dev/fb0 live-mmap (issue #149) — DEFER.

## ★ Verification is DETERMINISTIC (this is the big unlock vs the advisor's concurrency-trap concern)
Under a serializing server there is NO concurrent submit → "no corruption under concurrent submit" DISSOLVES.
What remains is single-UART-friendly + deterministic:
- **Phase 1a (smallest step):** refactor phoenix_v3d_ioctl → IPC stub to v3d-server; run gl_det_harness
  (tools/v3d-driver-port/gl_det_harness.c) through it; **PASS = output crc32 (v3d_phoenix_winsys.c:679)
  bit-identical to the in-process reference.** De-risks IPC marshalling + cross-proc BO map, Mesa unchanged.
- **Phase 1b:** two client procs render known-CRC scenes, server serializes FIFO (TLB flush between clients,
  already per-submit v3d_phoenix_winsys.c:489); **PASS = BOTH CRCs correct** = the headline "two processes,
  one GPU, correct output", no concurrency to reproduce.
- Then wire copy-first compositing of N GL clients into Xphoenix windows.

## Effort + regression
COARSE-app ~1 week (server skeleton + IPC stub + one BO-share); +~1 week COARSE-job FIFO + per-client TLB
discipline. Regression risk LOW/bounded: an existing fullscreen demo = one client holding an exclusive lease
for its lifetime = byte-for-byte today's register/PT/submit; the hard GPU logic (cache-flush ordering
v3d_phoenix_winsys.c:807-815, VA alloc, tile-alloc) does NOT change. Risk confined to IPC marshalling + cross-
proc BO mapping — both isolated + CRC-testable (Phase 1a). Rollback via manifests.

## NEEDS THE OWNER (arch-collaborative; owner back ~1 week)
1. **Expectation reset:** multi-app = time-sliced (windows update in turn), NOT hw-concurrent (one core, one
   submit iface). Confirm that's the goal (almost certainly yes).
2. **Isolation posture:** accept NO memory protection between GPU clients (matches upstream Linux, no GMP) — or
   invest in GMP (unproven on this HW). Recommend accept-no-isolation.
3. **Transport:** copy-first (works now) vs shared-BO/DRI3 zero-copy (needs a peer physmem-share primitive +
   /dev/fb0 mmap). Recommend copy-first.
4. **Scope gate:** enforced whole-GPU lease (COARSE-app, ~1wk, safely one-app-at-a-time) vs full job-arbitrating
   server (~3wk, interleaved multi-window). Explicit owner call.

## STATUS / next (advisor re-consulted 2026-08-13 — DESIGN IS THE DELIVERABLE; do NOT implement now)
Correction to the "regression LOW/bounded" claim above: Phase 1a is NOT "today's behavior via IPC" — it adds a
process boundary + **cross-process BO mapping** (the client must CPU-fill vertex/texture/uniform BOs whose pages
the SERVER owns + inserts into the PT), which rests on an **UNVERIFIED** Phoenix kernel facility (mapping a peer
process's physmem into a client). So regression risk is **gated on that unverified question**, not settled.
Also: Phase 1a demonstrates nothing owner-visible (one app relocated = today's picture; the multi-window payoff is
1b + compositing, several more turns). And the SCOPE GATE (§4: ~1wk lease vs ~3wk server) is the OWNER'S call —
starting the build now front-runs a decision reserved for the owner (back ~1 week). **Decision: bank this design as
the #3 deliverable; the owner signs off the scope on return. Fill the remaining week with DETERMINISTIC finishable
wins — bash/zsh+coreutils (highest owner value, verifiable over psh), qemu 11.1, a CV/ML inference demo.** The
v3d-server is a fine post-return / owner-signed-off build, not a solo last-week re-arch.
