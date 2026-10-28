#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace emergences::scenarios::ants3d {

struct TerrainVertex;

struct WaterCrossing {
    glm::vec3 center{0.0F}; // Y is the bridge deck, spanning the river along X.
    float halfLength{16.0F};
    float halfWidth{5.0F};
};

struct WaterPond {
    glm::vec3 center{0.0F}; // Y is the local water level.
    glm::vec2 radii{8.0F};
};

// The same polygonal shorelines drive water meshes and finite-radius contacts.
// River bridges are generated with the river, so every seed has dry crossings.
class WaterTerrain {
public:
    void configure(std::uint32_t seed, float halfExtent);
    // Call after the original ground is built, before applying shapeHeight.
    void fitPondLevels(const std::function<float(float, float)>& sampleGround);
    [[nodiscard]] float shapeHeight(float x, float z, float rawHeight) const noexcept;
    [[nodiscard]] bool isWater(float x, float z) const noexcept;
    [[nodiscard]] bool walkable(float x, float z, float radius = 0.0F) const noexcept;
    [[nodiscard]] std::optional<float> bridgeHeight(float x, float z) const noexcept;
    void resolveCollisionXZ(glm::vec3& position, glm::vec3& velocity, float radius) const noexcept;
    void appendMesh(std::vector<TerrainVertex>& water, std::vector<TerrainVertex>& bridges) const;

    [[nodiscard]] float riverCenter(float z) const noexcept;
    [[nodiscard]] float riverHalfWidth(float z) const noexcept;
    [[nodiscard]] float riverLevel() const noexcept { return riverLevel_; }
    [[nodiscard]] std::span<const WaterCrossing> crossings() const noexcept { return crossings_; }
    [[nodiscard]] std::span<const WaterPond> ponds() const noexcept { return ponds_; }

private:
    struct ShoreContact {
        float clearance{0.0F}; // Positive outside water.
        glm::vec2 point{0.0F};
        glm::vec2 normal{1.0F, 0.0F}; // Points from water to land.
    };
    [[nodiscard]] glm::vec2 riverEdges(float z) const noexcept;
    [[nodiscard]] ShoreContact riverContact(glm::vec2 point) const noexcept;
    [[nodiscard]] ShoreContact pondContact(std::size_t index, glm::vec2 point) const noexcept;
    [[nodiscard]] bool onCrossing(float x, float z, float radius) const noexcept;

    std::uint32_t seed_{0};
    float halfExtent_{128.0F};
    float riverLevel_{2.5F};
    float riverStep_{1.0F};
    std::vector<glm::vec2> banks_; // Left/right X at consecutive Z rows.
    std::vector<WaterCrossing> crossings_;
    std::vector<WaterPond> ponds_;
    std::vector<std::vector<glm::vec2>> pondShorelines_;
};

} // namespace emergences::scenarios::ants3d
