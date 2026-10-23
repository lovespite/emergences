#pragma once

#include "sim/gpu/GpuSimulation.h"

namespace emergences::scenarios::boids {

// Accepts zero to three distinct built-in rules in any order; preserves their
// original weighted-sum order. Unknown and duplicate rules are rejected.
[[nodiscard]] sim::gpu::EncodedRules encodeBoidsGpuRules(
    std::span<const std::unique_ptr<sim::Rule>> rules);
[[nodiscard]] std::unique_ptr<sim::gpu::GpuSimulation> makeBoidsGpu(
    sim::ComputeDevice& device, std::string shaderPath, std::size_t count = 0,
    sim::World world = {}, std::uint32_t seed = 0xC0FFEEU);

} // namespace emergences::scenarios::boids
