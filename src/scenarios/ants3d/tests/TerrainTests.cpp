#include "scenarios/ants3d/Terrain.h"
#include "scenarios/ants3d/TerrainNavigation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {
using emergences::scenarios::ants3d::Terrain;
using emergences::scenarios::ants3d::TerrainVertex;
using emergences::scenarios::ants3d::TerrainNavigation;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool sameMesh(std::span<const TerrainVertex> first, std::span<const TerrainVertex> second) {
    return first.size() == second.size() && std::equal(first.begin(), first.end(), second.begin(),
        [](const auto& a, const auto& b) {
            return a.position == b.position && a.normal == b.normal && a.color == b.color;
        });
}

void seededGeneration() {
    Terrain first(42), second(42);
    require(first.seed() == 42 && second.seed() == 42, "Terrain must expose its generation seed");
    require(sameMesh(first.mesh(), second.mesh()) && sameMesh(first.roofMesh(), second.roofMesh()) &&
                sameMesh(first.featureMesh(), second.featureMesh()),
            "Equal seeds must produce identical terrain geometry, normals and biome colors");
    require(first.caves().size() == second.caves().size() && !first.caves().empty(),
            "Generated maps must contain caves");
    require(first.caves()[0].center == second.caves()[0].center,
            "Equal seeds must place caves identically");
    require(first.caveChambers().size() == second.caveChambers().size() &&
                first.caveEntrances().size() == second.caveEntrances().size(),
            "Equal seeds must preserve cave topology");
    for (std::size_t i = 0; i < first.caveChambers().size(); ++i) {
        const auto& a = first.caveChambers()[i];
        const auto& b = second.caveChambers()[i];
        require(a.center == b.center && a.halfLength == b.halfLength &&
                    a.halfWidth == b.halfWidth && a.height == b.height,
                "Equal seeds must reproduce every chamber's shape");
    }
    require(std::equal(first.caveEntrances().begin(), first.caveEntrances().end(),
                       second.caveEntrances().begin()), "Equal seeds must reproduce all cave entrances");
    require(first.water().ponds().size() == second.water().ponds().size() &&
                first.water().crossings().size() == second.water().crossings().size(),
            "Equal seeds must preserve water bodies and river crossings");
    for (std::size_t i = 0; i < first.water().ponds().size(); ++i) {
        const auto& a = first.water().ponds()[i];
        const auto& b = second.water().ponds()[i];
        require(a.center == b.center && a.radii == b.radii,
                "Equal seeds must reproduce pond shapes and water levels");
    }
    for (std::size_t i = 0; i < first.water().crossings().size(); ++i) {
        const auto& a = first.water().crossings()[i];
        const auto& b = second.water().crossings()[i];
        require(a.center == b.center && a.halfLength == b.halfLength && a.halfWidth == b.halfWidth,
                "Equal seeds must reproduce bridge placement and dimensions");
    }
    for (float z = -Terrain::WorldHalfExtent; z <= Terrain::WorldHalfExtent; z += 4.0F)
        require(first.water().riverCenter(z) == second.water().riverCenter(z) &&
                    first.water().riverHalfWidth(z) == second.water().riverHalfWidth(z),
                "Equal seeds must reproduce the river's entire course");

    second.regenerate(43);
    require(second.seed() == 43 && !sameMesh(first.mesh(), second.mesh()),
            "A new seed must regenerate the terrain mesh");
    require(first.caves()[0].center != second.caves()[0].center,
            "A new seed must change cave placement");
    std::size_t changed = 0, samples = 0;
    for (float z = -120.0F; z <= 120.0F; z += 20.0F) {
        for (float x = -120.0F; x <= 120.0F; x += 20.0F) {
            ++samples;
            if (std::abs(first.baseHeight(x, z) - second.baseHeight(x, z)) > 0.1F) ++changed;
        }
    }
    require(changed > samples / 2, "Seed variation must affect hills across the map");
    second.regenerate(42);
    require(sameMesh(first.mesh(), second.mesh()) && sameMesh(first.roofMesh(), second.roofMesh()) &&
                sameMesh(first.featureMesh(), second.featureMesh()) &&
            first.caves()[0].center == second.caves()[0].center,
            "Regenerating the original seed must exactly restore the map without accumulating geometry");
}

