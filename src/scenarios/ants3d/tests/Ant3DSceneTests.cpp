#include "scenarios/ants3d/Ant3DScene.h"
#include "scenarios/ants3d/TerrainNavigation.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {
using emergences::scenarios::ants3d::Ant3DScene;
using emergences::scenarios::ants3d::AntActivity;
using emergences::scenarios::ants3d::FoodSource;
using emergences::scenarios::ants3d::Terrain;
using emergences::scenarios::ants3d::TerrainNavigation;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function> void invalid(Function&& function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid argument must be rejected");
}

void ticks(Ant3DScene& scene, std::size_t steps) {
    for (std::size_t i = 0; i < steps; ++i)
        require(scene.advance(Ant3DScene::FixedStepSeconds) == 1, "A fixed tick must execute one step");
}

FoodSource& food(Ant3DScene& scene, std::size_t index = 0) {
    return const_cast<FoodSource&>(scene.foodSources()[index]);
}

void removeFood(Ant3DScene& scene) {
    scene.setFoodSpawnInterval(120.0F);
    for (std::size_t i = 0; i < scene.foodSources().size(); ++i) food(scene, i).remaining = 0;
}

glm::vec3 grounded(const Terrain& terrain, glm::vec3 position) {
    position.y = terrain.navigationHeight(position.x, position.z) + Ant3DScene::AntRadius;
    position.y = terrain.walkingHeight(position, Ant3DScene::AntRadius);
    return position;
}

void placeForager(Ant3DScene& scene, glm::vec3 position) {
    for (int step = 0; scene.activities()[0] != AntActivity::Foraging && step < 100; ++step) ticks(scene, 1);
    require(scene.activities()[0] == AntActivity::Foraging, "The sensing fixture must complete a natural nest departure");
    auto& ant = const_cast<emergences::sim::Agent&>(scene.agents()[0]);
    ant.position = position;
    ant.velocity = glm::vec3{0.0F};
}

void requireSameMotion(const Ant3DScene& first, const Ant3DScene& second, const char* message) {
    require(first.count() == second.count(), "Motion comparisons need matching populations");
    for (std::size_t i = 0; i < first.count(); ++i)
        require(first.agents()[i].position == second.agents()[i].position &&
                first.agents()[i].velocity == second.agents()[i].velocity &&
                first.activities()[i] == second.activities()[i], message);
}

// Traffic regressions need a sustained supply independent of whether random
// explorers happen to discover a patch. Simulate a pickup at the body's actual
// location, keeping the nest machinery, routes and food accounting untouched.
void supplyReturningFood(Ant3DScene& scene) {
    for (std::size_t i = 0; i < scene.count(); ++i) {
        if (scene.activities()[i] != AntActivity::Foraging) continue;
        const auto source = std::find_if(scene.foodSources().begin(), scene.foodSources().end(),
            [](const auto& patch) { return patch.remaining > 0; });
        require(source != scene.foodSources().end(), "The arrival fixture must retain food to collect");
        --const_cast<FoodSource&>(*source).remaining;
        const_cast<std::uint8_t&>(scene.carryingFood()[i]) = 1;
        const_cast<AntActivity&>(scene.activities()[i]) = AntActivity::Returning;
    }
}

std::vector<float> sampledHeights(const Terrain& terrain) {
    std::vector<float> heights;
    for (float z = -120.0F; z <= 120.0F; z += 30.0F)
        for (float x = -120.0F; x <= 120.0F; x += 30.0F)
            heights.push_back(terrain.baseHeight(x, z));
    return heights;
}

void clocksAndDeterminism() {
    Ant3DScene first(32, 42), second(32, 42);
    ticks(first, 60); ticks(second, 60);
    for (std::size_t i = 0; i < first.count(); ++i) {
        require(first.agents()[i].position == second.agents()[i].position, "Seeded positions differ");
        require(first.agents()[i].velocity == second.agents()[i].velocity, "Seeded velocities differ");
    }
    first.setPaused(true);
    require(first.advance(1.0) == 0, "Pause must stop elapsed-time advancement");
    require(first.singleStep() && first.totalSteps() == 61, "Paused single step must advance exactly once");
    invalid([&] { first.advance(-1.0); });
    invalid([&] { first.advance(std::numeric_limits<double>::infinity()); });
    invalid([&] { first.advance(0.0, std::numeric_limits<double>::quiet_NaN()); });
    invalid([&] { first.setCount(Ant3DScene::MaxAnts + 1); });
    require(first.count() == 32 && first.totalSteps() == 61, "Invalid input must preserve state");
    first.reset(); second.reset();
    require(first.generation() == 2 && second.generation() == 2, "Reset must publish a new colony generation");
    require(first.paused() && first.totalSteps() == 0, "Reset must preserve pause and reset time");
    for (std::size_t i = 0; i < first.count(); ++i)
        require(first.agents()[i].position == second.agents()[i].position, "Reset must restore seeded population");
    first.setPaused(false);
    require(first.advance(100.0) == Ant3DScene::MaxStepsPerAdvance && first.lastAdvanceLimited(),
            "Long frame gaps must have bounded work");
    require(first.advance(4.0 * Ant3DScene::FixedStepSeconds, 0.0) == 1 && first.lastAdvanceLimited(),
            "Zero budget must permit one step and drop pending full steps");
    Ant3DScene empty(0);
    ticks(empty, 2);
    Ant3DScene different(32, 43);
    require(different.foodSources()[0].position != second.foodSources()[0].position,
            "Changing seed must change the generated food layout");
}

