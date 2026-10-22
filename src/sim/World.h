#pragma once

#include <glm/vec3.hpp>

namespace emergences::sim {

enum class BoundaryMode {
    Wrap,
    Bounce,
};

// Axis-aligned simulation bounds. Each component of minimum must be less than
// maximum. Boundary handling and neighbor lookup belong to the backend.
struct World {
    glm::vec3 minimum{-25.0F};
    glm::vec3 maximum{25.0F};
    BoundaryMode boundary{BoundaryMode::Wrap};
};

} // namespace emergences::sim
