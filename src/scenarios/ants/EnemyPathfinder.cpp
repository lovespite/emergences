#include "scenarios/ants/EnemyPathfinder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

#include "scenarios/ants/AntScene.h"

namespace emergences::scenarios::ants {
namespace {

constexpr int GridSide = 79;
constexpr int NodeCount = GridSide * GridSide;
constexpr float GridSpacing = 2.0F;
constexpr float GridOrigin = -78.0F;
constexpr std::array<int, 8> OffsetX{-1, 0, 1, -1, 1, -1, 0, 1};
constexpr std::array<int, 8> OffsetZ{-1, -1, -1, 0, 0, 1, 1, 1};

glm::vec3 nodePosition(int node) noexcept {
    return {GridOrigin + static_cast<float>(node % GridSide) * GridSpacing, 0.0F,
            GridOrigin + static_cast<float>(node / GridSide) * GridSpacing};
}

bool finite(glm::vec3 position) noexcept {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

float distance(glm::vec3 left, glm::vec3 right) noexcept {
    return std::hypot(left.x - right.x, left.z - right.z);
}

int nearestCoordinate(float coordinate) noexcept {
    return std::clamp(static_cast<int>(std::lround((coordinate - GridOrigin) / GridSpacing)),
                      0, GridSide - 1);
}

struct OpenNode {
    float estimate;
    float cost;
    int node;
};

struct LaterNode {
    bool operator()(const OpenNode& left, const OpenNode& right) const noexcept {
        if (left.estimate != right.estimate) return left.estimate > right.estimate;
        if (left.cost != right.cost) return left.cost > right.cost;
        return left.node > right.node;
    }
};

} // namespace

void EnemyPathfinder::rebuild(std::span<const Obstacle> obstacles, float clearanceRadius) {
    if (!std::isfinite(clearanceRadius) || clearanceRadius < 0.0F ||
        clearanceRadius >= AntScene::WorldHalfExtent) {
        throw std::invalid_argument("Enemy navigation clearance must be finite and in [0, 80)");
    }
    std::vector<Circle> circles;
    circles.reserve(obstacles.size() + 1);
    circles.push_back({glm::vec3{0.0F}, 9.0F * 9.0F});
    for (const auto& obstacle : obstacles) {
        const float radius = obstacle.radius + clearanceRadius;
        if (!finite(obstacle.position) || !std::isfinite(obstacle.radius) ||
            obstacle.radius < 0.0F || !std::isfinite(radius * radius)) {
            throw std::invalid_argument("Enemy navigation obstacles must be finite and nonnegative");
        }
        circles.push_back({obstacle.position, radius * radius});
    }

    circles_ = std::move(circles);
    boundary_ = AntScene::WorldHalfExtent - clearanceRadius;
    navigableNodes_.assign(NodeCount, 0);
    connections_.assign(NodeCount, 0);
    for (int node = 0; node < NodeCount; ++node) {
        navigableNodes_[node] = navigable(nodePosition(node)) ? 1 : 0;
    }
    for (int node = 0; node < NodeCount; ++node) {
        if (!navigableNodes_[node]) continue;
        const int x = node % GridSide;
        const int z = node / GridSide;
        for (std::size_t direction = 0; direction < OffsetX.size(); ++direction) {
            const int nextX = x + OffsetX[direction];
            const int nextZ = z + OffsetZ[direction];
            if (nextX < 0 || nextX >= GridSide || nextZ < 0 || nextZ >= GridSide) continue;
            const int next = nextZ * GridSide + nextX;
            if (next <= node) continue;
            // Endpoint occupancy alone is insufficient: even a diagonal between
            // two free nodes can cut through an inflated circular obstacle.
            if (navigableNodes_[next] && clearSegment(nodePosition(node), nodePosition(next))) {
                connections_[node] |= static_cast<std::uint8_t>(1U << direction);
                connections_[next] |= static_cast<std::uint8_t>(1U << (7U - direction));
            }
        }
    }

    components_.assign(NodeCount, -1);
    std::vector<int> pending;
    pending.reserve(NodeCount);
    int component = 0;
    for (int node = 0; node < NodeCount; ++node) {
        if (!navigableNodes_[node] || components_[node] >= 0) continue;
        pending.clear();
        pending.push_back(node);
        components_[node] = component;
        for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
            const int current = pending[cursor];
            for (std::size_t direction = 0; direction < OffsetX.size(); ++direction) {
                if (!(connections_[current] & (1U << direction))) continue;
                const int next = current + OffsetZ[direction] * GridSide + OffsetX[direction];
                if (components_[next] >= 0) continue;
                components_[next] = component;
                pending.push_back(next);
            }
        }
        ++component;
    }
}

bool EnemyPathfinder::navigable(glm::vec3 position) const noexcept {
    if (circles_.empty() || !finite(position) || std::abs(position.x) > boundary_ ||
        std::abs(position.z) > boundary_) return false;
    for (const auto& circle : circles_) {
        const float dx = position.x - circle.position.x;
        const float dz = position.z - circle.position.z;
        if (dx * dx + dz * dz <= circle.radiusSquared) return false;
    }
    return true;
}

bool EnemyPathfinder::clearSegment(glm::vec3 start, glm::vec3 goal) const noexcept {
    if (circles_.empty() || !finite(start) || !finite(goal) ||
        std::abs(start.x) > boundary_ || std::abs(start.z) > boundary_ ||
        std::abs(goal.x) > boundary_ || std::abs(goal.z) > boundary_) return false;
    const float dx = goal.x - start.x;
    const float dz = goal.z - start.z;
    const float lengthSquared = dx * dx + dz * dz;
    for (const auto& circle : circles_) {
        const float offsetX = circle.position.x - start.x;
        const float offsetZ = circle.position.z - start.z;
        const float along = lengthSquared > 0.0F
                                ? std::clamp((offsetX * dx + offsetZ * dz) / lengthSquared, 0.0F, 1.0F)
                                : 0.0F;
        const float separationX = offsetX - along * dx;
        const float separationZ = offsetZ - along * dz;
        if (separationX * separationX + separationZ * separationZ <= circle.radiusSquared) return false;
    }
    return true;
}

std::vector<glm::vec3> EnemyPathfinder::findPath(glm::vec3 start, glm::vec3 goal) const {
    if (!navigable(start) || !navigable(goal)) return {};
    if (clearSegment(start, goal)) return {goal};

    std::array<int, 9> startNodes{};
    std::array<int, 9> goalNodes{};
    std::size_t startCount = 0;
    std::size_t goalCount = 0;

    // Connect the exact endpoints to all reachable nodes in their surrounding
    // 3x3 neighborhoods; rounding either endpoint onto a blocked node is unsafe.
    const auto connectEndpoint = [&](glm::vec3 endpoint, bool isStart) {
        bool connected = false;
        const int centerX = nearestCoordinate(endpoint.x);
        const int centerZ = nearestCoordinate(endpoint.z);
        for (int z = std::max(0, centerZ - 1); z <= std::min(GridSide - 1, centerZ + 1); ++z) {
            for (int x = std::max(0, centerX - 1); x <= std::min(GridSide - 1, centerX + 1); ++x) {
                const int node = z * GridSide + x;
                const glm::vec3 position = nodePosition(node);
                if (!navigableNodes_[node] || !clearSegment(endpoint, position)) continue;
                if (!isStart) {
                    bool reachableComponent = false;
                    for (std::size_t index = 0; index < startCount; ++index) {
                        reachableComponent = reachableComponent || components_[startNodes[index]] == components_[node];
                    }
                    if (!reachableComponent) continue;
                }
                connected = true;
                if (isStart) {
                    startNodes[startCount++] = node;
                } else {
                    goalNodes[goalCount++] = node;
                }
            }
        }
        return connected;
    };
    if (!connectEndpoint(start, true) || !connectEndpoint(goal, false)) return {};

    // Most rejected density candidates never allocate search state: cached
    // connected components establish whether any endpoint pair can be reached.
    constexpr float Infinity = std::numeric_limits<float>::infinity();
    std::vector<float> costs(NodeCount, Infinity);
    std::vector<int> predecessors(NodeCount, -1);
    std::vector<std::uint8_t> connectsToGoal(NodeCount, 0);
    std::priority_queue<OpenNode, std::vector<OpenNode>, LaterNode> open;
    for (std::size_t index = 0; index < startCount; ++index) {
        const int node = startNodes[index];
        const glm::vec3 position = nodePosition(node);
        const float cost = distance(start, position);
        costs[node] = cost;
        open.push({cost + distance(position, goal), cost, node});
    }
    for (std::size_t index = 0; index < goalCount; ++index) connectsToGoal[goalNodes[index]] = 1;

    int reached = -1;
    while (!open.empty()) {
        const OpenNode current = open.top();
        open.pop();
        if (current.cost != costs[current.node]) continue;
        if (connectsToGoal[current.node]) {
            reached = current.node;
            break;
        }
        const auto connections = connections_[current.node];
        for (std::size_t direction = 0; direction < OffsetX.size(); ++direction) {
            if (!(connections & (1U << direction))) continue;
            const int next = current.node + OffsetZ[direction] * GridSide + OffsetX[direction];
            const glm::vec3 position = nodePosition(next);
            const float cost = current.cost + distance(nodePosition(current.node), position);
            if (cost >= costs[next]) continue;
            costs[next] = cost;
            predecessors[next] = current.node;
            open.push({cost + distance(position, goal), cost, next});
        }
    }
    if (reached < 0) return {};

    std::vector<glm::vec3> route;
    for (int node = reached; node >= 0; node = predecessors[node]) route.push_back(nodePosition(node));
    std::reverse(route.begin(), route.end());
    route.push_back(goal);

    std::vector<glm::vec3> result;
    glm::vec3 anchor = start;
    for (std::size_t next = 0; next < route.size();) {
        std::size_t visible = route.size() - 1;
        while (visible > next && !clearSegment(anchor, route[visible])) --visible;
        // Keep the exact geometry checks for endpoint links and shortcut edges.
        if (!clearSegment(anchor, route[visible])) return {};
        result.push_back(route[visible]);
        anchor = route[visible];
        next = visible + 1;
    }
    return result;
}

} // namespace emergences::scenarios::ants
