#include "scenarios/boids/BoidsApp.h"
#include "scenarios/boids/BoidsScene.h"
#include "scenarios/boids/BoidsConfig.h"
#include "scenarios/ants/AntScene.h"
#include "scenarios/ants/AntPresentation.h"
#include "scenarios/ants3d/Ant3DScene.h"
#include "scenarios/ants3d/Ant3DInteraction.h"
#include "scenarios/ants3d/Ant3DPresentation.h"
#include "core/MeshRenderer.h"
#include "scenarios/particle_life/ParticleLifeScene.h"
#include "scenarios/particle_life/ParticleLifePresentation.h"
#include "scenarios/slime_mold/SlimeMoldScene.h"
#include "scenarios/slime_mold/SlimeMoldPresentation.h"
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

const char* sceneWindowTitle(int scenario) {
    switch (scenario) {
    case 1: return EMERGENCES_WINDOW_TITLE_PREFIX "Ant Colony";
    case 2: return EMERGENCES_WINDOW_TITLE_PREFIX "Particle Life";
    case 3: return EMERGENCES_WINDOW_TITLE_PREFIX "Ant Colony 3D";
    case 4: return EMERGENCES_WINDOW_TITLE_PREFIX "Slime Mold";
    default: return EMERGENCES_WINDOW_TITLE_PREFIX "Boids";
    }
}

struct Controls {
    int scenario = 0;
    config::AntSettings ants;
    config::Ant3DSettings ants3d;
    ants3d::Ant3DInteraction ant3dInteraction;
    int antScenario = 1;
    std::optional<core::Camera::State> ants2dCamera;
    std::optional<core::Camera::State> ants3dCamera;
    config::ParticleLifeSettings particleLife;
    config::SlimeMoldSettings slimeMold;
    std::string slimeMoldBackendError;
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
    std::string particleLifeBackendError;
};

struct Actions {
    bool reset = false;
    bool step = false;
    bool skipElapsed = false;
};

// Scene and Dimensions share one transition path. The two colonies retain
// their own simulation objects and camera poses while the UI groups them together.
bool selectScenario(Controls& controls, core::Camera& camera, int scenario,
                    bool particleLife3d) {
    if (scenario == controls.scenario) return false;
    if (controls.scenario == 1) controls.ants2dCamera = camera.state();
    if (controls.scenario == 3) {
        controls.ant3dInteraction.stopFollowing(camera);
        controls.ants3dCamera = camera.state();
    }
    controls.scenario = scenario;
    if (scenario == 1 || scenario == 3) {
        controls.antScenario = scenario;
        const auto& saved = scenario == 1 ? controls.ants2dCamera : controls.ants3dCamera;
        if (saved) camera.setState(*saved);
        else if (scenario == 1) ants::resetAntCamera(camera);
        else ants3d::resetAnt3DCamera(camera);
    } else if (scenario == 4) {
        slime_mold::resetSlimeMoldCamera(camera);
    } else if (scenario == 2) {
        particle_life::resetParticleLifeCamera(camera, particleLife3d);
    } else {
        camera.reset();
    }
    return true;
}

// Clamp text entry as well as dragging, and keep nonfinite text out of the model.
bool slider(const char* label, float& value, float minimum, float maximum) {
    const float old = value;
    if (!ImGui::SliderFloat(label, &value, minimum, maximum, "%.2f",
                            ImGuiSliderFlags_AlwaysClamp)) return false;
    value = std::isfinite(value) ? std::clamp(value, minimum, maximum) : old;
    return value != old;
}