void generatedWorldGeometry() {
    require(Terrain::WorldHalfExtent >= 128.0F, "The expanded world must span at least 256 units");
    for (const auto seed : {0U, 12345U, std::numeric_limits<std::uint32_t>::max()}) {
        Terrain terrain(seed);
        require(!terrain.mesh().empty() && !terrain.roofMesh().empty() && !terrain.featureMesh().empty(),
                "Each generated world must render its ground, cave roof, water and bridges");
        float lowest = std::numeric_limits<float>::infinity();
        float highest = -lowest;
        float minimumX = lowest, maximumX = highest, minimumZ = lowest, maximumZ = highest;
        std::size_t checkedGroundVertices = 0;
        const auto cave = terrain.caves()[0];
        for (const auto mesh : {terrain.mesh(), terrain.roofMesh(), terrain.featureMesh()}) for (const auto& vertex : mesh) {
            require(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) &&
                    std::isfinite(vertex.position.z) && std::isfinite(vertex.normal.x) &&
                    std::isfinite(vertex.normal.y) && std::isfinite(vertex.normal.z),
                    "Generated geometry must remain finite for every supported seed");
            require(std::abs(glm::length(vertex.normal) - 1.0F) < 0.001F,
                    "Generated mesh normals must be normalized");
            require(std::isfinite(vertex.color.x) && std::isfinite(vertex.color.y) &&
                    std::isfinite(vertex.color.z) && vertex.color.x >= 0.0F && vertex.color.x <= 1.0F &&
                    vertex.color.y >= 0.0F && vertex.color.y <= 1.0F && vertex.color.z >= 0.0F && vertex.color.z <= 1.0F,
                    "Biome colors must stay within the visible color range");
            minimumX = std::min(minimumX, vertex.position.x); maximumX = std::max(maximumX, vertex.position.x);
            minimumZ = std::min(minimumZ, vertex.position.z); maximumZ = std::max(maximumZ, vertex.position.z);
            const float height = terrain.baseHeight(vertex.position.x, vertex.position.z);
            // Rock faces and bridge decks are additional layers; the ground
            // triangles still span the entire map and share the collision field.
            if (std::abs(vertex.position.y - height) >= 0.001F) continue;
            lowest = std::min(lowest, height); highest = std::max(highest, height);
            ++checkedGroundVertices;
        }
        require(minimumX == -Terrain::WorldHalfExtent && maximumX == Terrain::WorldHalfExtent &&
                minimumZ == -Terrain::WorldHalfExtent && maximumZ == Terrain::WorldHalfExtent,
                "Ground geometry must cover the expanded world bounds");
        require(checkedGroundVertices > 1000 && highest - lowest > 7.0F,
                "Each map must contain substantial hills across its walking floor");
        for (std::size_t index = 0; index + 2 < terrain.mesh().size(); index += 3) {
            const auto a = terrain.mesh()[index].position;
            const auto b = terrain.mesh()[index + 1].position;
            const auto c = terrain.mesh()[index + 2].position;
            if (std::abs(a.y - terrain.baseHeight(a.x, a.z)) >= 0.001F ||
                std::abs(b.y - terrain.baseHeight(b.x, b.z)) >= 0.001F ||
                std::abs(c.y - terrain.baseHeight(c.x, c.z)) >= 0.001F) continue;
            const auto center = (a + b + c) / 3.0F;
            require(std::abs(center.y - terrain.baseHeight(center.x, center.z)) < 0.001F,
                    "Collision height between grid samples must match the rendered triangle plane");
        }
        require(std::abs(cave.center.x) + cave.halfLength < Terrain::WorldHalfExtent &&
                std::abs(cave.center.z) + cave.halfWidth < Terrain::WorldHalfExtent &&
                cave.center.y == terrain.baseHeight(cave.center.x, cave.center.z),
                "Generated caves must sit on the floor and stay inside the world");

        constexpr float Radius = 0.28F;
        for (float z = -Terrain::WorldHalfExtent; z <= Terrain::WorldHalfExtent; z += 8.0F) {
            for (float x = -Terrain::WorldHalfExtent; x <= Terrain::WorldHalfExtent; x += 8.0F) {
                glm::vec3 position{x, terrain.baseHeight(x, z) + Radius, z};
                glm::vec3 velocity{1.0F, 0.0F, 1.0F};
                terrain.resolveCollision(position, velocity, Radius);
                if (!terrain.sphereClear(position, Radius))
                    std::cerr << "ground projection seed=" << seed << " input=" << x << ',' << z
                              << " result=" << position.x << ',' << position.y << ',' << position.z << '\n';
                require(terrain.sphereClear(position, Radius),
                        "Collision projection must ground spheres safely over the generated map and at its bounds");
            }
        }
    }
}

