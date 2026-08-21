# Porting an RTOS to the Raspberry Pi 4, entirely by AI — a field report

*Draft. Covers ~2026-03-19 → 2026-08-05 (~4.5 months); ~1400 coordination-repo commits.
The whole Phoenix-RTOS Raspberry Pi 4 (BCM2711) bring-up was done by an AI agent driven
only through chat — no human wrote code. A human operator steered by prompt, ran the
occasional physical action (swap an SD card, hold a keyboard), and gave the occasional
decisive clarification. This is an honest account of what that looked like: what the AI
found easy, what took dozens of cycles, and why hardware is uniquely hard for a text agent.*

## The arc

Phoenix-RTOS is a small microkernel OS. The Pi 4 is a quad-core Cortex-A72 with a
famously under-documented, firmware-mediated boot and a grab-bag of Broadcom peripherals
(VideoCore GPU, GENET Ethernet, a PCIe-attached VL805 USB controller, an SDHCI eMMC).
Over ~4.5 months the port went from "does not boot" to:

- **Boot & core**: EL3→EL1 handoff, caches + MMU, GICv2, generic timer, **4-core SMP**.
- **Console/IO**: PL011 UART + an HDMI framebuffer console (driving a vendored FreeBSD
  `teken` terminal emulator), GENET Ethernet + lwIP, **an NFS root over netboot**.
- **Peripherals**: PCIe→VL805 xHCI with USB HID keyboard+mouse, SD/eMMC storage with an
  ext2 root, thermal, hardware RNG, GPIO, PWM audio.
- **Graphics**: the full Mesa **V3D** stack — **OpenGL and Vulkan** — running on the real
  GPU with no DRM kernel driver, and later **GPU-accelerated 2D X** (glamor on that GL, the
  X root rendered by the V3D and presented to the framebuffer).
- **Userspace**: an X11 desktop (kdrive) with **twm/Window Maker**, **SDL2**, and a stack of
  ported apps: **GLQuake (QuakeSpasm), vkQuake (Vulkan), Quake II**, Dillo (live HTTPS), MC.
- **Languages & toolchain**: **CPython 3.14** (HTTPS/TLS, sqlite3, C-extension `dlopen`), Lua,
  a **Redis** server (with RDB persistence), **SQLite** (incl. single-process WAL), coreutils,
  bash, busybox — and a from-source cross-toolchain rebase to **gcc 16.2.0** (full C+C++).

## What turned out *easy* for the AI

Broadly: **anything with a tight, textual feedback loop and a good reference.**

- **Reusing upstream instead of reinventing.** The GPU is the headline: rather than
  hand-encode QPU shaders, the agent *ported Mesa's existing V3D driver* and wrote only a
  thin OS-specific "winsys". Same for lwIP, teken, SDL2, the Quake engines. When a correct
  reference exists and the compiler + a test give fast feedback, the AI is very effective.
- **Mechanical breadth.** Cross-compiling 45 X11 archives, wiring up a dozen device
  drivers, chasing undefined symbols to zero in a single-ELF link — the kind of wide,
  tedious work that exhausts humans, the agent does tirelessly and consistently.
- **Systematic bisection with a fast signal.** "Which of these 140 translation units
  breaks the build" or "which config string does the map loader hang on" — when each trial
  is a quick build or a grep, the loop converges fast.
- **Applying a known pattern to a new instance.** Once the first userspace driver worked
  (mmap the MMIO uncached, poke registers), thermal / GPIO / hwrng / audio followed quickly.

## What turned out *hard* — and why

The hard cases share a root: **the failure's cause was invisible in the text the agent
could see.** Some took *dozens* of build-boot cycles.

- **The vanishing torches (~40 cycles).** In vkQuake, two wall torches simply didn't appear.
  The agent chased "the texture is dark on the GPU" for many boots — the texture was
  actually *fine*. The real cause: the no-WSI framebuffer scanout keys on the color
  buffer's **alpha** channel, and the torch's fullbright pixels had alpha≈0, so the scanout
  silently dropped them. Nothing in any log said "alpha". It took forcing constant colors +
  alpha=1 in the shader to *see* the flame reappear and finally reason backwards to the
  cause. Lesson that then paid off everywhere: **on this display path, opaque geometry must
  write alpha=1.**
- **The #67 false-metric trap.** A model-geometry glitch was declared "fixed" more than
  once — by a metric (cross-boot determinism) that didn't actually measure the bug. The
  real cause was a single-pose vertex buffer straddling a 4 KB page boundary. The lesson is
  about *AI failure modes specifically*: an agent will happily converge on a green metric
  that doesn't test the real thing. Building a *faithful* reproduction (a model gallery with
  per-model attribution) was what finally cracked it.
