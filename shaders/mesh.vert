#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(push_constant) uniform View {
    mat4 viewProjection;
    vec4 right;
    vec4 up;
    vec4 options;
} view;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 color;
layout(location = 2) out float distanceToEye;
layout(location = 3) flat out vec3 towardEye;

void main() {
    gl_Position = view.viewProjection * vec4(inPosition, 1.0);
    normal = inNormal;
    color = inColor;
    // Perspective clip W is eye-space distance along the view direction.
    distanceToEye = max(gl_Position.w, 0.0);
    towardEye = normalize(cross(view.right.xyz, view.up.xyz));
}
