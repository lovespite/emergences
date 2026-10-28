#include "scenarios/ants3d/Ant3DPresentation.h"
#include "scenarios/ants3d/Ant3DScene.h"
#include "core/Camera.h"
#include "core/FrameRenderer.h"
#include "core/MeshGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <imgui.h>

namespace emergences::scenarios::ants3d {
namespace {
constexpr glm::vec3 gold{0.95F, 0.62F, 0.17F};
constexpr glm::vec3 green{0.38F, 0.78F, 0.18F};
constexpr glm::vec3 carryMarker{0.18F, 1.0F, 0.12F};

const std::vector<core::MeshVertex>& antModel() {
    // Local geometry is reused; each ant is oriented to its walking surface.
    static const auto model = [] {
        std::vector<core::MeshVertex> mesh;
        core::appendEllipsoid(mesh, {0, -0.07F, -0.115F}, {0.09F, 0.075F, 0.125F},
            {0.025F, 0.025F, 0.025F}, glm::mat3{1}, 6, 3);
        core::appendEllipsoid(mesh, {0, -0.07F, 0.015F}, {0.055F, 0.06F, 0.075F},
            {0.045F, 0.045F, 0.045F}, glm::mat3{1}, 6, 3);
        core::appendEllipsoid(mesh, {0, -0.06F, 0.14F}, {0.07F, 0.065F, 0.065F},
            {0.035F, 0.035F, 0.035F}, glm::mat3{1}, 6, 3);
        for (const float side : {-1.0F, 1.0F}) {
            for (int leg = -1; leg <= 1; ++leg) {
                const float z = static_cast<float>(leg) * 0.065F;
                const glm::vec3 root{side * 0.035F, -0.07F, z};
                const glm::vec3 knee{side * 0.14F, -0.08F, z * 1.5F};
                const glm::vec3 foot{side * 0.185F, -Ant3DScene::AntRadius, z * 1.6F};
                core::appendSegment(mesh, root, knee, 0.014F, {0.025F, 0.025F, 0.025F}, 3);
                core::appendSegment(mesh, knee, foot, 0.011F, {0.035F, 0.035F, 0.035F}, 3);
            }
            core::appendSegment(mesh, {side * 0.035F, -0.035F, 0.18F},
                {side * 0.08F, -0.025F, 0.25F}, 0.009F, {0.035F, 0.035F, 0.035F}, 3);
        }
        return mesh;
    }();
    return model;
}


}

void applyAnt3DSettings(Ant3DScene& scene, const config::Ant3DSettings& settings) {
    scene.setMovementSpeed(settings.movementSpeed);
    scene.setNestExitCount(static_cast<std::size_t>(settings.nestExitCount));
    scene.setNestEntryCount(static_cast<std::size_t>(settings.nestEntryCount));
    scene.setFoodSpawnInterval(settings.foodSpawnInterval);
    scene.setCount(static_cast<std::size_t>(settings.count));
    scene.randomize(static_cast<std::uint32_t>(settings.seed));
    scene.setPaused(settings.paused);
}

void captureAnt3DSettings(const Ant3DScene& scene, config::Ant3DSettings& settings) {
    settings.count = static_cast<int>(scene.count());
    settings.seed = static_cast<int>(scene.seed());
    settings.paused = scene.paused();
    settings.movementSpeed = scene.movementSpeed();
    settings.nestExitCount = static_cast<int>(scene.nestExitCount());
    settings.nestEntryCount = static_cast<int>(scene.nestEntryCount());
    settings.foodSpawnInterval = scene.foodSpawnInterval();
}

void resetAnt3DCamera(core::Camera& camera) {
    // Fit the full map in square and landscape viewports, including hilltops.
    camera.setState({{0.0F, 4.0F, 0.0F}, -0.70F, 0.75F, Terrain::WorldHalfExtent * 4.0F});
}

Ant3DPanelActions drawAnt3DControls(Ant3DScene& scene, config::Ant3DSettings& settings,
    core::Camera& camera, const core::FrameRenderer& renderer, bool& antialiasing,
    std::optional<std::size_t> selectedAnt, bool following, FollowView followView) {
    Ant3DPanelActions actions;
    ImGui::TextDisabled("CPU | Solid terrain | Independent 3D world");
    ImGui::Text("Map: %.0f x %.0f | Seed: %u", static_cast<double>(Terrain::WorldHalfExtent * 2),
        static_cast<double>(Terrain::WorldHalfExtent * 2), static_cast<unsigned>(scene.seed()));
    int count = static_cast<int>(scene.count());
    if (ImGui::SliderInt("Ants", &count, 0, static_cast<int>(Ant3DScene::MaxAnts), "%d", ImGuiSliderFlags_AlwaysClamp)) {
        scene.setCount(static_cast<std::size_t>(std::clamp(count, 0, static_cast<int>(Ant3DScene::MaxAnts))));
        actions.skipElapsed = true;
        actions.worldReset = true;
    }
    float movementSpeed = scene.movementSpeed();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5F);
    if (ImGui::SliderFloat("Movement speed", &movementSpeed, Ant3DScene::MinimumMovementSpeed,
        Ant3DScene::MaximumMovementSpeed, "%.1f units/s", ImGuiSliderFlags_AlwaysClamp) && std::isfinite(movementSpeed))
        scene.setMovementSpeed(movementSpeed);
    bool paused = scene.paused();
    if (ImGui::Checkbox("Paused", &paused)) { scene.setPaused(paused); actions.skipElapsed = true; }
    ImGui::SameLine();
    ImGui::BeginDisabled(!paused);
    actions.step = ImGui::Button("Step");
    ImGui::EndDisabled();
    ImGui::SameLine();
    actions.reset = ImGui::Button("Reset");
    ImGui::Text("Simulation: %.2f s | fixed 120 Hz", scene.elapsedSimulationSeconds());
    const auto returning = std::count(scene.carryingFood().begin(), scene.carryingFood().end(), std::uint8_t{1});
    ImGui::Text("Delivered: %zu | Returning: %zu", scene.deliveredFood(), static_cast<std::size_t>(returning));
    const auto queued = std::count(scene.activities().begin(), scene.activities().end(), AntActivity::Queued);
    const auto cooling = std::count(scene.activities().begin(), scene.activities().end(), AntActivity::CoolingDown);
    ImGui::Text("Outside: %zu | Queued: %zu | Cooling: %zu", scene.count() -
        static_cast<std::size_t>(queued + cooling), static_cast<std::size_t>(queued), static_cast<std::size_t>(cooling));
    std::size_t underground = 0;
    for (std::size_t i = 0; i < scene.count(); ++i)
        if (scene.active(i) && scene.terrain().insideCave(scene.agents()[i].position)) ++underground;
    ImGui::Text("In cave: %zu | Contacts: %zu", underground, scene.collisionContacts());
    if (selectedAnt && *selectedAnt < scene.count()) {
        const auto index = *selectedAnt;
        const char* activity = "Foraging";
        switch (scene.activities()[index]) {
        case AntActivity::Queued: activity = "Waiting inside nest"; break;
        case AntActivity::Leaving: activity = "Leaving nest"; break;
        case AntActivity::Foraging: break;
        case AntActivity::Returning: activity = "Carrying food home"; break;
        case AntActivity::Entering: activity = "Entering nest"; break;
        case AntActivity::CoolingDown: activity = "Resting inside nest"; break;
        }
        ImGui::TextColored(ImVec4(gold.r, gold.g, gold.b, 1), "Selected ant #%u",
            static_cast<unsigned>(scene.agents()[index].id));
        ImGui::Text("%s", activity);
        if (scene.activities()[index] == AntActivity::CoolingDown)
            ImGui::Text("Cooldown: %.1f s", static_cast<double>(scene.cooldownRemaining()[index]));
        ImGui::TextDisabled(following ? "Space / Esc: stop following" :
            scene.active(index) ? "Space: follow selected ant" : "Follow available after leaving the nest");
    } else ImGui::TextDisabled("Space: follow latest ant out of nest");
    ImGui::TextDisabled("%s | F2: switch view",
        followView == FollowView::FirstPerson ? "First person" : "Third person");

