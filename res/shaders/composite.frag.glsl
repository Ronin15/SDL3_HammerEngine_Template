#version 450

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;

// Output color
layout(location = 0) out vec4 outColor;

// Scene texture sampler
layout(set = 2, binding = 0) uniform sampler2D sceneSampler;

// Composite uniforms
layout(set = 3, binding = 0) uniform CompositeUBO {
    float subPixelOffsetX;
    float subPixelOffsetY;
    float zoom;
    float padding;
};

void main() {
    // Apply sub-pixel offset for smooth camera movement
    vec2 uv = fragTexCoord + vec2(subPixelOffsetX, subPixelOffsetY);

    // Sample scene texture
    outColor = texture(sceneSampler, uv);
}
