#include "core/ComputeDevice.h"
#include "core/Context.h"
#include "core/Window.h"
#include "scenarios/boids/Alignment.h"
#include "scenarios/boids/BoidsGpu.h"
#include "scenarios/boids/Cohesion.h"
#include "scenarios/boids/Separation.h"
#include "sim/cpu/CpuSimulation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include <glm/geometric.hpp>

namespace {
using namespace emergences;
using sim::Agent;
using sim::cpu::CpuSimulation;
namespace boids = scenarios::boids;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void reject(Function&& function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Expected invalid operation to be rejected");
}
bool identical(std::span<const Agent> left, std::span<const Agent> right) {
    return left.size() == right.size() && (left.empty() ||
        std::memcmp(left.data(), right.data(), left.size_bytes()) == 0);
}
void addRules(sim::Simulation& simulation) {
    simulation.rules().clear();
    // Deliberately use a different order than the factory defaults.
    simulation.rules().push_back(std::make_unique<boids::Alignment>(0.45F, 5.0F));
    simulation.rules().push_back(std::make_unique<boids::Cohesion>(0.11F, 6.0F));
    simulation.rules().push_back(std::make_unique<boids::Separation>(2.0F, 2.0F));
}

// Instruments the real Vulkan device: tests can prove stepping and coloring do
// not smuggle state through a host snapshot, without replacing GPU execution.
class ObservedDevice final : public sim::ComputeDevice {
public:
    explicit ObservedDevice(const core::Context& context) : actual{context} {}
    sim::BufferHandle createBuffer(std::size_t bytes) override { return actual.createBuffer(bytes); }
    void destroyBuffer(sim::BufferHandle handle) noexcept override { actual.destroyBuffer(handle); }
    void upload(sim::BufferHandle handle, std::span<const std::byte> bytes) override {
        ++uploads; actual.upload(handle, bytes);
    }
    void download(sim::BufferHandle handle, std::span<std::byte> bytes) override {
        ++downloads; actual.download(handle, bytes);
    }
    sim::PipelineHandle createPipeline(std::string_view path, std::uint32_t bindings,
        std::uint32_t constants) override { return actual.createPipeline(path, bindings, constants); }
    void destroyPipeline(sim::PipelineHandle handle) noexcept override { actual.destroyPipeline(handle); }
    void execute(std::span<const sim::ComputeDispatch> batch) override { actual.execute(batch); }
    void waitIdle() override { actual.waitIdle(); }
    core::VulkanComputeDevice actual;
    std::size_t uploads{}, downloads{};
};

float maximumPositionError = 0.0F, maximumVelocityError = 0.0F;
void compare(std::span<const Agent> expected, std::span<const Agent> actual,
    const sim::World& world, float tolerance = 0.0005F) {
    require(expected.size() == actual.size(), "GPU result count differs from CPU");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto& left = expected[index];
        const auto& right = actual[index];
        require(left.id == right.id && left.size == right.size && left.color == right.color &&
            left.reserved == right.reserved, "GPU failed to preserve agent metadata");
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            require(std::isfinite(right.position[axis]) && std::isfinite(right.velocity[axis]),
                "GPU produced a nonfinite state");
            require(right.position[axis] >= world.minimum[axis] && right.position[axis] <= world.maximum[axis],
                "GPU produced an out-of-bounds position");
        }
        const float positionError = glm::length(world.displacement(left.position, right.position));
        const float velocityError = glm::length(left.velocity - right.velocity);
        maximumPositionError = std::max(maximumPositionError, positionError);
        maximumVelocityError = std::max(maximumVelocityError, velocityError);
        if (positionError > tolerance || velocityError > tolerance) {
            throw std::runtime_error("CPU/GPU mismatch at agent " + std::to_string(index) +
                ": position=" + std::to_string(positionError) + ", velocity=" + std::to_string(velocityError));
        }
    }
}

