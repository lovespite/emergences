#include "scenarios/particle_life/ParticleLifeScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {

using emergences::sim::Agent;
using emergences::scenarios::particle_life::ParticleLifeParameters;
using emergences::scenarios::particle_life::ParticleLifeScene;
constexpr float Step = static_cast<float>(ParticleLifeScene::FixedStepSeconds);

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Function>
void requireInvalid(Function&& action, const char* message) {
    bool rejected = false;
    try { action(); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}

bool close(float left, float right, float tolerance = 1.0e-5F) {
    return std::abs(left - right) <= tolerance;
}

bool sameAgent(const Agent& left, const Agent& right) {
    return left.id == right.id && left.position == right.position &&
           left.velocity == right.velocity && left.color == right.color &&
           left.size == right.size && left.reserved == right.reserved;
}

std::vector<Agent> copyAgents(const ParticleLifeScene& scene) {
    return {scene.agents().begin(), scene.agents().end()};
}

void requireSameAgents(const std::vector<Agent>& expected, const ParticleLifeScene& scene) {
    require(std::equal(expected.begin(), expected.end(), scene.agents().begin(),
                       scene.agents().end(), sameAgent), "Particle states must match exactly");
}

Agent particle(std::uint32_t id, float x, float z = 0.0F) {
    Agent agent;
    agent.id = id;
    agent.position = {x, 0.0F, z};
    return agent;
}

void prepare(ParticleLifeScene& scene) {
    ParticleLifeParameters parameters;
    parameters.interactionRadius = 10.0F;
    parameters.force = 12.0F;
    parameters.friction = 0.0F;
    parameters.maxSpeed = 100.0F;
    scene.setParameters(parameters);
    scene.setMatrix({});
}

void ticks(ParticleLifeScene& scene, std::size_t count) {
    for (std::size_t tick = 0; tick < count; ++tick) {
        require(scene.advance(ParticleLifeScene::FixedStepSeconds) == 1,
                "A fixed timestep must run exactly one step");
    }
}

void forceCurve() {
    const auto force = [](float radius, float attraction) {
        return ParticleLifeScene::interactionForce(radius, attraction, 0.3F, 2.0F);
    };
    require(force(0.0F, 1.0F) == -2.0F && close(force(0.15F, 1.0F), -1.0F),
            "The core must repel linearly even for mutually attracting species");
    require(close(force(0.3F, 1.0F), 0.0F) && close(force(0.65F, 0.8F), 0.8F) &&
                close(force(0.825F, 0.8F), 0.4F),
            "The outer force must form the expected triangular attraction curve");
    require(close(force(0.65F, -0.4F), -0.4F) && force(1.0F, 1.0F) == 0.0F &&
                force(2.0F, 1.0F) == 0.0F && force(0.15F, -1.0F) == force(0.15F, 1.0F),
            "The matrix controls outer attraction and repulsion, with finite support");
    requireInvalid([&] { force(-1.0F, 1.0F); }, "Negative distance must be rejected");
    requireInvalid([&] { force(0.5F, 2.0F); }, "Out-of-range attraction must be rejected");
    requireInvalid([&] { (void)ParticleLifeScene::interactionForce(0.5F, 1.0F, 1.0F, 2.0F); },
                   "A singular core ratio must be rejected");
}

void pairForcesAndMatrixDirection() {
    ParticleLifeScene scene{0};
    prepare(scene);
    const std::array agents{particle(10, -3.25F), particle(20, 3.25F)};
    const std::array<std::uint8_t, 2> species{0, 1};
    scene.setAgents(agents, species);
    scene.setInteraction(0, 1, 0.8F);
    scene.setInteraction(1, 0, -0.4F);
    ticks(scene, 1);
    require(close(scene.agents()[0].velocity.x, 0.08F) &&
                close(scene.agents()[1].velocity.x, 0.04F),
            "Matrix row must be the receiver; asymmetric forces can propel both species together");
    require(close(scene.agents()[0].position.x, -3.25F + 0.08F * Step) &&
                close(scene.agents()[1].position.x, 3.25F + 0.04F * Step),
            "Position integration must use the newly updated velocities");

    scene.setAgents(agents, species);
    scene.setInteraction(0, 1, 1.0F);
    scene.setInteraction(1, 0, 1.0F);
    ticks(scene, 1);
    require(close(scene.agents()[0].velocity.x, 0.1F) &&
                close(scene.agents()[1].velocity.x, -0.1F),
            "Symmetric attraction must preserve the center of mass");
    const std::array nearby{particle(10, -0.5F), particle(20, 0.5F)};
    scene.setAgents(nearby, species);
    ticks(scene, 1);
    require(scene.agents()[0].velocity.x < 0.0F && scene.agents()[1].velocity.x > 0.0F,
            "The core must prevent same-species or cross-species collapse");
    const std::array distant{particle(10, -5.5F), particle(20, 5.5F)};
    scene.setAgents(distant, species);
    ticks(scene, 1);
    require(scene.agents()[0].velocity == glm::vec3{0.0F} &&
                scene.agents()[1].velocity == glm::vec3{0.0F},
            "Particles beyond the interaction radius must not affect each other");
}

void periodicInteractionsAndMovement() {
    ParticleLifeScene seam{0};
    ParticleLifeScene center{0};
    prepare(seam);
    prepare(center);
    const std::array<std::uint8_t, 2> species{0, 0};
    const std::array seamAgents{particle(0, 49.0F), particle(1, -49.0F)};
    const std::array centerAgents{particle(0, -1.0F), particle(1, 1.0F)};
    seam.setAgents(seamAgents, species);
    center.setAgents(centerAgents, species);
    ticks(seam, 1);
    ticks(center, 1);
    for (std::size_t index = 0; index < species.size(); ++index) {
        require(seam.agents()[index].velocity == center.agents()[index].velocity,
                "Forces must cross the periodic seam using minimum-image distance");
    }
    auto mover = particle(0, 49.99F, -49.99F);
    mover.velocity = {20.0F, 0.0F, -20.0F};
    seam.setAgents(std::span{&mover, 1}, std::span{species.data(), 1});
    ticks(seam, 1);
    require(close(seam.agents()[0].position.x, 49.99F + 20.0F * Step - 100.0F) &&
                close(seam.agents()[0].position.z, -49.99F - 20.0F * Step + 100.0F) &&
                seam.agents()[0].velocity == mover.velocity,
            "Movement must wrap both planar axes without reflecting velocity");
}

void threeDimensionalForcesAndWrapping() {
    ParticleLifeScene scene{0};
    scene.setThreeDimensional(true);
    prepare(scene);
    const std::array<std::uint8_t, 2> species{0, 1};
    std::array agents{particle(10, 0.0F), particle(20, 0.0F)};
    agents[0].position.y = -3.25F;
    agents[1].position.y = 3.25F;
    scene.setAgents(agents, species);
    scene.setInteraction(0, 1, 0.8F);
    scene.setInteraction(1, 0, -0.4F);
    ticks(scene, 1);
    require(close(scene.agents()[0].velocity.y, 0.08F) &&
                close(scene.agents()[1].velocity.y, 0.04F) &&
                scene.agents()[0].velocity.x == 0.0F && scene.agents()[0].velocity.z == 0.0F,
            "3D forces must act along Y and preserve receiver/source matrix direction");
    require(close(scene.agents()[0].position.y, -3.25F + 0.08F * Step),
            "3D integration must advance Y using the newly computed velocity");

    ParticleLifeScene centered{0};
    centered.setThreeDimensional(true);
    prepare(centered);
    // Check each seam separately and the corner where all axes wrap together.
    const std::array<glm::vec3, 4> directions{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}}};
    scene.setMatrix({});
    for (const auto& direction : directions) {
        agents[0].position = direction * 49.0F;
        agents[1].position = direction * -49.0F;
        scene.setAgents(agents, species);
        agents[0].position = -direction;
        agents[1].position = direction;
        centered.setAgents(agents, species);
        ticks(scene, 1);
        ticks(centered, 1);
        for (std::size_t index = 0; index < agents.size(); ++index) {
            require(scene.agents()[index].velocity == centered.agents()[index].velocity,
                    "3D periodic forces must use the shortest displacement across every seam");
        }
    }
    auto mover = particle(0, 49.99F, 49.99F);
    mover.position.y = -49.99F;
    mover.velocity = {20.0F, -20.0F, 20.0F};
    scene.setAgents(std::span{&mover, 1}, std::span{species.data(), 1});
    ticks(scene, 1);
    require(close(scene.agents()[0].position.x, 49.99F + 20.0F * Step - 100.0F) &&
                close(scene.agents()[0].position.y, -49.99F - 20.0F * Step + 100.0F) &&
                close(scene.agents()[0].position.z, 49.99F + 20.0F * Step - 100.0F) &&
                scene.agents()[0].velocity == mover.velocity,
            "3D movement must wrap all three axes without reflecting velocity");
}

