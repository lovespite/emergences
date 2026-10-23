#include "scenarios/boids/BoidsGpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "scenarios/boids/Alignment.h"
#include "scenarios/boids/Cohesion.h"
#include "scenarios/boids/Separation.h"

namespace emergences::scenarios::boids {

sim::gpu::EncodedRules encodeBoidsGpuRules(std::span<const std::unique_ptr<sim::Rule>> rules) {
    struct Record { std::uint32_t kind; float weight; float radius; std::uint32_t padding{}; };
    struct Parameters { std::array<std::uint32_t, 4> header{}; std::array<Record, 3> rules{}; };
    static_assert(sizeof(Parameters) == 64);
    if (rules.size() > 3) throw std::invalid_argument("GPU Boids supports at most three distinct rules");
    Parameters parameters;
    parameters.header[0] = static_cast<std::uint32_t>(rules.size());
    std::array<bool, 3> seen{};
    sim::gpu::EncodedRules result;
    for (std::size_t index = 0; index < rules.size(); ++index) {
        const auto* rule = rules[index].get();
        std::uint32_t kind;
        if (dynamic_cast<const Separation*>(rule)) kind = 0;
        else if (dynamic_cast<const Alignment*>(rule)) kind = 1;
        else if (dynamic_cast<const Cohesion*>(rule)) kind = 2;
        else throw std::invalid_argument("GPU Boids does not support this rule type");
        if (seen[kind]) throw std::invalid_argument("GPU Boids does not support duplicate rule types");
        seen[kind] = true;
        if (!std::isfinite(rule->weight()) || rule->weight() < 0.0F ||
            !std::isfinite(rule->radius()) || rule->radius() < 0.0F) {
            throw std::invalid_argument("GPU rules require finite nonnegative parameters");
        }
        parameters.rules[index] = {kind, rule->weight(), rule->radius(), 0};
        if (rule->weight() > 0.0F) result.maximumRadius = std::max(result.maximumRadius, rule->radius());
    }
    result.data.resize(sizeof(parameters));
    std::memcpy(result.data.data(), &parameters, sizeof(parameters));
    return result;
}

std::unique_ptr<sim::gpu::GpuSimulation> makeBoidsGpu(sim::ComputeDevice& device,
    std::string shaderPath, std::size_t count, sim::World world, std::uint32_t seed) {
    auto simulation = std::make_unique<sim::gpu::GpuSimulation>(device,
        std::move(shaderPath), encodeBoidsGpuRules, count, world, seed);
    simulation->rules().push_back(std::make_unique<Separation>());
    simulation->rules().push_back(std::make_unique<Alignment>());
    simulation->rules().push_back(std::make_unique<Cohesion>());
    return simulation;
}

} // namespace emergences::scenarios::boids
