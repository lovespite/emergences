#include "scenarios/boids/BoidsScene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>

#include <glm/geometric.hpp>

#include "scenarios/boids/Alignment.h"
#include "scenarios/boids/BoidsGpu.h"
#include "scenarios/boids/BoidsRuleKernel.h"
#include "scenarios/boids/Cohesion.h"
#include "scenarios/boids/Separation.h"

namespace emergences::scenarios::boids {
namespace {

glm::vec3 intensityColor(double intensity) {
    constexpr glm::vec3 low{0.10F, 0.35F, 0.95F};
    constexpr glm::vec3 high{1.0F, 0.25F, 0.05F};
    const float fraction = static_cast<float>(std::clamp(intensity, 0.0, 1.0));
    return low * (1.0F - fraction) + high * fraction;
}

void validateColorMode(ColorMode mode) {
    switch (mode) {
    case ColorMode::Direction:
    case ColorMode::Speed:
    case ColorMode::Density:
    case ColorMode::Solid:
        return;
    }
    throw std::invalid_argument("Unknown boids color mode");
}

} // namespace

BoidsScene::BoidsScene(std::size_t count, std::uint32_t seed)
    : seed_{seed}, simulation_{count, sim::World{glm::vec3{-40.0F}, glm::vec3{40.0F}}, seed} {
    simulation_.rules().push_back(std::make_unique<Separation>());
    simulation_.rules().push_back(std::make_unique<Alignment>());
    simulation_.rules().push_back(std::make_unique<Cohesion>());
    simulation_.setRuleKernel(std::make_unique<BoidsRuleKernel>());
    refreshColors(colorMode_);
}

BoidsScene::~BoidsScene() = default;

sim::cpu::CpuSimulation& BoidsScene::simulation() noexcept { return simulation_; }
const sim::cpu::CpuSimulation& BoidsScene::simulation() const noexcept { return simulation_; }
sim::Simulation& BoidsScene::currentSimulation() noexcept {
    return backend_ == sim::Backend::GPU ? static_cast<sim::Simulation&>(*gpuSimulation_)
                                         : simulation_;
}
const sim::Simulation& BoidsScene::currentSimulation() const noexcept {
    return backend_ == sim::Backend::GPU ? static_cast<const sim::Simulation&>(*gpuSimulation_)
                                         : simulation_;
}
sim::Backend BoidsScene::backend() const noexcept { return backend_; }
sim::gpu::GpuSimulation* BoidsScene::gpuSimulation() noexcept { return gpuSimulation_.get(); }
const sim::gpu::GpuSimulation* BoidsScene::gpuSimulation() const noexcept {
    return gpuSimulation_.get();
}

void BoidsScene::initializeGpu(sim::ComputeDevice& device, std::string shaderPath) {
    if (backend_ == sim::Backend::GPU) {
        throw std::logic_error("Switch to CPU before replacing the GPU simulation");
    }
    attachGpu(makeBoidsGpu(device, std::move(shaderPath), 0, simulation_.world(), seed_));
}

void BoidsScene::attachGpu(std::unique_ptr<sim::gpu::GpuSimulation> simulation) {
    if (!simulation) {
        throw std::invalid_argument("Cannot attach a null GPU simulation");
    }
    if (backend_ == sim::Backend::GPU) {
        throw std::logic_error("Switch to CPU before replacing the GPU simulation");
    }
    gpuSimulation_ = std::move(simulation);
}

void BoidsScene::setBackend(sim::Backend backend) {
    if (backend != sim::Backend::CPU && backend != sim::Backend::GPU) {
        throw std::invalid_argument("Unknown simulation backend");
    }
    if (backend == backend_) {
        return;
    }
    if (!gpuSimulation_) {
        throw std::logic_error("No GPU simulation is attached");
    }
    auto& source = currentSimulation();
    sim::Simulation& destination = backend == sim::Backend::GPU
        ? static_cast<sim::Simulation&>(*gpuSimulation_) : simulation_;
    if (backend == sim::Backend::GPU) {
        // Validate before changing either backend's settings or rule ownership.
        (void)encodeBoidsGpuRules(source.rules());
        if (!destination.interventions().empty()) {
            throw std::invalid_argument("GPU simulations do not support interventions");
        }
    }
    const auto agents = source.readback();
    destination.setWorld(source.world());
    destination.setMotionLimits(source.motionLimits());
    destination.setAgents(agents);
    destination.rules().swap(source.rules());
    // Everything after the transfer is nonthrowing. Source remains authoritative
    // if a device allocation, upload, or readback fails above.
    backend_ = backend;
    accumulator_ = 0.0;
    lastAdvanceLimited_ = false;
    displayAgents_.clear();
}

std::size_t BoidsScene::advance(double elapsedSeconds, double workBudgetSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) {
        throw std::invalid_argument("Scene elapsed time must be finite and nonnegative");
    }
    if (std::isnan(workBudgetSeconds) || workBudgetSeconds < 0.0) {
        throw std::invalid_argument("Scene work budget must be nonnegative or positive infinity");
    }
    lastAdvanceLimited_ = false;
    if (paused_) {
        return 0;
    }

