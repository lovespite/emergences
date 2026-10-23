#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "sim/cpu/CpuSimulation.h"

namespace emergences::sim {
class ComputeDevice;
namespace gpu {
class GpuSimulation;
}
}

namespace emergences::scenarios::boids {

enum class ColorMode {
    Direction,
    Speed,
    Density,
    Solid,
};

// Scene state shared by the interactive application and headless tests.
// Rendering colors are copies: selecting a color mode never changes simulation
// records or the trajectories produced by the same seed and fixed time steps.
class BoidsScene {
public:
    static constexpr double FixedStepSeconds = 1.0 / 120.0;
    static constexpr std::size_t MaxStepsPerAdvance = 8;
    static constexpr float DensityRadius = 5.0F;
    static constexpr std::size_t DensitySaturationNeighbors = 32;

    explicit BoidsScene(std::size_t count = 2048, std::uint32_t seed = 0xC0FFEEU);
    ~BoidsScene();

    // CPU storage remains available for CPU-only interventions and controls.
    // Use currentSimulation() for parameters and state of the active backend.
    [[nodiscard]] sim::cpu::CpuSimulation& simulation() noexcept;
    [[nodiscard]] const sim::cpu::CpuSimulation& simulation() const noexcept;
    [[nodiscard]] sim::Simulation& currentSimulation() noexcept;
    [[nodiscard]] const sim::Simulation& currentSimulation() const noexcept;
    [[nodiscard]] sim::Backend backend() const noexcept;
    [[nodiscard]] sim::gpu::GpuSimulation* gpuSimulation() noexcept;
    [[nodiscard]] const sim::gpu::GpuSimulation* gpuSimulation() const noexcept;

    // Creates GPU storage using this scene's seed for identical reset behavior.
    void initializeGpu(sim::ComputeDevice& device, std::string shaderPath);
    // Attach only while CPU is active. The device must outlive the scene and the
    // GPU must use the same seed as this scene so reset remains reproducible.
    void attachGpu(std::unique_ptr<sim::gpu::GpuSimulation> simulation);
    // Copies current state/settings and transfers rule ownership explicitly.
    // CPU interventions stay on CPU. Successful changes retain pause/clock,
    // discard fractional time, and invalidate the display copy until refresh.
    // A failed transfer leaves the active backend and its state unchanged.
    void setBackend(sim::Backend backend);

    // Rejects nonfinite or negative elapsed time, including while paused.
    // Accumulates fractional steps but limits each call to eight steps; elapsed
    // time beyond that budget is discarded so a stalled frame cannot catch up
    // indefinitely. An optional wall-clock work budget is checked after each
    // complete step; at least one due step is always allowed. If it expires,
    // remaining whole steps are discarded while fractional time is retained.
    // Budgets must be nonnegative and may be positive infinity (the default).
    // Returns the number of simulation steps performed.
    std::size_t advance(double elapsedSeconds,
                        double workBudgetSeconds = std::numeric_limits<double>::infinity());
    // True when the latest advance discarded time through either limit. Valid
    // advances, pause changes, single-step calls, and resets clear stale status.
    [[nodiscard]] bool lastAdvanceLimited() const noexcept;
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;

    // Pause transitions discard fractional frame time. Reset keeps the current
    // pause state, count, world, motion limits, rules, and interventions.
    void reset();
    bool singleStep();
    [[nodiscard]] double elapsedSimulationSeconds() const noexcept;
    [[nodiscard]] std::uint64_t totalSteps() const noexcept;

    // Direction maps normalized XYZ velocity to RGB; speed maps [0, max speed]
    // from blue to orange. Density uses the same fixed blue-to-orange scale for
    // 0..32 neighbors within radius 5, honoring the world's boundary mode.
    // Call after changing simulation state. The returned view is invalidated by
    // the next refresh/reset; displayAgents() exposes the last refreshed copy.
    std::span<const sim::Agent> refreshColors(ColorMode mode);
    [[nodiscard]] std::span<const sim::Agent> displayAgents() const noexcept;

private:
    std::uint32_t seed_;
    sim::cpu::CpuSimulation simulation_;
    std::unique_ptr<sim::gpu::GpuSimulation> gpuSimulation_;
    sim::Backend backend_{sim::Backend::CPU};
    sim::cpu::SpatialHash densityGrid_;
    std::vector<sim::Neighbor> densityNeighbors_;
    std::vector<sim::Agent> displayAgents_;
    double accumulator_{0.0};
    std::uint64_t totalSteps_{0};
    bool paused_{false};
    bool lastAdvanceLimited_{false};
    ColorMode colorMode_{ColorMode::Direction};
};

} // namespace emergences::scenarios::boids
