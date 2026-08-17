#version 450
// mv + a PUSH-CONSTANT mat4 => gl_Position.w is RUNTIME-computed (non-foldable), unlike the
// literal w=1.0 of every clean probe. Tests whether the perspective-interp (payload_w) path,
// which the compiler elides when w is a compile-time 1.0, is the striping trigger.
layout(push_constant) uniform PC { mat4 m; } pc;
layout(location = 0) out vec4 vTexcoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vFog;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vTexcoord = vec4(p * 2.0, 0.0, 0.0);
    vColor = vec4(1.0);
    gl_Position = pc.m * vec4(p * 2.0 - 1.0, 0.0, 1.0);
    vFog = gl_Position.w;
}
