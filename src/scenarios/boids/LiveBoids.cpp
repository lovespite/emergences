#include "scenarios/boids/BoidsApp.h"
#include "scenarios/boids/BoidsScene.h"
#include "scenarios/boids/CursorAttract.h"
#include "scenarios/boids/CursorRepel.h"
#include "core/Camera.h"
#include "core/Context.h"
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
    int shape = 0;
    int color = 0;
    float size = 0.45F;
    bool grid = true;
    bool bounds = true;
    bool antialiasing = false;
    bool cursor = false;
    int cursorMode = 0;
    float radius = 8.0F;
    float strength = 30.0F;
    float planeHeight = 0.0F;
    float simulationMs = 0.0F;
    std::size_t simulationSteps = 0;
    bool simulationLimited = false;
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

Actions drawPanel(BoidsScene& scene, Controls& controls, core::Camera& camera,
                  const core::Context& context, const core::FrameRenderer& renderer) {
    Actions actions;
    auto& simulation = scene.simulation();
    ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, std::max(180.0F,
        std::min(690.0F, ImGui::GetIO().DisplaySize.y - 36.0F))), ImGuiCond_Always);
    ImGui::Begin("Emergences | Boids", nullptr, ImGuiWindowFlags_NoResize);
    ImGui::TextColored(ImVec4(0.3F, 0.8F, 0.95F, 1.0F), "PHASE 3 / LIVE BOIDS");
    if (ImGui::BeginCombo("Backend", "CPU")) {
        ImGui::Selectable("CPU", true);
        ImGui::BeginDisabled();
        ImGui::Selectable("GPU compute (Phase 4)", false);
        ImGui::EndDisabled();
        ImGui::EndCombo();
    }
    int count = static_cast<int>(simulation.count());
    if (ImGui::SliderInt("Agents", &count, 0, 20000, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        simulation.setCount(static_cast<std::size_t>(std::clamp(count, 0, 20000)));
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
            ImGui::Checkbox("Cursor force", &controls.cursor);
            ImGui::BeginDisabled(!controls.cursor);
            ImGui::RadioButton("Repel", &controls.cursorMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Attract", &controls.cursorMode, 1);
            slider("Force radius", controls.radius, 0.0F, 20.0F);
            slider("Force strength", controls.strength, 0.0F, 100.0F);
            slider("Plane height", controls.planeHeight, -19.0F, 19.0F);
            ImGui::EndDisabled();
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
    if (controls.simulationLimited) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.35F, 1.0F), "Simulation slowed to keep controls responsive.");
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

} // namespace

