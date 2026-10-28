#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "sim/Agent.h"
#include "sim/Backend.h"
#include "sim/ComputeDevice.h"

namespace emergences::scenarios::slime_mold {

struct SlimeMoldParameters {
    float speed{8.0F};
    float sensorDistance{2.5F};
    float sensorAngle{0.65F};
    float turnSpeed{12.0F};
    float wander{0.5F};
    float depositRate{8.0F};
    float diffusion{18.0F};
    float evaporation{0.35F};
    float foodAttraction{6.0F};
};

struct SlimeMoldState {
    // Agent::reserved stores heading in radians; velocity is for display.
    std::vector<sim::Agent> agents;
    std::vector<float> trail;
};

class SlimeMoldGpu;

// Physarum-inspired chemotaxis on a periodic XZ plane. Each synchronous step
// senses the old field, moves/deposits, then diffuses/evaporates the new field.
class SlimeMoldScene {
public:
    static constexpr std::size_t MaxAgents = 100000;
    static constexpr std::uint32_t GridSize = 256;
    static constexpr std::size_t CellCount = GridSize * GridSize;
    static constexpr float WorldHalfExtent = 50.0F;
    static constexpr float MaxTrail = 1000.0F;
    static constexpr double FixedStepSeconds = 1.0 / 60.0;
    static constexpr std::size_t MaxStepsPerAdvance = 8;

    explicit SlimeMoldScene(std::size_t count = 30000, std::uint32_t seed = 12345);
    ~SlimeMoldScene();
    // Device must outlive this scene. CPU remains active until setBackend().
    void initializeGpu(sim::ComputeDevice&, std::string shaderPath);
    void setBackend(sim::Backend);
    [[nodiscard]] sim::Backend backend() const noexcept { return backend_; }
    [[nodiscard]] bool gpuAvailable() const noexcept { return gpu_ != nullptr; }
    [[nodiscard]] const SlimeMoldParameters& parameters() const noexcept { return parameters_; }
    void setParameters(const SlimeMoldParameters&);
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] std::uint32_t seed() const noexcept { return seed_; }
    [[nodiscard]] std::size_t foodCount() const noexcept { return foodCount_; }
    // Population/food changes regenerate the seeded world and restart time.
    void setCount(std::size_t);
    void setSeed(std::uint32_t);
    void setFoodCount(std::size_t);
    void reset();
    void setPaused(bool) noexcept;
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    bool singleStep();
    std::size_t advance(double seconds,
        double workBudgetSeconds = std::numeric_limits<double>::infinity());
    [[nodiscard]] bool lastAdvanceLimited() const noexcept { return limited_; }
    [[nodiscard]] std::uint64_t totalSteps() const noexcept { return steps_; }
    [[nodiscard]] double elapsedSimulationSeconds() const noexcept { return steps_ * FixedStepSeconds; }

    // GPU borrowed views are empty; only explicit snapshots download state.
    [[nodiscard]] std::span<const sim::Agent> agents() const noexcept;
    [[nodiscard]] std::span<const float> trail() const noexcept;
    [[nodiscard]] std::span<const float> nutrients() const noexcept { return nutrients_; }
    [[nodiscard]] std::span<const glm::vec3> foodSources() const noexcept { return food_; }
    [[nodiscard]] SlimeMoldState readback() const;
    // Validated, alias-safe import; resets time, preserves controls/backend/food.
    void setState(const SlimeMoldState&);

    // Rebuild display only when state/options change. GPU returns an empty view
    // and exposes renderBuffer(); CPU returns render records with Agent's ABI.
    std::span<const sim::Agent> refreshDisplay(float exposure, bool showAgents);
    [[nodiscard]] sim::BufferHandle renderBuffer() const noexcept;
    [[nodiscard]] std::size_t renderCount(bool showAgents) const noexcept {
        return CellCount + (showAgents ? count_ : 0);
    }

private:
    void regenerate(std::size_t, std::uint32_t, std::size_t);
    void step();
    SlimeMoldParameters parameters_;
    SlimeMoldState state_;
    std::vector<float> nextTrail_;
    std::vector<std::uint32_t> deposits_;
    std::vector<float> nutrients_;
    std::vector<glm::vec3> food_;
    std::vector<sim::Agent> display_;
    std::unique_ptr<SlimeMoldGpu> gpu_;
    sim::Backend backend_{sim::Backend::CPU};
    std::size_t count_{};
    std::uint32_t seed_{};
    std::size_t foodCount_{5};
    std::uint64_t steps_{};
    double accumulator_{};
    bool paused_{};
    bool limited_{};
    bool displayDirty_{true};
    bool showAgents_{};
    float exposure_{-1.0F};
};

} // namespace emergences::scenarios::slime_mold
