#!/usr/bin/env python3
"""Build Mesa's OpenGL frontend (st/mesa + main + program + glapi + GLSL compiler)
as a Phoenix static lib (GLQuake Path C, Phase 4), atop libv3d-phoenix.a.

Reuses build-v3d-phoenix.py's transform/abssrc/by_abs (host per-file flags ->
aarch64-phoenix). C files use aarch64-phoenix-gcc; C++ files (the GLSL compiler,
54 .cpp) use aarch64-phoenix-g++. The host build configured these files but never
ran their codegen, so we first `ninja` the generated GL headers in HOSTBUILD.

Output: /tmp/libGL-phoenix.a (the GL frontend; links with libv3d-phoenix.a + a
frontend/harness). Excludes x86-only sse_minmax.c (portable fallback exists).

Usage: python3 build-gl-phoenix.py
"""
import os, json, subprocess, glob

# pull in transform/abssrc/by_abs/TC/MESA/HOSTBUILD/COMPAT/SHIM/PORT/AR
_pre = open(os.path.join(os.path.dirname(__file__), "build-v3d-phoenix.py")).read().split("def main")[0]
exec(_pre)

TCXX = TC.rsplit("-", 1)[0] + "-g++"          # aarch64-phoenix-g++
GLOBJ = "/tmp/globj"
GL_LIB = "/tmp/libGL-phoenix.a"

# GL frontend source dirs (in compile_commands), and files to exclude.
GL_DIRS = ["/src/mesa/main/", "/mesa/state_tracker/", "/mesa/program/",
           "/src/mapi/", "/compiler/glsl/",
           "/src/mesa/math/", "/src/mesa/vbo/", "/src/mesa/glapi/shared-glapi/",
           "/gallium/auxiliary/cso_cache/",
           "/gallium/auxiliary/util/u_vbuf.", "/gallium/auxiliary/util/u_threaded_context.",
           "/gallium/auxiliary/util/u_index_modify.", "/gallium/auxiliary/util/u_sampler.",
           "/c11/impl/threads_posix.", "/src/util/u_thread.", "/src/util/log."]
# NOTE: gallium/auxiliary/draw/ (sw vertex pipeline) + compiler/spirv/ are
# deliberately NOT included — they drag in the whole sw-render subtree (tgsi_exec,
# translate, nir_to_tgsi) that v3d (HW vertex) never uses at runtime. st references
# a handful of draw_*/spirv_to_nir symbols which gl_stubs.c stubs instead.
GL_EXCLUDE = ["sse_minmax.c", "spirv2nir.c"]                  # x86 SSE intrinsics; portable path exists

# generated GL entrypoint .c (not in compile_commands; compiled with a mesa/main
# template's flags). Provide glClear/_mesa_marshal_*/_mesa_enum_to_string/dispatch.
GEN_C = ["src/mesa/glapi/glapi/gen/api_exec_init.c",
         "src/mesa/glapi/glapi/gen/enums.c"] + \
        [f"src/mesa/glapi/glapi/gen/marshal_generated{i}.c" for i in range(8)]

# generated GL headers to ninja-build in the host tree before compiling.
GEN_HEADERS = [
    "src/mesa/glapi/glapi/gen/dispatch.h", "src/mesa/glapi/glapi/gen/api_exec_decl.h",
    "src/mesa/glapi/glapi/gen/api_save_init.h", "src/mesa/glapi/glapi/gen/api_save.h",
    "src/mesa/glapi/glapi/gen/api_beginend_init.h", "src/mesa/glapi/glapi/gen/api_hw_select_init.h",
    "src/mesa/glapi/glapi/gen/marshal_generated.h", "src/mesa/glapi/glapi/gen/indirect.h",
    "src/mesa/glapi/glapi/gen/indirect_size.h", "src/mesa/glapi/glapi/gen/glapi_mapi_tmp.h",
    "src/mesa/glapi/shared-glapi/shared_glapi_mapi_tmp.h",
    "src/compiler/glsl/glsl_parser.h", "src/compiler/glsl/glcpp/glcpp-lex.c",
    "src/compiler/glsl/glcpp/glcpp-parse.h",
    "src/compiler/spirv/spirv_info.h", "src/compiler/spirv/vtn_generator_ids.h", "src/util/format/u_format_pack.h",
    "src/compiler/glsl/astc_glsl.h", "src/compiler/glsl/bc1_glsl.h",
    "src/compiler/glsl/bc4_glsl.h", "src/compiler/glsl/etc2_rgba_stitch_glsl.h",
    "src/compiler/glsl/cross_platform_settings_piece_all.h",
    "src/mesa/format_info.h", "src/mesa/get_hash.h",
    "src/mesa/program/program_parse.tab.h", "src/mesa/program/lex.yy.c",
    "src/mesa/glapi/glapi/gen/api_exec_init.c", "src/mesa/glapi/glapi/gen/enums.c",
] + [f"src/mesa/glapi/glapi/gen/marshal_generated{i}.c" for i in range(8)]


def gen_headers():
    r = subprocess.run(["ninja"] + GEN_HEADERS, cwd=HOSTBUILD, capture_output=True, text=True)
    print(f"[gen-headers] ninja rc={r.returncode}" + ("" if r.returncode == 0 else "\n" + r.stderr[-400:]))


def main():
    gen_headers()
    db = json.load(open(f"{HOSTBUILD}/compile_commands.json"))
    files = [e for e in db
             if any(d in e["file"] for d in GL_DIRS)
             and not any(x in e["file"] for x in GL_EXCLUDE)]
    os.makedirs(GLOBJ, exist_ok=True)
    objs, fails = [], []
    for e in files:
        src = abssrc(e["file"])
        out = f"{GLOBJ}/{os.path.basename(src)}.o"
        cmd = transform(e, src, out)
        if src.endswith(".cpp"):
            cmd[0] = TCXX
        r = subprocess.run(cmd, cwd=HOSTBUILD, capture_output=True, text=True)
        if r.returncode == 0:
            objs.append(out)
        else:
            fails.append((os.path.relpath(src, MESA),
                          [l for l in r.stderr.splitlines() if "error:" in l][:1]))
    # generated GL entrypoint .c — compile with a mesa/main template's flags.
    tmpl = next(e for e in db if e["file"].endswith("src/mesa/main/context.c"))
    for rel in GEN_C:
        src = os.path.normpath(os.path.join(HOSTBUILD, rel))
        if not os.path.exists(src):
            print(f"  [gen-c] MISSING {rel}"); continue
        out = f"{GLOBJ}/{os.path.basename(rel)}.o"
        cmd = transform(tmpl, src, out)
        r = subprocess.run(cmd, cwd=HOSTBUILD, capture_output=True, text=True)
        if r.returncode == 0:
            objs.append(out)
        else:
            fails.append((rel, [l for l in r.stderr.splitlines() if "error:" in l][:1]))

    print(f"[gl] OK={len(objs)} FAIL={len(fails)} (of {len(files)+len(GEN_C)})")
    for f, errs in fails:
        print(f"  FAIL {f} :: " + (errs[0].split('error:')[-1].strip()[:70] if errs else "?"))
    if objs:
        if os.path.exists(GL_LIB):
            os.remove(GL_LIB)
        subprocess.run([AR, "rcs", GL_LIB] + objs, check=True)
        print(f"[archive] {GL_LIB} ({len(objs)} objs, {os.path.getsize(GL_LIB)//1024} KiB)")


if __name__ == "__main__":
    main()
