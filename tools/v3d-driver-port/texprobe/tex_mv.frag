#version 450
layout(location = 0) in vec4 vTexcoord;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vFog;
layout(location = 0) out vec4 oColor;
layout(set = 0, binding = 0) uniform sampler2D tex;
void main() {
    vec4 c = vColor * texture(tex, vTexcoord.xy);   // reads texcoord + color varyings, like basic
    float fog = clamp(exp(-vFog * vFog * 0.0), 0.0, 1.0);  // reads fog varying; == 1.0
    oColor = vec4(mix(vec3(0.0), c.rgb, fog), 1.0);
}