void localFoodDiscovery() {
    Ant3DScene explorers(24, 173), distantFood(24, 173);
    removeFood(explorers);
    distantFood.setFoodSpawnInterval(120.0F);
    // Moving stocked patches to a remote corner must not change any departure
    // heading or later search decision compared with a world without food.
    for (std::size_t i = 0; i < distantFood.foodSources().size(); ++i)
        food(distantFood, i).position = grounded(distantFood.terrain(), {108.0F, 0.0F, 108.0F});
    for (int step = 0; step < 600; ++step) {
        ticks(explorers, 1); ticks(distantFood, 1);
        requireSameMotion(explorers, distantFood, "Unseen food must not influence initial or ongoing exploration");
        for (std::size_t i = 0; i < distantFood.count(); ++i)
            if (distantFood.active(i)) require(glm::length(distantFood.agents()[i].position - food(distantFood).position) > 10.0F,
                    "The remote-food comparison must remain outside sensing range");
    }
    bool variedHeading = false;
    const auto firstHeading = glm::normalize(explorers.agents()[0].velocity);
    for (std::size_t i = 1; i < explorers.count(); ++i) {
        const auto velocity = explorers.agents()[i].velocity;
        if (explorers.activities()[i] == AntActivity::Foraging && glm::length(velocity) > 1.0F)
            variedHeading = variedHeading || glm::dot(firstHeading, glm::normalize(velocity)) < 0.5F;
    }
    require(variedHeading, "Explorers must spread into distinct headings instead of sharing a global destination");
    const auto beforePause = explorers.agents()[0].position;
    explorers.setPaused(true);
    require(explorers.advance(3.0) == 0 && explorers.agents()[0].position == beforePause,
            "Pausing must freeze random exploration");
    require(explorers.singleStep(), "Random exploration must support paused single stepping");
    ticks(distantFood, 1);
    requireSameMotion(explorers, distantFood, "Pause and single-step must preserve seeded search decisions");
    explorers.setPaused(false);
    explorers.reset(); removeFood(explorers); ticks(explorers, 601);
    requireSameMotion(explorers, distantFood, "Reset must replay the complete seeded exploration path");

    Ant3DScene nearby(1, 173), depleted(1, 173), movedAway(1, 173);
    TerrainNavigation navigation;
    navigation.rebuild(nearby.terrain(), Ant3DScene::AntRadius);
    navigation.setHome(nearby.nestPosition());
    glm::vec3 start{0.0F}, destination{0.0F};
    bool openSegment = false;
    for (float z = -100.0F; z < -50.0F && !openSegment; z += 10.0F) {
        for (float x = -100.0F; x < -50.0F && !openSegment; x += 10.0F) {
            start = grounded(nearby.terrain(), {x, 0.0F, z});
            destination = grounded(nearby.terrain(), {x + 8.0F, 0.0F, z});
            openSegment = nearby.terrain().sphereClear(start, Ant3DScene::AntRadius) &&
                          nearby.terrain().sphereClear(destination, Ant3DScene::AntRadius) &&
                          navigation.segmentClear(start, destination) && glm::length(destination - start) < 9.0F;
        }
    }
    require(openSegment, "Local food sensing needs a clear nearby walking segment");
    for (auto* scene : {&nearby, &depleted, &movedAway}) {
        removeFood(*scene);
        placeForager(*scene, start);
        food(*scene).position = destination;
        food(*scene).remaining = 100;
        ticks(*scene, 9);
    }
    requireSameMotion(nearby, depleted, "Identical nearby food must produce identical sensing");
    food(depleted).remaining = 0;
    food(movedAway).position = grounded(movedAway.terrain(), {108.0F, 0.0F, 108.0F});
    const auto discoveryPosition = nearby.agents()[0].position;
    for (int step = 0; step < 120; ++step) {
        ticks(nearby, 1); ticks(depleted, 1); ticks(movedAway, 1);
        requireSameMotion(depleted, movedAway,
                "Depleted and out-of-range targets must both be forgotten before further movement");
    }
    require(glm::length(nearby.agents()[0].position - destination) < glm::length(discoveryPosition - destination) - 2.0F,
            "A nearby visible food patch must attract a searching ant");
    require(glm::length(nearby.agents()[0].position - depleted.agents()[0].position) > 0.5F &&
            glm::length(depleted.agents()[0].position - discoveryPosition) > 0.5F,
            "An exhausted patch must release its ant back into active exploration");
    bool collected = false;
    for (int step = 0; step < 360 && !collected; ++step) {
        ticks(nearby, 1);
        collected = nearby.carryingFood()[0] != 0;
    }
    require(collected && food(nearby).remaining == 99,
            "An ant must approach and collect locally discovered food");
    require(!depleted.carryingFood()[0] && !movedAway.carryingFood()[0],
            "Forgotten food targets must never grant food remotely");

    // The cave floor and rock roof can share XZ coordinates and lie within the
    // smell radius. A blocked walking segment must still prevent attraction.
    Ant3DScene obscured(1, 173), noFood(1, 173);
    glm::vec3 floor{0.0F}, roof{0.0F};
    bool separateLayers = false;
    for (const auto& chamber : obscured.terrain().caveChambers()) {
        floor = grounded(obscured.terrain(), chamber.center);
        roof = floor;
        roof.y = obscured.terrain().surfaceHeight(roof.x, roof.z) + Ant3DScene::AntRadius + 1.0F;
        roof.y = obscured.terrain().walkingHeight(roof, Ant3DScene::AntRadius);
        const float separation = glm::length(roof - floor);
        separateLayers = separation > 3.0F && separation < 9.5F &&
                         obscured.terrain().sphereClear(floor, Ant3DScene::AntRadius) &&
                         obscured.terrain().sphereClear(roof, Ant3DScene::AntRadius) &&
                         !navigation.segmentClear(floor, roof);
        if (separateLayers) break;
    }
    require(separateLayers, "The wall fixture must use two valid nearby layers separated by rock");
    removeFood(obscured); removeFood(noFood);
    placeForager(obscured, floor); placeForager(noFood, floor);
    food(obscured).position = roof; food(obscured).remaining = 100;
    for (int step = 0; step < 60; ++step) {
        ticks(obscured, 1); ticks(noFood, 1);
        requireSameMotion(obscured, noFood, "Food across solid rock or an inaccessible layer must not attract explorers");
    }
    require(!obscured.carryingFood()[0] && food(obscured).remaining == 100,
            "Ants must not collect nearby food through a cave roof");

    // A previously sensed source can be moved/reused between periodic scans.
    // Make its collection radius span the roof to isolate the pickup-time path
    // check from the separate sensing-distance and collection-distance checks.
    Ant3DScene cachedTarget(1, 173);
    removeFood(cachedTarget);
    placeForager(cachedTarget, floor);
    glm::vec3 visibleFood{0.0F};
    bool visibleSource = false;
    for (int direction = 0; direction < 16 && !visibleSource; ++direction) {
        const float angle = static_cast<float>(direction) * 0.3926990817F;
        visibleFood = grounded(cachedTarget.terrain(), floor + glm::vec3{4.5F * std::cos(angle), 0.0F, 4.5F * std::sin(angle)});
        visibleSource = glm::length(visibleFood - floor) < 8.0F && navigation.segmentClear(floor, visibleFood);
    }
    require(visibleSource, "The cached-target fixture needs initially visible food on the cave floor");
    food(cachedTarget).position = visibleFood;
    food(cachedTarget).remaining = 100;
    // End immediately after a periodic sensing tick. The next tick must use
    // the cached target, while the old waypoint remains farther than 0.8 units.
    do { ticks(cachedTarget, 1); } while (cachedTarget.totalSteps() % 8 != 1);
    require(!cachedTarget.carryingFood()[0] &&
            glm::length(visibleFood - cachedTarget.agents()[0].position) > 3.0F,
            "The visible source must be sensed before the ant reaches collection distance");
    food(cachedTarget).position = roof;
    food(cachedTarget).radius = Ant3DScene::FoodDetectionRadius;
    require(glm::length(roof - cachedTarget.agents()[0].position) < Ant3DScene::FoodDetectionRadius &&
            !navigation.segmentClear(cachedTarget.agents()[0].position, roof),
            "The reused food slot must remain nearby while its walking path becomes blocked");
    ticks(cachedTarget, 1);
    require(!cachedTarget.carryingFood()[0] && food(cachedTarget).remaining == 100,
            "Cached food targets must recheck their walking path when collecting between sensing updates");
}

