#include "scenarios/ants/AntScene.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace emergences::scenarios::ants {
namespace {

constexpr float Pi = std::numbers::pi_v<float>;
constexpr float StepSeconds = static_cast<float>(AntScene::FixedStepSeconds);
constexpr float CellSize = 2.0F * AntScene::WorldHalfExtent /
                           static_cast<float>(AntScene::GridResolution - 1);
constexpr glm::vec3 SearchingColor{0.90F, 0.70F, 0.35F};
constexpr glm::vec3 CarryingColor{0.35F, 1.0F, 0.45F};
constexpr float EnemyAvoidanceMargin = 5.0F;
constexpr float EscapeDuration = 2.0F;

void validateCount(std::size_t count) {
    if (count > AntScene::MaxAnts) {
        throw std::invalid_argument("Ant count exceeds the 20000 ant limit");
    }
}

void validateParameter(float value, float maximum) {
    if (!std::isfinite(value) || value < 0.0F || value > maximum) {
        throw std::invalid_argument("Ant parameter is nonfinite or outside its allowed range");
    }
}

float turnToward(float heading, float target, float maximumTurn) {
    const float difference = std::remainder(target - heading, 2.0F * Pi);
    return heading + std::clamp(difference, -maximumTurn, maximumTurn);
}

glm::vec3 direction(float heading) {
    return {std::cos(heading), 0.0F, std::sin(heading)};
}

float angle(const glm::vec3& vector) {
    return std::atan2(vector.z, vector.x);
}

// Stateless noise keeps wandering deterministic regardless of frame partition.
float noise(std::uint32_t seed, std::uint32_t id, std::uint64_t tick) {
    std::uint32_t value = seed ^ (id * 0x9E3779B9U) ^
                          (static_cast<std::uint32_t>(tick / 24U) * 0x85EBCA6BU);
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return static_cast<float>(value >> 8U) / 8388607.5F - 1.0F;
}

void reflect(glm::vec3& position, glm::vec3& velocity, float extent) {
    if (position.x < -extent || position.x > extent) {
        position.x = std::clamp(position.x, -extent, extent);
        velocity.x = position.x < 0.0F ? std::abs(velocity.x) : -std::abs(velocity.x);
    }
    if (position.z < -extent || position.z > extent) {
        position.z = std::clamp(position.z, -extent, extent);
        velocity.z = position.z < 0.0F ? std::abs(velocity.z) : -std::abs(velocity.z);
    }
    position.y = 0.0F;
    velocity.y = 0.0F;
}

struct GridSample {
    std::size_t x;
    std::size_t z;
    std::size_t nextX;
    std::size_t nextZ;
    float blendX;
    float blendZ;
};

GridSample gridSample(const glm::vec3& position) {
    constexpr std::size_t last = AntScene::GridResolution - 1;
    const float x = std::clamp((position.x + AntScene::WorldHalfExtent) / CellSize,
                               0.0F, static_cast<float>(last));
    const float z = std::clamp((position.z + AntScene::WorldHalfExtent) / CellSize,
                               0.0F, static_cast<float>(last));
    const auto cellX = static_cast<std::size_t>(x);
    const auto cellZ = static_cast<std::size_t>(z);
    return {cellX, cellZ, std::min(cellX + 1, last), std::min(cellZ + 1, last),
            x - static_cast<float>(cellX), z - static_cast<float>(cellZ)};
}

} // namespace

AntScene::AntScene(std::size_t count, std::uint32_t seed)
    : seed_{seed}, random_{seed},
      blockedCells_(GridResolution * GridResolution, 0),
      foodPheromone_(GridResolution * GridResolution, 0.0F),
      homePheromone_(GridResolution * GridResolution, 0.0F),
      fieldScratch_(GridResolution * GridResolution, 0.0F) {
    validateCount(count);
    agents_.resize(count);
    reset();
}

const AntParameters& AntScene::parameters() const noexcept { return parameters_; }

