# Clean-build blocker: libmcs `rintf` breaks C++ Mesa compile in a fresh toolchain (2026-07-22)

## What happened

The clean, pure-public Docker build (`docker build` from the raw org Dockerfile URL,
`--no-cache`, empty context — nothing local used) got all the way through: cloned the 18
org forks, fetched upstream Mesa @ `mesa-26.2.0-rc1` + **applied `patches/mesa/` cleanly**,
built the cross toolchain, then **FAILED** at `build-quakespasm-phoenix.py`:

```
=== compile: 67/67 TUs OK ===
=== archived 67 objs -> .../libquakespasm.a ===
=== LINK FAILED: 73 undefined symbols ===     (st_setup_arrays, st_update_array,
    string_to_uint_map_*, _mesa_LinkProgram_no_error, _mesa_GetUniformLocation_no_error ...)
collect2: error: ld returned 1 exit status
[FAIL] build-quakespasm-phoenix.py failed
```

## Root cause (confirmed)

`build-gl-phoenix.py` compiles Mesa GL sources into `libGL-phoenix.a`. In the clean build,
the **C++** Mesa sources fail to compile and drop out of libGL:

```
FAIL src/mesa/state_tracker/st_atom_array.cpp :: 'float rintf(float)' was declared ...
FAIL src/compiler/glsl/ast_*.cpp             :: 'float rintf(float)' was declared ...
```

=> the whole state-tracker `st_*` + `string_to_uint_map.cpp` set drops from libGL →
`libGL-phoenix.a` is missing `st_setup_arrays` etc. → the GLQuake link fails on 73 undefined.

The trigger is a **`rintf` C++ declaration conflict** seen by the `aarch64-phoenix-g++` of a
**freshly built toolchain** (built from the *published* libphoenix, which now carries the
upstream **libmcs** libm — commits `2480901`/`1fe50cb`/`d0a2884`). The **local** build was
MASKED: the local `.toolchain/` predates the libmcs merge, so its `math.h` doesn't have the
conflict. A clean toolchain does. (Same libphoenix/libmcs math family as the earlier
`__infd` link-order issue that broke libXfont2/glib2.)

- `libphoenix/libm/libmcs/libm/include/math.h` declares `extern float rintf(float);` (line
  194) INSIDE an `extern "C"` block — so libmcs's header alone looks C++-safe. The conflict
  is therefore a CLASH between that and a second `rintf` declaration the C++ compile sees
  (libphoenix's own `include/math.h` and/or g++'s `<cmath>`), with mismatched linkage/spec —
  the classic *"declared 'extern' and later 'static'"* / redeclaration form.

## Why it matters

The PUBLISH is complete and correct (all 19 repos, mesa patch applies clean, forks clone,
toolchain builds). But a **public user cannot build the image from clean** until this is
fixed — it's a real release blocker, not a publish problem.

## Fix plan (next work item — multi-step, ~hours)

1. **Get the exact g++ error** (truncated in the log). Reproduce with a fresh toolchain:
   either rebuild the toolchain from the published libphoenix, or extract the post-merge
   sysroot `math.h` and compile a 1-line `#include <cmath>`/`<math.h>` C++ probe with
   `aarch64-phoenix-g++`. That pins the exact conflicting declarations.
2. **Fix in libphoenix `math.h`** (the C++ path): reconcile the `rintf` (and likely the
   sibling `lrintf`/`llrintf`/other float funcs + the `__infd` const) declarations so C++
   (`extern "C"` + matching signature, no extern/static clash) compiles cleanly. Careful: it
   is libc — must not break the C build or the ABI. This is a libphoenix (published sibling)
   change → re-push after.
3. **Rebuild the toolchain** (`build-phoenix-toolchain-linux.sh`, ~20 min) so the local
   toolchain matches the published libphoenix (also removes the stale-toolchain masking).
4. **Re-run the clean Docker build** to verify libGL is complete + the image builds.
5. Re-verify the local HW build still works (the toolchain change could shift libc behavior).

## Evidence

Full clean-build log was at `$CLAUDE_JOB_DIR/tmp/cleanbuild.log` (ephemeral). Key lines quoted
above. The build reached "materialized 84/88 generated sources" then the C++ compile FAILs;
84/88 is a red herring — the real failure is the rintf C++ conflict, not missing codegen.
</content>
