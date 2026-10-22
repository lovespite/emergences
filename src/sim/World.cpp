#include "sim/World.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace emergences::sim {
namespace {

void requireFinite(const glm::vec3& value) {
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(value[axis])) {
            throw std::invalid_argument("World vectors must be finite");
        }
    }
}

double positiveRemainder(double value, double period) {
    const double result = std::fmod(value, period);
    return result < 0.0 ? result + period : result;
}

} // namespace

void World::validate() const {
    requireFinite(minimum);
    requireFinite(maximum);
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        const double extent = static_cast<double>(maximum[axis]) - minimum[axis];
        if (extent <= 0.0 || extent > std::numeric_limits<float>::max()) {
            throw std::invalid_argument("World extents must be positive finite floats");
        }
    }
    if (boundary != BoundaryMode::Wrap && boundary != BoundaryMode::Bounce) {
        throw std::invalid_argument("Unknown world boundary mode");
    }
}

glm::vec3 World::displacement(const glm::vec3& from, const glm::vec3& to) const {
    validate();
    requireFinite(from);
    requireFinite(to);
    glm::vec3 result{0.0F};
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        double delta = static_cast<double>(to[axis]) - from[axis];
        if (boundary == BoundaryMode::Wrap) {
            const double extent = static_cast<double>(maximum[axis]) - minimum[axis];
            delta = std::remainder(delta, extent);
        }
        if (std::abs(delta) > std::numeric_limits<float>::max()) {
            throw std::overflow_error("World displacement exceeds float range");
        }
        result[axis] = static_cast<float>(delta);
    }
    return result;
}

void World::applyBoundary(glm::vec3& position, glm::vec3& velocity) const {
    validate();
    requireFinite(position);
    requireFinite(velocity);
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        const double low = minimum[axis];
        const double extent = static_cast<double>(maximum[axis]) - low;
        if (boundary == BoundaryMode::Wrap) {
            if (position[axis] >= minimum[axis] && position[axis] < maximum[axis]) {
                continue;
            }
            const double offset = positiveRemainder(static_cast<double>(position[axis]) - low,
                                                    extent);
            const float wrapped = static_cast<float>(low + offset);
            // Casting can round a point just below maximum onto maximum.
            position[axis] = std::clamp(wrapped, minimum[axis],
                                       std::nextafter(maximum[axis], minimum[axis]));
        } else {
            if (position[axis] >= minimum[axis] && position[axis] <= maximum[axis]) {
                if (position[axis] == minimum[axis]) {
                    velocity[axis] = std::abs(velocity[axis]);
                } else if (position[axis] == maximum[axis]) {
                    velocity[axis] = -std::abs(velocity[axis]);
                }
                continue;
            }
            const double offset = positiveRemainder(static_cast<double>(position[axis]) - low,
                                                    2.0 * extent);
            const bool reflected = offset > extent;
            const double folded = reflected ? 2.0 * extent - offset : offset;
            position[axis] = std::clamp(static_cast<float>(low + folded),
                                       minimum[axis], maximum[axis]);
            if (reflected) {
                velocity[axis] = -velocity[axis];
            }
            if (position[axis] == minimum[axis]) {
                velocity[axis] = std::abs(velocity[axis]);
            } else if (position[axis] == maximum[axis]) {
                velocity[axis] = -std::abs(velocity[axis]);
            }
        }
    }
}

} // namespace emergences::sim