void AntScene::setParameters(const AntParameters& parameters) {
    validateParameter(parameters.speed, 30.0F);
    validateParameter(parameters.sensorDistance, 20.0F);
    validateParameter(parameters.sensorAngle, Pi);
    validateParameter(parameters.turnSpeed, 20.0F);
    validateParameter(parameters.wander, 10.0F);
    validateParameter(parameters.depositRate, 30.0F);
    validateParameter(parameters.evaporation, 10.0F);
    validateParameter(parameters.diffusion, 10.0F);
    validateParameter(parameters.foodSpawnInterval, 120.0F);
    if (parameters.foodSpawnInterval < 1.0F) {
        throw std::invalid_argument("Food spawn interval must be at least one simulated second");
    }
    if (parameters.foodSpawnInterval != parameters_.foodSpawnInterval) {
        nextFoodSpawnTime_ = elapsedSimulationSeconds() + parameters.foodSpawnInterval;
    }
    parameters_ = parameters;
    for (std::size_t index = 0; index < agents_.size(); ++index) {
        agents_[index].velocity = direction(headings_[index]) * parameters_.speed;
    }
}

std::size_t AntScene::count() const noexcept { return agents_.size(); }

void AntScene::setCount(std::size_t count) {
    validateCount(count);
    if (count != agents_.size()) {
        agents_.resize(count);
        reset();
    }
}

std::uint32_t AntScene::seed() const noexcept { return seed_; }

std::size_t AntScene::obstacleCount() const noexcept { return obstacleCount_; }

void AntScene::setObstacleCount(std::size_t count) {
    if (count > MaxObstacles) {
        throw std::invalid_argument("Obstacle count exceeds the 48 obstacle limit");
    }
    if (count != obstacleCount_) {
        obstacleCount_ = count;
        reset();
    }
}

float AntScene::random(float minimum, float maximum) {
    const float unit = static_cast<float>(random_() >> 8U) / 16777216.0F;
    return minimum + (maximum - minimum) * unit;
}

void AntScene::reset() {
    random_.seed(seed_);
    foodSources_.clear();
    enemies_.clear();
    enemyHeadings_.clear();
    enemyPursuits_.clear();
    clearPheromones();
    accumulator_ = 0.0;
    totalSteps_ = 0;
    deliveredFood_ = 0;
    totalGeneratedFood_ = 0;
    nextFoodSpawnTime_ = parameters_.foodSpawnInterval;
    lastAdvanceLimited_ = false;

    // Separated angular sectors keep food discoverable around the whole nest,
    // while seeded angle, distance, radius and quantity vary the environment.
    const float rotation = random(-Pi, Pi);
    for (std::size_t index = 0; index < 6; ++index) {
        const float heading = rotation + static_cast<float>(index) * (2.0F * Pi / 6.0F) +
                              random(-0.25F, 0.25F);
        const float distance = random(26.0F, 68.0F);
        foodSources_.push_back({direction(heading) * distance, random(1.6F, 2.8F),
                                static_cast<int>(random(400.0F, 901.0F))});
        totalGeneratedFood_ += static_cast<std::size_t>(foodSources_.back().remaining);
    }
    for (std::size_t index = 0; index < 3; ++index) {
        const float heading = random(-Pi, Pi);
        enemies_.push_back({direction(random(-Pi, Pi)) * random(34.0F, 66.0F),
                            direction(heading) * random(1.5F, 2.8F), random(1.2F, 1.8F)});
        enemyHeadings_.push_back(heading);
        enemyPursuits_.push_back({{}, 0, glm::length(enemies_.back().velocity)});
    }
    generateTerrain();
    float navigationRadius = 0.0F;
    for (const auto& enemy : enemies_) navigationRadius = std::max(navigationRadius, enemy.radius);
    enemyPathfinder_.rebuild(obstacles_, navigationRadius + 0.1F);

    carryingFood_.assign(agents_.size(), 0);
    headings_.resize(agents_.size());
    trailStrength_.assign(agents_.size(), 1.0F);
    escapes_.assign(agents_.size(), {});
    for (std::size_t index = 0; index < agents_.size(); ++index) {
        const float heading = random(-Pi, Pi);
        const float radius = std::sqrt(random(0.0F, 1.0F)) * nestRadius() * 0.85F;
        headings_[index] = heading;
        agents_[index] = {};
        agents_[index].id = static_cast<std::uint32_t>(index);
        agents_[index].position = direction(heading) * radius;
        agents_[index].velocity = direction(heading) * parameters_.speed;
        agents_[index].size = AntRadius * 2.0F;
        agents_[index].color = SearchingColor;
    }
}

void AntScene::randomize(std::uint32_t seed) {
    seed_ = seed;
    reset();
}

void AntScene::clearPheromones() noexcept {
    std::fill(foodPheromone_.begin(), foodPheromone_.end(), 0.0F);
    std::fill(homePheromone_.begin(), homePheromone_.end(), 0.0F);
}

