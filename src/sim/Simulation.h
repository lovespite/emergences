#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "sim/Agent.h"
#include "sim/Backend.h"
#include "sim/Intervention.h"
#include "sim/Rule.h"

namespace emergences::sim {

class Simulation {
public:
    virtual ~Simulation() = default;

    // dt is a finite, nonnegative number of seconds.
    virtual void step(float dt) = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual std::size_t count() const noexcept = 0;
    virtual void setCount(std::size_t count) = 0;
    [[nodiscard]] virtual Backend backend() const noexcept = 0;

    // CPU: borrowed view with count() records, invalidated by the next mutation.
    // GPU: empty view; this accessor must not trigger implicit GPU readback.
    // The zero-copy GPU resource / synchronization bridge is deferred to Phase 4.
    [[nodiscard]] virtual std::span<const Agent> agents() const noexcept = 0;

    // The simulation owns each inserted object. Mutate these collections only
    // between steps; null entries are not allowed. Rules expose their own UI
    // parameters without transferring ownership to the caller.
    [[nodiscard]] virtual std::vector<std::unique_ptr<Rule>>& rules() noexcept = 0;
    [[nodiscard]] virtual std::vector<std::unique_ptr<Intervention>>&
        interventions() noexcept = 0;

    // CPU backends apply interventions. A future GPU backend must reject a
    // nonempty intervention list instead of silently ignoring it.
};

} // namespace emergences::sim
