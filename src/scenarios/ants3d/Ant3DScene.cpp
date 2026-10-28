#include "scenarios/ants3d/Ant3DScene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace emergences::scenarios::ants3d {
namespace {
constexpr float Pi = 3.14159265358979323846F;
constexpr float CellSize = 1.2F;
constexpr int GridSide = static_cast<int>(2.0F * Ant3DScene::WorldHalfExtent / CellSize) + 1;
constexpr std::size_t MaxTrailPoints = 1800;
constexpr glm::vec3 AntColor{0.0F};

glm::vec3 horizontalDirection(glm::vec3 delta) noexcept {
    delta.y = 0.0F;
    const float length = glm::length(delta);
    return length > 1.0e-6F ? delta / length : glm::vec3{0.0F};
}
} // namespace

Ant3DScene::Ant3DScene(std::size_t count, std::uint32_t seed)
    : terrain_(seed), seed_(seed), random_(seed) {
    if (count > MaxAnts) throw std::invalid_argument("3D ant count exceeds 3000");
    agents_.resize(count);
    reset();
}

std::size_t Ant3DScene::count() const noexcept { return agents_.size(); }
std::uint32_t Ant3DScene::seed() const noexcept { return seed_; }
void Ant3DScene::setSeed(std::uint32_t seed) { randomize(seed); }
void Ant3DScene::randomize(std::uint32_t seed) {
    if (terrain_.seed() != seed) terrain_.regenerate(seed);
    seed_ = seed;
    reset();
}
void Ant3DScene::setCount(std::size_t count) {
    if (count > MaxAnts) throw std::invalid_argument("3D ant count exceeds 3000");
    if (count == agents_.size()) return;
    agents_.resize(count);
    reset();
}

float Ant3DScene::movementSpeed() const noexcept { return movementSpeed_; }
void Ant3DScene::setMovementSpeed(float speed) {
    if (!std::isfinite(speed) || speed < MinimumMovementSpeed || speed > MaximumMovementSpeed)
        throw std::invalid_argument("3D ant movement speed must be between 0.5 and 20");
    movementSpeed_ = speed;
}
std::size_t Ant3DScene::nestExitCount() const noexcept { return exitGates_.size(); }
void Ant3DScene::setNestExitCount(std::size_t count) {
    if (count < 1 || count > MaximumNestGates) throw std::invalid_argument("3D nest exit count must be between 1 and 4");
    if (count == exitGates_.size()) return;
    exitGates_.resize(count);
    reset();
}
std::size_t Ant3DScene::nestEntryCount() const noexcept { return entryGates_.size(); }
void Ant3DScene::setNestEntryCount(std::size_t count) {
    if (count < 1 || count > MaximumNestGates) throw std::invalid_argument("3D nest entry count must be between 1 and 4");
    if (count == entryGates_.size()) return;
    entryGates_.resize(count);
    reset();
}
float Ant3DScene::foodSpawnInterval() const noexcept { return foodSpawnInterval_; }
void Ant3DScene::setFoodSpawnInterval(float seconds) {
    if (!std::isfinite(seconds) || seconds < 1.0F || seconds > 120.0F)
        throw std::invalid_argument("3D food generation interval must be between 1 and 120 seconds");
    if (seconds == foodSpawnInterval_) return;
    foodSpawnInterval_ = seconds;
    foodSpawnElapsed_ = 0.0;
}

void Ant3DScene::resetClock() noexcept { accumulator_ = 0.0; totalSteps_ = 0; lastAdvanceLimited_ = false; }