std::size_t AntScene::advance(double elapsedSeconds, double workBudgetSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) {
        throw std::invalid_argument("Scene elapsed time must be finite and nonnegative");
    }
    if (std::isnan(workBudgetSeconds) || workBudgetSeconds < 0.0) {
        throw std::invalid_argument("Scene work budget must be nonnegative or positive infinity");
    }
    lastAdvanceLimited_ = false;
    if (paused_) {
        return 0;
    }
    constexpr double maximumElapsed = FixedStepSeconds * MaxStepsPerAdvance;
    constexpr double roundingTolerance = FixedStepSeconds * 1.0e-12;
    lastAdvanceLimited_ = elapsedSeconds > maximumElapsed;
    accumulator_ += std::min(elapsedSeconds, maximumElapsed);
    const bool budgeted = std::isfinite(workBudgetSeconds);
    const auto started = budgeted ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point{};
    std::size_t steps = 0;
    while (steps < MaxStepsPerAdvance && accumulator_ + roundingTolerance >= FixedStepSeconds) {
        step();
        accumulator_ = std::max(0.0, accumulator_ - FixedStepSeconds);
        ++totalSteps_;
        ++steps;
        if (budgeted && std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count() >= workBudgetSeconds) {
            if (accumulator_ + roundingTolerance >= FixedStepSeconds) {
                const double remainingSteps = std::floor(
                    (accumulator_ + roundingTolerance) / FixedStepSeconds);
                accumulator_ = std::max(0.0, accumulator_ - remainingSteps * FixedStepSeconds);
                lastAdvanceLimited_ = true;
            }
            break;
        }
    }
    return steps;
}

bool AntScene::lastAdvanceLimited() const noexcept { return lastAdvanceLimited_; }
void AntScene::setPaused(bool paused) noexcept {
    lastAdvanceLimited_ = false;
    if (paused_ != paused) {
        accumulator_ = 0.0;
        paused_ = paused;
    }
}
bool AntScene::paused() const noexcept { return paused_; }
bool AntScene::singleStep() {
    lastAdvanceLimited_ = false;
    if (!paused_) {
        return false;
    }
    step();
    ++totalSteps_;
    return true;
}
std::uint64_t AntScene::totalSteps() const noexcept { return totalSteps_; }
double AntScene::elapsedSimulationSeconds() const noexcept {
    return static_cast<double>(totalSteps_) * FixedStepSeconds;
}

std::span<const sim::Agent> AntScene::agents() const noexcept { return agents_; }
std::span<const std::uint8_t> AntScene::carryingFood() const noexcept { return carryingFood_; }
std::span<const FoodSource> AntScene::foodSources() const noexcept { return foodSources_; }
std::span<const Enemy> AntScene::enemies() const noexcept { return enemies_; }
std::span<const Obstacle> AntScene::obstacles() const noexcept { return obstacles_; }
std::span<const float> AntScene::foodPheromone() const noexcept { return foodPheromone_; }
std::span<const float> AntScene::homePheromone() const noexcept { return homePheromone_; }
glm::vec3 AntScene::nestPosition() const noexcept { return {0.0F, 0.0F, 0.0F}; }
float AntScene::nestRadius() const noexcept { return 2.5F; }
std::size_t AntScene::deliveredFood() const noexcept { return deliveredFood_; }
std::size_t AntScene::totalGeneratedFood() const noexcept { return totalGeneratedFood_; }
double AntScene::secondsUntilFoodSpawn() const noexcept {
    return std::max(0.0, nextFoodSpawnTime_ - elapsedSimulationSeconds());
}

