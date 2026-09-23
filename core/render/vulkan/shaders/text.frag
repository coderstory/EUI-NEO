#version 450

layout(location = 0) in vec2 vUv;
layout(location = 1) in float vColored;
layout(location = 2) in vec4 vColor;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uGrayAtlas;
layout(set = 0, binding = 1) uniform sampler2D uColorAtlas;

layout(push_constant) uniform PushConstants {
    vec4 windowSize;
    vec4 color;
} pc;

void main() {
    // v0.8：颜色烘焙在每顶点里；pc.color 降级为批级 tint（默认白）。
    vec4 color = vColor * pc.color;
    if (vColored > 0.5) {
        vec4 sampleColor = texture(uColorAtlas, vUv);
        if (sampleColor.a <= 0.0) {
            discard;
        }
        outColor = sampleColor * color.a;
    } else {
        float alpha = texture(uGrayAtlas, vUv).r;
        if (alpha <= 0.0) {
            discard;
        }
        outColor = vec4(color.rgb, color.a * alpha);
    }
}
