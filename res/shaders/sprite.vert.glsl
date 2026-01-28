#version 450

// Input: SpriteVertex (20 bytes)
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

// View-projection matrix (push constant slot 0)
layout(set = 1, binding = 0) uniform ViewProjection {
    mat4 viewProjection;
};

void main() {
    gl_Position = viewProjection * vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
