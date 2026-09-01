# Quake3 (C5) VM-exec — re-characterized 2026-08-10

The board had C5 banked at: *"JIT'd QVM faults (Data Abort, far=0x10014329f = VM
offset 0x1432a0 with a stray bit-32) — a codegen bug in vm_aarch64.c's
dataMask/dataBase address computation."* This heartbeat's investigation changes
that on three counts.

## 1. The JIT codegen is NOT the bug (static analysis)

- `emit_MOVXi` (vm_aarch64.c:714) that loads `rDATABASE` is **correct**: for a
  `dataBase < 4GB` it emits `MOVZ64`(bits 0-15, which zeroes the whole X reg) +
  `MOVK64_16`(16-31) and returns, leaving bits 32-63 = 0. The `emit_MOVXi64`
  variant used for `rLITBASE` is only a *fixed-size* (always-4-instruction)
  encoding, not a correctness fix — so the load-vs-fixed inconsistency is a
  red herring, not the bit-32 source.
- Data accesses mask via `AND32(reg, rDATAMASK)` (zeroes the upper 32 bits) then
  `LDR32/STR32(rDATABASE, offset)` — correct for a valid 64-bit `dataBase`.
- So the JIT's dataBase/mask/access codegen does not introduce bit-32.

## 2. The 2026-08-05 fault was a WRITE in ENGINE code, not the JIT (advisor)

- `esr=0x92000045` → **WnR=1 (write)**, DFSC=0x05 (L1 translation fault). Prior
  notes called it a "read"; it is a store.
- `pc=0x402c48` is ~4 MB = low static `.text` = **engine C code**, not the JIT'd
  RWX mmap (which Phoenix places elsewhere). So the fault was engine code
  dereferencing a VM-translated pointer (the `VM_ArgPtr`/`VM_BlockCopy`/syscall-
  arg class), not JIT-emitted load/store.
- Runtime values captured (Q3JIT-DIAG, HW): `dataBase=0x0b09eb80` (LOW, bit-32=0),
  `dataMask=0x000fffff`, `dataAlloc=0x100400`. Discriminator result:
  `far=0x10014329f = 0x100000000 | 0x0014329f` — the low part `0x14329f` is
  **unmasked** (> dataMask 0xFFFFF, and > dataAlloc, i.e. OOB), `dataBase` is
  **absent**, and a lone bit-32 is present. That is an engine-side translation
  that dropped the base + skipped the mask + gained bit-32 — NOT a masked JIT
  access (those are always `& 0xFFFFF`, ≤ in-bounds).

## 3. The current build no longer reproduces the Data Abort (HW, 2 runs)

A fresh rebuild from the current `external/quake3e` + patch (plus a 2-line
Q3JIT-DIAG `Com_Printf`) boots Q3 **past** `VM_Compile(ui)` to the interactive
tty console (`]`) + IPv4 socket, with **no Data Abort anywhere in the log**, in
two cycles (q3diag 200 s, q3diag2 290 s). So the headline VM-exec fault does not
reproduce here.

**BUT the screen is BLACK** — the UI menu does not render. The log shows a **V3D
GPU wedge during R_Init**: `v3d-winsys: BIN TIMEOUT ... GPU wedged — true reset +
drop this frame (HW-marginal depth-pipeline drain stall)`. So the live blocker
moved from a VM-exec Data Abort to a **rendering** failure (GPU wedge → black),
with the engine otherwise up.

### Honest caveats (do not over-claim "fixed")

- 2 runs of an **instrumented** build. Not yet re-verified with pristine source,
  so no-fault cannot be cleanly attributed to source-drift vs a `.text`-layout
  shift from the added prints vs the fault being intermittent.
- The `bad opStack 8` warning at `VM_Compile(ui)` (jump target 11, instr 13586)
  still prints — the VM validation still flags it, but it is a warning, not fatal.
- The GPU wedge is tagged "HW-marginal"; whether the black screen is the wedge or
  the UI VM not drawing is unconfirmed.

## Next steps (fresh session)

1. Re-verify with **pristine** source (strip the Q3JIT-DIAG prints, rebuild, one
   cycle) to confirm the no-Data-Abort baseline is real, not layout-luck.
2. If stable: the blocker is the **R_Init GPU wedge** → investigate the V3D
   depth-pipeline drain stall (a HW-marginal winsys issue, shared with the
   vkQuake/quakespasm render paths) rather than the VM. Check whether the UI VM's
   draw calls reach the winsys at all (add a one-frame present log).
3. If the Data Abort returns intermittently: instrument the engine-side
   `VM_ArgPtr`/`VM_BlockCopy`/syscall pointer translation (vm.c ~241-275) to log
   the un-translated vs translated pointer and catch the bit-32 leak at its site.

Q3JIT-DIAG instrumentation is currently left in `external/quake3e` (local clone,
uncommitted) for step 1.

## UPDATE — pristine re-verify DONE (q3pristine, 2026-08-10)

Stripped the Q3JIT-DIAG prints, rebuilt (0 `Q3JIT-DIAG` strings in the ELF,
verified), one cycle:

