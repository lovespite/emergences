#pragma once

#include <string_view>
#include <vector>

#include "sim/Agent.h"
#include "sim/World.h"

namespace emergences::sim {

// Optional CPU-only operation, applied after rules. References passed to apply
// are borrowed and must not be retained beyond the call.
class Intervention {
public:
    virtual ~Intervention() = default;

    virtual void apply(float dt, std::vector<Agent>& agents, World& world) = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace emergences::sim
