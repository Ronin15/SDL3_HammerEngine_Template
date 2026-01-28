#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D sceneTex;

layout(set = 3, binding = 0) uniform CompositeUBO {
    vec2 subPixelOffset;
    float zoom;
    float _padding;
};

void main() {
    // Apply sub-pixel offset and zoom
    vec2 uv = fragTexCoord / zoom + subPixelOffset;
    outColor = texture(sceneTex, uv);
}
