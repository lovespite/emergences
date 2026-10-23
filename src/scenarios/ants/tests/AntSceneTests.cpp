#include "scenarios/ants/AntScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {

using namespace emergences;
using scenarios::ants::AntParameters;
using scenarios::ants::AntScene;
using scenarios::ants::Enemy;
using scenarios::ants::FoodSource;
using scenarios::ants::Obstacle;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function>
void requireInvalid(Function&& action, const char* message) {
    bool rejected = false;
    try {
        action();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
}

template <class Value>
std::vector<Value> copy(std::span<const Value> values) {
    return {values.begin(), values.end()};
}

bool sameAgent(const sim::Agent& left, const sim::Agent& right) {
    return left.position == right.position && left.velocity == right.velocity &&
           left.color == right.color && left.size == right.size &&
           left.reserved == right.reserved && left.id == right.id;
}

bool sameFood(const FoodSource& left, const FoodSource& right) {
    return left.position == right.position && left.radius == right.radius &&
           left.remaining == right.remaining;
}

bool sameEnemy(const Enemy& left, const Enemy& right) {
    return left.position == right.position && left.velocity == right.velocity &&
           left.radius == right.radius;
}

bool sameObstacle(const Obstacle& left, const Obstacle& right) {
    return left.position == right.position && left.radius == right.radius;
}

bool sameParameters(const AntParameters& left, const AntParameters& right) {
    return left.speed == right.speed && left.sensorDistance == right.sensorDistance &&
           left.sensorAngle == right.sensorAngle && left.turnSpeed == right.turnSpeed &&
           left.wander == right.wander && left.depositRate == right.depositRate &&
           left.evaporation == right.evaporation && left.diffusion == right.diffusion &&
           left.foodSpawnInterval == right.foodSpawnInterval;
}

struct State {
    std::vector<sim::Agent> agents;
    std::vector<std::uint8_t> carrying;
    std::vector<FoodSource> food;
    std::vector<Enemy> enemies;
    std::vector<Obstacle> obstacles;
    std::size_t obstacleCount;
    std::vector<float> homePheromone;
    std::vector<float> foodPheromone;
    std::size_t delivered;
    std::size_t generated;
    double spawnCountdown;
    std::uint64_t steps;
};

State state(const AntScene& scene) {
    return {copy(scene.agents()), copy(scene.carryingFood()), copy(scene.foodSources()),
            copy(scene.enemies()), copy(scene.obstacles()), scene.obstacleCount(),
            copy(scene.homePheromone()), copy(scene.foodPheromone()),
            scene.deliveredFood(), scene.totalGeneratedFood(), scene.secondsUntilFoodSpawn(),
            scene.totalSteps()};
}

void requireSameState(const State& expected, const AntScene& actual) {
    require(std::equal(expected.agents.begin(), expected.agents.end(), actual.agents().begin(),
                       actual.agents().end(), sameAgent),
            "Ant trajectories and display records must replay exactly");
    require(std::equal(expected.food.begin(), expected.food.end(), actual.foodSources().begin(),
                       actual.foodSources().end(), sameFood),
            "Food placement and consumption must replay exactly");
    require(std::equal(expected.enemies.begin(), expected.enemies.end(), actual.enemies().begin(),
                       actual.enemies().end(), sameEnemy),
            "Hostile creature positions and velocities must replay exactly");
    require(expected.obstacleCount == actual.obstacleCount() &&
                std::equal(expected.obstacles.begin(), expected.obstacles.end(), actual.obstacles().begin(),
                           actual.obstacles().end(), sameObstacle),
            "Terrain settings and obstacle placement must replay exactly");
    require(std::equal(expected.carrying.begin(), expected.carrying.end(), actual.carryingFood().begin(),
                       actual.carryingFood().end()),
            "Carried food must replay exactly");
    require(std::equal(expected.homePheromone.begin(), expected.homePheromone.end(),
                       actual.homePheromone().begin(), actual.homePheromone().end()) &&
                std::equal(expected.foodPheromone.begin(), expected.foodPheromone.end(),
                           actual.foodPheromone().begin(), actual.foodPheromone().end()),
            "Pheromone deposition, diffusion, and evaporation must replay exactly");
    require(expected.delivered == actual.deliveredFood() && expected.steps == actual.totalSteps(),
            "Food delivery and fixed-step clock must replay exactly");
    require(expected.generated == actual.totalGeneratedFood() &&
                expected.spawnCountdown == actual.secondsUntilFoodSpawn(),
            "Generated food and replenishment schedule must replay exactly");
}

void ticks(AntScene& scene, int count) {
    for (int tick = 0; tick < count; ++tick) {
        require(scene.advance(AntScene::FixedStepSeconds) == 1,
                "One fixed frame must advance one simulation step");
    }
}

bool emptyField(std::span<const float> field) {
    return std::all_of(field.begin(), field.end(), [](float value) { return value == 0.0F; });
}

double mass(std::span<const float> field) {
    return std::accumulate(field.begin(), field.end(), 0.0);
}

void requireBoundedField(std::span<const float> field) {
    require(field.size() == AntScene::GridResolution * AntScene::GridResolution,
            "Pheromone fields must cover the whole world grid");
    require(std::all_of(field.begin(), field.end(), [](float value) {
        return std::isfinite(value) && value >= 0.0F && value <= AntScene::MaximumPheromone;
    }), "Pheromone concentrations must stay finite, nonnegative, and capped");
}

void requirePlanarPosition(const glm::vec3& position) {
    require(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
                position.y == 0.0F && std::abs(position.x) <= AntScene::WorldHalfExtent &&
                std::abs(position.z) <= AntScene::WorldHalfExtent,
            "Ants, food, hostile creatures, and obstacles must stay finite and inside the XZ world");
}

void requireClearOfTerrain(const AntScene& scene, const glm::vec3& position, float radius,
                          const char* message) {
    for (const auto& obstacle : scene.obstacles()) {
        require(glm::length(position - obstacle.position) + 1.0e-4F >= radius + obstacle.radius,
                message);
    }
}

void requireTerrainClearance(const AntScene& scene) {
    for (const auto& ant : scene.agents()) {
        requireClearOfTerrain(scene, ant.position, AntScene::AntRadius,
                              "Ant bodies must not penetrate solid terrain");
    }
    for (const auto& enemy : scene.enemies()) {
        requireClearOfTerrain(scene, enemy.position, enemy.radius,
                              "Hostile creatures must not penetrate solid terrain");
    }
    for (const auto& food : scene.foodSources()) {
        requireClearOfTerrain(scene, food.position, food.radius,
                              "Initial and replenished food patches must remain outside terrain");
    }
}

std::vector<std::size_t> terrainCells(const AntScene& scene) {
    constexpr float cellSize = 2.0F * AntScene::WorldHalfExtent /
                               static_cast<float>(AntScene::GridResolution - 1);
    std::vector<std::size_t> cells;
    for (std::size_t z = 0; z < AntScene::GridResolution; ++z) {
        for (std::size_t x = 0; x < AntScene::GridResolution; ++x) {
            const glm::vec3 position{static_cast<float>(x) * cellSize - AntScene::WorldHalfExtent,
                                     0.0F,
                                     static_cast<float>(z) * cellSize - AntScene::WorldHalfExtent};
            if (std::any_of(scene.obstacles().begin(), scene.obstacles().end(), [&](const Obstacle& obstacle) {
                    const auto delta = position - obstacle.position;
                    return glm::dot(delta, delta) < obstacle.radius * obstacle.radius;
                })) {
                cells.push_back(z * AntScene::GridResolution + x);
            }
        }
    }
    return cells;
}

void requireEmptyTerrainCells(const AntScene& scene, std::span<const std::size_t> cells) {
    for (const auto index : cells) {
        require(scene.homePheromone()[index] == 0.0F && scene.foodPheromone()[index] == 0.0F,
                "Neither deposition nor diffusion may put pheromones inside solid terrain");
    }
}

void requireBoundedWorld(const AntScene& scene) {
    require(scene.agents().size() == scene.count() && scene.carryingFood().size() == scene.count(),
            "Ant state and carried-food records must match the selected population");
    for (const auto& ant : scene.agents()) {
        requirePlanarPosition(ant.position);
        require(std::isfinite(ant.velocity.x) && std::isfinite(ant.velocity.z) && ant.velocity.y == 0.0F &&
                    glm::length(ant.velocity) <= scene.parameters().speed + 1.0e-4F,
                "Ant velocities must be planar, finite, and respect configured speed");
    }
    for (const auto& food : scene.foodSources()) {
        requirePlanarPosition(food.position);
        require(food.remaining >= 0 && std::isfinite(food.radius) && food.radius > 0.0F,
                "Food quantities must remain nonnegative and have valid radii");
        require(std::abs(food.position.x) + food.radius <= AntScene::WorldHalfExtent &&
                    std::abs(food.position.z) + food.radius <= AntScene::WorldHalfExtent &&
                    glm::length(food.position - scene.nestPosition()) > scene.nestRadius() + food.radius,
                "Food patches must fit inside the map and stay outside the nest");
    }
    require(scene.foodSources().size() <= AntScene::MaxFoodSources,
            "Food storage must remain bounded even during repeated replenishment");
    for (const auto& enemy : scene.enemies()) {
        requirePlanarPosition(enemy.position);
        require(std::isfinite(enemy.velocity.x) && std::isfinite(enemy.velocity.z) && enemy.velocity.y == 0.0F &&
                    std::isfinite(enemy.radius) && enemy.radius > 0.0F,
                "Hostile creatures must have finite planar velocities and valid radii");
    }
    require(scene.obstacles().size() == scene.obstacleCount() &&
                scene.obstacleCount() <= AntScene::MaxObstacles,
            "The generated terrain must match the selected obstacle count");
    for (std::size_t index = 0; index < scene.obstacles().size(); ++index) {
        const auto& obstacle = scene.obstacles()[index];
        requirePlanarPosition(obstacle.position);
        require(std::isfinite(obstacle.radius) && obstacle.radius >= AntScene::MinObstacleRadius &&
                    obstacle.radius <= AntScene::MaxObstacleRadius &&
                    std::abs(obstacle.position.x) + obstacle.radius <= AntScene::WorldHalfExtent &&
                    std::abs(obstacle.position.z) + obstacle.radius <= AntScene::WorldHalfExtent,
                "Every obstacle must fit inside the world and the supported size range");
        require(glm::length(obstacle.position - scene.nestPosition()) >=
                    obstacle.radius + scene.nestRadius(),
                "Terrain must leave the nest unobstructed");
        for (std::size_t other = 0; other < index; ++other) {
            const auto& previous = scene.obstacles()[other];
            require(glm::length(obstacle.position - previous.position) + 1.0e-4F >=
                        obstacle.radius + previous.radius + 4.0F,
                    "Generated obstacles must leave passages wide enough for hostile creatures");
        }
    }
    requireTerrainClearance(scene);
    requireBoundedField(scene.homePheromone());
    requireBoundedField(scene.foodPheromone());
}

void deterministicWorldAndReplay() {
    require(AntScene::WorldHalfExtent == 80.0F && AntScene::GridResolution == 256,
            "The map must double to 160 by 160 while retaining pheromone grid detail");
    AntScene scene{48, 42};
    AntScene matching{48, 42};
    require(!scene.foodSources().empty() && !scene.enemies().empty(),
            "A colony world must contain food and hostile creatures");
    require(emptyField(scene.homePheromone()) && emptyField(scene.foodPheromone()),
            "Pheromones start empty in the randomly generated world");
    requireBoundedWorld(scene);
    const State initial = state(scene);
    requireSameState(initial, matching);
    ticks(scene, 120);
    const State evolved = state(scene);
    ticks(matching, 120);
    requireSameState(evolved, matching);

    scene.setPaused(true);
    scene.reset();
    require(scene.paused() && scene.elapsedSimulationSeconds() == 0.0 && !scene.lastAdvanceLimited(),
            "Reset must retain pause while clearing the simulation clock and budget indication");
    requireSameState(initial, scene);
    scene.setPaused(false);
    ticks(scene, 120);
    requireSameState(evolved, scene);

    AntScene different{48, 43};
    require(!std::equal(initial.food.begin(), initial.food.end(), different.foodSources().begin(),
                        different.foodSources().end(), sameFood),
            "Different seeds must generate different food positions");
    require(!std::equal(initial.enemies.begin(), initial.enemies.end(), different.enemies().begin(),
                        different.enemies().end(), sameEnemy),
            "Different seeds must generate different hostile creature positions");
    require(!std::equal(initial.obstacles.begin(), initial.obstacles.end(), different.obstacles().begin(),
                        different.obstacles().end(), sameObstacle),
            "Different seeds must generate different terrain");
    scene.setPaused(true);
    scene.randomize(43);
    require(scene.seed() == 43 && scene.paused(), "Randomization must select the seed and retain pause");
    requireSameState(state(different), scene);
}

void terrainGenerationAndControls() {
    AntScene scene{16, 29};
    require(scene.obstacleCount() == AntScene::DefaultObstacleCount && !scene.obstacles().empty(),
            "A new ant world must enable its default random terrain");
    ticks(scene, 2);
    const State beforeUnchanged = state(scene);
    scene.setObstacleCount(scene.obstacleCount());
    requireSameState(beforeUnchanged, scene);

    auto parameters = scene.parameters();
    parameters.speed = 12.0F;
    parameters.foodSpawnInterval = 3.0F;
    scene.setParameters(parameters);
    scene.setPaused(true);
    scene.setObstacleCount(0);
    require(scene.obstacleCount() == 0 && scene.obstacles().empty() && scene.paused() &&
                scene.totalSteps() == 0 && scene.secondsUntilFoodSpawn() == 3.0 &&
                sameParameters(parameters, scene.parameters()),
            "Disabling terrain must reset the world while retaining pause and colony parameters");
    requireBoundedWorld(scene);
    require(scene.singleStep(), "A world with no terrain must still simulate");
    scene.setObstacleCount(AntScene::MaxObstacles);
    require(scene.obstacleCount() == AntScene::MaxObstacles && scene.totalSteps() == 0 && scene.paused(),
            "The maximum obstacle count must be supported without changing pause state");
    requireBoundedWorld(scene);
    const State maximumTerrain = state(scene);
    requireInvalid([&] { scene.setObstacleCount(AntScene::MaxObstacles + 1); },
                   "Terrain counts above the supported maximum must be rejected");
    requireInvalid([&] { scene.setObstacleCount(std::numeric_limits<std::size_t>::max()); },
                   "Overflow-sized terrain counts must be rejected");
    requireSameState(maximumTerrain, scene);
    require(scene.singleStep(), "A world with maximum terrain must still simulate");
    scene.reset();
    requireSameState(maximumTerrain, scene);
    require(scene.paused() && sameParameters(parameters, scene.parameters()),
            "Reset must retain terrain controls, pause, and colony parameters");
    scene.setCount(8);
    require(scene.obstacleCount() == AntScene::MaxObstacles,
            "Changing the population must retain the selected terrain count");
    scene.randomize(30);
    AntScene matching{8, 30};
    matching.setParameters(parameters);
    matching.setObstacleCount(AntScene::MaxObstacles);
    requireSameState(state(matching), scene);
    require(scene.paused(), "Regenerating terrain with a new seed must retain pause");

    for (const std::uint32_t seed : {0U, 1U, 2U, 7U, 42U, 71U, 12345U, 4294967295U}) {
        AntScene generated{0, seed};
        generated.setObstacleCount(AntScene::MaxObstacles);
        requireBoundedWorld(generated);
        require(std::any_of(generated.obstacles().begin(), generated.obstacles().end(), [](const Obstacle& obstacle) {
                    return obstacle.radius > 6.0F;
                }), "Terrain must include rocks larger than the former 4.5-unit radius limit");
    }
}

void terrainMovementAndPheromones() {
    for (const std::uint32_t seed : {42U, 314U}) {
        AntScene scene{160, seed};
        scene.setObstacleCount(AntScene::MaxObstacles);
        auto parameters = scene.parameters();
        parameters.speed = 30.0F;
        parameters.turnSpeed = 0.0F;
        parameters.wander = 0.0F;
        parameters.foodSpawnInterval = 1.0F;
        parameters.depositRate = 30.0F;
        parameters.diffusion = seed == 42U ? 10.0F : 0.0F;
        parameters.evaporation = 0.0F;
        scene.setParameters(parameters);
        const auto blockedCells = terrainCells(scene);
        require(!blockedCells.empty(), "Terrain must occupy cells in the pheromone grid");
        const auto initialSupply = scene.totalGeneratedFood();
        const auto initialObstacles = copy(scene.obstacles());
        for (int tick = 0; tick < 1440; ++tick) {
            ticks(scene, 1);
            requireTerrainClearance(scene);
            if ((tick + 1) % 120 == 0) {
                requireBoundedWorld(scene);
                requireEmptyTerrainCells(scene, blockedCells);
            }
        }
        require(scene.totalGeneratedFood() > initialSupply && mass(scene.homePheromone()) > 0.0,
                "Terrain collision checks must exercise replenished food and active pheromone trails");
        require(std::equal(initialObstacles.begin(), initialObstacles.end(), scene.obstacles().begin(),
                           scene.obstacles().end(), sameObstacle),
                "Terrain must remain fixed while ants and hostile creatures move");
    }
}

void framePartitioning() {
    AntScene at120Hz{24, 9};
    AntScene at60Hz{24, 9};
    AntScene at144Hz{24, 9};
    for (int frame = 0; frame < 120; ++frame) {
        at120Hz.advance(1.0 / 120.0);
    }
    for (int frame = 0; frame < 60; ++frame) {
        at60Hz.advance(1.0 / 60.0);
    }
    for (int frame = 0; frame < 144; ++frame) {
        at144Hz.advance(1.0 / 144.0);
    }
    require(at120Hz.totalSteps() == 120 && at60Hz.totalSteps() == 120 && at144Hz.totalSteps() == 120,
            "Ordinary frame partitions must each simulate 120 steps per second");
    require(at120Hz.elapsedSimulationSeconds() == 1.0, "The clock must count simulated time only");
    requireSameState(state(at120Hz), at60Hz);
    requireSameState(state(at120Hz), at144Hz);
}

void clockAndBudgets() {
    AntScene scene{2};
    constexpr double step = AntScene::FixedStepSeconds;
    require(scene.advance(step * 0.5) == 0 && scene.advance(step * 0.5) == 1,
            "Fractional frame durations must accumulate into complete steps");
    require(!scene.singleStep(), "Manual stepping is only available while paused");
    scene.advance(step * 0.5);
    scene.setPaused(true);
    const State beforePause = state(scene);
    require(scene.advance(30.0) == 0, "Paused wall time must be discarded");
    requireSameState(beforePause, scene);
    require(scene.singleStep() && scene.totalSteps() == beforePause.steps + 1,
            "Paused single step must advance exactly once");
    scene.setPaused(false);
    require(scene.advance(step * 0.5) == 0 && scene.advance(step * 0.5) == 1,
            "Resume must discard fractional time from before pausing");

    require(scene.advance(4.0 * step, 0.0) == 1 && scene.lastAdvanceLimited(),
            "A zero work budget must permit one due step and report the discarded backlog");
    require(scene.advance(0.0, 0.0) == 0 && !scene.lastAdvanceLimited(),
            "Discarded whole steps must not leak into the following frame");
    require(scene.advance(3.5 * step, 0.0) == 1 && scene.lastAdvanceLimited(),
            "A limited frame must discard whole steps but retain fractional time");
    require(scene.advance(0.5 * step, 0.0) == 1 && !scene.lastAdvanceLimited(),
            "The retained fraction must complete one step without reporting a slowdown");
    require(scene.advance(2.0 * step, std::numeric_limits<double>::infinity()) == 2,
            "An infinite work budget must permit all due steps within the step cap");
    require(scene.advance(std::numeric_limits<double>::max()) == AntScene::MaxStepsPerAdvance &&
                scene.lastAdvanceLimited(), "Stalled frames must have bounded simulation work");
    require(scene.advance(0.0) == 0, "Stalled-frame backlog must be discarded");

    for (const bool paused : {false, true}) {
        scene.setPaused(paused);
        const State beforeInvalid = state(scene);
        for (const double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::quiet_NaN()}) {
            requireInvalid([&] { scene.advance(invalid); },
                           "Invalid frame durations must be rejected even while paused");
        }
        for (const double invalid : {-1.0, -std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::quiet_NaN()}) {
            requireInvalid([&] { scene.advance(step, invalid); },
                           "Invalid work budgets must be rejected even while paused");
        }
        requireSameState(beforeInvalid, scene);
        require(scene.advance(0.0) == 0, "Rejected durations cannot accumulate simulation time");
    }

    scene.setPaused(false);
    scene.advance(step * 0.5);
    scene.reset();
    require(scene.totalSteps() == 0 && scene.advance(step * 0.5) == 0,
            "Reset must discard fractional simulation time");
    AntScene manual{8, 7};
    AntScene automatic{8, 7};
    manual.setPaused(true);
    require(manual.singleStep(), "A paused colony can be stepped manually");
    automatic.advance(step);
    requireSameState(state(automatic), manual);
}