void AntScene::generateTerrain() {
    obstacles_.clear();
    std::fill(blockedCells_.begin(), blockedCells_.end(), std::uint8_t{0});
    if (obstacleCount_ == 0) return;

    // A separate random stream keeps the colony and initial food unchanged when
    // density changes. Larger rocks shrink next to existing neighbors to keep
    // four world units of clearance for both ants and hostile creatures.
    std::mt19937 terrainRandom{seed_ ^ 0xD1B54A35U};
    const auto unit = [&terrainRandom] {
        return static_cast<float>(terrainRandom() >> 8U) / 16777216.0F;
    };
    std::array<Obstacle, 100> candidates;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        candidates[index] = {{-67.5F + 15.0F * static_cast<float>(index % 10) + unit() * 2.0F - 1.0F,
                               0.0F,
                              -67.5F + 15.0F * static_cast<float>(index / 10) + unit() * 2.0F - 1.0F},
                              MinObstacleRadius + unit() * (MaxObstacleRadius - MinObstacleRadius)};
    }
    for (std::size_t remaining = candidates.size(); remaining > 1; --remaining) {
        std::swap(candidates[remaining - 1], candidates[terrainRandom() % remaining]);
    }
    for (auto candidate : candidates) {
        for (const auto& obstacle : obstacles_) {
            candidate.radius = std::min(candidate.radius,
                glm::length(candidate.position - obstacle.position) - obstacle.radius - 4.0F);
        }
        if (glm::length(candidate.position - nestPosition()) < candidate.radius + nestRadius() + 7.0F)
            continue;
        const bool nearFood = std::any_of(foodSources_.begin(), foodSources_.end(), [&](const FoodSource& food) {
            return glm::length(candidate.position - food.position) < candidate.radius + food.radius + 2.0F;
        });
        const bool nearEnemy = std::any_of(enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
            return glm::length(candidate.position - enemy.position) < candidate.radius + enemy.radius + 1.0F;
        });
        if (nearFood || nearEnemy) continue;
        obstacles_.push_back(candidate);
        if (obstacles_.size() == obstacleCount_) break;
    }
    // Sites remain at least 13 units apart, so even a 7-unit neighbor leaves
    // room for a 2-unit rock and a 4-unit passage. The 100 candidates still
    // accommodate 48 rocks after reserving nest, food and enemy spawn space.
    for (std::size_t z = 0; z < GridResolution; ++z) {
        for (std::size_t x = 0; x < GridResolution; ++x) {
            const glm::vec3 position{-WorldHalfExtent + static_cast<float>(x) * CellSize, 0.0F,
                                      -WorldHalfExtent + static_cast<float>(z) * CellSize};
            blockedCells_[z * GridResolution + x] = terrainClear(position, 0.0F) ? 0 : 1;
        }
    }
}

bool AntScene::terrainClear(const glm::vec3& position, float radius) const noexcept {
    for (const auto& obstacle : obstacles_) {
        const auto delta = position - obstacle.position;
        const float clearance = radius + obstacle.radius;
        if (glm::dot(delta, delta) <= clearance * clearance) return false;
    }
    return true;
}

bool AntScene::clearPath(const glm::vec3& from, const glm::vec3& to, float radius) const noexcept {
    const auto segment = to - from;
    const float lengthSquared = glm::dot(segment, segment);
    for (const auto& obstacle : obstacles_) {
        const float fraction = lengthSquared > 1.0e-8F
            ? std::clamp(glm::dot(obstacle.position - from, segment) / lengthSquared, 0.0F, 1.0F) : 0.0F;
        const auto delta = from + segment * fraction - obstacle.position;
        const float clearance = obstacle.radius + radius;
        if (glm::dot(delta, delta) <= clearance * clearance) return false;
    }
    return true;
}

float AntScene::avoidTerrain(const glm::vec3& position, float heading, float radius,
                            float speed, float maximumTurn, std::uint32_t id) const {
    if (speed == 0.0F) return heading;
    const auto forward = direction(heading);
    const float lookAhead = std::clamp(speed * 0.5F, 1.5F, 8.0F);
    const Obstacle* nearest = nullptr;
    float nearestDistance = std::numeric_limits<float>::infinity();
    for (const auto& obstacle : obstacles_) {
        const auto offset = obstacle.position - position;
        const float along = glm::dot(offset, forward);
        if (along <= 0.0F) continue;
        const float clearance = obstacle.radius + radius + 0.6F;
        const auto closest = forward * std::min(along, lookAhead) - offset;
        if (glm::dot(closest, closest) < clearance * clearance && along < nearestDistance) {
            nearest = &obstacle;
            nearestDistance = along;
        }
    }
    if (nearest == nullptr) return heading;
    const auto normal = glm::normalize(position - nearest->position);
    glm::vec3 tangent{-normal.z, 0.0F, normal.x};
    const float alignment = glm::dot(tangent, forward);
    if (alignment < -1.0e-5F || (std::abs(alignment) <= 1.0e-5F && (id & 1U) != 0)) tangent = -tangent;
    return turnToward(heading, angle(tangent + normal * 0.15F), std::max(maximumTurn, 6.0F * StepSeconds));
}