void Ant3DScene::reset() {
    ++generation_;
    random_.seed(seed_);
    resetClock();
    deliveredFood_ = 0;
    totalGeneratedFood_ = 0;
    foodSpawnElapsed_ = 0.0;
    collisionContacts_ = 0;
    carryingFood_.assign(count(), 0);
    activities_.assign(count(), AntActivity::Queued);
    cooldownRemaining_.assign(count(), 0.0F);
    exitQueue_.clear();
    std::fill(exitGates_.begin(), exitGates_.end(), NestGate{});
    std::fill(entryGates_.begin(), entryGates_.end(), NestGate{});
    exitSequences_.assign(count(), 0);
    lastExitSequence_ = 0;
    foodTargets_.assign(count(), NoFood);
    wanderRandom_.resize(count());
    explorationHeadings_.assign(count(), glm::vec3{1, 0, 0});
    explorationRemaining_.assign(count(), 0.0F);
    wanderPhases_.resize(count());
    routeTargets_.resize(count());
    gridNext_.resize(count());
    gridHeads_.assign(GridSide * GridSide, -1);
    occupiedCells_.clear();
    occupiedCells_.reserve(count());
    pheromoneTrail_.clear();
    foodSources_.clear();
    std::uniform_real_distribution<float> foodJitter(-1.2F, 1.2F);
    const Cave& cave = terrain_.caves().front();
    const auto entrance = *std::min_element(terrain_.caveEntrances().begin(), terrain_.caveEntrances().end(),
        [](glm::vec3 a, glm::vec3 b) { return a.x < b.x; });
    const float left = entrance.x;
    const float right = cave.center.x + cave.halfLength;
    if (!navigationReady_ || navigationSeed_ != seed_) {
        navigation_.rebuild(terrain_, AntRadius);
        navigationSeed_ = seed_;
        navigationReady_ = true;
    }
    // A navigable home cell alone does not guarantee room for all nest
    // lanes. Move the nest west when a seeded shore or cave edge clips any
    // part of a traversal; gate occupants follow these reserved paths exactly.
    for (int offset = 0; offset <= 40; offset += 2) {
        nestPosition_ = navigation_.setHome(entrance + glm::vec3{-10.0F - static_cast<float>(offset), 0.0F, 0.0F});
        nestPosition_.y = terrain_.navigationHeight(nestPosition_.x, nestPosition_.z) + 0.08F;
        bool clear = true;
        for (int sample = 0; sample <= 56 && clear; ++sample) {
            const float progress = static_cast<float>(sample) / 56.0F;
            for (std::size_t gate = 0; gate < nestExitCount() && clear; ++gate)
                clear = terrain_.sphereClear(gatePosition(true, progress, gate), AntRadius);
            for (std::size_t gate = 0; gate < nestEntryCount() && clear; ++gate)
                clear = terrain_.sphereClear(gatePosition(false, progress, gate), AntRadius);
        }
        if (clear) break;
    }
    // Food can be discovered in caves and across bridges during exploration.
    // Keep every patch on the nest's connected component.
    for (glm::vec3 position : {
             terrain_.caveChambers().front().center,
             {right + 16.0F, 0.0F, cave.center.z + 26.0F},
             {left - 10.0F, 0.0F, cave.center.z - 25.0F},
             {right + 19.0F, 0.0F, cave.center.z - 35.0F},
             {-WorldHalfExtent * 0.82F, 0.0F, -WorldHalfExtent * 0.45F},
             {WorldHalfExtent * 0.82F, 0.0F, -WorldHalfExtent * 0.45F},
             {-WorldHalfExtent * 0.82F, 0.0F, WorldHalfExtent * 0.45F},
             {WorldHalfExtent * 0.82F, 0.0F, WorldHalfExtent * 0.45F}}) {
        position.x += foodJitter(random_); position.z += foodJitter(random_);
        position = navigation_.closestPoint(position);
        position.y = terrain_.navigationHeight(position.x, position.z) + 0.08F;
        foodSources_.push_back({position, 2.4F, 1200});
        totalGeneratedFood_ += 1200;
    }
    updateNavigationDestinations();
    // The colony starts inside the nest. Hidden occupants share its location
    // without taking part in rendering, navigation, or body collisions.
    for (std::size_t i = 0; i < count(); ++i) {
        sim::Agent ant;
        ant.id = static_cast<std::uint32_t>(i);
        ant.size = 2.0F * AntRadius;
        ant.position = nestPosition_;
        ant.color = AntColor;
        agents_[i] = ant;
        exitQueue_.push_back(i);
        // Per-ant streams keep exploration independent of unseen food generation.
        wanderRandom_[i] = seed_ ^ (static_cast<std::uint32_t>(i) * 0x9e3779b9U) ^ 0x85ebca6bU;
        wanderPhases_[i] = 2.0F * Pi * nextWanderRandom(i);
        routeTargets_[i] = ant.position;
    }
}