void populationsAndParameters() {
    AntScene scene{0, 17};
    requireBoundedWorld(scene);
    ticks(scene, 8);
    require(emptyField(scene.homePheromone()) && emptyField(scene.foodPheromone()),
            "An empty colony cannot create random pheromone trails");
    scene.setPaused(true);
    auto parameters = scene.parameters();
    parameters.speed = 12.0F;
    parameters.sensorDistance = 9.0F;
    parameters.evaporation = 0.6F;
    scene.setParameters(parameters);
    scene.setCount(AntScene::MaxAnts);
    require(scene.count() == AntScene::MaxAnts && scene.paused() && scene.totalSteps() == 0,
            "The maximum population must be supported and reset the clock while retaining pause");
    require(sameParameters(parameters, scene.parameters()), "Population changes must retain colony parameters");
    requireBoundedWorld(scene);
    const State maximumPopulation = state(scene);
    requireInvalid([&] { scene.setCount(AntScene::MaxAnts + 1); },
                   "Population above the supported maximum must be rejected");
    requireInvalid([&] { scene.setCount(std::numeric_limits<std::size_t>::max()); },
                   "Overflow-sized populations must be rejected");
    requireSameState(maximumPopulation, scene);
    requireInvalid([] { AntScene excessive{AntScene::MaxAnts + 1}; },
                   "The constructor must also enforce the population limit");
    scene.setCount(1);
    scene.reset();
    require(scene.paused() && scene.count() == 1 && sameParameters(parameters, scene.parameters()),
            "Reset must retain the requested population and parameters");

    const State beforeInvalid = state(scene);
    const std::vector<float AntParameters::*> fields{
        &AntParameters::speed, &AntParameters::sensorDistance, &AntParameters::sensorAngle,
        &AntParameters::turnSpeed, &AntParameters::wander, &AntParameters::depositRate,
        &AntParameters::evaporation, &AntParameters::diffusion};
    for (auto field : fields) {
        for (const float invalid : {-1.0F, std::numeric_limits<float>::infinity(),
                                    std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::max()}) {
            auto rejected = parameters;
            rejected.*field = invalid;
            requireInvalid([&] { scene.setParameters(rejected); },
                           "Every colony parameter must reject nonfinite and out-of-range values");
            require(sameParameters(parameters, scene.parameters()),
                    "Rejected parameter updates must leave the existing settings intact");
            requireSameState(beforeInvalid, scene);
        }
    }
    for (const float invalid : {0.0F, 0.5F, 121.0F, -1.0F,
                                std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()}) {
        auto rejected = parameters;
        rejected.foodSpawnInterval = invalid;
        requireInvalid([&] { scene.setParameters(rejected); },
                       "Food spawn intervals must be finite and between one and 120 seconds");
        require(sameParameters(parameters, scene.parameters()),
                "Invalid spawn intervals must leave parameters unchanged");
        requireSameState(beforeInvalid, scene);
    }

    AntParameters stopped{};
    for (auto field : fields) {
        stopped.*field = 0.0F;
    }
    scene.setParameters(stopped);
    const auto stationaryPosition = scene.agents()[0].position;
    require(scene.singleStep() && scene.agents()[0].position == stationaryPosition,
            "Zero speed and zero steering must keep ants stationary without invalid arithmetic");
    requireBoundedWorld(scene);
}

