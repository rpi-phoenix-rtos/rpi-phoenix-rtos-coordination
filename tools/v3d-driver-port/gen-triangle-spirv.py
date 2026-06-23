#!/usr/bin/env python3
"""Generate minimal SPIR-V for a vertexless solid-color triangle, and emit it both
as standalone .spv files (for offline validation with spirv2nir) and as a C header
(triangle_spirv.h) the V3DV harness #includes.

WHY hand-authored: the host has no glslc/glslangValidator/shaderc and no internet, so
the usual GLSL->SPIR-V path is unavailable. The triangle is vertexless (the VS derives
3 clip-space positions from gl_VertexIndex), so there is also no vertex buffer / vertex
input state to get right for first-light. The FS writes a constant color.

Equivalent GLSL:

  // vertex (vert.spv), entry "main"
  #version 450
  layout(location = 0) out vec3 vColor;
  vec2 positions[3] = vec2[](vec2(0.0,-0.5), vec2(0.5,0.5), vec2(-0.5,0.5));
  vec3 colors[3]    = vec3[](vec3(1,0,0), vec3(0,1,0), vec3(0,0,1));
  void main() {
      gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
      vColor = colors[gl_VertexIndex];
  }

  // fragment (frag.spv), entry "main"
  #version 450
  layout(location = 0) in  vec3 vColor;
  layout(location = 0) out vec4 oColor;
  void main() { oColor = vec4(vColor, 1.0); }

To keep the hand-encoding small + robust we DON'T use arrays/SSA-indexing in SPIR-V
(that needs OpAccessChain into an array constant, error-prone by hand). Instead the VS
selects the position/color by gl_VertexIndex using OpSelect over two comparisons. The FS
just appends 1.0 to the interpolated color.

Validate:  spirv2nir -s vs vert.spv   /   spirv2nir -s fs frag.spv
"""
import struct, sys, os

# --- tiny SPIR-V assembler ------------------------------------------------------------
MAGIC = 0x07230203
VERSION = 0x00010000   # SPIR-V 1.0 (Vulkan 1.0 baseline; widest acceptance)
GENERATOR = 0

class Mod:
    def __init__(self):
        self.words = []
        self.bound = 1            # next free id
    def id(self):
        i = self.bound
        self.bound += 1
        return i
    def emit(self, op, *operands):
        # operands: ints or strings (literal). strings -> packed words, null-terminated.
        body = []
        for o in operands:
            if isinstance(o, str):
                body += str_words(o)
            else:
                body.append(o & 0xffffffff)
        wc = len(body) + 1
        self.words.append((wc << 16) | op)
        self.words += body
    def blob(self):
        header = [MAGIC, VERSION, GENERATOR, self.bound, 0]
        return struct.pack("<%dI" % (len(header) + len(self.words)),
                           *(header + self.words))

