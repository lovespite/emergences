#include "core/ComputeDevice.h"
#include "core/Context.h"
#include "core/Window.h"
#include "scenarios/particle_life/ParticleLifeGpu.h"
#include "scenarios/particle_life/ParticleLifeScene.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/geometric.hpp>

namespace {
using namespace emergences;
using scenarios::particle_life::ParticleLifeGpu;
using scenarios::particle_life::ParticleLifeScene;
using sim::Agent;
constexpr float Step = static_cast<float>(ParticleLifeScene::FixedStepSeconds);

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class ObservedDevice final : public sim::ComputeDevice {
public:
    explicit ObservedDevice(const core::Context& context) : actual{context} {}
    sim::BufferHandle createBuffer(std::size_t bytes) override { return actual.createBuffer(bytes); }
    void destroyBuffer(sim::BufferHandle handle) noexcept override { actual.destroyBuffer(handle); }
    void upload(sim::BufferHandle handle, std::span<const std::byte> bytes) override {
        ++uploads;
        actual.upload(handle, bytes);
    }
    void download(sim::BufferHandle handle, std::span<std::byte> bytes) override {
        ++downloads;
        actual.download(handle, bytes);
    }
    sim::PipelineHandle createPipeline(std::string_view path, std::uint32_t bindings,
        std::uint32_t constants) override { return actual.createPipeline(path, bindings, constants); }
    void destroyPipeline(sim::PipelineHandle handle) noexcept override { actual.destroyPipeline(handle); }
    void execute(std::span<const sim::ComputeDispatch> batch) override { actual.execute(batch); }
    void waitIdle() override { actual.waitIdle(); }
    core::VulkanComputeDevice actual;
    std::size_t uploads{}, downloads{};
};

float maximumPositionError{}, maximumVelocityError{};
void compare(std::span<const Agent> expected, std::span<const Agent> actual,
    const sim::World& world, float tolerance = 0.002F) {
    require(expected.size() == actual.size(), "GPU count differs from CPU reference");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& a = expected[i];
        const auto& b = actual[i];
        require(a.id == b.id && a.size == b.size && a.color == b.color && a.reserved == b.reserved,
            "GPU evolution changed particle metadata");
        require(b.position.y == 0.0F && b.velocity.y == 0.0F, "GPU particles left the XZ plane");
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            require(std::isfinite(b.position[axis]) && std::isfinite(b.velocity[axis]), "Nonfinite GPU state");
            require(b.position[axis] >= world.minimum[axis] && b.position[axis] < world.maximum[axis],
                "GPU state escaped periodic bounds");
        }
        const float positionError = glm::length(world.displacement(a.position, b.position));
        const float velocityError = glm::length(a.velocity - b.velocity);
        maximumPositionError = std::max(maximumPositionError, positionError);
        maximumVelocityError = std::max(maximumVelocityError, velocityError);
        if (positionError > tolerance || velocityError > tolerance) {
            std::cerr << "Particle " << i << ": position error=" << positionError
                << ", velocity error=" << velocityError << '\n';
            throw std::runtime_error("GPU trajectory differs from CPU reference beyond tolerance");
        }
    }
}

void requireIdentical(std::span<const Agent> expected, std::span<const Agent> actual) {
    require(expected.size() == actual.size() && (expected.empty() ||
        std::memcmp(expected.data(), actual.data(), expected.size_bytes()) == 0),
        "Backend transfer changed particle state");
}

