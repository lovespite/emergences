#include "scenarios/particle_life/ParticleLifeScene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <glm/geometric.hpp>

#include "scenarios/particle_life/ParticleLifeGpu.h"

namespace emergences::scenarios::particle_life {
namespace {

constexpr float StepSeconds = static_cast<float>(ParticleLifeScene::FixedStepSeconds);

void validateRange(float value, float minimum, float maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        throw std::invalid_argument("Particle life parameter is nonfinite or outside its allowed range");
    }
}

void validateCount(std::size_t count) {
    if (count > ParticleLifeScene::MaxParticles) {
        throw std::invalid_argument("Particle life count exceeds the 10000 particle limit");
    }
}

float unitRandom(std::mt19937& generator) {
    // Explicit mapping makes seeded results independent of distribution libraries.
    return static_cast<float>(generator() >> 8U) / 16777216.0F;
}

glm::vec3 limitSpeed(const glm::vec3& velocity, float maximum) {
    const double speed = glm::length(glm::dvec3{velocity});
    if (speed <= maximum) return velocity;
    auto limited = glm::vec3{glm::dvec3{velocity} * (maximum / speed)};
    // Float component rounding can put the length just above the requested
    // cap. Keep the scene's strict input invariant valid for its own snapshots.
    if (glm::length(glm::dvec3{limited}) > maximum) {
        limited *= 1.0F - 2.0F * std::numeric_limits<float>::epsilon();
    }
    return limited;
}

float radialForce(float distance, float attraction, float coreRatio, float repulsion) {
    if (distance >= 1.0F) return 0.0F;
    if (distance < coreRatio) return repulsion * (distance / coreRatio - 1.0F);
    // Tom Mohr's Particle Life force curve, with adjustable core repulsion:
    // https://particle-life.com/java-framework/getting-started.html
    return attraction * std::max(0.0F, 1.0F - std::abs(2.0F * distance - 1.0F - coreRatio) /
                                             (1.0F - coreRatio));
}

glm::vec3 overlapDirection(std::uint32_t receiver, std::uint32_t source, bool threeDimensional) {
    // A stable, antisymmetric direction separates coincident particles without
    // introducing frame-dependent randomness or array-order bias.
    const std::uint32_t low = std::min(receiver, source);
    const std::uint32_t high = std::max(receiver, source);
    std::uint32_t hash = low * 0x9E3779B9U ^ high * 0x85EBCA6BU;
    hash ^= hash >> 16U;
    hash *= 0x7FEB352DU;
    hash ^= hash >> 15U;
    if (threeDimensional) {
        constexpr std::array<glm::vec3, 6> directions{{
            {1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F},
            {0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
            {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, -1.0F}}};
        return directions[hash % directions.size()] * (receiver < source ? 1.0F : -1.0F);
    }
    constexpr float diagonal = 0.7071067811865475F;
    constexpr std::array<glm::vec3, 8> directions{{
        {1.0F, 0.0F, 0.0F}, {diagonal, 0.0F, diagonal}, {0.0F, 0.0F, 1.0F},
        {-diagonal, 0.0F, diagonal}, {-1.0F, 0.0F, 0.0F},
        {-diagonal, 0.0F, -diagonal}, {0.0F, 0.0F, -1.0F},
        {diagonal, 0.0F, -diagonal}}};
    return directions[hash % directions.size()] * (receiver < source ? 1.0F : -1.0F);
}

} // namespace

ParticleLifeScene::ParticleLifeScene(std::size_t count, std::uint32_t seed) : seed_{seed} {
    validateCount(count);
    randomizeMatrix(314159U);
    // Positive self-attraction gives the initial world visible groups while
    // cross-species forces remain independently sampled and asymmetric.
    for (std::size_t index = 0; index < MaxSpecies; ++index) {
        auto& value = matrix_[index * MaxSpecies + index];
        value = 0.4F + 0.5F * std::abs(value);
    }
    resetPopulation(count, speciesCount_, seed_, threeDimensional_);
}

ParticleLifeScene::~ParticleLifeScene() = default;

void ParticleLifeScene::initializeGpu(sim::ComputeDevice& device, std::string shaderPath) {
    if (backend_ != sim::Backend::CPU) {
        throw std::logic_error("Particle life GPU initialization requires the CPU backend");
    }
    auto next = std::make_unique<ParticleLifeGpu>(device, std::move(shaderPath));
    gpu_.swap(next);
}

void ParticleLifeScene::setBackend(sim::Backend backend) {
    if (backend != sim::Backend::CPU && backend != sim::Backend::GPU) {
        throw std::invalid_argument("Unknown particle life backend");
    }
    if (backend == backend_) return;
    if (backend == sim::Backend::GPU) {
        if (!gpu_) throw std::logic_error("Particle life GPU backend has not been initialized");
        gpu_->setState(agents_, species_);
    } else {
        auto nextAgents = gpu_->readback();
        if (nextAgents.size() != species_.size()) {
            throw std::runtime_error("Particle life GPU readback returned the wrong population size");
        }
        std::vector<glm::vec3> nextVelocities(nextAgents.size());
        agents_.swap(nextAgents);
        nextVelocities_.swap(nextVelocities);
    }
    backend_ = backend;
    accumulator_ = 0.0;
    lastAdvanceLimited_ = false;
}

sim::Backend ParticleLifeScene::backend() const noexcept { return backend_; }
bool ParticleLifeScene::gpuAvailable() const noexcept { return gpu_ != nullptr; }
sim::BufferHandle ParticleLifeScene::stateBuffer() const noexcept {
    return backend_ == sim::Backend::GPU ? gpu_->stateBuffer() : sim::BufferHandle{};
}
std::vector<sim::Agent> ParticleLifeScene::readback() const {
    return backend_ == sim::Backend::GPU ? gpu_->readback() : agents_;
}

bool ParticleLifeScene::threeDimensional() const noexcept { return threeDimensional_; }
void ParticleLifeScene::setThreeDimensional(bool enabled) {
    if (enabled != threeDimensional_) {
        resetPopulation(count(), speciesCount_, seed_, enabled);
    }
}

const ParticleLifeParameters& ParticleLifeScene::parameters() const noexcept { return parameters_; }

void ParticleLifeScene::setParameters(const ParticleLifeParameters& parameters) {
    validateRange(parameters.interactionRadius, 0.0F, WorldHalfExtent);
    if (parameters.interactionRadius == 0.0F) {
        throw std::invalid_argument("Particle life interaction radius must be positive");
    }
    validateRange(parameters.force, 0.0F, 200.0F);
    validateRange(parameters.friction, 0.0F, 30.0F);
    validateRange(parameters.repulsion, 0.0F, 10.0F);
    validateRange(parameters.coreRatio, 0.01F, 0.95F);
    validateRange(parameters.maxSpeed, 0.0F, 100.0F);
    if (backend_ == sim::Backend::GPU) {
        if (parameters.maxSpeed < parameters_.maxSpeed) gpu_->clampSpeed(parameters.maxSpeed);
    } else {
        for (auto& agent : agents_) agent.velocity = limitSpeed(agent.velocity, parameters.maxSpeed);
    }
    parameters_ = parameters;
}

std::size_t ParticleLifeScene::count() const noexcept { return agents_.size(); }
void ParticleLifeScene::setCount(std::size_t count) {
    validateCount(count);
    if (count != agents_.size()) {
        resetPopulation(count, speciesCount_, seed_, threeDimensional_);
    }
}

std::size_t ParticleLifeScene::speciesCount() const noexcept { return speciesCount_; }
void ParticleLifeScene::setSpeciesCount(std::size_t count) {
    if (count == 0 || count > MaxSpecies) {
        throw std::invalid_argument("Particle life species count must be between one and eight");
    }
    if (count != speciesCount_) {
        resetPopulation(agents_.size(), count, seed_, threeDimensional_);
    }
}

std::uint32_t ParticleLifeScene::seed() const noexcept { return seed_; }
void ParticleLifeScene::setSeed(std::uint32_t seed) {
    resetPopulation(agents_.size(), speciesCount_, seed, threeDimensional_);
}

void ParticleLifeScene::resetClock() noexcept {
    accumulator_ = 0.0;
    totalSteps_ = 0;
    lastAdvanceLimited_ = false;
}

void ParticleLifeScene::reset() {
    resetPopulation(agents_.size(), speciesCount_, seed_, threeDimensional_);
}

void ParticleLifeScene::resetPopulation(std::size_t count, std::size_t speciesCount,
                                       std::uint32_t seed, bool threeDimensional) {
    std::mt19937 generator{seed};
    std::vector<sim::Agent> nextAgents(count);
    std::vector<std::uint8_t> nextSpecies(count);
    std::vector<glm::vec3> nextVelocities(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto type = static_cast<std::uint8_t>(index % speciesCount);
        nextSpecies[index] = type;
        auto& agent = nextAgents[index];
        agent.id = static_cast<std::uint32_t>(index);
        agent.position = {(unitRandom(generator) * 2.0F - 1.0F) * WorldHalfExtent,
                          0.0F, (unitRandom(generator) * 2.0F - 1.0F) * WorldHalfExtent};
        if (threeDimensional) {
            agent.position.y = (unitRandom(generator) * 2.0F - 1.0F) * WorldHalfExtent;
        }
        agent.color = speciesColor(type);
        agent.size = 0.7F;
    }
    if (backend_ == sim::Backend::GPU) gpu_->setState(nextAgents, nextSpecies);
    agents_.swap(nextAgents);
    species_.swap(nextSpecies);
    nextVelocities_.swap(nextVelocities);
    speciesCount_ = speciesCount;
    seed_ = seed;
    threeDimensional_ = threeDimensional;
    world_.minimum.y = threeDimensional ? -WorldHalfExtent : -1.0F;
    world_.maximum.y = threeDimensional ? WorldHalfExtent : 1.0F;
    resetClock();
}

std::size_t ParticleLifeScene::advance(double elapsedSeconds, double workBudgetSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) {
        throw std::invalid_argument("Scene elapsed time must be finite and nonnegative");
    }
    if (std::isnan(workBudgetSeconds) || workBudgetSeconds < 0.0) {
        throw std::invalid_argument("Scene work budget must be nonnegative or positive infinity");
    }
    lastAdvanceLimited_ = false;
    if (paused_) return 0;
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

void ParticleLifeScene::setPaused(bool paused) noexcept {
    lastAdvanceLimited_ = false;
    if (paused_ != paused) {
        accumulator_ = 0.0;
        paused_ = paused;
    }
}
bool ParticleLifeScene::paused() const noexcept { return paused_; }
bool ParticleLifeScene::singleStep() {
    lastAdvanceLimited_ = false;
    if (!paused_) return false;
    step();
    ++totalSteps_;
    return true;
}
std::uint64_t ParticleLifeScene::totalSteps() const noexcept { return totalSteps_; }
double ParticleLifeScene::elapsedSimulationSeconds() const noexcept {
    return static_cast<double>(totalSteps_) * FixedStepSeconds;
}
bool ParticleLifeScene::lastAdvanceLimited() const noexcept { return lastAdvanceLimited_; }

std::span<const sim::Agent> ParticleLifeScene::agents() const noexcept {
    return backend_ == sim::Backend::GPU ? std::span<const sim::Agent>{} : agents_;
}
std::span<const std::uint8_t> ParticleLifeScene::species() const noexcept { return species_; }
const sim::World& ParticleLifeScene::world() const noexcept { return world_; }
const std::array<float, ParticleLifeScene::MaxSpecies * ParticleLifeScene::MaxSpecies>&
ParticleLifeScene::matrix() const noexcept { return matrix_; }

void ParticleLifeScene::setInteraction(std::size_t receiver, std::size_t source, float value) {
    if (receiver >= MaxSpecies || source >= MaxSpecies) {
        throw std::invalid_argument("Particle life interaction species index exceeds matrix bounds");
    }
    validateRange(value, -1.0F, 1.0F);
    matrix_[receiver * MaxSpecies + source] = value;
}
void ParticleLifeScene::setMatrix(const std::array<float, MaxSpecies * MaxSpecies>& matrix) {
    for (float value : matrix) validateRange(value, -1.0F, 1.0F);
    matrix_ = matrix;
}
void ParticleLifeScene::randomizeMatrix(std::uint32_t seed) {
    std::mt19937 generator{seed};
    for (auto& value : matrix_) value = 2.0F * unitRandom(generator) - 1.0F;
}

glm::vec3 ParticleLifeScene::speciesColor(std::size_t index) noexcept {
    constexpr std::array<glm::vec3, MaxSpecies> colors{{
        {1.0F, 0.30F, 0.38F}, {0.30F, 0.86F, 1.0F}, {1.0F, 0.79F, 0.22F},
        {0.45F, 1.0F, 0.55F}, {0.76F, 0.43F, 1.0F}, {1.0F, 0.48F, 0.80F},
        {1.0F, 0.53F, 0.20F}, {0.76F, 0.91F, 0.91F}}};
    return colors[index % colors.size()];
}

void ParticleLifeScene::setAgents(std::span<const sim::Agent> agents,
                                  std::span<const std::uint8_t> species) {
    validateCount(agents.size());
    if (agents.size() != species.size()) {
        throw std::invalid_argument("Particle life agents and species must have matching lengths");
    }
    std::unordered_set<std::uint32_t> ids;
    ids.reserve(agents.size());
    for (std::size_t index = 0; index < agents.size(); ++index) {
        const auto& agent = agents[index];
        if (species[index] >= speciesCount_ || !ids.insert(agent.id).second ||
            (!threeDimensional_ && (agent.position.y != 0.0F || agent.velocity.y != 0.0F)) ||
            !std::isfinite(agent.size) || agent.size <= 0.0F || !std::isfinite(agent.reserved)) {
            throw std::invalid_argument("Invalid particle life particle metadata or nonplanar state");
        }
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(agent.position[axis]) || !std::isfinite(agent.velocity[axis]) ||
                agent.position[axis] < world_.minimum[axis] ||
                agent.position[axis] >= world_.maximum[axis]) {
                throw std::invalid_argument("Particle life particles must be finite and inside the world");
            }
            validateRange(agent.color[axis], 0.0F, 1.0F);
        }
        if (glm::length(glm::dvec3{agent.velocity}) > parameters_.maxSpeed) {
            throw std::invalid_argument("Particle life particle velocity exceeds maximum speed");
        }
    }
    std::vector<sim::Agent> nextAgents{agents.begin(), agents.end()};
    std::vector<std::uint8_t> nextSpecies{species.begin(), species.end()};
    std::vector<glm::vec3> nextVelocities(agents.size());
    for (std::size_t index = 0; index < nextAgents.size(); ++index) {
        nextAgents[index].color = speciesColor(nextSpecies[index]);
    }
    if (backend_ == sim::Backend::GPU) gpu_->setState(nextAgents, nextSpecies);
    agents_.swap(nextAgents);
    species_.swap(nextSpecies);
    nextVelocities_.swap(nextVelocities);
    resetClock();
}