void AntScene::moveOnTerrain(glm::vec3& position, glm::vec3& velocity, float radius) const {
    const float speed = glm::length(velocity);
    if (speed == 0.0F) return;
    float timeLeft = StepSeconds;
    // Swept circle contacts prevent tunneling even at maximum speed or with
    // steering disabled. Remaining motion slides tangentially around the rock.
    for (int contact = 0; contact < 3; ++contact) {
        const auto motion = velocity * timeLeft;
        const float a = glm::dot(motion, motion);
        if (a < 1.0e-12F) return;
        float firstHit = 1.0F;
        const Obstacle* hit = nullptr;
        for (const auto& obstacle : obstacles_) {
            const float clearance = obstacle.radius + radius + 0.001F;
            const auto offset = position - obstacle.position;
            const float b = glm::dot(offset, motion);
            if (b >= 0.0F) continue;
            const float c = glm::dot(offset, offset) - clearance * clearance;
            const float discriminant = b * b - a * c;
            if (discriminant < 0.0F) continue;
            const float fraction = std::max(0.0F, (-b - std::sqrt(discriminant)) / a);
            if (fraction < firstHit) {
                firstHit = fraction;
                hit = &obstacle;
            }
        }
        position += motion * firstHit;
        if (hit == nullptr) return;
        const auto normal = glm::normalize(position - hit->position);
        position = hit->position + normal * (hit->radius + radius + 0.002F);
        const glm::vec3 tangent{-normal.z, 0.0F, normal.x};
        velocity = tangent * (glm::dot(velocity, tangent) < 0.0F ? -speed : speed);
        timeLeft *= 1.0F - firstHit;
    }
}

void AntScene::spawnFood() {
    for (std::size_t index = 0; index < FoodSourcesPerSpawn; ++index) {
        const auto empty = std::find_if(foodSources_.begin(), foodSources_.end(),
                                       [](const FoodSource& food) { return food.remaining == 0; });
        if (empty == foodSources_.end() && foodSources_.size() >= MaxFoodSources) {
            return;
        }

        FoodSource food;
        food.radius = random(1.6F, 2.8F);
        const float extent = WorldHalfExtent - food.radius;
        const float nestClearance = nestRadius() + food.radius + 4.0F;
        bool placed = false;
        for (int attempt = 0; attempt < 128; ++attempt) {
            food.position = {random(-extent, extent), 0.0F, random(-extent, extent)};
            if (glm::dot(food.position, food.position) >= nestClearance * nestClearance &&
                terrainClear(food.position, food.radius + 1.0F)) {
                placed = true;
                break;
            }
        }
        if (!placed) return;
        food.remaining = static_cast<int>(random(400.0F, 901.0F));
        totalGeneratedFood_ += static_cast<std::size_t>(food.remaining);
        if (empty != foodSources_.end()) {
            *empty = food;
        } else {
            foodSources_.push_back(food);
        }
    }
}

float AntScene::sampleField(const std::vector<float>& field,
                            const glm::vec3& position) const noexcept {
    const auto sample = gridSample(position);
    const float bottom = field[sample.z * GridResolution + sample.x] * (1.0F - sample.blendX) +
                         field[sample.z * GridResolution + sample.nextX] * sample.blendX;
    const float top = field[sample.nextZ * GridResolution + sample.x] * (1.0F - sample.blendX) +
                      field[sample.nextZ * GridResolution + sample.nextX] * sample.blendX;
    return bottom * (1.0F - sample.blendZ) + top * sample.blendZ;
}

void AntScene::deposit(std::vector<float>& field, const glm::vec3& position, float amount) {
    const auto sample = gridSample(position);
    const auto add = [this, &field](std::size_t x, std::size_t z, float value) {
        const auto index = z * GridResolution + x;
        if (blockedCells_[index] != 0) return;
        auto& cell = field[index];
        cell = std::min(MaximumPheromone, cell + value);
    };
    add(sample.x, sample.z, amount * (1.0F - sample.blendX) * (1.0F - sample.blendZ));
    add(sample.nextX, sample.z, amount * sample.blendX * (1.0F - sample.blendZ));
    add(sample.x, sample.nextZ, amount * (1.0F - sample.blendX) * sample.blendZ);
    add(sample.nextX, sample.nextZ, amount * sample.blendX * sample.blendZ);
}