void trajectories(ObservedDevice& device, const std::string& shader) {
    for (const auto boundary : {sim::BoundaryMode::Wrap, sim::BoundaryMode::Bounce}) {
        const sim::World world{{-10, -7, -4}, {10, 7, 4}, boundary};
        for (const std::size_t count : {0U, 1U, 7U, 129U, 257U}) {
            CpuSimulation cpu{count, world, 42};
            auto gpu = boids::makeBoidsGpu(device, shader, count, world, 42);
            addRules(cpu); addRules(*gpu);
            cpu.setMotionLimits({0, 8, 20}); gpu->setMotionLimits({0, 8, 20});
            auto initial = cpu.readback();
            for (std::size_t index = 0; index < initial.size(); ++index) {
                initial[index].id = static_cast<std::uint32_t>(1000 + index % 3); // duplicate IDs are legal
                initial[index].color = {0.1F, 0.3F, 0.7F};
                initial[index].reserved = 0.25F;
            }
            if (count > 2) {
                initial[0].position = {-9.95F, 0, 0}; initial[0].velocity = {-3, 0, 0};
                initial[1].position = {9.95F, 0, 0}; initial[1].velocity = {3, 0, 0};
                initial[2].position = initial[0].position; // exact overlap: separation skips it
            }
            cpu.setAgents(initial); gpu->setAgents(initial);
            require(gpu->agents().empty(), "GPU agents() must be empty");
            compare(cpu.agents(), gpu->readback(), world, 0.0F);
            for (unsigned step = 0; step < 12; ++step) {
                if (step == 5) {
                    cpu.rules()[0]->setRadius(0); gpu->rules()[0]->setRadius(0);
                    cpu.rules()[2]->setWeight(0); gpu->rules()[2]->setWeight(0);
                }
                const auto reads = device.downloads;
                cpu.step(1.0F / 120.0F); gpu->step(1.0F / 120.0F);
                require(device.downloads == reads, "GPU stepping performed an implicit state readback");
                compare(cpu.agents(), gpu->readback(), world);
            }
        }
    }
}

void crowdedAndMaximumGrid(ObservedDevice& device, const std::string& shader) {
    // A single occupied cell exceeds a workgroup and any usual fixed bucket cap.
    // The second case spans the entire 32^3-cell / 256-scan-block grid.
    for (const bool crowded : {true, false}) {
        sim::World world{{-100, -100, -100}, {100, 100, 100}};
        CpuSimulation cpu{513, world, 77};
        auto gpu = boids::makeBoidsGpu(device, shader, 0, world, 77);
        cpu.setMotionLimits({0, 8, 20}); gpu->setMotionLimits({0, 8, 20});
        auto state = cpu.readback();
        if (crowded) {
            for (std::size_t index = 0; index < state.size(); ++index) {
                state[index].position = {0.0001F * static_cast<float>(index), 0, 0};
                state[index].velocity = {static_cast<float>(index % 7) - 3.0F, 1, 0};
            }
        } else {
            // Neighbors straddling distant scan-block boundaries plus an exact
            // radius pair establish that nonempty cells in all blocks survive.
            for (std::size_t index = 0; index + 1 < state.size(); index += 2) {
                state[index + 1].position = state[index].position + glm::vec3{0.2F, 0, 0};
                world.applyBoundary(state[index + 1].position, state[index + 1].velocity);
            }
            state[0].position = {-99.5F, -99.5F, -99.5F};
            state[1].position = {99.5F, -99.5F, -99.5F};
        }
        cpu.setAgents(state); gpu->setAgents(state);
        cpu.rules().push_back(std::make_unique<boids::Alignment>(0.8F, crowded ? 5.0F : 1.0F));
        cpu.rules().push_back(std::make_unique<boids::Cohesion>(0.2F, crowded ? 5.0F : 1.0F));
        gpu->rules().clear();
        gpu->rules().push_back(std::make_unique<boids::Alignment>(0.8F, crowded ? 5.0F : 1.0F));
        gpu->rules().push_back(std::make_unique<boids::Cohesion>(0.2F, crowded ? 5.0F : 1.0F));
        cpu.step(0.02F); gpu->step(0.02F);
        compare(cpu.agents(), gpu->readback(), world);
    }
}