void trajectories(ObservedDevice& device, const std::string& shader) {
    ParticleLifeGpu gpu{device, shader};
    for (const std::size_t count : {0U, 1U, 127U, 128U, 129U, 257U, 2000U}) {
        ParticleLifeScene cpu{count, 827U};
        cpu.setSpeciesCount(8);
        gpu.setState(cpu.agents(), cpu.species());
        require(gpu.count() == count && gpu.stateBuffer() != 0, "GPU population upload failed");
        for (int tick = 0; tick < 8; ++tick) {
            cpu.advance(ParticleLifeScene::FixedStepSeconds);
            gpu.step(Step, cpu.parameters(), cpu.matrix());
        }
        compare(cpu.agents(), gpu.readback(), cpu.world());
    }

    ParticleLifeScene cpu{6};
    std::vector<Agent> edge(cpu.agents().begin(), cpu.agents().end());
    const std::array<glm::vec3, 6> positions{{
        {49.9F, 0.0F, 0.0F}, {-49.9F, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F},
        {-3.9F, 0.0F, 20.0F}, {3.9F, 0.0F, 20.0F}}};
    const std::array<std::uint8_t, 6> types{5, 2, 3, 1, 0, 4};
    for (std::size_t i = 0; i < edge.size(); ++i) {
        edge[i].position = positions[i];
        edge[i].id = static_cast<std::uint32_t>(11 + i * 17);
        edge[i].reserved = 0.125F * static_cast<float>(i);
    }
    edge[0].velocity = {19.0F, 0.0F, 0.0F};
    cpu.setAgents(edge, types);
    cpu.setInteraction(0, 4, 0.8F);
    cpu.setInteraction(4, 0, -0.6F);
    gpu.setState(cpu.agents(), cpu.species());
    for (int tick = 0; tick < 16; ++tick) {
        cpu.advance(ParticleLifeScene::FixedStepSeconds);
        gpu.step(Step, cpu.parameters(), cpu.matrix());
        compare(cpu.agents(), gpu.readback(), cpu.world());
    }

    // Dense particles exceed a workgroup and occupy the same CPU hash cell.
    cpu.setCount(513);
    std::vector<Agent> dense(cpu.agents().begin(), cpu.agents().end());
    for (auto& agent : dense) agent.position *= 0.03F;
    cpu.setAgents(dense, cpu.species());
    gpu.setState(cpu.agents(), cpu.species());
    for (int tick = 0; tick < 6; ++tick) {
        cpu.advance(ParticleLifeScene::FixedStepSeconds);
        gpu.step(Step, cpu.parameters(), cpu.matrix());
    }
    compare(cpu.agents(), gpu.readback(), cpu.world());
}

void parameterChanges(ObservedDevice& device, const std::string& shader) {
    ParticleLifeScene cpu{129, 15U};
    ParticleLifeGpu gpu{device, shader};
    std::vector<Agent> initial(cpu.agents().begin(), cpu.agents().end());
    for (std::size_t i = 0; i < initial.size(); ++i) {
        initial[i].position *= 0.1F;
        initial[i].velocity = {static_cast<float>(i % 7) - 3.0F, 0.0F, 1.0F};
    }
    cpu.setAgents(initial, cpu.species());
    gpu.setState(cpu.agents(), cpu.species());
    for (float radius : {1.0F, 12.0F, 50.0F}) {
        auto parameters = cpu.parameters();
        parameters.interactionRadius = radius;
        parameters.force = radius == 1.0F ? 0.0F : 71.0F;
        parameters.friction = 2.7F;
        parameters.repulsion = 4.2F;
        parameters.coreRatio = 0.17F;
        parameters.maxSpeed = 40.0F;
        cpu.setParameters(parameters);
        cpu.randomizeMatrix(static_cast<std::uint32_t>(radius));
        const auto uploads = device.uploads;
        for (int tick = 0; tick < 6; ++tick) {
            cpu.advance(ParticleLifeScene::FixedStepSeconds);
            gpu.step(Step, cpu.parameters(), cpu.matrix());
        }
        require(device.uploads == uploads + 1, "Changed rules must upload exactly once");
        compare(cpu.agents(), gpu.readback(), cpu.world());
    }
}