void configurableSpeedAndGates() {
    Ant3DScene scene(16, 173);
    require(scene.movementSpeed() == 4.7F && scene.nestExitCount() == 1 && scene.nestEntryCount() == 1,
            "Movement and gate defaults must preserve the existing colony");
    invalid([&] { scene.setMovementSpeed(0.49F); });
    invalid([&] { scene.setMovementSpeed(20.01F); });
    invalid([&] { scene.setMovementSpeed(std::numeric_limits<float>::quiet_NaN()); });
    invalid([&] { scene.setNestExitCount(0); });
    invalid([&] { scene.setNestEntryCount(5); });
    require(!scene.latestExitedAnt(), "A new colony must have no completed departure");
    ticks(scene, 1);
    require(!scene.latestExitedAnt(), "Starting a traversal must not count as a completed departure");
    const auto generation = scene.generation();
    scene.setMovementSpeed(9.4F);
    require(scene.generation() == generation && scene.totalSteps() == 1 && scene.active(0),
            "Movement speed must apply live without resetting the colony");
    ticks(scene, 22);
    require(scene.latestExitedAnt() == 0, "A faster traversal must finish sooner and publish its ant");
    scene.setNestExitCount(4);
    scene.setNestEntryCount(3);
    require(scene.totalSteps() == 0 && !scene.latestExitedAnt() && scene.movementSpeed() == 9.4F,
            "Gate changes must reset departure history and retain movement settings");
    ticks(scene, 1);
    require(std::count(scene.activities().begin(), scene.activities().end(), AntActivity::Leaving) == 4,
            "All configured exits must independently admit an ant");
    ticks(scene, 22);
    require(scene.latestExitedAnt() == 3, "Simultaneous completions must have stable gate ordering");
    for (std::size_t i = 0; i < scene.nestExitCount(); ++i)
        for (std::size_t j = i + 1; j < scene.nestExitCount(); ++j)
            require(glm::length(scene.nestExitPosition(i) - scene.nestExitPosition(j)) > 2.0F * Ant3DScene::AntRadius,
                    "Distinct exits must have room for finite ant bodies");
    scene.reset();
    require(!scene.latestExitedAnt(), "Reset must clear departure history");

    Ant3DScene slow(1, 173), fast(1, 173);
    slow.setMovementSpeed(2.35F);
    fast.setMovementSpeed(9.4F);
    ticks(slow, 120); ticks(fast, 120);
    require(glm::length(fast.agents()[0].velocity) > glm::length(slow.agents()[0].velocity) * 2.0F,
            "Movement speed must affect ordinary foraging as well as gate traversal");

    for (const auto seed : {0U, 23U, 42U, 173U, 12345U, std::numeric_limits<std::uint32_t>::max()}) {
        Ant3DScene gates(16, seed);
        gates.setNestExitCount(4); gates.setNestEntryCount(4);
        for (int tick = 0; tick < 100; ++tick) {
            ticks(gates, 1);
            for (std::size_t i = 0; i < gates.count(); ++i)
                if (gates.active(i)) require(gates.terrain().sphereClear(gates.agents()[i].position, Ant3DScene::AntRadius, 0.012F),
                        "Every configured gate must traverse clear seeded terrain");
        }
    }
}

