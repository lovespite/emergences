#include "scenarios/boids/BoidsRuleKernel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "scenarios/boids/Alignment.h"
#include "scenarios/boids/Cohesion.h"
#include "scenarios/boids/Separation.h"

namespace emergences::scenarios::boids {

bool BoidsRuleKernel::prepare(std::span<const std::unique_ptr<sim::Rule>> rules) {
    prepared_ = false;
    if (rules.size() != 3) {
        return false;
    }
    std::array<bool, 3> seen{};
    for (std::size_t position = 0; position < rules.size(); ++position) {
        const sim::Rule* rule = rules[position].get();
        std::size_t kind;
        if (dynamic_cast<const Separation*>(rule)) {
            kind = SeparationIndex;
        } else if (dynamic_cast<const Alignment*>(rule)) {
            kind = AlignmentIndex;
        } else if (dynamic_cast<const Cohesion*>(rule)) {
            kind = CohesionIndex;
        } else {
            return false;
        }
        if (seen[kind]) {
            return false;
        }
        seen[kind] = true;
        order_[position] = kind;
        weights_[kind] = rule->weight();
        radii_[kind] = rule->radius();
        radiusSquared_[kind] = static_cast<double>(radii_[kind]) * radii_[kind];
    }
    prepared_ = true;
    return true;
}

glm::dvec3 BoidsRuleKernel::acceleration(const sim::Agent& self,
    std::span<const sim::Neighbor> neighbors, const sim::World& world) const {
    if (!prepared_) {
        throw std::logic_error("Boids kernel requires a supported prepared rule collection");
    }
    // Preserve each original rule's precision and visitation order. In
    // particular, alignment/cohesion accumulate floats while separation uses
    // doubles, then every steering result is rounded to float before weighting.
    glm::dvec3 separationSum{0.0};
    glm::vec3 velocitySum{0.0F};
    glm::vec3 displacementSum{0.0F};
    std::array<std::size_t, 3> counts{};
    const bool separationActive = weights_[SeparationIndex] > 0.0F && radii_[SeparationIndex] > 0.0F;
    const bool alignmentActive = weights_[AlignmentIndex] > 0.0F && radii_[AlignmentIndex] > 0.0F;
    const bool cohesionActive = weights_[CohesionIndex] > 0.0F && radii_[CohesionIndex] > 0.0F;
    for (const sim::Neighbor& neighbor : neighbors) {
        if (neighbor.distanceSquared < 0.0) {
            continue;
        }
        if (separationActive && neighbor.distanceSquared <= radiusSquared_[SeparationIndex]) {
            const glm::vec3 displacement = neighbor.displacement ? *neighbor.displacement
                : world.displacement(self.position, neighbor.agent->position);
            if (displacement != glm::vec3{0.0F}) {
                separationSum -= glm::dvec3{displacement} /
                    std::max(neighbor.distanceSquared, 1.0e-6);
                ++counts[SeparationIndex];
            }
        }
        if (alignmentActive && neighbor.distanceSquared <= radiusSquared_[AlignmentIndex]) {
            velocitySum += neighbor.agent->velocity;
            ++counts[AlignmentIndex];
        }
        if (cohesionActive && neighbor.distanceSquared <= radiusSquared_[CohesionIndex]) {
            displacementSum += neighbor.displacement ? *neighbor.displacement
                : world.displacement(self.position, neighbor.agent->position);
            ++counts[CohesionIndex];
        }
    }
    const std::array<glm::vec3, 3> steering{
        counts[SeparationIndex] == 0 ? glm::vec3{0.0F}
            : glm::vec3{separationSum / static_cast<double>(counts[SeparationIndex])},
        counts[AlignmentIndex] == 0 ? glm::vec3{0.0F}
            : velocitySum / static_cast<float>(counts[AlignmentIndex]) - self.velocity,
        counts[CohesionIndex] == 0 ? glm::vec3{0.0F}
            : displacementSum / static_cast<float>(counts[CohesionIndex]),
    };
    glm::dvec3 result{0.0};
    for (const std::size_t kind : order_) {
        if (weights_[kind] == 0.0F) {
            continue;
        }
        if (!std::isfinite(steering[kind].x) || !std::isfinite(steering[kind].y) ||
            !std::isfinite(steering[kind].z)) {
            throw std::invalid_argument("Rule returned a nonfinite acceleration");
        }
        result += glm::dvec3{steering[kind]} * static_cast<double>(weights_[kind]);
    }
    return result;
}

} // namespace emergences::scenarios::boids