void sceneTransfersAndResidentState(ObservedDevice& device, const std::string& shader) {
    ParticleLifeScene scene{257, 901U};
    scene.advance(1.0 / 60.0);
    const auto initial = scene.readback();
    const auto initialMatrix = scene.matrix();
    const std::vector<std::uint8_t> initialTypes(scene.species().begin(), scene.species().end());
    const auto steps = scene.totalSteps();
    scene.setPaused(true);
    scene.initializeGpu(device, shader);
    scene.setBackend(sim::Backend::GPU);
    require(scene.paused() && scene.totalSteps() == steps && scene.agents().empty() &&
        scene.stateBuffer() != 0 && scene.count() == initial.size(), "GPU switch changed scene controls");
    requireIdentical(initial, scene.readback());
    require(scene.singleStep() && scene.totalSteps() == steps + 1, "GPU single step failed");
    scene.setPaused(false);
    scene.advance(ParticleLifeScene::FixedStepSeconds); // Warm up the parameter upload.
    const auto downloads = device.downloads;
    const auto uploads = device.uploads;
    for (int tick = 0; tick < 8; ++tick) scene.advance(ParticleLifeScene::FixedStepSeconds);
    require(device.downloads == downloads && device.uploads == uploads,
        "Normal GPU steps must retain particles and unchanged parameters on the GPU");
    auto parameters = scene.parameters();
    parameters.maxSpeed = 0.5F;
    scene.setParameters(parameters);
    require(device.downloads == downloads, "GPU speed limit editing must not read back particles");
    for (const auto& agent : scene.readback()) {
        require(glm::length(glm::dvec3(agent.velocity)) <= 0.5, "GPU speed clamp exceeded the new limit");
    }
    const auto gpuState = scene.readback();
    const auto gpuSteps = scene.totalSteps();
    scene.setBackend(sim::Backend::CPU);
    requireIdentical(gpuState, scene.agents());
    require(scene.totalSteps() == gpuSteps && scene.matrix() == initialMatrix &&
        std::equal(initialTypes.begin(), initialTypes.end(), scene.species().begin()),
        "Switching back to CPU changed time, rules, or species");
    scene.setBackend(sim::Backend::GPU);
    scene.setPaused(true);
    scene.reset();
    ParticleLifeScene seeded{257, 901U};
    requireIdentical(seeded.agents(), scene.readback());
    require(scene.paused() && scene.totalSteps() == 0, "GPU reset changed pause or retained clock");
    scene.randomizeMatrix(42U);
    scene.setSpeciesCount(8);
    scene.setCount(0);
    require(scene.count() == 0 && scene.readback().empty(), "Empty GPU scene reset failed");
    scene.setCount(129);
    scene.setSeed(123U);
    seeded.setSpeciesCount(8);
    seeded.setCount(129);
    seeded.setSeed(123U);
    requireIdentical(seeded.agents(), scene.readback());
    scene.setBackend(sim::Backend::CPU);
    require(scene.count() == 129, "GPU population changes were not preserved on transfer");
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// Optional, reproducible timing; no machine-specific performance assertion.
// Every timed CPU/GPU step starts from the exact same state. GPU timings include
// synchronous submission/fence completion, excluding upload/readback and rendering.
void benchmark(ObservedDevice& device, const std::string& shader) {
    ParticleLifeGpu gpu{device, shader};
    std::cout << "population,fixture,cpu_median_ms,gpu_median_ms,speedup\n";
    for (std::size_t count : {2000U, 5000U, 10000U}) {
        for (bool dense : {false, true}) {
            ParticleLifeScene cpu{count, 12345U};
            const std::vector<std::uint8_t> types(cpu.species().begin(), cpu.species().end());
            auto snapshot = cpu.readback();
            if (dense) {
                for (auto& agent : snapshot) {
                    const auto group = agent.id % 4;
                    const glm::vec3 center{-20.0F + 40.0F * static_cast<float>(group % 2), 0.0F,
                        -20.0F + 40.0F * static_cast<float>(group / 2)};
                    agent.position = center + agent.position * 0.06F;
                }
            }
            std::vector<double> cpuTimes, gpuTimes;
            for (int trial = 0; trial < 6; ++trial) {
                cpu.setAgents(snapshot, types);
                gpu.setState(snapshot, types);
                const auto startCpu = std::chrono::steady_clock::now();
                cpu.advance(ParticleLifeScene::FixedStepSeconds);
                const auto endCpu = std::chrono::steady_clock::now();
                gpu.step(Step, cpu.parameters(), cpu.matrix());
                const auto endGpu = std::chrono::steady_clock::now();
                if (trial != 0) {
                    cpuTimes.push_back(std::chrono::duration<double, std::milli>(endCpu - startCpu).count());
                    gpuTimes.push_back(std::chrono::duration<double, std::milli>(endGpu - endCpu).count());
                }
            }
            compare(cpu.agents(), gpu.readback(), cpu.world());
            const double cpuMs = median(cpuTimes), gpuMs = median(gpuTimes);
            std::cout << count << ',' << (dense ? "four_dense_clusters" : "uniform") << ','
                << std::fixed << std::setprecision(3) << cpuMs << ',' << gpuMs << ',' << cpuMs / gpuMs << '\n';
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        core::Window window{320, 240, "Particle Life GPU verification"};
        core::Context context{window};
#if !defined(NDEBUG)
        require(context.validationEnabled(), "Debug GPU tests require Vulkan validation");
#endif
        {
            ObservedDevice device{context};
            const std::string shader = std::string{EMERGENCES_SHADER_DIRECTORY} + "/compute/particle_life.comp.spv";
            if (argc == 2 && std::string_view{argv[1]} == "--benchmark") benchmark(device, shader);
            else {
                trajectories(device, shader);
                parameterChanges(device, shader);
                sceneTransfersAndResidentState(device, shader);
            }
        }
        context.waitIdle();
        require(context.validationErrorCount() == 0, "Vulkan validation reported Particle Life errors");
        std::cout << std::defaultfloat << std::setprecision(6)
            << "Particle Life GPU checks passed; max position error=" << maximumPositionError
            << ", max velocity error=" << maximumVelocityError << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Particle Life GPU test failure: " << error.what() << '\n';
        return 1;
    }
}
