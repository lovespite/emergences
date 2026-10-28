#pragma once

#include "core/MeshRenderer.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace emergences::core {

// Flat normals make the triangulated terrain and insect bodies readable under
// directional light. Degenerate pole/cap triangles are omitted.
inline void appendTriangle(std::vector<MeshVertex>& vertices, const glm::vec3& a,
                           const glm::vec3& b, const glm::vec3& c,
                           const glm::vec3& color) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 cross = glm::cross(ab, ac);
    const float squaredLength = glm::dot(cross, cross);
    // A relative tolerance also rejects coincident sphere-pole vertices when
    // fused multiply-add leaves a tiny nonzero cross product after cancellation.
    const float areaScale = glm::dot(ab, ab) * glm::dot(ac, ac);
    if (!(squaredLength > std::max(1.0e-16F, areaScale * 1.0e-12F))) return;
    const glm::vec3 normal = cross / std::sqrt(squaredLength);
    vertices.push_back({a, normal, color});
    vertices.push_back({b, normal, color});
    vertices.push_back({c, normal, color});
}

inline void appendQuad(std::vector<MeshVertex>& vertices, const glm::vec3& a,
                       const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
                       const glm::vec3& color) {
    appendTriangle(vertices, a, b, c, color);
    appendTriangle(vertices, a, c, d, color);
}

// orientation's columns give the local X/Y/Z axes in world coordinates.
inline void appendEllipsoid(std::vector<MeshVertex>& vertices, const glm::vec3& center,
                            const glm::vec3& radii, const glm::vec3& color,
                            const glm::mat3& orientation = glm::mat3(1.0F),
                            unsigned int slices = 8, unsigned int stacks = 4) {
    if (!(radii.x > 0.0F && radii.y > 0.0F && radii.z > 0.0F)) return;
    slices = std::clamp(slices, 3U, 64U);
    stacks = std::clamp(stacks, 2U, 32U);
    constexpr float pi = std::numbers::pi_v<float>;
    const auto point = [&](unsigned int latitude, unsigned int longitude) {
        if (latitude == 0) return center + orientation * glm::vec3(0, radii.y, 0);
        if (latitude == stacks) return center - orientation * glm::vec3(0, radii.y, 0);
        const float theta = pi * static_cast<float>(latitude) / static_cast<float>(stacks);
        const float phi = 2.0F * pi * static_cast<float>(longitude) / static_cast<float>(slices);
        const glm::vec3 unit(std::sin(theta) * std::cos(phi), std::cos(theta),
                             std::sin(theta) * std::sin(phi));
        return center + orientation * (unit * radii);
    };
    for (unsigned int latitude = 0; latitude < stacks; ++latitude) {
        for (unsigned int longitude = 0; longitude < slices; ++longitude) {
            const auto a = point(latitude, longitude);
            const auto b = point(latitude, longitude + 1);
            const auto c = point(latitude + 1, longitude + 1);
            const auto d = point(latitude + 1, longitude);
            appendQuad(vertices, a, b, c, d, color);
        }
    }
}

// Closed, low-poly cylinder, useful for legs, antennae and diagnostic outlines.
inline void appendSegment(std::vector<MeshVertex>& vertices, const glm::vec3& from,
                          const glm::vec3& to, float radius, const glm::vec3& color,
                          unsigned int sides = 6) {
    const glm::vec3 delta = to - from;
    const float squaredLength = glm::dot(delta, delta);
    if (!(squaredLength > 1.0e-12F) || !(radius > 0.0F)) return;
    sides = std::clamp(sides, 3U, 32U);
    const glm::vec3 axis = delta / std::sqrt(squaredLength);
    const glm::vec3 reference = std::abs(axis.y) < 0.9F ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 right = glm::normalize(glm::cross(axis, reference));
    const glm::vec3 up = glm::cross(axis, right);
    constexpr float tau = 2.0F * std::numbers::pi_v<float>;
    for (unsigned int side = 0; side < sides; ++side) {
        const float first = tau * static_cast<float>(side) / static_cast<float>(sides);
        const float second = tau * static_cast<float>(side + 1) / static_cast<float>(sides);
        const auto a = radius * (std::cos(first) * right + std::sin(first) * up);
        const auto b = radius * (std::cos(second) * right + std::sin(second) * up);
        appendQuad(vertices, from + a, from + b, to + b, to + a, color);
        appendTriangle(vertices, from, from + b, from + a, color);
        appendTriangle(vertices, to, to + a, to + b, color);
    }
}

inline void appendRing(std::vector<MeshVertex>& vertices, const glm::vec3& center,
                       const glm::vec3& normal, float radius, float tubeRadius,
                       const glm::vec3& color, unsigned int segments = 32) {
    const float squaredLength = glm::dot(normal, normal);
    if (!(squaredLength > 1.0e-12F) || !(radius > 0.0F) || !(tubeRadius > 0.0F)) return;
    segments = std::clamp(segments, 6U, 96U);
    const glm::vec3 axis = normal / std::sqrt(squaredLength);
    const glm::vec3 reference = std::abs(axis.y) < 0.9F ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 right = glm::normalize(glm::cross(axis, reference));
    const glm::vec3 up = glm::cross(axis, right);
    constexpr float tau = 2.0F * std::numbers::pi_v<float>;
    for (unsigned int segment = 0; segment < segments; ++segment) {
        const float first = tau * static_cast<float>(segment) / static_cast<float>(segments);
        const float second = tau * static_cast<float>(segment + 1) / static_cast<float>(segments);
        const auto a = center + radius * (std::cos(first) * right + std::sin(first) * up);
        const auto b = center + radius * (std::cos(second) * right + std::sin(second) * up);
        appendSegment(vertices, a, b, tubeRadius, color, 4);
    }
}

} // namespace emergences::core
