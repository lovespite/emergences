#include "scenarios/boids/BoidsConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

#include "config/AppConfig.h"
#include "scenarios/boids/BoidsScene.h"
#include "sim/ComputeDevice.h"

namespace {

using namespace emergences;
using scenarios::boids::applyBoidsSettings;
using scenarios::boids::BoidsScene;
using scenarios::boids::captureBoidsSettings;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

config::BoidsSettings configuredSettings() {
    config::BoidsSettings settings;
    settings.count = 47;
    settings.gpu = false;
    settings.paused = true;
    settings.alignmentWeight = 0.75F;
    settings.alignmentRadius = 7.0F;
    settings.cohesionWeight = 0.11F;
    settings.cohesionRadius = 9.0F;
    settings.separationWeight = 3.25F;
    settings.separationRadius = 4.0F;
    settings.minSpeed = 8.0F;
    settings.maxSpeed = 14.0F;
    settings.maxAcceleration = 27.0F;
    settings.bounce = true;
    return settings;
}

void requireConfiguredSpeeds(BoidsScene& scene, const config::BoidsSettings& settings) {
    const auto agents = scene.currentSimulation().readback();
    require(agents.size() == static_cast<std::size_t>(settings.count),
        "Configured count must be applied before the initial reset");
    const auto initialSpeed = (settings.minSpeed + settings.maxSpeed) * 0.5F;
    for (const auto& agent : agents) {
        const auto speed = glm::length(agent.velocity);
        require(std::isfinite(speed) && speed >= settings.minSpeed - 1.0e-5F &&
            speed <= settings.maxSpeed + 1.0e-5F && std::abs(speed - initialSpeed) < 1.0e-5F,
            "Initial flock velocities must reset using the configured speed interval");
    }
}

void cpuSettingsAndReset() {
    BoidsScene scene{6};
    scene.advance(BoidsScene::FixedStepSeconds);
    // Settings are identified by rule name, independently of UI/list ordering.
    auto& rules = scene.simulation().rules();
    std::rotate(rules.begin(), rules.begin() + 1, rules.end());
    const auto settings = configuredSettings();
    applyBoidsSettings(scene, settings);
    require(scene.backend() == sim::Backend::CPU && scene.paused() && scene.totalSteps() == 0,
        "Loading parameters resets the clock and restores pause on the active backend");
    requireConfiguredSpeeds(scene, settings);
    require(scene.displayAgents().size() == static_cast<std::size_t>(settings.count),
        "Reset must refresh the CPU display copy with the configured count");

    config::BoidsSettings captured;
    captured.gpu = true;
    captured.cursor = true;
    captured.cursorAttract = true;
    captured.cursorRadius = 12.0F;
    captured.cursorStrength = 51.0F;
    captured.cursorPlaneHeight = -3.0F;
    auto expected = settings;
    expected.gpu = captured.gpu;
    expected.cursor = captured.cursor;
    expected.cursorAttract = captured.cursorAttract;
    expected.cursorRadius = captured.cursorRadius;
    expected.cursorStrength = captured.cursorStrength;
    expected.cursorPlaneHeight = captured.cursorPlaneHeight;
    const BoidsScene& constScene = scene;
    captureBoidsSettings(constScene, captured);
    require(captured == expected,
        "Capture must read every simulation setting and preserve backend/cursor preferences");

    auto edited = settings;
    edited.cohesionWeight = 0.37F;
    edited.paused = false;
    edited.gpu = true;
    edited.bounce = false;
    applyBoidsSettings(scene, edited);
    captureBoidsSettings(scene, captured);
    require(captured.cohesionWeight == 0.37F && !captured.paused && !captured.bounce &&
        scene.backend() == sim::Backend::CPU,
        "Reapplying settings must restore changed values without selecting a backend");
}

void emptyFlockAndInvalidCount() {
    BoidsScene scene{8};
    auto settings = configuredSettings();
    settings.count = 0;
    applyBoidsSettings(scene, settings);
    require(scene.paused() && scene.currentSimulation().count() == 0 &&
        scene.displayAgents().empty() && scene.totalSteps() == 0,
        "An empty saved flock must restore successfully while remaining paused");
    config::BoidsSettings captured;
    captureBoidsSettings(scene, captured);
    require(captured.count == 0 && captured.paused,
        "An empty flock and pause must be remembered exactly");
    settings.count = -1;
    bool rejected = false;
    try {
        applyBoidsSettings(scene, settings);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && scene.currentSimulation().count() == 0,
        "Negative counts must fail before converting to an allocation size");
}

// Exercises configuration with GPU ownership without requiring Vulkan or a
// graphics driver. Compute arithmetic is covered by the integration suite.
class MemoryComputeDevice final : public sim::ComputeDevice {
public:
    sim::BufferHandle createBuffer(std::size_t bytes) override {
        const auto handle = nextHandle_++;
        buffers_.emplace(handle, std::vector<std::byte>(bytes));
        return handle;
    }
    void destroyBuffer(sim::BufferHandle handle) noexcept override { buffers_.erase(handle); }
    void upload(sim::BufferHandle handle, std::span<const std::byte> bytes) override {
        auto& storage = buffers_.at(handle);
        require(bytes.size() <= storage.size(), "Upload must fit its buffer");
        std::copy(bytes.begin(), bytes.end(), storage.begin());
    }
    void download(sim::BufferHandle handle, std::span<std::byte> bytes) override {
        const auto& storage = buffers_.at(handle);
        require(bytes.size() <= storage.size(), "Download must fit its buffer");
        std::copy_n(storage.begin(), bytes.size(), bytes.begin());
        ++downloads;
    }
    sim::PipelineHandle createPipeline(std::string_view, std::uint32_t, std::uint32_t) override {
        return nextHandle_++;
    }
    void destroyPipeline(sim::PipelineHandle) noexcept override {}
    void execute(std::span<const sim::ComputeDispatch>) override {}
    void waitIdle() override {}

    std::size_t downloads{};

private:
    std::uint64_t nextHandle_{1};
    std::map<sim::BufferHandle, std::vector<std::byte>> buffers_;
};

void activeGpuSettings() {
    MemoryComputeDevice device;
    BoidsScene scene{3};
    scene.initializeGpu(device, "unused.spv");
    scene.setBackend(sim::Backend::GPU);
    const auto settings = configuredSettings();
    applyBoidsSettings(scene, settings);
    require(scene.backend() == sim::Backend::GPU && scene.simulation().count() == 3 &&
        scene.simulation().motionLimits().maximumSpeed == 8.0F,
        "Applying settings must modify GPU state and leave inactive CPU storage alone");
    requireConfiguredSpeeds(scene, settings);
    config::BoidsSettings captured;
    captured.gpu = false;
    const auto downloads = device.downloads;
    const BoidsScene& constScene = scene;
    captureBoidsSettings(constScene, captured);
    require(captured == settings && device.downloads == downloads,
        "Capture must use active GPU parameters without particle readback or changing its preference");
    require(scene.paused() && scene.totalSteps() == 0,
        "GPU settings must restore pause and reset the simulation clock");
    scene.setBackend(sim::Backend::CPU);
    captureBoidsSettings(scene, captured);
    require(captured == settings,
        "Configured GPU settings must survive a subsequent backend transfer");
}

} // namespace

int main() {
    try {
        cpuSettingsAndReset();
        emptyFlockAndInvalidCount();
        activeGpuSettings();
        std::cout << "Boids configuration tests passed (CPU/GPU settings, reset and capture)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Boids configuration test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
