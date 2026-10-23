#include "scenarios/boids/BoidsApp.h"
#include "scenarios/boids/BoidsScene.h"
#include "scenarios/boids/BoidsConfig.h"
#include "scenarios/ants/AntScene.h"
#include "scenarios/ants/AntPresentation.h"
#include "config/AppConfig.h"
#include "scenarios/boids/BoidsGpu.h"
#include "scenarios/boids/CursorAttract.h"
#include "scenarios/boids/CursorRepel.h"
#include "core/Camera.h"
#include "core/Context.h"
#include "core/ComputeDevice.h"
#include "core/FrameRenderer.h"
#include "core/ImGuiLayer.h"
#include "core/LineRenderer.h"
#include "core/ParticleRenderer.h"
#include "core/Window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/geometric.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <vector>

namespace emergences::scenarios::boids {
namespace {

struct Controls {
    int scenario = 0;
    config::AntSettings ants;
    bool preferGpu = true;
    bool panelVisible = true;
    int shape = 0;
    int color = 0;
    float size = 0.45F;
    bool grid = true;
    bool bounds = true;
    bool antialiasing = true;
    bool cursor = false;
    int cursorMode = 0;
    float radius = 8.0F;
    float strength = 30.0F;
    float planeHeight = 0.0F;
    float simulationMs = 0.0F;
    std::size_t simulationSteps = 0;
    bool simulationLimited = false;
    double simulationWarningUntil = 0.0;
    std::string backendError;
};

struct Actions {
    bool reset = false;
    bool step = false;
    bool skipElapsed = false;
};

// Clamp text entry as well as dragging, and keep nonfinite text out of the model.
bool slider(const char* label, float& value, float minimum, float maximum) {
    const float old = value;
    if (!ImGui::SliderFloat(label, &value, minimum, maximum, "%.2f",
                            ImGuiSliderFlags_AlwaysClamp)) return false;
    value = std::isfinite(value) ? std::clamp(value, minimum, maximum) : old;
    return value != old;
}

Actions drawPanel(BoidsScene& scene, ants::AntScene& antScene, Controls& controls, core::Camera& camera,
                  const core::Context& context, const core::FrameRenderer& renderer,
                  bool& fullscreen, const config::ConfigFile* config) {
    Actions actions;
    ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, std::max(180.0F,
        std::min(690.0F, ImGui::GetIO().DisplaySize.y - 36.0F))), ImGuiCond_Always);
    ImGui::Begin("Emergences", nullptr, ImGuiWindowFlags_NoResize);
    ImGui::TextColored(ImVec4(0.3F, 0.8F, 0.95F, 1.0F), "%s",
        controls.scenario == 1 ? "ANT COLONY / LIVE ECOSYSTEM" : "LIVE BOIDS");
    ImGui::Checkbox("Fullscreen (F11)", &fullscreen);
    ImGui::SameLine();
    ImGui::TextDisabled("F1: hide panel");
    if (ImGui::Combo("Scene", &controls.scenario, "Boids\0Ant Colony\0")) {
        actions.skipElapsed = true;
        if (controls.scenario == 1) ants::resetAntCamera(camera);
        else camera.reset();
    }
    if (controls.scenario == 1) {
        if (config) {
            if (!config->lastError().empty()) ImGui::TextWrapped("Settings: %s", config->lastError().c_str());
            if (!config->warnings().empty() && ImGui::TreeNode("Settings warnings")) {
                for (const auto& warning : config->warnings()) ImGui::TextWrapped("%s", warning.c_str());
                ImGui::TreePop();
            }
        }
        const auto antActions = ants::drawAntControls(antScene, controls.ants, camera, renderer,
            controls.antialiasing, controls.grid, controls.bounds);
        actions.reset = antActions.reset;
        actions.step = antActions.step;
        actions.skipElapsed |= antActions.skipElapsed;
        ImGui::Separator();
        ImGui::TextDisabled("Left drag: orbit | Middle drag: pan");
        ImGui::TextDisabled("Wheel: zoom | Escape: close");
        ImGui::Text("%.1f FPS | simulation %.2f ms (%zu steps)", static_cast<double>(ImGui::GetIO().Framerate),
            static_cast<double>(controls.simulationMs), controls.simulationSteps);
        if (controls.simulationLimited && !antScene.paused())
            ImGui::TextColored(ImVec4(1, 0.75F, 0.35F, 1), "Simulation slowed to keep controls responsive.");
        else ImGui::TextDisabled("Inactive scenes keep their state.");
        ImGui::TextDisabled("%s", context.deviceName().c_str());
        ImGui::End();
        return actions;
    }
    if (ImGui::BeginCombo("Backend", scene.backend() == sim::Backend::CPU ? "CPU" : "GPU compute")) {
        for (const auto backend : {sim::Backend::CPU, sim::Backend::GPU}) {
            ImGui::BeginDisabled(backend == sim::Backend::GPU && scene.gpuSimulation() == nullptr);
            if (ImGui::Selectable(backend == sim::Backend::CPU ? "CPU" : "GPU compute",
                                  scene.backend() == backend)) {
                try {
                    scene.setBackend(backend);
                    controls.preferGpu = backend == sim::Backend::GPU;
                    controls.backendError.clear();
                    actions.skipElapsed = true;
                } catch (const std::exception& error) {
                    controls.backendError = error.what();
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    if (!controls.backendError.empty()) ImGui::TextWrapped("%s", controls.backendError.c_str());
    if (config) {
        if (!config->lastError().empty()) ImGui::TextWrapped("Settings: %s", config->lastError().c_str());
        if (!config->warnings().empty() && ImGui::TreeNode("Settings warnings")) {
            for (const auto& warning : config->warnings()) ImGui::TextWrapped("%s", warning.c_str());
            ImGui::TreePop();
        }
    }
    auto& simulation = scene.currentSimulation();
    int count = static_cast<int>(simulation.count());
    if (ImGui::SliderInt("Agents", &count, 0, 100000, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        simulation.setCount(static_cast<std::size_t>(std::clamp(count, 0, 100000)));
    }
    bool paused = scene.paused();
    if (ImGui::Checkbox("Paused", &paused)) {
        scene.setPaused(paused);
        actions.skipElapsed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!scene.paused());
    actions.step = ImGui::Button("Step");
    ImGui::EndDisabled();
    ImGui::SameLine();
    actions.reset = ImGui::Button("Reset");
    ImGui::Text("Simulation: %.2f s | fixed 120 Hz", scene.elapsedSimulationSeconds());

    if (ImGui::BeginTabBar("Controls")) {
        if (ImGui::BeginTabItem("Behavior")) {
            if (ImGui::BeginTable("Rules", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Rule", ImGuiTableColumnFlags_WidthFixed, 78.0F);
                ImGui::TableSetupColumn("Weight");
                ImGui::TableSetupColumn("Radius");
                ImGui::TableHeadersRow();
                for (auto& rule : simulation.rules()) {
                    ImGui::PushID(rule.get());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const auto name = rule->name();
                    ImGui::TextUnformatted(name.data(), name.data() + name.size());
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1);
                    float weight = rule->weight();
                    if (slider("##Weight", weight, 0.0F, 5.0F)) rule->setWeight(weight);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1);
                    float radius = rule->radius();
                    if (slider("##Radius", radius, 0.0F, 15.0F)) rule->setRadius(radius);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("Weight 0 disables a rule.");
            auto limits = simulation.motionLimits();
            bool changed = slider("Min speed", limits.minimumSpeed, 0.0F, limits.maximumSpeed);
            changed |= slider("Max speed", limits.maximumSpeed,
                              std::max(0.1F, limits.minimumSpeed), 30.0F);
            changed |= slider("Max accel", limits.maximumAcceleration, 0.0F, 60.0F);
            if (changed) simulation.setMotionLimits(limits);
            int boundary = simulation.world().boundary == sim::BoundaryMode::Wrap ? 0 : 1;
            if (ImGui::Combo("Boundary", &boundary, "Wrap\0Bounce\0")) {
                auto world = simulation.world();
                world.boundary = boundary == 0 ? sim::BoundaryMode::Wrap : sim::BoundaryMode::Bounce;
                simulation.setWorld(world);
            }
            ImGui::Separator();
            ImGui::BeginDisabled(scene.backend() == sim::Backend::GPU);
            ImGui::Checkbox("Cursor force", &controls.cursor);
            ImGui::BeginDisabled(!controls.cursor);
            ImGui::RadioButton("Repel", &controls.cursorMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Attract", &controls.cursorMode, 1);
            slider("Force radius", controls.radius, 0.0F, 20.0F);
            slider("Force strength", controls.strength, 0.0F, 100.0F);
            slider("Plane height", controls.planeHeight,
                   simulation.world().minimum.y + 1.0F, simulation.world().maximum.y - 1.0F);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (scene.backend() == sim::Backend::GPU) ImGui::TextDisabled("Cursor force is available on CPU.");
            ImGui::TextWrapped("Hover over the world to apply the force. The rings show its 3D radius on the selected height plane.");
            ImGui::TextDisabled("Force pauses over UI and during camera input.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("View")) {
            const auto samples = renderer.supportedAntialiasingSampleCount();
            ImGui::BeginDisabled(samples == VK_SAMPLE_COUNT_1_BIT);
            ImGui::Checkbox("Anti-aliasing (MSAA)", &controls.antialiasing);
            ImGui::EndDisabled();
            if (samples == VK_SAMPLE_COUNT_1_BIT) ImGui::TextDisabled("MSAA unavailable on this device.");
            else ImGui::TextDisabled("MSAA: %ux when enabled", static_cast<unsigned>(samples));
            ImGui::Combo("Shape", &controls.shape, "Arrows\0Dots\0");
            slider("Size", controls.size, 0.1F, 1.5F);
            ImGui::Combo("Color", &controls.color, "Direction\0Speed\0Density\0Solid\0");
            if (controls.color == static_cast<int>(ColorMode::Density)) {
                ImGui::TextWrapped("Neighbors within 5 world units: blue = 0, red = 32 or more. Periodic seams are included.");
            }
            ImGui::Checkbox("Floor grid", &controls.grid);
            ImGui::Checkbox("World bounds", &controls.bounds);
            if (ImGui::Button("Reset camera")) camera.reset();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Left drag: orbit | Middle drag: pan");
    ImGui::TextDisabled("Wheel: zoom | Escape: close");
    ImGui::Text("%.1f FPS | simulation %.2f ms (%zu steps)", static_cast<double>(ImGui::GetIO().Framerate),
                static_cast<double>(controls.simulationMs), controls.simulationSteps);
    if (actions.reset || actions.skipElapsed || scene.paused()) {
        controls.simulationWarningUntil = 0.0;
    } else if (controls.simulationLimited) {
        // Sample the completed frame here, so even a step taking over a second
        // keeps the warning visible. Hide only after a full second of recovery.
        controls.simulationWarningUntil = ImGui::GetTime() + 1.0;
    }
    // Keep the footer geometry identical with and without a warning. Removing
    // this item each fast frame moves the footer and can toggle the scrollbar,
    // changing the width and wrapping of the entire control panel.
    constexpr const char* simulationWarning = "Simulation slowed to keep\ncontrols responsive.";
    if (ImGui::GetTime() < controls.simulationWarningUntil) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.35F, 1.0F), "%s", simulationWarning);
    } else {
        ImGui::Dummy(ImGui::CalcTextSize(simulationWarning));
    }
    ImGui::TextWrapped("%s", context.deviceName().c_str());
    ImGui::TextDisabled("Validation: %s", context.validationEnabled() ? "on" : "off");
    ImGui::End();
    return actions;
}

std::optional<glm::vec3> cursorTarget(const core::Camera& camera, const sim::World& world,
                                     const Controls& controls, float x, float y,
                                     float aspect, bool inputAllowed) {
    if (!controls.cursor || !inputAllowed) return std::nullopt;
    const auto hit = camera.screenToPlane(x, y, aspect,
        glm::vec3{0.0F, controls.planeHeight, 0.0F}, glm::vec3{0.0F, 1.0F, 0.0F});
    if (!hit) return std::nullopt;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        if ((*hit)[axis] < world.minimum[axis] || (*hit)[axis] >= world.maximum[axis]) {
            return std::nullopt;
        }
    }
    return hit;
}

void updateForces(CursorRepel& repel, CursorAttract& attract, const Controls& controls,
                   const std::optional<glm::vec3>& target) {
    for (CursorIntervention* force : {static_cast<CursorIntervention*>(&repel),
                                      static_cast<CursorIntervention*>(&attract)}) {
        force->setRadius(controls.radius);
        force->setStrength(controls.strength);
        if (target) force->setPosition(*target);
    }
    repel.setEnabled(target.has_value() && controls.cursorMode == 0);
    attract.setEnabled(target.has_value() && controls.cursorMode == 1);
}

// Screen overlay avoids mutable GPU line buffers. Three great circles indicate
// a spherical force; geometry is deliberately visible through the flock.
void drawCursor(const glm::vec3& target, const Controls& controls, const glm::mat4& matrix) {
    const auto display = ImGui::GetIO().DisplaySize;
    const auto project = [&](const glm::vec3& position) -> std::optional<ImVec2> {
        const auto clip = matrix * glm::vec4(position, 1.0F);
        if (clip.w <= 0.0F || clip.z < 0.0F || clip.z > clip.w) return std::nullopt;
        return ImVec2((clip.x / clip.w + 1.0F) * 0.5F * display.x,
                      (clip.y / clip.w + 1.0F) * 0.5F * display.y);
    };
    auto* draw = ImGui::GetBackgroundDrawList();
    const ImU32 color = controls.cursorMode == 0 ? IM_COL32(255, 155, 80, 210)
                                                : IM_COL32(80, 205, 255, 210);
    for (int plane = 0; plane < 3; ++plane) {
        std::optional<ImVec2> previous;
        for (int i = 0; i <= 64; ++i) {
            const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(i) / 64.0F;
            glm::vec3 offset{0.0F};
            offset[plane] = controls.radius * std::cos(angle);
            offset[(plane + 1) % 3] = controls.radius * std::sin(angle);
            auto point = project(target + offset);
            if (point && previous) draw->AddLine(*previous, *point, color, 1.3F);
            previous = point;
        }
    }
    if (const auto center = project(target)) {
        draw->AddCircleFilled(*center, 3.0F, color);
        draw->AddText(ImVec2(center->x + 8.0F, center->y), color,
                       controls.cursorMode == 0 ? "REPEL" : "ATTRACT");
    }
}

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void smokeSettings(std::size_t frame, BoidsScene& scene, Controls& controls,
                    core::Camera& camera, Actions& actions) {
    auto& simulation = scene.simulation();
    if (frame == 0) simulation.setCount(0);
    if (frame == 2) simulation.setCount(1);
    if (frame == 4) simulation.setCount(257);
    if (frame == 8) simulation.setCount(4096);
    if (frame == 10) simulation.setCount(96);
    if (frame == 12) simulation.setCount(512);
    if (frame == 14) scene.setPaused(true);
    if (frame == 17) actions.step = true;
    if (frame == 18) { actions.reset = true; camera.reset(); }
    if (frame == 19) scene.setPaused(false);
    if (frame == 20) {
        simulation.rules()[1]->setWeight(2.0F);
        simulation.rules()[2]->setRadius(7.0F);
        simulation.rules()[0]->setWeight(0.0F);
    }
    if (frame == 22) {
        simulation.setMotionLimits({2.0F, 6.0F, 12.0F});
        auto world = simulation.world();
        world.boundary = sim::BoundaryMode::Bounce;
        simulation.setWorld(world);
    }
    if (frame == 32) {
        simulation.rules()[0]->setWeight(2.0F);
        simulation.setMotionLimits({1.0F, 8.0F, 20.0F});
        auto world = simulation.world();
        world.boundary = sim::BoundaryMode::Wrap;
        simulation.setWorld(world);
        simulation.setCount(2048);
    }
    if (frame == 36) {
        // Regress the reported long-running case: tight local flocks greatly
        // increase neighbor counts even though the particle count is unchanged.
        std::vector<sim::Agent> clustered{simulation.agents().begin(), simulation.agents().end()};
        for (std::size_t i = 0; i < clustered.size(); ++i) {
            const auto group = i / 128;
            const glm::vec3 center{-15.0F + 10.0F * static_cast<float>(group % 4),
                -12.0F + 24.0F * static_cast<float>(group / 8),
                -12.0F + 24.0F * static_cast<float>((group / 4) % 2)};
            clustered[i].position = center + clustered[i].position * 0.025F;
        }
        simulation.setAgents(clustered);
    }
    controls.cursor = frame >= 24 && frame < 32;
    controls.cursorMode = frame < 28 ? 0 : 1;
    controls.color = static_cast<int>((frame / 8) % 4);
    controls.shape = frame >= 8 && frame < 24 ? 1 : 0;
    controls.grid = frame < 10 || frame >= 14;
    controls.bounds = frame < 12 || frame >= 16;
    if (frame == 6) { camera.orbit(50.0F, -15.0F); camera.zoom(1.0F); }
    if (frame == 12) camera.pan(20.0F, 10.0F, 540.0F);
}

void checkedSwitch(BoidsScene& scene, sim::Backend backend) {
    const auto before = scene.currentSimulation().readback();
    const auto steps = scene.totalSteps();
    const bool paused = scene.paused();
    scene.setBackend(backend);
    const auto after = scene.currentSimulation().readback();
    require(before.size() == after.size(), "Backend switch changed agent count");
    require(scene.totalSteps() == steps && scene.paused() == paused, "Backend switch changed clock or pause");
    for (std::size_t i = 0; i < before.size(); ++i) {
        require(before[i].id == after[i].id && before[i].color == after[i].color &&
                before[i].size == after[i].size &&
                glm::length(before[i].position - after[i].position) < 1.0e-5F &&
                glm::length(before[i].velocity - after[i].velocity) < 1.0e-5F,
                "Backend switch lost simulation state");
    }
}

void gpuSmokeSettings(std::size_t frame, BoidsScene& scene, Controls& controls, Actions& actions) {
    if (frame == 0) { checkedSwitch(scene, sim::Backend::GPU); scene.currentSimulation().setCount(0); }
    if (frame == 2) scene.currentSimulation().setCount(1);
    if (frame == 4) scene.currentSimulation().setCount(257);
    if (frame == 6 || frame == 30) checkedSwitch(scene, sim::Backend::CPU);
    if (frame == 7 || frame == 32) checkedSwitch(scene, sim::Backend::GPU);
    if (frame == 8) scene.currentSimulation().setCount(100000);
    if (frame == 10) scene.currentSimulation().setCount(96);
    if (frame == 12) scene.currentSimulation().setCount(512);
    if (frame == 14) scene.setPaused(true);
    if (frame == 17) actions.step = true;
    if (frame == 18) actions.reset = true;
    if (frame == 19) scene.setPaused(false);
    if (frame == 20) {
        scene.currentSimulation().rules()[0]->setWeight(0.0F);
        scene.currentSimulation().rules()[1]->setRadius(9.0F);
    }
    if (frame == 22 || frame == 34) {
        auto world = scene.currentSimulation().world();
        world.boundary = frame == 22 ? sim::BoundaryMode::Bounce : sim::BoundaryMode::Wrap;
        scene.currentSimulation().setWorld(world);
        scene.currentSimulation().setMotionLimits({2.0F, 6.0F, 12.0F});
    }
    if (frame == 28) scene.currentSimulation().setCount(0);
    if (frame == 31) scene.currentSimulation().setCount(2048);
    controls.cursor = frame >= 24 && frame < 28;
    controls.color = static_cast<int>((frame / 4) % 4);
    controls.shape = static_cast<int>((frame / 8) % 2);
}

} // namespace

int BoidsApp::run(const std::filesystem::path& shaderDirectory, bool smokeTest, bool gpuSmokeTest,
                  config::ConfigFile* config, bool startAnts, bool antsSmokeTest) {
    const bool anySmokeTest = smokeTest || gpuSmokeTest || antsSmokeTest;
    if (anySmokeTest) config = nullptr;
    core::Window window{1280, 800, "Emergences | Boids & Ant Colony"};
    if (config) window.setFullscreen(config->settings().view.fullscreen);
    core::Context context{window};
    std::cout << "Vulkan initialized on: " << context.deviceName() << '\n';
    std::cout << "Validation layer: " << (context.validationEnabled() ? "enabled" : "disabled") << '\n';
#if !defined(NDEBUG)
    if (anySmokeTest && !context.validationEnabled()) {
        throw std::runtime_error("Debug smoke test requires VK_LAYER_KHRONOS_validation");
    }
#endif
    std::size_t presentedFrames = 0;
    std::size_t generations = 0;
    bool observedMotion = false;
    bool observedRepel = false;
    bool observedAttract = false;
    bool observedGpuMotion = false;
    bool observedAntMotion = false;
    bool observedAntTrail = false;
    bool observedFoodTrail = false;
    bool observedSceneSwitch = false;
    std::uint64_t inactiveAntSteps = 0;
    {
        core::VulkanComputeDevice compute{context};
        core::ParticleRenderer particles{context, shaderDirectory};
        core::ParticleRenderer environmentParticles{context, shaderDirectory};
        core::LineRenderer lines{context, shaderDirectory};
        core::LineRenderer antLines{context, shaderDirectory, true, ants::AntScene::WorldHalfExtent};
        core::ImGuiLayer ui{context, window};
        // Submission owner dies first, waiting while all recorded resources live.
        core::FrameRenderer renderer{context, window};
        core::Camera camera;
        BoidsScene scene;
        ants::AntScene antScene;
        Controls controls;
        if (config) {
            const auto& settings = config->settings();
            controls.scenario = settings.scenario;
            controls.ants = settings.ants;
            ants::applyAntSettings(antScene, settings.ants);
            applyBoidsSettings(scene, settings.boids);
            controls.preferGpu = settings.boids.gpu;
            controls.panelVisible = settings.view.panelVisible;
            controls.shape = settings.view.agentShape;
            controls.color = settings.view.colorMode;
            controls.size = settings.view.agentSize;
            controls.grid = settings.view.grid;
            controls.bounds = settings.view.bounds;
            controls.antialiasing = settings.view.antialiasing;
            controls.cursor = settings.boids.cursor;
            controls.cursorMode = settings.boids.cursorAttract ? 1 : 0;
            controls.radius = settings.boids.cursorRadius;
            controls.strength = settings.boids.cursorStrength;
            controls.planeHeight = settings.boids.cursorPlaneHeight;
            const auto& savedCamera = settings.camera;
            camera.setState({{savedCamera.targetX, savedCamera.targetY, savedCamera.targetZ},
                             savedCamera.yaw, savedCamera.pitch, savedCamera.distance});
        }
        if (startAnts || antsSmokeTest) {
            if (controls.scenario != 1 || !config) ants::resetAntCamera(camera);
            controls.scenario = 1;
        }
        try {
            scene.initializeGpu(compute, (shaderDirectory / "compute/boids.comp.spv").string());
            // Keep the preference when GPU initialization fails, so a temporary
            // device problem does not permanently change the saved backend.
            if (!smokeTest && controls.preferGpu) scene.setBackend(sim::Backend::GPU);
        } catch (const std::exception& error) {
            if (gpuSmokeTest) throw;
            controls.backendError = std::string{"GPU compute unavailable: "} + error.what();
        }
        if (gpuSmokeTest) require(scene.backend() == sim::Backend::GPU, "GPU must be the default backend");
        auto repelOwner = std::make_unique<CursorRepel>();
        auto attractOwner = std::make_unique<CursorAttract>();
        auto& repel = *repelOwner;
        auto& attract = *attractOwner;
        scene.simulation().interventions().push_back(std::move(repelOwner));
        scene.simulation().interventions().push_back(std::move(attractOwner));
        std::string reportedConfigError;
        const auto saveSettings = [&] {
            if (!config) return;
            auto& settings = config->settings();
            settings.scenario = controls.scenario;
            ants::captureAntSettings(antScene, controls.ants);
            settings.ants = controls.ants;
            captureBoidsSettings(scene, settings.boids);
            settings.boids.gpu = controls.preferGpu;
            settings.boids.cursor = controls.cursor;
            settings.boids.cursorAttract = controls.cursorMode == 1;
            settings.boids.cursorRadius = controls.radius;
            settings.boids.cursorStrength = controls.strength;
            settings.boids.cursorPlaneHeight = controls.planeHeight;
            settings.view.fullscreen = window.isFullscreen();
            settings.view.panelVisible = controls.panelVisible;
            settings.view.agentShape = controls.shape;
            settings.view.colorMode = controls.color;
            settings.view.agentSize = controls.size;
            settings.view.grid = controls.grid;
            settings.view.bounds = controls.bounds;
            settings.view.antialiasing = controls.antialiasing;
            const auto state = camera.state();
            settings.camera = {state.target.x, state.target.y, state.target.z,
                               state.yaw, state.pitch, state.distance};
            if (!config->save() && config->lastError() != reportedConfigError) {
                std::cerr << "[Config] " << config->lastError() << '\n';
            }
            reportedConfigError = config->lastError();
        };
        saveSettings();
        std::vector<core::Particle> renderParticles;
        std::vector<core::Particle> antEnvironment;
        auto previous = std::chrono::steady_clock::now();
        auto nextSettingsSave = previous + std::chrono::seconds{1};
        const auto deadline = previous + std::chrono::seconds{55};
        while (!window.shouldClose()) {
            window.pollEvents();
            if (window.shouldClose()) break;
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - previous).count();
            previous = now;
            if (anySmokeTest && now >= deadline) throw std::runtime_error("Scene smoke test timed out");
            if (anySmokeTest) controls.antialiasing = (presentedFrames >= 2 && presentedFrames < 16) || presentedFrames >= 20;
            renderer.setAntialiasing(controls.antialiasing);
            bool fullscreen = window.isFullscreen();
            const bool presented = renderer.drawFrame(
                [&](VkRenderPass pass, std::uint32_t imageCount) {
                    particles.configure(pass, renderer.sampleCount());
                    environmentParticles.configure(pass, renderer.sampleCount());
                    lines.configure(pass, renderer.sampleCount());
                    antLines.configure(pass, renderer.sampleCount());
                    ui.configure(pass, imageCount, renderer.sampleCount());
                },
                [&](VkCommandBuffer commands, VkExtent2D extent, std::size_t frame) {
                    if (anySmokeTest) {
                        if (presentedFrames == 10 || presentedFrames == 11 || presentedFrames == 14)
                            ImGui::GetIO().AddKeyEvent(ImGuiKey_F1, true);
                        if (presentedFrames == 12 || presentedFrames == 15)
                            ImGui::GetIO().AddKeyEvent(ImGuiKey_F1, false);
                    }
                    ui.beginFrame();
                    bool panelJustShown = false;
                    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
                        controls.panelVisible = !controls.panelVisible;
                        panelJustShown = controls.panelVisible;
                    }
                    if (anySmokeTest) {
                        require(controls.panelVisible == (presentedFrames < 10 || presentedFrames >= 14),
                                "F1 must hide and restore the panel without repeating while held");
                        require(renderer.sampleCount() == (controls.antialiasing
                            ? renderer.supportedAntialiasingSampleCount() : VK_SAMPLE_COUNT_1_BIT),
                            "Boids MSAA toggle did not select the expected sample count");
                    }
                    Actions actions;
                    if (smokeTest) smokeSettings(presentedFrames, scene, controls, camera, actions);
                    if (gpuSmokeTest) gpuSmokeSettings(presentedFrames, scene, controls, actions);
                    if (antsSmokeTest) {
                        if (presentedFrames == 1) antScene.setCount(0);
                        if (presentedFrames == 2) antScene.setCount(1);
                        if (presentedFrames == 3) antScene.setCount(257);
                        if (presentedFrames == 4) antScene.setCount(1500);
                        if (presentedFrames == 8) antScene.setPaused(true);
                        if (presentedFrames == 9) actions.step = true;
                        if (presentedFrames == 10) antScene.setPaused(false);
                        if (presentedFrames == 12) { antScene.randomize(729U); actions.skipElapsed = true; }
                        if (presentedFrames == 18) antScene.clearPheromones();
                        if (presentedFrames == 24) actions.reset = true;
                        if (presentedFrames == 30) {
                            // Exercise mature foraging trails as well as the empty initial field.
                            const auto foodBefore = antScene.totalGeneratedFood();
                            for (int tick = 0; tick < 180; ++tick) antScene.advance(1.0 / 15.0);
                            require(antScene.totalGeneratedFood() > foodBefore,
                                "Ant world must generate food periodically without resetting the colony");
                        }
                        if (presentedFrames == 32) {
                            inactiveAntSteps = antScene.totalSteps();
                            controls.scenario = 0;
                            scene.setPaused(true);
                        }
                        if (presentedFrames == 36) {
                            require(antScene.totalSteps() == inactiveAntSteps, "Inactive ants must not advance");
                            controls.scenario = 1;
                            observedSceneSwitch = true;
                        }
                        controls.ants.showHomePheromone = presentedFrames < 26 || presentedFrames >= 30;
                        controls.ants.showFoodPheromone = presentedFrames < 28 || presentedFrames >= 30;
                    }
                    if (controls.panelVisible) {
                        const auto panelActions = drawPanel(scene, antScene, controls, camera, context, renderer, fullscreen, config);
                        actions.reset |= panelActions.reset;
                        actions.step |= panelActions.step;
                        actions.skipElapsed |= panelActions.skipElapsed;
                    }
                    // Exercise the same deferred fullscreen request as the checkbox.
                    if (anySmokeTest && presentedFrames == 16) fullscreen = true;
                    if (anySmokeTest && presentedFrames == 20) fullscreen = false;
                    const auto& io = ImGui::GetIO();
                    // Capture is computed before this frame's windows are drawn.
                    const bool mouseCaptured = panelJustShown || (controls.panelVisible && ui.wantsMouse());
                    const bool cameraInput = ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                        ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ui.wheelDelta() != 0.0F;
                    if (!mouseCaptured && !anySmokeTest) {
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) camera.pan(io.MouseDelta.x, io.MouseDelta.y, io.DisplaySize.y);
                        camera.zoom(ui.wheelDelta());
                    }
                    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                    if (controls.scenario == 1) {
                        repel.setEnabled(false);
                        attract.setEnabled(false);
                        const auto stepsBefore = antScene.totalSteps();
                        const auto oldAgents = antScene.agents();
                        const auto firstPosition = oldAgents.empty() ? glm::vec3{0} : oldAgents.front().position;
                        const auto started = std::chrono::steady_clock::now();
                        controls.simulationSteps = 0;
                        controls.simulationLimited = false;
                        if (actions.reset) antScene.reset();
                        else {
                            if (actions.step && antScene.singleStep()) ++controls.simulationSteps;
                            if (!actions.skipElapsed) {
                                const double budget = antsSmokeTest ? (presentedFrames >= 40 ? 0.0 :
                                    std::numeric_limits<double>::infinity()) : 0.008;
                                controls.simulationSteps += antScene.advance(antsSmokeTest ? 1.0 / 60.0 : elapsed, budget);
                                controls.simulationLimited = antScene.lastAdvanceLimited();
                            }
                        }
                        controls.simulationMs = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - started).count();
                        ants::makeAntParticles(antScene, controls.ants, renderParticles, antEnvironment);
                        if (antsSmokeTest) {
                            require(renderParticles.size() == antScene.count(), "Ant render count mismatch");
                            require(!antEnvironment.empty(), "Ant world objects were not rendered");
                            if (antScene.paused() && !actions.step && !actions.reset)
                                require(antScene.totalSteps() == stepsBefore, "Paused ants advanced");
                            if (actions.step) require(antScene.totalSteps() == stepsBefore + 1, "Ant single step failed");
                            if (actions.reset) require(antScene.totalSteps() == 0, "Ant reset clock failed");
                            if (presentedFrames >= 40) require(controls.simulationSteps == 1 && controls.simulationLimited,
                                "Ant frame budget failed");
                            if (!antScene.agents().empty() && !actions.reset && antScene.totalSteps() > stepsBefore)
                                observedAntMotion |= antScene.agents().front().position != firstPosition;
                            for (float value : antScene.homePheromone()) observedAntTrail |= value > 0.0F;
                            for (float value : antScene.foodPheromone()) observedFoodTrail |= value > 0.0F;
                        }
                        core::SceneView view;
                        view.viewProjection = camera.viewProjection(aspect);
                        view.right = glm::vec4(camera.right(), 0);
                        view.up = glm::vec4(camera.up(), 0);
                        view.options = {1.0F, 1.0F, 0, 0};
                        ui.endFrame();
                        if (antsSmokeTest && !controls.panelVisible)
                            require(ImGui::GetDrawData()->TotalVtxCount == 0, "Hidden ant panel left UI commands");
                        antLines.draw(commands, view, controls.grid, controls.bounds);
                        environmentParticles.draw(commands, frame, antEnvironment, view);
                        view.options = {controls.ants.agentSize, 2.0F, 0, 0};
                        particles.draw(commands, frame, renderParticles, view);
                        ui.record(commands);
                        return;
                    }
                    const bool inputAllowed = !mouseCaptured && !cameraInput &&
                        ImGui::IsMousePosValid() && glfwGetWindowAttrib(window.nativeHandle(), GLFW_HOVERED) &&
                        glfwGetWindowAttrib(window.nativeHandle(), GLFW_FOCUSED);
                    const auto target = cursorTarget(camera, scene.currentSimulation().world(), controls,
                        smokeTest ? 0.5F : io.MousePos.x / io.DisplaySize.x,
                        smokeTest ? 0.5F : io.MousePos.y / io.DisplaySize.y, aspect,
                        scene.backend() == sim::Backend::CPU && (smokeTest ? presentedFrames != 26 : inputAllowed));
                    updateForces(repel, attract, controls, target);
                    const auto stepsBefore = scene.totalSteps();
                    const auto oldAgents = scene.currentSimulation().agents();
                    const glm::vec3 firstPosition = oldAgents.empty() ? glm::vec3{0.0F} : oldAgents.front().position;
                    // For each force mode, compare the actual scene trajectory
                    // to identical rules/state without interventions. This checks
                    // the app's wiring, beyond merely checking enable flags.
                    std::optional<BoidsScene> unforced;
                    if (smokeTest && (presentedFrames == 24 || presentedFrames == 28)) {
                        unforced.emplace(0);
                        auto& reference = unforced->simulation();
                        auto& actual = scene.simulation();
                        reference.setWorld(actual.world());
                        reference.setMotionLimits(actual.motionLimits());
                        reference.setAgents(oldAgents);
                        for (std::size_t i = 0; i < actual.rules().size(); ++i) {
                            reference.rules()[i]->setWeight(actual.rules()[i]->weight());
                            reference.rules()[i]->setRadius(actual.rules()[i]->radius());
                        }
                        unforced->advance(1.0 / 60.0);
                    }
                    const auto simulationStart = std::chrono::steady_clock::now();
                    controls.simulationSteps = 0;
                    controls.simulationLimited = false;
                    if (actions.reset) scene.reset();
                    else {
                        if (actions.step && scene.singleStep()) ++controls.simulationSteps;
                        if (!actions.skipElapsed) {
                            // Interactive catch-up gets 8 ms, checked between
                            // complete fixed steps. A slow step must not turn
                            // into eight slow steps before the next UI frame.
                            // Zero budget in the last smoke frames reliably
                            // exercises overload without machine-speed asserts.
                            const double workBudget = anySmokeTest
                                ? (presentedFrames >= 40 ? 0.0 : std::numeric_limits<double>::infinity())
                                : 0.008;
                            controls.simulationSteps += scene.advance(anySmokeTest ? 1.0 / 60.0 : elapsed, workBudget);
                            controls.simulationLimited = scene.lastAdvanceLimited();
                        }
                    }
                    controls.simulationMs = std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - simulationStart).count();
                    const auto agents = scene.refreshColors(static_cast<ColorMode>(controls.color));
                    if (gpuSmokeTest) {
                        if (scene.backend() == sim::Backend::GPU) {
                            require(agents.empty() && scene.currentSimulation().agents().empty(),
                                    "GPU rendering must not implicitly read back agents");
                            require(!repel.enabled() && !attract.enabled(), "GPU cursor force was enabled");
                        }
                        if (scene.paused() && !actions.step && !actions.reset)
                            require(scene.totalSteps() == stepsBefore, "Paused GPU advanced");
                        if (actions.step) require(scene.totalSteps() == stepsBefore + 1, "GPU single step failed");
                        if (actions.reset) require(scene.totalSteps() == 0, "GPU reset clock failed");
                        if (presentedFrames == 5) {
                            const auto state = scene.currentSimulation().readback();
                            require(state.size() == 257, "GPU smoke count mismatch");
                            sim::cpu::CpuSimulation initial{257, scene.currentSimulation().world()};
                            observedGpuMotion = state.front().position != initial.agents().front().position;
                            require(observedGpuMotion, "GPU state did not advance");
                        }
                    }
                    renderParticles.resize(agents.size());
                    for (std::size_t i = 0; i < agents.size(); ++i) {
                        const auto& agent = agents[i];
                        renderParticles[i] = {agent.position, agent.size, agent.velocity, 0.0F, agent.color, agent.id};
                    }
                    if (smokeTest) {
                        require(renderParticles.size() == scene.simulation().count(), "Boids render count mismatch");
                        if (scene.paused() && !actions.step && !actions.reset) {
                            require(scene.totalSteps() == stepsBefore, "Paused Boids advanced");
                        }
                        if (actions.reset) require(scene.totalSteps() == 0, "Boids reset did not clear time");
                        if (actions.step) require(scene.totalSteps() == stepsBefore + 1, "Boids single step failed");
                        if (presentedFrames >= 40) {
                            require(controls.simulationSteps == 1 && controls.simulationLimited,
                                    "Dense Boids must discard catch-up work when the frame budget is exhausted");
                        }
                        if (!agents.empty() && !actions.reset && scene.totalSteps() > stepsBefore) {
                            observedMotion |= glm::length(agents.front().position - firstPosition) > 0.0F;
                        }
                        if (unforced) {
                            bool changed = false;
                            const auto reference = unforced->simulation().agents();
                            for (std::size_t i = 0; i < agents.size(); ++i) {
                                changed |= agents[i].velocity != reference[i].velocity;
                            }
                            require(changed, "Cursor force did not change the live Boids trajectory");
                            observedRepel |= repel.enabled();
                            observedAttract |= attract.enabled();
                        }
                        if (presentedFrames == 26) require(!repel.enabled() && !attract.enabled(), "Cursor capture did not suppress forces");
                    }
                    core::SceneView view;
                    view.viewProjection = camera.viewProjection(aspect);
                    view.right = glm::vec4(camera.right(), 0.0F);
                    view.up = glm::vec4(camera.up(), 0.0F);
                    view.options = {controls.size, static_cast<float>(controls.shape), 0.0F, 0.0F};
                    if (target) drawCursor(*target, controls, view.viewProjection);
                    ui.endFrame();
                    if (anySmokeTest && !controls.panelVisible) {
                        require(ImGui::GetDrawData()->TotalVtxCount == 0,
                                "Hidden controls must not leave UI draw commands");
                    }
                    lines.draw(commands, view, controls.grid, controls.bounds);
                    if (scene.backend() == sim::Backend::GPU) {
                        const auto buffer = scene.gpuSimulation()->renderBuffer();
                        particles.drawBuffer(commands, frame, compute.buffer(buffer), compute.bufferSize(buffer),
                            static_cast<std::uint32_t>(scene.currentSimulation().count()), view);
                    } else {
                        particles.draw(commands, frame, renderParticles, view);
                    }
                    ui.record(commands);
                });
            // Finish submitting the current frame before changing the native surface.
            if (fullscreen != window.isFullscreen()) window.setFullscreen(fullscreen);
            if (config && now >= nextSettingsSave) {
                saveSettings();
                nextSettingsSave = now + std::chrono::seconds{1};
            }
            if (anySmokeTest && presented) {
                require(window.isFullscreen() == (presentedFrames >= 16 && presentedFrames < 20),
                        "Fullscreen request did not change the window mode");
            }
            if (!presented) {
                window.waitEvents();
                previous = std::chrono::steady_clock::now();
                continue;
            }
            ++presentedFrames;
            if (anySmokeTest) {
                if (presentedFrames == 4) window.resize(960, 640);
                else if (presentedFrames == 8) window.resize(1280, 800);
                else if (presentedFrames >= 48 && renderer.swapchainGeneration() >= 3) break;
            }
        }
        saveSettings();
        context.waitIdle();
        generations = renderer.swapchainGeneration();
    }
    if (context.validationErrorCount() != 0) {
        throw std::runtime_error("Vulkan validation reported " +
                                 std::to_string(context.validationErrorCount()) + " error(s)");
    }
    if (smokeTest) {
        require(presentedFrames >= 48 && generations >= 3 && observedMotion && observedRepel && observedAttract,
                "Boids smoke test closed before all checks completed");
        std::cout << "Boids smoke test passed: " << presentedFrames << " presented frames, "
                  << generations << " swapchain generations, counts 0/1/257/4096/96/512/2048, "
                     "fixed steps/pause/reset/rules/limits/boundaries/colors/cursor/dense budget/MSAA/fullscreen/F1 panel, 0 validation errors.\n";
    }
    if (gpuSmokeTest) {
        require(presentedFrames >= 48 && generations >= 3 && observedGpuMotion,
                "GPU Boids smoke test closed before all checks completed");
        std::cout << "GPU Boids smoke test passed: " << presentedFrames << " frames, " << generations
                  << " swapchain generations, CPU/GPU switches, 0/1/257/100000 agents, pause/step/reset, "
                     "colors/boundaries/MSAA/fullscreen/F1 panel, zero-copy rendering, 0 validation errors.\n";
    }
    if (antsSmokeTest) {
        require(presentedFrames >= 48 && generations >= 3 && observedAntMotion && observedAntTrail &&
            observedFoodTrail && observedSceneSwitch,
            "Ant smoke test closed before all checks completed");
        std::cout << "Ant smoke test passed: " << presentedFrames << " frames, " << generations
            << " swapchain generations, food/hostiles/trails, 0/1/257/1500 ants, pause/step/reset/seed, "
               "scene switches/budget/MSAA/fullscreen/F1 panel, 0 validation errors.\n";
    }
    return 0;
}

} // namespace emergences::scenarios::boids
