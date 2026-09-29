# W38, night of 18→19 Sep — barriers measured, allocator residue, TD-22

Archived from the weekly log 2026-09-19 to keep it readable. Results live in
[`docs/inprogress/WEEK-2026-W38.md`](../inprogress/WEEK-2026-W38.md) §3; the analyses are in
`docs/misc/2026-09-18-dma-barrier-audit.md`, `…-audio-dma-stall-captures.md` and
`…-allocator-guard-residue.md`.

**★★ THE AUDIO STALL: contained, shape known** (2026-09-18). Full record — every register, null,
citation and correction: [`2026-09-18-audio-dma-stall-captures.md`](../misc/2026-09-18-audio-dma-stall-captures.md).
✅ **CONTAINED — the one thing a live demo would notice is gone.** The driver called a **parked**
channel "started"; it now grades by **progress**, re-arms up to 3×, else degrades `/dev/audio0` to a
**paced null sink**, so the app runs silent instead of blocking. Gated 6/6 twice + a 10/10 negative
control; README's on-stage advice says this per build vintage.
✅ **Five real defects fixed on the way:** a bus error latched on 100 % of boots · a clock start that
moved the source mux and the enable in one bus cycle · the firmware's MASH divider inherited over an
exact integer divide · a re-arm check that could not fail · and **no memory barrier at all** between
the DMA control block and the MMIO kick. None is claimed as the cure.
★★ **The shape changed: per-BOOT, not per-arm** — ~14 000 in-process trials, 0 failures, including
4 000 arms of the real PWM1/DREQ 1/ch 5 path through a new diagnostic ioctl.

★★ **That audit found FOUR more, one live on the demo path**
([`2026-09-18-dma-barrier-audit.md`](../misc/2026-09-18-dma-barrier-audit.md)): the **V3D TFU submit**
kicked the engine with nothing ordering its source texels *or* the fresh PTEs — that is every GL
mipmap/blit and every Vulkan image copy — and the CL path's own comment made it *look* covered by
pointing at a barrier inside an `#ifdef` that ships off. Also **xHCI's event ring** read TRB fields
without ordering them against the cycle bit that publishes them (Linux has that barrier, with a
comment), **SDHCI reads** had none between "complete" and reading the payload, and the **V3D
direct-mailbox fallback** had neither direction. All four fixed. Checked and already correct: V3D
CL/CSD, genet TX+RX, xHCI doorbells, rpivid, rpi4-vcmbox. ⚠ Reasoning, not measurement — no observed
defect is attributed to any of them.
✅ **The seven pending commits were pre-flighted before the one build that gates them all**
(a compile error would have cost a 40-minute cycle): **build-safe** — no compile error under the
port's `-Werror`, no arch break (every touched file builds for `aarch64a72-generic` *only*, and
`xhci.c` already carried four unguarded `dsb sy`), every barrier on the correct side, and the new
32-byte `_IOWR` fits the inline `raw` path on both sides of the message ABI. Three "surely this is
already covered" traps were checked and cleared rather than assumed. **Nine reviewer findings, four
fixed now** (devices `HEAD~1`, `HEAD`): the ARMTRIALS printf re-read the registers it had just
latched, so the UART line and the ioctl result could disagree about the very cycle being captured ·
`trials` never reported being clipped · the server and winsys wedge verdicts were **not** identical —
the server called a BO with no CPU view `foreign`, which is the exact branch the observation keys on ·
and the `audio_write()` barrier's comment claimed a guarantee a free-running engine cannot give.
Banked, not fixed: the two CL dumps share a log prefix (grep noted in the code), ~100 lines are
duplicated between the server and the winsys copy, and `RPI4AUDIO_ARMTRIALS` is a permanent
diagnostic in a published header that can block the driver's only thread for ~100 s.

