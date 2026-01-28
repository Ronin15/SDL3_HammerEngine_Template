#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

layout(set = 1, binding = 0) uniform UBO {
    mat4 viewProjection;
};

void main() {
    gl_Position = viewProjection * vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
}
