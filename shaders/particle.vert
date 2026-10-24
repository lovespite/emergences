#version 450
struct Particle {
    vec3 position; float size;
    vec3 velocity; float reserved;
    vec3 color; uint id;
};
layout(std430, set = 0, binding = 0) readonly buffer Particles { Particle particles[]; };
layout(push_constant) uniform View {
    mat4 viewProjection;
    vec4 right;
    vec4 up;
    vec4 options;
} view;
layout(location = 0) out vec3 color;
layout(location = 1) out vec2 localPosition;
layout(location = 2) flat out int dotMode;
layout(location = 3) flat out int shadedDot;

void main() {
    Particle p = particles[gl_InstanceIndex];
    vec2 velocity = vec2(dot(p.velocity, view.right.xyz), dot(p.velocity, view.up.xyz));
    vec2 forward = dot(velocity, velocity) > 1e-8 ? normalize(velocity) : vec2(0.0, 1.0);
    vec2 side = vec2(forward.y, -forward.x);
    // Two triangles form a wide arrowhead with a recessed center at the tail.
    const vec2 arrow[6] = vec2[](vec2(0.0, 1.0), vec2(-0.9, -0.65), vec2(0.0, -0.15),
                                vec2(0.0, 1.0), vec2(0.0, -0.15), vec2(0.9, -0.65));
    const vec2 quad[6] = vec2[](vec2(-1,-1),vec2(1,-1),vec2(1,1),
                               vec2(-1,-1),vec2(1,1),vec2(-1,1));
    dotMode = int(view.options.y + 0.5);
    shadedDot = int(view.options.z + 0.5);
    localPosition = dotMode != 0 ? quad[gl_VertexIndex] : arrow[gl_VertexIndex];
    vec2 offset = dotMode == 1 ? localPosition * 0.5 :
                  side * localPosition.x + forward * localPosition.y;
    vec3 position = p.position + (view.right.xyz * offset.x + view.up.xyz * offset.y)
                    * max(p.size, 0.0) * view.options.x;
    gl_Position = view.viewProjection * vec4(position, 1.0);
    color = p.color;
}