void colorsAndConfiguration(ObservedDevice& device, const std::string& shader) {
    auto gpu = boids::makeBoidsGpu(device, shader, 37, {{-4, -5, -8}, {4, 5, 8}}, 9);
    gpu->step(0.01F);
    const auto original = gpu->readback();
    const auto originalHandle = gpu->stateBuffer();
    for (const auto mode : {sim::gpu::ColorMode::Direction, sim::gpu::ColorMode::Speed,
            sim::gpu::ColorMode::Density, sim::gpu::ColorMode::Solid}) {
        for (const float radius : {0.0F, 5.0F, 12.0F}) {
            const auto reads = device.downloads;
            gpu->refreshColors(mode, radius);
            require(reads == device.downloads, "GPU color refresh performed state readback");
            require(gpu->stateBuffer() == originalHandle && gpu->renderBuffer() != originalHandle,
                "GPU coloring did not use independent display storage");
            require(identical(original, gpu->readback()), "Coloring changed GPU simulation state");
            const auto rendered = gpu->readbackRender();
            for (std::size_t index = 0; index < rendered.size(); ++index) {
                const auto& agent = original[index];
                glm::vec3 expected;
                if (mode == sim::gpu::ColorMode::Direction) {
                    const float speed = glm::length(agent.velocity);
                    expected = speed > 0 ? agent.velocity / speed * 0.5F + 0.5F : glm::vec3{0.5F};
                } else if (mode == sim::gpu::ColorMode::Solid) expected = {0.20F, 0.75F, 1.0F};
                else {
                    float intensity = glm::length(agent.velocity) / gpu->motionLimits().maximumSpeed;
                    if (mode == sim::gpu::ColorMode::Density) {
                        std::size_t neighbors = 0;
                        if (radius > 0) for (std::size_t other = 0; other < original.size(); ++other) {
                            const auto delta = gpu->world().displacement(agent.position, original[other].position);
                            if (other != index && glm::dot(delta, delta) <= radius * radius) ++neighbors;
                        }
                        intensity = static_cast<float>(neighbors) / 32.0F;
                    }
                    intensity = std::clamp(intensity, 0.0F, 1.0F);
                    expected = glm::vec3{0.1F, 0.35F, 0.95F} * (1 - intensity) +
                        glm::vec3{1, 0.25F, 0.05F} * intensity;
                }
                require(glm::length(expected - rendered[index].color) < 0.00001F,
                    "GPU display color differs from reference (including periodic deduplication)");
                require(agent.position == rendered[index].position && agent.velocity == rendered[index].velocity &&
                    agent.id == rendered[index].id, "Display pass changed non-color metadata");
            }
        }
    }
    gpu->setCount(43);
    const auto enlarged = gpu->readback();
    compare(original, std::span{enlarged}.first(original.size()), gpu->world());
    gpu->setCount(13);
    require(gpu->count() == 13, "GPU shrink did not set the new count");
    sim::World changed{{-3, -6, -9}, {3, 6, 9}, sim::BoundaryMode::Bounce};
    CpuSimulation cpu{0, gpu->world(), 9}; cpu.setAgents(gpu->readback());
    cpu.setWorld(changed); gpu->setWorld(changed);
    cpu.setMotionLimits({0, 2, 3}); gpu->setMotionLimits({0, 2, 3});
    compare(cpu.agents(), gpu->readback(), changed, 0.000001F);
    cpu.reset(); gpu->reset();
    require(identical(cpu.agents(), gpu->readback()), "GPU reset differs from deterministic CPU reset");
    gpu->setCount(0); gpu->step(0.01F); gpu->refreshColors(sim::gpu::ColorMode::Density);
    require(gpu->readback().empty(), "Empty GPU simulation retained agents");
}