void Ant3DScene::setPaused(bool paused) noexcept { paused_ = paused; accumulator_ = 0.0; }
bool Ant3DScene::paused() const noexcept { return paused_; }
bool Ant3DScene::singleStep() {
    if (!paused_) return false;
    step(); ++totalSteps_; lastAdvanceLimited_ = false;
    return true;
}

std::size_t Ant3DScene::advance(double elapsedSeconds, double workBudgetSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 || std::isnan(workBudgetSeconds) || workBudgetSeconds < 0.0)
        throw std::invalid_argument("3D ant elapsed time and budget must be nonnegative");
    lastAdvanceLimited_ = false;
    if (paused_) return 0;
    // Bound before summing to prevent overflow from arbitrarily large valid
    // frame gaps. Fractional time is retained; dropped full steps are reported.
    const double maximum = FixedStepSeconds * static_cast<double>(MaxStepsPerAdvance);
    if (elapsedSeconds > maximum) lastAdvanceLimited_ = true;
    accumulator_ += std::min(elapsedSeconds, maximum);
    const auto start = std::chrono::steady_clock::now();
    std::size_t completed = 0;
    while (accumulator_ + 1.0e-12 >= FixedStepSeconds && completed < MaxStepsPerAdvance) {
        step(); ++totalSteps_; ++completed;
        accumulator_ = std::max(0.0, accumulator_ - FixedStepSeconds);
        if (completed > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= workBudgetSeconds) {
            if (accumulator_ + 1.0e-12 >= FixedStepSeconds) lastAdvanceLimited_ = true;
            break;
        }
    }
    if (accumulator_ + 1.0e-12 >= FixedStepSeconds) {
        accumulator_ = std::fmod(accumulator_, FixedStepSeconds);
        lastAdvanceLimited_ = true;
    }
    return completed;
}

std::uint64_t Ant3DScene::totalSteps() const noexcept { return totalSteps_; }
std::uint64_t Ant3DScene::generation() const noexcept { return generation_; }
double Ant3DScene::elapsedSimulationSeconds() const noexcept { return static_cast<double>(totalSteps_) * FixedStepSeconds; }
bool Ant3DScene::lastAdvanceLimited() const noexcept { return lastAdvanceLimited_; }
std::span<const sim::Agent> Ant3DScene::agents() const noexcept { return agents_; }
std::span<const std::uint8_t> Ant3DScene::carryingFood() const noexcept { return carryingFood_; }
std::span<const AntActivity> Ant3DScene::activities() const noexcept { return activities_; }
bool Ant3DScene::active(std::size_t index) const noexcept {
    return index < activities_.size() && activities_[index] != AntActivity::Queued &&
           activities_[index] != AntActivity::CoolingDown;
}
std::optional<std::size_t> Ant3DScene::latestExitedAnt() const noexcept {
    std::optional<std::size_t> latest;
    std::uint64_t sequence = 0;
    for (std::size_t i = 0; i < count(); ++i) {
        // A new departure only becomes eligible after it has left the gate.
        if (activities_[i] == AntActivity::Leaving || !active(i) || exitSequences_[i] <= sequence) continue;
        latest = i;
        sequence = exitSequences_[i];
    }
    return latest;
}
std::span<const float> Ant3DScene::cooldownRemaining() const noexcept { return cooldownRemaining_; }
std::span<const FoodSource> Ant3DScene::foodSources() const noexcept { return foodSources_; }
std::span<const TrailPoint> Ant3DScene::pheromoneTrail() const noexcept { return pheromoneTrail_; }
glm::vec3 Ant3DScene::nestPosition() const noexcept { return nestPosition_; }
glm::vec3 Ant3DScene::nestExitPosition(std::size_t gate) const noexcept { return gatePosition(true, 1.0F, gate); }
glm::vec3 Ant3DScene::nestEntryPosition(std::size_t gate) const noexcept { return gatePosition(false, 0.0F, gate); }
float Ant3DScene::nestRadius() const noexcept { return 3.2F; }
std::size_t Ant3DScene::deliveredFood() const noexcept { return deliveredFood_; }
std::size_t Ant3DScene::totalGeneratedFood() const noexcept { return totalGeneratedFood_; }
std::size_t Ant3DScene::collisionContacts() const noexcept { return collisionContacts_; }
const Terrain& Ant3DScene::terrain() const noexcept { return terrain_; }

