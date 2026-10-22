#pragma once

#include <cmath>
#include <stdexcept>

#include "sim/Rule.h"

namespace emergences::scenarios::boids::detail {

inline float validatedParameter(float value) {
    if (!std::isfinite(value) || value < 0.0F) {
        throw std::invalid_argument("Boids rule weight and radius must be finite and nonnegative");
    }
    return value;
}

inline bool withinRadius(const sim::Neighbor& neighbor, float radius) {
    // Widen before squaring so every finite radius remains representable.
    const double radiusSquared = static_cast<double>(radius) * radius;
    return radius > 0.0F && neighbor.distanceSquared >= 0.0 &&
           neighbor.distanceSquared <= radiusSquared;
}

} // namespace emergences::scenarios::boids::detail
