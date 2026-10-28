#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

#include "scenarios/ants3d/WaterTerrain.h"

namespace emergences::scenarios::ants3d {

struct TerrainVertex {
    glm::vec3 position{0.0F};
    glm::vec3 normal{0.0F, 1.0F, 0.0F};
    glm::vec3 color{1.0F};
};

struct Cave {
    glm::vec3 center{0.0F}; // Y is the floor height at the center.
    float halfLength{20.0F}; // Bounding radii along X and Z.
    float halfWidth{4.0F};
    float height{3.4F};
};

// Seeded terrain with connected, domed chambers beneath an irregular hill.
// The cached height field is shared by the ground mesh and collision queries.
class Terrain {
public:
    static constexpr float WorldHalfExtent = 128.0F;

    explicit Terrain(std::uint32_t seed = 12345U);
    void regenerate(std::uint32_t seed);
    [[nodiscard]] std::uint32_t seed() const noexcept { return seed_; }
    [[nodiscard]] std::span<const TerrainVertex> mesh() const noexcept;
    // Hill cover and high ceilings; hide them to reveal chambers and low walls.
    [[nodiscard]] std::span<const TerrainVertex> roofMesh() const noexcept;
    [[nodiscard]] std::span<const TerrainVertex> featureMesh() const noexcept { return featureMesh_; }
    [[nodiscard]] const WaterTerrain& water() const noexcept { return water_; }
    [[nodiscard]] std::span<const Cave> caves() const noexcept;
    [[nodiscard]] std::span<const Cave> caveChambers() const noexcept;
    [[nodiscard]] std::span<const glm::vec3> caveEntrances() const noexcept;
    // One navigation layer: cave floor inside the carved footprint, hill above
    // solid rock elsewhere. The caller must reject steep edges between layers.
    [[nodiscard]] float navigationHeight(float x, float z) const noexcept;
    [[nodiscard]] bool navigationClear(glm::vec3 bodyCenter, float radius) const noexcept;
    [[nodiscard]] float baseHeight(float x, float z) const noexcept;
    [[nodiscard]] float surfaceHeight(float x, float z) const noexcept;
    [[nodiscard]] glm::vec3 surfaceNormal(glm::vec3 position) const noexcept;
    [[nodiscard]] float walkingHeight(glm::vec3 position, float radius) const noexcept;
    [[nodiscard]] bool insideCave(glm::vec3 position) const noexcept;
    [[nodiscard]] bool sphereClear(glm::vec3 position, float radius,
                                   float tolerance = 0.002F) const noexcept;
    // Projects a sphere onto its current walkable layer and resolves rock/world
    // contacts. Velocity loses its component into contact normals.
    void resolveCollision(glm::vec3& position, glm::vec3& velocity, float radius) const noexcept;

private:
    static constexpr int GroundResolution = 256;
    [[nodiscard]] float sampleGround(float x, float z, glm::vec3* normal = nullptr) const noexcept;
    [[nodiscard]] glm::vec3 groundColor(float x, float z, float height) const noexcept;
    static constexpr int CaveResolution = 160;
    static constexpr float CaveSpacing = 0.5F;
    [[nodiscard]] float sampleCave(float x, float z, bool upper,
                                 glm::vec3* normal = nullptr) const noexcept;
    [[nodiscard]] float supportHeight(float x, float z, float radius, bool upper) const noexcept;
    [[nodiscard]] float ceilingHeight(float x, float z) const noexcept;
    [[nodiscard]] glm::vec3 normalAt(float x, float z, bool upper) const noexcept;
    [[nodiscard]] float rockClearance(glm::vec3 position, float radius, glm::vec3& outward) const noexcept;
    void buildCaves();
    void buildHeightField();
    void buildMesh();

    std::uint32_t seed_{12345U};
    WaterTerrain water_;
    std::vector<float> heights_;
    std::vector<Cave> caves_;
    std::vector<Cave> chambers_;
    std::vector<glm::vec3> entrances_;
    std::vector<float> moundHeights_;
    std::vector<float> cavityHeights_;
    std::vector<TerrainVertex> mesh_;
    std::vector<TerrainVertex> roofMesh_;
    std::vector<TerrainVertex> featureMesh_;
};

} // namespace emergences::scenarios::ants3d