void Ant3DScene::updateNavigationDestinations() {
    std::vector<glm::vec3> destinations;
    destinations.reserve(nestEntryCount());
    for (std::size_t gate = 0; gate < nestEntryCount(); ++gate) destinations.push_back(nestEntryPosition(gate));
    // Only homeward routes are known globally. Food is approached locally.
    navigation_.setDestinations(destinations);
}

void Ant3DScene::spawnFood() {
    std::uniform_real_distribution<float> coordinate(-WorldHalfExtent * 0.88F, WorldHalfExtent * 0.88F);
    for (int batch = 0; batch < 2; ++batch) {
        const auto empty = std::find_if(foodSources_.begin(), foodSources_.end(), [](const FoodSource& food) {
            return food.remaining == 0;
        });
        if (empty == foodSources_.end() && foodSources_.size() >= MaxFoodSources) break;
        for (int attempt = 0; attempt < 32; ++attempt) {
            auto position = navigation_.closestPoint({coordinate(random_), 0.0F, coordinate(random_)});
            position.y = terrain_.navigationHeight(position.x, position.z) + 0.08F;
            if (glm::length(position - nestPosition_) < 9.0F) continue;
            const bool separated = std::all_of(foodSources_.begin(), foodSources_.end(), [&](const FoodSource& food) {
                return food.remaining == 0 || glm::length(position - food.position) >= food.radius + 5.0F;
            });
            if (!separated) continue;
            const FoodSource food{position, 2.4F, 1200};
            if (empty == foodSources_.end()) foodSources_.push_back(food);
            else *empty = food;
            totalGeneratedFood_ += static_cast<std::size_t>(food.remaining);
            break;
        }
    }
}

float Ant3DScene::nextWanderRandom(std::size_t index) noexcept {
    auto value = (wanderRandom_[index] += 0x9e3779b9U);
    value = (value ^ (value >> 16U)) * 0x7feb352dU;
    value = (value ^ (value >> 15U)) * 0x846ca68bU;
    value ^= value >> 16U;
    return static_cast<float>(value >> 8U) / 16777216.0F;
}

std::size_t Ant3DScene::nearbyFood(glm::vec3 position) const noexcept {
    std::size_t nearest = NoFood;
    float nearestSquared = FoodDetectionRadius * FoodDetectionRadius;
    for (std::size_t index = 0; index < foodSources_.size(); ++index) {
        const auto& food = foodSources_[index];
        if (food.remaining <= 0) continue;
        const auto delta = food.position - position;
        const float squared = glm::dot(delta, delta);
        if (squared > nearestSquared || !navigation_.segmentClear(position, food.position)) continue;
        nearest = index;
        nearestSquared = squared;
    }
    return nearest;
}

std::size_t Ant3DScene::cellIndex(glm::vec3 position) const noexcept {
    const int x = std::clamp(static_cast<int>((position.x + WorldHalfExtent) / CellSize), 0, GridSide - 1);
    const int z = std::clamp(static_cast<int>((position.z + WorldHalfExtent) / CellSize), 0, GridSide - 1);
    return static_cast<std::size_t>(z * GridSide + x);
}

void Ant3DScene::rebuildGrid() {
    // Clearing only occupied buckets keeps contact solving proportional to the
    // colony size when the map grows, even during repeated solver passes.
    for (const auto cell : occupiedCells_) gridHeads_[cell] = -1;
    occupiedCells_.clear();
    for (std::size_t i = 0; i < count(); ++i) {
        if (!active(i)) continue;
        const std::size_t cell = cellIndex(agents_[i].position);
        if (gridHeads_[cell] < 0) occupiedCells_.push_back(cell);
        gridNext_[i] = gridHeads_[cell];
        gridHeads_[cell] = static_cast<int>(i);
    }
}

