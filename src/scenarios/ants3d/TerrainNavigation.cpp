#include "scenarios/ants3d/TerrainNavigation.h"
#include "scenarios/ants3d/Terrain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace emergences::scenarios::ants3d {
namespace {
constexpr std::array<int, 8> X{-1, 1, 0, 0, -1, 1, -1, 1};
constexpr std::array<int, 8> Z{0, 0, -1, 1, -1, -1, 1, 1};
constexpr std::array<int, 8> Opposite{1, 0, 3, 2, 7, 6, 5, 4};
float horizontalSquared(glm::vec3 value) { return value.x * value.x + value.z * value.z; }
}

glm::vec3 TerrainNavigation::grounded(float x, float z) const noexcept {
    glm::vec3 p{x, terrain_->navigationHeight(x, z) + radius_, z};
    p.y = terrain_->walkingHeight(p, radius_);
    return p;
}

bool TerrainNavigation::segmentClear(glm::vec3 a, glm::vec3 b) const noexcept {
    const float length = std::sqrt(horizontalSquared(b - a));
    // Sample below half a body radius, including small clipped portal rims.
    // Coarser probes can miss a narrow wall band then oscillate on that edge.
    const int samples = std::max(1, static_cast<int>(std::ceil(length / 0.125F)));
    glm::vec3 previous = a;
    float previousFloor = terrain_->navigationHeight(a.x, a.z);
    for (int sample = 0; sample <= samples; ++sample) {
        const float t = static_cast<float>(sample) / samples;
        const glm::vec3 p = grounded(std::lerp(a.x, b.x, t), std::lerp(a.z, b.z, t));
        // Check the actual walking layer before inflating the steering margin;
        // a larger support sphere can otherwise snap onto a nearby rock lip.
        if (!terrain_->navigationClear(p, radius_)) return false;
        // A displaced ant can touch a shore while still being physically clear.
        // Grow the steering margin along the segment so it can move away from
        // that contact instead of rejecting every possible recovery direction.
        const float clearanceRadius = radius_ + std::min(0.06F, length * t * 0.25F);
        glm::vec3 roomy = p;
        roomy.y = terrain_->walkingHeight(p, clearanceRadius);
        if (!terrain_->navigationClear(roomy, clearanceRadius)) return false;
        const float floor = terrain_->navigationHeight(p.x, p.z);
        const float run = std::sqrt(horizontalSquared(p - previous));
        // Sphere support can change at triangle seams without a ground step.
        // Compare the actual walking floor to reject cliffs/layer changes.
        if (std::abs(floor - previousFloor) > 1.65F * run + 0.18F) return false;
        if (sample == 0 && std::abs(p.y - a.y) > 1.0F) return false;
        if (sample == samples && std::abs(p.y - b.y) > 1.0F) return false;
        previous = p;
        previousFloor = floor;
    }
    return true;
}

void TerrainNavigation::rebuild(const Terrain& terrain, float radius) {
    terrain_ = &terrain;
    radius_ = radius;
    side_ = static_cast<int>(2.0F * Terrain::WorldHalfExtent / spacing_);
    minimum_ = -Terrain::WorldHalfExtent + spacing_ * 0.5F;
    cells_.assign(static_cast<std::size_t>(side_ * side_), {});
    next_.clear(); distances_.clear(); destinations_.clear();
    for (int z = 0; z < side_; ++z) {
        for (int x = 0; x < side_; ++x) {
            auto& cell = cells_[static_cast<std::size_t>(z * side_ + x)];
            cell.position = grounded(minimum_ + x * spacing_, minimum_ + z * spacing_);
            cell.clear = terrain.navigationClear(cell.position, radius_);
        }
    }
    for (int z = 0; z < side_; ++z) {
        for (int x = 0; x < side_; ++x) {
            const int current = z * side_ + x;
            auto& cell = cells_[static_cast<std::size_t>(current)];
            if (!cell.clear) continue;
            for (int direction = 0; direction < 8; ++direction) {
                const int nx = x + X[direction], nz = z + Z[direction];
                if (nx < 0 || nx >= side_ || nz < 0 || nz >= side_) continue;
                const int neighbor = nz * side_ + nx;
                if (neighbor <= current || !cells_[static_cast<std::size_t>(neighbor)].clear) continue;
                if (!segmentClear(cell.position, cells_[static_cast<std::size_t>(neighbor)].position)) continue;
                cell.edges |= static_cast<std::uint8_t>(1U << direction);
                cells_[static_cast<std::size_t>(neighbor)].edges |= static_cast<std::uint8_t>(1U << Opposite[direction]);
            }
        }
    }
}