void pheromoneLifecycle() {
    AntScene scene{1, 12};
    auto parameters = scene.parameters();
    parameters.speed = 0.0F;
    parameters.wander = 0.0F;
    parameters.depositRate = 30.0F;
    parameters.diffusion = 0.0F;
    parameters.evaporation = 0.0F;
    scene.setParameters(parameters);
    ticks(scene, 16);
    require(mass(scene.homePheromone()) > 0.0 && emptyField(scene.foodPheromone()),
            "Searching ants must create home trails through their own movement and deposition");
    const auto supportBefore = std::count_if(scene.homePheromone().begin(), scene.homePheromone().end(),
                                             [](float value) { return value > 0.0F; });
    parameters.depositRate = 0.0F;
    parameters.diffusion = 1.5F;
    scene.setParameters(parameters);
    ticks(scene, 60);
    const auto supportAfter = std::count_if(scene.homePheromone().begin(), scene.homePheromone().end(),
                                            [](float value) { return value > 0.0F; });
    require(supportAfter > supportBefore, "Diffusion must spread deposited pheromone to neighboring cells");
    requireBoundedWorld(scene);
    const double beforeEvaporation = mass(scene.homePheromone());
    parameters.diffusion = 0.0F;
    parameters.evaporation = 2.0F;
    scene.setParameters(parameters);
    ticks(scene, 60);
    require(mass(scene.homePheromone()) > 0.0 && mass(scene.homePheromone()) < beforeEvaporation,
            "Existing trails must fade when deposition stops and evaporation is enabled");
    const auto stepsBeforeClear = scene.totalSteps();
    scene.clearPheromones();
    require(emptyField(scene.homePheromone()) && emptyField(scene.foodPheromone()) &&
                scene.totalSteps() == stepsBeforeClear,
            "Clearing pheromones must erase both trails without resetting the world clock");
    ticks(scene, 1);
    require(emptyField(scene.homePheromone()) && emptyField(scene.foodPheromone()),
            "Cleared pheromones must stay empty while deposition is disabled");
}