glm::vec3 Ant3DScene::routeTarget(glm::vec3 position, std::size_t index) const noexcept {
    if (!carryingFood_[index]) {
        if (foodTargets_[index] != NoFood) return foodSources_[foodTargets_[index]].position;
        return navigation_.explorationTarget(position, explorationHeadings_[index]);
    }
    const float variation = (static_cast<float>((index * 17U) % 19U) / 18.0F - 0.5F) * 1.2F;
    // Keep right of each path in both directions to separate returning streams.
    return navigation_.target(position, index % nestEntryCount(), 0.5F + variation);
}

void Ant3DScene::resolveAntCollisions() {
    constexpr float Diameter = 2.0F * AntRadius;
    collisionContacts_ = 0;
    // Rebuild each pass because terrain projection can move an ant across a
    // cell boundary. The narrow phase uses full 3D distances, separating ants
    // in a chamber from any ants walking on the rock above them.
    // Dense queues need more relaxation passes; ordinary contacts still stop
    // as soon as both penetration and grounded motion have converged.
    for (int pass = 0; pass < 256; ++pass) {
        rebuildGrid();
        bool collided = false;
        float maximumPenetration = 0.0F;
        float maximumDisplacement = 0.0F;
        for (std::size_t i = 0; i < count(); ++i) {
            if (!active(i)) continue;
            const int cell = static_cast<int>(cellIndex(agents_[i].position));
            const int cellX = cell % GridSide, cellZ = cell / GridSide;
            for (int z = std::max(0, cellZ - 1); z <= std::min(GridSide - 1, cellZ + 1); ++z) {
                for (int x = std::max(0, cellX - 1); x <= std::min(GridSide - 1, cellX + 1); ++x) {
                    for (int entry = gridHeads_[static_cast<std::size_t>(z * GridSide + x)]; entry >= 0;
                         entry = gridNext_[static_cast<std::size_t>(entry)]) {
                        const std::size_t j = static_cast<std::size_t>(entry);
                        if (j <= i) continue;
                        glm::vec3 delta = agents_[i].position - agents_[j].position;
                        const float squared = glm::dot(delta, delta);
                        if (squared >= Diameter * Diameter) continue;
                        const float distance = std::sqrt(squared);
                        maximumPenetration = std::max(maximumPenetration, Diameter - distance);
                        glm::vec3 normal = distance > 1.0e-6F ? delta / distance :
                            glm::vec3{std::cos(static_cast<float>(i) * 2.4F), 0.0F, std::sin(static_cast<float>(i) * 2.4F)};
                        // Ground constraints remove most vertical corrections.
                        // Dividing by horizontal length preserves the requested
                        // sphere separation on sloped surfaces.
                        const float horizontalSquared = std::max(0.15F, normal.x * normal.x + normal.z * normal.z);
                        // Nest traversals reserve their lane. Other ants yield
                        // rather than pushing a gate occupant out of its slot.
                        const bool fixedI = activities_[i] == AntActivity::Leaving || activities_[i] == AntActivity::Entering;
                        const bool fixedJ = activities_[j] == AntActivity::Leaving || activities_[j] == AntActivity::Entering;
                        const float weightI = fixedI ? 0.0F : (fixedJ ? 1.0F : 0.5F);
                        const float weightJ = fixedJ ? 0.0F : (fixedI ? 1.0F : 0.5F);
                        if (fixedI && fixedJ) continue;
                        const glm::vec3 correction = normal * ((Diameter - distance + 0.0003F) / horizontalSquared);
                        const auto beforeI = agents_[i].position;
                        const auto beforeJ = agents_[j].position;
                        agents_[i].position += correction * weightI; agents_[j].position -= correction * weightJ;
                        const float relative = glm::dot(agents_[i].velocity - agents_[j].velocity, normal);
                        if (relative < 0.0F) {
                            agents_[i].velocity -= normal * relative * weightI;
                            agents_[j].velocity += normal * relative * weightJ;
                        }
                        if (!fixedI) terrain_.resolveCollision(agents_[i].position, agents_[i].velocity, AntRadius);
                        if (!fixedJ) terrain_.resolveCollision(agents_[j].position, agents_[j].velocity, AntRadius);
                        maximumDisplacement = std::max({maximumDisplacement,
                            glm::length(agents_[i].position - beforeI), glm::length(agents_[j].position - beforeJ)});
                        collided = true;
                        if (pass == 0) ++collisionContacts_;
                    }
                }
            }
        }
        // A tiny horizontal correction can change the supporting ground plane
        // and create a new contact. Convergence includes the projected motion,
        // not only penetration measured before terrain constraints were solved.
        if (!collided || (maximumPenetration < 0.001F && maximumDisplacement < 0.001F)) break;
    }
}