★★ **The allocator's silent guard firings are NOT a smashed header — the application is cleared**
([`2026-09-18-allocator-guard-residue.md`](../misc/2026-09-18-allocator-guard-residue.md)). The 99
residue events form an **intact chunk grid** per heap — non-overlapping, 73 of 95 consecutive pairs
exactly adjacent — so these are blocks the allocator really carved; the heap's recorded **extent**
stopped covering blocks still live in it (code 6/8). ⚠ The `why=` guard **has never fired in the
field**: it postdates every archived event. ⛔ "Name the `operator delete` caller" is retired — it
targets the half the grid evidence cleared, and `-fomit-frame-pointer` makes it impossible anyway.
★ **An unchecked store would produce exactly this:** `_malloc_heapAlloc()` validated the address
`mmap` returned against *released* heaps only, then wrote the new heap's size at that base — so a
region handed back over a **live** heap shrinks its extent in one store. Now checked and reported.
ⓘ **The kernel refuted that route:** `_map_find` returns only exact leaf gaps and every way its
bookkeeping goes stale makes gaps look *too small*; fresh anon pages are eagerly zeroed. ★ What
survives needs no kernel bug — heaps pack **back-to-back**, so a chunk whose `->heap` names the
*preceding* heap reads identically, and `hend?=1` is exactly that adjacency.
🔧 Two discarded return values fixed: the kernel dropped `_map_add`'s `-EEXIST` (*the* overlap
signal), the allocator dropped `munmap`'s. ⚠ The new overlap check itself had the **"grader that
cannot fail"** shape — making it testable found it **skipped the total overlap**, and that a planted
test heap is discarded by `malloc_heapSizeValid`, so three positive cases passed against a check that
never ran. Four harness cases now prove it reports. ✅ **TD-22 fixed** (kernel, **unbuilt**): `_map_find`'s
right-hand leaf return could hand back a non-`MAP_FIXED` hint sitting nearer a gap's end than `size`,
overlapping the next entry. The bound has been in the source all along but **commented out** — and it
could not simply be restored, because on that `if` `rmaxgap` is a *subtree* maximum, so gating the
descent on it would refuse subtrees that do have room. The check went at the **leaf**, where the value
is that node's exact gap; a refusal falls through to the parent walk. ✅ **GATED 6/6** (`td22gate`, 06:14–06:50): 0
faults, torches present, real frames on all five games, **0 `MAP OVERLAP` reports**; manifest
`2026-09-19-w38-td22-gated.md`. It got its own gate because every process start maps through this
path — a red gate would then have had exactly one suspect. **Nothing is unbuilt now.**
✅ **10-run STK soak on that final tree: 10/10 clean** (`finaltree`, 06:54–07:48) — 0 faults, 0
guards, audio healthy on the first arm in all ten, and **flipstat 47 in every single run**. So the
shipping tree now has breadth (6/6 gate), repetition (10/10 + the earlier 12/12) and duration (26 min
X endurance). ⓘ The heap guard still has not fired: ~31 STK runs since it was armed.
🧠 Durable findings written to memory so they survive this session: the **measured** CB-fetch race
(with its "2.9 % does not transfer" caveat and the three probe bugs that all traced to `SOURCE_AD`
surviving RESET), the allocator residue clearing the application, and `syntax-check.sh`.
ⓘ One devices commit is unbuilt — `rpi4-audio.h` **comment only** (corrects a stall-rate figure that
was wrong in a published header, and records TD-23). No behaviour, no ABI, so it needs no gate; the
next build picks it up. ✅ **Cross-compiled on its own while the Pi was busy**,
and it **caught a break**: `*prev/*next` in a comment forms `/*`, which `-Werror=comment` rejects —
that would have failed the gate build hours later. Now clean.
🔧 **That is now a script:** `./scripts/syntax-check.sh <repo> <file.c>` compiles ONE file under the
build's real flags (`-Werror` included) and **writes nothing**, so it works while a bench holds the
Pi — which a `--scope core` build cannot, because it ends in the image stage and overwrites the TFTP
`loader.disk`. It recovers the command with `make -n` rather than hardcoding flags; verified on the
kernel *and* devices, and against a planted undefined symbol so it is not another check that cannot
fail.
✅ **The 25-boot rate run: 25/25 healthy on the FIRST arm**, 1784–1880 ring words, **0 stalls, 0
re-arms, 0 degrades, 0 faults** on the frozen build. ⚠ **Underpowered — a weak bound, not a result:**
against the published rates (~1 boot in 41–70) a clean 25 has probability **54–70 %** even if nothing
had changed; ruling the stall out at 95 % needs ~121 boots (≈ 12 h). What pinned the defect was the
in-process work, not this bench. 💾 The frozen loader is kept as
`artifacts/rpi4b/loader-frozen-stallrate2.disk`; copy it back over the bootfs `loader.disk` (that
directory *is* the TFTP root) to measure that exact build again.
✅ **Built + gated 21:09–21:51** (`--scope core --with-tests --with-ports --with-showcase`): the
thirteen commits above — five barrier fixes, the wedge dump, the ARMTRIALS degrade, the allocator's
extent/overlap/munmap checks, the kernel's overlap report. **Six-app gate 6/6**, 0 faults, torches
present, real flipstat frames on all five games; manifest `2026-09-18-w38-barriers-gated.md`.
✅ **Verified on the artifact, not the stage list:** sysroot `libphoenix.a` and the staged *and*
live-export `supertuxkart` all carry `OVERLAPPING a live heap`, where every one was **0** before the
build — the games are framework ports whose final link is unconditional, so `--scope core
--with-ports` does relink them. ⓘ STK's CMake link error in the build log is the **by-design** stage-2
failure; `M3 complete` is the line that says the real link happened.
ⓘ **No allocator or kernel guard fired anywhere in the gate** — armed and present in the binary, but
not exercised: the residue shows up in a minority of STK runs and this was one run.
✅ **12-run STK soak on the new build: 12/12 clean** (`stkguard`, 22:14–23:18, the gate's 2-lap
hacienda profile) — **0 faults, 0 allocator guards, 0 kernel guards**, audio healthy on the **first**
arm in all twelve, and **flipstat 47 in every single run**. That is the stability evidence the
thirteen commits needed beyond one gate: they touch every `mmap` and four DMA hot paths.
ⓘ **The heap residue did not reproduce**, so `hend?` is still unread — ~21 STK runs since the guard
was armed, 0 fires.
❌ **Hunted host-side too, and it did not reproduce:** **60 M operations** (150 seeds × 400 k, 1.8 M
mmap/munmap cycles, 9.4 bn chunk-walk steps) against the real `malloc_dl.c` — **150/150 OK, 0
spontaneous violations**. The only fires are the harness's deliberate injections, which is a useful
positive control: the corrupt-header path including `why=` works end to end in this build.
⚠ **As said before the run, a host null clears nothing** — the harness uses the **host's** `mmap`,
and every surviving hypothesis is about how heaps are *placed*. It extends the "not the allocator's
own logic" bound from ~8 M operations to ~68 M, and no further.
✅ **X desktop endurance on the new build: ~26 min, 0 faults** (`xendure`, 23:34–00:01, 102 HDMI
frames) — **0 allocator/kernel guards, 0 render timeouts, 0 wedges**. The V3D **TFU** barrier is the
one fix that is live on the demo path (glamor blits continuously), and duration is what a single gate
cycle cannot cover. ★ **Graded by CONTENT, not by a clean log:** the 00:01 frame shows the V3D GL
window still spinning its 3D scene, `python3` Game of Life at **gen 26762 / 17.5 gen/s**, `top` with
30 live tasks (v3d, glamor daemon, wmaker, xterm, xclock, xbill, nfs, lwip) and `xclock` reading the
correct wall time. 📸 `artifacts/hdmi/2026-09/20260919-000105-xendure-tick.png` is the best single "whole
system working" still produced so far — worth considering for the reel.
🔧 ⚠ **Two traps caught in the tooling, both mine.** (1) `stkguard` collided with a 2026-09-10 bench,
so the first grep over `*stkguard-T*` returned **seven** logs for a four-trial-old run — two builds
reading as one clean series; only the duplicate `T1/T2/T3` gave it away, and the bench now warns on a
reused label. (2) I edited `test-cycle-bench.sh` **while it was running**: bash reads scripts by byte
offset, so the running copy resumed mid-token, printed a line from an unrelated branch (`lane: SD
CARD`) and died with a syntax error `bash -n` cannot reproduce. Only the bench's own summary was
lost — all 12 logs are intact and the lane is still `nfsroot`.
★★★ **The barrier race is MEASURED, not argued** — `pwmdma --cb-race`, 5000 trials per arm:
**with `dsb sy` 5000/5000 correct, 0 stale; without it 146 stale** (2.9 %, first on trial 0). A
"stale" fetch is the DMA engine reading a control block's *previous* contents — bytes the CPU had
already overwritten. That is exactly the Normal-NC → Device hazard the five barrier fixes were landed
against, so the audit stops being reasoning and becomes a result. ⚠ The control arm's null **bounds**
the rate rather than proving the ordering is architecturally guaranteed, and neither arm covers a
first-ever fetch of freshly `mmap`'d memory (the probe reuses one CB).
✅ **100-boot rate bench on the barriered build: 0 stalls** (`barrierrate`, 01:15–05:50).
**100/100 trials graded** — every one carries the audio verdict, so none is silently uncountable —
**0 stalls, 0 re-arms, 0 degrades, 0 faults, 0 guards**; advance 1784–1960 words.
⚠ **Suggestive, not proof — and the weaker of the two possible outcomes.** Against the published
~1-in-41-to-70 rate, P(0 in 100 | nothing changed) is **8–24 %**; 95 % confidence needs **121–208**
boots. The pre-barrier 25-boot run was *also* clean, so the two cannot be compared. A **stall** would
have been the informative result, because the abort print now carries the `CONBLK`/`SRC`/`DEST`/`LEN`
fields the archive never had. **The defect has not recurred; the instrument is in place for when it
does.** 💾 Build under test preserved as `artifacts/rpi4b/loader-barriers-21_09.disk` (`58fd8641…`).
🔎 **Does that race explain the audio stall? Plausible, untested — and I checked rather than
assumed.** The arm wrote a control block and kicked with **no barrier** until tonight, happens **once
per boot**, and `MAP_CONTIGUOUS` is not zeroed — a first fetch that missed our stores hands the
channel a garbage `SRC`/`PERMAP` and parks it exactly as captured. ⛔ But the archive **cannot**
settle it: all three captures predate the `CONBLK`/`SRC`/`DEST`/`LEN` fields, and what they do show
(`PWM_DMAC=0x80000804`, `PWM_CTL=0xa1a1`) are CPU-written MMIO registers, not CB-loaded ones.
⚠ **And the 14 000-arm null does not argue against it:** `audio_dmaArm()` rewrites **nothing** in the
control block, so every re-arm fetches identical bytes — a stale fetch returns the right values and
streams fine. **ARMTRIALS is structurally blind to this**; only the first arm on a fresh page can
fail. ⓘ Rates do not transfer either (2.9 % is a CB-fetch rate on another channel at one preload
depth) — the overlap with ~1-boot-in-41-to-70 is *consistent with*, not *predicted by*. ⏭ It now
tests itself: both the barrier and the CB fields ship, so the next stall on this build is informative
either way.
🔧 **It took three fixes to the probe to get there, all one fact:** `abort+RESET` clears `TXFR_LEN`
and `CONBLK_AD` on BCM2711 but **not** `SOURCE_AD`. That broke the sentinel (refused every run), the
readback poll (exited before polling — 4969/5000 "not fetched", ungradeable) and, worst, the
classifier, which treated "not fetched" as `len==0 && src==0` — so a retained address would have
landed in a variant window and scored as a **stale fetch**, manufacturing the very result the probe
exists to test for. ⓘ Both of the probe's own refusals fired correctly and stopped a bad number being
reported; only the third run graded.