std::size_t remainingFood(const AntScene& scene) {
    std::size_t remaining = 0;
    for (const auto& food : scene.foodSources()) {
        remaining += static_cast<std::size_t>(food.remaining);
    }
    return remaining;
}

void periodicFoodLifecycle() {
    AntScene scene{1, 37};
    require(scene.parameters().foodSpawnInterval == 10.0F &&
                scene.secondsUntilFoodSpawn() == 10.0 && scene.foodSources().size() == 6,
            "Food replenishment must default to every ten simulated seconds");
    auto parameters = scene.parameters();
    parameters.foodSpawnInterval = 1.0F;
    parameters.speed = 0.0F;
    parameters.diffusion = 0.0F;
    parameters.evaporation = 0.0F;
    scene.setParameters(parameters);
    const auto initialFood = copy(scene.foodSources());
    const auto initialGenerated = scene.totalGeneratedFood();
    require(initialGenerated == remainingFood(scene), "Initial food must be included in generated supply");
    const auto antPosition = scene.agents()[0].position;
    ticks(scene, 119);
    require(scene.foodSources().size() == 6 && scene.totalGeneratedFood() == initialGenerated &&
                std::abs(scene.secondsUntilFoodSpawn() - AntScene::FixedStepSeconds) < 1.0e-12,
            "Replenishment must wait until the complete simulation interval has elapsed");
    scene.setPaused(true);
    const auto beforePause = state(scene);
    require(scene.advance(120.0) == 0, "Paused wall time cannot trigger food generation");
    requireSameState(beforePause, scene);
    const auto pheromoneBeforeSpawn = mass(scene.homePheromone());
    require(scene.singleStep(), "A manual step advances the food schedule while paused");
    require(scene.foodSources().size() == 6 + AntScene::FoodSourcesPerSpawn &&
                scene.totalGeneratedFood() > initialGenerated && scene.secondsUntilFoodSpawn() == 1.0,
            "Exactly two new food patches must appear at the interval boundary");
    require(std::equal(initialFood.begin(), initialFood.end(), scene.foodSources().begin(), sameFood) &&
                scene.agents()[0].position == antPosition &&
                mass(scene.homePheromone()) > pheromoneBeforeSpawn && scene.totalSteps() == 120,
            "Replenishment must preserve existing food, ants, trails, and the simulation clock");
    requireBoundedWorld(scene);
    scene.setPaused(false);
    ticks(scene, 60);
    const double beforeParameterEdit = scene.secondsUntilFoodSpawn();
    parameters.wander = 2.0F;
    scene.setParameters(parameters);
    require(scene.secondsUntilFoodSpawn() == beforeParameterEdit,
            "Unrelated parameter edits must preserve the food countdown");
    parameters.foodSpawnInterval = 2.0F;
    scene.setParameters(parameters);
    require(scene.secondsUntilFoodSpawn() == 2.0,
            "Changing the food interval must restart its countdown");
    ticks(scene, 239);
    require(scene.foodSources().size() == 8, "A changed interval must not generate food early");
    ticks(scene, 1);
    require(scene.foodSources().size() == 10, "A changed interval must generate food when due");
    scene.setPaused(true);
    scene.reset();
    require(scene.foodSources().size() == 6 && scene.totalGeneratedFood() == initialGenerated &&
                scene.secondsUntilFoodSpawn() == 2.0 && scene.totalSteps() == 0 && scene.paused(),
            "Reset must restore initial supply and a full countdown while retaining pause and interval");
    require(std::equal(initialFood.begin(), initialFood.end(), scene.foodSources().begin(),
                       scene.foodSources().end(), sameFood),
            "Reset must restore the original seeded food placement");
}

