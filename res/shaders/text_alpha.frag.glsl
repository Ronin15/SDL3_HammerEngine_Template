#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

void main() {
    float alpha = texture(texSampler, fragTexCoord).a * fragColor.a;
    outColor = vec4(fragColor.rgb * alpha, alpha);
}
