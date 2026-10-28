#include "scenarios/ants3d/Ant3DPresentation.h"
#include "scenarios/ants3d/Ant3DScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <iterator>
#include <numbers>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {
using emergences::core::MeshVertex;
using emergences::scenarios::ants3d::Ant3DScene;
using emergences::scenarios::ants3d::AntActivity;
using emergences::scenarios::ants3d::makeAnt3DColliders;
using emergences::scenarios::ants3d::makeAnt3DMesh;
using Vertex = std::array<float, 9>;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Compare geometry without depending on triangle order or model tessellation.
std::vector<Vertex> sortedGeometry(const std::vector<MeshVertex>& mesh) {
    std::vector<Vertex> result;
    result.reserve(mesh.size());
    for (const auto& vertex : mesh) result.push_back({
        vertex.position.x, vertex.position.y, vertex.position.z,
        vertex.normal.x, vertex.normal.y, vertex.normal.z,
        vertex.color.r, vertex.color.g, vertex.color.b});
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<Vertex> addedGeometry(const std::vector<MeshVertex>& mesh,
                                  const std::vector<MeshVertex>& background) {
    const auto rendered = sortedGeometry(mesh);
    const auto hidden = sortedGeometry(background);
    require(std::includes(rendered.begin(), rendered.end(), hidden.begin(), hidden.end()),
            "Hiding an ant must leave the world geometry unchanged");
    std::vector<Vertex> result;
    std::set_difference(rendered.begin(), rendered.end(), hidden.begin(), hidden.end(),
                        std::back_inserter(result));
    return result;
}

bool black(const Vertex& vertex) {
    return std::max({vertex[6], vertex[7], vertex[8]}) <= 0.10F &&
           std::min({vertex[6], vertex[7], vertex[8]}) >= 0.0F &&
           std::abs(vertex[6] - vertex[7]) < 0.015F &&
           std::abs(vertex[7] - vertex[8]) < 0.015F;
}

bool green(const Vertex& vertex) {
    return vertex[7] > 0.8F && vertex[7] > vertex[6] * 2.0F &&
           vertex[7] > vertex[8] * 2.0F;
}

std::vector<Vertex> blackGeometry(const std::vector<MeshVertex>& mesh) {
    auto result = sortedGeometry(mesh);
    std::erase_if(result, [](const auto& vertex) { return !black(vertex); });
    return result;
}

template<class Predicate>
void advanceUntil(Ant3DScene& scene, Predicate&& condition, const char* message) {
    constexpr std::size_t MaximumTicks = 120 * 120;
    for (std::size_t tick = 0; tick < MaximumTicks && !condition(); ++tick)
        scene.advance(Ant3DScene::FixedStepSeconds);
    require(condition(), message);
}

void placeNearbyFood(Ant3DScene& scene) {
    // Appearance checks need a known collection event, independently of where
    // an unladen ant happens to explore. Keep the food outside pickup range
    // but within local detection, along a genuinely walkable short segment.
    const auto start = scene.agents()[0].position;
    for (int direction = 0; direction < 32; ++direction) {
        const float angle = static_cast<float>(direction) * 2.0F * std::numbers::pi_v<float> / 32.0F;
        const glm::vec3 offset{4.0F * std::cos(angle), 0.0F, 4.0F * std::sin(angle)};
        auto previous = start;
        bool clear = true;
        for (int sample = 1; sample <= 32; ++sample) {
            auto point = start + offset * (static_cast<float>(sample) / 32.0F);
            point.y = scene.terrain().navigationHeight(point.x, point.z) + Ant3DScene::AntRadius;
            point.y = scene.terrain().walkingHeight(point, Ant3DScene::AntRadius);
            if (!scene.terrain().sphereClear(point, Ant3DScene::AntRadius) || std::abs(point.y - previous.y) > 0.25F) {
                clear = false;
                break;
            }
            previous = point;
        }
        if (!clear) continue;
        auto& food = const_cast<emergences::scenarios::ants3d::FoodSource&>(scene.foodSources()[0]);
        food.position = previous;
        food.position.y = scene.terrain().navigationHeight(previous.x, previous.z) + 0.08F;
        food.radius = 0.5F;
        require(glm::length(food.position - start) > food.radius + Ant3DScene::AntRadius &&
                glm::length(food.position - start) < Ant3DScene::FoodDetectionRadius,
                "The food fixture must require walking within local detection range");
        return;
    }
    throw std::runtime_error("A nearby food fixture must have a clear approach from the nest");
}

void lifecycleGeometry() {
    emergences::config::Ant3DSettings settings;
    settings.showTrails = false;
    Ant3DScene scene(1, 173), empty(0, 173);
    std::vector<MeshVertex> mesh, background, selected, colliders;

    makeAnt3DMesh(empty, settings, background);
    makeAnt3DMesh(scene, settings, mesh, 0);
    require(sortedGeometry(mesh) == sortedGeometry(background),
            "Initially queued ants must not render bodies, markers or selection rings");
    makeAnt3DColliders(scene, colliders);
    require(colliders.empty(), "Initially queued ants must not render collision volumes");

    advanceUntil(scene, [&] { return scene.active(0); }, "The queued ant must emerge");
    makeAnt3DMesh(scene, settings, mesh);
    makeAnt3DMesh(scene, settings, background, std::nullopt, 0);
    const auto body = addedGeometry(mesh, background);
    require(!body.empty(), "A visible ant must render a body");
    require(std::all_of(body.begin(), body.end(), black),
            "All visible ant body parts must be black");
    require(blackGeometry(background).empty(), "First-person hiding must omit the ant body");
    makeAnt3DColliders(scene, colliders);
    require(!colliders.empty(), "A visible ant must render its collision volume when requested");
    makeAnt3DColliders(scene, colliders, 0);
    require(colliders.empty(), "First-person hiding must omit the followed ant collision volume");

    makeAnt3DMesh(scene, settings, selected, 0);
    require(!addedGeometry(selected, mesh).empty(), "Selecting a visible ant must add a visible highlight");
    require(blackGeometry(selected) == blackGeometry(mesh),
            "Selection must leave the black body unchanged");
    makeAnt3DMesh(scene, settings, selected, 0, 0);
    require(sortedGeometry(selected) == sortedGeometry(background),
            "First-person hiding must also omit the selection highlight");

    advanceUntil(scene, [&] { return scene.activities()[0] == AntActivity::Foraging; },
                 "The ant must finish its exit before arranging the collection fixture");
    placeNearbyFood(scene);
    const auto beforeCollection = scene.agents()[0].position;
    advanceUntil(scene, [&] { return scene.carryingFood()[0] != 0; },
                 "The ant must discover and collect nearby food for the marker check");
    require(glm::length(scene.agents()[0].position - beforeCollection) > 2.0F,
            "The marker fixture must exercise actual movement toward locally detected food");
    makeAnt3DMesh(scene, settings, mesh);
    makeAnt3DMesh(scene, settings, background, std::nullopt, 0);
    const auto laden = addedGeometry(mesh, background);
    require(std::any_of(laden.begin(), laden.end(), black), "A carrying ant must retain its black body");
    bool markerSeen = false;
    const auto position = scene.agents()[0].position;
    for (const auto& vertex : laden) {
        if (black(vertex)) continue;
        require(green(vertex), "The carrying indicator must be green, with no body recoloring");
        require(vertex[1] > position.y + Ant3DScene::AntRadius,
                "The carrying indicator must float above the ant");
        const float x = vertex[0] - position.x, z = vertex[2] - position.z;
        require(x * x + z * z < Ant3DScene::AntRadius * Ant3DScene::AntRadius,
                "The carrying indicator must remain a small dot above its ant");
        markerSeen = true;
    }
    require(markerSeen, "A carrying ant must render a green indicator");
    const auto hiddenLaden = sortedGeometry(background);
    require(std::none_of(hiddenLaden.begin(), hiddenLaden.end(), green),
            "First-person hiding must omit the carrying indicator");
    makeAnt3DMesh(scene, settings, selected, 0, 0);
    require(sortedGeometry(selected) == hiddenLaden,
            "A followed carrying ant must not render its body, marker or selection highlight");

    advanceUntil(scene, [&] { return scene.activities()[0] == AntActivity::CoolingDown; },
                 "The ant must naturally deliver food and enter cooldown");
    require(scene.deliveredFood() > 0 && scene.cooldownRemaining()[0] >= Ant3DScene::MinimumCooldownSeconds &&
            scene.cooldownRemaining()[0] <= Ant3DScene::MaximumCooldownSeconds,
            "A food delivery must start the configured cooldown");
    makeAnt3DMesh(scene, settings, mesh, 0);
    makeAnt3DMesh(scene, settings, background, 0, 0);
    const auto coolingGeometry = sortedGeometry(mesh);
    require(coolingGeometry == sortedGeometry(background) &&
            std::none_of(coolingGeometry.begin(), coolingGeometry.end(), [](const auto& vertex) {
                return black(vertex) || green(vertex);
            }), "Cooling ants must not render bodies, carrying dots or selection highlights");
    makeAnt3DColliders(scene, colliders);
    require(colliders.empty(), "Cooling ants must not render collision volumes");
}

void settingsAndFollowGeometry() {
    using namespace emergences::scenarios::ants3d;
    emergences::config::Ant3DSettings settings;
    settings.count = 2;
    settings.seed = 173;
    settings.movementSpeed = 8.0F;
    settings.nestExitCount = 3;
    settings.nestEntryCount = 2;
    settings.foodSpawnInterval = 7.0F;
    Ant3DScene scene(0, 173);
    applyAnt3DSettings(scene, settings);
    emergences::config::Ant3DSettings captured;
    captureAnt3DSettings(scene, captured);
    require(captured.count == settings.count && captured.seed == settings.seed &&
            captured.movementSpeed == settings.movementSpeed &&
            captured.nestExitCount == settings.nestExitCount &&
            captured.nestEntryCount == settings.nestEntryCount &&
            captured.foodSpawnInterval == settings.foodSpawnInterval,
            "Applying and capturing settings must preserve all 3D colony controls");

    advanceUntil(scene, [&] { return scene.latestExitedAnt().has_value(); },
                 "A configured exit must complete departure");
    emergences::core::Camera camera;
    Ant3DInteraction interaction;
    require(interaction.toggleFollow(scene, camera), "Auto follow must select a departed ant");
    std::vector<MeshVertex> firstPerson, thirdPerson, expected;
    const auto drawFollowing = [&](auto& output) {
        makeAnt3DMesh(scene, settings, output, interaction.selectedAnt(),
            interaction.firstPersonFollowing() ? interaction.selectedAnt() : std::nullopt);
    };
    drawFollowing(firstPerson);
    interaction.toggleFollowView(scene, camera);
    drawFollowing(thirdPerson);
    makeAnt3DMesh(scene, settings, expected, interaction.selectedAnt());
    require(sortedGeometry(thirdPerson) == sortedGeometry(expected) &&
            !addedGeometry(thirdPerson, firstPerson).empty(),
            "Third-person follow must restore the selected ant's body and ring");
    interaction.toggleFollowView(scene, camera);
    drawFollowing(expected);
    require(sortedGeometry(expected) == sortedGeometry(firstPerson),
            "Switching back to first person must hide the body and ring again");
}
} // namespace

int main() {
    try { lifecycleGeometry(); settingsAndFollowGeometry(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "3D ant visibility, black bodies, carrying markers and selection geometry tests passed\n";
    return 0;
}
