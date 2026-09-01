# Dynamic linking / shared libraries in Phoenix-RTOS — feasibility & design

**Task T-DYNLINK** (owner directive 2026-08-10: "assess if adding dynamic linking and
shared library support is feasible; if so design + implement + test it as a general system
feature, not only for Pi4"). This is the feasibility + design deliverable; implementation is a
staged follow-on. All facts below are sourced from current code (aarch64 MMU path).

## TL;DR

The owner's ask decomposes into **two materially different capabilities** — worth choosing
between explicitly:

- **(A) `dlopen()` of plugins into an otherwise-static program.** Unblocks the concrete Pi4
  use-cases (GL/GLX/DRI driver modules, codec/browser plugins, game mods). **Tractable and
  ZERO kernel change** — every primitive it needs already exists. Effort ~1–2 focused sessions
  for a working PoC. One real caveat (TLS-in-plugins). **Recommended first.**
- **(B) A full shared-library system** — shared `libc.so`, all programs dynamically linked via
  an `ld.so`/`PT_INTERP` interpreter, smaller images, standard dynamic executables. This is the
  literal reading of "shared libraries as a general system feature." It requires the hard kernel
  exec-ABI work **plus a dynamic-TLS implementation that does not exist today** — a much larger,
  multi-subsystem commitment.

**Both are feasible.** Recommendation: do **A first** (high value / low risk / no kernel
change), treat **B as a separate, larger, explicitly-chosen program**. A is NOT a substitute for
B — if the owner specifically wants shared `libc.so` / smaller images / standard dynamic
executables, that is B, and the dynamic-TLS gap alone makes it a much bigger job. **A-vs-B scope
is the owner's call; this doc gives both with costs.**

## Where Phoenix is today (static, single-ELF, position-dependent)

Independently confirmed on both the kernel and userspace sides:

- **Toolchain CAN already emit `-shared -fPIC` aarch64 `.so`** (verified: produces a valid
  `ELF ... shared object ... dynamically linked`). The compiler/binutils side is not the gap.
- **Everything is built static, non-PIC, fixed-VADDR.** No `-fPIC/-fpie/-shared/-pie` in
  `phoenix-rtos-build/target/aarch64.mk` or `Makefile.common`; the toolchain default links
  `ET_EXEC` at `0x400000`, **no `.interp`, no `.dynamic`, no `.rela.*`**. libphoenix is a static
  `libc.a` linked whole-archive into every program.
- **Kernel loader is static-only.** `process_load64` (proc/process.c:602-699) maps only
  `PT_LOAD` segments **at their exact `p_vaddr` with no load bias**, skips `p_vaddr==0`, and does
  **no relocation** on the MMU path (`R_AARCH64_*`/`Elf64_Rela` handling exists only in the
  NOMMU `#else`, not compiled for Pi4). `PT_DYNAMIC`/`PT_INTERP` are `#define`d (proc/elf.h:52-53)
  but **never acted on**.
- **Initial user stack is a Phoenix ABI, not SysV — and carries no auxv.** `process_exec`
  (process.c:1191-1246) pushes 4 words `{cleanupFn, argc, argv, envp}` (cleanupFn always `NULL`
  today); there is no `AT_PHDR/AT_BASE/AT_ENTRY/...` vector. `crt0.S` `_start` just pops those 4
  words and tail-calls `_startc`.
- **TLS is local-exec only.** The kernel sets `TPIDR_EL0` per-thread directly
  (hal/aarch64/cpu.c:297-303); TLS is discovered from **section** headers `.tdata/.tbss`
  (process.c:622-638), not `PT_TLS`. There is **no `__tls_get_addr`, no DTV, no dynamic-TLS
  relocation** anywhere.
- **No `dlopen/dlsym/dlfcn`, no `ld.so`, no rtld** exist. `include/link.h` is a vestigial stub.

### Three enablers already present (the reason A is cheap)

1. **File-backed `mmap` + `MAP_FIXED` + `mprotect` all exist** (syscalls.c:60-160; vm/map.c:585).
   A process can map an `.so` file into its own address space at a chosen base at runtime.
2. **`vm_mmap` accepts `PROT_EXEC`** — `sys_mmap` passes `prot` through (syscalls.c:113) and
   `vm_protToAttr` maps `PROT_EXEC → PGHD_EXEC` (vm/map.c:562). So a segment can be mapped **R-X
   directly at map time**, no post-hoc escalation needed.
3. **The ABI already reserves the dynamic-linker hook.** `crt0-common.c:84` — `if (cleanup !=
   NULL) atexit(cleanup)` with the comment "cleanup function is not NULL when the dynamic linker
   is used". stack[0] is the `_dl_fini` slot; the contract was anticipated.

## Phase A — in-process `dlopen()` for plugins (recommended, no kernel change)

The main program stays a normal static `ET_EXEC`. At runtime it calls `dlopen("foo.so")`; a
userspace runtime (`libdl` + a small in-process loader) does everything via existing syscalls.

**Load flow (all userspace):**
1. `open()` + read the `.so` (a `-fPIC` `ET_DYN`).
2. For each `PT_LOAD`: `mmap(MAP_FIXED)` at `base + p_vaddr` **with its final protection** — text
   `PROT_READ|PROT_EXEC`, data `PROT_READ|PROT_WRITE`. `base` is a single runtime-chosen bias
   (first-fit; no ASLR needed).
3. Apply relocations from `.rela.dyn`/`.rela.plt`: `R_AARCH64_RELATIVE` (`*slot = base + addend`),
   `R_AARCH64_GLOB_DAT`/`JUMP_SLOT`/`ABS64` (symbol lookup). **All relocation targets live in the
   RW segments (`.got/.got.plt/.data`)** — with PIC, `.text` is never written, so **no protection
   escalation ever happens** and the W^X policy (`vm_mprotect` rejects escalation past
   `protOrig`, map.c:883) is never triggered.
4. Optional `GNU_RELRO`: `mprotect` the relro range to read-only after relocation — this is a
   *de-escalation* (remove W), which the policy permits.
5. Run the `.so`'s `DT_INIT_ARRAY`; return a handle. `dlsym` walks the `.so`'s `.dynsym`.

**Symbol resolution — made clean by the no-ASLR property.** A plugin's undefined symbols
(`malloc`, `printf`, host callbacks) must resolve to the **host's** copies. Because the host is
fixed-VADDR with no ASLR, its **link-time `.symtab` addresses are valid at runtime verbatim** —
the loader resolves host symbols by reading the on-disk host ELF's symbol table. Requirement:
**the host must ship unstripped** (today we deploy `prog.stripped`) *or* carry an explicit export
table. (An explicit host-registered export table is the more controlled option and avoids
depending on `.symtab`.)

**Build rule — avoid the two-heap hazard.** Plugins MUST be built `-shared -fPIC` **leaving libc
undefined** (resolved at load time against the host), NOT statically linking their own libc.
Otherwise the plugin gets a second `malloc`/stdio instance with independent metadata operating on
the same heap → corruption. This is the deeper reason host-symbol resolution matters (beyond
callbacks).

**The one real caveat — TLS in plugins.** A dlopened `.so` that uses `__thread` needs dynamic TLS
(`__tls_get_addr` + DTV), which doesn't exist. Phase A mitigations: (a) build plugins without new
`__thread` state (many GL/codec plugins can); (b) support only initial-exec TLS with a small
pre-reserved surplus in the thread TLS block. Full general-dynamic TLS is deferred to Phase B.

**Deliverables for A:** `libphoenix/dl/` (`dlopen/dlsym/dlclose/dlerror` + the loader), a
`<dlfcn.h>`, a build recipe for `-fPIC` `ET_DYN` plugins, and a host build flag to export symbols
(unstripped or export-table). **PoC target:** a trivial host that `dlopen`s a `.so` exporting one
function that itself calls host `printf` — proves map + relocate + bidirectional symbol
resolution + execute. Then a real target (e.g. a GL-driver-shaped plugin).

**Kernel change required for A: none** (verified: `PROT_EXEC`-at-mmap works; the map-at-final-
protection design never escalates). Risk is contained to a new userspace library.

## Phase B — full shared-library system (larger; explicitly choose)

Standard dynamically-linked executables: main program is `ET_DYN`/PIE with `PT_INTERP` naming an
`ld.so` that maps `libc.so` + dependencies. Unlocks smaller images and the conventional model.

**Kernel exec-ABI work (the non-negotiable prerequisite):**
1. Parse `PT_INTERP` in `process_load64`, `proc_lookup` + `vm_objectGet` the interpreter (the
   FS-exec path already does exactly this object acquisition, process.c:1864-1878).
2. **Add a load bias** to `process_load64` — map at `bias + p_vaddr`, stop skipping `p_vaddr==0`
   (needed for any `ET_DYN`, i.e. both the interpreter and PIE mains). Co-equal blocker with the ABI.
3. Load the interpreter as a second object at a chosen base; set `entry = interp_base + e_entry`.
4. Convey startup metadata. **Two sub-options:**
   - **(B-a1) stock musl/glibc ld.so** — synthesize a full SysV `argc/argv/NULL/envp/NULL/auxv`
     stack (`AT_PHDR/AT_BASE/AT_ENTRY/AT_PHNUM/AT_PHENT/AT_RANDOM`). Bigger kernel + crt0 change,
     but reuses a mature loader.
   - **(B-a2) Phoenix-native ld.so** — pass interp metadata via the *existing* `stackArg`
     mechanism; smallest kernel change; but we write/maintain the loader. Best-aligned with the
     current ABI (recommended if B is chosen).
5. In-kernel dynamic linking (kernel resolves `DT_NEEDED` + relocations) is **rejected** — it
   would import a large security-sensitive subsystem into the microkernel; relocation belongs in
   userspace ld.so.

**Userspace work for B:** rebuild all of userspace (incl. libphoenix) `-fPIC`; produce a
`libc.so` + PIC crt; write/port the ld.so; add `dlopen` (shares the Phase-A loader core).

**The hard gap unique to B — dynamic TLS.** A shared `libc.so` needs general-dynamic / initial-
exec TLS (`__tls_get_addr`, a DTV per thread, `R_AARCH64_TLS*` relocations, kernel/`ld.so`
cooperation on TLS block layout). None of this exists; the current path sets `TPIDR_EL0` to a
single static block. This is a substantial subsystem in its own right and is the main reason B is
materially bigger than A.

**Effort/risk for B:** multi-session, kernel-ABI + libc + ld.so + TLS, real boot-regression risk
(gate every step with `--scope core` + netboot smoke + manifests). High value for a "general
system feature," but should be an explicit owner decision, not a side effect of A.

## Recommendation

1. **Ship Phase A first** — a userspace `dlopen` for plugins. Zero kernel change, unblocks the
   real Pi4 wins (GL/GLX/DRI modules, codecs, mods), contained risk. Start with the PoC above.
2. **Surface the A-vs-B choice to the owner.** If "shared libraries as a general system feature"
   specifically means shared `libc.so` / dynamically-linked everything / smaller images, that is
   Phase B and carries the dynamic-TLS + kernel-ABI cost. Phase A's loader core is reusable for B,
   so A is not wasted work toward B.

## Verified-fact index (current code)

| Fact | Source |
|------|--------|
| Toolchain emits valid `-shared -fPIC` aarch64 `.so` | probe (this session) |
| Loader maps only `PT_LOAD` at fixed `p_vaddr`, no bias, no MMU-path relocation | process.c:602-699, 647/652/677; NOMMU-only reloc guard :424/852/1126 |
| `PT_INTERP`/`PT_DYNAMIC` defined but never used | elf.h:52-53; grep of loader |
| Initial stack = 4-word Phoenix ABI, `cleanupFn=NULL`, no auxv | process.c:1191-1246; crt0.S:22-25 |
| `_dl_fini`/cleanup slot pre-reserved in crt | crt0-common.c:84 |
| File-backed mmap + MAP_FIXED + mprotect exist | syscalls.c:60-160; vm/map.c:585 |
| `vm_mmap` accepts `PROT_EXEC` (map R-X at map time) | syscalls.c:113; vm/map.c:552-563 |
| W^X: `vm_mprotect` rejects escalation past `protOrig` (de-escalation OK) | vm/map.c:853,883 |
| No ASLR (first-fit) → host `.symtab` addrs valid at runtime | vm/map.c:177-245 |
| TLS local-exec only; no `__tls_get_addr`/DTV/dynamic-TLS | hal/aarch64/cpu.c:297-303; process.c:622-638; errno.c:24 |
| Static libc.a, non-PIC, ET_EXEC @0x400000 | aarch64.mk:20/29; Makefile.common:227-231 |

Next step: a Phase-A `dlopen` PoC (a fresh, well-scoped session — do NOT tack onto a long turn).
