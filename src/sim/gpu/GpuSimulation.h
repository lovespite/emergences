#pragma once

#include <functional>
#include <string>

#include "sim/ComputeDevice.h"
#include "sim/Simulation.h"

namespace emergences::sim::gpu {

// Shader-specific bytes are supplied by the scenario, keeping sim independent
// of rule implementations. The encoder validates supported types and ordering.
struct EncodedRules {
    std::vector<std::byte> data;
    float maximumRadius{};
};
using RuleEncoder = std::function<EncodedRules(std::span<const std::unique_ptr<Rule>>)>;
enum class ColorMode : std::uint32_t { Direction, Speed, Density, Solid };

// Owns GPU resources through a backend-neutral device. Device must outlive this
// object. Normal stepping, color refresh and rendering never read state to CPU.
// Explicit configuration changes may synchronize/read back to preserve state.
class GpuSimulation final : public Simulation {
public:
    GpuSimulation(ComputeDevice& device, std::string shaderPath, RuleEncoder encoder,
        std::size_t count = 0, World world = {}, std::uint32_t seed = 0xC0FFEEU);
    ~GpuSimulation() override;
    GpuSimulation(const GpuSimulation&) = delete;
    GpuSimulation& operator=(const GpuSimulation&) = delete;

    void step(float dt) override;
    void reset() override;
    [[nodiscard]] std::size_t count() const noexcept override { return count_; }
    void setCount(std::size_t count) override;
    [[nodiscard]] Backend backend() const noexcept override { return Backend::GPU; }
    [[nodiscard]] std::span<const Agent> agents() const noexcept override { return {}; }
    [[nodiscard]] std::vector<Agent> readback() override;
    [[nodiscard]] const World& world() const noexcept override { return world_; }
    void setWorld(const World&) override;
    [[nodiscard]] MotionLimits motionLimits() const noexcept override { return limits_; }
    void setMotionLimits(const MotionLimits&) override;
    void setAgents(std::span<const Agent>) override;
    [[nodiscard]] std::vector<std::unique_ptr<Rule>>& rules() noexcept override { return rules_; }
    [[nodiscard]] std::vector<std::unique_ptr<Intervention>>& interventions() noexcept override {
        return interventions_;
    }

    [[nodiscard]] BufferHandle stateBuffer() const noexcept { return state_[current_]; }
    [[nodiscard]] BufferHandle renderBuffer() const noexcept { return display_; }
    // Call after state changes. Display storage is a GPU copy with independent
    // colors; density includes every neighbor within radius (no occupancy cap).
    BufferHandle refreshColors(ColorMode mode, float densityRadius = 5.0F,
        float densitySaturation = 32.0F);
    [[nodiscard]] std::vector<Agent> readbackRender();

private:
    struct Constants;
    void cleanup() noexcept;
    void replaceState(std::span<const Agent>);
    Constants constants(float dt, float radius) const;
    ComputeDispatch dispatch(const Constants&, std::uint32_t stage, std::uint32_t groups) const;
    void appendGrid(std::vector<ComputeDispatch>&, const Constants&) const;
    void uploadRules(const EncodedRules&);

    ComputeDevice& device_;
    RuleEncoder encoder_;
    World world_;
    MotionLimits limits_;
    std::uint32_t seed_;
    std::size_t count_{};
    PipelineHandle pipeline_{};
    BufferHandle state_[2]{};
    unsigned current_{};
    BufferHandle display_{}, cellCounts_{}, cellOffsets_{}, cellCursors_{}, indices_{}, blockSums_{};
    BufferHandle parameters_{};
    std::size_t parameterCapacity_{};
    std::vector<std::byte> uploadedRules_;
    std::vector<std::unique_ptr<Rule>> rules_;
    std::vector<std::unique_ptr<Intervention>> interventions_;
};

} // namespace emergences::sim::gpu