void AntScene::updateField(std::vector<float>& field) {
    if (parameters_.diffusion == 0.0F && parameters_.evaporation == 0.0F) {
        return;
    }
    const float diffusion = parameters_.diffusion * StepSeconds / (CellSize * CellSize);
    const float retention = std::exp(-parameters_.evaporation * StepSeconds);
    // The validated diffusion maximum keeps this explicit stencil stable
    // (coefficient <= 0.212 < 1/4). Clamped neighbors impose no-flux borders.
    for (std::size_t z = 0; z < GridResolution; ++z) {
        const auto previousZ = z == 0 ? z : z - 1;
        const auto nextZ = std::min(z + 1, GridResolution - 1);
        for (std::size_t x = 0; x < GridResolution; ++x) {
            const auto previousX = x == 0 ? x : x - 1;
            const auto nextX = std::min(x + 1, GridResolution - 1);
            const auto index = z * GridResolution + x;
            if (blockedCells_[index] != 0) {
                fieldScratch_[index] = 0.0F;
                continue;
            }
            const float center = field[index];
            // Reflect flux at rock surfaces, just as at the world boundary.
            const auto neighbor = [&](std::size_t cell) {
                return blockedCells_[cell] != 0 ? center : field[cell];
            };
            const float neighbors = neighbor(z * GridResolution + previousX) +
                                    neighbor(z * GridResolution + nextX) +
                                    neighbor(previousZ * GridResolution + x) +
                                    neighbor(nextZ * GridResolution + x);
            fieldScratch_[index] = std::clamp(
                (center + diffusion * (neighbors - 4.0F * center)) * retention,
                0.0F, MaximumPheromone);
        }
    }
    field.swap(fieldScratch_);
}

void AntScene::planEnemyPaths() {
    // Sample a local head count, not pheromones: old trails must not attract
    // predators after ants have moved away. The coarse density map is separate
    // from both the diffusion field and the finer A* navigation graph.
    constexpr int resolution = 40;
    constexpr float spacing = 2.0F * WorldHalfExtent / static_cast<float>(resolution);
    constexpr int reach = static_cast<int>(EnemyDensityRadius / spacing) + 1;
    struct DensityCell {
        std::size_t count{0};
        glm::vec3 sum{0.0F};
    };
    struct Target {
        glm::vec3 position;
        std::size_t count;
        float distanceSquared;
        std::size_t cell;
    };
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        const auto& enemy = enemies_[index];
        auto& pursuit = enemyPursuits_[index];
        pursuit.path.clear();
        pursuit.waypoint = 0;
        if (agents_.empty() || !enemyPathfinder_.navigable(enemy.position)) continue;
        std::array<DensityCell, resolution * resolution> density{};
        for (const auto& ant : agents_) {
            const auto offset = ant.position - enemy.position;
            if (glm::dot(offset, offset) > EnemyTrackingRange * EnemyTrackingRange ||
                glm::dot(ant.position, ant.position) <= 9.0F * 9.0F) continue;
            const int cellX = static_cast<int>((ant.position.x + WorldHalfExtent) / spacing);
            const int cellZ = static_cast<int>((ant.position.z + WorldHalfExtent) / spacing);
            for (int z = std::max(0, cellZ - reach); z <= std::min(resolution - 1, cellZ + reach); ++z) {
                for (int x = std::max(0, cellX - reach); x <= std::min(resolution - 1, cellX + reach); ++x) {
                    const glm::vec3 center{-WorldHalfExtent + (static_cast<float>(x) + 0.5F) * spacing,
                                           0.0F,
                                          -WorldHalfExtent + (static_cast<float>(z) + 0.5F) * spacing};
                    const auto delta = ant.position - center;
                    if (glm::dot(delta, delta) > EnemyDensityRadius * EnemyDensityRadius) continue;
                    auto& cell = density[static_cast<std::size_t>(z * resolution + x)];
                    ++cell.count;
                    cell.sum += ant.position;
                }
            }
        }
        std::vector<Target> targets;
        for (std::size_t cell = 0; cell < density.size(); ++cell) {
            if (density[cell].count == 0) continue;
            auto position = density[cell].sum / static_cast<float>(density[cell].count);
            // A crowd can straddle a rock; its centroid may lie inside it.
            // In that case pursue a nearby clear density sample instead.
            if (!enemyPathfinder_.navigable(position)) {
                position = {-WorldHalfExtent + (static_cast<float>(cell % resolution) + 0.5F) * spacing,
                             0.0F,
                            -WorldHalfExtent + (static_cast<float>(cell / resolution) + 0.5F) * spacing};
            }
            const auto offset = position - enemy.position;
            const float distanceSquared = glm::dot(offset, offset);
            if (distanceSquared > EnemyTrackingRange * EnemyTrackingRange ||
                !enemyPathfinder_.navigable(position)) continue;
            targets.push_back({position, density[cell].count, distanceSquared, cell});
        }
        std::sort(targets.begin(), targets.end(), [](const Target& left, const Target& right) {
            if (left.count != right.count) return left.count > right.count;
            if (left.distanceSquared != right.distanceSquared) return left.distanceSquared < right.distanceSquared;
            return left.cell < right.cell;
        });
        std::vector<glm::vec3> attempted;
        for (const auto& target : targets) {
            if (std::find(attempted.begin(), attempted.end(), target.position) != attempted.end()) continue;
            attempted.push_back(target.position);
            pursuit.path = enemyPathfinder_.findPath(enemy.position, target.position);
            if (!pursuit.path.empty()) break;
        }
    }
}

