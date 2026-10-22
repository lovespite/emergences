#include "scenarios/boids/CursorAttract.h"
#include "scenarios/boids/CursorRepel.h"
#include "sim/cpu/CpuSimulation.h"

#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using emergences::scenarios::boids::CursorAttract;
using emergences::scenarios::boids::CursorIntervention;
using emergences::scenarios::boids::CursorRepel;
using emergences::sim::Agent;
using emergences::sim::BoundaryMode;
using emergences::sim::World;
using emergences::sim::cpu::CpuSimulation;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(const glm::vec3& actual, const glm::vec3& expected, float tolerance = 0.0001F) {
    return glm::length(actual - expected) < tolerance;
}

template <typename Exception = std::invalid_argument, typename Action>
void requireThrows(Action action) {
    try {
        action();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Invalid cursor input must throw the expected exception");
}

bool sameAgent(const Agent& left, const Agent& right) {
    return left.position == right.position && left.velocity == right.velocity &&
           left.color == right.color && left.size == right.size && left.reserved == right.reserved &&
           left.id == right.id;
}

void defaultsAndInactiveStates() {
    World world;
    Agent original;
    original.position = {1.0F, 0.0F, 0.0F};
    original.velocity = {0.0F, 2.0F, 0.0F};
    original.color = {0.1F, 0.3F, 0.5F};
    original.size = 2.0F;
    original.reserved = 0.25F;
    original.id = 42U;
    CursorRepel repel;
    CursorAttract attract;
    require(repel.name() == "CursorRepel" && attract.name() == "CursorAttract",
            "Cursor interventions must have distinct names");
    const std::array<CursorIntervention*, 2> interventions{&repel, &attract};
    for (CursorIntervention* cursor : interventions) {
        require(!cursor->enabled() && cursor->radius() == 8.0F && cursor->strength() == 30.0F &&
                    cursor->position() == glm::vec3(0.0F),
                "Cursor defaults must start disabled at the origin with scenario parameters");
        std::vector agents{original};
        cursor->apply(1.0F, agents, world);
        require(sameAgent(agents.front(), original), "Disabled cursors must not change agents");
        cursor->setEnabled(true);
        cursor->apply(0.0F, agents, world);
        require(sameAgent(agents.front(), original), "A paused cursor must not change agents");
        cursor->setRadius(0.0F);
        cursor->apply(1.0F, agents, world);
        require(sameAgent(agents.front(), original), "Zero cursor radius must disable the force");
        cursor->setRadius(8.0F);
        cursor->setStrength(0.0F);
        cursor->apply(1.0F, agents, world);
        require(sameAgent(agents.front(), original), "Zero cursor strength must disable the force");
        cursor->setStrength(30.0F);
        agents.clear();
        cursor->apply(1.0F, agents, world);
        require(agents.empty(), "A cursor must not create agents");
    }
}

void directionsAndTimeScaling() {
    World world;
    const World originalWorld = world;
    Agent original;
    original.position = {4.0F, 6.0F, 3.0F};
    original.velocity = {1.0F, -2.0F, 3.0F};
    original.color = {0.2F, 0.4F, 0.6F};
    original.size = 1.5F;
    original.reserved = 0.5F;
    original.id = 91U;
    CursorRepel repel(20.0F, 10.0F);
    CursorAttract attract(20.0F, 10.0F);
    const std::array<CursorIntervention*, 2> interventions{&repel, &attract};
    for (CursorIntervention* cursor : interventions) {
        cursor->setPosition({1.0F, 2.0F, 3.0F});
        cursor->setEnabled(true);
        std::vector fullStep{original};
        std::vector halfSteps{original};
        cursor->apply(0.5F, fullStep, world);
        cursor->apply(0.25F, halfSteps, world);
        cursor->apply(0.25F, halfSteps, world);
        const float sign = cursor == &repel ? 1.0F : -1.0F;
        require(near(fullStep.front().velocity, original.velocity + sign * glm::vec3(3.0F, 4.0F, 0.0F)),
                "Cursor impulses must point radially, use world position, and add to velocity");
        require(near(fullStep.front().velocity, halfSteps.front().velocity),
                "At a fixed position, two half impulses must equal one full impulse");
        fullStep.front().velocity = original.velocity;
        require(sameAgent(fullStep.front(), original),
                "Cursor interventions must preserve all fields except velocity");
        require(world.minimum == originalWorld.minimum && world.maximum == originalWorld.maximum &&
                    world.boundary == originalWorld.boundary,
                "Cursor interventions must not alter the world");
    }
}

void localSmoothFalloff() {
    World world;
    world.boundary = BoundaryMode::Bounce;
    CursorRepel repel(8.0F, 4.0F);
    repel.setEnabled(true);
    std::vector<Agent> agents(6);
    const std::array distances{1.0F, 2.0F, 3.0F, 3.999F, 4.0F, 4.001F};
    for (std::size_t index = 0; index < agents.size(); ++index) {
        agents[index].position.x = distances[index];
    }
    repel.apply(1.0F, agents, world);
    require(near(agents[0].velocity, {6.75F, 0.0F, 0.0F}) &&
                near(agents[1].velocity, {4.0F, 0.0F, 0.0F}) &&
                near(agents[2].velocity, {1.25F, 0.0F, 0.0F}),
            "Cursor force must smoothly diminish with distance");
    require(agents[3].velocity.x > 0.0F && agents[3].velocity.x < 0.00001F,
            "Cursor force must approach zero continuously at its radius");
    require(agents[4].velocity == glm::vec3(0.0F) && agents[5].velocity == glm::vec3(0.0F),
            "Agents at or beyond the cursor radius must receive no impulse");
    repel.setRadius(2.0F);
    repel.setStrength(16.0F);
    repel.setPosition({2.0F, 0.0F, 0.0F});
    agents.assign(1, Agent{});
    agents[0].position = {3.0F, 0.0F, 0.0F};
    repel.apply(1.0F, agents, world);
    require(near(agents[0].velocity, {8.0F, 0.0F, 0.0F}),
            "Position, radius, and strength changes must apply immediately");
}

void periodicSeams() {
    World world;
    world.minimum = glm::vec3(-5.0F);
    world.maximum = glm::vec3(5.0F);
    CursorRepel repel(8.0F, 1.0F);
    CursorAttract attract(8.0F, 1.0F);
    const std::array<CursorIntervention*, 2> interventions{&repel, &attract};
    for (CursorIntervention* cursor : interventions) {
        cursor->setEnabled(true);
        for (const float side : {-1.0F, 1.0F}) {
            cursor->setPosition({side * 4.75F, 0.0F, 0.0F});
            std::vector<Agent> agents(1);
            agents[0].position = {-side * 4.75F, 0.0F, 0.0F};
            world.boundary = BoundaryMode::Wrap;
            cursor->apply(0.5F, agents, world);
            const float sign = cursor == &repel ? 1.0F : -1.0F;
            require(near(agents[0].velocity, {sign * side * 2.0F, 0.0F, 0.0F}),
                    "Cursor forces must use the shortest displacement through either periodic seam");
            agents[0].velocity = glm::vec3(0.0F);
            world.boundary = BoundaryMode::Bounce;
            cursor->apply(0.5F, agents, world);
            require(agents[0].velocity == glm::vec3(0.0F),
                    "A bouncing world must not apply cursor force across opposite walls");
        }
    }
}

void overlapsAndTinyDistances() {
    World world;
    CursorRepel repel(2.0F, 1.0F);
    CursorAttract attract(2.0F, 1.0F);
    repel.setEnabled(true);
    attract.setEnabled(true);
    std::vector<Agent> agents(6);
    for (std::size_t index = 0; index < agents.size(); ++index) {
        agents[index].id = static_cast<std::uint32_t>(index);
    }
    auto repeated = agents;
    auto attracted = agents;
    repel.apply(0.5F, agents, world);
    repel.apply(0.5F, repeated, world);
    attract.apply(0.5F, attracted, world);
    glm::vec3 combined{0.0F};
    for (std::size_t index = 0; index < agents.size(); ++index) {
        require(std::abs(glm::length(agents[index].velocity) - 1.0F) < 0.0001F &&
                    agents[index].velocity == repeated[index].velocity,
                "Coincident repulsion must be finite, bounded, and deterministic");
        require(attracted[index].velocity == glm::vec3(0.0F),
                "An attracted agent already at the cursor must stay at rest");
        combined += agents[index].velocity;
    }
    require(combined == glm::vec3(0.0F), "Coincident agents must not all be repelled in one direction");
    agents.assign(1, Agent{});
    agents[0].position = {0.0F, 1.0e-30F, 0.0F};
    repel.apply(0.5F, agents, world);
    require(near(agents[0].velocity, {0.0F, 1.0F, 0.0F}),
            "A near coincidence must retain its radial direction without float-square underflow");
}

template <typename Cursor>
void invalidParameters() {
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Cursor cursor(2.0F, 3.0F);
    cursor.setPosition({1.0F, 2.0F, 3.0F});
    World world;
    std::vector<Agent> agents(1);
    for (const float invalid : {-1.0F, infinity, -infinity, nan}) {
        requireThrows([invalid] { const Cursor badStrength(invalid, 1.0F); });
        requireThrows([invalid] { const Cursor badRadius(1.0F, invalid); });
        requireThrows([&] { cursor.setStrength(invalid); });
        requireThrows([&] { cursor.setRadius(invalid); });
        requireThrows([&] { cursor.apply(invalid, agents, world); });
        require(cursor.strength() == 2.0F && cursor.radius() == 3.0F,
                "Rejected cursor parameter changes must preserve the previous configuration");
    }
    for (const float invalid : {infinity, -infinity, nan}) {
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            glm::vec3 position{0.0F};
            position[axis] = invalid;
            requireThrows([&] { cursor.setPosition(position); });
            require(cursor.position() == glm::vec3(1.0F, 2.0F, 3.0F),
                    "Rejected positions must leave the cursor in its previous location");
        }
    }
    const Cursor disabled(0.0F, 0.0F);
    require(disabled.strength() == 0.0F && disabled.radius() == 0.0F,
            "Zero cursor strength and radius must be valid");
    const float maximum = std::numeric_limits<float>::max();
    cursor.setStrength(maximum);
    cursor.setRadius(maximum);
    require(cursor.strength() == maximum && cursor.radius() == maximum,
            "All finite nonnegative cursor parameters must be accepted");
}