void replenishmentReplayAndCapacity() {
    AntScene scene{0, 71};
    AntScene matching{0, 71};
    auto parameters = scene.parameters();
    parameters.foodSpawnInterval = 1.0F;
    parameters.diffusion = 0.0F;
    parameters.evaporation = 0.0F;
    scene.setParameters(parameters);
    matching.setParameters(parameters);
    ticks(scene, 360);
    for (int frame = 0; frame < 180; ++frame) {
        matching.advance(1.0 / 60.0);
    }
    require(scene.foodSources().size() == 12, "Repeated intervals must each add two food patches");
    requireSameState(state(scene), matching);
    ticks(scene, 720);
    require(scene.foodSources().size() == AntScene::MaxFoodSources &&
                scene.totalGeneratedFood() == remainingFood(scene),
            "An unconsumed world must reach the live food cap without losing supply");
    const auto fullFood = copy(scene.foodSources());
    const auto fullGenerated = scene.totalGeneratedFood();
    ticks(scene, 360);
    require(scene.totalGeneratedFood() == fullGenerated &&
                std::equal(fullFood.begin(), fullFood.end(), scene.foodSources().begin(),
                           scene.foodSources().end(), sameFood),
            "A full world must skip replenishment without replacing unconsumed patches");
    requireBoundedWorld(scene);
    scene.setCount(1);
    require(scene.foodSources().size() == 6 && scene.secondsUntilFoodSpawn() == 1.0 &&
                scene.totalGeneratedFood() == remainingFood(scene),
            "Population resets must also reset replenishment and generated-food accounting");

    AntScene limited{0, 71};
    limited.setParameters(parameters);
    require(limited.advance(120.0, 0.0) == 1 && limited.foodSources().size() == 6 &&
                std::abs(limited.secondsUntilFoodSpawn() - (1.0 - AntScene::FixedStepSeconds)) < 1.0e-12,
            "Dropped frame time must not advance the food generation schedule");
}

