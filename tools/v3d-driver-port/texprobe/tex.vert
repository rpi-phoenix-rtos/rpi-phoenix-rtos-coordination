#version 450
layout(location = 0) out vec2 vUV;
// fullscreen triangle from gl_VertexIndex, UV in [0,2] so the texture tiles ~2x
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUV = p * 2.0;                       // 0..4 UV => tiles the texture, exposes tiling/read bugs
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
