#version 450
// Vertex-ATTRIBUTE variant of tex.vert: UV comes from a vertex-buffer attribute
// (not computed from gl_VertexIndex) — the one delta vs the clean probe, to test
// whether the vertex attribute-fetch / VS->VPM path is the striping trigger.
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec2 vUV;
void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    vUV = in_uv;
}