void AntScene::updateEnemies() {
    if (totalSteps_ % EnemyReplanSteps == 0) planEnemyPaths();
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        auto& enemy = enemies_[index];
        auto& pursuit = enemyPursuits_[index];
        const float speed = pursuit.speed;
        auto& heading = enemyHeadings_[index];
        if (!pursuit.path.empty()) {
            while (pursuit.waypoint < pursuit.path.size() &&
                   glm::length(pursuit.path[pursuit.waypoint] - enemy.position) <= 0.001F) {
                ++pursuit.waypoint;
            }
            enemy.velocity = glm::vec3{0.0F};
            if (pursuit.waypoint < pursuit.path.size()) {
                const auto offset = pursuit.path[pursuit.waypoint] - enemy.position;
                const float distance = glm::length(offset);
                const float targetHeading = angle(offset);
                heading = turnToward(heading, targetHeading, 2.5F * StepSeconds);
                // Turn before following each segment so smoothing the heading
                // cannot cut an A* corner through a rock or the protected nest.
                if (std::abs(std::remainder(targetHeading - heading, 2.0F * Pi)) < 0.05F) {
                    enemy.velocity = offset / distance * std::min(speed, distance / StepSeconds);
                    moveOnTerrain(enemy.position, enemy.velocity, enemy.radius);
                }
            }
            continue;
        }
        heading += noise(seed_ ^ 0xA12D4F19U, static_cast<std::uint32_t>(index), totalSteps_) *
                   0.8F * StepSeconds;
        // Enemies roam outside the nest's protected inner area.
        if (glm::dot(enemy.position, enemy.position) < 9.0F * 9.0F) {
            heading = turnToward(heading, angle(enemy.position), 2.5F * StepSeconds);
        }
        heading = avoidTerrain(enemy.position, heading, enemy.radius, speed, 2.5F * StepSeconds,
                               static_cast<std::uint32_t>(index));
        enemy.velocity = direction(heading) * speed;
        moveOnTerrain(enemy.position, enemy.velocity, enemy.radius);
        reflect(enemy.position, enemy.velocity, WorldHalfExtent - enemy.radius);
        heading = angle(enemy.velocity);
    }
}

