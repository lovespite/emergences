#include "sim/World.h"
#include "sim/cpu/SpatialHash.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

using emergences::sim::Agent;
using emergences::sim::BoundaryMode;
using emergences::sim::Neighbor;
using emergences::sim::World;
using emergences::sim::cpu::SpatialHash;

static_assert(std::is_aggregate_v<World>);

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template<class Exception, class Function>
void requireThrows(Function&& function, const char* message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

bool near(float actual, float expected, float tolerance = 0.00001F) {
    return std::abs(actual - expected) <= tolerance * std::max(1.0F, std::abs(expected));
}

void requireVector(const glm::vec3& actual, const glm::vec3& expected, const char* message) {
    require(near(actual.x, expected.x) && near(actual.y, expected.y) && near(actual.z, expected.z),
            message);
}

double referenceDistanceSquared(const World& world, const glm::vec3& a, const glm::vec3& b) {
    double result = 0.0;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        double distance = std::abs(static_cast<double>(b[axis]) - a[axis]);
        if (world.boundary == BoundaryMode::Wrap) {
            const double extent = static_cast<double>(world.maximum[axis]) - world.minimum[axis];
            distance = std::min(distance, extent - distance);
        }
        result += distance * distance;
    }
    return result;
}

void checkGrid(const std::vector<Agent>& agents, const World& world,
               float maximumRadius, const std::vector<float>& radii) {
    SpatialHash grid;
    grid.rebuild(agents, world, maximumRadius);
    std::vector<Neighbor> neighbors;
    for (const float radius : radii) {
        for (std::size_t self = 0; self < agents.size(); ++self) {
            // A stale sentinel verifies that every query replaces the output.
            neighbors.push_back({nullptr, -1.0F});
            grid.query(self, radius, neighbors);
            std::vector<std::size_t> actual;
            std::vector<std::size_t> expected;
            const double radiusSquared = static_cast<double>(radius) * radius;
            for (std::size_t other = 0; other < agents.size(); ++other) {
                if (other != self && radius > 0.0F &&
                    referenceDistanceSquared(world, agents[self].position, agents[other].position) <=
                        radiusSquared) {
                    expected.push_back(other);
                }
            }
            for (const Neighbor& neighbor : neighbors) {
                const auto found = std::find_if(agents.begin(), agents.end(), [&](const Agent& agent) {
                    return &agent == neighbor.agent;
                });
                require(found != agents.end(), "Grid must return borrowed pointers into the input span");
                const auto index = static_cast<std::size_t>(found - agents.begin());
                actual.push_back(index);
                const double expectedDistance = referenceDistanceSquared(
                    world, agents[self].position, agents[index].position);
                require(std::abs(neighbor.distanceSquared - expectedDistance) <=
                            std::max(std::numeric_limits<double>::denorm_min(),
                                     expectedDistance * 1.0e-12),
                        "Grid must preserve the double precision periodic squared distance");
                require(neighbor.displacement.has_value() && *neighbor.displacement ==
                            world.displacement(agents[self].position, agents[index].position),
                        "Cached displacement must exactly match the checked World result");
                const auto checked = world.displacement(agents[self].position, agents[index].position);
                for (glm::length_t axis = 0; axis < 3; ++axis) {
                    require(std::signbit((*neighbor.displacement)[axis]) == std::signbit(checked[axis]),
                            "Cached displacement must also preserve signed zero");
                }
            }
            std::sort(actual.begin(), actual.end());
            require(actual == expected, "Grid neighbor indices must equal brute force without duplicates");
        }
    }
}

void wrappingWorld() {
    const World world{{-2.0F, 10.0F, -1.0F}, {3.0F, 14.0F, 1.0F}, BoundaryMode::Wrap};
    world.validate();
    glm::vec3 position{18.0F, -3.0F, 6.0F};
    glm::vec3 velocity{-3.0F, 2.0F, 1.0F};
    world.applyBoundary(position, velocity);
    requireVector(position, {-2.0F, 13.0F, 0.0F}, "Wrap must handle repeated positive and negative crossings");
    requireVector(velocity, {-3.0F, 2.0F, 1.0F}, "Wrap must preserve velocity");
    position = world.maximum;
    world.applyBoundary(position, velocity);
    requireVector(position, world.minimum, "Wrap upper bound must map to lower bound");
    requireVector(world.displacement({-1.9F, 10.1F, -0.9F}, {2.9F, 13.9F, 0.9F}),
                  {-0.2F, -0.2F, -0.2F}, "Wrap displacement must cross seams by the shortest route");
    requireVector(world.displacement({-2.0F, 10.0F, -1.0F}, {0.5F, 12.0F, 0.0F}),
                  {2.5F, 2.0F, 1.0F}, "Half-period ties must keep direction");
    requireVector(world.displacement({0.5F, 12.0F, 0.0F}, {-2.0F, 10.0F, -1.0F}),
                  {-2.5F, -2.0F, -1.0F}, "Half-period ties must be antisymmetric");

    const World wide{{-1.0e30F, -1.0e30F, -1.0e30F}, {1.0e30F, 1.0e30F, 1.0e30F}};
    position = {1.0F, -1.0F, std::numeric_limits<float>::denorm_min()};
    const glm::vec3 original = position;
    wide.applyBoundary(position, velocity);
    require(position == original, "Already valid positions must retain precision in a large world");

    const World tiny{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}};
    position = glm::vec3(-std::numeric_limits<float>::denorm_min());
    tiny.applyBoundary(position, velocity);
    require(position.x >= 0.0F && position.x < 1.0F, "Rounded wrapped positions must remain half-open");
    position = glm::vec3(std::numeric_limits<float>::max());
    tiny.applyBoundary(position, velocity);
    require(position.x >= 0.0F && position.x < 1.0F, "Extreme finite overshoots must wrap without overflow");
}