- **The netboot flakiness that wasn't what it looked like (this autonomous run).** Game
  binaries "failed to exec" ~50% of boots. A careful kernel investigation built a detailed,
  plausible theory (eager BSS commit stalling exec). It was *wrong about the dominant case*:
  the real failure was `psh: <bin> not found` — a boot-order race where the shell runs
  before the NFS root takeover completes. One `ls` to warm the path, then the exec, and it
  was reliable — then a two-line harness change made it permanent. A reminder that a
  well-argued root-cause can still be the wrong one; the empirical `not found` in the log
  was the tell, once actually read.
- **SMP, caches, PCIe, USB enumeration.** Each was a multi-week saga of the same shape:
  a register or DMA or cache-coherency state that is *real* but *not printed anywhere*, so
  the agent had to build instrumentation to make the invisible visible, form a hardware
  mental model, and test it. USB enumeration went from ~50% flaky to 11/11 only after the
  agent found *two* independent bugs (a controller `AddressDevice` that never completed, and
  a DMA pool smashed by stale cache lines on recycled pages) — neither visible without
  purpose-built probes and a Linux driver read as an oracle.

## Techniques that mattered most

1. **Make the invisible visible.** The single highest-leverage investment was
   *observability*: capturing HDMI frames and analyzing them with pixel math; a UDP
   "diag" responder to poke a running board; an in-process backtrace facility; QEMU's
   gdbstub for register-level state; per-stage `stderr` probes. Almost every hard bug was
   cracked the cycle *after* the agent added the right probe.
2. **Deterministic rollback.** Every validated integration state was snapshotted to a
   manifest of sibling-repo SHAs, so a regression could be undone across seven repos with
   one command. This is what makes long autonomous runs safe.
3. **Read the real oracle.** For USB, PCIe, GENET, the agent read the Linux/FreeBSD driver
   for the *authoritative* init sequence rather than guessing from datasheets.
4. **Distrust your own green metric.** The recurring AI failure mode here was declaring
   victory on a proxy. Faithful, adversarial reproduction beat convenient metrics every time.

## The human's actual impact

The operator wrote no code, but a handful of one-line clarifications each saved days:

- *"The keyboard isn't being touched — nobody is using it."* This reframed a "phantom
  keyboard input" bug from a hardware glitch into a software bug, redirecting the search.
- *"Are you sure the flame texture is dark? It looks like the whole object isn't drawn."*
  This pushed the torch investigation off the (wrong) texture-brightness track.
- Decisive scoping: *"reuse Mesa, don't rewrite"*; *"push to our org"*; *"work as long as
  needed."* Direction, not implementation.

The pattern: the human is most valuable not as a coder but as a **source of ground truth
the agent cannot observe** (what's physically happening in the room, what the goal really
is) and as a **circuit-breaker** on a search that has wandered.

## Why hardware is uniquely hard for a text agent

Ordinary software bugs leave a trail *in text* — a stack trace, a failed assertion, a diff.
Hardware bugs often don't. The state that matters (a DMA descriptor's ownership bit, whether
a cache line was evicted, what tiling format the GPU actually wrote, whether a controller's
internal command ring was cleared by a reset) is **not in any log unless you build the log**.
So the agent's real task on hardware is less "fix the code" and more "**construct a mental
model of an unobservable machine from indirect evidence**" — then design an experiment that
makes one hypothesis distinguishable from another in the few bytes of text it can see. That
is genuinely hard, it is where the multi-week sagas came from, and it is, we suspect, the
frontier for AI-driven systems work: not code generation, but *hypothesis design under
observability constraints*.

## The autonomous phase

The final stretch — the bulk of this timeline — ran unattended: the operator went on vacation and
left a task list. The agent worked in a self-scheduled loop (a cron heartbeat) against a durable
task board, committing and pushing small verified changes, retrying through intermittent network
and build failures, and rebuilding its own working memory across many context resets. Over dozens
of heartbeats it:

- **Shipped a full SDL2 port** (HW-validated: fullscreen GL on V3D, keyboard/mouse, audio) and, on
  it, brought **Quake II to a full-screen 3D render** on the GPU — the fourth engine on the port
  after GLQuake, vkQuake (re-verified), and a Quake III cross-link.