void threeDimensionalOverlaps() {
    ParticleLifeScene forward{0};
    ParticleLifeScene reverse{0};
    forward.setThreeDimensional(true);
    reverse.setThreeDimensional(true);
    prepare(forward);
    prepare(reverse);
    const std::array<std::uint8_t, 2> species{};
    std::array<bool, 3> separatedAxes{};
    for (std::uint32_t id = 1; id <= 32; ++id) {
        std::array agents{particle(0, 0.0F), particle(id, 0.0F)};
        forward.setAgents(agents, species);
        std::reverse(agents.begin(), agents.end());
        reverse.setAgents(agents, species);
        ticks(forward, 1);
        ticks(reverse, 1);
        const auto velocity = forward.agents()[0].velocity;
        require(glm::length(velocity) > 0.0F && velocity == -forward.agents()[1].velocity &&
                    velocity == reverse.agents()[1].velocity &&
                    forward.agents()[0].position == reverse.agents()[1].position,
                "3D overlaps must separate finitely, antisymmetrically and independently of array order");
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            separatedAxes[static_cast<std::size_t>(axis)] =
                separatedAxes[static_cast<std::size_t>(axis)] || velocity[axis] != 0.0F;
        }
    }
    require(std::all_of(separatedAxes.begin(), separatedAxes.end(), [](bool used) { return used; }),
            "3D overlap separation must use all three spatial axes, including Y");
}

