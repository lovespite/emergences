#include "scenarios/ants/AntPresentation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

#include <imgui.h>

#include "core/Camera.h"
#include "core/FrameRenderer.h"
#include "scenarios/ants/AntScene.h"

namespace emergences::scenarios::ants {
namespace {
bool slider(const char* label, float& value, float low, float high) {
    const float previous = value;
    if (!ImGui::SliderFloat(label, &value, low, high, "%.2f", ImGuiSliderFlags_AlwaysClamp))
        return false;
    value = std::isfinite(value) ? std::clamp(value, low, high) : previous;
    return value != previous;
}

void dot(std::vector<core::Particle>& output, glm::vec3 position, float size, glm::vec3 color) {
    output.push_back({position, size, {}, 0.0F, color, 0});
}
}

void applyAntSettings(AntScene& scene, const config::AntSettings& settings) {
    scene.setParameters({settings.speed, settings.sensorDistance, settings.sensorAngle,
        settings.turnSpeed, settings.wander, settings.depositRate, settings.evaporation, settings.diffusion,
        settings.foodSpawnInterval});
    scene.setCount(static_cast<std::size_t>(settings.count));
    scene.randomize(static_cast<std::uint32_t>(settings.seed));
    scene.setPaused(settings.paused);
}

void captureAntSettings(const AntScene& scene, config::AntSettings& settings) {
    const auto& parameters = scene.parameters();
    settings.count = static_cast<int>(scene.count());
    settings.seed = static_cast<int>(scene.seed());
    settings.paused = scene.paused();
    settings.speed = parameters.speed;
    settings.sensorDistance = parameters.sensorDistance;
    settings.sensorAngle = parameters.sensorAngle;
    settings.turnSpeed = parameters.turnSpeed;
    settings.wander = parameters.wander;
    settings.depositRate = parameters.depositRate;
    settings.evaporation = parameters.evaporation;
    settings.diffusion = parameters.diffusion;
    settings.foodSpawnInterval = parameters.foodSpawnInterval;
}

void resetAntCamera(core::Camera& camera) {
    camera.setState({{-AntScene::WorldHalfExtent * 0.2F, 0.0F, 0.0F}, 0.0F, 1.12F,
        AntScene::WorldHalfExtent * 2.875F});
}

AntPanelActions drawAntControls(AntScene& scene, config::AntSettings& settings,
    core::Camera& camera, const core::FrameRenderer& renderer,
    bool& antialiasing, bool& grid, bool& bounds) {
    AntPanelActions actions;
    ImGui::TextDisabled("CPU | %.0f x %.0f ground world",
        static_cast<double>(2.0F * AntScene::WorldHalfExtent),
        static_cast<double>(2.0F * AntScene::WorldHalfExtent));
    int count = static_cast<int>(scene.count());
    if (ImGui::SliderInt("Ants", &count, 0, static_cast<int>(AntScene::MaxAnts), "%d",
                         ImGuiSliderFlags_AlwaysClamp)) {
        scene.setCount(static_cast<std::size_t>(std::clamp(count, 0, static_cast<int>(AntScene::MaxAnts))));
        actions.skipElapsed = true;
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
    const auto carrying = scene.carryingFood();
    const auto returning = std::count(carrying.begin(), carrying.end(), std::uint8_t{1});
    int foodRemaining = 0;
    for (const auto& food : scene.foodSources()) foodRemaining += food.remaining;
    ImGui::Text("Delivered: %llu | Returning: %zu", static_cast<unsigned long long>(scene.deliveredFood()),
                static_cast<std::size_t>(returning));
    ImGui::Text("Food left: %d | Hostiles: %zu", foodRemaining, scene.enemies().size());

    if (ImGui::BeginTabBar("Ant controls")) {
        if (ImGui::BeginTabItem("World")) {
            // Keep a draft separate from the active settings captured by autosave.
            static auto activeSeed = scene.seed();
            static int seedDraft = static_cast<int>(activeSeed);
            if (activeSeed != scene.seed()) {
                activeSeed = scene.seed();
                seedDraft = static_cast<int>(activeSeed);
            }
            if (ImGui::InputInt("World seed", &seedDraft)) seedDraft = std::max(0, seedDraft);
            if (ImGui::Button("Generate from seed")) {
                scene.randomize(static_cast<std::uint32_t>(seedDraft));
                actions.skipElapsed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Random world")) {
                static std::mt19937 generator{std::random_device{}()};
                settings.seed = std::uniform_int_distribution<int>{0, std::numeric_limits<int>::max()}(generator);
                scene.randomize(static_cast<std::uint32_t>(settings.seed));
                actions.skipElapsed = true;
            }
            ImGui::TextWrapped("Regenerates food and roaming hostiles. Reset replays the current seed. Changing the ant count also restarts the world.");
            auto parameters = scene.parameters();
            ImGui::SetNextItemWidth(190.0F);
            if (slider("Food interval (s)", parameters.foodSpawnInterval, 1.0F, 120.0F)) {
                scene.setParameters(parameters);
            }
            const auto activeSources = std::count_if(scene.foodSources().begin(), scene.foodSources().end(),
                [](const FoodSource& source) { return source.remaining > 0; });
            ImGui::Text("Next food: %.1f s | Patches: %zu / %zu", scene.secondsUntilFoodSpawn(),
                static_cast<std::size_t>(activeSources), AntScene::MaxFoodSources);
            ImGui::TextWrapped("Adds up to 2 random food patches each interval, keeping existing food. At 24 active patches, waits for space. Uses simulation time; pausing freezes the timer.");
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.95F, 0.72F, 0.22F, 1), "GOLD / Nest");
            ImGui::TextColored(ImVec4(0.35F, 0.95F, 0.35F, 1), "GREEN / Food");
            ImGui::TextColored(ImVec4(1.0F, 0.22F, 0.20F, 1), "RED / Hostile creatures");
            ImGui::TextWrapped("Ants explore, collect food and return to the nest. Hostiles roam the ground; nearby ants flee and may detour around them.");
            ImGui::TextWrapped("Pheromones start empty. Exploring ants leave home trails; returning ants leave food trails. Trails diffuse and evaporate over time.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Behavior")) {
            auto parameters = scene.parameters();
            bool changed = slider("Speed", parameters.speed, 0.1F, 20.0F);
            changed |= slider("Sensor distance", parameters.sensorDistance, 0.1F, 10.0F);
            changed |= slider("Sensor angle", parameters.sensorAngle, 0.05F, 1.5F);
            changed |= slider("Turn speed", parameters.turnSpeed, 0.1F, 12.0F);
            changed |= slider("Wander", parameters.wander, 0.0F, 8.0F);
            changed |= slider("Deposit rate", parameters.depositRate, 0.0F, 20.0F);
            changed |= slider("Evaporation", parameters.evaporation, 0.0F, 3.0F);
            changed |= slider("Diffusion", parameters.diffusion, 0.0F, 8.0F);
            if (changed) scene.setParameters(parameters);
            if (ImGui::Button("Clear pheromones")) scene.clearPheromones();
            ImGui::TextWrapped("Higher evaporation shortens trail memory. Higher diffusion spreads trails. New food appears automatically; set its interval in World.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("View")) {
            ImGui::Checkbox("Home pheromone (blue)", &settings.showHomePheromone);
            ImGui::Checkbox("Food pheromone (green)", &settings.showFoodPheromone);
            slider("Ant size", settings.agentSize, 0.1F, 1.5F);
            ImGui::BeginDisabled(renderer.supportedAntialiasingSampleCount() == VK_SAMPLE_COUNT_1_BIT);
            ImGui::Checkbox("Anti-aliasing (MSAA)", &antialiasing);
            ImGui::EndDisabled();
            ImGui::Checkbox("Floor grid", &grid);
            ImGui::Checkbox("World bounds", &bounds);
            if (ImGui::Button("Reset camera")) resetAntCamera(camera);
            ImGui::TextColored(ImVec4(0.92F, 0.86F, 0.63F, 1), "CREAM / Exploring ants");
            ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.12F, 1), "AMBER / Carrying food");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return actions;
}

void makeAntParticles(const AntScene& scene, const config::AntSettings& settings,
    std::vector<core::Particle>& antParticles, std::vector<core::Particle>& environment) {
    antParticles.clear();
    environment.clear();
    const auto agents = scene.agents();
    const auto carrying = scene.carryingFood();
    antParticles.reserve(agents.size());
    for (std::size_t i = 0; i < agents.size(); ++i) {
        const auto& ant = agents[i];
        const glm::vec3 color = carrying[i] ? glm::vec3{1.0F, 0.55F, 0.12F}
                                           : glm::vec3{0.92F, 0.86F, 0.63F};
        antParticles.push_back({ant.position + glm::vec3{0, 0.35F, 0}, 1.0F,
            ant.velocity, 0, color, ant.id});
    }
    constexpr auto resolution = AntScene::GridResolution;
    constexpr float cellSize = 2.0F * AntScene::WorldHalfExtent / static_cast<float>(resolution - 1);
    const auto home = scene.homePheromone();
    const auto food = scene.foodPheromone();
    environment.reserve(resolution * resolution + 256);
    for (std::size_t z = 0; z < resolution; ++z) {
        for (std::size_t x = 0; x < resolution; ++x) {
            const auto index = z * resolution + x;
            const float h = settings.showHomePheromone ? 1.0F - std::exp(-home[index] * 0.7F) : 0.0F;
            const float f = settings.showFoodPheromone ? 1.0F - std::exp(-food[index] * 0.7F) : 0.0F;
            if (std::max(h, f) < 0.025F) continue;
            const glm::vec3 color = glm::min(glm::vec3{0.07F, 0.30F, 0.75F} * h +
                glm::vec3{0.12F, 0.75F, 0.24F} * f, glm::vec3{1.0F});
            dot(environment, {-AntScene::WorldHalfExtent + static_cast<float>(x) * cellSize,
                0.02F, -AntScene::WorldHalfExtent + static_cast<float>(z) * cellSize},
                cellSize * 1.5F, color);
        }
    }
    const auto nest = scene.nestPosition();
    dot(environment, nest + glm::vec3{0, 0.12F, 0}, scene.nestRadius() * 1.5F, {0.30F, 0.18F, 0.06F});
    for (int i = 0; i < 40; ++i) {
        const float angle = static_cast<float>(i) * 2.0F * std::numbers::pi_v<float> / 40.0F;
        dot(environment, nest + glm::vec3{std::cos(angle) * scene.nestRadius(), 0.18F,
            std::sin(angle) * scene.nestRadius()}, 0.4F, {0.95F, 0.72F, 0.22F});
    }
    for (const auto& source : scene.foodSources()) {
        if (source.remaining <= 0) continue;
        const int grains = std::min(18, 1 + source.remaining / 12);
        for (int i = 0; i < grains; ++i) {
            const float angle = static_cast<float>(i) * 2.399963F;
            const float radius = source.radius * 0.8F * std::sqrt(static_cast<float>(i) / 18.0F);
            dot(environment, source.position + glm::vec3{std::cos(angle) * radius, 0.3F,
                std::sin(angle) * radius}, 0.75F, {0.35F, 0.95F, 0.30F});
        }
    }
    for (const auto& enemy : scene.enemies()) {
        dot(environment, enemy.position + glm::vec3{0, 0.5F, 0}, enemy.radius * 2.0F, {0.85F, 0.12F, 0.10F});
        const float length = std::sqrt(enemy.velocity.x * enemy.velocity.x + enemy.velocity.z * enemy.velocity.z);
        const glm::vec3 forward = length > 0 ? enemy.velocity / length : glm::vec3{0, 0, 1};
        const glm::vec3 side{forward.z, 0, -forward.x};
        for (float sign : {-1.0F, 1.0F}) {
            dot(environment, enemy.position + forward * enemy.radius * 0.55F +
                side * sign * enemy.radius * 0.3F + glm::vec3{0, 1.0F, 0}, 0.42F, {1.0F, 0.75F, 0.55F});
        }
    }
}

} // namespace emergences::scenarios::ants
