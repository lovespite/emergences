#pragma once

#include <glm/vec3.hpp>

namespace emergences::sim {

enum class BoundaryMode {
    Wrap,
    Bounce,
};

// Axis-aligned simulation bounds. Bounds and their positive extents must be
// finite and representable as floats. Public data preserves aggregate setup.
struct World {
    glm::vec3 minimum{-25.0F};
    glm::vec3 maximum{25.0F};
    BoundaryMode boundary{BoundaryMode::Wrap};

    // Invalid bounds or boundary modes throw std::invalid_argument.
    void validate() const;

    // From-to vector, using the shortest periodic image in a wrapping world.
    // Exact half-period ties preserve the sign for positions inside the world.
    // Input vectors must be finite; an unrepresentable result throws.
    [[nodiscard]] glm::vec3 displacement(const glm::vec3& from,
                                         const glm::vec3& to) const;

    // Finite inputs are required. Wrap uses [minimum, maximum); bounce uses
    // closed bounds, reflects arbitrary overshoot, and points inward at walls.
    void applyBoundary(glm::vec3& position, glm::vec3& velocity) const;
};

} // namespace emergences::sim
