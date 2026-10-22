#include "scenarios/boids/Separation.h"

#include <algorithm>

#include "scenarios/boids/BoidsRuleUtils.h"

namespace emergences::scenarios::boids {

Separation::Separation(float weight, float radius)
    : weight_(detail::validatedParameter(weight)), radius_(detail::validatedParameter(radius)) {}

glm::vec3 Separation::steer(const sim::Agent& self,
                            std::span<const sim::Neighbor> neighbors,
                            const sim::World& world) const {
    constexpr double minimumDistanceSquared = 1.0e-6;
    glm::dvec3 separationSum{0.0};
    std::size_t count = 0;
    for (const sim::Neighbor& neighbor : neighbors) {
        if (!detail::withinRadius(neighbor, radius_)) {
            continue;
        }
        const glm::vec3 displacement = world.displacement(self.position, neighbor.agent->position);
        // An exact overlap has no direction. Ignore it rather than inventing one;
        // near overlaps retain their direction with a bounded inverse-distance force.
        if (displacement == glm::vec3(0.0F)) {
            continue;
        }
        separationSum -= glm::dvec3{displacement} /
                         std::max(neighbor.distanceSquared, minimumDistanceSquared);
        ++count;
    }
    return count == 0 ? glm::vec3(0.0F) : glm::vec3{separationSum / static_cast<double>(count)};
}

void Separation::setWeight(float weight) {
    weight_ = detail::validatedParameter(weight);
}

void Separation::setRadius(float radius) {
    radius_ = detail::validatedParameter(radius);
}

} // namespace emergences::scenarios::boids