Actions drawPanel(BoidsScene& scene, ants::AntScene& antScene, ants3d::Ant3DScene& ant3dScene,
                  particle_life::ParticleLifeScene& particleLifeScene, slime_mold::SlimeMoldScene& slimeMoldScene,
                  Controls& controls, core::Camera& camera,
                  const core::Context& context, const core::FrameRenderer& renderer,
                  bool& fullscreen, const config::ConfigFile* config) {
    Actions actions;
    ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, std::max(180.0F,
        std::min(690.0F, ImGui::GetIO().DisplaySize.y - 36.0F))), ImGuiCond_Always);
    ImGui::Begin("Emergences", nullptr, ImGuiWindowFlags_NoResize);
    ImGui::TextColored(ImVec4(0.3F, 0.8F, 0.95F, 1.0F), "%s",
        controls.scenario == 1 ? "ANT COLONY / LIVE ECOSYSTEM" :
        controls.scenario == 2 ? "PARTICLE LIFE / EMERGENT SYSTEMS" :
        controls.scenario == 3 ? "ANT COLONY / 3D TERRAIN" :
        controls.scenario == 4 ? "SLIME MOLD / LIVING NETWORKS" : "LIVE BOIDS");
    ImGui::Checkbox("Fullscreen (F11)", &fullscreen);
    ImGui::SameLine();
    ImGui::TextDisabled("F1: hide panel");
    int selectedScene = controls.scenario == 3 ? 1 : controls.scenario == 4 ? 3 : controls.scenario;
    if (ImGui::Combo("Scene", &selectedScene, "Boids\0Ant Colony\0Particle Life\0Slime Mold\0")) {
        actions.skipElapsed |= selectScenario(controls, camera,
            selectedScene == 1 ? controls.antScenario : selectedScene == 3 ? 4 : selectedScene,
            particleLifeScene.threeDimensional());
    }
    if (controls.scenario == 1 || controls.scenario == 3) {
        int dimensions = controls.scenario == 3 ? 1 : 0;
        if (ImGui::Combo("Dimensions", &dimensions, "2D plane\0" "3D terrain\0")) {
            actions.skipElapsed |= selectScenario(controls, camera,
                dimensions == 1 ? 3 : 1, particleLifeScene.threeDimensional());
        }
        ImGui::TextDisabled("Each dimension keeps its colony and camera.");
    }
    if (controls.scenario != 0) {
        if (config) {
            if (!config->lastError().empty()) ImGui::TextWrapped("Settings: %s", config->lastError().c_str());
            if (!config->warnings().empty() && ImGui::TreeNode("Settings warnings")) {
                for (const auto& warning : config->warnings()) ImGui::TextWrapped("%s", warning.c_str());
                ImGui::TreePop();
            }
        }
        if (controls.scenario == 1) {
            const auto panel = ants::drawAntControls(antScene, controls.ants, camera, renderer,
                controls.antialiasing, controls.grid, controls.bounds);
            actions.reset = panel.reset;
            actions.step = panel.step;
            actions.skipElapsed |= panel.skipElapsed;
        } else if (controls.scenario == 3) {
            const auto panel = ants3d::drawAnt3DControls(ant3dScene, controls.ants3d, camera, renderer,
                controls.antialiasing, controls.ant3dInteraction.selectedAnt(),
                controls.ant3dInteraction.following(), controls.ant3dInteraction.followView());
            if (panel.cameraChanged) controls.ant3dInteraction.stopFollowing(camera, false);
            if (panel.reset || panel.worldReset) controls.ant3dInteraction.clear(camera, !panel.cameraChanged);
            actions.reset = panel.reset;
            actions.step = panel.step;
            actions.skipElapsed |= panel.skipElapsed;
        } else if (controls.scenario == 4) {
            const auto panel = slime_mold::drawSlimeMoldControls(slimeMoldScene, controls.slimeMold,
                camera, controls.antialiasing, controls.slimeMoldBackendError);
            actions.reset = panel.reset;
            actions.step = panel.step;
            actions.skipElapsed |= panel.skipElapsed;
        } else {
            const auto panel = particle_life::drawParticleLifeControls(particleLifeScene,
                controls.particleLife, camera, renderer, controls.antialiasing, controls.grid, controls.bounds,
                controls.particleLifeBackendError);
            actions.reset = panel.reset;
            actions.step = panel.step;
            actions.skipElapsed |= panel.skipElapsed;
        }
        ImGui::Separator();
        ImGui::TextDisabled(controls.scenario == 3 ?
            "Left/right drag: look | Middle: pan" : "Left drag: orbit | Middle drag: pan");
        if (controls.scenario == 3) {
            ImGui::TextDisabled("WASD: move");
            ImGui::TextDisabled("Q/E: down/up | Shift: faster");
        }
        ImGui::TextDisabled(controls.scenario == 3 && controls.ant3dInteraction.following() ?
            "Space / Escape: stop follow | F2: view" : "Wheel: zoom | Escape: close");
        ImGui::Text("%.1f FPS | simulation %.2f ms (%zu steps)", static_cast<double>(ImGui::GetIO().Framerate),
            static_cast<double>(controls.simulationMs), controls.simulationSteps);
        const bool paused = controls.scenario == 1 ? antScene.paused() :
            controls.scenario == 3 ? ant3dScene.paused() :
            controls.scenario == 4 ? slimeMoldScene.paused() : particleLifeScene.paused();
        if (controls.simulationLimited && !paused)
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

void checkedParticleLifeSwitch(particle_life::ParticleLifeScene& scene, sim::Backend backend) {
    const auto before = scene.readback();
    const auto steps = scene.totalSteps();
    const bool paused = scene.paused();
    const bool threeDimensional = scene.threeDimensional();
    scene.setBackend(backend);
    const auto after = scene.readback();
    require(before.size() == after.size(), "Particle Life backend switch changed count");
    require(scene.totalSteps() == steps && scene.paused() == paused,
        "Particle Life backend switch changed clock or pause");
    require(scene.threeDimensional() == threeDimensional,
        "Particle Life backend switch changed dimensions");
    for (std::size_t i = 0; i < before.size(); ++i) {
        require(before[i].position == after[i].position && before[i].velocity == after[i].velocity &&
            before[i].id == after[i].id && before[i].color == after[i].color &&
            before[i].size == after[i].size && before[i].reserved == after[i].reserved,
            "Particle Life backend switch lost simulation state");
    }
}

void checkedParticleLifeDimensions(particle_life::ParticleLifeScene& scene, bool threeDimensional,
                                  core::Camera& camera) {
    const auto matrix = scene.matrix();
    const auto parameters = scene.parameters();
    const auto backend = scene.backend();
    const auto count = scene.count();
    const auto species = scene.speciesCount();
    const auto seed = scene.seed();
    const auto paused = scene.paused();
    scene.setThreeDimensional(threeDimensional);
    require(scene.threeDimensional() == threeDimensional && scene.totalSteps() == 0 &&
        scene.backend() == backend && scene.count() == count && scene.speciesCount() == species &&
        scene.seed() == seed && scene.paused() == paused && scene.matrix() == matrix,
        "Particle Life dimension switch lost state or failed to reset its clock");
    const auto& updated = scene.parameters();
    require(updated.interactionRadius == parameters.interactionRadius && updated.force == parameters.force &&
        updated.friction == parameters.friction && updated.repulsion == parameters.repulsion &&
        updated.coreRatio == parameters.coreRatio && updated.maxSpeed == parameters.maxSpeed,
        "Particle Life dimension switch changed parameters");
    const auto state = scene.readback();
    require(threeDimensional ? std::any_of(state.begin(), state.end(), [](const auto& agent) {
        return agent.position.y != 0.0F;
    }) : std::all_of(state.begin(), state.end(), [](const auto& agent) {
        return agent.position.y == 0.0F && agent.velocity.y == 0.0F;
    }), "Particle Life dimension switch did not regenerate the expected volume or plane");
    particle_life::resetParticleLifeCamera(camera, threeDimensional);
}

} // namespace

