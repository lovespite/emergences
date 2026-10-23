#version 450
layout(location = 0) in vec3 color;
layout(location = 1) in vec2 localPosition;
layout(location = 2) flat in int dotMode;
layout(location = 0) out vec4 outColor;
layout(constant_id = 0) const bool smoothDots = false;
void main() {
    float coverage = 1.0;
    if (dotMode == 1) {
        float radius = length(localPosition);
        if (smoothDots) {
            float edgeWidth = max(fwidth(radius), 0.0001);
            coverage = clamp((1.0 - radius) / edgeWidth + 0.5, 0.0, 1.0);
            if (coverage <= 0.0) discard;
        } else if (radius > 1.0) {
            discard;
        }
    }
    outColor = vec4(color, coverage);
}
