#pragma once

#include <array>
#include "scenarios/slime_mold/SlimeMoldScene.h"

namespace emergences::scenarios::slime_mold {

// Backend-neutral GPU owner. No Vulkan headers or CPU simulation calls.
class SlimeMoldGpu final {
public:
    SlimeMoldGpu(sim::ComputeDevice&, const std::string& shaderPath);
    ~SlimeMoldGpu();
    SlimeMoldGpu(const SlimeMoldGpu&) = delete;
    SlimeMoldGpu& operator=(const SlimeMoldGpu&) = delete;
    // Allocate/upload replacements before releasing live resources.
    void setState(const SlimeMoldState&, std::span<const float> nutrients);
    void step(const SlimeMoldParameters&, std::uint32_t seed, std::uint32_t tick);
    void refreshDisplay(float exposure, bool showAgents);
    [[nodiscard]] SlimeMoldState readback();
    [[nodiscard]] sim::BufferHandle renderBuffer() const noexcept { return buffers_[6]; }

private:
    struct Constants;
    sim::ComputeDispatch dispatch(Constants, std::uint32_t stage, std::size_t items) const;
    sim::ComputeDevice& device_;
    sim::PipelineHandle pipeline_{};
    // Two agent states, two trail fields, integer deposits, nutrients, display.
    std::array<sim::BufferHandle, 7> buffers_{};
    std::size_t count_{};
    unsigned current_{};
};
} // namespace emergences::scenarios::slime_mold