// The scenes are owned and non-const; their borrowed views let these fixtures
// isolate steering behavior without adding production-only editing controls.
void prepareThreatFixture(AntScene& scene) {
    scene.setObstacleCount(0);
    auto parameters = scene.parameters();
    parameters.wander = 0.0F;
    parameters.depositRate = 0.0F;
    parameters.diffusion = 0.0F;
    parameters.evaporation = 0.0F;
    parameters.foodSpawnInterval = 120.0F;
    scene.setParameters(parameters);
    for (const auto& food : scene.foodSources()) const_cast<FoodSource&>(food).remaining = 0;
}

void placeThreat(AntScene& scene, const glm::vec3& position) {
    auto* enemies = const_cast<Enemy*>(scene.enemies().data());
    for (std::size_t index = 0; index < scene.enemies().size(); ++index) {
        enemies[index].position = index == 0 ? position : glm::vec3{-70.0F, 0.0F, -70.0F};
        enemies[index].radius = 1.5F;
    }
}

void attractToThreat(AntScene& scene, const glm::vec3& center) {
    constexpr float spacing = 2.0F * AntScene::WorldHalfExtent /
                              static_cast<float>(AntScene::GridResolution - 1);
    auto* field = const_cast<float*>(scene.foodPheromone().data());
    for (std::size_t z = 0; z < AntScene::GridResolution; ++z) {
        for (std::size_t x = 0; x < AntScene::GridResolution; ++x) {
            const glm::vec3 position{static_cast<float>(x) * spacing - AntScene::WorldHalfExtent,
                                     0.0F,
                                     static_cast<float>(z) * spacing - AntScene::WorldHalfExtent};
            const auto delta = position - center;
            field[z * AntScene::GridResolution + x] = 20.0F * std::exp(-glm::dot(delta, delta) / 200.0F);
        }
    }
}