void AntScene::step() {
    const double completedTime = static_cast<double>(totalSteps_ + 1) * FixedStepSeconds;
    if (completedTime + FixedStepSeconds * 1.0e-9 >= nextFoodSpawnTime_) {
        spawnFood();
        nextFoodSpawnTime_ += parameters_.foodSpawnInterval;
    }
    updateField(homePheromone_);
    updateField(foodPheromone_);
    updateEnemies();
    const float maximumTurn = parameters_.turnSpeed * StepSeconds;
    const float trailDecay = std::exp(-0.07F * StepSeconds);
    const float foodDetection = std::max(8.0F, parameters_.sensorDistance * 2.0F);
    for (std::size_t index = 0; index < agents_.size(); ++index) {
        auto& ant = agents_[index];
        auto& heading = headings_[index];
        auto& carrying = carryingFood_[index];
        auto& strength = trailStrength_[index];
        if (carrying != 0 && glm::dot(ant.position, ant.position) <= nestRadius() * nestRadius()) {
            carrying = 0;
            ++deliveredFood_;
            strength = 1.0F;
            heading += Pi;
        }

        const float headingBeforeSensing = heading;
        auto& escape = escapes_[index];
        glm::vec3 avoidance{0.0F};
        float nearestThreatDistance = std::numeric_limits<float>::infinity();
        glm::vec3 nearestThreatDirection{0.0F};
        for (const auto& enemy : enemies_) {
            auto away = ant.position - enemy.position;
            const float distance = glm::length(away);
            const float detection = enemy.radius + EnemyAvoidanceMargin;
            if (distance < detection) {
                away = distance < 1.0e-4F ? direction(heading) : away / distance;
                avoidance += away * (detection - distance);
                if (distance < nearestThreatDistance) {
                    nearestThreatDistance = distance;
                    nearestThreatDirection = away;
                }
            }
        }
        if (std::isfinite(nearestThreatDistance)) {
            // Opposing threats can cancel their sum; still leave the closest one.
            escape.heading = angle(glm::dot(avoidance, avoidance) > 1.0e-6F
                                       ? avoidance : nearestThreatDirection);
            escape.secondsRemaining = EscapeDuration;
        }
        const bool escaping = escape.secondsRemaining > 0.0F;
        if (escaping) {
            // Keep going after crossing the danger boundary. Resuming attraction
            // immediately traps ants in a ring around an occupied food source.
            heading = turnToward(headingBeforeSensing, escape.heading,
                                 std::max(maximumTurn * 3.0F, 6.0F * StepSeconds));
            escape.secondsRemaining = std::max(0.0F, escape.secondsRemaining - StepSeconds);
        } else {
            const auto& targetField = carrying != 0 ? homePheromone_ : foodPheromone_;
            const auto sense = [&](float sensorHeading) {
                const auto target = ant.position + direction(sensorHeading) * parameters_.sensorDistance;
                return clearPath(ant.position, target) ? sampleField(targetField, target) : 0.0F;
            };
            const float left = sense(heading - parameters_.sensorAngle);
            const float forward = sense(heading);
            const float right = sense(heading + parameters_.sensorAngle);
            float sensedHeading = heading;
            const float current = sampleField(targetField, ant.position);
            // A relative winner can still point downhill at an old trail peak.
            // Cross such peaks and resume exploration instead of orbiting them.
            if (std::max({left, forward, right}) <= current + 1.0e-5F) {
                sensedHeading += noise(seed_, ant.id, totalSteps_) * parameters_.wander * StepSeconds;
            } else if (left > forward + 1.0e-5F && left > right + 1.0e-5F) {
                sensedHeading -= maximumTurn;
            } else if (right > forward + 1.0e-5F && right > left + 1.0e-5F) {
                sensedHeading += maximumTurn;
            } else if (forward < 1.0e-4F || std::abs(left - right) < 1.0e-5F) {
                sensedHeading += noise(seed_, ant.id, totalSteps_) * parameters_.wander * StepSeconds;
            }
            heading = sensedHeading;

            if (carrying != 0) {
                // A path integration bias keeps a carrier headed home when a trail
                // is weak or broken, while local sensors still influence its route.
                const auto homeDirection = glm::normalize(-ant.position);
                const auto desired = homeDirection + direction(sensedHeading) * 0.45F;
                heading = turnToward(headingBeforeSensing, angle(desired), maximumTurn);
            } else {
                FoodSource* nearest = nullptr;
                float nearestDistanceSquared = foodDetection * foodDetection;
                for (auto& food : foodSources_) {
                    if (food.remaining == 0) {
                        continue;
                    }
                    const auto delta = food.position - ant.position;
                    const float distanceSquared = glm::dot(delta, delta);
                    if (distanceSquared < nearestDistanceSquared && clearPath(ant.position, food.position, AntRadius)) {
                        nearest = &food;
                        nearestDistanceSquared = distanceSquared;
                    }
                }
                if (nearest != nullptr) {
                    if (nearestDistanceSquared <= nearest->radius * nearest->radius) {
                        --nearest->remaining;
                        carrying = 1;
                        strength = 1.0F;
                        heading += Pi;
                    } else {
                        heading = turnToward(headingBeforeSensing, angle(nearest->position - ant.position), maximumTurn);
                    }
                }
            }
        }
        heading = avoidTerrain(ant.position, heading, AntRadius, parameters_.speed, maximumTurn, ant.id);
        ant.velocity = direction(heading) * parameters_.speed;
        moveOnTerrain(ant.position, ant.velocity, AntRadius);
        reflect(ant.position, ant.velocity, WorldHalfExtent);
        if (parameters_.speed > 0.0F) {
            heading = angle(ant.velocity);
        } else {
            heading = std::remainder(heading, 2.0F * Pi);
        }
        ant.color = carrying != 0 ? CarryingColor : SearchingColor;
        strength *= trailDecay;
        // Detours taken while fleeing do not advertise a useful foraging route.
        if (!escaping) {
            deposit(carrying != 0 ? foodPheromone_ : homePheromone_, ant.position,
                    parameters_.depositRate * StepSeconds * strength);
        }
    }
}

} // namespace emergences::scenarios::ants
