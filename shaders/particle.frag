#version 450
layout(location = 0) in vec3 color;
layout(location = 1) in vec2 localPosition;
layout(location = 2) flat in int dotMode;
layout(location = 0) out vec4 outColor;
layout(constant_id = 0) const bool smoothDots = false;
float segmentDistance(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    return length(p - a - clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0) * ab);
}
void main() {
    float coverage = 1.0;
    if (dotMode == 2) {
        // Abdomen, thorax, head and six legs turn with the ant's actual heading.
        vec2 p = localPosition;
        float edge = min(length((p - vec2(0, -0.43)) / vec2(0.28, 0.39)) - 1.0,
                         length((p - vec2(0, 0.08)) / vec2(0.18, 0.25)) - 1.0) * 0.18;
        edge = min(edge, length(p - vec2(0, 0.48)) - 0.23);
        p.x = abs(p.x);
        edge = min(edge, segmentDistance(p, vec2(0.12, 0.20), vec2(0.58, 0.50)) - 0.045);
        edge = min(edge, segmentDistance(p, vec2(0.12, 0.03), vec2(0.63, -0.03)) - 0.045);
        edge = min(edge, segmentDistance(p, vec2(0.12, -0.13), vec2(0.55, -0.50)) - 0.045);
        edge = min(edge, segmentDistance(p, vec2(0.10, 0.63), vec2(0.28, 0.88)) - 0.035);
        if (smoothDots) {
            coverage = clamp(0.5 - edge / max(fwidth(edge), 0.0001), 0.0, 1.0);
            if (coverage <= 0.0) discard;
        } else if (edge > 0.0) discard;
    }
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
