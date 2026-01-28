#version 450

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Fullscreen triangle trick - no vertex buffer needed
    // Generates vertices for indices 0, 1, 2 that cover the entire screen
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    fragTexCoord = vec2(pos.x, 1.0 - pos.y);  // Flip Y for SDL coordinate system
}