void chamberGeometryAndClearance() {
    constexpr float Radius = 0.28F;
    for (const auto seed : {0U, 42U, 12345U, std::numeric_limits<std::uint32_t>::max()}) {
        Terrain terrain(seed);
        require(terrain.caveChambers().size() >= 3 && terrain.caveEntrances().size() >= 2,
                "A cave must have multiple spacious chambers and multiple entrances");
        for (const auto& chamber : terrain.caveChambers()) {
            require(chamber.halfLength >= 4.5F && chamber.halfWidth >= 4.5F && chamber.height >= 4.0F,
                    "Chambers must have meaningful width and headroom in both horizontal directions");
            for (float dx : {-0.35F, 0.0F, 0.35F}) for (float dz : {-0.35F, 0.0F, 0.35F}) {
                glm::vec3 point = chamber.center + glm::vec3{dx * chamber.halfLength, Radius, dz * chamber.halfWidth};
                point.y = terrain.walkingHeight(point, Radius);
                require(terrain.sphereClear(point, Radius) && terrain.insideCave(point),
                        "A chamber must contain an open two-dimensional walking area under its roof");
                require(terrain.surfaceHeight(point.x, point.z) - point.y > 2.0F,
                        "The chamber floor must be below a genuinely three-dimensional rock roof");
            }
        }
        // Sample floor and roof junctions throughout the footprint, including
        // irregular portal lips and the ends of chamber branches.
        const auto cave = terrain.caves().front();
        for (float z = cave.center.z - cave.halfWidth; z <= cave.center.z + cave.halfWidth; z += 2.0F) {
            for (float x = cave.center.x - cave.halfLength; x <= cave.center.x + cave.halfLength; x += 2.0F) {
                for (float y : {terrain.baseHeight(x, z) + 0.15F, terrain.surfaceHeight(x, z) + 0.2F}) {
                    glm::vec3 point{x, y, z}, velocity{1.0F, 0.0F, 1.0F};
                    terrain.resolveCollision(point, velocity, Radius);
                    if (!terrain.sphereClear(point, Radius))
                        std::cerr << "cave projection seed=" << seed << " input=" << x << ',' << y << ',' << z
                                  << " result=" << point.x << ',' << point.y << ',' << point.z << '\n';
                    require(terrain.sphereClear(point, Radius),
                            "Projection must clear irregular chamber walls, roof lips and ground together");
                }
            }
        }
    }
    // This narrow off-grid hill/portal junction lies between navigation samples.
    // It may block a route, but collision projection must still free the body.
    Terrain seam(0);
    const auto seamCenter = seam.caves().front().center;
    for (float radius : {Radius, Radius + 0.06F}) {
        const float x = seamCenter.x - 28.0F, z = seamCenter.z - 1.26F;
        glm::vec3 point{x, seam.navigationHeight(x, z) + radius, z};
        glm::vec3 velocity{0.0F, 0.0F, 1.0F};
        seam.resolveCollision(point, velocity, radius);
        require(seam.sphereClear(point, radius), "Collision projection must clear off-grid cave-foot seams");
    }
}

