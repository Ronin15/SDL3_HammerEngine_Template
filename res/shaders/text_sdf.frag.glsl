#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

void main() {
    const float smoothing = (1.0 / 16.0);
    float distance = texture(texSampler, fragTexCoord).a;
    float alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance) * fragColor.a;
    outColor = vec4(fragColor.rgb * alpha, alpha);
}