void multipleGateTraffic() {
    Ant3DScene scene(64, 173);
    scene.setNestExitCount(4); scene.setNestEntryCount(4);
    scene.setMovementSpeed(9.4F);
    std::vector<bool> entriesUsed(4, false);
    bool concurrentEntries = false;
    for (std::size_t tick = 0; tick < 4800; ++tick) {
        ticks(scene, 1);
        supplyReturningFood(scene);
        const auto leaving = std::count(scene.activities().begin(), scene.activities().end(), AntActivity::Leaving);
        const auto entering = std::count(scene.activities().begin(), scene.activities().end(), AntActivity::Entering);
        require(leaving <= 4 && entering <= 4, "Every configured gate must retain at most one occupant");
        concurrentEntries = concurrentEntries || entering > 1;
        for (std::size_t i = 0; i < scene.count(); ++i) {
            if (scene.activities()[i] == AntActivity::Entering) entriesUsed[i % 4] = true;
            if (!scene.active(i) || tick % 60 != 0) continue;
            require(scene.terrain().sphereClear(scene.agents()[i].position, Ant3DScene::AntRadius, 0.012F),
                    "Multiple gates and faster ants must remain clear of terrain");
            for (std::size_t j = i + 1; j < scene.count(); ++j)
                if (scene.active(j)) require(glm::length(scene.agents()[i].position - scene.agents()[j].position) >=
                    2.0F * Ant3DScene::AntRadius - 0.02F, "Simultaneous gate traffic must preserve body separation");
        }
    }
    require(std::all_of(entriesUsed.begin(), entriesUsed.end(), [](bool used) { return used; }) && concurrentEntries,
            "Returning ants must route through all configured entrances independently");
    std::size_t carrying = 0, remaining = 0;
    for (auto value : scene.carryingFood()) carrying += value;
    for (const auto& food : scene.foodSources()) remaining += static_cast<std::size_t>(food.remaining);
    require(scene.deliveredFood() > 0 && scene.deliveredFood() + carrying + remaining == scene.totalGeneratedFood(),
            "Multiple gates must deliver and conserve initial and generated food");
}

void foodGenerationSchedule() {
    Ant3DScene scene(0, 42), replay(0, 42);
    require(scene.foodSpawnInterval() == 10.0F && scene.foodSources().size() == 8 && scene.totalGeneratedFood() == 9600,
            "Food generation must preserve the original initial patches");
    invalid([&] { scene.setFoodSpawnInterval(0.0F); });
    invalid([&] { scene.setFoodSpawnInterval(121.0F); });
    invalid([&] { scene.setFoodSpawnInterval(std::numeric_limits<float>::infinity()); });
    scene.setFoodSpawnInterval(1.0F); replay.setFoodSpawnInterval(1.0F);
    ticks(scene, 119);
    require(scene.foodSources().size() == 8, "Food must not spawn before its interval elapses");
    scene.setPaused(true);
    scene.advance(20.0);
    require(scene.totalGeneratedFood() == 9600, "Paused time must not generate food");
    require(scene.singleStep() && scene.foodSources().size() == 10 && scene.totalGeneratedFood() == 12000,
            "A fixed step at the interval must generate two food patches");
    ticks(replay, 120);
    for (std::size_t i = 0; i < scene.foodSources().size(); ++i)
        require(scene.foodSources()[i].position == replay.foodSources()[i].position,
                "Food generation must be reproducible from the seed");
    const auto firstNewPosition = scene.foodSources()[8].position;
    scene.setPaused(false);
    ticks(scene, 60);
    scene.setFoodSpawnInterval(2.0F);
    ticks(scene, 239);
    require(scene.foodSources().size() == 10, "Changing the interval must restart its countdown");
    ticks(scene, 1);
    require(scene.foodSources().size() == 12, "The updated interval must generate the next batch");
    scene.setFoodSpawnInterval(1.0F);
    ticks(scene, 120 * 10);
    require(scene.foodSources().size() == Ant3DScene::MaxFoodSources,
            "Food generation must retain a bounded number of patches");
    std::size_t remaining = 0;
    for (const auto& food : scene.foodSources()) remaining += static_cast<std::size_t>(food.remaining);
    require(remaining == scene.totalGeneratedFood(), "Generated food must be included in conservation accounting");
    scene.reset();
    require(scene.foodSources().size() == 8 && scene.totalGeneratedFood() == 9600,
            "Reset must restore initial food and its generation schedule");
    ticks(scene, 120);
    require(scene.foodSources()[8].position == firstNewPosition, "Reset must reproduce the first generated batch");
}