void bouncingWorld() {
    const World world{{-2.0F, 10.0F, -1.0F}, {3.0F, 14.0F, 1.0F}, BoundaryMode::Bounce};
    glm::vec3 position{14.0F, -3.0F, 6.0F};
    glm::vec3 velocity{3.0F, -2.0F, 1.0F};
    world.applyBoundary(position, velocity);
    requireVector(position, {2.0F, 13.0F, 0.0F}, "Bounce must fold repeated overshoots into bounds");
    requireVector(velocity, {-3.0F, -2.0F, -1.0F}, "Bounce velocity must use the reflection parity");
    position = world.minimum;
    velocity = glm::vec3(-2.0F);
    world.applyBoundary(position, velocity);
    requireVector(velocity, glm::vec3(2.0F), "Lower wall velocity must point inward");
    position = world.maximum;
    world.applyBoundary(position, velocity);
    requireVector(velocity, glm::vec3(-2.0F), "Upper wall velocity must point inward");
    requireVector(world.displacement(world.minimum, world.maximum), {5.0F, 4.0F, 2.0F},
                  "Bounce displacement must use ordinary Euclidean separation");
    position = glm::vec3(std::numeric_limits<float>::max());
    world.applyBoundary(position, velocity);
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        require(position[axis] >= world.minimum[axis] && position[axis] <= world.maximum[axis] &&
                    std::isfinite(velocity[axis]), "Extreme finite bounce must stay bounded and finite");
    }
}

void emptySingletonAndEdges() {
    const World wrap{{0.0F, 0.0F, 0.0F}, {10.0F, 3.0F, 1.0F}, BoundaryMode::Wrap};
    SpatialHash grid;
    std::vector<Neighbor> output;
    grid.rebuild({}, wrap, 1.0F);
    requireThrows<std::out_of_range>([&] { grid.query(0, 1.0F, output); },
                                    "Empty grid query must reject a nonexistent self");
    std::vector<Agent> agents(1);
    agents[0].position = {1.0F, 1.0F, 0.5F};
    checkGrid(agents, wrap, 20.0F, {0.0F, 1.0F, 20.0F});
    agents.resize(2);
    agents[0].position = glm::vec3(0.0F);
    agents[1].position = {0.1F, 0.0F, 0.0F};
    grid.rebuild(agents, wrap, 0.1F);
    grid.query(0, 0.1F, output);
    require(output.size() == 1 && output[0].distanceSquared == static_cast<double>(0.1F) * 0.1F,
            "An exact 0.1F radius neighbor must retain its precise squared distance");
    agents.resize(8);
    const glm::vec3 positions[] = {{0.0F, 0.0F, 0.0F}, {9.9F, 2.9F, 0.9F},
        {2.5F, 1.5F, 0.5F}, {5.0F, 1.5F, 0.5F}, {7.5F, 1.5F, 0.5F},
        {2.5F, 1.5F, 0.5F}, {2.6F, 1.5F, 0.5F}, {5.01F, 1.5F, 0.5F}};
    for (std::size_t index = 0; index < agents.size(); ++index) {
        agents[index].position = positions[index];
        agents[index].id = 7; // Self-exclusion is by index even with duplicate IDs.
    }
    for (const float radius : {0.0F, 0.1F, 0.4F, 2.5F, 3.0F, 4.0F, 20.0F}) {
        checkGrid(agents, wrap, radius, {0.0F, radius * 0.5F, radius});
    }
    World bounce = wrap;
    bounce.boundary = BoundaryMode::Bounce;
    agents[1].position = bounce.maximum;
    for (const float radius : {0.1F, 2.5F, 3.0F, 20.0F}) {
        checkGrid(agents, bounce, radius, {0.0F, radius * 0.5F, radius});
    }
}