void strongTrailsDoNotTrapAntsAroundEnemies() {
    AntScene scene{64, 12345};
    prepareThreatFixture(scene);
    const glm::vec3 center{30.0F, 0.0F, 30.0F};
    attractToThreat(scene, center);
    auto* ants = const_cast<sim::Agent*>(scene.agents().data());
    for (std::size_t index = 0; index < scene.count(); ++index) {
        // Preserve the seeded heading and start each ant approaching the danger.
        ants[index].position = center - glm::normalize(ants[index].velocity) * 7.5F;
    }
    std::vector<float> furthest(scene.count(), 0.0F);
    for (int tick = 0; tick < 600; ++tick) {
        // Pin the enemy before each step; its ordinary movement during the step
        // is less than 0.03 units and cannot chase ants out of the fixture.
        placeThreat(scene, center);
        ticks(scene, 1);
        for (std::size_t index = 0; index < scene.count(); ++index) {
            furthest[index] = std::max(furthest[index], glm::length(ants[index].position - center));
        }
    }
    require(std::all_of(furthest.begin(), furthest.end(), [](float distance) { return distance > 14.0F; }),
            "Every ant must escape an enemy ring even when a persistent strong trail attracts it inward");
}

void escapePersistsThenForagingResumes() {
    AntScene scene{1, 12345};
    prepareThreatFixture(scene);
    const glm::vec3 center{30.0F, 0.0F, 30.0F};
    attractToThreat(scene, center);
    auto& ant = const_cast<sim::Agent&>(scene.agents().front());
    const auto outward = glm::normalize(ant.velocity);
    ant.position = center + outward * 6.0F;
    placeThreat(scene, center);
    ticks(scene, 1);
    const auto started = ant.position;
    for (int tick = 0; tick < 120; ++tick) {
        placeThreat(scene, {-70.0F, 0.0F, -70.0F});
        ticks(scene, 1);
    }
    require(glm::dot(ant.position - started, outward) > 5.0F,
            "After danger disappears, an ant must keep escaping instead of turning back toward its trail");

    const glm::vec3 sideways{-outward.z, 0.0F, outward.x};
    auto& food = const_cast<FoodSource&>(scene.foodSources().front());
    food = {ant.position + outward * 6.0F + sideways * 3.0F, 1.6F, 5};
    bool pickedUpFood = false;
    for (int tick = 0; tick < 600 && !pickedUpFood; ++tick) {
        placeThreat(scene, {-70.0F, 0.0F, -70.0F});
        ticks(scene, 1);
        pickedUpFood = scene.carryingFood().front() != 0;
    }
    require(pickedUpFood,
            "A safe ant must resume seeking and collecting food after its escape interval ends");
}