void randomizedCavePlacement() {
    constexpr float Radius = 0.28F;
    float minimumX = Terrain::WorldHalfExtent, maximumX = -minimumX;
    float minimumZ = minimumX, maximumZ = maximumX;
    std::vector<glm::vec3> centers;
    for (const auto seed : {0U, 1U, 2U, 3U, 7U, 23U, 42U, 101U, 173U, 1024U, 12345U,
                            std::numeric_limits<std::uint32_t>::max()}) {
        Terrain terrain(seed);
        const auto center = terrain.caves().front().center;
        minimumX = std::min(minimumX, center.x); maximumX = std::max(maximumX, center.x);
        minimumZ = std::min(minimumZ, center.z); maximumZ = std::max(maximumZ, center.z);
        if (std::none_of(centers.begin(), centers.end(), [&](glm::vec3 previous) {
                return previous.x == center.x && previous.z == center.z;
            })) centers.push_back(center);
        for (const auto mesh : {terrain.mesh(), terrain.roofMesh(), terrain.featureMesh()}) {
            for (const auto& vertex : mesh)
                require(std::abs(vertex.position.x) <= Terrain::WorldHalfExtent &&
                        std::abs(vertex.position.z) <= Terrain::WorldHalfExtent,
                        "Random cave placement must keep every rendered vertex inside the world");
        }
        TerrainNavigation navigation;
        navigation.rebuild(terrain, Radius);
        const auto westEntrance = *std::min_element(terrain.caveEntrances().begin(), terrain.caveEntrances().end(),
            [](const auto& a, const auto& b) { return a.x < b.x; });
        const auto home = navigation.setHome(westEntrance + glm::vec3{-9.0F, Radius, 0.0F});
        require(!terrain.insideCave(home), "The west nest approach must remain outside the relocated cave");
        const auto checkDryAndConnected = [&](glm::vec3 point) {
            require(terrain.water().walkable(point.x, point.z, Radius),
                    "Random cave placement must keep rooms and advertised portals clear of water");
            point.y = terrain.navigationHeight(point.x, point.z) + Radius;
            point.y = terrain.walkingHeight(point, Radius);
            if (!terrain.navigationClear(point, Radius) || !navigation.reachable(point))
                std::cerr << "cave access seed=" << seed << " position=" << point.x << ',' << point.y << ',' << point.z << '\n';
            require(terrain.navigationClear(point, Radius) && navigation.reachable(point),
                    "Relocated cave chambers and portals must connect to the outdoor nest approach");
        };
        for (const auto& chamber : terrain.caveChambers()) {
            checkDryAndConnected(chamber.center);
            for (const float x : {-0.35F, 0.0F, 0.35F}) {
                for (const float z : {-0.35F, 0.0F, 0.35F})
                    require(terrain.water().walkable(chamber.center.x + x * chamber.halfLength,
                                                    chamber.center.z + z * chamber.halfWidth, Radius),
                            "Relocated rooms must preserve dry floor area around their centers");
            }
        }
        for (const auto portal : terrain.caveEntrances()) checkDryAndConnected(portal);
    }
    require(centers.size() >= 10, "Independent map seeds must select diverse cave positions");
    require(maximumX - minimumX >= 40.0F && maximumZ - minimumZ >= 70.0F,
            "Cave locations must vary broadly across the map rather than clustering near its center");
}