std::size_t TerrainNavigation::index(glm::vec3 position) const noexcept {
    const int x = std::clamp(static_cast<int>(std::round((position.x - minimum_) / spacing_)), 0, side_ - 1);
    const int z = std::clamp(static_cast<int>(std::round((position.z - minimum_) / spacing_)), 0, side_ - 1);
    return static_cast<std::size_t>(z * side_ + x);
}

std::size_t TerrainNavigation::nearest(glm::vec3 position, bool connectedOnly) const {
    float best = std::numeric_limits<float>::infinity();
    std::size_t selected = cells_.size();
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        if (!cells_[i].clear || (connectedOnly && !cells_[i].connected)) continue;
        const float distance = horizontalSquared(cells_[i].position - position);
        if (distance < best) { best = distance; selected = i; }
    }
    if (selected == cells_.size()) throw std::runtime_error("Generated world has no reachable walking ground");
    return selected;
}

glm::vec3 TerrainNavigation::setHome(glm::vec3 desired) {
    const auto home = nearest(desired, false);
    // The selected connected component determines each destination's source
    // cell. Even identical destination coordinates need new fields afterward.
    next_.clear(); distances_.clear(); destinations_.clear();
    for (auto& cell : cells_) cell.connected = false;
    std::vector<std::size_t> pending{home};
    cells_[home].connected = true;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto cell = pending[i];
        for (int d = 0; d < 8; ++d) {
            if (!(cells_[cell].edges & (1U << d))) continue;
            const auto neighbor = static_cast<std::size_t>(static_cast<int>(cell) + Z[d] * side_ + X[d]);
            if (cells_[neighbor].connected) continue;
            cells_[neighbor].connected = true;
            pending.push_back(neighbor);
        }
    }
    return cells_[home].position;
}

glm::vec3 TerrainNavigation::closestPoint(glm::vec3 desired) const {
    return cells_[nearest(desired, true)].position;
}

bool TerrainNavigation::reachable(glm::vec3 position) const noexcept {
    return !cells_.empty() && cells_[index(position)].connected;
}

glm::vec3 TerrainNavigation::explorationTarget(glm::vec3 position, glm::vec3 heading) const noexcept {
    if (cells_.empty()) return position;
    heading.y = 0.0F;
    const float lengthSquared = horizontalSquared(heading);
    heading = lengthSquared > 0.0001F && std::isfinite(lengthSquared)
        ? heading / std::sqrt(lengthSquared) : glm::vec3{1.0F, 0.0F, 0.0F};
    // Prefer continuing forward, then turn progressively farther to either
    // side. A full half-turn lets a scout leave dead ends without a food route.
    constexpr std::array<float, 7> Cosine{1.0F, 0.8660254F, 0.5F, 0.0F, -0.5F, -0.8660254F, -1.0F};
    constexpr std::array<float, 7> Sine{0.0F, 0.5F, 0.8660254F, 1.0F, 0.8660254F, 0.5F, 0.0F};
    for (const float distance : {4.0F, 2.0F, 0.75F}) {
        for (std::size_t angle = 0; angle < Cosine.size(); ++angle) {
            for (const float side : {1.0F, -1.0F}) {
                if (side < 0.0F && (angle == 0 || angle + 1 == Cosine.size())) continue;
                const float sine = Sine[angle] * side;
                const glm::vec3 direction{heading.x * Cosine[angle] - heading.z * sine, 0.0F,
                                           heading.z * Cosine[angle] + heading.x * sine};
                const auto candidate = grounded(position.x + direction.x * distance,
                                                position.z + direction.z * distance);
                if (segmentClear(position, candidate)) return candidate;
            }
        }
    }
    // A very narrow corner can fall between the angular probes. Inspect only
    // neighboring graph cells, keeping recovery local and allocation-free.
    const auto current = index(position);
    const int cx = static_cast<int>(current) % side_, cz = static_cast<int>(current) / side_;
    float best = -std::numeric_limits<float>::infinity();
    glm::vec3 result = position;
    for (int z = std::max(0, cz - 2); z <= std::min(side_ - 1, cz + 2); ++z) {
        for (int x = std::max(0, cx - 2); x <= std::min(side_ - 1, cx + 2); ++x) {
            const auto& cell = cells_[static_cast<std::size_t>(z * side_ + x)];
            if (!cell.clear || cell.edges == 0) continue;
            const auto delta = cell.position - position;
            const float distance = std::sqrt(horizontalSquared(delta));
            if (distance < 0.25F || distance > 5.0F) continue;
            const float score = glm::dot(delta, heading) / distance + distance * 0.1F;
            if (score <= best || !segmentClear(position, cell.position)) continue;
            best = score;
            result = cell.position;
        }
    }
    return result;
}

