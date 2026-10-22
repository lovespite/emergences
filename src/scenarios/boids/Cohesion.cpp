#include "scenarios/boids/Cohesion.h"

#include "scenarios/boids/BoidsRuleUtils.h"

namespace emergences::scenarios::boids {

Cohesion::Cohesion(float weight, float radius)
    : weight_(detail::validatedParameter(weight)), radius_(detail::validatedParameter(radius)) {}

glm::vec3 Cohesion::steer(const sim::Agent& self,
                          std::span<const sim::Neighbor> neighbors,
                          const sim::World& world) const {
    glm::vec3 displacementSum{0.0F};
    std::size_t count = 0;
    for (const sim::Neighbor& neighbor : neighbors) {
        if (detail::withinRadius(neighbor, radius_)) {
            displacementSum += world.displacement(self.position, neighbor.agent->position);
            ++count;
        }
    }
    return count == 0 ? glm::vec3(0.0F) : displacementSum / static_cast<float>(count);
}

void Cohesion::setWeight(float weight) {
    weight_ = detail::validatedParameter(weight);
}

void Cohesion::setRadius(float radius) {
    radius_ = detail::validatedParameter(radius);
}

} // namespace emergences::scenarios::boids
