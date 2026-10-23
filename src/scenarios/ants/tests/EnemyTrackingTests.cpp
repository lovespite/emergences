#include "scenarios/ants/AntScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace {

using emergences::scenarios::ants::AntScene;
using emergences::scenarios::ants::Enemy;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void freezeColony(AntScene& scene) {
    auto parameters = scene.parameters();
    parameters.speed = 0.0F;
    parameters.depositRate = 0.0F;
    parameters.diffusion = 0.0F;
    parameters.evaporation = 0.0F;
    parameters.foodSpawnInterval = 120.0F;
    scene.setParameters(parameters);
}

void prepareOpenGround(AntScene& scene) {
    scene.setObstacleCount(0);
    freezeColony(scene);
}

// The owned scenes are non-const. Mutating elements of their borrowed views
// creates controlled behavioral fixtures without exposing production setters.
void placeGroup(AntScene& scene, std::size_t first, std::size_t count,
                const glm::vec3& center) {
    auto* ants = const_cast<emergences::sim::Agent*>(scene.agents().data());
    for (std::size_t index = 0; index < count; ++index) {
        const float x = static_cast<float>(index % 5) - 2.0F;
        const float z = static_cast<float>((index / 5) % 5) - 2.0F;
        ants[first + index].position = center + glm::vec3{x * 0.15F, 0.0F, z * 0.15F};
        ants[first + index].velocity = glm::vec3{0.0F};
    }
}

void placeColony(AntScene& scene, const glm::vec3& center) {
    placeGroup(scene, 0, scene.count(), center);
}

Enemy& firstEnemy(AntScene& scene) {
    return const_cast<Enemy&>(scene.enemies().front());
}

void tick(AntScene& scene) {
    require(scene.advance(AntScene::FixedStepSeconds) == 1,
            "Tracking fixtures must advance one fixed simulation step");
}

void ticks(AntScene& scene, int count) {
    for (int index = 0; index < count; ++index) tick(scene);
}

bool sameEnemy(const Enemy& left, const Enemy& right) {
    return left.position == right.position && left.velocity == right.velocity &&
           left.radius == right.radius;
}

void requireClearEnemies(const AntScene& scene) {
    for (const auto& enemy : scene.enemies()) {
        require(std::isfinite(enemy.position.x) && std::isfinite(enemy.position.z),
                "Tracking must retain finite enemy positions");
        for (const auto& obstacle : scene.obstacles()) {
            require(glm::length(enemy.position - obstacle.position) + 1.0e-4F >=
                        enemy.radius + obstacle.radius,
                    "Tracking enemies must not pass through rock surfaces");
        }
    }
}

bool reaches(AntScene& scene, const glm::vec3& target, int maximumSteps,
             bool protectNest = false) {
    for (int index = 0; index < maximumSteps; ++index) {
        tick(scene);
        requireClearEnemies(scene);
        if (protectNest) {
            require(glm::length(scene.enemies().front().position - scene.nestPosition()) >=
                        9.0F - 1.0e-4F,
                    "A tracking route must stay outside the protected nest area");
        }
        if (glm::length(scene.enemies().front().position - target) < 3.0F) return true;
    }
    return false;
}

void denseGroupWinsOverCloserSparseGroup() {
    AntScene scene{48, 12451U};
    prepareOpenGround(scene);
    const auto start = scene.enemies().front().position;
    const auto radial = glm::normalize(start);
    const glm::vec3 tangent{-radial.z, 0.0F, radial.x};
    const auto dense = start - radial * 10.0F + tangent * 18.0F;
    const auto sparse = start - radial * 8.0F - tangent * 6.0F;
    placeGroup(scene, 0, 40, dense);
    placeGroup(scene, 40, 8, sparse);
    require(glm::length(sparse - start) < glm::length(dense - start),
            "The sparse group must be the closer alternative in this fixture");

    float closestSparse = glm::length(start - sparse);
    bool reachedDense = false;
    for (int index = 0; index < 2400; ++index) {
        tick(scene);
        const auto position = scene.enemies().front().position;
        closestSparse = std::min(closestSparse, glm::length(position - sparse));
        if (glm::length(position - dense) < 3.0F) {
            reachedDense = true;
            break;
        }
    }
    require(reachedDense,
            "Enemies must reach the denser nearby group even when another group is closer");
    require(closestSparse > 4.0F,
            "The enemy must choose the dense group before visiting the sparse alternative");
}