void randomizedBruteForce() {
    std::mt19937 random(0x9e3779b9U);
    std::uniform_real_distribution<float> unit(0.0F, 1.0F);
    for (int trial = 0; trial < 8; ++trial) {
        const glm::vec3 low{-17.25F + unit(random), 2.125F, -0.25F};
        const glm::vec3 extent{21.25F + unit(random), 3.5F + unit(random), 0.5F + unit(random)};
        World world{low, low + extent};
        std::vector<Agent> agents(129);
        for (Agent& agent : agents) {
            for (glm::length_t axis = 0; axis < 3; ++axis) {
                agent.position[axis] = std::min(low[axis] + extent[axis] * unit(random),
                                               std::nextafter(world.maximum[axis], low[axis]));
            }
        }
        agents.front().position = low;
        agents.back().position = glm::vec3(std::nextafter(world.maximum.x, low.x),
                                           std::nextafter(world.maximum.y, low.y),
                                           std::nextafter(world.maximum.z, low.z));
        for (const BoundaryMode mode : {BoundaryMode::Wrap, BoundaryMode::Bounce}) {
            world.boundary = mode;
            for (const float maximumRadius : {0.17F, 0.61F, 1.3F, 3.17F, 8.0F, 50.0F}) {
                checkGrid(agents, world, maximumRadius,
                          {0.0F, maximumRadius * 0.1F, maximumRadius * 0.7F, maximumRadius});
            }
        }
    }
}

void extremeGridScales() {
    const float tiny = std::numeric_limits<float>::denorm_min();
    std::vector<Agent> agents(3);
    agents[0].position = glm::vec3(0.0F);
    agents[1].position = {tiny, 0.0F, 0.0F};
    agents[2].position = {tiny * 2.0F, 0.0F, 0.0F};
    checkGrid(agents, World{}, tiny, {0.0F, tiny});
    const World tinyWorld{glm::vec3(0.0F), glm::vec3(tiny * 4.0F)};
    checkGrid(agents, tinyWorld, tiny, {0.0F, tiny});
    const float large = 1.0e30F;
    const World largeWorld{glm::vec3(0.0F), glm::vec3(large)};
    agents[1].position = glm::vec3(large * 0.25F);
    agents[2].position = glm::vec3(large * 0.75F);
    checkGrid(agents, largeWorld, std::numeric_limits<float>::max(),
              {large * 0.25F, large, std::numeric_limits<float>::max()});
    const World asymmetric{glm::vec3{-large}, glm::vec3{1.0F}};
    agents[0].position = glm::vec3{0.0F};
    agents[1].position = asymmetric.minimum;
    agents[2].position = glm::vec3{0.5F};
    checkGrid(agents, asymmetric, large, {1.0F, large});
}

void invalidInputsAndRebuild() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const World world : {World{glm::vec3(0.0F), glm::vec3(0.0F)},
                             World{glm::vec3(1.0F), glm::vec3(0.0F)},
                             World{glm::vec3(nan), glm::vec3(1.0F)},
                             World{glm::vec3(0.0F), glm::vec3(infinity)},
                             World{glm::vec3(-std::numeric_limits<float>::max()),
                                   glm::vec3(std::numeric_limits<float>::max())},
                             World{glm::vec3(0.0F), glm::vec3(1.0F), static_cast<BoundaryMode>(42)}}) {
        requireThrows<std::invalid_argument>([&] { world.validate(); }, "Invalid world must be rejected");
    }
    const World world;
    glm::vec3 position{0.0F};
    glm::vec3 velocity{nan};
    requireThrows<std::invalid_argument>([&] { world.applyBoundary(position, velocity); },
                                        "Nonfinite boundary velocity must be rejected");
    requireThrows<std::invalid_argument>([&] { (void)world.displacement(position, glm::vec3(infinity)); },
                                        "Nonfinite displacement inputs must be rejected");
    SpatialHash grid;
    std::vector<Neighbor> output;
    requireThrows<std::logic_error>([&] { grid.query(0, 1.0F, output); },
                                   "Query before rebuild must be rejected");
    std::vector<Agent> agents(2);
    agents[1].position.x = 0.25F;
    grid.rebuild(agents, world, 1.0F);
    for (const float radius : {-1.0F, nan, infinity, 1.01F}) {
        requireThrows<std::invalid_argument>([&] { grid.query(0, radius, output); },
                                            "Invalid query radius must be rejected");
    }
    for (const float radius : {-1.0F, nan, infinity}) {
        requireThrows<std::invalid_argument>([&] { grid.rebuild(agents, world, radius); },
                                            "Invalid maximum radius must be rejected");
    }
    std::vector<Agent> invalid(1);
    for (const glm::vec3 value : {glm::vec3(nan), world.maximum, world.minimum - glm::vec3(1.0F)}) {
        invalid[0].position = value;
        requireThrows<std::invalid_argument>([&] { grid.rebuild(invalid, world, 1.0F); },
                                            "Invalid positions must be rejected");
        grid.query(0, 1.0F, output);
        require(output.size() == 1 && output[0].agent == &agents[1],
                "Failed rebuild must preserve the previous valid grid");
    }
    agents[1].position.x = 10.0F;
    grid.rebuild(agents, world, 1.0F);
    grid.query(0, 1.0F, output);
    require(output.empty(), "Rebuild must discard the old spatial assignments");
    requireThrows<std::out_of_range>([&] { grid.query(agents.size(), 1.0F, output); },
                                    "Invalid self index must be rejected");
}

} // namespace

int main() {
    try {
        wrappingWorld();
        bouncingWorld();
        emptySingletonAndEdges();
        randomizedBruteForce();
        extremeGridScales();
        invalidInputsAndRebuild();
        std::cout << "World and spatial hash tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "World and spatial hash test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
