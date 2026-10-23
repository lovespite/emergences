#include "scenarios/ants/AntScene.h"

#include <algorithm>
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

float AntScene::random(float minimum, float maximum) {
    const float unit = static_cast<float>(random_() >> 8U) / 16777216.0F;
    return minimum + (maximum - minimum) * unit;
}

void AntScene::reset() {
    random_.seed(seed_);
    foodSources_.clear();
    enemies_.clear();
    enemyHeadings_.clear();
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
    }

    carryingFood_.assign(agents_.size(), 0);
    headings_.resize(agents_.size());
    trailStrength_.assign(agents_.size(), 1.0F);
    for (std::size_t index = 0; index < agents_.size(); ++index) {
        const float heading = random(-Pi, Pi);
        const float radius = std::sqrt(random(0.0F, 1.0F)) * nestRadius() * 0.85F;
        headings_[index] = heading;
        agents_[index] = {};
        agents_[index].id = static_cast<std::uint32_t>(index);
        agents_[index].position = direction(heading) * radius;
        agents_[index].velocity = direction(heading) * parameters_.speed;
        agents_[index].size = 0.42F;
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
std::span<const float> AntScene::foodPheromone() const noexcept { return foodPheromone_; }
std::span<const float> AntScene::homePheromone() const noexcept { return homePheromone_; }
glm::vec3 AntScene::nestPosition() const noexcept { return {0.0F, 0.0F, 0.0F}; }
float AntScene::nestRadius() const noexcept { return 2.5F; }
std::size_t AntScene::deliveredFood() const noexcept { return deliveredFood_; }
std::size_t AntScene::totalGeneratedFood() const noexcept { return totalGeneratedFood_; }
double AntScene::secondsUntilFoodSpawn() const noexcept {
    return std::max(0.0, nextFoodSpawnTime_ - elapsedSimulationSeconds());
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
        do {
            food.position = {random(-extent, extent), 0.0F, random(-extent, extent)};
        } while (glm::dot(food.position, food.position) < nestClearance * nestClearance);
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
    const auto add = [&field](std::size_t x, std::size_t z, float value) {
        auto& cell = field[z * GridResolution + x];
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
            const float center = field[index];
            const float neighbors = field[z * GridResolution + previousX] +
                                    field[z * GridResolution + nextX] +
                                    field[previousZ * GridResolution + x] +
                                    field[nextZ * GridResolution + x];
            fieldScratch_[index] = std::clamp(
                (center + diffusion * (neighbors - 4.0F * center)) * retention,
                0.0F, MaximumPheromone);
        }
    }
    field.swap(fieldScratch_);
}

void AntScene::updateEnemies() {
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        auto& enemy = enemies_[index];
        const float speed = glm::length(enemy.velocity);
        auto& heading = enemyHeadings_[index];
        heading += noise(seed_ ^ 0xA12D4F19U, static_cast<std::uint32_t>(index), totalSteps_) *
                   0.8F * StepSeconds;
        // Enemies roam outside the nest's protected inner area.
        if (glm::dot(enemy.position, enemy.position) < 9.0F * 9.0F) {
            heading = turnToward(heading, angle(enemy.position), 2.5F * StepSeconds);
        }
        enemy.velocity = direction(heading) * speed;
        enemy.position += enemy.velocity * StepSeconds;
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
        const auto& targetField = carrying != 0 ? homePheromone_ : foodPheromone_;
        const float left = sampleField(targetField,
            ant.position + direction(heading - parameters_.sensorAngle) * parameters_.sensorDistance);
        const float forward = sampleField(targetField,
            ant.position + direction(heading) * parameters_.sensorDistance);
        const float right = sampleField(targetField,
            ant.position + direction(heading + parameters_.sensorAngle) * parameters_.sensorDistance);
        float sensedHeading = heading;
        if (left > forward + 1.0e-5F && left > right + 1.0e-5F) {
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
                if (distanceSquared < nearestDistanceSquared) {
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

        glm::vec3 avoidance{0.0F};
        for (const auto& enemy : enemies_) {
            auto away = ant.position - enemy.position;
            const float distance = glm::length(away);
            const float detection = enemy.radius + 5.0F;
            if (distance < detection) {
                if (distance < 1.0e-4F) {
                    away = direction(heading + Pi);
                } else {
                    away /= distance;
                }
                avoidance += away * (detection - distance);
            }
        }
        if (glm::dot(avoidance, avoidance) > 1.0e-6F) {
            heading = turnToward(heading, angle(avoidance), std::max(maximumTurn * 3.0F,
                                                                     6.0F * StepSeconds));
        }
        ant.velocity = direction(heading) * parameters_.speed;
        ant.position += ant.velocity * StepSeconds;
        reflect(ant.position, ant.velocity, WorldHalfExtent);
        if (parameters_.speed > 0.0F) {
            heading = angle(ant.velocity);
        } else {
            heading = std::remainder(heading, 2.0F * Pi);
        }
        ant.color = carrying != 0 ? CarryingColor : SearchingColor;
        strength *= trailDecay;
        deposit(carrying != 0 ? foodPheromone_ : homePheromone_, ant.position,
                parameters_.depositRate * StepSeconds * strength);
    }
}

} // namespace emergences::scenarios::ants