void mapLifecycle() {
    Ant3DScene scene(16, 42);
    const auto heights = sampledHeights(scene.terrain());
    const auto caveCenter = scene.terrain().caves()[0].center;
    const auto nest = scene.nestPosition();
    const auto firstFood = scene.foodSources()[0].position;
    require(scene.terrain().seed() == scene.seed(), "Scene seed must generate its terrain");
    require(std::any_of(scene.foodSources().begin(), scene.foodSources().end(), [](const auto& food) {
        return std::abs(food.position.x) > 48.0F || std::abs(food.position.z) > 48.0F;
    }), "Foraging targets must use the expanded world");
    for (const auto& food : scene.foodSources()) {
        require(std::abs(food.position.x) + food.radius < Ant3DScene::WorldHalfExtent &&
                std::abs(food.position.z) + food.radius < Ant3DScene::WorldHalfExtent,
                "Food patches must fit inside the world");
        require(std::abs(food.position.y - scene.terrain().navigationHeight(food.position.x, food.position.z)) < 0.1F,
                "Food patches must rest on the generated surface");
    }
    ticks(scene, 5);
    scene.reset();
    require(sampledHeights(scene.terrain()) == heights && scene.terrain().caves()[0].center == caveCenter,
            "Reset must retain the generated map");
    require(scene.nestPosition() == nest && scene.foodSources()[0].position == firstFood,
            "Reset must restore the seeded nest and food layout");
    scene.setCount(24);
    require(sampledHeights(scene.terrain()) == heights, "Population changes must preserve the generated map");
    scene.setPaused(true);
    scene.randomize(43);
    require(scene.seed() == 43 && scene.terrain().seed() == 43,
            "Randomize must apply the new seed to both colony and terrain");
    require(sampledHeights(scene.terrain()) != heights && scene.terrain().caves()[0].center != caveCenter,
            "Randomize must change hills and cave placement");
    require(scene.count() == 24 && scene.paused() && scene.totalSteps() == 0,
            "Randomize must retain population and pause state while resetting the simulation");
    scene.setSeed(42);
    require(sampledHeights(scene.terrain()) == heights && scene.nestPosition() == nest,
            "Restoring a seed must recreate the original map and nest");
}

void navigationThroughCavesAndWater() {
    constexpr float Radius = Ant3DScene::AntRadius;
    for (const auto seed : {0U, 42U, std::numeric_limits<std::uint32_t>::max()}) {
        Terrain terrain(seed);
        TerrainNavigation navigation;
        navigation.rebuild(terrain, Radius);
        const auto entrance = *std::min_element(terrain.caveEntrances().begin(), terrain.caveEntrances().end(),
            [](const auto& a, const auto& b) { return a.x < b.x; });
        const auto home = navigation.setHome(entrance + glm::vec3{-9.0F, Radius, 0.0F});
        std::vector<glm::vec3> destinations;
        for (const auto& chamber : terrain.caveChambers()) destinations.push_back(chamber.center);
        for (const auto& portal : terrain.caveEntrances()) destinations.push_back(portal);
        destinations.push_back({115.0F, 0.0F, 0.0F});
        for (auto& destination : destinations) {
            destination.y = terrain.navigationHeight(destination.x, destination.z) + Radius;
            destination.y = terrain.walkingHeight(destination, Radius);
            require(navigation.reachable(destination),
                    "Every chamber, entrance and far river bank must be connected to the outdoor world");
        }
        navigation.setDestinations(destinations);
        bool crossedBridge = false;
        for (std::size_t route = 0; route < destinations.size(); ++route) {
            auto position = home;
            bool arrived = false;
            for (int step = 0; step < 3000; ++step) {
                auto remaining = destinations[route] - position;
                remaining.y = 0.0F;
                if (glm::length(remaining) < 0.6F) { arrived = true; break; }
                auto direction = navigation.target(position, route) - position;
                direction.y = 0.0F;
                const float length = glm::length(direction);
                if (length <= 0.001F)
                    std::cerr << "stuck route seed=" << seed << " target=" << route << " at "
                              << position.x << ',' << position.y << ',' << position.z << '\n';
                require(length > 0.001F, "A connected navigation route must not become stuck");
                auto velocity = direction / length;
                position += velocity * std::min(0.32F, length);
                terrain.resolveCollision(position, velocity, Radius);
                require(terrain.sphereClear(position, Radius),
                        "Routes must stay outside solid cave walls, water and world boundaries");
                if (terrain.water().isWater(position.x, position.z)) {
                    require(terrain.water().bridgeHeight(position.x, position.z).has_value(),
                            "A route across water must stay on a physical crossing");
                    crossedBridge = true;
                }
            }
            if (!arrived) std::cerr << "route failed seed=" << seed << " target=" << route << " at "
                                   << position.x << ',' << position.y << ',' << position.z << '\n';
            require(arrived, "A sphere following navigation must reach every advertised cave entrance/chamber and far bank");
            if (route < terrain.caveChambers().size())
                require(terrain.insideCave(position), "Routes to chamber food must enter the cave instead of climbing its roof");
        }
        require(crossedBridge, "Reaching the far bank of the map-spanning river must exercise a bridge");
    }
}

