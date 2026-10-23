#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "sim/Agent.h"
#include "sim/Backend.h"
#include "sim/Intervention.h"
#include "sim/MotionLimits.h"
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
    [[nodiscard]] virtual const World& world() const noexcept = 0;
    virtual void setWorld(const World&) = 0;
    [[nodiscard]] virtual MotionLimits motionLimits() const noexcept = 0;
    virtual void setMotionLimits(const MotionLimits&) = 0;
    virtual void setAgents(std::span<const Agent>) = 0;
    // Explicit snapshot; unlike agents(), GPU backends synchronize and read back.
    [[nodiscard]] virtual std::vector<Agent> readback() = 0;

    // CPU: borrowed view with count() records, invalidated by the next mutation.
    // GPU: empty view; this accessor must not trigger implicit GPU readback.
    // GPU storage is exposed separately by gpu::GpuSimulation via ComputeDevice.
    [[nodiscard]] virtual std::span<const Agent> agents() const noexcept = 0;

    // The simulation owns each inserted object. Mutate these collections only
    // between steps; null entries are not allowed. Rules expose their own UI
    // parameters without transferring ownership to the caller.
    [[nodiscard]] virtual std::vector<std::unique_ptr<Rule>>& rules() noexcept = 0;
    [[nodiscard]] virtual const std::vector<std::unique_ptr<Rule>>& rules() const noexcept = 0;
    [[nodiscard]] virtual std::vector<std::unique_ptr<Intervention>>&
        interventions() noexcept = 0;

    // CPU backends apply interventions. GPU backends must reject a
    // nonempty intervention list instead of silently ignoring it.
};

} // namespace emergences::sim