- **Root-caused and fixed the netboot reliability** that had silently taxed every test — first a
  boot-order race, then the deeper finding that the NFS root was serving a *two-week-stale
  userspace on a freshly-built kernel*, an ABI drift it closed with an automatic sync.
- **Filled library gaps centrally** — a batch of libm rounding/min-max functions, host- and
  HW-validated with a new regression test — rather than stubbing them per-port; and **generalized
  its own crash/hang backtrace facility into a reusable `libdbg` corelib**.
- **Made Dillo HTTPS-capable** (wiring mbedTLS, GPLv3-clean), cleaned hundreds of lines of dead
  diagnostics for publication, and kept the documentation and a per-issue registry current.
- Pushed the **Quake III engine all the way to a live V3D render + a running QVM interpreter**,
  then **banked it** at a precisely-diagnosed VM-execution bug rather than chase it indefinitely.
- **Ported an ffmpeg decode core and played video on the screen.** From a feasibility scan, through
  filling the last four libc gaps it hit (`erf`/`exp2`/`log2f`/`scalbn`), a reproducible LGPL-clean
  build scaffold, and finally decoding **MJPEG then H.264 *bit-exactly*** on the hardware (the decoded
  luma matched the host decoder to the integer) and looping a clip onto the `/dev/fb0` framebuffer as
  **moving video on the HDMI output** — a from-scratch RTOS playing H.264. Every step was a bounded,
  HW-verified increment: a runtime fault was root-caused (not guessed) to a *stack overflow* from
  H.264's deep decoder and fixed with a large-stack thread; and an early "the on-device demo is
  infra-gated" bank was correctly *revisited* once the agent noticed the gating was about multi-MB
  video, not a 1 KB test frame.

A second wave began when the operator, mid-vacation, escalated the brief — *take risks, kernel changes
are fine, always compare against a Linux-on-Pi4 reference, and don't treat the backlog as drained.* The agent:

- **Gave the board the internet, then a browser on it.** Host NAT + a Phoenix default route + an NTP
  clock-sync (the Pi has no RTC, so its 1970 clock had been failing every TLS certificate) brought the
  board online, and **Dillo rendered a live, CA-verified HTTPS page on the HDMI screen** — a from-scratch
  RTOS browsing the real web. Two enabling kernel fixes rode along: a `poll()`-readiness path for socket
  fds, and demand-zeroing the ELF `.bss` at load (like Linux) instead of eagerly committing it, which
  unblocked exec of the multi-megabyte game/app binaries over NFS.
- **Put the GPU in a window.** A feasibility pass found the textbook accelerated-X routes (GLX/DRI/Glamor)
  *structurally* blocked on this port — no DRM device, no inter-process buffer sharing, no dynamic loader —
  and then sidestepped all three: render with the V3D to an offscreen buffer, read it back, and present it
  into an ordinary X window. From there, incrementally: a window-manager-decorated GPU window, a multi-app
  desktop, a **concurrent GPU-app-and-decoded-video "media desktop,"** and finally **Window Maker as a real
  desktop environment** (dock, clip, populated app menu) — the tractable answer to an "XFce" goal that a
  scan showed was impractical (its GTK/D-Bus stack is entirely unported, and this port has no dynamic loader).
- **Extended the video player into that window**, and **measured rather than guessed the NFS bottleneck** —
  building a throughput probe, comparing against the Linux-Pi4 reference, and concluding the game-load
  slowness was the 100 Mbps *physical* link plus RPC pipelining, *not* a single fixable kernel bug (the
  read-ahead-clustering and lazy-open paths were already good). It then **precisely localized — and
  banked — a Quake-1 multiplayer connect failure** (the engine's `connect` never reaches the socket, via
  either launch path), and **fixed a flake in its own test harness** (a submitting-newline dropped ~half the
  time over the netboot UART, which had silently cost it retry after retry).

Much of this wave was *composition*: the new capabilities were thin glue joining primitives the port had
already proven — the GPU winsys, the X client stack, the ffmpeg decoder — which is why they landed as single
verified increments rather than new engines. And the agent gravitated toward what it could *see*: with only
an HDMI capture and a serial console for autonomous ground truth, visually-verifiable work (a window, a page,
a desktop, a playing video) was what it could confirm alone — and that quietly shaped which of the open
tasks it chose to advance.