void waterBlocksAndBridgesConnect() {
    constexpr float Radius = 0.28F;
    for (const auto seed : {0U, 23U, 12345U, std::numeric_limits<std::uint32_t>::max()}) {
        Terrain terrain(seed);
        const auto& water = terrain.water();
        require(!water.ponds().empty() && !water.crossings().empty(),
                "Each map must contain ponds and a guaranteed river crossing");
        for (float z = -Terrain::WorldHalfExtent; z <= Terrain::WorldHalfExtent; z += 2.0F) {
            const float x = water.riverCenter(z);
            require(water.isWater(x, z), "The river must extend continuously between opposite map edges");
            if (!water.bridgeHeight(x, z))
                require(!water.walkable(x, z, Radius), "River water outside a crossing must block walking");
        }
        for (const auto& pond : water.ponds()) {
            require(water.isWater(pond.center.x, pond.center.z) &&
                        !water.walkable(pond.center.x, pond.center.z, Radius),
                    "Ponds must have physical blocked interiors");
            require(terrain.baseHeight(pond.center.x, pond.center.z) < pond.center.y - 0.8F,
                    "Ponds must occupy a real basin below the water surface");
            auto position = pond.center;
            glm::vec3 velocity{1.0F, 0.0F, 1.0F};
            terrain.resolveCollision(position, velocity, Radius);
            require(terrain.sphereClear(position, Radius) && water.walkable(position.x, position.z, Radius),
                    "A body projected from a pond must end safely on shore");
        }

        const float bank = water.riverCenter(0.0F) - water.riverHalfWidth(0.0F);
        glm::vec3 position{bank - 2.0F, 0.0F, 0.0F};
        position.y = terrain.walkingHeight(position, Radius);
        for (int step = 0; step < 400; ++step) {
            glm::vec3 velocity{1.0F, 0.0F, 0.0F};
            position += velocity * 0.12F;
            terrain.resolveCollision(position, velocity, Radius);
            require(water.walkable(position.x, position.z, Radius) && terrain.sphereClear(position, Radius),
                    "Repeated movement into an unbridged bank must not penetrate water");
        }
        require(position.x < water.riverCenter(position.z), "Water collision must prevent direct river crossing");

        for (const auto& bridge : water.crossings()) {
            position = bridge.center - glm::vec3{bridge.halfLength + 2.0F, 0.0F, 0.0F};
            position.y = terrain.walkingHeight(position, Radius);
            bool walkedAboveWater = false;
            const int steps = static_cast<int>(std::ceil((2.0F * bridge.halfLength + 4.0F) / 0.2F));
            for (int step = 0; step < steps; ++step) {
                glm::vec3 velocity{1.0F, 0.0F, 0.0F};
                position.x += 0.2F;
                terrain.resolveCollision(position, velocity, Radius);
                require(terrain.sphereClear(position, Radius), "Bridge and bank approaches must be continuously walkable");
                if (water.isWater(position.x, position.z)) {
                    require(position.y >= bridge.center.y + Radius - 0.01F,
                            "Crossing bodies must walk on the visible deck above the river");
                    walkedAboveWater = true;
                }
            }
            require(walkedAboveWater && position.x > bridge.center.x + bridge.halfLength,
                    "Every generated bridge must let a finite-radius body reach the opposite bank");
        }
    }
}