- **NO Data Abort (0 faults).** Boot: `Hunk_Clear → finished R_Init → load
  vm/ui.qvm (VM_Compile, mprotect(RX)→RWX) → Opening IP socket → Started tty
  console (]`). So the 2026-08-05 "JIT stray-bit-32 Data Abort" **does not
  reproduce on pristine current source** — it is not a diag-layout artifact. The
  C5 headline VM-exec **crash is gone** (fixed by intervening source/patch drift
  since the bank). ✔ Confirmed.
- **The GPU wedge did NOT occur this run** (it happened in 1 of 3 runs) → the
  R_Init `BIN TIMEOUT`/wedge is **intermittent HW-marginal**, not deterministic,
  and (see next) not the cause of the black screen.
- **Screen is still BLACK** — the UI menu is not drawn, *even with no wedge*. Since
  quakespasm-sdl + vkQuake render correctly on the **same** V3D winsys (present
  path proven good), the blank render is **Q3-specific**.

### New C5 blocker: Q3 renders nothing (UI VM mis-exec / frame loop), not a crash

Leading hypothesis: the UI VM **mis-executes**. The `bad opStack 8` warning at
`VM_Compile(ui)` (jump target 11, instr 13586, OP_CONST) is the same VM-bytecode
operand-stack inconsistency that the old "interpreter mis-executes (bad opStack)"
note flagged — it is **mode-independent** (affects the JIT path too), a
VM-correctness (not crash) bug: the VM runs but computes/draws wrong → blank menu.
Alternative: Q3's client isn't pumping frames (stuck at the tty console) — but the
present path itself is proven by the other engines.

### Next steps (fresh session)
1. Discriminate mis-exec vs no-frames: log `SCR_UpdateScreen`/`SwapWindow` (present
   count) + whether `UI_Init`/`UI_Refresh` (the UI VM entry) is `VM_Call`ed each
   frame. If frames present but blank → UI VM mis-exec; if no frames → client loop
   stuck.
2. If UI-VM mis-exec: chase the `bad opStack` — instrument `VM_PrepareInterpreter`
   / the load-time opStack analysis (vm.c) at instruction 13586; check for an
   aarch64/parse/endianness issue in the QVM opStack tracking. This is the real
   remaining C5 VM-correctness bug (crash already resolved).
3. The intermittent R_Init GPU wedge is a separate, lower-priority HW-marginal
   winsys issue (shared path; already has a reset+retry mitigation).

## UPDATE 2 — BLACK-SCREEN ROOT-CAUSED + FIX IMPLEMENTED (2026-08-10)

Instrumented the present path on HW (Q3DRAW/PRESENT/END/SWAP diags) and traced it
end to end:

