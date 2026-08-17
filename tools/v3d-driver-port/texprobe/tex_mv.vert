#version 450
// MULTI-VARYING variant: 3 varyings like vkQuake basic (vec4 texcoord + vec4 color + float fog),
// all COMPUTED (vertexless) — isolates the VARYING COUNT/VPM-layout as the striping trigger.
layout(location = 0) out vec4 vTexcoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vFog;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vTexcoord = vec4(p * 2.0, 0.0, 0.0);   // UV 0..4 (visible 0..2) -> 1:1 with 128^2 src
    vColor = vec4(1.0);                     // white: no tint, isolate the count not the value
    vFog = 1.0;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