void colonyBehavior() {
    Ant3DScene scene(80, 173);
    std::vector<AntActivity> previousActivities(scene.activities().begin(), scene.activities().end());
    std::vector<std::size_t> completedDepartures(scene.count(), 0);
    std::size_t departureSequence = 0;
    std::size_t contacts = 0;
    bool explorationSeen = false, trailSeen = false;
    bool simultaneousTraffic = false, cooldownSeen = false;
    float smallestDistance = 1000.0F;
    for (std::size_t tick = 0; tick < 6000; ++tick) {
        scene.advance(Ant3DScene::FixedStepSeconds);
        std::optional<std::size_t> expectedLatest;
        std::size_t newestSequence = 0;
        for (std::size_t i = 0; i < scene.count(); ++i) {
            if (previousActivities[i] == AntActivity::Leaving && scene.activities()[i] == AntActivity::Foraging)
                completedDepartures[i] = ++departureSequence;
            previousActivities[i] = scene.activities()[i];
            if (scene.active(i) && scene.activities()[i] != AntActivity::Leaving && completedDepartures[i] > newestSequence) {
                expectedLatest = i;
                newestSequence = completedDepartures[i];
            }
        }
        require(scene.latestExitedAnt() == expectedLatest, "Latest departure must follow completion chronology across repeated trips and returns");
        if (tick >= 2400 && tick % 240 == 0) supplyReturningFood(scene);
        const auto leaving = std::count(scene.activities().begin(), scene.activities().end(), AntActivity::Leaving);
        const auto entering = std::count(scene.activities().begin(), scene.activities().end(), AntActivity::Entering);
        require(leaving <= 1 && entering <= 1, "Each direction must admit at most one nest gate occupant");
        simultaneousTraffic = simultaneousTraffic || (leaving == 1 && entering == 1);
        for (std::size_t i = 0; i < scene.count(); ++i) {
            require(scene.agents()[i].color == glm::vec3{0.0F}, "Every ant must remain black, including food carriers");
            if (!scene.active(i)) {
                require(scene.agents()[i].position == scene.nestPosition() && scene.agents()[i].velocity == glm::vec3{0.0F},
                        "Hidden ants must stay inside the nest without collision displacement");
            }
            if (scene.activities()[i] == AntActivity::CoolingDown) {
                cooldownSeen = true;
                require(!scene.carryingFood()[i] && scene.cooldownRemaining()[i] > 0.0F &&
                        scene.cooldownRemaining()[i] <= Ant3DScene::MaximumCooldownSeconds,
                        "A cooling ant must have delivered its food and a bounded remaining cooldown");
            }
        }
        contacts += scene.collisionContacts();
        trailSeen = trailSeen || !scene.pheromoneTrail().empty();
        if (tick % 30 != 0) continue;
        for (std::size_t i = 0; i < scene.count(); ++i) {
            if (!scene.active(i)) continue;
            const auto& ant = scene.agents()[i];
            if (!scene.terrain().sphereClear(ant.position, Ant3DScene::AntRadius, 0.012F))
                std::cerr << "terrain penetration tick=" << tick << " ant=" << i << " at "
                          << ant.position.x << ',' << ant.position.y << ',' << ant.position.z << '\n';
            require(scene.terrain().sphereClear(ant.position, Ant3DScene::AntRadius, 0.012F),
                    "Moving ants must remain outside solid terrain");
            explorationSeen = explorationSeen || glm::length(ant.position - scene.nestPosition()) > 12.0F;
            for (std::size_t j = i + 1; j < scene.count(); ++j)
                if (scene.active(j)) smallestDistance = std::min(smallestDistance, glm::length(ant.position - scene.agents()[j].position));
        }
    }
    if (smallestDistance < 2.0F * Ant3DScene::AntRadius - 0.02F)
        std::cerr << "minimum sphere distance: " << smallestDistance << '\n';
    require(smallestDistance >= 2.0F * Ant3DScene::AntRadius - 0.02F, "Sphere collision solver must prevent overlap");
    require(contacts > 0, "The test must exercise actual ant contacts");
    require(explorationSeen, "Random explorers must leave the immediate nest neighborhood");
    require(scene.deliveredFood() > 0 && trailSeen, "Foragers must collect food, return home, and leave trails");
    require(simultaneousTraffic && cooldownSeen, "Independent nest lanes must support simultaneous entry and exit before cooldown");
    std::size_t carrying = 0, remaining = 0;
    for (auto value : scene.carryingFood()) carrying += value;
    for (const auto& food : scene.foodSources()) remaining += static_cast<std::size_t>(food.remaining);
    require(scene.deliveredFood() + carrying + remaining == scene.totalGeneratedFood(), "Food must be conserved");
}