void incrementalNavigationDestinations() {
    constexpr float Radius = 0.28F;
    Terrain terrain(42);
    TerrainNavigation incremental;
    incremental.rebuild(terrain, Radius);
    glm::vec3 home{-80.0F, 0.0F, -80.0F};
    incremental.setHome(home);
    std::vector<glm::vec3> destinations{
        incremental.closestPoint({-90.0F, 0.0F, 90.0F}),
        incremental.closestPoint({90.0F, 0.0F, -90.0F})};
    const auto compareFreshRoutes = [&] {
        // A separately rebuilt graph/field set provides the same public result
        // regardless of which destination indices the incremental cache kept.
        TerrainNavigation fresh;
        fresh.rebuild(terrain, Radius);
        fresh.setHome(home);
        fresh.setDestinations(destinations);
        std::vector<glm::vec3> samples(destinations);
        for (const float x : {-100.0F, -40.0F, 0.0F, 40.0F, 100.0F})
            for (const float z : {-100.0F, -40.0F, 0.0F, 40.0F, 100.0F})
                samples.push_back(fresh.closestPoint({x, 0.0F, z}));
        for (const auto& chamber : terrain.caveChambers())
            samples.push_back(fresh.closestPoint(chamber.center));
        for (const auto& bridge : terrain.water().crossings())
            samples.push_back(fresh.closestPoint(bridge.center));
        for (const auto sample : samples) {
            for (std::size_t destination = 0; destination < destinations.size(); ++destination) {
                for (const float offset : {-0.6F, 0.0F, 0.8F})
                    require(incremental.target(sample, destination, offset) == fresh.target(sample, destination, offset),
                            "Incremental destination updates must match freshly built routes");
            }
            require(incremental.target(sample, destinations.size()) == sample,
                    "Removed destinations must not retain stale routing fields");
        }
    };
    const auto updateAndCompare = [&] {
        incremental.setDestinations(destinations);
        compareFreshRoutes();
    };
    updateAndCompare();
    updateAndCompare(); // Identical positions retain valid routes.
    destinations.push_back(incremental.closestPoint({85.0F, 0.0F, 85.0F}));
    updateAndCompare(); // Food appends without moving the existing targets.
    destinations[1] = incremental.closestPoint({-75.0F, 0.0F, -25.0F});
    updateAndCompare(); // A depleted patch's slot receives a new position.
    std::swap(destinations[0], destinations[2]);
    updateAndCompare(); // The destination index remains part of the contract.
    destinations.resize(1);
    updateAndCompare();
    destinations.push_back(incremental.closestPoint({60.0F, 0.0F, -40.0F}));
    updateAndCompare(); // Regrowing must not reuse a discarded field.

    home = {90.0F, 0.0F, 90.0F};
    const auto newHome = incremental.setHome(home);
    require(incremental.target(newHome, 0) == newHome,
            "Changing home must invalidate fields until destinations are resubmitted");
    updateAndCompare(); // Same coordinates after a new connected-component search.
    terrain.regenerate(173);
    incremental.rebuild(terrain, Radius);
    incremental.setHome(home);
    updateAndCompare(); // Same coordinates against a different terrain graph.

    destinations.clear();
    updateAndCompare();
    destinations.push_back(incremental.closestPoint({-60.0F, 0.0F, 60.0F}));
    updateAndCompare();
}

