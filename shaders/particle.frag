#version 450
layout(location = 0) in vec3 color;
layout(location = 1) in vec2 localPosition;
layout(location = 2) flat in int dotMode;
layout(location = 0) out vec4 outColor;
void main() {
    if (dotMode == 1 && dot(localPosition, localPosition) > 1.0) discard;
    outColor = vec4(color, 1.0);
}