glm::vec3 Ant3DScene::gatePosition(bool outbound, float progress, std::size_t gate) const noexcept {
    const float outward = outbound ? progress : 1.0F - progress;
    const float angle = static_cast<float>(gate) * (outbound ? 0.35F : -0.35F);
    const float x = 2.4F + 1.4F * outward, z = outbound ? 0.65F : -0.65F;
    glm::vec3 position = nestPosition_ + glm::vec3{
        x * std::cos(angle) - z * std::sin(angle), 0.0F,
        x * std::sin(angle) + z * std::cos(angle)};
    position.y = terrain_.navigationHeight(position.x, position.z) + AntRadius;
    position.y = terrain_.walkingHeight(position, AntRadius);
    return position;
}

void Ant3DScene::updateNest() {
    constexpr float Dt = static_cast<float>(FixedStepSeconds);
    for (std::size_t i = 0; i < count(); ++i) {
        if (activities_[i] != AntActivity::CoolingDown) continue;
        cooldownRemaining_[i] = std::max(0.0F, cooldownRemaining_[i] - Dt);
        if (cooldownRemaining_[i] == 0.0F) {
            activities_[i] = AntActivity::Queued;
            exitQueue_.push_back(i);
        }
    }
    const float speedScale = movementSpeed_ / DefaultMovementSpeed;
    // Stable gate order also defines which completion is newest when several
    // ants emerge in the same fixed step.
    for (std::size_t index = 0; index < exitGates_.size(); ++index) {
        auto& gate = exitGates_[index];
        if (gate.ant == NoAnt) continue;
        gate.progress = std::min(1.0F, gate.progress + Dt * speedScale / NestTraversalSeconds);
        auto& ant = agents_[gate.ant];
        const auto previous = ant.position;
        ant.position = gatePosition(true, gate.progress, index);
        ant.velocity = (ant.position - previous) / Dt;
        if (gate.progress >= 1.0F) {
            activities_[gate.ant] = AntActivity::Foraging;
            routeTargets_[gate.ant] = ant.position;
            exitSequences_[gate.ant] = ++lastExitSequence_;
            gate.ant = NoAnt;
        }
    }
    for (std::size_t index = 0; index < entryGates_.size(); ++index) {
        auto& gate = entryGates_[index];
        if (gate.ant == NoAnt) continue;
        // Join the reserved lane from the body's actual position, then walk
        // through it. Admission must not teleport a returning ant to the gate.
        const float length = gate.approachLength + 1.4F;
        const float duration = length * NestTraversalSeconds / (1.4F * speedScale);
        gate.progress = std::min(1.0F, gate.progress + Dt / duration);
        auto& ant = agents_[gate.ant];
        const auto previous = ant.position;
        const float distance = gate.progress * length;
        if (distance < gate.approachLength) {
            ant.position = glm::mix(gate.approachStart, nestEntryPosition(index), distance / gate.approachLength);
            ant.position.y = terrain_.navigationHeight(ant.position.x, ant.position.z) + AntRadius;
            ant.position.y = terrain_.walkingHeight(ant.position, AntRadius);
        } else {
            ant.position = gatePosition(false, (distance - gate.approachLength) / 1.4F, index);
        }
        ant.velocity = (ant.position - previous) / Dt;
        if (gate.progress >= 1.0F) {
            ++deliveredFood_;
            carryingFood_[gate.ant] = 0;
            activities_[gate.ant] = AntActivity::CoolingDown;
            cooldownRemaining_[gate.ant] = std::uniform_real_distribution<float>(
                MinimumCooldownSeconds, MaximumCooldownSeconds)(random_);
            ant.position = nestPosition_;
            ant.velocity = glm::vec3{0.0F};
            gate.ant = NoAnt;
        }
    }
    for (std::size_t index = 0; index < exitGates_.size(); ++index) {
        auto& gate = exitGates_[index];
        if (gate.ant != NoAnt || exitQueue_.empty()) continue;
        const auto start = gatePosition(true, 0.0F, index);
        bool clear = terrain_.sphereClear(start, AntRadius);
        for (std::size_t i = 0; i < count() && clear; ++i)
            if (active(i) && glm::length(agents_[i].position - start) < 2.0F * AntRadius + 0.02F) clear = false;
        if (clear) {
            gate.ant = exitQueue_.front();
            exitQueue_.pop_front();
            foodTargets_[gate.ant] = NoFood;
            const float heading = 2.0F * Pi * nextWanderRandom(gate.ant);
            explorationHeadings_[gate.ant] = {std::cos(heading), 0.0F, std::sin(heading)};
            explorationRemaining_[gate.ant] = 1.5F + 2.5F * nextWanderRandom(gate.ant);
            gate.progress = 0.0F;
            activities_[gate.ant] = AntActivity::Leaving;
            agents_[gate.ant].position = start;
            agents_[gate.ant].velocity = (gatePosition(true, 1.0F, index) - start) * (speedScale / NestTraversalSeconds);
        }
    }
}