    if (ImGui::BeginTabBar("3D ant controls")) {
        if (ImGui::BeginTabItem("World")) {
            ImGui::Spacing();
            int exitCount = static_cast<int>(scene.nestExitCount());
            int entryCount = static_cast<int>(scene.nestEntryCount());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5F);
            if (ImGui::SliderInt("Nest exits", &exitCount, 1, static_cast<int>(Ant3DScene::MaximumNestGates),
                "%d", ImGuiSliderFlags_AlwaysClamp)) {
                scene.setNestExitCount(static_cast<std::size_t>(exitCount));
                actions.skipElapsed = actions.worldReset = true;
            }
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5F);
            if (ImGui::SliderInt("Nest entrances", &entryCount, 1, static_cast<int>(Ant3DScene::MaximumNestGates),
                "%d", ImGuiSliderFlags_AlwaysClamp)) {
                scene.setNestEntryCount(static_cast<std::size_t>(entryCount));
                actions.skipElapsed = actions.worldReset = true;
            }
            ImGui::TextDisabled("Changing gates restarts the colony.");
            float foodInterval = scene.foodSpawnInterval();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5F);
            if (ImGui::SliderFloat("Food spawn interval", &foodInterval, 1.0F, 120.0F, "%.1f s", ImGuiSliderFlags_AlwaysClamp) &&
                std::isfinite(foodInterval))
                scene.setFoodSpawnInterval(foodInterval);
            ImGui::TextWrapped("Food grows every interval of simulated time, up to 24 patches. Changing the interval restarts its timer.");
            ImGui::TextWrapped("Ants explore independently until food is within 10 world units along a clear walking path. Depleted or out-of-range food no longer attracts them.");
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.82F, 0.85F, 0.65F, 1), "CAVERNS / RIVER / BRIDGES / PONDS");
            ImGui::Text("Chambers: %zu | Entrances: %zu | Bridges: %zu",
                scene.terrain().caveChambers().size(), scene.terrain().caveEntrances().size(),
                scene.terrain().water().crossings().size());
            ImGui::TextWrapped("Caverns move to a random dry location with each world seed. Explore winding passages and side chambers. Water blocks ants; bridges connect the river banks.");
            ImGui::Separator();
            static std::uint32_t activeSeed = scene.seed();
            static int seedDraft = static_cast<int>(activeSeed);
            if (activeSeed != scene.seed()) { activeSeed = scene.seed(); seedDraft = static_cast<int>(activeSeed); }
            if (ImGui::InputInt("World seed", &seedDraft)) seedDraft = std::max(0, seedDraft);
            if (ImGui::Button("Generate from seed")) {
                scene.randomize(static_cast<std::uint32_t>(seedDraft));
                resetAnt3DCamera(camera);
                actions.skipElapsed = true;
                actions.worldReset = true;
                actions.cameraChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Random map")) {
                static std::mt19937 random{std::random_device{}()};
                scene.randomize(static_cast<std::uint32_t>(std::uniform_int_distribution<int>{0,
                    std::numeric_limits<int>::max()}(random)));
                resetAnt3DCamera(camera);
                actions.skipElapsed = true;
                actions.worldReset = true;
                actions.cameraChanged = true;
            }
            ImGui::TextWrapped("The world seed reproduces terrain, cave location, nest, ants and food. Generate from seed or Random map restarts the world and opens the overview; Reset replays the current seed. Use View for nest and cave close-ups.");
            ImGui::Separator();
            ImGui::TextWrapped("Ants queue inside the nest. Each entrance and exit admits one ant at a time; all gates can run together. Food delivery starts a hidden 5-30 s rest before rejoining the exit queue.");
            ImGui::TextColored(ImVec4(gold.r, gold.g, gold.b, 1), "GOLD / Nest and selected ant ring");
            ImGui::TextColored(ImVec4(green.r, green.g, green.b, 1), "GREEN / Food and carrying marker");
            ImGui::TextDisabled("BLACK / All ants");
            ImGui::TextColored(ImVec4(0.2F, 0.75F, 0.9F, 1), "BLUE / Water and foraging trails");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Collision")) {
            ImGui::Spacing();
            ImGui::Text("Body radius: %.2f | Diameter: %.2f",
                static_cast<double>(Ant3DScene::AntRadius), static_cast<double>(Ant3DScene::AntRadius * 2));
            ImGui::TextWrapped("Ants have solid bodies. Ground, cave walls, shores and world edges block movement; bridges provide safe river crossings.");
            ImGui::TextWrapped("Ants waiting or cooling down inside the nest are hidden and have no collisions.");
            ImGui::Checkbox("Show collision volumes", &settings.showColliders);
            ImGui::TextDisabled("Cyan rings show the physical body radius.");
            ImGui::Checkbox("Show cave roofs", &settings.showCaveRoofs);
            ImGui::TextWrapped("Hide roofs to inspect chambers and winding passages. Collision remains active in the cutaway view.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("View")) {
            ImGui::Spacing();
            ImGui::Checkbox("Show trails", &settings.showTrails);
            ImGui::Checkbox("Show cave roofs", &settings.showCaveRoofs);
            ImGui::Checkbox("Show collision volumes", &settings.showColliders);
            ImGui::BeginDisabled(renderer.supportedAntialiasingSampleCount() == VK_SAMPLE_COUNT_1_BIT);
            ImGui::Checkbox("Anti-aliasing (MSAA)", &antialiasing);
            ImGui::EndDisabled();
            ImGui::TextWrapped("Click the scene, then use WASD to move, Q/E to descend/ascend, and Shift to move faster. Drag the left or right mouse button to look around from the camera position. Middle-drag pans; the wheel zooms.");
            ImGui::TextWrapped("Click an ant to select it. Space starts or stops following; without a selection it follows the latest ant to leave the nest that is still outside. F2 switches first/third person. Esc returns to your previous camera. Entering the nest also ends follow. The gold ring marks selection; a small green dot means it is carrying food.");
            if (ImGui::Button("Overview")) { resetAnt3DCamera(camera); actions.cameraChanged = true; }
            ImGui::SameLine();
            if (ImGui::Button("Nest close-up")) {
                camera.setState({scene.nestPosition(), -0.7F, 0.7F, 25.0F});
                actions.cameraChanged = true;
            }
            if (ImGui::Button("Cave entrance")) {
                camera.setState({scene.terrain().caveEntrances().front() + glm::vec3{2, 1, 0}, -1.35F, 0.28F, 30.0F});
                actions.cameraChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cave cutaway")) {
                settings.showCaveRoofs = false;
                camera.setState({scene.terrain().caves().front().center, -0.7F, 0.85F, 90.0F});
                actions.cameraChanged = true;
            }
            if (ImGui::Button("Cave interior")) {
                settings.showCaveRoofs = true;
                const auto eye = scene.terrain().caveChambers().front().center + glm::vec3{0, 1.4F, 0};
                camera.setState({eye + glm::vec3{8, 0, 0}, -std::numbers::pi_v<float> / 2.0F, 0.0F, 8.0F});
                actions.cameraChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("River bridges")) {
                camera.setState({scene.terrain().water().crossings().front().center, -0.7F, 0.8F, 70.0F});
                actions.cameraChanged = true;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return actions;
}

void makeAnt3DTerrainMesh(const Ant3DScene& scene, const config::Ant3DSettings& settings,
    std::vector<core::MeshVertex>& output) {
    output.clear();
    const auto& terrain = scene.terrain();
    output.reserve(terrain.mesh().size() + terrain.featureMesh().size() +
                   (settings.showCaveRoofs ? terrain.roofMesh().size() : 0));
    const auto appendTerrain = [&](std::span<const TerrainVertex> mesh) {
        for (const auto& vertex : mesh) output.push_back({vertex.position, vertex.normal, vertex.color});
    };
    appendTerrain(terrain.mesh());
    if (settings.showCaveRoofs) appendTerrain(terrain.roofMesh());
    appendTerrain(terrain.featureMesh());
}

void makeAnt3DMesh(const Ant3DScene& scene, const config::Ant3DSettings& settings,
    std::vector<core::MeshVertex>& output, std::optional<std::size_t> selectedAnt,
    std::optional<std::size_t> hiddenAnt) {
    output.clear();
    const auto& terrain = scene.terrain();
    const auto& model = antModel();
    output.reserve(scene.count() * model.size() + 16000);

    const auto nest = scene.nestPosition();
    core::appendEllipsoid(output, nest + glm::vec3{0, 0.08F, 0},
        {scene.nestRadius() * 0.7F, 0.3F, scene.nestRadius() * 0.7F}, {0.22F, 0.12F, 0.045F}, glm::mat3{1}, 16, 4);
    for (int i = 0; i < 48; ++i) {
        const float angle = static_cast<float>(i) * 2.0F * std::numbers::pi_v<float> / 48.0F;
        auto p = nest + glm::vec3{std::cos(angle) * scene.nestRadius(), 0, std::sin(angle) * scene.nestRadius()};
        // Open the rim along every configured lane's heading.
        const auto alignedWithGate = [&](glm::vec3 gate) {
            auto direction = gate - nest;
            direction.y = 0.0F;
            direction = glm::normalize(direction);
            const auto radial = p - nest;
            return glm::dot(radial, direction) > 0.0F &&
                std::abs(radial.x * direction.z - radial.z * direction.x) < 0.42F;
        };
        bool opening = false;
        for (std::size_t gate = 0; gate < scene.nestExitCount(); ++gate)
            opening |= alignedWithGate(scene.nestExitPosition(gate));
        for (std::size_t gate = 0; gate < scene.nestEntryCount(); ++gate)
            opening |= alignedWithGate(scene.nestEntryPosition(gate));
        if (opening) continue;
        p.y = terrain.baseHeight(p.x, p.z) + 0.12F;
        core::appendEllipsoid(output, p, {0.20F, 0.15F, 0.20F}, gold, glm::mat3{1}, 5, 2);
    }
    for (const auto& food : scene.foodSources()) {
        if (food.remaining <= 0) continue;
        const int grains = std::min(28, 3 + food.remaining / 10);
        for (int i = 0; i < grains; ++i) {
            const float angle = static_cast<float>(i) * 2.399963F;
            const float radius = food.radius * std::sqrt(static_cast<float>(i + 1) / static_cast<float>(grains)) * 0.7F;
            auto p = food.position + glm::vec3{std::cos(angle) * radius, 0, std::sin(angle) * radius};
            p.y = terrain.walkingHeight(p, 0.24F);
            core::appendEllipsoid(output, p, {0.28F, 0.22F, 0.32F}, green, glm::mat3{1}, 6, 3);
        }
    }
    if (settings.showTrails) {
        for (const auto& trail : scene.pheromoneTrail()) {
            auto p = trail.position;
            // Trail positions are body centers; offset to the same local walking floor.
            const auto normal = terrain.surfaceNormal(p);
            p -= normal * (Ant3DScene::AntRadius - 0.02F);
            const auto side = glm::normalize(glm::cross(normal, glm::vec3{0,0,1}));
            const auto forward = glm::cross(side, normal);
            const float r = 0.09F + std::clamp(trail.strength, 0.0F, 1.0F) * 0.08F;
            core::appendQuad(output, p - side*r - forward*r, p - side*r + forward*r,
                p + side*r + forward*r, p + side*r - forward*r,
                glm::vec3{0.12F, 0.46F, 0.56F} * (0.45F + 0.55F * std::clamp(trail.strength, 0.0F, 1.0F)));
        }
    }
    const auto agents = scene.agents();
    for (std::size_t i = 0; i < agents.size(); ++i) {
        if (!scene.active(i) || hiddenAnt == i) continue;
        const auto& ant = agents[i];
        const auto normal = terrain.surfaceNormal(ant.position);
        auto forward = ant.velocity - normal * glm::dot(ant.velocity, normal);
        if (glm::dot(forward, forward) < 1.0e-8F) forward = glm::cross(glm::vec3{1,0,0}, normal);
        forward = glm::normalize(forward);
        const auto side = glm::normalize(glm::cross(normal, forward));
        const glm::mat3 orientation{side, normal, forward};
        for (const auto& vertex : model) output.push_back({ant.position + orientation * vertex.position,
            orientation * vertex.normal, vertex.color});
        if (scene.carryingFood()[i]) core::appendEllipsoid(output,
            ant.position + glm::vec3{0, 0.42F, 0}, {0.085F, 0.085F, 0.085F}, carryMarker, glm::mat3{1}, 8, 4);
        if (selectedAnt == i) {
            constexpr int Segments = 32;
            constexpr float Radius = Ant3DScene::AntRadius + 0.10F;
            const auto center = ant.position - normal * (Ant3DScene::AntRadius - 0.035F);
            for (int segment = 0; segment < Segments; ++segment) {
                const float first = 2.0F * std::numbers::pi_v<float> * static_cast<float>(segment) / Segments;
                const float next = 2.0F * std::numbers::pi_v<float> * static_cast<float>(segment + 1) / Segments;
                core::appendSegment(output, center + Radius * (side * std::cos(first) + forward * std::sin(first)),
                    center + Radius * (side * std::cos(next) + forward * std::sin(next)), 0.018F, gold, 4);
            }
        }
    }
}

void makeAnt3DColliders(const Ant3DScene& scene, std::vector<core::MeshVertex>& output,
    std::optional<std::size_t> hiddenAnt) {
    output.clear();
    constexpr int Segments = 24;
    output.reserve(scene.count() * 3 * Segments * 2);
    for (std::size_t i = 0; i < scene.count(); ++i) {
        if (!scene.active(i) || hiddenAnt == i) continue;
        const auto& ant = scene.agents()[i];
        for (int plane = 0; plane < 3; ++plane) {
            for (int segment = 0; segment < Segments; ++segment) {
                for (int endpoint = 0; endpoint < 2; ++endpoint) {
                    const float angle = 2.0F * std::numbers::pi_v<float> *
                        static_cast<float>(segment + endpoint) / static_cast<float>(Segments);
                    auto p = ant.position;
                    p[plane] += Ant3DScene::AntRadius * std::cos(angle);
                    p[(plane + 1) % 3] += Ant3DScene::AntRadius * std::sin(angle);
                    output.push_back({p, {0, 1, 0}, {0.20F, 0.95F, 1.0F}});
                }
            }
        }
    }
}

}
