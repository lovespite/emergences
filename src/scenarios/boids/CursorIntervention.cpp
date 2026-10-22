#include "scenarios/boids/CursorIntervention.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace emergences::scenarios::boids {
namespace {

float validatedParameter(float value) {
    if (!std::isfinite(value) || value < 0.0F) {
        throw std::invalid_argument("Cursor strength and radius must be finite and nonnegative");
    }
    return value;
}

glm::vec3 checkedVelocity(const glm::dvec3& value) {
    const double maximum = std::numeric_limits<float>::max();
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(value[axis]) || std::abs(value[axis]) > maximum) {
            throw std::overflow_error("Cursor impulse exceeds velocity range; use a smaller time step");
        }
    }
    return glm::vec3{value};
}

glm::dvec3 overlapDirection(std::uint32_t id) {
    // A coincident repelled agent has no radial direction. Stable ID-derived
    // axes separate overlaps without randomness or a zero-vector normalize.
    glm::dvec3 direction{0.0};
    direction[static_cast<glm::length_t>(id % 3U)] = (id / 3U) % 2U == 0U ? 1.0 : -1.0;
    return direction;
}

} // namespace

CursorIntervention::CursorIntervention(float strength, float radius, Direction direction)
    : strength_(validatedParameter(strength)), radius_(validatedParameter(radius)),
      direction_(direction) {}

void CursorIntervention::apply(float dt, std::vector<sim::Agent>& agents, sim::World& world) {
    if (!std::isfinite(dt) || dt < 0.0F) {
        throw std::invalid_argument("Cursor dt must be finite and nonnegative");
    }
    if (!enabled_ || dt == 0.0F || strength_ == 0.0F || radius_ == 0.0F) {
        return;
    }
    world.validate();
    const double radius = radius_;
    const double impulse = static_cast<double>(strength_) * dt;
    for (sim::Agent& agent : agents) {
        const glm::dvec3 displacement{world.displacement(position_, agent.position)};
        const double distance = glm::length(displacement);
        if (distance >= radius) {
            continue;
        }
        glm::dvec3 direction{0.0};
        if (distance > 0.0) {
            direction = displacement / distance;
        } else if (direction_ == Direction::Repel) {
            direction = overlapDirection(agent.id);
        } else {
            // An attracted agent already at the cursor needs no impulse.
            continue;
        }
        if (direction_ == Direction::Attract) {
            direction = -direction;
        }
        const double fraction = distance / radius;
        const double remaining = 1.0 - fraction;
        // Reverse smoothstep has zero slope at both the center and the edge.
        const double falloff = remaining * remaining * (1.0 + 2.0 * fraction);
        agent.velocity = checkedVelocity(glm::dvec3{agent.velocity} + direction * (impulse * falloff));
    }
}

void CursorIntervention::setPosition(const glm::vec3& position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        throw std::invalid_argument("Cursor position must be finite");
    }
    position_ = position;
}

void CursorIntervention::setStrength(float strength) {
    strength_ = validatedParameter(strength);
}

void CursorIntervention::setRadius(float radius) {
    radius_ = validatedParameter(radius);
}

} // namespace emergences::scenarios::boids
