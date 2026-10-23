#include "scenarios/boids/BoidsScene.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

#include "scenarios/boids/BoidsGpu.h"
#include "scenarios/boids/CursorRepel.h"

namespace {

using namespace emergences;
using scenarios::boids::BoidsScene;
using scenarios::boids::ColorMode;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template<class Callback>
void requireFailure(Callback&& callback, const char* message) {
    bool failed = false;
    try {
        callback();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, message);
}

void requireSameState(std::span<const sim::Agent> left, std::span<const sim::Agent> right) {
    require(left.size() == right.size(), "Backend transfer must preserve count");
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto& a = left[index];
        const auto& b = right[index];
        require(a.position == b.position && a.velocity == b.velocity && a.size == b.size &&
            a.reserved == b.reserved && a.color == b.color && a.id == b.id,
            "Backend transfer must preserve every particle field");
    }
}

// Tests orchestration and explicit state transfer without a graphics driver.
// Shader arithmetic is covered separately by the Vulkan integration tests.
class MemoryComputeDevice final : public sim::ComputeDevice {
public:
    sim::BufferHandle createBuffer(std::size_t size) override {
        const auto handle = nextHandle_++;
        buffers_.emplace(handle, std::vector<std::byte>(size));
        return handle;
    }
    void destroyBuffer(sim::BufferHandle handle) noexcept override { buffers_.erase(handle); }
    void upload(sim::BufferHandle handle, std::span<const std::byte> bytes) override {
        if (failUpload) {
            throw std::runtime_error("Injected upload failure");
        }
        auto& storage = buffers_.at(handle);
        require(bytes.size() <= storage.size(), "Upload must fit its storage");
        std::copy(bytes.begin(), bytes.end(), storage.begin());
    }
    void download(sim::BufferHandle handle, std::span<std::byte> bytes) override {
        if (failDownload) {
            throw std::runtime_error("Injected download failure");
        }
        ++downloads;
        const auto& storage = buffers_.at(handle);
        require(bytes.size() <= storage.size(), "Download must fit its storage");
        std::copy_n(storage.begin(), bytes.size(), bytes.begin());
    }
    sim::PipelineHandle createPipeline(std::string_view, std::uint32_t, std::uint32_t) override {
        return nextHandle_++;
    }
    void destroyPipeline(sim::PipelineHandle) noexcept override {}
    void execute(std::span<const sim::ComputeDispatch> dispatches) override {
        for (const auto& dispatch : dispatches) {
            require(dispatch.pipeline != 0 && dispatch.groupsX > 0,
                "Scene must submit valid compute dispatches");
            for (const auto buffer : dispatch.buffers) {
                require(buffers_.contains(buffer), "Dispatch buffers must remain alive");
            }
        }
        ++executions;
    }
    void waitIdle() override {}

    bool failUpload{false};
    bool failDownload{false};
    std::size_t downloads{};
    std::size_t executions{};

private:
    std::uint64_t nextHandle_{1};
    std::map<sim::BufferHandle, std::vector<std::byte>> buffers_;
};

void attach(BoidsScene& scene, MemoryComputeDevice& device, std::uint32_t seed) {
    scene.attachGpu(scenarios::boids::makeBoidsGpu(device, "unused.spv", 0,
        scene.simulation().world(), seed));
}

void stateAndSettingsTransfer() {
    MemoryComputeDevice device;
    BoidsScene scene{0, 21};
    auto& cpu = scene.simulation();
    cpu.setWorld({{-12.0F, -18.0F, -24.0F}, {12.0F, 18.0F, 24.0F},
        sim::BoundaryMode::Bounce});
    cpu.setMotionLimits({0.0F, 13.0F, 17.0F});
    std::array<sim::Agent, 3> particles{};
    for (std::size_t index = 0; index < particles.size(); ++index) {
        auto& particle = particles[index];
        particle.id = static_cast<std::uint32_t>(90 - index * 7);
        particle.position = {static_cast<float>(index), -2.0F, 3.0F};
        particle.velocity = {2.0F, 1.0F, -1.0F};
        particle.color = {0.2F, 0.3F, 0.4F};
        particle.size = 0.75F;
        particle.reserved = 4.5F;
    }
    cpu.setAgents(particles);
    cpu.rules()[0]->setWeight(3.25F);
    cpu.rules()[1]->setRadius(7.5F);
    const auto* firstRule = cpu.rules()[0].get();
    cpu.interventions().push_back(std::make_unique<scenarios::boids::CursorRepel>());
    const auto* intervention = cpu.interventions()[0].get();
    attach(scene, device, 21);
    scene.setPaused(true);
    scene.setBackend(sim::Backend::GPU);
    auto& gpu = scene.currentSimulation();
    require(scene.backend() == sim::Backend::GPU && &gpu == scene.gpuSimulation(),
        "The active simulation must track the selected backend");
    require(scene.paused() && scene.totalSteps() == 0,
        "Backend selection must retain the pause state and simulation clock");
    requireSameState(particles, gpu.readback());
    require(gpu.world().minimum == cpu.world().minimum &&
        gpu.world().maximum == cpu.world().maximum &&
        gpu.world().boundary == sim::BoundaryMode::Bounce &&
        gpu.motionLimits().minimumSpeed == 0.0F &&
        gpu.motionLimits().maximumSpeed == 13.0F &&
        gpu.motionLimits().maximumAcceleration == 17.0F,
        "Backend selection must transfer world and every motion limit");
    require(gpu.rules()[0].get() == firstRule && gpu.rules()[0]->weight() == 3.25F &&
        gpu.rules()[1]->radius() == 7.5F,
        "Backend selection must transfer the active rule objects and parameters");
    require(gpu.interventions().empty() && cpu.interventions()[0].get() == intervention,
        "CPU-only interventions must remain owned by CPU");
    require(gpu.agents().empty() && scene.displayAgents().empty(),
        "GPU state must not masquerade as a CPU particle view");

    gpu.rules()[0]->setWeight(1.75F);
    gpu.setMotionLimits({0.0F, 12.0F, 19.0F});
    auto changedWorld = gpu.world();
    changedWorld.boundary = sim::BoundaryMode::Wrap;
    gpu.setWorld(changedWorld);
    scene.setBackend(sim::Backend::CPU);
    require(&scene.currentSimulation() == &cpu && scene.paused(),
        "Returning to CPU must retain pause and expose CPU storage");
    requireSameState(particles, cpu.agents());
    require(cpu.rules()[0].get() == firstRule && cpu.rules()[0]->weight() == 1.75F &&
        cpu.world().boundary == sim::BoundaryMode::Wrap &&
        cpu.motionLimits().maximumSpeed == 12.0F &&
        cpu.motionLimits().maximumAcceleration == 19.0F,
        "Settings edited on GPU must survive return to CPU");
    require(cpu.interventions()[0].get() == intervention,
        "Returning to CPU must restore access to existing interventions");
    require(scene.refreshColors(ColorMode::Solid).size() == particles.size(),
        "CPU display records must become available after a color refresh");
    scene.setBackend(sim::Backend::GPU);
    scene.setBackend(sim::Backend::CPU);
    requireSameState(particles, cpu.agents());
    require(cpu.rules()[0].get() == firstRule,
        "Repeated switches must not replace or duplicate active rules");
}