void localExplorationAvoidsObstacles() {
    constexpr float Radius = 0.28F;
    Terrain terrain(42);
    TerrainNavigation navigation;
    const glm::vec3 unsetPosition{-80.0F, 0.0F, -80.0F};
    require(navigation.explorationTarget(unsetPosition, {1.0F, 0.0F, 0.0F}) == unsetPosition,
            "Exploration without a navigation graph must safely retain its position");
    navigation.rebuild(terrain, Radius);
    const auto home = navigation.setHome(unsetPosition);
    const auto ground = [&](float x, float z) {
        glm::vec3 point{x, terrain.navigationHeight(x, z) + Radius, z};
        point.y = terrain.walkingHeight(point, Radius);
        return point;
    };
    // Direct sensing must not connect a cave floor to food on the rock above,
    // even when both endpoints have exactly the same horizontal coordinates.
    const auto chamber = terrain.caveChambers().front().center;
    const auto floor = ground(chamber.x, chamber.z);
    auto roof = floor;
    roof.y = terrain.surfaceHeight(roof.x, roof.z) + Radius;
    roof.y = terrain.walkingHeight(roof, Radius);
    require(roof.y - floor.y > 1.0F && terrain.navigationClear(floor, Radius) &&
                terrain.navigationClear(roof, Radius),
            "The layer-isolation probe must have separately walkable floor and roof endpoints");
    require(navigation.segmentClear(floor, floor), "A stationary point on the walking floor must be clear");
    require(!navigation.segmentClear(floor, roof) && !navigation.segmentClear(roof, floor),
            "A direct segment must reject different walking layers in both directions");
    struct Probe { glm::vec3 position; glm::vec3 heading; glm::vec3 target; };
    std::vector<Probe> probes;
    const auto addProbe = [&](glm::vec3 position, glm::vec3 heading) {
        const auto target = navigation.explorationTarget(position, heading);
        auto delta = target - position; delta.y = 0.0F;
        require(glm::length(delta) > 0.25F && glm::length(delta) <= 5.01F,
                "A scout must find a local escape instead of remaining against an obstacle");
        require(terrain.navigationClear(target, Radius) && navigation.segmentClear(position, target),
                "Exploration waypoints and their entire approach must remain walkable");
        require(std::abs(target.y - ground(target.x, target.z).y) < 0.001F,
                "Exploration waypoints must follow the grounded walking layer");
        probes.push_back({position, heading, target});
        return target;
    };

    bool openForward = false;
    for (const glm::vec3 heading : {glm::vec3{1.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 0.0F, 1.0F},
                                   glm::vec3{-1.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 0.0F, -1.0F}}) {
        const auto expected = ground(home.x + 4.0F * heading.x, home.z + 4.0F * heading.z);
        if (!navigation.segmentClear(home, expected)) continue;
        require(addProbe(home, heading * 3.0F + glm::vec3{0.0F, 7.0F, 0.0F}) == expected,
                "An unobstructed scout must follow its supplied horizontal heading");
        openForward = true;
        break;
    }
    require(openForward, "The test map must provide an open exploration heading");
    addProbe(home, {0.0F, 0.0F, 0.0F});

    bool worldEdge = false;
    for (const float z : {-100.0F, -60.0F, 0.0F, 60.0F, 100.0F}) {
        const auto position = ground(Terrain::WorldHalfExtent - 0.5F, z);
        if (!terrain.navigationClear(position, Radius)) continue;
        require(!navigation.segmentClear(position, ground(position.x + 4.0F, z)),
                "The edge probe must face outside the walkable world");
        const auto target = addProbe(position, {1.0F, 0.0F, 0.0F});
        require(target.x <= position.x, "Exploration must turn along or away from the world boundary");
        worldEdge = true;
        break;
    }
    require(worldEdge, "The test map must provide a walkable world-edge probe");

    bool riverBank = false;
    for (float z = -100.0F; z <= 100.0F; z += 10.0F) {
        const auto& water = terrain.water();
        const auto position = ground(water.riverCenter(z) - water.riverHalfWidth(z) - 1.0F, z);
        const auto forward = ground(position.x + 4.0F, z);
        if (!terrain.navigationClear(position, Radius) || water.walkable(forward.x, forward.z, Radius) ||
            navigation.segmentClear(position, forward)) continue;
        addProbe(position, {1.0F, 0.0F, 0.0F});
        riverBank = true;
        break;
    }
    require(riverBank, "The test map must provide an unbridged shore probe");

    bool caveWall = false;
    for (const auto& chamber : terrain.caveChambers()) {
        for (const glm::vec3 heading : {glm::vec3{1.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 0.0F, 1.0F},
                                       glm::vec3{-1.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 0.0F, -1.0F}}) {
            for (float distance = 0.0F; distance <= std::max(chamber.halfLength, chamber.halfWidth); distance += 0.5F) {
                const auto position = ground(chamber.center.x + heading.x * distance,
                                             chamber.center.z + heading.z * distance);
                if (!terrain.insideCave(position) || !terrain.navigationClear(position, Radius) ||
                    navigation.segmentClear(position, ground(position.x + heading.x * 4.0F,
                                                              position.z + heading.z * 4.0F))) continue;
                addProbe(position, heading);
                caveWall = true;
                break;
            }
            if (caveWall) break;
        }
        if (caveWall) break;
    }
    require(caveWall, "The test map must provide a blocked chamber-wall probe");

    // Scouts must behave exactly the same before food exists and after food
    // is added or moved elsewhere. Only their own heading and local ground count.
    for (const glm::vec3 food : {glm::vec3{90.0F, 0.0F, -90.0F}, glm::vec3{-90.0F, 0.0F, 90.0F}}) {
        const std::vector<glm::vec3> destinations{navigation.closestPoint(food)};
        navigation.setDestinations(destinations);
        for (const auto& probe : probes)
            require(navigation.explorationTarget(probe.position, probe.heading) == probe.target,
                    "Undiscovered food destinations must never influence local exploration");
    }
}
} // namespace

int main() {
    try { seededGeneration(); generatedWorldGeometry(); chamberGeometryAndClearance(); randomizedCavePlacement(); waterBlocksAndBridgesConnect(); incrementalNavigationDestinations(); localExplorationAvoidsObstacles(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Seeded 3D terrain, collision, destination routing and local exploration tests passed\n";
    return 0;
}
