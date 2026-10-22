#pragma once

#include <cstdint>

#include "sim/Simulation.h"
#include "sim/cpu/SpatialHash.h"

namespace emergences::sim::cpu {

struct MotionLimits {
    float minimumSpeed{1.0F};
    float maximumSpeed{8.0F};
    float maximumAcceleration{20.0F};
};

// All rules read the same old state. Weighted accelerations are summed, limited,
// then integrated with semi-implicit Euler. Interventions run in insertion order
// after integration; their resulting world/state is validated before committing.
// A failed step leaves the visible agents/world unchanged (callback side effects
// outside those records cannot be rolled back). Use a fixed dt for reproducibility.
class CpuSimulation final : public Simulation {
public:
    explicit CpuSimulation(std::size_t count = 0, World world = {},
                           std::uint32_t seed = 0xC0FFEEU);

    void step(float dt) override;
    void reset() override;
    [[nodiscard]] std::size_t count() const noexcept override;
    void setCount(std::size_t count) override;
    [[nodiscard]] Backend backend() const noexcept override;
    [[nodiscard]] std::span<const Agent> agents() const noexcept override;
    [[nodiscard]] std::vector<std::unique_ptr<Rule>>& rules() noexcept override;
    [[nodiscard]] std::vector<std::unique_ptr<Intervention>>& interventions() noexcept override;

    [[nodiscard]] const World& world() const noexcept;
    void setWorld(const World& world);
    [[nodiscard]] MotionLimits motionLimits() const noexcept;
    void setMotionLimits(const MotionLimits& limits);

    // Replaces state, preserving supplied IDs. IDs are metadata, not array keys;
    // callers needing unique IDs must also account for newly generated indices.
    // Imported records and intervention results must be finite with size >= 0.
    // Positions and speeds are normalized to the configured world and limits.
    void setAgents(std::span<const Agent> agents);

private:
    [[nodiscard]] Agent makeAgent(std::size_t index) const;
    void normalize(std::vector<Agent>& agents, const World& world,
                   const MotionLimits& limits) const;

    World world_;
    MotionLimits limits_;
    std::uint32_t seed_;
    std::vector<Agent> agents_;
    std::vector<Agent> next_;
    SpatialHash grid_;
    std::vector<Neighbor> neighbors_;
    std::vector<Neighbor> ruleNeighbors_;
    std::vector<std::unique_ptr<Rule>> rules_;
    std::vector<std::unique_ptr<Intervention>> interventions_;
};

} // namespace emergences::sim::cpu