void crowdedPopulation() {
    Ant3DScene scene(600, 23);
    float minimumSquared = std::numeric_limits<float>::infinity();
    std::size_t minimumTick = 0, firstContact = 0, secondContact = 0;
    for (std::size_t tick = 0; tick < 1200; ++tick) {
        scene.advance(Ant3DScene::FixedStepSeconds);
        for (std::size_t i = 0; i < scene.count(); ++i) {
            if (!scene.active(i)) continue;
            if (tick % 120 == 0) {
                if (!scene.terrain().sphereClear(scene.agents()[i].position, Ant3DScene::AntRadius, 0.012F))
                    std::cerr << "crowded terrain penetration tick=" << tick << " ant=" << i << " state="
                              << static_cast<int>(scene.activities()[i]) << " at " << scene.agents()[i].position.x << ','
                              << scene.agents()[i].position.y << ',' << scene.agents()[i].position.z << '\n';
                require(scene.terrain().sphereClear(scene.agents()[i].position, Ant3DScene::AntRadius, 0.012F),
                        "Default population must remain clear of terrain under crowd pressure");
            }
            // A terrain projection can create a new contact late in a solver
            // pass. Check every tick so a transient overlap cannot be missed.
            for (std::size_t j = i + 1; j < scene.count(); ++j) {
                if (!scene.active(j)) continue;
                const auto delta = scene.agents()[i].position - scene.agents()[j].position;
                const float squared = glm::dot(delta, delta);
                if (squared < minimumSquared) {
                    minimumSquared = squared;
                    minimumTick = tick; firstContact = i; secondContact = j;
                }
            }
        }
    }
    const float minimum = std::sqrt(minimumSquared);
    if (minimum < 2.0F * Ant3DScene::AntRadius - 0.02F)
        std::cerr << "crowded distance: " << minimum << " tick=" << minimumTick
                  << " ants=" << firstContact << ',' << secondContact << '\n';
    require(minimum >= 2.0F * Ant3DScene::AntRadius - 0.02F, "Default population must retain finite body volume");
    for (const auto seed : {0U, 23U, 42U, 173U, 12345U, std::numeric_limits<std::uint32_t>::max()}) {
        Ant3DScene maximum(Ant3DScene::MaxAnts, seed);
        require(std::all_of(maximum.activities().begin(), maximum.activities().end(), [](auto state) {
            return state == AntActivity::Queued;
        }), "Maximum population must begin hidden inside the nest");
        for (int tick = 0; tick < 100; ++tick) {
            ticks(maximum, 1);
            require(maximum.active(0), "Every seeded nest must admit its first ant without a terrain deadlock");
            for (std::size_t i = 0; i < maximum.count(); ++i)
                if (maximum.active(i)) require(maximum.terrain().sphereClear(maximum.agents()[i].position, Ant3DScene::AntRadius, 0.012F),
                        "Gate traversal must remain clear of seeded terrain");
        }
    }
}

void sustainedNestTraffic() {
    // This seeded layout previously stopped accepting food at 234 deliveries:
    // the FIFO leader became trapped behind the fixed six-column waiting area.
    // Supply food to the explorers after they have dispersed, then to subsequent
    // departures. This retains their actual positions and natural return routes
    // while keeping arrival pressure independent of chance food discovery.
    Ant3DScene scene(1123, 16277612U);
    std::vector<AntActivity> previousActivity(scene.activities().begin(), scene.activities().end());
    std::vector<glm::vec3> previousPosition;
    for (const auto& ant : scene.agents()) previousPosition.push_back(ant.position);
    std::vector<glm::vec3> nearEntrance;
    std::size_t previousDeliveries = 0, admissions = 0;
    bool simultaneousTraffic = false;
    constexpr std::size_t StepsPerSecond = 120;
    for (std::size_t tick = 1; tick <= 180 * StepsPerSecond; ++tick) {
        ticks(scene, 1);
        if (tick >= 80 * StepsPerSecond) supplyReturningFood(scene);
        std::size_t leaving = 0, entering = 0;
        nearEntrance.clear();
        for (std::size_t i = 0; i < scene.count(); ++i) {
            const auto activity = scene.activities()[i];
            const auto position = scene.agents()[i].position;
            leaving += activity == AntActivity::Leaving;
            entering += activity == AntActivity::Entering;
            if (previousActivity[i] == AntActivity::Returning && activity == AntActivity::Entering) {
                ++admissions;
                const float displacement = glm::length(position - previousPosition[i]);
                // Ordinary contact and terrain projection occur before the
                // state changes, so allow one body radius for that correction.
                if (displacement > Ant3DScene::AntRadius)
                    std::cerr << "entry displacement=" << displacement << " tick=" << tick << " ant=" << i << '\n';
                require(displacement <= Ant3DScene::AntRadius,
                        "Admission must continue from the ant's position without teleporting to the entrance");
            } else if (previousActivity[i] == AntActivity::Entering && activity == AntActivity::Entering) {
                require(glm::length(position - previousPosition[i]) <= 0.1F,
                        "Visible entry traversal must move continuously through the gate");
            }
            previousActivity[i] = activity;
            previousPosition[i] = position;
            if (tick % StepsPerSecond == 0 && scene.active(i)) {
                if (!scene.terrain().sphereClear(position, Ant3DScene::AntRadius, 0.012F))
                    std::cerr << "sustained traffic terrain penetration tick=" << tick << " ant=" << i << " at "
                              << position.x << ',' << position.y << ',' << position.z << '\n';
                require(scene.terrain().sphereClear(position, Ant3DScene::AntRadius, 0.012F),
                        "Sustained nest traffic must remain outside solid terrain");
                if (glm::length(position - scene.nestEntryPosition()) < 8.0F)
                    nearEntrance.push_back(position);
            }
        }
        require(leaving <= 1 && entering <= 1,
                "Each nest direction must retain one occupant under sustained traffic");
        simultaneousTraffic = simultaneousTraffic || (leaving == 1 && entering == 1);
        if (tick % StepsPerSecond == 0) {
            // Restrict pair checks to the busy entrance, once per second, so
            // this long regression does not scan the whole colony quadratically.
            for (std::size_t i = 0; i < nearEntrance.size(); ++i) {
                for (std::size_t j = i + 1; j < nearEntrance.size(); ++j) {
                    const float distance = glm::length(nearEntrance[i] - nearEntrance[j]);
                    if (distance < 2.0F * Ant3DScene::AntRadius - 0.02F)
                        std::cerr << "sustained traffic body distance=" << distance << " tick=" << tick << '\n';
                    require(distance >= 2.0F * Ant3DScene::AntRadius - 0.02F,
                            "Ants at the busy entrance must retain their collision volume");
                }
            }
            std::size_t carrying = 0, remaining = 0;
            for (auto value : scene.carryingFood()) carrying += value;
            for (const auto& food : scene.foodSources()) remaining += static_cast<std::size_t>(food.remaining);
            require(scene.deliveredFood() + carrying + remaining == scene.totalGeneratedFood(),
                    "Food must be conserved throughout sustained nest traffic");
        }
        if (tick >= 100 * StepsPerSecond && tick % (20 * StepsPerSecond) == 0) {
            if (tick > 100 * StepsPerSecond) {
                if (scene.deliveredFood() <= previousDeliveries)
                    std::cerr << "delivery stalled at " << tick / StepsPerSecond << " seconds: "
                              << scene.deliveredFood() << " deliveries\n";
                require(scene.deliveredFood() > previousDeliveries,
                        "A busy nest must continue accepting food in every 20-second arrival window");
            }
            previousDeliveries = scene.deliveredFood();
        }
    }
    require(admissions > 0 && simultaneousTraffic,
            "The sustained traffic regression must exercise continuous admission and independent gate directions");
}