void simultaneousIntegrationAndOverlap() {
    ParticleLifeScene ordered{0};
    ParticleLifeScene reversed{0};
    prepare(ordered);
    prepare(reversed);
    std::array agents{particle(7, -4.0F, -2.0F), particle(13, 0.0F),
                      particle(19, 2.0F, 4.0F), particle(29, 0.0F)};
    std::array<std::uint8_t, 4> species{0, 1, 2, 3};
    ordered.setAgents(agents, species);
    std::reverse(agents.begin(), agents.end());
    std::reverse(species.begin(), species.end());
    reversed.setAgents(agents, species);
    ordered.randomizeMatrix(41);
    reversed.randomizeMatrix(41);
    ticks(ordered, 30);
    ticks(reversed, 30);
    for (const auto& expected : ordered.agents()) {
        const auto found = std::find_if(reversed.agents().begin(), reversed.agents().end(),
                                       [&](const auto& agent) { return agent.id == expected.id; });
        require(found != reversed.agents().end() &&
                    glm::length(found->position - expected.position) < 1.0e-5F &&
                    glm::length(found->velocity - expected.velocity) < 1.0e-5F,
                "Reordering particles must not bias simultaneous integration, including overlaps");
    }
    const std::array coincident{particle(10, 0.0F), particle(20, 0.0F)};
    const std::array<std::uint8_t, 2> types{0, 0};
    ordered.setAgents(coincident, types);
    ticks(ordered, 1);
    require(glm::length(ordered.agents()[0].position - ordered.agents()[1].position) > 0.0F &&
                ordered.agents()[0].velocity == -ordered.agents()[1].velocity,
            "Overlapping particles must separate with finite, opposite velocities");
}

void spatialHashMatchesAllPairs() {
    for (const bool threeDimensional : {false, true}) {
        for (float radius : {5.0F, 12.0F, 50.0F}) {
            ParticleLifeScene scene{97, 7921};
            scene.setThreeDimensional(threeDimensional);
            auto parameters = scene.parameters();
            parameters.interactionRadius = radius;
            parameters.force = 1.0F;
            parameters.friction = 0.0F;
            parameters.maxSpeed = 100.0F;
            scene.setParameters(parameters);
            const auto initial = copyAgents(scene);
            std::vector<glm::vec3> expected(initial.size());
            for (std::size_t receiver = 0; receiver < initial.size(); ++receiver) {
                glm::dvec3 acceleration{0.0};
                for (std::size_t source = 0; source < initial.size(); ++source) {
                    if (receiver == source) continue;
                    const auto offset = scene.world().displacement(initial[receiver].position,
                                                                   initial[source].position);
                    const float distance = glm::length(offset);
                    if (distance >= radius) continue;
                    require(distance > 0.0F, "Reference fixture must not contain overlaps");
                    const float force = ParticleLifeScene::interactionForce(distance / radius,
                        scene.matrix()[scene.species()[receiver] * ParticleLifeScene::MaxSpecies +
                                       scene.species()[source]], parameters.coreRatio, parameters.repulsion);
                    acceleration += glm::dvec3{offset / distance} * static_cast<double>(force);
                }
                expected[receiver] = glm::vec3{acceleration * static_cast<double>(Step)};
            }
            ticks(scene, 1);
            for (std::size_t index = 0; index < initial.size(); ++index) {
                require(glm::length(expected[index] - scene.agents()[index].velocity) < 1.0e-6F,
                        "Spatial hashing must match all-pairs forces, including coarse periodic cells");
            }
        }
    }
}

