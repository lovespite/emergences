#pragma once

#include <cstdint>
#include <exception>

#include "sim/Simulation.h"
#include "sim/cpu/RuleKernel.h"
#include "sim/cpu/SpatialHash.h"

namespace emergences::sim::cpu {

using MotionLimits = sim::MotionLimits;

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
    [[nodiscard]] const std::vector<std::unique_ptr<Rule>>& rules() const noexcept override;
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
    [[nodiscard]] std::vector<Agent> readback() override { return agents_; }

    // Configuration and rule changes are only allowed between steps. A kernel
    // is optional; nullptr or prepare(false) keeps arbitrary rules serial.
    void setRuleKernel(std::unique_ptr<RuleKernel> kernel);
    // 0: automatic (small flocks stay serial, at most 16 threads); 1: serial.
    // Larger requests are capped by the OpenMP runtime and agent count. Builds
    // without OpenMP always use one thread, but still use a supported kernel.
    void setWorkerCount(std::size_t count) noexcept;
    [[nodiscard]] std::size_t workerCount() const noexcept;

private:
    // Keep frequently updated vector metadata off other workers' cache lines.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // Padding to the next cache line is intentional.
#endif
    struct alignas(64) WorkerScratch {
        std::vector<Neighbor> neighbors;
        std::vector<Neighbor> ruleNeighbors;
        std::exception_ptr failure;
        std::size_t failureIndex{0};
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    [[nodiscard]] std::size_t workersForStep(bool useKernel) const noexcept;
    [[nodiscard]] Agent makeAgent(std::size_t index) const;
    void normalize(std::vector<Agent>& agents, const World& world,
                   const MotionLimits& limits) const;

    World world_;
    MotionLimits limits_;
    std::uint32_t seed_;
    std::vector<Agent> agents_;
    std::vector<Agent> next_;
    SpatialHash grid_;
    std::unique_ptr<RuleKernel> ruleKernel_;
    std::size_t workerCount_{0};
    std::vector<WorkerScratch> workerScratch_;
    std::vector<std::unique_ptr<Rule>> rules_;
    std::vector<std::unique_ptr<Intervention>> interventions_;
};

} // namespace emergences::sim::cpu
