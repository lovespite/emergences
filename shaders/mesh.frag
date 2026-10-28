#version 450

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 color;
layout(location = 2) in float distanceToEye;
layout(location = 3) flat in vec3 towardEye;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(normal);
    const vec3 sunlight = normalize(vec3(-0.48, 0.82, 0.31));
    float diffuse = max(dot(n, sunlight), 0.0);
    float sky = n.y * 0.5 + 0.5;
    vec3 ambient = mix(vec3(0.24, 0.21, 0.18), vec3(0.40, 0.44, 0.47), sky);
    vec3 shaded = color * (ambient + vec3(0.73, 0.69, 0.58) * diffuse);
    float highlight = pow(max(dot(n, normalize(sunlight + towardEye)), 0.0), 22.0);
    shaded += vec3(0.055) * highlight;
    // Keep the colony legible while gently separating distant hills.
    float fog = smoothstep(100.0, 340.0, distanceToEye) * 0.24;
    shaded = mix(shaded, vec3(0.17, 0.22, 0.26), fog);
    outColor = vec4(shaded, 1.0);
}