void backendIntegrationAndOverflow() {
    World world;
    world.boundary = BoundaryMode::Bounce;
    CpuSimulation simulation(0, world);
    simulation.setMotionLimits({0.0F, 10.0F, 20.0F});
    Agent original;
    original.position = {1.0F, 0.0F, 0.0F};
    original.velocity = {2.0F, 0.0F, 0.0F};
    simulation.setAgents(std::array{original});
    auto cursor = std::make_unique<CursorRepel>(8.0F, 4.0F);
    auto* cursorControl = cursor.get();
    cursor->setEnabled(true);
    simulation.interventions().push_back(std::move(cursor));
    simulation.step(0.5F);
    require(near(simulation.agents()[0].position, {2.0F, 0.0F, 0.0F}) &&
                near(simulation.agents()[0].velocity, {4.0F, 0.0F, 0.0F}),
            "The backend must apply cursor impulses at the post-integration position");
    simulation.setMotionLimits({0.0F, 3.0F, 20.0F});
    simulation.setAgents(std::array{original});
    simulation.step(0.5F);
    require(near(simulation.agents()[0].velocity, {3.0F, 0.0F, 0.0F}),
            "The backend must enforce its speed limit after cursor impulses");
    const Agent stationary;
    simulation.setAgents(std::array{stationary});
    cursorControl->setStrength(std::numeric_limits<float>::max());
    requireThrows<std::overflow_error>([&] { simulation.step(2.0F); });
    require(sameAgent(simulation.agents()[0], stationary),
            "An overflowing cursor impulse must not commit partial backend state");
}

} // namespace

int main() {
    try {
        defaultsAndInactiveStates();
        directionsAndTimeScaling();
        localSmoothFalloff();
        periodicSeams();
        overlapsAndTinyDistances();
        invalidParameters<CursorRepel>();
        invalidParameters<CursorAttract>();
        backendIntegrationAndOverflow();
        std::cout << "Cursor intervention tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Cursor intervention test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
