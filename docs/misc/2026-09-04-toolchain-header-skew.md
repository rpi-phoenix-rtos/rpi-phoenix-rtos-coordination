# The GPU archives compile against a July snapshot of libphoenix's headers

**Status (2026-09-04): measured, latent, not yet biting. The fix is known and is the
rule this repo already wrote down for a different set of binaries.**

## What is actually true

The clean-rebuild runbook's risk #0 said "~400 Mesa objects resolve libc headers out
of `.toolchain/aarch64-phoenix/aarch64-phoenix/include` — the hand-maintained bundle,
whose only recent contents are two headers somebody copied by hand". Two parts of that
are wrong, and the correction matters because it changes what the hazard is.

Asking the compiler instead of guessing (`aarch64-phoenix-gcc -E -v`), the search path is:

```
.../lib/gcc/aarch64-phoenix/16.2.0/include
.../lib/gcc/aarch64-phoenix/16.2.0/include-fixed
.../aarch64-phoenix/include          <-- 3 entries: c++/, signal.h, string.h
.../aarch64-phoenix/usr/include      <-- 67 headers: THIS is where libc comes from
```

So the libc headers come from `aarch64-phoenix/usr/include`, not from the 3-entry
directory the runbook named. `stdio.h` there is dated **2026-07-23**; `unistd.h` was
touched **2026-09-01**. Every GPU archive
(`sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/build-{v3d,gl,v3dv}-phoenix.py`)
invokes the toolchain gcc with **no `--sysroot`**, so all of Mesa sees that snapshot,
while the rest of the system compiles against `_build/<target>/sysroot`.

## How bad the skew is — measured, not assumed

Comparing all shared headers between the toolchain snapshot and `sources/libphoenix/include`
(162 shared headers, 1,677 shared object-like macros):

| Class | Count | Consequence |
|---|---|---|
| Macros present only in the snapshot | **0** | — |
| Macros present only in live libphoenix | 2 | additive |
| Declarations present only in live libphoenix | many (`*at()` family, `stpncpy`, `memccpy`, `strtok_r`, …) | **fails loudly**: gcc-16/C23 makes an implicit declaration a hard error, so Mesa cannot silently mis-call these |
| **Macros whose VALUE differs** | **5** | **silent** |

The five are the whole hazard, and they are not innocuous — two pairs are **swapped**:

| Macro | Toolchain snapshot | Live libphoenix |
|---|---|---|
| `PTHREAD_PROCESS_PRIVATE` | `1` | `0` |
| `PTHREAD_PROCESS_SHARED` | `0` | `1` |
| `PTHREAD_PRIO_NONE` | `0` | `PH_LOCK_PROTO_NOINHERIT` = **1** |
| `PTHREAD_PRIO_INHERIT` | `1` | `PH_LOCK_PROTO_INHERIT` = **0** |
| `PTHREAD_PRIO_PROTECT` | `2` | `PH_LOCK_PROTO_PRIOCEILING` |

(`PH_LOCK_PROTO_*` values from `phoenix-rtos-kernel/include/threads.h:25-26`.)

Code compiled against the snapshot that asks for `PTHREAD_PRIO_INHERIT` passes `1`,
which the live libphoenix reads as **NOINHERIT** — priority inheritance silently
inverted on that mutex. Likewise `PROCESS_SHARED` becomes `PRIVATE`. This is the same
class as the recorded `_SC_NPROCESSORS` incident: a *value* skew, so nothing warns.

**Exposure today: none.** Neither `external/mesa/src/**` nor the built archives in
`tools/.gpu-libs/*.a` reference `PTHREAD_PRIO_*`, `PTHREAD_PROCESS_*`,
`pthread_mutexattr_setpshared` or `setprotocol` (checked by grep and by `nm -u`). So
this is a trap, not a live bug — it fires the first time any bare-toolchain-built code
sets a mutex attribute.

## The fix, and why it is not new policy

`scripts/build-rootfs-helpers.sh:70-88` already states the rule for the launcher
helpers, and states it as non-optional:

> Build against the SYSROOT the rest of the build just produced, never the toolchain's
> own bundled copy of libphoenix/libc/libm + headers. … Compiling with no `--sysroot`
> silently links these helpers against whatever libphoenix happened to be copied there
> last — the documented "stale `.toolchain` libphoenix.a" footgun (observed as CPython's
> `create_gil PyCOND_INIT failed`).

It even fails hard when the sysroot is absent. The GPU archives simply never got the
same treatment. So the fix is to apply the existing rule:

1. Pass `--sysroot=$buildroot/_build/<target>/sysroot` (plus the `-B`/`-iprefix` pair
   that `phoenix-rtos-build/makes/setup-sysroot.mk:17-23` uses) in
   `build-{v3d,gl,v3dv}-phoenix.py`, and fail if that sysroot is absent — same
   contract as the helpers.
2. That requires **core before gpu**. Today `PHASE gpu` runs first (confirmed in this
   build's own log, line 39, before core), which is *why* no sysroot exists to point
   at. The dependency that forced the order is only "the five game ports need the
   archives" — and the game ports run in the ports phase, after core. So
   `core → gpu → ports` satisfies both constraints; nothing needs the archives before
   core.
3. Expect the newly-visible headers to surface real compile errors that the stale
   snapshot hid. That is the point, and under C23 they surface as errors, not as
   silent misbehaviour.

**Correction to step 2's cost (2026-09-04, after reading the orchestration).** "Move the
gpu phase after core" is not a two-line swap. `rebuild-rpi4b-fast.sh:562-574` runs the gpu
phase before a **single** `build.sh` invocation, and that one invocation covers core *and*
ports *and* fs *and* project. The real constraint is narrower than the comment there
implies: since 2026-09-03 no game binary goes into `loader.disk`, so what actually needs
the archives is the **ports** stage (the five game ports link
`tools/.gpu-libs/lib{GL,v3d,v3dv}-phoenix.a` by absolute path and `b_die` without them —
`:590-600`). Core needs nothing from the gpu phase.

So the ordering wanted is `core → gpu → ports`, which needs `build.sh` split across two
invocations. The building blocks already exist (`--scope core`, `--ports-only`), so this is
an orchestration change of maybe 30 lines, not a refactor — but it is more than a swap, and
with exposure currently nil it is a **planned improvement, not an urgent fix**. Do it when
the clean-image chain is green, not in the middle of it.

## Cheapest verification

After the change, the skew must be zero by construction. Re-run the macro-value scan
(this document's table) against the sysroot the archives actually used; it must report
**0** differing values. Then one GPU smoke cycle (GLQuake or vkQuake torch check) to
confirm the archives still work.

## Do not "fix" it by re-copying headers into the toolchain

Hand-copying the current libphoenix headers into `.toolchain/.../usr/include` would
make today's diff zero and re-open tomorrow's: it is the same hand-maintained bundle
whose staleness caused this. The sysroot is generated by the build; point at it.