void outsideRangeAndProtectedGroupsPreserveWandering() {
    AntScene protectedColony{32, 86420U};
    AntScene distantColony{32, 86420U};
    prepareOpenGround(protectedColony);
    prepareOpenGround(distantColony);
    const auto radial = glm::normalize(distantColony.enemies().front().position);
    placeColony(protectedColony, protectedColony.nestPosition());
    placeColony(distantColony, -radial * 60.0F);

    for (int index = 0; index < 360; ++index) {
        tick(protectedColony);
        tick(distantColony);
        require(sameEnemy(protectedColony.enemies().front(), distantColony.enemies().front()),
                "Groups outside sensing range or inside the nest must leave roaming unchanged");
    }
}

void movingAndDisappearingGroupsInvalidateOldRoutes() {
    AntScene scene{32, 15531U};
    prepareOpenGround(scene);
    const auto start = scene.enemies().front().position;
    const auto radial = glm::normalize(start);
    const glm::vec3 tangent{-radial.z, 0.0F, radial.x};
    const auto original = start - radial * 8.0F + tangent * 14.0F;
    placeColony(scene, original);
    ticks(scene, 480);
    require(glm::length(scene.enemies().front().position - original) <
                glm::length(start - original) - 2.0F,
            "The enemy must begin following the original group");

    const auto beforeRetarget = scene.enemies().front().position;
    const auto moved = beforeRetarget - tangent * 18.0F;
    placeColony(scene, moved);
    require(reaches(scene, moved, 2400),
            "An enemy must replan and reach a group that moves away from its old route");

    const auto beforeDisappearance = scene.enemies().front().position;
    placeColony(scene, scene.nestPosition());
    ticks(scene, 720);
    require(glm::length(scene.enemies().front().position - beforeDisappearance) > 6.0F,
            "After the group disappears, the enemy must resume roaming instead of circling its old target");
}

void resetDiscardsTrackingHistory() {
    constexpr std::uint32_t seed = 75221U;
    AntScene reused{32, seed};
    prepareOpenGround(reused);
    const auto start = reused.enemies().front().position;
    const auto radial = glm::normalize(start);
    const glm::vec3 tangent{-radial.z, 0.0F, radial.x};
    placeColony(reused, start - radial * 8.0F + tangent * 14.0F);
    ticks(reused, 480);
    reused.reset();

    AntScene fresh{32, seed};
    prepareOpenGround(fresh);
    const auto newTarget = start - radial * 8.0F - tangent * 14.0F;
    placeColony(reused, newTarget);
    placeColony(fresh, newTarget);
    for (int index = 0; index < 480; ++index) {
        tick(reused);
        tick(fresh);
        require(std::equal(reused.enemies().begin(), reused.enemies().end(),
                           fresh.enemies().begin(), fresh.enemies().end(), sameEnemy),
                "Reset must discard cached targets and paths and reproduce a fresh scene");
    }
}

void routesAroundGeneratedRock() {
    AntScene scene{32, 16033U};
    scene.setObstacleCount(1);
    freezeColony(scene);
    const auto& rock = scene.obstacles().front();
    auto& enemy = firstEnemy(scene);
    const auto radial = glm::normalize(rock.position);
    const glm::vec3 across{-radial.z, 0.0F, radial.x};
    const float offset = rock.radius + enemy.radius + 4.0F;
    enemy.position = rock.position - across * offset;
    const auto target = rock.position + across * offset;
    placeColony(scene, target);
    requireClearEnemies(scene);
    require(reaches(scene, target, 4800),
            "An enemy must find a route to a group on the far side of a generated rock");
}

void routesAroundProtectedNest() {
    AntScene scene{32, 44711U};
    prepareOpenGround(scene);
    firstEnemy(scene).position = {14.0F, 0.0F, 0.0F};
    const glm::vec3 target{-14.0F, 0.0F, 0.0F};
    placeColony(scene, target);
    require(reaches(scene, target, 4800, true),
            "An enemy must route around the nest to a nearby group on its opposite side");
}

} // namespace

int main() {
    try {
        denseGroupWinsOverCloserSparseGroup();
        outsideRangeAndProtectedGroupsPreserveWandering();
        movingAndDisappearingGroupsInvalidateOldRoutes();
        resetDiscardsTrackingHistory();
        routesAroundGeneratedRock();
        routesAroundProtectedNest();
        std::cout << "Enemy tracking tests passed (density, range, replanning, replay, and obstacle routes)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Enemy tracking test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