void TerrainNavigation::setDestinations(std::span<const glm::vec3> destinations) {
    if (destinations.size() == destinations_.size() &&
        std::equal(destinations.begin(), destinations.end(), destinations_.begin())) return;
    // Preserve unchanged distance fields when only some destinations move.
    next_.resize(destinations.size());
    distances_.resize(destinations.size());
    for (std::size_t index = 0; index < destinations.size(); ++index) {
        if (index < destinations_.size() && destinations[index] == destinations_[index]) continue;
        auto& distances = distances_[index];
        auto& next = next_[index];
        distances.assign(cells_.size(), std::numeric_limits<float>::infinity());
        next.assign(cells_.size(), -1);
        const auto start = nearest(destinations[index], true);
        using Entry = std::pair<float, std::size_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
        distances[start] = 0.0F; next[start] = static_cast<int>(start);
        queue.push({0.0F, start});
        while (!queue.empty()) {
            const auto [cost, current] = queue.top(); queue.pop();
            if (cost > distances[current]) continue;
            for (int d = 0; d < 8; ++d) {
                if (!(cells_[current].edges & (1U << d))) continue;
                const auto neighbor = static_cast<std::size_t>(static_cast<int>(current) + Z[d] * side_ + X[d]);
                const float candidate = cost + glm::length(cells_[current].position - cells_[neighbor].position);
                if (candidate >= distances[neighbor]) continue;
                distances[neighbor] = candidate; next[neighbor] = static_cast<int>(current);
                queue.push({candidate, neighbor});
            }
        }
    }
    destinations_.assign(destinations.begin(), destinations.end());
}

glm::vec3 TerrainNavigation::target(glm::vec3 position, std::size_t destination, float lateralOffset) const noexcept {
    if (destination >= next_.size()) return position;
    const auto& next = next_[destination];
    std::size_t current = index(position);
    // Crowd pressure can move a body onto the edge of a blocked grid cell.
    // Recover to a locally reachable cell rather than steering through a wall.
    if (next[current] < 0 || !segmentClear(position, cells_[current].position)) {
        float best = std::numeric_limits<float>::infinity();
        std::size_t chosen = current;
        const int cx = static_cast<int>(current) % side_, cz = static_cast<int>(current) / side_;
        for (int z = std::max(0, cz - 2); z <= std::min(side_ - 1, cz + 2); ++z) {
            for (int x = std::max(0, cx - 2); x <= std::min(side_ - 1, cx + 2); ++x) {
                const auto candidate = static_cast<std::size_t>(z * side_ + x);
                const float distance = horizontalSquared(cells_[candidate].position - position);
                if (next[candidate] < 0 || distance >= best || !segmentClear(position, cells_[candidate].position)) continue;
                chosen = candidate; best = distance;
            }
        }
        if (!std::isfinite(best)) return position;
        current = chosen;
    }
    if (horizontalSquared(destinations_[destination] - position) < 16.0F &&
        segmentClear(position, destinations_[destination])) return destinations_[destination];
    glm::vec3 result = cells_[current].position;
    for (int lookahead = 0; lookahead < 3; ++lookahead) {
        const int candidate = next[current];
        if (candidate < 0 || candidate == static_cast<int>(current)) break;
        if (!segmentClear(position, cells_[static_cast<std::size_t>(candidate)].position)) break;
        current = static_cast<std::size_t>(candidate);
        result = cells_[current].position;
    }
    auto direction = result - position; direction.y = 0.0F;
    const float distance = glm::length(direction);
    if (distance > 1.0F && lateralOffset != 0.0F) {
        direction /= distance;
        const auto offset = grounded(result.x - direction.z * lateralOffset, result.z + direction.x * lateralOffset);
        if (segmentClear(position, offset)) result = offset;
    }
    return result;
}
}