    constexpr double maximumElapsed = FixedStepSeconds * MaxStepsPerAdvance;
    lastAdvanceLimited_ = elapsedSeconds > maximumElapsed;
    accumulator_ += std::min(elapsedSeconds, maximumElapsed);
    const bool budgeted = std::isfinite(workBudgetSeconds);
    const auto started = budgeted ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point{};
    std::size_t steps = 0;
    // Compensate only for rounding near a step boundary, so equivalent frame
    // partitions such as 60 Hz and 144 Hz produce the same fixed-step count.
    constexpr double roundingTolerance = FixedStepSeconds * 1.0e-12;
    while (steps < MaxStepsPerAdvance && accumulator_ + roundingTolerance >= FixedStepSeconds) {
        currentSimulation().step(static_cast<float>(FixedStepSeconds));
        accumulator_ = std::max(0.0, accumulator_ - FixedStepSeconds);
        ++totalSteps_;
        ++steps;
        if (budgeted && std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count() >= workBudgetSeconds) {
            if (accumulator_ + roundingTolerance >= FixedStepSeconds) {
                const double remainingSteps = std::floor(
                    (accumulator_ + roundingTolerance) / FixedStepSeconds);
                accumulator_ = std::max(0.0, accumulator_ - remainingSteps * FixedStepSeconds);
                lastAdvanceLimited_ = true;
            }
            break;
        }
    }
    return steps;
}

bool BoidsScene::lastAdvanceLimited() const noexcept { return lastAdvanceLimited_; }

void BoidsScene::setPaused(bool paused) noexcept {
    lastAdvanceLimited_ = false;
    if (paused_ != paused) {
        accumulator_ = 0.0;
        paused_ = paused;
    }
}

bool BoidsScene::paused() const noexcept { return paused_; }

void BoidsScene::reset() {
    currentSimulation().reset();
    accumulator_ = 0.0;
    totalSteps_ = 0;
    lastAdvanceLimited_ = false;
    refreshColors(colorMode_);
}

bool BoidsScene::singleStep() {
    lastAdvanceLimited_ = false;
    if (!paused_) {
        return false;
    }
    currentSimulation().step(static_cast<float>(FixedStepSeconds));
    ++totalSteps_;
    return true;
}

double BoidsScene::elapsedSimulationSeconds() const noexcept {
    return static_cast<double>(totalSteps_) * FixedStepSeconds;
}

std::uint64_t BoidsScene::totalSteps() const noexcept { return totalSteps_; }

std::span<const sim::Agent> BoidsScene::refreshColors(ColorMode mode) {
    validateColorMode(mode);
    if (backend_ == sim::Backend::GPU) {
        gpuSimulation_->refreshColors(static_cast<sim::gpu::ColorMode>(mode), DensityRadius,
            static_cast<float>(DensitySaturationNeighbors));
        colorMode_ = mode;
        displayAgents_.clear();
        return {};
    }
    const auto agents = simulation_.agents();
    displayAgents_.assign(agents.begin(), agents.end());
    if (mode == ColorMode::Density) {
        densityGrid_.rebuild(agents, simulation_.world(), DensityRadius);
    }
    const double maximumSpeed = simulation_.motionLimits().maximumSpeed;
    for (std::size_t index = 0; index < displayAgents_.size(); ++index) {
        auto& agent = displayAgents_[index];
        switch (mode) {
        case ColorMode::Direction: {
            const glm::dvec3 velocity{agent.velocity};
            const double speed = glm::length(velocity);
            agent.color = speed > 0.0 ? glm::vec3{velocity / speed * 0.5 + 0.5}
                                      : glm::vec3{0.5F};
            break;
        }
        case ColorMode::Speed:
            agent.color = intensityColor(maximumSpeed > 0.0
                ? glm::length(glm::dvec3{agent.velocity}) / maximumSpeed : 0.0);
            break;
        case ColorMode::Density:
            densityGrid_.query(index, DensityRadius, densityNeighbors_);
            agent.color = intensityColor(static_cast<double>(densityNeighbors_.size()) /
                                        static_cast<double>(DensitySaturationNeighbors));
            break;
        case ColorMode::Solid:
            agent.color = {0.20F, 0.75F, 1.0F};
            break;
        }
    }
    colorMode_ = mode;
    return displayAgents_;
}

std::span<const sim::Agent> BoidsScene::displayAgents() const noexcept { return displayAgents_; }

} // namespace emergences::scenarios::boids
