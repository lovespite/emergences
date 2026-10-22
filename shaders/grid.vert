#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 vertexColor;
layout(location = 0) out vec3 color;

// Matches core::SceneView and the particle shader's push-constant layout.
layout(push_constant) uniform View {
    mat4 viewProjection;
    vec4 right;
    vec4 up;
    vec4 options;
} view;

void main() {
    gl_Position = view.viewProjection * vec4(position, 1.0);
    color = vertexColor;
}
