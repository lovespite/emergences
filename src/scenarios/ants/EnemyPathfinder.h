#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

namespace emergences::scenarios::ants {

struct Obstacle;

// Deterministic CPU navigation on the XZ plane. Rebuild after terrain or the
// creature's clearance changes. The nest keeps a fixed nine-unit exclusion zone.
class EnemyPathfinder {
public:
    void rebuild(std::span<const Obstacle> obstacles, float clearanceRadius);

    // Waypoints exclude start and include the exact goal. An empty result means
    // that an endpoint is invalid or that no safe grid route could be found.
    [[nodiscard]] std::vector<glm::vec3> findPath(glm::vec3 start, glm::vec3 goal) const;
    [[nodiscard]] bool navigable(glm::vec3 position) const noexcept;

private:
    struct Circle {
        glm::vec3 position{0.0F};
        float radiusSquared{0.0F};
    };

    [[nodiscard]] bool clearSegment(glm::vec3 start, glm::vec3 goal) const noexcept;

    std::vector<Circle> circles_;
    std::vector<std::uint8_t> navigableNodes_;
    std::vector<std::uint8_t> connections_;
    std::vector<int> components_;
    float boundary_{0.0F};
};

} // namespace emergences::scenarios::ants