class UnsupportedRule final : public sim::Rule {
public:
    glm::vec3 steer(const Agent&, std::span<const sim::Neighbor>, const sim::World&) const override { return {}; }
    std::string_view name() const noexcept override { return "unsupported"; }
    float weight() const noexcept override { return 1; }
    float radius() const noexcept override { return 1; }
    void setWeight(float) override {}
    void setRadius(float) override {}
};
class UnsupportedIntervention final : public sim::Intervention {
public:
    void apply(float, std::vector<Agent>&, sim::World&) override {}
    std::string_view name() const noexcept override { return "unsupported"; }
};
void invalidAndBoundary(ObservedDevice& device, const std::string& shader) {
    auto gpu = boids::makeBoidsGpu(device, shader, 1);
    const auto state = gpu->readback();
    gpu->interventions().push_back(std::make_unique<UnsupportedIntervention>());
    reject([&] { gpu->step(0.01F); }); reject([&] { gpu->step(0); });
    gpu->interventions().clear();
    gpu->rules().clear(); gpu->rules().push_back(std::make_unique<UnsupportedRule>());
    reject([&] { gpu->step(0.01F); }); reject([&] { gpu->step(0); });
    gpu->rules().clear(); gpu->rules().push_back(std::make_unique<boids::Alignment>());
    gpu->rules().push_back(std::make_unique<boids::Alignment>());
    reject([&] { gpu->step(0.01F); });
    require(identical(state, gpu->readback()), "Rejected step modified GPU state");
    reject([&] { gpu->step(-1); });
    gpu->rules().clear();
    // Exact walls, multiple reflections and wrapped overshoot without rules.
    for (const auto boundary : {sim::BoundaryMode::Wrap, sim::BoundaryMode::Bounce}) {
        const sim::World world{{-1, -1, -1}, {1, 1, 1}, boundary};
        CpuSimulation cpu{1, world, 4};
        gpu->setWorld(world);
        cpu.setMotionLimits({0, 8, 20}); gpu->setMotionLimits({0, 8, 20});
        std::array<Agent, 1> agents{};
        agents[0].position = {1, -1, 0}; agents[0].velocity = {4, -3, 1};
        cpu.setAgents(agents); gpu->setAgents(agents);
        cpu.step(3.0F); gpu->step(3.0F);
        compare(cpu.agents(), gpu->readback(), world);
    }
}

void largePopulation(ObservedDevice& device, const std::string& shader) {
    auto gpu = boids::makeBoidsGpu(device, shader, 100000, {{-40, -40, -40}, {40, 40, 40}}, 5);
    gpu->step(1.0F / 120.0F); // pipeline warmup and initial parameter upload
    const auto uploads = device.uploads, reads = device.downloads;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned step = 0; step < 3; ++step) gpu->step(1.0F / 120.0F);
    const double milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count() / 3.0;
    require(device.downloads == reads && device.uploads == uploads,
        "Steady GPU steps unexpectedly transferred host data");
    const auto state = gpu->readback();
    require(state.size() == 100000, "100k GPU run lost agents");
    for (std::size_t index = 0; index < state.size(); ++index) {
        require(state[index].id == index, "100k GPU run lost agent IDs");
        const float speed = glm::length(state[index].velocity);
        require(std::isfinite(speed) && speed >= 0.999F && speed <= 8.001F,
            "100k GPU run violated speed limits");
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            require(state[index].position[axis] >= -40.0F && state[index].position[axis] < 40.0F,
                "100k GPU run violated world bounds");
        }
    }
    std::cout << "100000 agents: " << milliseconds << " ms/step (3 synchronous warmed steps; no FPS target)\n";
}
}

int main() {
    try {
        core::Window window{320, 240, "GPU simulation correctness"};
        core::Context context{window};
        {
            ObservedDevice device{context};
            const std::string shader = std::string{EMERGENCES_SHADER_DIRECTORY} + "/compute/boids.comp.spv";
            trajectories(device, shader);
            crowdedAndMaximumGrid(device, shader);
            colorsAndConfiguration(device, shader);
            invalidAndBoundary(device, shader);
            largePopulation(device, shader);
        }
        context.waitIdle();
        require(context.validationErrorCount() == 0, "Vulkan validation reported GPU simulation errors");
        std::cout << "GPU tests passed; max position error=" << maximumPositionError
            << ", max velocity error=" << maximumVelocityError << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GPU simulation test failure: " << error.what() << '\n';
        return 1;
    }
}