void dimensionChangesResetDeterministically() {
    ParticleLifeScene scene{96, 23};
    require(!scene.threeDimensional(), "Existing scenes must default to 2D");
    scene.setSpeciesCount(8);
    const auto planarInitial = copyAgents(scene);
    constexpr std::array<glm::vec3, 3> legacyPositions{{
        {0x1.bad348p+0F, 0.0F, 0x1.0e8e28p+4F},
        {0x1.6591fp+5F, 0.0F, -0x1.f804bcp+4F},
        {0x1.a8bc4cp+4F, 0.0F, 0x1.c2afe8p+4F}}};
    for (std::size_t index = 0; index < legacyPositions.size(); ++index) {
        require(planarInitial[index].position == legacyPositions[index],
                "Adding 3D must preserve the existing seeded 2D position sequence");
    }
    const std::vector<std::uint8_t> initialSpecies(scene.species().begin(), scene.species().end());
    scene.setInteraction(0, 1, 0.75F);
    const auto matrix = scene.matrix();
    auto parameters = scene.parameters();
    parameters.friction = 7.0F;
    scene.setParameters(parameters);
    ticks(scene, 2);
    scene.setPaused(true);
    scene.setThreeDimensional(true);
    require(scene.threeDimensional() && scene.world().minimum == glm::vec3{-50.0F} &&
                scene.world().maximum == glm::vec3{50.0F} && scene.paused() && scene.totalSteps() == 0 &&
                scene.seed() == 23 && scene.speciesCount() == 8 && scene.count() == 96 &&
                scene.parameters().friction == 7.0F && scene.matrix() == matrix &&
                std::equal(initialSpecies.begin(), initialSpecies.end(), scene.species().begin()),
            "Entering 3D must reset into a cubic world while retaining population settings and controls");
    const auto volumeInitial = copyAgents(scene);
    std::array<bool, 3> occupiedAxes{};
    for (const auto& agent : scene.agents()) {
        require(agent.velocity == glm::vec3{0.0F}, "New 3D particles must begin at rest");
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            require(agent.position[axis] >= -50.0F && agent.position[axis] < 50.0F,
                    "3D initialization must remain within the half-open cubic world");
            occupiedAxes[static_cast<std::size_t>(axis)] =
                occupiedAxes[static_cast<std::size_t>(axis)] || agent.position[axis] != 0.0F;
        }
    }
    require(std::all_of(occupiedAxes.begin(), occupiedAxes.end(), [](bool used) { return used; }),
            "3D initialization must populate all three axes");
    require(scene.singleStep(), "A paused 3D scene must support manual steps");
    const auto evolved = copyAgents(scene);
    scene.setThreeDimensional(true);
    require(scene.totalSteps() == 1, "Selecting the active dimension must retain the clock");
    requireSameAgents(evolved, scene);
    scene.reset();
    requireSameAgents(volumeInitial, scene);
    scene.setThreeDimensional(false);
    require(!scene.threeDimensional() && scene.paused() && scene.totalSteps() == 0,
            "Returning to 2D must restart the scene and retain pause");
    requireSameAgents(planarInitial, scene);
    scene.setThreeDimensional(true);
    requireSameAgents(volumeInitial, scene);
    scene.setPaused(false);
    scene.advance(ParticleLifeScene::FixedStepSeconds * 0.5);
    scene.setThreeDimensional(false);
    require(scene.advance(ParticleLifeScene::FixedStepSeconds * 0.5) == 0,
            "Changing dimension must discard fractional elapsed time");
}