void nestLifecycle() {
    Ant3DScene scene(8, 173);
    require(!scene.active(scene.count()), "Invalid ant IDs must never be active");
    std::vector<bool> emerged(scene.count(), false);
    std::size_t next = 0;
    for (std::size_t tick = 0; tick < 400; ++tick) {
        ticks(scene, 1);
        for (std::size_t i = 0; i < scene.count(); ++i) {
            if (!scene.active(i) || emerged[i]) continue;
            require(i == next++, "Initial departure order must be FIFO");
            emerged[i] = true;
            require(scene.activities()[i] == AntActivity::Leaving, "Every departure must pass through the exit gate");
        }
        if (tick == 0) require(next == 1 && scene.collisionContacts() == 0,
                              "Only one ant may emerge initially; hidden nest occupants must not collide");
    }
    require(next == scene.count(), "The initial queue must keep releasing ants");

    // Follow one complete trip, then replay it after reset to check the seeded
    // cooldown draw independently of wall-clock frame pacing.
    scene.setCount(1);
    const auto firstTrip = [&]() {
        std::size_t enteringTicks = 0;
        bool offeredFood = false;
        for (std::size_t tick = 0; tick < 120 * 120; ++tick) {
            ticks(scene, 1);
            if (!offeredFood && scene.activities()[0] == AntActivity::Foraging) {
                food(scene).position = scene.agents()[0].position;
                offeredFood = true;
            }
            if (scene.activities()[0] == AntActivity::Entering) ++enteringTicks;
            if (scene.activities()[0] != AntActivity::CoolingDown) continue;
            require(enteringTicks * Ant3DScene::FixedStepSeconds >= Ant3DScene::NestTraversalSeconds - 0.001,
                    "Delivery must wait until the entire entry traversal finishes");
            require(scene.deliveredFood() == 1 && !scene.active(0) && !scene.carryingFood()[0],
                    "Delivery must immediately hide the ant and consume its carried food");
            const float duration = scene.cooldownRemaining()[0];
            require(duration >= Ant3DScene::MinimumCooldownSeconds && duration <= Ant3DScene::MaximumCooldownSeconds,
                    "New cooldowns must be sampled in the 5–30 second range");
            return duration;
        }
        throw std::runtime_error("A single ant must complete its first delivery");
    };
    const float firstDuration = firstTrip();
    scene.setPaused(true);
    scene.advance(5.0);
    require(scene.cooldownRemaining()[0] == firstDuration, "Paused wall-clock time must not consume cooldown");
    require(scene.singleStep(), "Paused stepping must advance nest cooldowns");
    require(std::abs(scene.cooldownRemaining()[0] - (firstDuration - Ant3DScene::FixedStepSeconds)) < 0.00001,
            "Cooldown must count fixed simulation steps");
    scene.setPaused(false);
    std::size_t coolingTicks = 1;
    while (scene.activities()[0] == AntActivity::CoolingDown && coolingTicks < 3700) {
        require(!scene.active(0) && scene.agents()[0].position == scene.nestPosition() && scene.collisionContacts() == 0,
                "A cooling ant must stay hidden, stationary, and outside collisions");
        ticks(scene, 1); ++coolingTicks;
    }
    require(std::abs(coolingTicks * Ant3DScene::FixedStepSeconds - firstDuration) <= 2.0 * Ant3DScene::FixedStepSeconds,
            "Re-emergence must wait for the sampled simulated cooldown duration");
    require(scene.activities()[0] == AntActivity::Leaving && scene.active(0),
            "A cooled ant must rejoin the departure gate");
    scene.reset();
    require(firstTrip() == firstDuration, "Reset must reproduce the seeded cooldown duration");
}
} // namespace

int main() {
    try { clocksAndDeterminism(); localFoodDiscovery(); configurableSpeedAndGates(); foodGenerationSchedule(); multipleGateTraffic(); mapLifecycle(); navigationThroughCavesAndWater(); nestLifecycle(); colonyBehavior(); crowdedPopulation(); sustainedNestTraffic(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "3D ant terrain, collisions, behavior, and fixed-step tests passed\n";
    return 0;
}