int BoidsApp::run(const std::filesystem::path& shaderDirectory, bool smokeTest) {
    core::Window window{1280, 800, "Emergences | CPU Boids"};
    core::Context context{window};
    std::cout << "Vulkan initialized on: " << context.deviceName() << '\n';
    std::cout << "Validation layer: " << (context.validationEnabled() ? "enabled" : "disabled") << '\n';
#if !defined(NDEBUG)
    if (smokeTest && !context.validationEnabled()) {
        throw std::runtime_error("Debug smoke test requires VK_LAYER_KHRONOS_validation");
    }
#endif
    std::size_t presentedFrames = 0;
    std::size_t generations = 0;
    bool observedMotion = false;
    bool observedRepel = false;
    bool observedAttract = false;
    {
        core::ParticleRenderer particles{context, shaderDirectory};
        core::LineRenderer lines{context, shaderDirectory};
        core::ImGuiLayer ui{context, window};
        // Submission owner dies first, waiting while all recorded resources live.
        core::FrameRenderer renderer{context, window};
        core::Camera camera;
        BoidsScene scene;
        Controls controls;
        auto repelOwner = std::make_unique<CursorRepel>();
        auto attractOwner = std::make_unique<CursorAttract>();
        auto& repel = *repelOwner;
        auto& attract = *attractOwner;
        scene.simulation().interventions().push_back(std::move(repelOwner));
        scene.simulation().interventions().push_back(std::move(attractOwner));
        std::vector<core::Particle> renderParticles;
        auto previous = std::chrono::steady_clock::now();
        const auto deadline = previous + std::chrono::seconds{55};
        while (!window.shouldClose()) {
            window.pollEvents();
            if (window.shouldClose()) break;
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - previous).count();
            previous = now;
            if (smokeTest && now >= deadline) throw std::runtime_error("Boids smoke test timed out");
            if (smokeTest) controls.antialiasing = (presentedFrames >= 2 && presentedFrames < 16) || presentedFrames >= 20;
            renderer.setAntialiasing(controls.antialiasing);
            const bool presented = renderer.drawFrame(
                [&](VkRenderPass pass, std::uint32_t imageCount) {
                    particles.configure(pass, renderer.sampleCount());
                    lines.configure(pass, renderer.sampleCount());
                    ui.configure(pass, imageCount, renderer.sampleCount());
                },
                [&](VkCommandBuffer commands, VkExtent2D extent, std::size_t frame) {
                    ui.beginFrame();
                    if (smokeTest) {
                        require(renderer.sampleCount() == (controls.antialiasing
                            ? renderer.supportedAntialiasingSampleCount() : VK_SAMPLE_COUNT_1_BIT),
                            "Boids MSAA toggle did not select the expected sample count");
                    }
                    Actions actions;
                    if (smokeTest) smokeSettings(presentedFrames, scene, controls, camera, actions);
                    const auto panelActions = drawPanel(scene, controls, camera, context, renderer);
                    actions.reset |= panelActions.reset;
                    actions.step |= panelActions.step;
                    actions.skipElapsed |= panelActions.skipElapsed;
                    const auto& io = ImGui::GetIO();
                    const bool cameraInput = ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                        ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ui.wheelDelta() != 0.0F;
                    if (!ui.wantsMouse() && !smokeTest) {
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) camera.pan(io.MouseDelta.x, io.MouseDelta.y, io.DisplaySize.y);
                        camera.zoom(ui.wheelDelta());
                    }
                    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
                    const bool inputAllowed = !ui.wantsMouse() && !cameraInput &&
                        ImGui::IsMousePosValid() && glfwGetWindowAttrib(window.nativeHandle(), GLFW_HOVERED) &&
                        glfwGetWindowAttrib(window.nativeHandle(), GLFW_FOCUSED);
                    const auto target = cursorTarget(camera, scene.simulation().world(), controls,
                        smokeTest ? 0.5F : io.MousePos.x / io.DisplaySize.x,
                        smokeTest ? 0.5F : io.MousePos.y / io.DisplaySize.y, aspect,
                        smokeTest ? presentedFrames != 26 : inputAllowed);
                    updateForces(repel, attract, controls, target);
                    const auto stepsBefore = scene.totalSteps();
                    const auto oldAgents = scene.simulation().agents();
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
                            const double workBudget = smokeTest
                                ? (presentedFrames >= 40 ? 0.0 : std::numeric_limits<double>::infinity())
                                : 0.008;
                            controls.simulationSteps += scene.advance(smokeTest ? 1.0 / 60.0 : elapsed, workBudget);
                            controls.simulationLimited = scene.lastAdvanceLimited();
                        }
                    }
                    controls.simulationMs = std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - simulationStart).count();
                    const auto agents = scene.refreshColors(static_cast<ColorMode>(controls.color));
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
                    lines.draw(commands, view, controls.grid, controls.bounds);
                    particles.draw(commands, frame, renderParticles, view);
                    ui.record(commands);
                });
            if (!presented) {
                window.waitEvents();
                previous = std::chrono::steady_clock::now();
                continue;
            }
            ++presentedFrames;
            if (smokeTest) {
                if (presentedFrames == 4) window.resize(960, 640);
                else if (presentedFrames == 8) window.resize(1280, 800);
                else if (presentedFrames >= 48 && renderer.swapchainGeneration() >= 3) break;
            }
        }
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
                     "fixed steps/pause/reset/rules/limits/boundaries/colors/cursor/dense budget/MSAA toggles, 0 validation errors.\n";
    }
    return 0;
}

} // namespace emergences::scenarios::boids