↩ Corrections banked: `DMA_CS=0x21` is the **healthy** state (9/10 boots read it while streaming), so
the captures' headline evidence was worthless · `CM_GATE` does not read back on this clock · the CL
dump added for the binner wedge landed on `rpi4-v3d`, which is **not auto-launched** — the shipping
games use the in-process winsys. ✅ Now armed there too (devices `663fd4d`), and it exposed that **two
existing fields read the wrong byte**: `BIN op=` sampled `ct0ca & ~3` and `RENDER wedge_op=` sampled
`ct1ca & ~0xf`, so old readings only hold when those were aligned.
✅ **Also fixed:** picocom kills its process group — that had been killing every boot-only bench at
trial 1 *and* hiding the netboot stage table (capture `a7514505f`).
📣 **Public docs caught up:** README + hardware matrix carry the containment; the reel has a publish
kit (chapters, title, description, caveats) at [`2026-09-18-reel-publish-kit.md`](../misc/2026-09-18-reel-publish-kit.md).
**📊 A clean day, measured not assumed:** all **238 UART logs / 56 MB** from 2026-09-18 through the
full fault regex: **0 fault patterns** (positive controls fire, so the detector is not vacuously
clean). ⓘ Quake III's gate frames moved 10 918 → 9 498 today; **not** a regression — fourteen archived
gates span 8 993-10 918 with everything else flat.
⏳ **Nothing built yet, deliberately:** a rebuild overwrites the TFTP `loader.disk` and the rate run is
measuring the *frozen* build. Everything above builds when it finishes.


**Same method, next defect — no capture yet:** 10 long STK races on the frozen build (6-lap profiles,
**31 806 frames**, the workload 3 of the 4 archived heap-guard fires came from) — **0 fires, 0
faults**. A bound with its exposure stated, not a clearance. What is banked: the shipping build is now
sampled with `why=`/`lheap?`/`freed?` in place, so the next fire says in one line whether a block
outlived its heap, a stale pointer's address got reused, or `->heap` was smashed.