float ParticleLifeScene::interactionForce(float normalizedDistance, float attraction,
                                         float coreRatio, float repulsion) {
    if (!std::isfinite(normalizedDistance) || normalizedDistance < 0.0F ||
        !std::isfinite(coreRatio) || coreRatio <= 0.0F || coreRatio >= 1.0F ||
        !std::isfinite(repulsion) || repulsion < 0.0F) {
        throw std::invalid_argument("Invalid particle life force curve input");
    }
    validateRange(attraction, -1.0F, 1.0F);
    return radialForce(normalizedDistance, attraction, coreRatio, repulsion);
}

void ParticleLifeScene::step() {
    if (backend_ == sim::Backend::GPU) {
        gpu_->step(StepSeconds, parameters_, matrix_, threeDimensional_);
        return;
    }
    grid_.rebuild(agents_, world_, parameters_.interactionRadius);
    const float damping = std::exp(-parameters_.friction * StepSeconds);
    for (std::size_t index = 0; index < agents_.size(); ++index) {
        grid_.query(index, parameters_.interactionRadius, neighbors_);
        // Double accumulation also limits rounding differences when neighboring
        // cells or particle array order change.
        glm::dvec3 acceleration{0.0};
        for (const auto& neighbor : neighbors_) {
            const auto source = static_cast<std::size_t>(neighbor.agent - agents_.data());
            const float distance = static_cast<float>(std::sqrt(neighbor.distanceSquared));
            const auto direction = distance > 0.0F ? *neighbor.displacement / distance
                                                  : overlapDirection(agents_[index].id, neighbor.agent->id,
                                                                     threeDimensional_);
            const float force = radialForce(distance / parameters_.interactionRadius,
                matrix_[species_[index] * MaxSpecies + species_[source]],
                parameters_.coreRatio, parameters_.repulsion);
            acceleration += glm::dvec3{direction} * static_cast<double>(force * parameters_.force);
        }
        const auto velocity = (agents_[index].velocity +
                               glm::vec3{acceleration * static_cast<double>(StepSeconds)}) * damping;
        nextVelocities_[index] = limitSpeed(velocity, parameters_.maxSpeed);
    }
    // All forces observe the same snapshot before any position is integrated.
    for (std::size_t index = 0; index < agents_.size(); ++index) {
        auto& agent = agents_[index];
        agent.velocity = nextVelocities_[index];
        agent.position += agent.velocity * StepSeconds;
        world_.applyBoundary(agent.position, agent.velocity);
    }
}

} // namespace emergences::scenarios::particle_life