A third wave took on the port's oldest open wound: **Wi-Fi**. The on-board Broadcom BCM43455 had been stuck
for months at a single stubborn symptom — the firmware downloaded into the chip byte-perfectly, its reset
vector was resident, the ARM core was released from halt, and yet *the firmware never ran*. Round after round
of notes had blamed the reset vector, the core-release sequence, the wrong ARM core. The agent threw all of
that out and ran the one experiment nobody had: it **replaced the 643 KB firmware with a twenty-byte program
of its own** — a hand-assembled counter loop reusing the real firmware's exact reset vector — released the
core, and read the counter back. It was climbing at ~9 million increments a second. *The release path had
been correct all along;* the chip had been ready to run released code the whole time. That single bisection
flipped the entire problem: the bug was not the release, it was a **firmware precondition**. From there the
agent ported the chip's enumeration ROM walk to discover the real on-die core addresses (finding, along the
way, that a hard-coded mailbox address had been off by 0x1000 — a status read at the wrong place, masquerading
as "no signal"), read the true RAM size from the core's bank registers, and found the actual fault: the NVRAM
image the firmware needs at boot had been placed **160 KB below the true top of RAM**, so the bootloader's
search for it came up empty. One address fix later, **the BCM43455 firmware booted** — clock up, mailbox
signalling `FWREADY`, data path enabled — and the agent, having just ported the driver's shared-memory
structure, **read the firmware's own console log back over the SDIO bus**: `Broadcom BCM4345 802.11 Wireless
Controller 7.45.234 … sdpcmd_dpc: Enable`. A from-scratch RTOS had brought a Wi-Fi chip's firmware to life,
and could now *watch it think*. Every step here was a small, decisive experiment chosen to bisect a large
unknown — and, notably, an early over-confident guess (that the RAM top was "probably fine") was overturned
by reading the hardware's own answer rather than trusting the inference.

From a live, observable firmware, the agent then built an actual driver on top of it — entirely in raw SDIO
transactions, with no operating-system Wi-Fi stack underneath. It brought up the SDPCM/BCDC control protocol over
the chip's data function (fixing a data-line wedge with a targeted controller reset, and a subtle receive-queue
bug where an asynchronous event sat ahead of the reply), and got the firmware to answer its first control command.
When the scan request was rejected — *"can not scan while driver is down"* — the readable firmware console again
turned a dead end into a precise next step: the chip's regulatory/channel table hadn't been uploaded, so the radio
had no channels to come up on. The agent added the channel-blob download, and **the Wi-Fi radio scanned the air and
returned sixteen real access points** — neighbouring home networks and an HP printer, each with its MAC address,
signal strength, and channel — a from-scratch RTOS seeing the wireless world around it for the first time. Every
layer of that stack (firmware boot, SDIO transport, the control-message protocol, the event channel, the
regulatory upload, the scan) was implemented from datasheets and the Linux driver's source as the reference, and
verified on the hardware one decisive experiment at a time.

Then it woke the chip's *other* radio. The BCM43455 is a Wi-Fi + Bluetooth combo part, and the agent turned to
Bluetooth next. A read-only dump of the pin-muxing revealed the firmware had wired no UART to the Bluetooth
controller at all — the debug console occupied the stable UART, and the alternate (mini) UART sat disabled — so the
agent routed that mini-UART to the Bluetooth pins itself, derived its baud rate from the live core clock, and
powered the radio on through the firmware mailbox. The first HCI command came back silent; a stronger reviewer's
one-line insight — the flow-control line was deasserted, so the controller had processed the command but was
politely holding its reply until told the host was ready — turned out to be exactly right, and once the agent
asserted it, **the Bluetooth controller answered**: an HCI reset acknowledged clean, and a version query identifying
a Broadcom Bluetooth 4.1 core. From there the agent uploaded the controller's ~64 KB patch-RAM firmware — all 323
records acknowledged over that hand-brought-up serial link — after which the radio reported a real Bluetooth MAC
address (adjacent to the Wi-Fi one, the same silicon) and ran a clean device-discovery inquiry. Both radios of the
combo chip were now fully up from a from-scratch RTOS that, weeks earlier, could not get the chip's firmware to
execute a single instruction.

A later wave turned inward — from adding capabilities to *consolidating* what the port had accumulated, and to
the kind of deep single-bug diagnosis that yields a precise answer rather than a demo:

- **Migrated the ad-hoc port scripts into first-class framework recipes.** Much of the ecosystem — the PNG/JPEG
  codecs, the FLTK toolkit, iconv/ffi, GLib, the Dillo browser, the X11 stack — had been built by hand-rolled
  shell scripts staging into `/tmp`: fine for bring-up, wrong for a project meant to be published and upstreamed.
  The agent moved each into a proper port-manager recipe so they resolve one another by declared dependency and
  build reproducibly — `libpng`, `libjpeg` (as **libjpeg-turbo**, since the build system's version parser rejected
  the legacy IJG version string), `fltk`, **`libiconv` as the *real* GNU libiconv 1.18, retiring a documented
  ASCII-only stub** (the modern gnulib that a two-years-older release had "refused to cross-compile" simply worked),
  `libffi`, `glib2`, and `dillo` — each build-verified through the real port manager, the whole
  `libpng→libjpeg→fltk→dillo` dependency chain composing end-to-end.
- Then — on a stronger reviewer's pointed reminder that **"build-verified" is not "works"** — it *booted* them.
  Seven freshly-migrated ports had never actually run, and the framework Dillo now linked *different* libraries
  (libjpeg-turbo not IJG, real libiconv not the stub) than the binary proven months earlier. Staged onto the netboot
  root and launched, the migrated **X server and Dillo rendered a full browser window on HDMI, then a live HTTPS page
  with a JPEG decoded by the new libjpeg-turbo** — proving the recipes produced working artifacts, and catching two
  gaps (an uninstalled plugin daemon; a `/usr/bin`-vs-`/bin` path) that seven green builds had hidden.
- It reopened the port's most-wanted graphics bug — a lightmap that renders black on large maps, plus the same class
  of read-side corruption in two other engines — and, instead of re-reading source, built a **store-versus-sample
  bisection**: upload a known pixel pattern to the exact texture layout the game uses, then read it back *two ways* —
  through the CPU transfer path and through the GPU's texture unit — and diff. The CPU path was byte-clean; the GPU
  sample was not. That split, plus a host-side audit proving the ported GPU tiling/descriptor code *identical* to
  upstream (which renders the same textures correctly on Linux) and the tiling math independent of the one hardware
  register the port hard-codes, **ruled out every layer months of notes had suspected** and localized the fault to a
  Phoenix-specific read-side interaction in the GPU winsys — a precise negative result, banked with a reproducer, for
  an attended dive rather than an unbounded unattended one.
- And it chased **why interactive `bash` exits at its first prompt.** A small tty probe on the hardware proved the
  terminal read path *correct* (line discipline, `VMIN` blocking, no ABI mismatch) but caught a real gap on the way —
  the `FIONREAD` ioctl that readline queries for pending input returned an error instead of a byte count — which the
  agent fixed in the shared tty library and HW-verified. `bash` still exits interactively, so that fix, though a
  genuine correctness bug, was *not the cause*; but a `bash` script now runs correctly and the whole terminal layer is
  *proven* not at fault, leaving the residual as a readline-internal question for a live terminal.

- It then reached the port's headline graphics goal — **GPU-accelerated 2D X**. The textbook path
  (GLX/DRI) had been ruled structurally impossible early on; the agent instead studied how the modern
  X server accelerates 2D — **glamor**, which needs only OpenGL 2.1, which the port already had — and
  found by reading the source that glamor's *core* is decoupled from the EGL/GBM/DRM plumbing everyone
  had assumed was the blocker. It wrote a small shim standing in for the absent `libepoxy` (mapping GL
  dispatch straight to the statically-linked Mesa driver) and a ~dozen-line context provider handing
  glamor the in-process V3D GL context, then re-backed the X root pixmap with a GL texture and presented
  it to `/dev/fb0`. On hardware: **xeyes, xcalc, and a twm desktop rendered by the V3D GPU** — the
  accelerated-X capability the "sidestep" takeaway below had called blocked, now actually achieved, with
  the single-GPU-process limit (the V3D is single-context) as the one real remaining constraint.
- It rebased the cross-**toolchain to gcc 16.2.0** — from source, to a *separate* prefix so the working
  gcc-14 was never at risk. The Phoenix target patches ported almost cleanly; the two real breaks were
  both instructive. gcc-16's aarch64 unwind refactor now references a `frame_state_reg_info` member the
  port's libgcc config never opted into (a one-line `md_unwind_def_header` fix). And gcc-16's libstdc++
  refused to compile because the port modelled the C11 `_Atomic int` inside `pthread_mutex_t` as C++
  `std::atomic<int>` — whose *deleted copy constructor* broke libstdc++'s mutex initialisation; making
  that a plain, layout-compatible `int` in the C++ view (the atomic accesses live only in the C library)
  yielded a full C **and** C++ gcc-16.2.0 toolchain that links runnable AArch64 binaries. The compile
  half of the rebase is done; the system-wide swap is left for an attended pass.
- With the headline goals banked, it dropped to a **test-and-close** cadence. Running two dozen
  never-executed libc/kernel test suites on the hardware surfaced — and it fixed — two real bugs of its
  own stack: a `pthread_detach` use-after-free on a freed handle, and `tmpfile()` failing on netboot
  because its backing directory was created on a root that the NFS takeover then replaced. And it closed
  deferred port features with tight one-cycle proofs: **Python HTTPS** over a real TLS 1.2 handshake,
  **Redis RDB persistence** across a full server restart, and **SQLite WAL** — working single-process,
  and precisely bounding *why* multi-process WAL cannot (the VFS has no shared-memory index). Small,
  sure, verifiable — the right gear once the big wins are in.

The limits it hit were *physical or judgment* boundaries, not cognitive ones: a 100 Mbps link it
couldn't rewire, an SD card it couldn't insert, and a host network it judged too risky to
reconfigure unattended (reconfiguring the netboot infrastructure everything depended on was not
worth a silent regression while no human could recover it).

## Takeaways

- For AI-driven low-level work, **invest in observability first** — the agent is only as
  good as the feedback loop you (or it) can build.
- **Reuse beats generation.** The best code the agent wrote was the glue around code it
  didn't write.
- The hard part isn't writing the fix; it's **seeing the bug**. On hardware, that means the
  agent must become an experimentalist, not just a programmer.
- A long-horizon agent with durable memory, rollback discipline, and a self-verifying loop
  can sustain real engineering progress for days without supervision — bounded mostly by
  what it physically cannot touch.
- **Distrust your own confident diagnosis.** The unattended run repeatedly mislabeled things —
  a missing game asset as "NFS flakiness," a black 3D view as an "alpha bug," an I-cache theory
  for a JIT crash — and had to correct itself against fresh evidence. A cheap experiment that
  *distinguishes* two hypotheses beats a well-argued but unverified one; a green proxy metric that
  a broken build also passes is worse than no metric.
- **Know when to bank a saga.** The hardest problems (the Quake III VM interpreter and JIT) were
  driven to a precise root cause and then deliberately shelved, with that characterization
  recorded, once the graphics port itself was proven. Banking hard-won understanding and moving on
  beats an open-ended rabbit hole — the more so unattended, where there is no one to call it.
- **The agent's own tests caught the agent's own bug.** Once the high-value backlog was drained, the
  loop shifted to a lighter cadence — hardening what was shipped rather than manufacturing new
  features. Writing regression tests for the libm functions it had *just shipped* immediately
  surfaced a real overflow in its own `scalbln` (a huge exponent clamped to `INT_MAX` wrapped to ~0
  instead of ∞). "I already verified this" is not the same as a test; on a long unsupervised run the
  cheapest guard against your own confident mistakes is to make the check executable. Knowing when
  to *drop to that gear* — small, sure, verifying turns instead of forcing another headline — is
  itself part of the judgment.
- **Compose what's already proven, and let validatability steer.** The richest late-stage results — a
  browser on the live internet, an accelerated-GPU app in a window, a Window Maker desktop, a video player —
  were almost all *recombinations* of primitives the port had already verified (the V3D winsys, the X client
  stack, the ffmpeg decoder), joined by thin glue; that is why they landed in single increments instead of
  multi-week ports. And when the textbook path was structurally blocked (accelerated X via GLX/DRI), the win
  came from *sidestepping* it (offscreen render + present-by-copy), not forcing it. Relatedly: on a run whose
  only autonomous ground truth is an HDMI frame and a serial log, the agent rationally favored work it could
  *see itself finish* — a sensible bias, but one worth naming, since it also means audio, interactivity, and
  anything needing a human's eyes or ears quietly slid down the queue regardless of stated priority.
- **A precise "not here" is a deliverable.** The hardest late items didn't end in a fix — they ended in a
  *localization*: the GPU corruption is not in the store path, not in the ported Mesa, not in a hard-coded
  register; the `bash` EOF is not in the tty read layer. Each came with a reproducer and a hand-off. Exhaustively
  ruling out the tractable layers is real engineering — it converts an open-ended "GPU tiling bug" into a named,
  bounded question an attended session can finish — and a genuine gap you fix on the way (the `FIONREAD` ioctl) is
  worth landing even when it turns out not to be the culprit. Late in a long run, when the shippable backlog is
  drained to owner-gated decisions, the honest move is a clean, evidence-backed hand-off, not a manufactured feature.
