#version 450
// The INTERSECTION: 3 varyings sourced from a MULTI-ATTRIBUTE vertex buffer (pos+uv+color),
// matching vkQuake basic's vertex feed. Tests whether VB-multi-attr x multi-varying is the trigger.
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 0) out vec4 vTexcoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vFog;
void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    vTexcoord = vec4(in_uv, 0.0, 0.0);
    vColor = in_color;
    vFog = 1.0;
}