void threeDimensionalValidationAndSpeedLimit() {
    ParticleLifeScene scene{0};
    scene.setThreeDimensional(true);
    prepare(scene);
    auto mover = particle(0, 1.0F, 3.0F);
    mover.position.y = 2.0F;
    mover.velocity = {3.0F, 4.0F, 12.0F};
    const std::uint8_t species = 0;
    scene.setAgents(std::span{&mover, 1}, std::span{&species, 1});
    auto parameters = scene.parameters();
    parameters.maxSpeed = 6.5F;
    scene.setParameters(parameters);
    require(scene.agents()[0].velocity == glm::vec3{1.5F, 2.0F, 6.0F},
            "3D speed limiting must include the Y velocity component");
    const auto accepted = copyAgents(scene);
    for (float invalidY : {-50.01F, 50.0F, std::numeric_limits<float>::quiet_NaN()}) {
        auto invalid = accepted;
        invalid[0].position.y = invalidY;
        requireInvalid([&] { scene.setAgents(invalid, std::span{&species, 1}); },
                       "3D particles must reject out-of-bounds or nonfinite Y positions");
        requireSameAgents(accepted, scene);
    }
    auto invalid = accepted;
    invalid[0].velocity.y = std::numeric_limits<float>::infinity();
    requireInvalid([&] { scene.setAgents(invalid, std::span{&species, 1}); },
                   "3D particles must reject nonfinite Y velocities");
    requireSameAgents(accepted, scene);
    scene.setThreeDimensional(false);
    auto nonplanar = copyAgents(scene);
    nonplanar[0].velocity.y = 0.1F;
    requireInvalid([&] { scene.setAgents(nonplanar, scene.species()); },
                   "Returning to 2D must restore planar velocity validation");
}

void dampingSpeedAndBounds() {
    ParticleLifeScene scene{0};
    prepare(scene);
    auto parameters = scene.parameters();
    parameters.friction = 4.0F;
    scene.setParameters(parameters);
    auto mover = particle(42, 0.0F);
    mover.velocity = {12.0F, 0.0F, 0.0F};
    const std::uint8_t species = 0;
    scene.setAgents(std::span{&mover, 1}, std::span{&species, 1});
    ticks(scene, 120);
    require(close(scene.agents()[0].velocity.x, 12.0F * std::exp(-4.0F), 2.0e-6F),
            "Friction must exponentially decay velocity in simulated seconds");

    scene.setCount(256);
    parameters.force = 200.0F;
    parameters.maxSpeed = 3.0F;
    parameters.repulsion = 10.0F;
    parameters.friction = 0.0F;
    parameters.interactionRadius = 50.0F;
    scene.setParameters(parameters);
    for (int step = 0; step < 120; ++step) {
        ticks(scene, 1);
        for (const auto& agent : scene.agents()) {
            require(std::isfinite(agent.position.x) && std::isfinite(agent.position.z) &&
                        agent.position.x >= -50.0F && agent.position.x < 50.0F &&
                        agent.position.z >= -50.0F && agent.position.z < 50.0F &&
                        agent.position.y == 0.0F && agent.velocity.y == 0.0F &&
                        glm::length(glm::dvec3{agent.velocity}) <= 3.0,
                    "Strong sustained forces must remain finite, planar, wrapped and speed-limited");
        }
    }
    scene.setAgents(scene.agents(), scene.species());
    const auto beforeFreeze = copyAgents(scene);
    parameters.maxSpeed = 0.0F;
    scene.setParameters(parameters);
    ticks(scene, 1);
    for (std::size_t index = 0; index < scene.count(); ++index) {
        require(scene.agents()[index].velocity == glm::vec3{0.0F} &&
                    scene.agents()[index].position == beforeFreeze[index].position,
                "Zero maximum speed must freeze all motion");
    }
}

