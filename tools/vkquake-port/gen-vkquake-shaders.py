#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Phoenix Systems. Author: Witold Bołt.
# Build recipe for the vkQuake Phoenix port (GPL-2.0-or-later); see COPYING.
"""Generate the embedded-SPIR-V C arrays vkQuake's renderer references at link
(world_vert_spv, alias_frag_spv, *_comp_spv, ...) for the aarch64-phoenix build.

vkQuake embeds each GLSL shader (Shaders/*.{vert,frag,comp}) as a
  const unsigned char <name>_spv[];
  const int          <name>_spv_size;
pair (see Shaders/shaders.h DECLARE_SHADER_SPV + Shaders/bintoc.c). The symbol name
is derived from the file: foo.vert -> foo_vert_spv, foo.frag -> foo_frag_spv,
foo.comp -> foo_comp_spv (matches Shaders/compile.sh).

Two modes, auto-selected:

  REAL  — if a glslang/glslangValidator (or glslc) is on PATH: compile each shader
          GLSL -> SPIR-V (the `sops` compute variants need --target-env vulkan1.1,
          per compile.sh) and emit the real bytes. This is what the on-HW pixel path
          needs.
  PLACEHOLDER — if no SPIR-V compiler is available (this host has none, and there is
          no network to fetch one): emit a *minimal valid SPIR-V module header*
          (5 words: magic, version, generator, bound=1, schema=0) for every shader so
          the symbols resolve at LINK and R_CreateShaderModule gets a structurally
          valid (if functionally empty) module. These are NOT runnable shaders — the
          renderer will need real SPIR-V before any pixel lands. The placeholder keeps
          the link/scaffold green; re-run this generator with glslang on PATH to swap
          in real bytes with no other change.

Output: one C file (default tools/vkquake-port/vkquake_shaders.c) defining all the
*_spv / *_spv_size arrays. Add it to the build (build-vkquake-phoenix.py picks it up).

Usage: python3 gen-vkquake-shaders.py [out.c]
"""
import os, struct, subprocess, sys, shutil

ROOT    = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHADERS = f"{ROOT}/external/vkquake/Shaders"
OUT     = sys.argv[1] if len(sys.argv) > 1 else f"{ROOT}/tools/vkquake-port/vkquake_shaders.c"

# file-extension -> symbol-suffix (matches Shaders/compile.sh)
EXT_SUFFIX = {".vert": "_vert", ".frag": "_frag", ".comp": "_comp"}

# Minimal *valid* SPIR-V module: just the 5-word header, no instructions. Accepted as a
# well-formed (empty) module by a SPIR-V parser; functionally a no-op. Placeholder only.
SPIRV_MAGIC   = 0x07230203
SPIRV_VERSION = 0x00010000   # SPIR-V 1.0 (Vulkan 1.0 baseline)
PLACEHOLDER_HEADER = struct.pack("<5I", SPIRV_MAGIC, SPIRV_VERSION, 0, 1, 0)


def find_glslang():
    for tool in ("glslangValidator", "glslang", "glslc"):
        p = shutil.which(tool)
        if p:
            return tool, p
    return None, None


def find_spirv_opt():
    """spirv-opt for the post-glslang optimize pass. Returns (path, has_canon) where
    has_canon = whether this spirv-opt supports --canonicalize-ids (glslang >= 16.0).
    Matches external/vkquake/meson.build's run-time probe."""
    p = shutil.which("spirv-opt")
    if not p:
        return None, False
    r = subprocess.run([p, "-h"], capture_output=True, text=True)
    return p, ("--canonicalize-ids" in (r.stdout or ""))


