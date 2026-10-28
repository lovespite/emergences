#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

namespace emergences::scenarios::ants3d {
class Terrain;

// Shared, seeded-map navigation. Each destination has one distance field for
// the whole colony; walls, water and abrupt changes of walking layer block edges.
class TerrainNavigation {
public:
    void rebuild(const Terrain& terrain, float radius);
    glm::vec3 setHome(glm::vec3 desired);
    [[nodiscard]] glm::vec3 closestPoint(glm::vec3 desired) const;
    // Reuse unchanged indexed routes when food patches are appended or moved.
    // Rebuild and setHome invalidate routes before destinations are resubmitted.
    void setDestinations(std::span<const glm::vec3> destinations);
    [[nodiscard]] glm::vec3 target(glm::vec3 position, std::size_t destination,
                                   float lateralOffset = 0.0F) const noexcept;
    // Local obstacle avoidance for a caller-owned wandering heading; neither
    // food destinations nor their distance fields influence this waypoint.
    [[nodiscard]] glm::vec3 explorationTarget(glm::vec3 position,
                                              glm::vec3 heading) const noexcept;
    [[nodiscard]] bool reachable(glm::vec3 position) const noexcept;
    // Whether a grounded body can walk directly between two nearby points.
    [[nodiscard]] bool segmentClear(glm::vec3 a, glm::vec3 b) const noexcept;

private:
    struct Cell {
        glm::vec3 position{0.0F};
        std::uint8_t edges{0};
        bool clear{false};
        bool connected{false};
    };
    [[nodiscard]] std::size_t index(glm::vec3 position) const noexcept;
    [[nodiscard]] std::size_t nearest(glm::vec3 position, bool connectedOnly) const;
    [[nodiscard]] glm::vec3 grounded(float x, float z) const noexcept;

    const Terrain* terrain_{nullptr};
    float radius_{0.28F};
    int side_{0};
    float spacing_{2.0F};
    float minimum_{0.0F};
    std::vector<Cell> cells_;
    std::vector<glm::vec3> destinations_;
    std::vector<std::vector<int>> next_;
    std::vector<std::vector<float>> distances_;
};
}
