#pragma once

#include <string_view>
#include <vector>

#include "sim/Agent.h"
#include "sim/World.h"

namespace emergences::sim {

// Optional CPU-only operation, applied in insertion order after rule integration.
// The CPU backend validates the final world/records, reapplies speed limits and
// boundaries, then commits. Interventions may change count as well as records.
// References passed to apply are borrowed and must not be retained beyond the call.
class Intervention {
public:
    virtual ~Intervention() = default;

    virtual void apply(float dt, std::vector<Agent>& agents, World& world) = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace emergences::sim