def str_words(s):
    b = s.encode("utf-8") + b"\0"
    while len(b) % 4:
        b += b"\0"
    return list(struct.unpack("<%dI" % (len(b) // 4), b))

# --- SPIR-V opcodes we use ------------------------------------------------------------
OpEntryPoint=15; OpExecutionMode=16; OpCapability=17; OpMemoryModel=14; OpExtInstImport=11
OpTypeVoid=19; OpTypeBool=20; OpTypeInt=21; OpTypeFloat=22; OpTypeVector=23
OpTypePointer=32; OpTypeFunction=33
OpConstant=43; OpConstantComposite=44
OpFunction=54; OpFunctionEnd=56; OpLabel=248; OpReturn=253
OpVariable=59; OpLoad=61; OpStore=62
OpCompositeExtract=81; OpCompositeConstruct=80
OpIEqual=170; OpSelect=169
OpDecorate=71; OpName=5
OpAccessChain=65

# storage classes
SC_INPUT=1; SC_OUTPUT=3; SC_FUNCTION=7
# builtins
BI_POSITION=0; BI_VERTEX_INDEX=42
# decorations
DEC_BUILTIN=11; DEC_LOCATION=30
# exec models
EM_VERTEX=0; EM_FRAGMENT=4
# exec modes
EXM_ORIGIN_UPPER_LEFT=7

def f32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def build_vs():
    m = Mod()
    m.emit(OpCapability, 1)                 # Shader
    glsl = m.id(); m.emit(OpExtInstImport, glsl, "GLSL.std.450")
    m.emit(OpMemoryModel, 0, 1)             # Logical, GLSL450

    # forward-declare entry-point interface ids by allocating now
    main_fn = m.id()
    gl_Position = m.id()    # output vec4 (via gl_PerVertex would be cleaner, but a bare
                            # BuiltIn Position output is accepted by spirv_to_nir)
    gl_VertexIndex = m.id() # input int
    vColor_out = m.id()     # output vec3 (location 0)

    m.emit(OpEntryPoint, EM_VERTEX, main_fn, "main", gl_Position, gl_VertexIndex, vColor_out)

    # decorations
    m.emit(OpName, main_fn, "main")
    m.emit(OpDecorate, gl_Position, DEC_BUILTIN, BI_POSITION)
    m.emit(OpDecorate, gl_VertexIndex, DEC_BUILTIN, BI_VERTEX_INDEX)
    m.emit(OpDecorate, vColor_out, DEC_LOCATION, 0)

    # types
    tvoid = m.id(); m.emit(OpTypeVoid, tvoid)
    tfn = m.id(); m.emit(OpTypeFunction, tfn, tvoid)
    tfloat = m.id(); m.emit(OpTypeFloat, tfloat, 32)
    tint = m.id(); m.emit(OpTypeInt, tint, 32, 1)
    tbool = m.id(); m.emit(OpTypeBool, tbool)
    tv2 = m.id(); m.emit(OpTypeVector, tv2, tfloat, 2)
    tv3 = m.id(); m.emit(OpTypeVector, tv3, tfloat, 3)
    tv4 = m.id(); m.emit(OpTypeVector, tv4, tfloat, 4)
    p_out_v4 = m.id(); m.emit(OpTypePointer, p_out_v4, SC_OUTPUT, tv4)
    p_out_v3 = m.id(); m.emit(OpTypePointer, p_out_v3, SC_OUTPUT, tv3)
    p_in_int = m.id(); m.emit(OpTypePointer, p_in_int, SC_INPUT, tint)

    # variables
    m.emit(OpVariable, p_out_v4, gl_Position, SC_OUTPUT)
    m.emit(OpVariable, p_in_int, gl_VertexIndex, SC_INPUT)
    m.emit(OpVariable, p_out_v3, vColor_out, SC_OUTPUT)

    # constants
    f0 = m.id(); m.emit(OpConstant, tfloat, f0, f32(0.0))
    f1 = m.id(); m.emit(OpConstant, tfloat, f1, f32(1.0))
    fp5 = m.id(); m.emit(OpConstant, tfloat, fp5, f32(0.5))
    fn5 = m.id(); m.emit(OpConstant, tfloat, fn5, f32(-0.5))
    i0 = m.id(); m.emit(OpConstant, tint, i0, 0)
    i1 = m.id(); m.emit(OpConstant, tint, i1, 1)

    # vertex 0: pos(0,-0.5) col(1,0,0); v1: pos(0.5,0.5) col(0,1,0); v2: pos(-0.5,0.5) col(0,0,1)
    pos0 = m.id(); m.emit(OpConstantComposite, tv2, pos0, f0, fn5)
    pos1 = m.id(); m.emit(OpConstantComposite, tv2, pos1, fp5, fp5)
    pos2 = m.id(); m.emit(OpConstantComposite, tv2, pos2, fn5, fp5)
    col0 = m.id(); m.emit(OpConstantComposite, tv3, col0, f1, f0, f0)
    col1 = m.id(); m.emit(OpConstantComposite, tv3, col1, f0, f1, f0)
    col2 = m.id(); m.emit(OpConstantComposite, tv3, col2, f0, f0, f1)

    # function body
    m.emit(OpFunction, tvoid, main_fn, 0, tfn)
    m.emit(OpLabel, m.id())
    vidx = m.id(); m.emit(OpLoad, tint, vidx, gl_VertexIndex)
    is0 = m.id(); m.emit(OpIEqual, tbool, is0, vidx, i0)
    is1 = m.id(); m.emit(OpIEqual, tbool, is1, vidx, i1)
    # pos = is0 ? pos0 : (is1 ? pos1 : pos2)
    pos_12 = m.id(); m.emit(OpSelect, tv2, pos_12, is1, pos1, pos2)
    pos = m.id(); m.emit(OpSelect, tv2, pos, is0, pos0, pos_12)
    col_12 = m.id(); m.emit(OpSelect, tv3, col_12, is1, col1, col2)
    col = m.id(); m.emit(OpSelect, tv3, col, is0, col0, col_12)
    px = m.id(); m.emit(OpCompositeExtract, tfloat, px, pos, 0)
    py = m.id(); m.emit(OpCompositeExtract, tfloat, py, pos, 1)
    posv4 = m.id(); m.emit(OpCompositeConstruct, tv4, posv4, px, py, f0, f1)
    m.emit(OpStore, gl_Position, posv4)
    m.emit(OpStore, vColor_out, col)
    m.emit(OpReturn)
    m.emit(OpFunctionEnd)
    return m.blob()


def build_fs():
    m = Mod()
    m.emit(OpCapability, 1)
    glsl = m.id(); m.emit(OpExtInstImport, glsl, "GLSL.std.450")
    m.emit(OpMemoryModel, 0, 1)

    main_fn = m.id()
    vColor_in = m.id()    # input vec3 location 0
    oColor = m.id()       # output vec4 location 0
    m.emit(OpEntryPoint, EM_FRAGMENT, main_fn, "main", vColor_in, oColor)
    m.emit(OpExecutionMode, main_fn, EXM_ORIGIN_UPPER_LEFT)
    m.emit(OpName, main_fn, "main")
    m.emit(OpDecorate, vColor_in, DEC_LOCATION, 0)
    m.emit(OpDecorate, oColor, DEC_LOCATION, 0)

    tvoid = m.id(); m.emit(OpTypeVoid, tvoid)
    tfn = m.id(); m.emit(OpTypeFunction, tfn, tvoid)
    tfloat = m.id(); m.emit(OpTypeFloat, tfloat, 32)
    tv3 = m.id(); m.emit(OpTypeVector, tv3, tfloat, 3)
    tv4 = m.id(); m.emit(OpTypeVector, tv4, tfloat, 4)
    p_in_v3 = m.id(); m.emit(OpTypePointer, p_in_v3, SC_INPUT, tv3)
    p_out_v4 = m.id(); m.emit(OpTypePointer, p_out_v4, SC_OUTPUT, tv4)

    m.emit(OpVariable, p_in_v3, vColor_in, SC_INPUT)
    m.emit(OpVariable, p_out_v4, oColor, SC_OUTPUT)

    f1 = m.id(); m.emit(OpConstant, tfloat, f1, f32(1.0))

    m.emit(OpFunction, tvoid, main_fn, 0, tfn)
    m.emit(OpLabel, m.id())
    c = m.id(); m.emit(OpLoad, tv3, c, vColor_in)
    r = m.id(); m.emit(OpCompositeExtract, tfloat, r, c, 0)
    g = m.id(); m.emit(OpCompositeExtract, tfloat, g, c, 1)
    b = m.id(); m.emit(OpCompositeExtract, tfloat, b, c, 2)
    out = m.id(); m.emit(OpCompositeConstruct, tv4, out, r, g, b, f1)
    m.emit(OpStore, oColor, out)
    m.emit(OpReturn)
    m.emit(OpFunctionEnd)
    return m.blob()


def c_array(name, blob):
    words = struct.unpack("<%dI" % (len(blob) // 4), blob)
    lines = [f"static const uint32_t {name}[] = {{"]
    for i in range(0, len(words), 6):
        chunk = ", ".join("0x%08x" % w for w in words[i:i + 6])
        lines.append("    " + chunk + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = sys.argv[1] if len(sys.argv) > 1 else "/tmp"
    vs = build_vs()
    fs = build_fs()
    open(os.path.join(outdir, "tri_vert.spv"), "wb").write(vs)
    open(os.path.join(outdir, "tri_frag.spv"), "wb").write(fs)
    hdr = ("/* GENERATED by gen-triangle-spirv.py -- do not edit. Vertexless solid-color\n"
           " * triangle: VS derives 3 clip positions+colors from gl_VertexIndex, FS writes\n"
           " * the interpolated color. No vertex buffer / vertex input state. */\n"
           "#ifndef TRIANGLE_SPIRV_H\n#define TRIANGLE_SPIRV_H\n#include <stdint.h>\n\n"
           + c_array("triangle_vert_spirv", vs) + "\n\n"
           + c_array("triangle_frag_spirv", fs) + "\n\n#endif\n")
    open(os.path.join(here, "triangle_spirv.h"), "w").write(hdr)
    print("wrote tri_vert.spv (%d bytes), tri_frag.spv (%d bytes), triangle_spirv.h"
          % (len(vs), len(fs)))


if __name__ == "__main__":
    main()
