#include "scenarios/ants/EnemyPathfinder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include "scenarios/ants/AntScene.h"

namespace {

using emergences::scenarios::ants::EnemyPathfinder;
using emergences::scenarios::ants::Obstacle;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

float segmentSeparationSquared(glm::vec3 start, glm::vec3 end, glm::vec3 center) {
    const float dx = end.x - start.x;
    const float dz = end.z - start.z;
    const float lengthSquared = dx * dx + dz * dz;
    const float projection = lengthSquared > 0.0F
                                 ? std::clamp(((center.x - start.x) * dx + (center.z - start.z) * dz) /
                                                  lengthSquared,
                                              0.0F, 1.0F)
                                 : 0.0F;
    const float separationX = start.x + projection * dx - center.x;
    const float separationZ = start.z + projection * dz - center.z;
    return separationX * separationX + separationZ * separationZ;
}

void requireSafeRoute(glm::vec3 start, glm::vec3 goal, std::span<const glm::vec3> path,
                      std::span<const Obstacle> obstacles, float clearance) {
    require(!path.empty(), "A reachable goal must produce a route");
    require(path.back() == goal, "The last waypoint must be the exact requested goal");
    for (const auto waypoint : path) {
        require(std::isfinite(waypoint.x) && std::isfinite(waypoint.y) && std::isfinite(waypoint.z),
                "Waypoints must be finite");
        require(std::abs(waypoint.x) <= 80.0F - clearance && std::abs(waypoint.z) <= 80.0F - clearance,
                "Waypoints must retain clearance from world boundaries");
        require(segmentSeparationSquared(start, waypoint, glm::vec3{0.0F}) >= 81.0F - 0.0001F,
                "Every route segment must avoid the protected nest");
        for (const auto& obstacle : obstacles) {
            const float radius = obstacle.radius + clearance;
            require(segmentSeparationSquared(start, waypoint, obstacle.position) >= radius * radius - 0.0001F,
                    "Every route segment, including endpoint links and shortcuts, must avoid inflated obstacles");
        }
        start = waypoint;
    }
}

void directRoutesAndNestProtection() {
    EnemyPathfinder finder;
    require(!finder.navigable({20.0F, 0.0F, 20.0F}), "An unbuilt navigator has no valid terrain");
    finder.rebuild({}, 1.5F);
    const glm::vec3 start{-40.3F, 0.0F, 30.1F};
    const glm::vec3 goal{40.1F, 0.0F, 35.3F};
    const auto straight = finder.findPath(start, goal);
    require(straight.size() == 1, "An unobstructed route should go directly to the goal");
    requireSafeRoute(start, goal, straight, {}, 1.5F);
    const glm::vec3 acrossStart{-20.0F, 0.0F, 0.0F};
    const glm::vec3 acrossGoal{20.0F, 0.0F, 0.0F};
    const auto aroundNest = finder.findPath(acrossStart, acrossGoal);
    require(aroundNest.size() > 1, "Routes across the nest must take a detour");
    requireSafeRoute(acrossStart, acrossGoal, aroundNest, {}, 1.5F);
    require(finder.findPath(start, {0.0F, 0.0F, 0.0F}).empty(), "A goal inside the nest is invalid");
    require(finder.findPath({8.0F, 0.0F, 0.0F}, goal).empty(), "A start inside the nest is invalid");
    require(finder.findPath(start, {78.6F, 0.0F, 30.0F}).empty(), "Goals must respect boundary clearance");
    require(finder.findPath(start, {std::numeric_limits<float>::quiet_NaN(), 0.0F, 30.0F}).empty(),
            "Nonfinite endpoints must be rejected");
    require(finder.findPath({std::numeric_limits<float>::infinity(), 0.0F, 30.0F}, goal).empty(),
            "Nonfinite starts must be rejected");
}

void obstacleDetoursAndDeterminism() {
    const std::vector<Obstacle> obstacles{{{30.0F, 0.0F, 30.0F}, 8.0F}};
    const glm::vec3 start{12.2F, 0.0F, 30.0F};
    const glm::vec3 goal{49.1F, 0.0F, 30.0F};
    EnemyPathfinder finder;
    finder.rebuild(obstacles, 1.9F);
    const auto route = finder.findPath(start, goal);
    require(route.size() > 1, "A large obstacle must cause a detour");
    requireSafeRoute(start, goal, route, obstacles, 1.9F);
    for (int repeat = 0; repeat < 8; ++repeat) {
        require(finder.findPath(start, goal) == route,
                "Symmetric route branches and equal A* costs must resolve deterministically");
    }
    finder.rebuild(obstacles, 1.9F);
    require(finder.findPath(start, goal) == route, "Rebuilding the same terrain must preserve the route");
    require(finder.findPath(start, {39.0F, 0.0F, 30.0F}).empty(),
            "A goal outside a rock but inside the creature clearance is invalid");
    auto retained = finder;
    finder.rebuild({}, 1.9F);
    require(finder.findPath(start, goal).size() == 1, "Rebuild must discard old obstacle connections");
    require(retained.findPath(start, goal) == route, "Copied navigators must retain independent terrain caches");
}

void diagonalEdgesAndExactEndpointLinks() {
    const std::vector<Obstacle> diagonalObstacle{{{21.0F, 0.0F, 21.0F}, 0.85F}};
    EnemyPathfinder finder;
    finder.rebuild(diagonalObstacle, 0.15F);
    const glm::vec3 start{20.0F, 0.0F, 20.0F};
    const glm::vec3 goal{22.0F, 0.0F, 22.0F};
    require(finder.navigable(start) && finder.navigable(goal), "Both diagonal endpoints must be unblocked");
    const auto diagonalRoute = finder.findPath(start, goal);
    require(diagonalRoute.size() > 1, "A diagonal edge must not cut through a rock between free nodes");
    requireSafeRoute(start, goal, diagonalRoute, diagonalObstacle, 0.15F);

    const std::vector<Obstacle> endpointObstacle{{{20.0F, 0.0F, 20.0F}, 1.0F}};
    finder.rebuild(endpointObstacle, 0.25F);
    const glm::vec3 nearBlockedNode{20.9F, 0.0F, 20.9F};
    const glm::vec3 endpointGoal{17.0F, 0.0F, 17.0F};
    require(finder.navigable(nearBlockedNode), "The precise start should be outside the expanded obstacle");
    require(!finder.navigable({20.0F, 0.0F, 20.0F}), "The nearest grid node should be blocked");
    requireSafeRoute(nearBlockedNode, endpointGoal, finder.findPath(nearBlockedNode, endpointGoal),
                     endpointObstacle, 0.25F);
    requireSafeRoute(endpointGoal, nearBlockedNode, finder.findPath(endpointGoal, nearBlockedNode),
                     endpointObstacle, 0.25F);
}

void unreachableAndReopenedTerrain() {
    std::vector<Obstacle> wall;
    for (int z = -78; z <= 78; z += 6) wall.push_back({{0.0F, 0.0F, static_cast<float>(z)}, 4.0F});
    EnemyPathfinder finder;
    finder.rebuild(wall, 1.5F);
    const glm::vec3 start{-35.0F, 0.0F, 30.0F};
    const glm::vec3 goal{35.0F, 0.0F, 30.0F};
    require(finder.navigable(start) && finder.navigable(goal), "The wall must separate valid endpoints");
    require(finder.findPath(start, goal).empty(), "A wall connecting world boundaries must be impassable");
    wall.erase(std::remove_if(wall.begin(), wall.end(), [](const Obstacle& obstacle) {
        return obstacle.position.z > 18.0F && obstacle.position.z < 42.0F;
    }), wall.end());
    finder.rebuild(wall, 1.5F);
    requireSafeRoute(start, goal, finder.findPath(start, goal), wall, 1.5F);
}

} // namespace

int main() {
    try {
        directRoutesAndNestProtection();
        obstacleDetoursAndDeterminism();
        diagonalEdgesAndExactEndpointLinks();
        unreachableAndReopenedTerrain();
        std::cout << "Enemy pathfinder tests passed (A*, clearance, endpoints, nest, reachability, and determinism)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Enemy pathfinder test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
