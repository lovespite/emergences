#pragma once

#include <array>
#include <cstddef>

#include "sim/cpu/RuleKernel.h"

namespace emergences::scenarios::boids {

// Fuses one Separation, Alignment and Cohesion rule in any order. Other rule
// collections use the CPU backend's generic path. prepare snapshots parameters;
// acceleration only reads that snapshot and can be called by multiple workers.
class BoidsRuleKernel final : public sim::cpu::RuleKernel {
public:
    bool prepare(std::span<const std::unique_ptr<sim::Rule>> rules) override;
    [[nodiscard]] glm::dvec3 acceleration(const sim::Agent& self,
        std::span<const sim::Neighbor> neighbors, const sim::World& world) const override;

private:
    static constexpr std::size_t SeparationIndex = 0;
    static constexpr std::size_t AlignmentIndex = 1;
    static constexpr std::size_t CohesionIndex = 2;

    std::array<float, 3> weights_{};
    std::array<float, 3> radii_{};
    std::array<double, 3> radiusSquared_{};
    std::array<std::size_t, 3> order_{};
    bool prepared_{false};
};

} // namespace emergences::scenarios::boids
