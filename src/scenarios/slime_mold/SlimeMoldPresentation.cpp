#include "scenarios/slime_mold/SlimeMoldPresentation.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <imgui.h>
#include "core/Camera.h"
#include "scenarios/slime_mold/SlimeMoldScene.h"

namespace emergences::scenarios::slime_mold {
void applySlimeMoldSettings(SlimeMoldScene& scene, const config::SlimeMoldSettings& s) {
    scene.setParameters({s.speed, s.sensorDistance, s.sensorAngle, s.turnSpeed, s.wander,
        s.depositRate, s.diffusion, s.evaporation, s.foodAttraction});
    scene.setCount(static_cast<std::size_t>(s.count));
    scene.setSeed(static_cast<std::uint32_t>(s.seed));
    scene.setFoodCount(static_cast<std::size_t>(s.foodCount));
    scene.setPaused(s.paused);
}
void captureSlimeMoldSettings(const SlimeMoldScene& scene, config::SlimeMoldSettings& s) {
    s.count = static_cast<int>(scene.count()); s.seed = static_cast<int>(scene.seed());
    s.foodCount = static_cast<int>(scene.foodCount()); s.paused = scene.paused();
    const auto& p = scene.parameters();
    s.speed = p.speed; s.sensorDistance = p.sensorDistance; s.sensorAngle = p.sensorAngle;
    s.turnSpeed = p.turnSpeed; s.wander = p.wander; s.depositRate = p.depositRate;
    s.diffusion = p.diffusion; s.evaporation = p.evaporation; s.foodAttraction = p.foodAttraction;
}
void resetSlimeMoldCamera(core::Camera& camera) {
    camera.setState({{-10,0,0}, 0, 1.56F, 143.75F});
}
SlimeMoldPanelActions drawSlimeMoldControls(SlimeMoldScene& scene, config::SlimeMoldSettings& s,
    core::Camera& camera, bool& antialiasing, std::string& error) {
    SlimeMoldPanelActions actions;
    int backend = scene.backend() == sim::Backend::GPU ? 1 : 0;
    if (ImGui::Combo("Backend", &backend, "CPU\0GPU compute\0")) {
        try {
            scene.setBackend(backend == 1 ? sim::Backend::GPU : sim::Backend::CPU);
            s.gpu = backend == 1; error.clear(); actions.skipElapsed = true;
        } catch (const std::exception& e) { error = e.what(); }
    }
    if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
    ImGui::TextDisabled("2D periodic world | 256 x 256 trail field");
    int count = static_cast<int>(scene.count());
    if (ImGui::SliderInt("Agents", &count, 0, static_cast<int>(SlimeMoldScene::MaxAgents), "%d", ImGuiSliderFlags_AlwaysClamp)) {
        scene.setCount(static_cast<std::size_t>(std::clamp(count, 0, static_cast<int>(SlimeMoldScene::MaxAgents))));
        actions.skipElapsed = true;
    }
    bool paused = scene.paused();
    if (ImGui::Checkbox("Paused", &paused)) { scene.setPaused(paused); actions.skipElapsed = true; }
    ImGui::SameLine(); ImGui::BeginDisabled(!paused);
    actions.step = ImGui::Button("Step"); ImGui::EndDisabled(); ImGui::SameLine();
    actions.reset = ImGui::Button("Reset");
    ImGui::Text("Simulation: %.2f s | fixed 60 Hz", scene.elapsedSimulationSeconds());
    if (ImGui::BeginTabBar("Slime mold controls")) {
        if (ImGui::BeginTabItem("Behavior")) {
            auto p = scene.parameters();
            const auto slider = [](const char* label, float& value, float low, float high) {
                const float previous = value;
                ImGui::SliderFloat(label, &value, low, high, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                value = std::isfinite(value) ? std::clamp(value, low, high) : previous;
            };
            slider("Speed", p.speed, 0, 30);
            slider("Sensor distance", p.sensorDistance, 0.1F, 15);
            slider("Sensor angle (rad)", p.sensorAngle, 0.01F, 3.14159265F);
            slider("Turn speed", p.turnSpeed, 0, 60);
            slider("Wander", p.wander, 0, 10);
            slider("Deposit rate", p.depositRate, 0, 100);
            slider("Diffusion", p.diffusion, 0, 60);
            slider("Evaporation", p.evaporation, 0, 10);
            slider("Food attraction", p.foodAttraction, 0, 100);
            scene.setParameters(p);
            ImGui::TextWrapped("Three sensors follow stronger trails. Moving agents reinforce paths; diffusion and evaporation reshape the network.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("World")) {
            int food = static_cast<int>(scene.foodCount());
            if (ImGui::SliderInt("Food sources", &food, 0, 12, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                scene.setFoodCount(static_cast<std::size_t>(std::clamp(food, 0, 12))); actions.skipElapsed = true;
            }
            static auto activeSeed = scene.seed();
            static int seedDraft = static_cast<int>(activeSeed);
            if (activeSeed != scene.seed()) {
                activeSeed = scene.seed(); seedDraft = static_cast<int>(activeSeed);
            }
            if (ImGui::InputInt("Seed", &seedDraft)) seedDraft = std::max(0, seedDraft);
            if (ImGui::Button("Generate")) {
                if (scene.seed() == static_cast<std::uint32_t>(seedDraft)) scene.reset();
                else scene.setSeed(static_cast<std::uint32_t>(seedDraft));
                actions.skipElapsed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Next seed")) {
                scene.setSeed((scene.seed()*1664525U+1013904223U) & 0x7fffffffU);
                s.seed = static_cast<int>(scene.seed()); actions.skipElapsed = true;
            }
            ImGui::TextWrapped("Food sources are persistent attractants. Set their count to zero for free network formation. Population, seed and food changes restart the world.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("View")) {
            const float oldExposure = s.exposure;
            ImGui::SliderFloat("Trail brightness", &s.exposure, 0.001F, 10, "%.3f",
                ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            s.exposure = std::isfinite(s.exposure) ? std::clamp(s.exposure, 0.001F, 10.0F) : oldExposure;
            ImGui::Checkbox("Show agents", &s.showAgents);
            ImGui::Checkbox("Show food", &s.showFood);
            ImGui::Checkbox("Antialiasing", &antialiasing);
            if (ImGui::Button("Reset camera")) resetSlimeMoldCamera(camera);
            ImGui::TextWrapped("Brightness changes only the display. GPU simulation and trail rendering stay on the device.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return actions;
}
void makeSlimeMoldFood(const SlimeMoldScene& scene, bool visible, std::vector<core::Particle>& particles) {
    particles.clear();
    if (!visible) return;
    for (const auto& position : scene.foodSources())
        particles.push_back({position+glm::vec3{0,0.2F,0}, 1.4F, {}, 0, {1.0F,0.55F,0.12F}, 0});
}
} // namespace emergences::scenarios::slime_mold
