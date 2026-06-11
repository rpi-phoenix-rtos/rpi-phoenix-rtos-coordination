# v3d-shader-tool — off-device V3D-4.2 shader compiler harness (GLQuake Path A)

Compiles trivial shaders through **Mesa's v3d NIR→QPU compiler on the host** and
dumps + disassembles the QPU bytecode, so we derive correct, validated V3D-4.2
shaders for the Phoenix bare-metal GPU path WITHOUT the Pi (the proprietary v3d
simulator is unavailable; we drive `v3d_compile()` directly).

## Status (2026-06-11): WORKS
`v3d_compile()` produces valid QPU for a fragment shader (10 inst, 4 threads);
disasm shows `thrsw` / `ldtlb` / `mov tlb,..` / thread-end — a well-formed FS.
Known refinement: the const color currently lowers to `mov tlb, 0` (value not
wired through; shader *structure* is correct). Next: VS + coordinate shader,
fix the color, then embed bytecode + a GL_SHADER_STATE_RECORD in rpi4-v3d-scout.

## Build recipe (host)
The harness is `v3d_shader_dump.c`, placed in the Mesa clone at
`external/mesa/src/broadcom/compiler/` with an `executable()` target added to
`external/mesa/src/broadcom/meson.build` (after `libbroadcom_v3d`):

```meson
executable('v3d_shader_dump', files('compiler/v3d_shader_dump.c'),
  include_directories : [inc_include, inc_src, inc_broadcom, inc_compiler],
  link_with : [libbroadcom_v3d],
  dependencies : [idep_nir, idep_mesautil, dep_libdrm, dep_valgrind, dep_thread],
  build_by_default : true)
```

Python 3.14 host needs mako/pyyaml/packaging in a uv venv (distutils gone in 3.14):
```
uv venv /tmp/mesa-py
uv pip install --python /tmp/mesa-py/bin/python mako pyyaml packaging
PATH=/tmp/mesa-py/bin:$PATH meson setup /tmp/mesa-v3d-build external/mesa \
  -Dgallium-drivers=v3d -Dvulkan-drivers= -Dplatforms= -Dglx=disabled \
  -Degl=disabled -Dgbm=disabled -Dbuildtype=release
PATH=/tmp/mesa-py/bin:$PATH ninja -C /tmp/mesa-v3d-build src/broadcom/v3d_shader_dump
/tmp/mesa-v3d-build/src/broadcom/v3d_shader_dump
```

devinfo for V3D 4.2 (from real Pi4 core IDENT0=0x04443356 / IDENT1=0x81001422):
ver=42, vpm_size=65536, qpu_count=8 (nslc2*qups4), clipper_xy_granularity=256,
cle_readahead=256, has_accumulators=true, page_size=4096.

Debugged to working via gdb (the off-device oracle): fixed v3d_fs_key fields,
store_output intrinsic (not nir_store_var → avoids derefs), output variable (for
c->outputs sizing), and full devinfo population (else SIGFPE in v3d_vir_to_qpu).