int BoidsApp::run(const std::filesystem::path& shaderDirectory, bool smokeTest, bool gpuSmokeTest,
                  config::ConfigFile* config, bool startAnts, bool antsSmokeTest,
                  bool startParticleLife, bool particleLifeSmokeTest, bool startParticleLife3d,
                  bool startAnts3d, bool ants3dSmokeTest, bool startSlimeMold, bool slimeMoldSmokeTest) {
    const bool anySmokeTest = smokeTest || gpuSmokeTest || antsSmokeTest || particleLifeSmokeTest || ants3dSmokeTest || slimeMoldSmokeTest;
    if (anySmokeTest) config = nullptr;
    int titledScenario = startSlimeMold || slimeMoldSmokeTest ? 4 : startAnts3d || ants3dSmokeTest ? 3 : startParticleLife || particleLifeSmokeTest || startParticleLife3d ? 2 :
        startAnts || antsSmokeTest ? 1 : config ? config->settings().scenario : 0;
    core::Window window{1280, 800, sceneWindowTitle(titledScenario)};
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
    bool observedAnt3dMotion = false;
    std::uint64_t inactiveAnt3dSteps = 0;
    bool observedAntTrail = false;
    bool observedFoodTrail = false;
    bool observedSceneSwitch = false;
    std::uint64_t inactiveAntSteps = 0;
    bool observedParticleLifeMotion = false;
    bool observedParticleLifeGpuMotion = false;
    bool observedParticleLife3dGpuMotion = false;
    bool observedParticleLife3dWrap = false;
    bool observedParticleLife2dReturn = false;
    std::uint64_t inactiveParticleLifeSteps = 0;
    std::uint64_t inactiveSlimeMoldSteps = 0;
    bool observedSlimeMoldTrail = false;
    bool observedSlimeMoldMotion = false;
    {
        core::VulkanComputeDevice compute{context};
        core::MeshRenderer ant3dMeshes{context, shaderDirectory};
        core::MeshRenderer ant3dTerrain{context, shaderDirectory};
        core::MeshRenderer ant3dColliders{context, shaderDirectory, VK_PRIMITIVE_TOPOLOGY_LINE_LIST};
        core::ParticleRenderer particles{context, shaderDirectory};
        core::ParticleRenderer environmentParticles{context, shaderDirectory};
        core::LineRenderer lines{context, shaderDirectory};
        core::LineRenderer antLines{context, shaderDirectory, true, ants::AntScene::WorldHalfExtent};
        core::LineRenderer particleLifeLines{context, shaderDirectory, true,
            particle_life::ParticleLifeScene::WorldHalfExtent};
        core::LineRenderer particleLifeVolumeLines{context, shaderDirectory, false,
            particle_life::ParticleLifeScene::WorldHalfExtent};
        core::ImGuiLayer ui{context, window};
        // Submission owner dies first, waiting while all recorded resources live.
        core::FrameRenderer renderer{context, window};
        core::Camera camera;
        BoidsScene scene;
        ants::AntScene antScene;
        ants3d::Ant3DScene ant3dScene;
        particle_life::ParticleLifeScene particleLifeScene;
        slime_mold::SlimeMoldScene slimeMoldScene;
        Controls controls;
        if (config) {
            const auto& settings = config->settings();
            controls.scenario = settings.scenario;
            controls.ants = settings.ants;
            controls.ants3d = settings.ants3d;
            ants3d::applyAnt3DSettings(ant3dScene, settings.ants3d);
            ants::applyAntSettings(antScene, settings.ants);
            controls.slimeMold = settings.slimeMold;
            slime_mold::applySlimeMoldSettings(slimeMoldScene, settings.slimeMold);
            controls.particleLife = settings.particleLife;
            particle_life::applyParticleLifeSettings(particleLifeScene, settings.particleLife);
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
        if (startAnts3d || ants3dSmokeTest) {
            if (controls.scenario != 3 || !config) ants3d::resetAnt3DCamera(camera);
            controls.scenario = 3;
        }
        if (startAnts || antsSmokeTest) {
            if (controls.scenario != 1 || !config) ants::resetAntCamera(camera);
            controls.scenario = 1;
        }
        if (startParticleLife3d) {
            const bool enteringVolume = !particleLifeScene.threeDimensional();
            controls.particleLife.threeDimensional = true;
            controls.particleLife.gpu = true;
            if (!config || !config->hasValue("scenarios.particle_life.interaction_radius"))
                controls.particleLife.interactionRadius = 22.0F;
            particle_life::applyParticleLifeSettings(particleLifeScene, controls.particleLife);
            if (enteringVolume || controls.scenario != 2 || !config)
                particle_life::resetParticleLifeCamera(camera, true);
            controls.scenario = 2;
        }
        if (startParticleLife || particleLifeSmokeTest) {
            if (controls.scenario != 2 || !config)
                particle_life::resetParticleLifeCamera(camera, particleLifeScene.threeDimensional());
            controls.scenario = 2;
        }
        if (startSlimeMold || slimeMoldSmokeTest) {
            if (controls.scenario != 4 || !config) slime_mold::resetSlimeMoldCamera(camera);
            controls.scenario = 4;
        }
        if (controls.scenario == 1 || controls.scenario == 3)
            controls.antScenario = controls.scenario;
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
        try {
            particleLifeScene.initializeGpu(compute, (shaderDirectory / "compute/particle_life.comp.spv").string());
            if (controls.particleLife.gpu) particleLifeScene.setBackend(sim::Backend::GPU);
        } catch (const std::exception& error) {
            if (particleLifeSmokeTest) throw;
            controls.particleLifeBackendError = std::string{"GPU compute unavailable: "} + error.what();
        }
        if (particleLifeSmokeTest) require(particleLifeScene.backend() == sim::Backend::GPU,
            "Particle Life must prefer GPU when available");
        try {
            slimeMoldScene.initializeGpu(compute, (shaderDirectory / "compute/slime_mold.comp.spv").string());
            if (controls.slimeMold.gpu) slimeMoldScene.setBackend(sim::Backend::GPU);
        } catch (const std::exception& error) {
            if (slimeMoldSmokeTest) throw;
            controls.slimeMoldBackendError = std::string{"GPU compute unavailable: "} + error.what();
        }
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
            ants3d::captureAnt3DSettings(ant3dScene, controls.ants3d);
            settings.ants3d = controls.ants3d;
            particle_life::captureParticleLifeSettings(particleLifeScene, controls.particleLife);
            settings.particleLife = controls.particleLife;
            slime_mold::captureSlimeMoldSettings(slimeMoldScene, controls.slimeMold);
            settings.slimeMold = controls.slimeMold;
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
            const auto state = controls.scenario == 3 ?
                controls.ant3dInteraction.overviewState(camera) : camera.state();
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
        std::vector<core::MeshVertex> ant3dGeometry;
        std::vector<core::MeshVertex> ant3dTerrainGeometry;
        std::optional<std::uint64_t> ant3dTerrainRevision;
        std::vector<core::MeshVertex> ant3dCollisionGeometry;
        auto previous = std::chrono::steady_clock::now();
        auto nextSettingsSave = previous + std::chrono::seconds{1};
        const auto deadline = previous + std::chrono::seconds{55};
        bool ant3dClickArmed = false;
        ImVec2 ant3dClickStart{};
        while (!window.shouldClose()) {
            window.setEscapeClosesWindow(!controls.ant3dInteraction.following());
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
                    ant3dMeshes.configure(pass, renderer.sampleCount());
                    ant3dTerrain.configure(pass, renderer.sampleCount());
                    ant3dColliders.configure(pass, renderer.sampleCount());
                    particles.configure(pass, renderer.sampleCount());
                    environmentParticles.configure(pass, renderer.sampleCount());
                    lines.configure(pass, renderer.sampleCount());
                    antLines.configure(pass, renderer.sampleCount());
                    particleLifeLines.configure(pass, renderer.sampleCount());
                    particleLifeVolumeLines.configure(pass, renderer.sampleCount());
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
                    if (slimeMoldSmokeTest) {
                        if (presentedFrames == 1) slimeMoldScene.setCount(0);
                        if (presentedFrames == 2) slimeMoldScene.setCount(1);
                        if (presentedFrames == 3) slimeMoldScene.setCount(257);
                        if (presentedFrames == 4) slimeMoldScene.setCount(100000);
                        if (presentedFrames == 5) slimeMoldScene.setCount(30000);
                        if (presentedFrames == 6 || presentedFrames == 7 || presentedFrames == 37) {
                            const auto before = slimeMoldScene.readback();
                            const auto steps = slimeMoldScene.totalSteps();
                            slimeMoldScene.setBackend(presentedFrames == 6 ? sim::Backend::CPU : sim::Backend::GPU);
                            const auto after = slimeMoldScene.readback();
                            require(before.trail == after.trail && before.agents.size() == after.agents.size() &&
                                slimeMoldScene.totalSteps() == steps, "Slime mold backend switch lost field or clock");
                            for (std::size_t i = 0; i < before.agents.size(); ++i)
                                require(before.agents[i].position == after.agents[i].position &&
                                    before.agents[i].reserved == after.agents[i].reserved,
                                    "Slime mold backend switch lost agent state");
                        }
                        if (presentedFrames == 8) slimeMoldScene.setPaused(true);
                        if (presentedFrames == 9) actions.step = true;
                        if (presentedFrames == 10) slimeMoldScene.setPaused(false);
                        if (presentedFrames == 12) slimeMoldScene.setSeed(729);
                        if (presentedFrames == 24) actions.reset = true;
                        if (presentedFrames == 26) slimeMoldScene.setFoodCount(0);
                        if (presentedFrames == 28) slimeMoldScene.setFoodCount(8);
                        controls.slimeMold.showAgents = presentedFrames >= 18 && presentedFrames < 30;
                        controls.slimeMold.exposure = presentedFrames >= 20 ? 0.8F : 0.4F;
                        if (presentedFrames == 32) {
                            inactiveSlimeMoldSteps = slimeMoldScene.totalSteps();
                            selectScenario(controls, camera, 2, false);
                            particleLifeScene.setPaused(true);
                        }
                        if (presentedFrames == 36) {
                            require(slimeMoldScene.totalSteps() == inactiveSlimeMoldSteps,
                                "Inactive slime mold advanced");
                            selectScenario(controls, camera, 4, false);
                            observedSceneSwitch = true;
                        }
                    }
                    if (particleLifeSmokeTest) {
                        if (presentedFrames == 1) particleLifeScene.setCount(0);
                        if (presentedFrames == 2) particleLifeScene.setCount(1);
                        if (presentedFrames == 3) particleLifeScene.setCount(257);
                        if (presentedFrames == 4) particleLifeScene.setCount(2000);
                        if (presentedFrames == 6) {
                            particleLifeScene.setSpeciesCount(8);
                            checkedParticleLifeSwitch(particleLifeScene, sim::Backend::CPU);
                        }
                        if (presentedFrames == 7) checkedParticleLifeSwitch(particleLifeScene, sim::Backend::GPU);
                        if (presentedFrames == 8) particleLifeScene.setPaused(true);
                        if (presentedFrames == 9) actions.step = true;
                        if (presentedFrames == 10) particleLifeScene.setPaused(false);
                        if (presentedFrames == 12) {
                            particleLifeScene.setSeed(729U);
                            particleLifeScene.randomizeMatrix(42U);
                            actions.skipElapsed = true;
                        }
                        if (presentedFrames == 13 || presentedFrames == 38) {
                            checkedParticleLifeDimensions(particleLifeScene, true, camera);
                            auto parameters = particleLifeScene.parameters();
                            parameters.interactionRadius = 22.0F;
                            particleLifeScene.setParameters(parameters);
                            actions.skipElapsed = true;
                        }
                        if (presentedFrames == 18) {
                            particleLifeScene.setInteraction(0, 1, 0.85F);
                            particleLifeScene.setInteraction(1, 0, -0.65F);
                            require(particleLifeScene.matrix()[1] == 0.85F &&
                                particleLifeScene.matrix()[particle_life::ParticleLifeScene::MaxSpecies] == -0.65F,
                                "Particle Life matrix lost directional interactions");
                        }
                        if (presentedFrames == 20) {
                            auto parameters = particleLifeScene.parameters();
                            parameters.force = 0.0F;
                            parameters.friction = 0.0F;
                            particleLifeScene.setParameters(parameters);
                            sim::Agent crossing;
                            crossing.position = {0.0F, 49.99F, 0.0F};
                            crossing.velocity = {0.0F, 20.0F, 0.0F};
                            const std::uint8_t species = 0;
                            particleLifeScene.setAgents(std::span{&crossing, 1}, std::span{&species, 1});
                        }
                        if (presentedFrames == 21) {
                            auto parameters = particleLifeScene.parameters();
                            parameters.force = 30.0F;
                            parameters.friction = 4.0F;
                            particleLifeScene.setParameters(parameters);
                            particleLifeScene.setCount(2000);
                        }
                        if (presentedFrames == 24) actions.reset = true;
                        if (presentedFrames == 25) checkedParticleLifeSwitch(particleLifeScene, sim::Backend::CPU);
                        if (presentedFrames == 26) checkedParticleLifeSwitch(particleLifeScene, sim::Backend::GPU);
                        if (presentedFrames == 28) particleLifeScene.setSpeciesCount(6);
                        if (presentedFrames == 30) {
                            checkedParticleLifeDimensions(particleLifeScene, false, camera);
                            observedParticleLife2dReturn = true;
                            actions.skipElapsed = true;
                        }
                        if (presentedFrames == 31) checkedParticleLifeSwitch(particleLifeScene, sim::Backend::CPU);
                        if (presentedFrames == 32) {
                            inactiveParticleLifeSteps = particleLifeScene.totalSteps();
                            controls.scenario = 0;
                            scene.setPaused(true);
                        }
                        if (presentedFrames == 34) {
                            require(particleLifeScene.totalSteps() == inactiveParticleLifeSteps,
                                "Inactive Particle Life advanced while showing Boids");
                            controls.scenario = 1;
                            antScene.setPaused(true);
                        }
                        if (presentedFrames == 36) {
                            require(particleLifeScene.totalSteps() == inactiveParticleLifeSteps,
                                "Inactive Particle Life advanced while showing ants");
                            controls.scenario = 2;
                            observedSceneSwitch = true;
                        }
                        if (presentedFrames == 37) checkedParticleLifeSwitch(particleLifeScene, sim::Backend::GPU);
                    }
                    if (ants3dSmokeTest) {
                        if (presentedFrames == 1) ant3dScene.setCount(0);
                        if (presentedFrames == 2) ant3dScene.setCount(1);
                        if (presentedFrames == 3) ant3dScene.setCount(257);
                        if (presentedFrames == 4) ant3dScene.setCount(600);
                        if (presentedFrames == 5) {
                            const auto position = ant3dScene.agents().front().position;
                            const auto picked = ants3d::pickAnt3D(ant3dScene,
                                {position + glm::vec3{0, 2, 0}, {0, -1, 0}}, controls.ants3d.showCaveRoofs);
                            require(picked == 0, "The departing ant must be selectable in the rendered scene");
                            controls.ant3dInteraction.select(picked, ant3dScene, camera);
                            require(controls.ant3dInteraction.toggleFollow(ant3dScene, camera),
                                "The selected ant must enter first-person view");
                        }
                        if (presentedFrames == 6) {
                            const auto overview = controls.ant3dInteraction.overviewState(camera);
                            controls.ant3dInteraction.toggleFollow(ant3dScene, camera);
                            const auto restored = camera.state();
                            require(!controls.ant3dInteraction.following() && restored.target == overview.target &&
                                restored.yaw == overview.yaw && restored.pitch == overview.pitch &&
                                restored.distance == overview.distance,
                                "Leaving first-person must restore the rendered scene camera");
                            const auto eyeBeforeLook = camera.position();
                            camera.look(35.0F, -15.0F);
                            require(glm::length(camera.position() - eyeBeforeLook) < 1.0e-4F,
                                "3D ant free look must rotate around the camera position");
                            camera.zoom(0.5F);
                        }
                        if (presentedFrames == 7) {
                            controls.ant3dInteraction.toggleFollowView(ant3dScene, camera);
                            require(controls.ant3dInteraction.toggleFollow(ant3dScene, camera),
                                "The selected ant must enter third-person follow");
                        }
                        if (presentedFrames == 8) ant3dScene.setPaused(true);
                        if (presentedFrames == 9) actions.step = true;
                        if (presentedFrames == 10) ant3dScene.setPaused(false);
                        if (presentedFrames == 11) {
                            controls.ant3dInteraction.stopFollowing(camera);
                            controls.ant3dInteraction.toggleFollowView(ant3dScene, camera);
                        }
                        if (presentedFrames == 12) { ant3dScene.randomize(729U); actions.skipElapsed = true; }
                        if (presentedFrames == 24) actions.reset = true;
                        if (presentedFrames == 26) {
                            ant3dScene.setMovementSpeed(9.4F);
                            ant3dScene.setNestExitCount(2);
                            ant3dScene.setNestEntryCount(3);
                            ant3dScene.setFoodSpawnInterval(1.0F);
                        }
                        if (presentedFrames == 30) {
                            controls.ant3dInteraction.clear(camera);
                            for (int step = 0; step < 120 && !ant3dScene.latestExitedAnt(); ++step)
                                ant3dScene.advance(ants3d::Ant3DScene::FixedStepSeconds);
                            require(controls.ant3dInteraction.toggleFollow(ant3dScene, camera) &&
                                controls.ant3dInteraction.selectedAnt() == ant3dScene.latestExitedAnt(),
                                "Space without selection must follow the latest completed departure");
                        }
                        if (presentedFrames == 31)
                            controls.ant3dInteraction.toggleFollowView(ant3dScene, camera);
                        controls.ants3d.showColliders = presentedFrames >= 26 && presentedFrames < 30;
                        controls.ants3d.showCaveRoofs = presentedFrames < 28 || presentedFrames >= 38;
                        if (presentedFrames == 32) {
                            inactiveAnt3dSteps = ant3dScene.totalSteps();
                            actions.skipElapsed |= selectScenario(controls, camera, 1,
                                particleLifeScene.threeDimensional());
                            require(controls.antScenario == 1, "2D dimension selection was not remembered");
                        }
                        if (presentedFrames == 36) {
                            require(ant3dScene.totalSteps() == inactiveAnt3dSteps,
                                "Inactive 3D ants advanced while showing 2D ants");
                            inactiveAntSteps = antScene.totalSteps();
                            require(inactiveAntSteps > 0, "2D ants did not retain an independent simulation");
                            actions.skipElapsed |= selectScenario(controls, camera, 3,
                                particleLifeScene.threeDimensional());
                            const auto restored = camera.state();
                            const auto saved = *controls.ants3dCamera;
                            require(controls.antScenario == 3 && restored.target == saved.target &&
                                restored.yaw == saved.yaw && restored.pitch == saved.pitch &&
                                restored.distance == saved.distance,
                                "Switching ant dimensions must restore the 3D camera");
                            require(ant3dScene.count() == 600 && antScene.count() == 1500 &&
                                ant3dScene.seed() == 729U && antScene.seed() == 12345U,
                                "Switching ant dimensions changed independent colony settings");
                            observedSceneSwitch = true;
                        }
                        if (presentedFrames == 38) {
                            // Returning through Scene uses the same remembered dimension as the UI.
                            selectScenario(controls, camera, 2, particleLifeScene.threeDimensional());
                            actions.skipElapsed |= selectScenario(controls, camera, controls.antScenario,
                                particleLifeScene.threeDimensional());
                            require(controls.scenario == 3, "Scene menu lost the selected ant dimension");
                        }
                    }
                    if (antsSmokeTest) {
                        if (presentedFrames == 1) antScene.setCount(0);
                        if (presentedFrames == 2) antScene.setCount(1);
                        if (presentedFrames == 3) antScene.setCount(257);
                        if (presentedFrames == 4) antScene.setCount(1500);
                        if (presentedFrames == 5) { antScene.setObstacleCount(0); actions.skipElapsed = true; }
                        if (presentedFrames == 6) { antScene.setObstacleCount(ants::AntScene::MaxObstacles); actions.skipElapsed = true; }
                        if (presentedFrames == 7) { antScene.setObstacleCount(ants::AntScene::DefaultObstacleCount); actions.skipElapsed = true; }
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
                    if (controls.scenario == 3) controls.ant3dInteraction.update(ant3dScene, camera);
                    if (controls.panelVisible) {
                        const auto panelActions = drawPanel(scene, antScene, ant3dScene, particleLifeScene, slimeMoldScene,
                            controls, camera, context, renderer, fullscreen, config);
                        actions.reset |= panelActions.reset;
                        actions.step |= panelActions.step;
                        actions.skipElapsed |= panelActions.skipElapsed;
                    }
                    if (controls.scenario != titledScenario) {
                        window.setTitle(sceneWindowTitle(controls.scenario));
                        titledScenario = controls.scenario;
                    }
                    // Exercise the same deferred fullscreen request as the checkbox.
                    if (anySmokeTest && presentedFrames == 16) fullscreen = true;
                    if (anySmokeTest && presentedFrames == 20) fullscreen = false;
                    const auto& io = ImGui::GetIO();
                    // Capture is computed before this frame's windows are drawn.
                    const bool mouseCaptured = panelJustShown || (controls.panelVisible && ui.wantsMouse());
                    const bool cameraInput = ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                        ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ui.wheelDelta() != 0.0F ||
                        (controls.scenario == 3 && ImGui::IsMouseDown(ImGuiMouseButton_Right));
                    const bool ant3dFollowing = controls.scenario == 3 && controls.ant3dInteraction.following();
                    const bool cameraFocused = controls.scenario != 3 ||
                        glfwGetWindowAttrib(window.nativeHandle(), GLFW_FOCUSED);
                    if (!mouseCaptured && !anySmokeTest && !ant3dFollowing && cameraFocused) {
                        const bool freeLook = controls.scenario == 3 &&
                            (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                             ImGui::IsMouseDragging(ImGuiMouseButton_Right));
                        if (freeLook) camera.look(io.MouseDelta.x, io.MouseDelta.y);
                        else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) camera.pan(io.MouseDelta.x, io.MouseDelta.y, io.DisplaySize.y);
                        camera.zoom(ui.wheelDelta());
                    }
                    const bool ant3dKeyboardAllowed = controls.scenario == 3 && !anySmokeTest && !panelJustShown &&
                        glfwGetWindowAttrib(window.nativeHandle(), GLFW_FOCUSED) &&
                        !io.WantCaptureKeyboard && !io.WantTextInput && !ImGui::IsAnyItemActive();
                    if (ant3dKeyboardAllowed) {
                        if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
                            controls.ant3dInteraction.toggleFollowView(ant3dScene, camera);
                        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                            controls.ant3dInteraction.stopFollowing(camera);
                        else if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
                            controls.ant3dInteraction.toggleFollow(ant3dScene, camera);
                    }
                    if (ant3dKeyboardAllowed && !controls.ant3dInteraction.following()) {
                        glm::vec3 direction{
                            static_cast<float>(ImGui::IsKeyDown(ImGuiKey_D)) - static_cast<float>(ImGui::IsKeyDown(ImGuiKey_A)),
                            static_cast<float>(ImGui::IsKeyDown(ImGuiKey_E)) - static_cast<float>(ImGui::IsKeyDown(ImGuiKey_Q)),
                            static_cast<float>(ImGui::IsKeyDown(ImGuiKey_W)) - static_cast<float>(ImGui::IsKeyDown(ImGuiKey_S))};
                        if (glm::dot(direction, direction) > 0.0F) {
                            // Normalize diagonals; a stalled frame must not teleport across the map.
                            const float distance = 28.0F * (io.KeyShift ? 3.0F : 1.0F) *
                                static_cast<float>(std::clamp(elapsed, 0.0, 0.1));
                            direction = glm::normalize(direction) * distance;
                            camera.move(direction.x, direction.y, direction.z);
                        }
                    }
                    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                    // A selection is a press/release on the scene. Dragging to
                    // rotate the camera, crossing UI, or losing focus cancels the candidate.
                    const bool pickAllowed = controls.scenario == 3 && !anySmokeTest && !mouseCaptured &&
                        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && ImGui::IsMousePosValid() &&
                        glfwGetWindowAttrib(window.nativeHandle(), GLFW_HOVERED) &&
                        glfwGetWindowAttrib(window.nativeHandle(), GLFW_FOCUSED);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        ant3dClickArmed = pickAllowed;
                        ant3dClickStart = io.MousePos;
                    }
                    const float clickDx = io.MousePos.x - ant3dClickStart.x;
                    const float clickDy = io.MousePos.y - ant3dClickStart.y;
                    if (!pickAllowed || clickDx * clickDx + clickDy * clickDy >
                        io.MouseDragThreshold * io.MouseDragThreshold ||
                        ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                        ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                        ImGui::IsMouseDown(ImGuiMouseButton_Right) || ui.wheelDelta() != 0.0F)
                        ant3dClickArmed = false;
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                        if (ant3dClickArmed && io.DisplaySize.x > 0.0F && io.DisplaySize.y > 0.0F) {
                            const auto ray = camera.screenRay(io.MousePos.x / io.DisplaySize.x,
                                io.MousePos.y / io.DisplaySize.y, aspect);
                            if (ray) {
                                const auto picked = ants3d::pickAnt3D(ant3dScene, *ray,
                                    controls.ants3d.showCaveRoofs, controls.ant3dInteraction.firstPersonFollowing() ?
                                        controls.ant3dInteraction.selectedAnt() : std::nullopt);
                                controls.ant3dInteraction.select(picked, ant3dScene, camera);
                            }
                        }
                        ant3dClickArmed = false;
                    }
                    if (controls.scenario == 3) {
                        repel.setEnabled(false);
                        attract.setEnabled(false);
                        const auto stepsBefore = ant3dScene.totalSteps();
                        const auto firstPosition = ant3dScene.agents().empty() ? glm::vec3{0} :
                            ant3dScene.agents().front().position;
                        const auto started = std::chrono::steady_clock::now();
                        controls.simulationSteps = 0;
                        controls.simulationLimited = false;
                        if (actions.reset) ant3dScene.reset();
                        else {
                            if (actions.step && ant3dScene.singleStep()) ++controls.simulationSteps;
                            if (!actions.skipElapsed) {
                                const double budget = ants3dSmokeTest ? (presentedFrames >= 40 ? 0.0 :
                                    std::numeric_limits<double>::infinity()) : 0.008;
                                controls.simulationSteps += ant3dScene.advance(
                                    ants3dSmokeTest ? 1.0 / 60.0 : elapsed, budget);
                                controls.simulationLimited = ant3dScene.lastAdvanceLimited();
                            }
                        }
                        controls.simulationMs = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - started).count();
                        // Terrain depends only on the world seed and roof view.
                        // Keep the large immutable mesh out of per-frame CPU
                        // generation and upload each revision once per GPU slot.
                        const std::uint64_t terrainRevision =
                            (static_cast<std::uint64_t>(ant3dScene.seed()) << 1U) |
                            static_cast<std::uint64_t>(controls.ants3d.showCaveRoofs);
                        if (ant3dTerrainRevision != terrainRevision) {
                            ants3d::makeAnt3DTerrainMesh(ant3dScene, controls.ants3d, ant3dTerrainGeometry);
                            ant3dTerrainRevision = terrainRevision;
                        }
                        controls.ant3dInteraction.update(ant3dScene, camera,
                            ants3dSmokeTest ? 1.0 / 60.0 : elapsed);
                        const auto followedAnt = controls.ant3dInteraction.firstPersonFollowing() ?
                            controls.ant3dInteraction.selectedAnt() : std::nullopt;
                        ants3d::makeAnt3DMesh(ant3dScene, controls.ants3d, ant3dGeometry,
                            controls.ant3dInteraction.selectedAnt(), followedAnt);
                        if (controls.ants3d.showColliders)
                            ants3d::makeAnt3DColliders(ant3dScene, ant3dCollisionGeometry, followedAnt);
                        if (ants3dSmokeTest) {
                            require(!ant3dTerrainGeometry.empty() && !ant3dScene.terrain().caves().empty(),
                                "3D ants must render solid terrain and caves");
                            if (presentedFrames == 5) require(followedAnt == 0 &&
                                glm::length(camera.position() - ant3dScene.agents().front().position) < 0.3F,
                                "The rendered first-person camera must move with the selected ant");
                            if (presentedFrames == 7 || presentedFrames == 31) {
                                require(controls.ant3dInteraction.following() && !followedAnt &&
                                    controls.ant3dInteraction.followView() == ants3d::FollowView::ThirdPerson,
                                    "Third-person follow must render the selected ant's body");
                                require(glm::length(camera.position() - ant3dScene.agents()[
                                    *controls.ant3dInteraction.selectedAnt()].position) > 0.3F,
                                    "The third-person camera must trail outside the selected ant");
                            }
                            if (ant3dScene.paused() && !actions.step && !actions.reset)
                                require(ant3dScene.totalSteps() == stepsBefore, "Paused 3D ants advanced");
                            if (actions.step) require(ant3dScene.totalSteps() == stepsBefore + 1,
                                "3D ant single step failed");
                            if (actions.reset) require(ant3dScene.totalSteps() == 0, "3D ant reset failed");
                            if (presentedFrames >= 40) require(controls.simulationSteps == 1 && controls.simulationLimited,
                                "3D ant frame budget failed");
                            if (!ant3dScene.agents().empty() && !actions.reset && ant3dScene.totalSteps() > stepsBefore)
                                observedAnt3dMotion |= ant3dScene.agents().front().position != firstPosition;
                            require(scene.totalSteps() == 0 && particleLifeScene.totalSteps() == 0 &&
                                antScene.totalSteps() == inactiveAntSteps,
                                "3D ant simulation advanced another scene");
                        }
                        core::SceneView view;
                        view.viewProjection = camera.viewProjection(aspect);
                        view.right = glm::vec4(camera.right(), 0);
                        view.up = glm::vec4(camera.up(), 0);
                        ui.endFrame();
                        ant3dTerrain.draw(commands, frame, ant3dTerrainGeometry, view, terrainRevision);
                        ant3dMeshes.draw(commands, frame, ant3dGeometry, view);
                        if (controls.ants3d.showColliders)
                            ant3dColliders.draw(commands, frame, ant3dCollisionGeometry, view);
                        ui.record(commands);
                        return;
                    }
                    if (controls.scenario == 4) {
                        repel.setEnabled(false); attract.setEnabled(false);
                        const auto stepsBefore = slimeMoldScene.totalSteps();
                        const auto before = slimeMoldSmokeTest ? slimeMoldScene.readback() : slime_mold::SlimeMoldState{};
                        const auto started = std::chrono::steady_clock::now();
                        controls.simulationSteps = 0; controls.simulationLimited = false;
                        if (actions.reset) slimeMoldScene.reset();
                        else {
                            if (actions.step && slimeMoldScene.singleStep()) ++controls.simulationSteps;
                            if (!actions.skipElapsed) {
                                const double budget = slimeMoldSmokeTest ? (presentedFrames >= 40 ? 0.0 :
                                    std::numeric_limits<double>::infinity()) : 0.008;
                                controls.simulationSteps += slimeMoldScene.advance(
                                    slimeMoldSmokeTest ? 1.0 / 30.0 : elapsed, budget);
                                controls.simulationLimited = slimeMoldScene.lastAdvanceLimited();
                            }
                        }
                        const auto display = slimeMoldScene.refreshDisplay(controls.slimeMold.exposure,
                            controls.slimeMold.showAgents);
                        controls.simulationMs = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - started).count();
                        renderParticles.resize(display.size());
                        for (std::size_t i = 0; i < display.size(); ++i) {
                            const auto& a = display[i];
                            renderParticles[i] = {a.position, a.size, a.velocity, 0, a.color, a.id};
                        }
                        slime_mold::makeSlimeMoldFood(slimeMoldScene, controls.slimeMold.showFood, antEnvironment);
                        if (slimeMoldSmokeTest) {
                            const auto state = slimeMoldScene.readback();
                            if (slimeMoldScene.paused() && !actions.step && !actions.reset)
                                require(slimeMoldScene.totalSteps() == stepsBefore, "Paused slime mold advanced");
                            if (actions.step) require(slimeMoldScene.totalSteps() == stepsBefore+1, "Slime mold step failed");
                            if (actions.reset) require(slimeMoldScene.totalSteps() == 0, "Slime mold reset failed");
                            if (presentedFrames >= 40) require(controls.simulationSteps == 1 && controls.simulationLimited,
                                "Slime mold work budget failed");
                            if (!state.agents.empty() && !actions.reset && slimeMoldScene.totalSteps() > stepsBefore)
                                observedSlimeMoldMotion |= state.agents.front().position != before.agents.front().position;
                            observedSlimeMoldTrail |= std::any_of(state.trail.begin(), state.trail.end(),
                                [](float t) { return t > 0; });
                            if (slimeMoldScene.backend() == sim::Backend::GPU)
                                require(display.empty() && slimeMoldScene.agents().empty() && slimeMoldScene.trail().empty(),
                                    "GPU slime mold must render without CPU state copies");
                        }
                        core::SceneView view;
                        view.viewProjection = camera.viewProjection(aspect);
                        view.right = {1,0,0,0}; view.up = {0,0,1,0};
                        view.options = {1,3,0,0};
                        ui.endFrame();
                        if (slimeMoldSmokeTest && !controls.panelVisible)
                            require(ImGui::GetDrawData()->TotalVtxCount == 0, "Hidden slime mold panel left draw commands");
                        if (slimeMoldScene.backend() == sim::Backend::GPU) {
                            const auto buffer = slimeMoldScene.renderBuffer();
                            particles.drawBuffer(commands, frame, compute.buffer(buffer), compute.bufferSize(buffer),
                                static_cast<std::uint32_t>(slimeMoldScene.renderCount(controls.slimeMold.showAgents)), view);
                        } else particles.draw(commands, frame, renderParticles, view);
                        view.options.y = 1;
                        environmentParticles.draw(commands, frame, antEnvironment, view);
                        ui.record(commands);
                        return;
                    }
                    if (controls.scenario == 2) {
                        repel.setEnabled(false);
                        attract.setEnabled(false);
                        const auto stepsBefore = particleLifeScene.totalSteps();
                        const auto oldAgents = particleLifeSmokeTest ? particleLifeScene.readback() : std::vector<sim::Agent>{};
                        const auto firstPosition = oldAgents.empty() ? glm::vec3{0} : oldAgents.front().position;
                        const auto started = std::chrono::steady_clock::now();
                        controls.simulationSteps = 0;
                        controls.simulationLimited = false;
                        if (actions.reset) particleLifeScene.reset();
                        else {
                            if (actions.step && particleLifeScene.singleStep()) ++controls.simulationSteps;
                            if (!actions.skipElapsed) {
                                const double budget = particleLifeSmokeTest ? (presentedFrames >= 40 ? 0.0 :
                                    std::numeric_limits<double>::infinity()) : 0.008;
                                controls.simulationSteps += particleLifeScene.advance(
                                    particleLifeSmokeTest ? 1.0 / 60.0 : elapsed, budget);
                                controls.simulationLimited = particleLifeScene.lastAdvanceLimited();
                            }
                        }
                        controls.simulationMs = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - started).count();
                        particle_life::makeParticleLifeParticles(particleLifeScene, renderParticles);
                        if (particleLifeSmokeTest) {
                            const bool gpu = particleLifeScene.backend() == sim::Backend::GPU;
                            require(gpu ? (particleLifeScene.agents().empty() && renderParticles.empty()) :
                                renderParticles.size() == particleLifeScene.count(),
                                "Particle Life must render GPU state without a CPU copy");
                            const auto state = particleLifeScene.readback();
                            require(state.size() == particleLifeScene.count(), "Particle Life state count mismatch");
                            for (std::size_t i = 0; i < state.size(); ++i) {
                                require(state[i].color == particle_life::ParticleLifeScene::speciesColor(
                                    particleLifeScene.species()[i]), "Particle Life species color mismatch");
                                for (glm::length_t axis = 0; axis < 3; ++axis) {
                                    require(std::isfinite(state[i].position[axis]) &&
                                        std::isfinite(state[i].velocity[axis]) &&
                                        state[i].position[axis] >= particleLifeScene.world().minimum[axis] &&
                                        state[i].position[axis] < particleLifeScene.world().maximum[axis],
                                        "Particle Life left its periodic world bounds");
                                }
                                if (!particleLifeScene.threeDimensional())
                                    require(state[i].position.y == 0.0F && state[i].velocity.y == 0.0F,
                                        "2D Particle Life left its plane");
                                else if (gpu && oldAgents.size() == state.size() && !actions.reset)
                                    observedParticleLife3dGpuMotion |=
                                        state[i].position.y != oldAgents[i].position.y && state[i].velocity.y != 0.0F;
                            }
                            if (presentedFrames == 20) {
                                require(gpu && state.size() == 1 && state[0].position.y < -49.0F &&
                                    std::abs(state[0].velocity.y - 20.0F) < 1.0e-4F,
                                    "3D GPU Particle Life failed to wrap across the Y boundary");
                                observedParticleLife3dWrap = true;
                            }
                            if (particleLifeScene.paused() && !actions.step && !actions.reset)
                                require(particleLifeScene.totalSteps() == stepsBefore, "Paused Particle Life advanced");
                            if (actions.step) require(particleLifeScene.totalSteps() == stepsBefore + 1,
                                "Particle Life single step failed");
                            if (actions.reset) require(particleLifeScene.totalSteps() == 0, "Particle Life reset failed");
                            if (presentedFrames >= 40) require(controls.simulationSteps == 1 && controls.simulationLimited,
                                "Particle Life frame budget failed");
                            if (!state.empty() && !actions.reset && particleLifeScene.totalSteps() > stepsBefore) {
                                const bool moved = state.front().position != firstPosition;
                                observedParticleLifeMotion |= moved;
                                observedParticleLifeGpuMotion |= gpu && moved;
                            }
                            require(scene.totalSteps() == 0 && antScene.totalSteps() == 0,
                                "Particle Life advanced inactive scenes");
                        }
                        core::SceneView view;
                        view.viewProjection = camera.viewProjection(aspect);
                        view.right = glm::vec4(camera.right(), 0);
                        view.up = glm::vec4(camera.up(), 0);
                        view.options = {controls.particleLife.agentSize, 1.0F,
                            particleLifeScene.threeDimensional() ? 1.0F : 0.0F, 0};
                        ui.endFrame();
                        if (particleLifeSmokeTest && !controls.panelVisible)
                            require(ImGui::GetDrawData()->TotalVtxCount == 0, "Hidden Particle Life panel left UI commands");
                        auto& lifeLines = particleLifeScene.threeDimensional()
                            ? particleLifeVolumeLines : particleLifeLines;
                        lifeLines.draw(commands, view, controls.grid, controls.bounds);
                        if (particleLifeScene.backend() == sim::Backend::GPU) {
                            const auto buffer = particleLifeScene.stateBuffer();
                            particles.drawBuffer(commands, frame, compute.buffer(buffer), compute.bufferSize(buffer),
                                static_cast<std::uint32_t>(particleLifeScene.count()), view);
                        } else {
                            particles.draw(commands, frame, renderParticles, view);
                        }
                        ui.record(commands);
                        return;
                    }
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
                                controls.simulationSteps += antScene.advance((antsSmokeTest || ants3dSmokeTest) ? 1.0 / 60.0 : elapsed, budget);
                                controls.simulationLimited = antScene.lastAdvanceLimited();
                            }
                        }
                        controls.simulationMs = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - started).count();
                        ants::makeAntParticles(antScene, controls.ants, renderParticles, antEnvironment);
                        if (antsSmokeTest) {
                            require(renderParticles.size() == antScene.count(), "Ant render count mismatch");
                            require(!antEnvironment.empty(), "Ant world objects were not rendered");
                            require(antScene.obstacles().size() == antScene.obstacleCount(),
                                "Ant terrain count mismatch");
                            for (const auto& obstacle : antScene.obstacles()) {
                                require(std::any_of(antEnvironment.begin(), antEnvironment.end(),
                                    [&obstacle](const core::Particle& particle) {
                                        return particle.position == obstacle.position + glm::vec3{0, 0.08F, 0} &&
                                            particle.size == obstacle.radius * 2.0F;
                                    }), "Obstacle rendering must match its collision footprint");
                            }
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
                        // Environmental circles use the same XZ ground plane as the simulation.
                        view.right = {1, 0, 0, 0};
                        view.up = {0, 0, 1, 0};
                        environmentParticles.draw(commands, frame, antEnvironment, view);
                        view.right = glm::vec4(camera.right(), 0);
                        view.up = glm::vec4(camera.up(), 0);
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
            << " swapchain generations, food/hostiles/trails/terrain, 0/24/48 obstacles, 0/1/257/1500 ants, pause/step/reset/seed, "
               "scene switches/budget/MSAA/fullscreen/F1 panel, 0 validation errors.\n";
    }
    if (ants3dSmokeTest) {
        require(presentedFrames >= 48 && generations >= 3 && observedAnt3dMotion && observedSceneSwitch,
            "3D ant smoke test closed before all checks completed");
        std::cout << "3D ant smoke test passed: " << presentedFrames << " frames, " << generations
            << " swapchain generations, solid terrain/caves/ants/colliders/cutaway, "
               "0/1/257/600 ants, pause/step/reset/seed, independent 2D/3D dimensions/cameras, "
               "first/third-person follow/latest departure/multiple nest gates, "
               "camera-centered free look/budget/MSAA/fullscreen/F1 panel, 0 validation errors.\n";
    }
    if (slimeMoldSmokeTest) {
        require(presentedFrames >= 48 && generations >= 3 && observedSlimeMoldMotion &&
            observedSlimeMoldTrail && observedSceneSwitch, "Slime mold smoke test incomplete");
        std::cout << "Slime mold smoke test passed: 48 frames, GPU field/agents/direct rendering, "
                     "CPU/GPU transfers, 0/1/257/30000/100000 agents, pause/step/reset/food/seed, "
                     "scene switches/budget/MSAA/fullscreen/F1, 0 validation errors.\n";
    }
    if (particleLifeSmokeTest) {
        require(presentedFrames >= 48 && generations >= 3 && observedParticleLifeMotion &&
            observedParticleLifeGpuMotion && observedParticleLife3dGpuMotion && observedParticleLife3dWrap &&
            observedParticleLife2dReturn && observedSceneSwitch,
            "Particle Life smoke test closed before all checks completed");
        std::cout << "Particle Life smoke test passed: " << presentedFrames << " frames, " << generations
            << " swapchain generations, 0/1/257/2000 particles, 6/8 species, matrix/seed/pause/step/reset, "
               "2D/3D switches/Y motion/Y wrap/CPU/GPU transfers/direct GPU rendering/scene switches/"
               "budget/MSAA/fullscreen/F1 panel, 0 validation errors.\n";
    }
    return 0;
}

} // namespace emergences::scenarios::boids