void resetsDiscardEscapeHistory() {
    AntScene scene{4, 12345};
    prepareThreatFixture(scene);
    for (const std::size_t count : {std::size_t{4}, std::size_t{6}}) {
        auto& ant = const_cast<sim::Agent&>(scene.agents().front());
        const glm::vec3 center{30.0F, 0.0F, 30.0F};
        ant.position = center + glm::normalize(ant.velocity) * 6.0F;
        placeThreat(scene, center);
        ticks(scene, 1);
        if (count == scene.count()) scene.reset();
        else scene.setCount(count);

        AntScene matching{count, scene.seed()};
        matching.setObstacleCount(0);
        matching.setParameters(scene.parameters());
        ticks(scene, 120);
        ticks(matching, 120);
        requireSameState(state(matching), scene);
    }
}

void foragingAndLongRunBounds() {
    AntScene scene{128, 42};
    const auto initialFood = remainingFood(scene);
    const auto initialEnemies = copy(scene.enemies());
    bool observedCarrying = false;
    bool observedFoodTrail = false;
    for (int second = 0; second < 60; ++second) {
        ticks(scene, 120);
        requireBoundedWorld(scene);
        observedCarrying = observedCarrying || std::any_of(scene.carryingFood().begin(), scene.carryingFood().end(),
                                                          [](std::uint8_t value) { return value != 0; });
        observedFoodTrail = observedFoodTrail || mass(scene.foodPheromone()) > 0.0;
        const auto carried = static_cast<std::size_t>(std::count_if(
            scene.carryingFood().begin(), scene.carryingFood().end(), [](std::uint8_t value) { return value != 0; }));
        require(remainingFood(scene) + carried + scene.deliveredFood() == scene.totalGeneratedFood(),
                "Food pickup and delivery must conserve the generated food supply");
    }
    require(observedCarrying && observedFoodTrail && scene.deliveredFood() > 0,
            "Ants must find food, carry it home, and deposit food trails during sustained simulation");
    require(remainingFood(scene) < scene.totalGeneratedFood() && scene.totalGeneratedFood() > initialFood,
            "Foraging must consume food while periodic replenishment increases total supply");
    require(!std::equal(initialEnemies.begin(), initialEnemies.end(), scene.enemies().begin(),
                        scene.enemies().end(), sameEnemy),
            "Hostile creatures must move through the environment");
}

} // namespace

int main() {
    try {
        deterministicWorldAndReplay();
        terrainGenerationAndControls();
        terrainMovementAndPheromones();
        framePartitioning();
        clockAndBudgets();
        populationsAndParameters();
        pheromoneLifecycle();
        periodicFoodLifecycle();
        replenishmentReplayAndCapacity();
        strongTrailsDoNotTrapAntsAroundEnemies();
        escapePersistsThenForagingResumes();
        resetsDiscardEscapeHistory();
        foragingAndLongRunBounds();
        std::cout << "Ant scene tests passed (seeded worlds, terrain, replay, controls, trails, food, and bounds)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Ant scene test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