void clocksResetAndGpuRendering() {
    MemoryComputeDevice device;
    BoidsScene scene{8, 17};
    const auto initial = scene.simulation().readback();
    scene.advance(BoidsScene::FixedStepSeconds);
    scene.advance(BoidsScene::FixedStepSeconds * 0.5);
    scene.initializeGpu(device, "unused.spv");
    scene.setBackend(sim::Backend::GPU);
    require(scene.totalSteps() == 1 && scene.advance(BoidsScene::FixedStepSeconds * 0.5) == 0,
        "Switching must retain completed time while discarding its old fraction");
    const auto downloads = device.downloads;
    require(scene.advance(BoidsScene::FixedStepSeconds * 0.5) == 1,
        "GPU stepping must accumulate new frame time normally");
    for (const auto mode : {ColorMode::Direction, ColorMode::Speed, ColorMode::Density,
                           ColorMode::Solid}) {
        require(scene.refreshColors(mode).empty() && scene.displayAgents().empty(),
            "GPU color refresh must return no CPU display copy");
    }
    require(device.downloads == downloads && device.executions > 0,
        "Ordinary GPU stepping and colors must not trigger CPU readback");
    scene.setPaused(true);
    require(scene.singleStep() && scene.totalSteps() == 3,
        "Single-step must use the active GPU backend and advance the scene clock");
    scene.reset();
    require(scene.paused() && scene.totalSteps() == 0 && scene.backend() == sim::Backend::GPU,
        "GPU reset must preserve backend and pause while clearing scene time");
    requireSameState(initial, scene.currentSimulation().readback());
    scene.setBackend(sim::Backend::CPU);
    requireSameState(initial, scene.simulation().agents());
}

void failedTransfersKeepActiveState() {
    MemoryComputeDevice device;
    BoidsScene scene{4, 33};
    scene.advance(BoidsScene::FixedStepSeconds);
    const auto initial = scene.simulation().readback();
    const auto* rule = scene.simulation().rules()[0].get();
    requireFailure([&] { scene.setBackend(sim::Backend::GPU); },
        "Selecting GPU without an attached device must fail");
    requireFailure([&] { scene.setBackend(static_cast<sim::Backend>(99)); },
        "Unknown backends must be rejected");
    requireFailure([&] { scene.attachGpu(nullptr); }, "A null GPU must be rejected");
    attach(scene, device, 33);
    device.failUpload = true;
    requireFailure([&] { scene.setBackend(sim::Backend::GPU); },
        "A failed GPU state upload must fail the backend selection");
    device.failUpload = false;
    require(scene.backend() == sim::Backend::CPU && scene.totalSteps() == 1 &&
        scene.simulation().rules()[0].get() == rule,
        "A failed upload must retain CPU ownership, rules and clock");
    requireSameState(initial, scene.simulation().agents());

    scene.setBackend(sim::Backend::GPU);
    device.failDownload = true;
    requireFailure([&] { scene.setBackend(sim::Backend::CPU); },
        "A failed GPU readback must fail the backend selection");
    device.failDownload = false;
    require(scene.backend() == sim::Backend::GPU && scene.totalSteps() == 1 &&
        scene.currentSimulation().rules()[0].get() == rule,
        "A failed readback must retain GPU ownership, rules and clock");
    requireSameState(initial, scene.currentSimulation().readback());
    scene.setBackend(sim::Backend::CPU);
    requireSameState(initial, scene.simulation().agents());

    scene.simulation().rules().push_back(nullptr);
    requireFailure([&] { scene.setBackend(sim::Backend::GPU); },
        "Unsupported GPU rules must be rejected before switching");
    require(scene.backend() == sim::Backend::CPU && scene.simulation().rules().size() == 4,
        "Rule validation failure must leave CPU rule ownership intact");
    scene.simulation().rules().pop_back();
}

} // namespace

int main() {
    try {
        stateAndSettingsTransfer();
        clocksResetAndGpuRendering();
        failedTransfersKeepActiveState();
        std::cout << "Boids backend tests passed (state transfer, ownership, clock and failures)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Boids backend test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