void deterministicResetAndControls() {
    ParticleLifeScene scene{96, 23};
    const auto initial = copyAgents(scene);
    const auto originalMatrix = scene.matrix();
    for (std::size_t type = 0; type < scene.speciesCount(); ++type) {
        require(scene.matrix()[type * ParticleLifeScene::MaxSpecies + type] > 0.0F,
                "Default rules must attract each species to itself");
    }
    ticks(scene, 30);
    require(!std::equal(initial.begin(), initial.end(), scene.agents().begin(), sameAgent),
            "Default particle life must evolve from its seeded starting state");
    const auto evolved = copyAgents(scene);
    scene.reset();
    requireSameAgents(initial, scene);
    ticks(scene, 30);
    requireSameAgents(evolved, scene);

    scene.setPaused(true);
    scene.setInteraction(0, 1, 0.75F);
    const auto editedMatrix = scene.matrix();
    auto parameters = scene.parameters();
    parameters.friction = 7.0F;
    scene.setParameters(parameters);
    scene.reset();
    require(scene.paused() && scene.totalSteps() == 0 && scene.parameters().friction == 7.0F &&
                scene.matrix() == editedMatrix, "Reset must retain pause state, parameters and matrix");
    requireSameAgents(initial, scene);
    scene.setSeed(24);
    require(scene.seed() == 24 && scene.paused() && scene.totalSteps() == 0,
            "Changing the seed must restart the population and preserve pause state");
    require(!std::equal(initial.begin(), initial.end(), scene.agents().begin(), sameAgent),
            "Changing the seed must change initial positions");
    scene.setSpeciesCount(8);
    scene.setCount(101);
    require(scene.speciesCount() == 8 && scene.count() == 101 && scene.species().size() == 101,
            "Population and species controls must update the scene");
    for (std::size_t index = 0; index < scene.count(); ++index) {
        require(scene.species()[index] < 8 &&
                    scene.agents()[index].color == ParticleLifeScene::speciesColor(scene.species()[index]),
                "Every particle must have an active species and matching display color");
    }
    const auto beforeRules = copyAgents(scene);
    scene.randomizeMatrix(9);
    const auto randomized = scene.matrix();
    scene.randomizeMatrix(10);
    require(scene.matrix() != randomized && scene.matrix() != originalMatrix,
            "Different matrix seeds must produce different rule sets");
    scene.randomizeMatrix(9);
    require(scene.matrix() == randomized && scene.seed() == 24,
            "Matrix randomization must be reproducible without changing the population seed");
    requireSameAgents(beforeRules, scene);
    scene.setCount(0);
    require(scene.singleStep() && scene.count() == 0, "An empty scene must support stepping");
}

void clockPartitioningAndBudgets() {
    ParticleLifeScene smallFrames{64, 71};
    ParticleLifeScene largeFrames{64, 71};
    for (int frame = 0; frame < 120; ++frame) smallFrames.advance(1.0 / 240.0);
    for (int frame = 0; frame < 30; ++frame) largeFrames.advance(1.0 / 60.0);
    require(smallFrames.totalSteps() == 60 && largeFrames.totalSteps() == 60,
            "Frame partition must not change simulated time");
    requireSameAgents(copyAgents(smallFrames), largeFrames);
    require(!smallFrames.singleStep(), "Manual stepping requires pause");
    smallFrames.setPaused(true);
    const auto paused = copyAgents(smallFrames);
    require(smallFrames.advance(120.0) == 0 && !smallFrames.lastAdvanceLimited(),
            "Paused wall-clock time must not enter the accumulator");
    requireSameAgents(paused, smallFrames);
    require(smallFrames.singleStep() && smallFrames.totalSteps() == 61,
            "A paused manual step must advance exactly one fixed step");
    smallFrames.setPaused(false);
    require(smallFrames.advance(120.0) == 8 && smallFrames.lastAdvanceLimited(),
            "A stalled frame must cap catch-up work at eight steps");
    require(smallFrames.advance(0.0) == 0 && !smallFrames.lastAdvanceLimited(),
            "Dropped wall time must not remain as future backlog");
    smallFrames.reset();
    require(smallFrames.advance(0.05, 0.0) == 1 && smallFrames.lastAdvanceLimited() &&
                smallFrames.advance(0.0) == 0,
            "A zero work budget must permit one step then drop pending complete steps");
    require(smallFrames.elapsedSimulationSeconds() == ParticleLifeScene::FixedStepSeconds,
            "Elapsed simulation time must count only completed steps");
    smallFrames.reset();
    smallFrames.advance(ParticleLifeScene::FixedStepSeconds * 0.5);
    smallFrames.setPaused(true);
    smallFrames.setPaused(false);
    require(smallFrames.advance(ParticleLifeScene::FixedStepSeconds * 0.5) == 0,
            "Changing pause state must clear a stale fractional frame");
}