void Ant3DScene::step() {
    constexpr float Dt = static_cast<float>(FixedStepSeconds);
    const float time = static_cast<float>(elapsedSimulationSeconds());
    foodSpawnElapsed_ += FixedStepSeconds;
    if (foodSpawnElapsed_ + 1.0e-10 >= static_cast<double>(foodSpawnInterval_)) {
        foodSpawnElapsed_ = std::max(0.0, foodSpawnElapsed_ - static_cast<double>(foodSpawnInterval_));
        spawnFood();
    }
    for (auto& trail : pheromoneTrail_) trail.strength *= 0.9984F;
    std::erase_if(pheromoneTrail_, [](const TrailPoint& trail) { return trail.strength < 0.08F; });

    updateNest();

    for (std::size_t i = 0; i < count(); ++i) {
        if (activities_[i] != AntActivity::Foraging && activities_[i] != AntActivity::Returning) continue;
        sim::Agent& ant = agents_[i];
        const auto oldCarrying = carryingFood_[i];
        const auto oldTarget = foodTargets_[i];
        bool refreshRoute = (totalSteps_ + i) % 8 == 0 || glm::length(routeTargets_[i] - ant.position) < 0.8F;
        if (!carryingFood_[i]) {
            if (foodTargets_[i] != NoFood) {
                const auto& food = foodSources_[foodTargets_[i]];
                const auto delta = food.position - ant.position;
                if (food.remaining <= 0 || glm::dot(delta, delta) > FoodDetectionRadius * FoodDetectionRadius)
                    foodTargets_[i] = NoFood;
            }
            if (refreshRoute || oldTarget != foodTargets_[i]) foodTargets_[i] = nearbyFood(ant.position);
            if (foodTargets_[i] != NoFood) {
                auto& food = foodSources_[foodTargets_[i]];
                // Crowds can displace a body after its last sensing update.
                // Recheck the path at pickup so cached targets cannot grant
                // food through a wall or from a different walking layer.
                if (glm::length(ant.position - food.position) < food.radius + AntRadius &&
                    navigation_.segmentClear(ant.position, food.position)) {
                    --food.remaining;
                    carryingFood_[i] = 1;
                    activities_[i] = AntActivity::Returning;
                    foodTargets_[i] = NoFood;
                }
            } else {
                explorationRemaining_[i] -= Dt;
                if (explorationRemaining_[i] <= 0.0F) {
                    const float turn = (nextWanderRandom(i) - 0.5F) * 2.4F;
                    const auto heading = explorationHeadings_[i];
                    explorationHeadings_[i] = {
                        heading.x * std::cos(turn) - heading.z * std::sin(turn), 0.0F,
                        heading.x * std::sin(turn) + heading.z * std::cos(turn)};
                    explorationRemaining_[i] = 1.5F + 2.5F * nextWanderRandom(i);
                    refreshRoute = true;
                }
            }
        }
        // Keep terrain navigation all the way to the actual entry. Fixed
        // waiting slots can cross cave walls and strand a whole arrival queue.
        if (refreshRoute || oldCarrying != carryingFood_[i] || oldTarget != foodTargets_[i]) {
            routeTargets_[i] = routeTarget(ant.position, i);
            if (!carryingFood_[i] && foodTargets_[i] == NoFood) {
                const auto heading = horizontalDirection(routeTargets_[i] - ant.position);
                if (glm::dot(heading, heading) > 0.0F) explorationHeadings_[i] = heading;
            }
        }
        glm::vec3 direction = horizontalDirection(routeTargets_[i] - ant.position);
        // Small steering variation sits on top of independent exploration,
        // locally sensed food, or the known homeward route.
        const float entryDistance = glm::length(nestEntryPosition(i % nestEntryCount()) - ant.position);
        const float wander = carryingFood_[i] && entryDistance < 4.0F ? 0.0F :
            std::sin(time * 1.6F + wanderPhases_[i]) * (carryingFood_[i] ? 0.09F : 0.23F);
        direction = horizontalDirection(direction + glm::vec3{-direction.z, 0.0F, direction.x} * wander);
        const glm::vec3 normal = terrain_.surfaceNormal(ant.position);
        direction.y = -(normal.x * direction.x + normal.z * direction.z) / std::max(0.25F, normal.y);
        // A blocked route returns the current position. Stop there instead of
        // inventing a heading that pushes the ant farther into the obstacle.
        if (glm::dot(direction, direction) > 1.0e-12F) direction = glm::normalize(direction);
        const float speed = carryingFood_[i] ?
            std::min(5.4F, entryDistance * 4.0F) * (movementSpeed_ / DefaultMovementSpeed) : movementSpeed_;
        ant.velocity += (direction * speed - ant.velocity) * 0.12F;
        ant.position += ant.velocity * Dt;
        terrain_.resolveCollision(ant.position, ant.velocity, AntRadius);
    }
    resolveAntCollisions();
    for (std::size_t index = 0; index < entryGates_.size(); ++index) {
        auto& gate = entryGates_[index];
        if (gate.ant != NoAnt) continue;
        const auto entry = nestEntryPosition(index);
        // Admit a body that has physically reached the entrance, not a FIFO
        // head stranded behind the crowd. The reserved occupant keeps its
        // collision volume and other ants yield while it traverses the lane.
        std::size_t next = NoAnt;
        // A contact ring can leave the exact gate center empty. Capture a
        // body within one diameter so such a ring cannot block admission.
        float closest = 2.0F * AntRadius + 0.04F;
        for (std::size_t i = 0; i < count(); ++i) {
            if (activities_[i] != AntActivity::Returning || i % nestEntryCount() != index) continue;
            const float distance = glm::length(agents_[i].position - entry);
            if (distance < closest && navigation_.segmentClear(agents_[i].position, entry)) {
                next = i;
                closest = distance;
            }
        }
        if (next != NoAnt) {
            gate.ant = next;
            gate.progress = 0.0F;
            gate.approachStart = agents_[next].position;
            gate.approachLength = closest;
            activities_[next] = AntActivity::Entering;
        }
    }
    // Only laden ants deposit food trails; samples stay on the actual walked
    // layer, including inside the cave, and fade in simulated time.
    if (totalSteps_ % 12 == 0) {
        for (std::size_t i = totalSteps_ % 7; i < count(); i += 7) {
            if (!active(i) || !carryingFood_[i]) continue;
            pheromoneTrail_.push_back({agents_[i].position, 1.0F});
        }
        if (pheromoneTrail_.size() > MaxTrailPoints)
            pheromoneTrail_.erase(pheromoneTrail_.begin(), pheromoneTrail_.begin() +
                                  static_cast<std::ptrdiff_t>(pheromoneTrail_.size() - MaxTrailPoints));
    }
}

} // namespace emergences::scenarios::ants3d