def compile_real(tool, path, shader_file, is_sops, spirv_opt, has_canon):
    """Compile GLSL -> SPIR-V exactly as external/vkquake/meson.build does:
      glslangValidator -V [--target-env vulkan1.1 for *sops*] -o X.spv INPUT
    then the optimize pass meson runs on EVERY shader (not just sops):
      spirv-opt -Os [--canonicalize-ids --strip-debug] X.spv -o X.spv
    The canonicalize/strip variant is used when spirv-opt advertises --canonicalize-ids
    (glslang >= 16.0, true on this host). The pass is non-functional (it optimizes +
    strips debug info, shrinking the module) — shaders render identically — but emitting
    it makes the embedded SPIR-V byte-identical to vkQuake's authoritative meson build."""
    out = "/tmp/_vkq_shader.spv"
    if tool == "glslc":
        cmd = [path, shader_file, "-o", out]
        if is_sops:
            cmd += ["--target-env=vulkan1.1"]
    else:  # glslangValidator / glslang
        cmd = [path, "-V", "--quiet", shader_file, "-o", out]
        if is_sops:
            cmd += ["--target-env", "vulkan1.1"]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=SHADERS)
    if r.returncode != 0:
        sys.stderr.write(f"  ! {os.path.basename(shader_file)} compile failed: "
                         f"{r.stderr.strip().splitlines()[-1] if r.stderr else '?'}\n")
        return None
    if spirv_opt:
        opt = [spirv_opt, "-Os"]
        if has_canon:
            opt += ["--canonicalize-ids", "--strip-debug"]
        opt += [out, "-o", out]
        ro = subprocess.run(opt, capture_output=True, text=True)
        if ro.returncode != 0:
            sys.stderr.write(f"  ! {os.path.basename(shader_file)} spirv-opt failed "
                             f"(using unoptimized SPIR-V): "
                             f"{ro.stderr.strip().splitlines()[-1] if ro.stderr else '?'}\n")
    with open(out, "rb") as fh:
        return fh.read()


def c_bytes(name, blob):
    lines = [f"const unsigned char {name}[] = {{"]
    for i in range(0, len(blob), 12):
        lines.append("\t" + ", ".join("0x%02x" % b for b in blob[i:i + 12]) + ",")
    lines.append("};")
    lines.append(f"const int {name}_size = {len(blob)};")
    return "\n".join(lines)


def main():
    tool, path = find_glslang()
    real = tool is not None
    spirv_opt, has_canon = find_spirv_opt() if real else (None, False)
    optdesc = ("" if not spirv_opt else
               " +spirv-opt -Os --canonicalize-ids --strip-debug" if has_canon
               else " +spirv-opt -Os")
    mode = f"REAL ({tool}{optdesc})" if real else "PLACEHOLDER (no glslang on PATH)"
    sys.stderr.write(f"gen-vkquake-shaders: mode = {mode}\n")

    shaders = []
    for f in sorted(os.listdir(SHADERS)):
        ext = os.path.splitext(f)[1]
        if ext in EXT_SUFFIX:
            shaders.append(f)

    out_lines = [
        "/* SPDX-License-Identifier: GPL-2.0-or-later",
        " *",
        " * Copyright (C) 2026 Phoenix Systems",
        " * Author: Witold Bołt",
        " *",
        " * Phoenix-RTOS platform backend for vkQuake (vkQuake is Copyright (C) id",
        " * Software, Inc. and the vkQuake developers, GPL-2.0-or-later). It implements",
        " * the vkQuake platform interface and is distributed under the same license as",
        " * the program it is built into; see COPYING in this directory.",
        " */",
        "/* GENERATED by tools/vkquake-port/gen-vkquake-shaders.py -- do not edit.",
        f" * Mode: {mode}.",
        " * Defines the embedded-SPIR-V arrays (<name>_spv / <name>_spv_size) that",
        " * vkQuake's renderer references (Shaders/shaders.h). In PLACEHOLDER mode each",
        " * array is a minimal valid (empty) SPIR-V header -- enough to LINK, NOT to",
        " * render. Re-run with glslang on PATH to emit real shader bytes. */",
        "// clang-format off",
        "",
    ]
    n_real = 0
    for f in shaders:
        base, ext = os.path.splitext(f)
        name = base + EXT_SUFFIX[ext] + "_spv"
        is_sops = "sops" in base
        blob = None
        if real:
            blob = compile_real(tool, path, f, is_sops, spirv_opt, has_canon)
            if blob is not None:
                n_real += 1
        if blob is None:
            blob = PLACEHOLDER_HEADER
        out_lines.append(c_bytes(name, blob))
        out_lines.append("")

    with open(OUT, "w") as fh:
        fh.write("\n".join(out_lines))

    kind = f"{n_real} real, {len(shaders) - n_real} placeholder" if real \
        else f"{len(shaders)} placeholder"
    sys.stderr.write(f"gen-vkquake-shaders: wrote {len(shaders)} shader arrays "
                     f"({kind}) -> {OUT}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