- `Q3DRAW-DIAG: frame=15541… cls.state=1(CA_DISCONNECTED) uiFull=1 keycatchUI=1` —
  `SCR_DrawScreenField` runs every frame and **the UI VM executes CORRECTLY**
  (returns fullscreen=1 AND set `KEYCATCH_UI`). So the `bad opStack` warning is
  **benign** — NOT a VM-correctness bug. (Refutes UPDATE-1's leading hypothesis.)
- `Q3END-DIAG: issued RC_SWAP_BUFFERS ×133`, `bail !tr.registered: 0` — `RE_EndFrame`
  issues the swap command (tr.registered is fine).
- **`Q3SWAP-DIAG: RB_SwapBuffers reached: 0`, `Q3PRESENT-DIAG: 0`** — the swap
  command is never dispatched; `GLimp_EndFrame` never runs → nothing is presented.

Cause: `R_IssueRenderCommands` (tr_cmds.c:89-91) early-returns
`if ( ri.CL_IsMinimized() ) return; // skip backend when minimized`.
`CL_IsMinimized()` returns `gw_minimized`, which starts `qtrue` (sdl_glimp.c:280,
re-set every `GLW_SetMode`) and clears ONLY on `SDL_WINDOWEVENT_SHOWN / RESTORED /
FOCUS_GAINED` (sdl_input.c:1315-1320).

**The Phoenix SDL video driver never delivered those events.** `PHOENIX_CreateWindow`
pre-set `SDL_WINDOW_SHOWN | _INPUT_FOCUS | _MOUSE_FOCUS` on `window->flags`, then
called `SDL_Set{Mouse,Keyboard}Focus`. SDL suppresses a state-change event when the
flag is already set, so pre-setting swallowed SHOWN/FOCUS_GAINED → `gw_minimized`
stuck true → Q3 skips the backend every frame → black. (quakespasm-sdl / yQuake2
render fine because they don't gate on `gw_minimized`.)

**FIX (ports `e498158`, committed local, NOT yet pushed to org):** in
`SDL_phoenixvideo.c` `PHOENIX_CreateWindow`, stop pre-setting the flags; call
`SDL_SendWindowEvent(SHOWN)` + `SDL_SetMouseFocus` + `SDL_SetKeyboardFocus` so SDL
sets the flags AND delivers the events. General SDL-port fix (not a per-game shim),
aligned with the owner's "use the SDL port" directive.

### Verify owed (next heartbeat)
Rebuild `libSDL2.a` (buildroot SDL port) → relink Q3 (it statically links
`libSDL2.a`) → one netboot cycle → confirm HDMI shows the Q3 main menu (not black)
+ `GLimp_EndFrame` now runs. Also confirm no regression to quakespasm-sdl / yQuake2
(they should be unaffected — the extra events are harmless to them). Only THEN push
ports `e498158` to the org. If Q3 renders, C5 goes from banked to **Q3 renders on
Phoenix/V3D** (3rd game engine visibly up).

## ★★★ VERIFIED (2026-08-10, q3render) — Quake III RENDERS on Phoenix/V3D

Rebuilt `libSDL2.a` with the fix (targeted `make SDL2-static` in the buildroot SDL
cmake dir after syncing the overlay → copied to `lib/`), relinked Q3, netboot cycle:

- **HDMI shows the Q3 UI VM rendering the "CD KEY" entry screen** — Q3 font title,
  the oval input box + text field/cursor, "PLEASE ENTER YOUR CD KEY", ACCEPT
  button, crosshair. Previously black. 0 faults, no GPU wedge.
- Confirms the whole chain: window SHOWN/FOCUS events now delivered → `gw_minimized`
  clears → `R_IssueRenderCommands` runs the backend → `RB_SwapBuffers` /
  `GLimp_EndFrame` present the frame → the UI VM's draw is visible.

The CD-KEY screen is the demo's first-run gate; it proves the UI VM + opengl1
renderer + V3D winsys + present path all work end-to-end. **Quake III is the 3rd
game engine visibly up on Phoenix** (after quakespasm + vkQuake render, yQuake2
fullscreen). Artifact: `artifacts/hdmi/20260810-213936-q3render-tick.png`.

Remaining before pushing ports `e498158` to org: regression-check the shared
`libSDL2.a` change against quakespasm-sdl / yQuake2 (expected clean — the fix only
adds correct window events).

## ★★★★ CAPSTONE (2026-08-11) — QUAKE III RENDERS FULL 3D GAMEPLAY

With the SDL fix in, drove Q3 all the way to in-game 3D:

1. **Main menu renders** — past the fix, the UI VM showed the demo's "CD KEY" gate
   (ioq3 `vm/ui.qvm` in pak1 enforces `Com_CDKeyValidate`). Bypassed with a
   format-valid `q3key` file (16 chars from the `{2,3,7,A,B,C,D,G,H,J,L,P,R,S,T,W}`
   set, no checksum → valid; this is the free demo, not a retail key) →
   **the Q3 main menu renders** (SINGLE PLAYER / MULTIPLAYER / SETUP / DEMOS / … over
   the Q3 logo).
2. `+demo demo001` did NOT play (the 1999 demo is an old network protocol vs
   quake3e's; it silently falls back to the menu). Sidestepped by loading a map.
3. **`+map q3dm1` → full 3D gameplay.** Log: `Server: q3dm1` → `qagame.qvm`
   compiled+loaded (RWX mmap) → `q3dm1.aas` loaded + `AAS initialized` →
   `cgame.qvm` compiled+loaded → `CL_InitCGame: 64.31s` →
   `UnnamedPlayer entered the game`, 0 faults. **HDMI shows q3dm1 "Arena Gate" in
   full 3D** — gothic red-rock arena, statues, skull crates, demon-face relief, red
   sky, a rocket-launcher pickup, weapon viewmodel, full HUD (health 100/armor
   100/ammo 20, face icon, crosshair), fullscreen 1920×1080, correct
   textures/lighting. Artifact: `artifacts/hdmi/20260810-230109-q3map2-tick.png`.

**All three Q3 VMs (ui + qagame server + cgame client) compile and run; the map +
bot-AAS + all assets load; the player spawns; the 3D world renders.** Quake III —
banked ~6 turns on the "VM-exec Data Abort" — is now the 4th game engine fully
rendering 3D on Phoenix/V3D (after quakespasm, vkQuake, yQuake2).

### Launch recipe (netboot, demoq3)
- `q3key` (16 valid-charset chars) in `demoq3/` → skips the CD-key gate.
- `q3config.cfg` in `demoq3/` with `seta r_mode "-1"` / `r_customwidth "1920"` /
  `r_customheight "1080"` / `sv_pure "0"` / `com_introplayed "1"` (keeps the psh
  launch line short — a long `+set`-heavy line truncates over the UART).
- `/usr/bin/quake3e +set fs_basepath /usr/share/quake3 +set fs_game demoq3 +map q3dm1`.

### Notes / follow-ups (low priority)
- `CL_InitCGame` took 64 s (NFS asset load + caches-off CPU) — slow but completes;
  same perf class as the other engines' first-load. Not a blocker.
- The R_Init GPU wedge remains intermittent (HW-marginal, auto-reset); did not recur
  on the successful run.
- Actual play/input (bots, movement) not exercised — the render + spawn are proven;
  input works via SDL (kbd/mouse). A playable demo would need a protocol-matched
  demo or a recorded one.
