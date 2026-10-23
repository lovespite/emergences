#pragma once

#include <memory>
#include <span>

#include <glm/vec3.hpp>

#include "sim/Rule.h"

namespace emergences::sim::cpu {

// Optional scenario-specific evaluation of the complete rule set in one pass.
// prepare runs once on the calling thread after rule parameter validation. It
// must snapshot everything acceleration needs and return false for unsupported
// rule sets; the backend then uses the original serial Rule::steer path.
// After a successful prepare, acceleration may run concurrently for different
// agents. It must be read-only, thread-safe and independent of invocation order.
class RuleKernel {
public:
    virtual ~RuleKernel() = default;

    [[nodiscard]] virtual bool prepare(std::span<const std::unique_ptr<Rule>> rules) = 0;

    // Receives the maximum-radius neighborhood in its original query order.
    // Apply each rule's own radius and weight, preserve its accumulator precision
    // and float steering result, then sum weights in the original rule order.
    // Validate each enabled rule's steering before summation, as the serial path
    // does. The returned acceleration is already weighted.
    [[nodiscard]] virtual glm::dvec3 acceleration(
        const Agent& self, std::span<const Neighbor> neighbors, const World& world) const = 0;
};

} // namespace emergences::sim::cpu
