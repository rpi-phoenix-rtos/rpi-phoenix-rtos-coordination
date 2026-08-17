#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;
layout(set = 0, binding = 0) uniform sampler2D tex;
void main() { oColor = vec4(texture(tex, vUV).rgb, 1.0); }
