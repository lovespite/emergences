#include "scenarios/boids/Alignment.h"

#include "scenarios/boids/BoidsRuleUtils.h"

namespace emergences::scenarios::boids {

Alignment::Alignment(float weight, float radius)
    : weight_(detail::validatedParameter(weight)), radius_(detail::validatedParameter(radius)) {}

glm::vec3 Alignment::steer(const sim::Agent& self,
                           std::span<const sim::Neighbor> neighbors,
                           const sim::World&) const {
    glm::vec3 velocitySum{0.0F};
    std::size_t count = 0;
    for (const sim::Neighbor& neighbor : neighbors) {
        if (detail::withinRadius(neighbor, radius_)) {
            velocitySum += neighbor.agent->velocity;
            ++count;
        }
    }
    return count == 0 ? glm::vec3(0.0F) : velocitySum / static_cast<float>(count) - self.velocity;
}

void Alignment::setWeight(float weight) {
    weight_ = detail::validatedParameter(weight);
}

void Alignment::setRadius(float radius) {
    radius_ = detail::validatedParameter(radius);
}

} // namespace emergences::scenarios::boids