void invalidInputsAreAtomic() {
    ParticleLifeScene scene{8, 7};
    ticks(scene, 2);
    const auto agents = copyAgents(scene);
    const auto matrix = scene.matrix();
    const auto parameters = scene.parameters();
    const auto stepCount = scene.totalSteps();
    requireInvalid([&] { scene.setCount(ParticleLifeScene::MaxParticles + 1); }, "Oversized count must fail");
    requireInvalid([&] { scene.setSpeciesCount(0); }, "Zero species must fail");
    requireInvalid([&] { scene.setSpeciesCount(9); }, "Excess species must fail");
    requireInvalid([&] { scene.setInteraction(8, 0, 1.0F); }, "Invalid matrix indices must fail");
    auto badMatrix = matrix;
    badMatrix.back() = std::numeric_limits<float>::quiet_NaN();
    requireInvalid([&] { scene.setMatrix(badMatrix); }, "NaN matrix entries must fail atomically");
    constexpr std::array fields{&ParticleLifeParameters::interactionRadius, &ParticleLifeParameters::force,
        &ParticleLifeParameters::friction, &ParticleLifeParameters::repulsion,
        &ParticleLifeParameters::coreRatio, &ParticleLifeParameters::maxSpeed};
    for (const auto field : fields) {
        for (float invalid : {-1.0F, std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()}) {
            auto bad = parameters;
            bad.*field = invalid;
            requireInvalid([&] { scene.setParameters(bad); }, "Invalid parameters must fail atomically");
        }
    }
    auto badRadius = parameters;
    badRadius.interactionRadius = 0.0F;
    requireInvalid([&] { scene.setParameters(badRadius); }, "Zero radius must fail");
    std::vector<std::uint8_t> species(scene.species().begin(), scene.species().end());
    requireInvalid([&] { scene.setAgents(agents, {}); }, "Mismatched particle/species lengths must fail");
    auto invalidAgents = agents;
    invalidAgents[1].id = invalidAgents[0].id;
    requireInvalid([&] { scene.setAgents(invalidAgents, species); }, "Duplicate IDs must fail");
    invalidAgents = agents;
    invalidAgents[0].position.x = 50.0F;
    requireInvalid([&] { scene.setAgents(invalidAgents, species); }, "Excluded upper world bound must fail");
    invalidAgents = agents;
    invalidAgents[0].position.y = 0.1F;
    requireInvalid([&] { scene.setAgents(invalidAgents, species); }, "Nonplanar particles must fail");
    invalidAgents = agents;
    invalidAgents[0].velocity.x = std::numeric_limits<float>::infinity();
    requireInvalid([&] { scene.setAgents(invalidAgents, species); }, "Infinite velocities must fail");
    species[0] = static_cast<std::uint8_t>(scene.speciesCount());
    requireInvalid([&] { scene.setAgents(agents, species); }, "Inactive species must fail");
    scene.setPaused(true);
    for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
        requireInvalid([&] { scene.advance(invalid); }, "Invalid elapsed time must fail while paused");
    }
    for (double invalid : {-1.0, -std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
        requireInvalid([&] { scene.advance(0.0, invalid); }, "Invalid work budget must fail while paused");
    }
    requireSameAgents(agents, scene);
    require(scene.matrix() == matrix && scene.totalSteps() == stepCount &&
                scene.parameters().force == parameters.force &&
                scene.parameters().maxSpeed == parameters.maxSpeed,
            "Rejected mutations must preserve all state and the simulation clock");
    scene.setAgents(scene.agents(), scene.species());
    requireSameAgents(agents, scene);
    require(scene.totalSteps() == 0 && scene.paused(),
            "Aliased replacement must safely copy the existing population and reset only its clock");
}

} // namespace

int main() {
    try {
        forceCurve();
        pairForcesAndMatrixDirection();
        periodicInteractionsAndMovement();
        threeDimensionalForcesAndWrapping();
        threeDimensionalOverlaps();
        simultaneousIntegrationAndOverlap();
        spatialHashMatchesAllPairs();
        dimensionChangesResetDeterministically();
        threeDimensionalValidationAndSpeedLimit();
        dampingSpeedAndBounds();
        deterministicResetAndControls();
        clockPartitioningAndBudgets();
        invalidInputsAreAtomic();
        std::cout << "Particle life tests passed (forces, wrapping, simultaneous integration, replay, controls, and bounds)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Particle life test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
