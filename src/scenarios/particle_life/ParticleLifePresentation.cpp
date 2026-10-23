#include "scenarios/particle_life/ParticleLifePresentation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>

#include <imgui.h>

#include "core/Camera.h"
#include "core/FrameRenderer.h"
#include "scenarios/particle_life/ParticleLifeScene.h"
#include "sim/Backend.h"

namespace emergences::scenarios::particle_life {
namespace {
bool slider(const char* label, float& value, float low, float high) {
    const float previous = value;
    if (!ImGui::SliderFloat(label, &value, low, high, "%.2f", ImGuiSliderFlags_AlwaysClamp))
        return false;
    value = std::isfinite(value) ? std::clamp(value, low, high) : previous;
    return value != previous;
}

int nextSeed(int seed) {
    // A reproducible sequence; keep seeds representable in the config's signed field.
    return static_cast<int>((static_cast<std::uint32_t>(seed) * 1664525U + 1013904223U) &
        static_cast<std::uint32_t>(std::numeric_limits<int>::max()));
}

void speciesLabel(std::size_t species) {
    const auto color = ParticleLifeScene::speciesColor(species);
    ImGui::TextColored(ImVec4(color.r, color.g, color.b, 1.0F), "%zu", species + 1);
}

void drawMatrix(ParticleLifeScene& scene) {
    const int speciesCount = static_cast<int>(scene.speciesCount());
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0F, 3.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0F, 3.0F));
    if (ImGui::BeginTable("Interaction matrix", speciesCount + 1,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Receiver", ImGuiTableColumnFlags_WidthFixed, 18.0F);
        for (int source = 0; source < speciesCount; ++source)
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("R");
        for (int source = 0; source < speciesCount; ++source) {
            ImGui::TableSetColumnIndex(source + 1);
            speciesLabel(static_cast<std::size_t>(source));
        }
        for (int receiver = 0; receiver < speciesCount; ++receiver) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            speciesLabel(static_cast<std::size_t>(receiver));
            for (int source = 0; source < speciesCount; ++source) {
                ImGui::TableSetColumnIndex(source + 1);
                const auto index = static_cast<std::size_t>(receiver) * ParticleLifeScene::MaxSpecies +
                    static_cast<std::size_t>(source);
                float value = scene.matrix()[index];
                const float strength = std::abs(value);
                const ImVec4 tint = value >= 0.0F
                    ? ImVec4(0.10F, 0.22F + 0.18F * strength, 0.22F, 1.0F)
                    : ImVec4(0.24F + 0.22F * strength, 0.12F, 0.17F, 1.0F);
                ImGui::PushID(static_cast<int>(index));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, tint);
                ImGui::SetNextItemWidth(-1.0F);
                if (ImGui::DragFloat("##force", &value, 0.01F, -1.0F, 1.0F, "%.1f",
                        ImGuiSliderFlags_AlwaysClamp) && std::isfinite(value)) {
                    scene.setInteraction(static_cast<std::size_t>(receiver),
                        static_cast<std::size_t>(source), std::clamp(value, -1.0F, 1.0F));
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Receiver %d <- source %d: %.3f", receiver + 1, source + 1,
                        static_cast<double>(scene.matrix()[index]));
                    ImGui::TextUnformatted("Drag to change; Ctrl+click to enter a value.");
                    ImGui::EndTooltip();
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
}
} // namespace

void applyParticleLifeSettings(ParticleLifeScene& scene, const config::ParticleLifeSettings& settings) {
    auto parameters = scene.parameters();
    parameters.interactionRadius = settings.interactionRadius;
    parameters.force = settings.force;
    parameters.friction = settings.friction;
    parameters.repulsion = settings.repulsion;
    parameters.coreRatio = settings.coreRatio;
    parameters.maxSpeed = settings.maxSpeed;
    scene.setParameters(parameters);
    scene.setCount(static_cast<std::size_t>(settings.count));
    scene.setSpeciesCount(static_cast<std::size_t>(settings.speciesCount));
    scene.setSeed(static_cast<std::uint32_t>(settings.seed));
    if (settings.customMatrix) scene.setMatrix(settings.matrix);
    scene.setPaused(settings.paused);
}

void captureParticleLifeSettings(const ParticleLifeScene& scene, config::ParticleLifeSettings& settings) {
    // The stored preference survives a temporary GPU initialization failure.
    // Only a successful user-requested backend switch updates settings.gpu.
    const auto& parameters = scene.parameters();
    settings.count = static_cast<int>(scene.count());
    settings.speciesCount = static_cast<int>(scene.speciesCount());
    settings.seed = static_cast<int>(scene.seed());
    settings.paused = scene.paused();
    settings.interactionRadius = parameters.interactionRadius;
    settings.force = parameters.force;
    settings.friction = parameters.friction;
    settings.repulsion = parameters.repulsion;
    settings.coreRatio = parameters.coreRatio;
    settings.maxSpeed = parameters.maxSpeed;
    settings.matrix = scene.matrix();
    settings.customMatrix = true;
}

void resetParticleLifeCamera(core::Camera& camera) {
    camera.setState({{-ParticleLifeScene::WorldHalfExtent * 0.2F, 0.0F, 0.0F},
        0.0F, 1.56F, ParticleLifeScene::WorldHalfExtent * 2.875F});
}

ParticleLifePanelActions drawParticleLifeControls(ParticleLifeScene& scene,
    config::ParticleLifeSettings& settings, core::Camera& camera,
    const core::FrameRenderer& renderer, bool& antialiasing, bool& grid, bool& bounds,
    std::string& backendError) {
    ParticleLifePanelActions actions;
    if (ImGui::BeginCombo("Backend", scene.backend() == sim::Backend::CPU ? "CPU" : "GPU compute")) {
        for (const auto backend : {sim::Backend::CPU, sim::Backend::GPU}) {
            ImGui::BeginDisabled(backend == sim::Backend::GPU && !scene.gpuAvailable());
            if (ImGui::Selectable(backend == sim::Backend::CPU ? "CPU" : "GPU compute",
                    scene.backend() == backend)) {
                try {
                    scene.setBackend(backend);
                    settings.gpu = backend == sim::Backend::GPU;
                    backendError.clear();
                    actions.skipElapsed = true;
                } catch (const std::exception& error) {
                    backendError = error.what();
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    if (!backendError.empty()) ImGui::TextWrapped("%s", backendError.c_str());
    ImGui::TextDisabled("%s | %.0f x %.0f wrapped world",
        scene.backend() == sim::Backend::CPU ? "CPU" : "GPU compute",
        static_cast<double>(2.0F * ParticleLifeScene::WorldHalfExtent),
        static_cast<double>(2.0F * ParticleLifeScene::WorldHalfExtent));
    int count = static_cast<int>(scene.count());
    if (ImGui::SliderInt("Particles", &count, 0, static_cast<int>(ParticleLifeScene::MaxParticles),
            "%d", ImGuiSliderFlags_AlwaysClamp) && count != static_cast<int>(scene.count())) {
        scene.setCount(static_cast<std::size_t>(std::clamp(count, 0,
            static_cast<int>(ParticleLifeScene::MaxParticles))));
        actions.skipElapsed = true;
    }
    int speciesCount = static_cast<int>(scene.speciesCount());
    if (ImGui::SliderInt("Species", &speciesCount, 1, static_cast<int>(ParticleLifeScene::MaxSpecies),
            "%d", ImGuiSliderFlags_AlwaysClamp) && speciesCount != static_cast<int>(scene.speciesCount())) {
        scene.setSpeciesCount(static_cast<std::size_t>(std::clamp(speciesCount, 1,
            static_cast<int>(ParticleLifeScene::MaxSpecies))));
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
    ImGui::Text("Simulation: %.2f s | fixed %.0f Hz", scene.elapsedSimulationSeconds(),
        1.0 / ParticleLifeScene::FixedStepSeconds);

    if (ImGui::BeginTabBar("Particle Life controls")) {
        if (ImGui::BeginTabItem("World")) {
            static auto activeSeed = scene.seed();
            static int seedDraft = static_cast<int>(activeSeed);
            if (activeSeed != scene.seed()) {
                activeSeed = scene.seed();
                seedDraft = static_cast<int>(activeSeed);
            }
            if (ImGui::InputInt("Particle seed", &seedDraft)) seedDraft = std::max(0, seedDraft);
            if (ImGui::Button("Generate particles")) {
                if (scene.seed() == static_cast<std::uint32_t>(seedDraft)) scene.reset();
                else scene.setSeed(static_cast<std::uint32_t>(seedDraft));
                actions.skipElapsed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Next seed")) {
                scene.setSeed(static_cast<std::uint32_t>(nextSeed(static_cast<int>(scene.seed()))));
                actions.skipElapsed = true;
            }
            ImGui::TextWrapped("Particles live on a 2D plane and wrap across the world edges. Each color is a species with its own attraction and repulsion rules.");
            ImGui::TextWrapped("Changing particle count, species count or particle seed restarts positions. Reset replays the current seed and keeps the interaction matrix.");
            ImGui::Separator();
            for (std::size_t species = 0; species < scene.speciesCount(); ++species) {
                const auto color = ParticleLifeScene::speciesColor(species);
                const auto labels = scene.species();
                const auto population = std::count(labels.begin(), labels.end(), static_cast<std::uint8_t>(species));
                ImGui::TextColored(ImVec4(color.r, color.g, color.b, 1.0F), "Species %zu", species + 1);
                ImGui::SameLine(110.0F);
                ImGui::Text("%zu particles", static_cast<std::size_t>(population));
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Behavior")) {
            auto parameters = scene.parameters();
            bool changed = slider("Interaction radius", parameters.interactionRadius, 1.0F, 30.0F);
            changed |= slider("Force", parameters.force, 0.0F, 100.0F);
            changed |= slider("Friction", parameters.friction, 0.0F, 20.0F);
            changed |= slider("Core repulsion", parameters.repulsion, 0.0F, 5.0F);
            changed |= slider("Core / radius", parameters.coreRatio, 0.05F, 0.8F);
            changed |= slider("Max speed", parameters.maxSpeed, 1.0F, 100.0F);
            if (changed) scene.setParameters(parameters);
            ImGui::TextWrapped("Particles repel at close range. Beyond the core, the matrix controls attraction or repulsion, fading to zero at the interaction radius.");
            ImGui::TextWrapped("Friction damps motion. Asymmetric attraction can make clusters chase one another. Parameters and matrix edits take effect immediately.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Matrix")) {
            static auto activeSeed = scene.seed();
            static int ruleSeed = static_cast<int>(activeSeed);
            if (activeSeed != scene.seed()) {
                activeSeed = scene.seed();
                ruleSeed = static_cast<int>(activeSeed);
            }
            if (ImGui::InputInt("Rule seed", &ruleSeed)) ruleSeed = std::max(0, ruleSeed);
            if (ImGui::Button("Generate rules"))
                scene.randomizeMatrix(static_cast<std::uint32_t>(ruleSeed));
            ImGui::SameLine();
            if (ImGui::Button("Randomize rules")) {
                ruleSeed = nextSeed(ruleSeed);
                scene.randomizeMatrix(static_cast<std::uint32_t>(ruleSeed));
            }
            ImGui::TextWrapped("Rows: receiver. Columns: source. Positive attracts; negative repels. Rules are asymmetric: A can chase B while B flees A.");
            drawMatrix(scene);
            ImGui::TextWrapped("Drag cells to edit [-1, 1]; Ctrl+click for precise input. The rule seed changes only interactions. Reset keeps these rules.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("View")) {
            slider("Particle size", settings.agentSize, 0.1F, 1.5F);
            ImGui::BeginDisabled(renderer.supportedAntialiasingSampleCount() == VK_SAMPLE_COUNT_1_BIT);
            ImGui::Checkbox("Anti-aliasing (MSAA)", &antialiasing);
            ImGui::EndDisabled();
            ImGui::Checkbox("Floor grid", &grid);
            ImGui::Checkbox("World bounds", &bounds);
            if (ImGui::Button("Reset camera")) resetParticleLifeCamera(camera);
            ImGui::TextWrapped("Colors identify species. The default camera looks down onto the simulation plane.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return actions;
}

void makeParticleLifeParticles(const ParticleLifeScene& scene, std::vector<core::Particle>& particles) {
    particles.clear();
    const auto agents = scene.agents();
    particles.reserve(agents.size());
    for (const auto& agent : agents)
        particles.push_back({agent.position, agent.size, agent.velocity, agent.reserved, agent.color, agent.id});
}

} // namespace emergences::scenarios::particle_life
