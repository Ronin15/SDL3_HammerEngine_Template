#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D sceneTex;

layout(set = 3, binding = 0, std140) uniform CompositeUBO {
    vec2 subPixelOffset;
    float zoom;
    float _pad0;
    vec4 ambientColor;  // RGB = tint color (0-1), A = blend strength
} compositeUbo;

void main() {
    // Apply sub-pixel offset and zoom
    vec2 uv = fragTexCoord / compositeUbo.zoom + compositeUbo.subPixelOffset;
    vec4 sceneColor = texture(sceneTex, uv);

    // Apply day/night ambient lighting tint
    // Multiply scene color by ambient color, blend based on alpha strength
    vec3 tinted = mix(sceneColor.rgb, sceneColor.rgb * compositeUbo.ambientColor.rgb, compositeUbo.ambientColor.a);

    outColor = vec4(tinted, sceneColor.a);
}
