#include "scenarios/boids/Alignment.h"
#include "scenarios/boids/Cohesion.h"
#include "scenarios/boids/Separation.h"

#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using emergences::scenarios::boids::Alignment;
using emergences::scenarios::boids::Cohesion;
using emergences::scenarios::boids::Separation;
using emergences::sim::Agent;
using emergences::sim::BoundaryMode;
using emergences::sim::Neighbor;
using emergences::sim::Rule;
using emergences::sim::World;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(const glm::vec3& actual, const glm::vec3& expected, float tolerance = 0.0001F) {
    return glm::length(actual - expected) < tolerance;
}

template <typename Action>
void requireInvalidArgument(Action action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid rule parameters must throw std::invalid_argument");
}

Neighbor neighborOf(const Agent& self, const Agent& neighbor, const World& world) {
    const glm::dvec3 displacement{world.displacement(self.position, neighbor.position)};
    return {&neighbor, glm::dot(displacement, displacement)};
}

void defaultsAndDirections() {
    const World world;
    Agent self;
    self.velocity = {1.0F, 0.0F, 0.0F};
    Agent first;
    first.position = {1.0F, 0.0F, 0.0F};
    first.velocity = {0.0F, 2.0F, 0.0F};
    Agent second;
    second.position = {0.0F, 2.0F, 0.0F};
    second.velocity = {0.0F, 0.0F, 2.0F};
    const std::array neighbors{neighborOf(self, first, world), neighborOf(self, second, world)};

    Alignment alignment;
    Cohesion cohesion;
    Separation separation;
    require(alignment.name() == "Alignment" && alignment.weight() == 1.0F && alignment.radius() == 5.0F,
            "Alignment defaults must match the scenario");
    require(cohesion.name() == "Cohesion" && cohesion.weight() == 0.6F && cohesion.radius() == 5.0F,
            "Cohesion defaults must match the scenario");
    require(separation.name() == "Separation" && separation.weight() == 1.5F && separation.radius() == 2.0F,
            "Separation defaults must match the scenario");
    require(near(alignment.steer(self, neighbors, world), {-1.0F, 1.0F, 1.0F}),
            "Alignment must steer toward average neighbor velocity relative to self velocity");
    require(near(cohesion.steer(self, neighbors, world), {0.5F, 1.0F, 0.0F}),
            "Cohesion must steer toward the local center");
    require(near(separation.steer(self, neighbors, world), {-0.5F, -0.25F, 0.0F}),
            "Separation must average inverse-distance vectors away from neighbors");

    const std::array<Rule*, 3> rules{&alignment, &cohesion, &separation};
    for (Rule* rule : rules) {
        const glm::vec3 unweighted = rule->steer(self, neighbors, world);
        for (const float weight : {0.0F, 0.25F, 3.0F}) {
            rule->setWeight(weight);
            require(near(rule->steer(self, neighbors, world), unweighted),
                    "Rule steering must leave weight application to the backend");
        }
    }
}

void independentRadii() {
    const World world;
    const Agent self;
    Agent close;
    close.position = {1.0F, 0.0F, 0.0F};
    close.velocity = {1.0F, 0.0F, 0.0F};
    Agent distant;
    distant.position = {0.0F, 4.0F, 0.0F};
    distant.velocity = {0.0F, 2.0F, 0.0F};
    const std::array neighbors{neighborOf(self, close, world), neighborOf(self, distant, world)};
    Alignment alignment(1.0F, 1.0F);
    Cohesion cohesion(1.0F, 4.0F);
    Separation separation(1.0F, 0.5F);
    require(near(alignment.steer(self, neighbors, world), {1.0F, 0.0F, 0.0F}),
            "Alignment must include its radius boundary and exclude more distant neighbors");
    require(near(cohesion.steer(self, neighbors, world), {0.5F, 2.0F, 0.0F}),
            "Cohesion must use its own radius, including its boundary");
    require(near(separation.steer(self, neighbors, world), glm::vec3(0.0F)),
            "Separation must independently reject neighbors outside its radius");
    alignment.setRadius(4.0F);
    cohesion.setRadius(1.0F);
    separation.setRadius(1.0F);
    require(near(alignment.steer(self, neighbors, world), {0.5F, 1.0F, 0.0F}),
            "Alignment radius changes must take effect immediately");
    require(near(cohesion.steer(self, neighbors, world), {1.0F, 0.0F, 0.0F}),
            "Cohesion radius changes must take effect immediately");
    require(near(separation.steer(self, neighbors, world), {-1.0F, 0.0F, 0.0F}),
            "Separation radius changes must take effect immediately");
}

void periodicSeams() {
    World world;
    world.minimum = glm::vec3(-5.0F);
    world.maximum = glm::vec3(5.0F);
    Agent self;
    self.position = {4.75F, 0.0F, 0.0F};
    self.velocity = {1.0F, 0.0F, 0.0F};
    Agent other;
    other.position = {-4.75F, 0.0F, 0.0F};
    other.velocity = {0.0F, 1.0F, 0.0F};
    const std::array wrappedNeighbors{neighborOf(self, other, world)};
    Alignment alignment(1.0F, 1.0F);
    Cohesion cohesion(1.0F, 1.0F);
    Separation separation(1.0F, 1.0F);
    require(near(alignment.steer(self, wrappedNeighbors, world), {-1.0F, 1.0F, 0.0F}),
            "Alignment must see neighbors across a periodic seam");
    require(near(cohesion.steer(self, wrappedNeighbors, world), {0.5F, 0.0F, 0.0F}),
            "Cohesion must approach through the nearest periodic seam");
    require(near(separation.steer(self, wrappedNeighbors, world), {-2.0F, 0.0F, 0.0F}),
            "Separation must repel away from the shortest periodic displacement");
    const std::array reverseNeighbors{neighborOf(other, self, world)};
    require(near(cohesion.steer(other, reverseNeighbors, world), {-0.5F, 0.0F, 0.0F}),
            "Cohesion must cross the reverse periodic seam correctly");
    require(near(separation.steer(other, reverseNeighbors, world), {2.0F, 0.0F, 0.0F}),
            "Separation must cross the reverse periodic seam correctly");

    world.boundary = BoundaryMode::Bounce;
    const std::array boundedNeighbors{neighborOf(self, other, world)};
    const std::array<Rule*, 3> rules{&alignment, &cohesion, &separation};
    for (const Rule* rule : rules) {
        require(near(rule->steer(self, boundedNeighbors, world), glm::vec3(0.0F)),
                "A bounded world must not create neighbors through its walls");
    }
}

void fractionalRadiusBoundary() {
    const World world;
    const Agent self;
    constexpr float radius = 0.1F;
    Agent boundary;
    boundary.position = {radius, 0.0F, 0.0F};
    boundary.velocity = {1.0F, 0.0F, 0.0F};
    Agent outside;
    outside.position = {std::nextafter(radius, 1.0F), 0.0F, 0.0F};
    outside.velocity = {0.0F, 1.0F, 0.0F};
    const std::array neighbors{neighborOf(self, boundary, world), neighborOf(self, outside, world)};
    const Alignment alignment(1.0F, radius);
    const Cohesion cohesion(1.0F, radius);
    const Separation separation(1.0F, radius);
    // Squaring 0.1F and narrowing the result to float rounds upward. Neighbor
    // distances must retain double precision through every independent filter.
    require(near(alignment.steer(self, neighbors, world), {1.0F, 0.0F, 0.0F}),
            "Alignment must include a fractional radius boundary and reject its next float");
    require(near(cohesion.steer(self, neighbors, world), {radius, 0.0F, 0.0F}),
            "Cohesion must retain a neighbor exactly on a fractional radius boundary");
    require(near(separation.steer(self, neighbors, world), {-10.0F, 0.0F, 0.0F}),
            "Separation must retain a neighbor exactly on a fractional radius boundary");
}

void emptyAndOverlappingNeighborhoods() {
    const World world;
    Agent self;
    self.velocity = {1.0F, 0.0F, 0.0F};
    Agent overlap;
    overlap.velocity = {0.0F, 1.0F, 0.0F};
    const std::array overlappingNeighbors{neighborOf(self, overlap, world)};
    Alignment alignment;
    Cohesion cohesion;
    Separation separation;
    const std::array<Rule*, 3> rules{&alignment, &cohesion, &separation};
    for (const Rule* rule : rules) {
        require(near(rule->steer(self, {}, world), glm::vec3(0.0F)),
                "An empty neighborhood must have zero steering");
    }
    require(near(alignment.steer(self, overlappingNeighbors, world), {-1.0F, 1.0F, 0.0F}),
            "Overlapping neighbors may still align velocities");
    require(near(cohesion.steer(self, overlappingNeighbors, world), glm::vec3(0.0F)),
            "An overlapping center must produce zero cohesion");
    require(near(separation.steer(self, overlappingNeighbors, world), glm::vec3(0.0F)),
            "Exact overlaps have no separation direction and must remain finite");

    Agent nearby;
    nearby.position = {0.0001F, 0.0F, 0.0F};
    const std::array nearOverlapNeighbors{neighborOf(self, nearby, world), neighborOf(self, overlap, world)};
    require(near(separation.steer(self, nearOverlapNeighbors, world), {-100.0F, 0.0F, 0.0F}, 0.001F),
            "Near overlaps must use the epsilon floor, with exact overlaps excluded from the average");
    for (Rule* rule : rules) {
        rule->setRadius(0.0F);
        require(near(rule->steer(self, overlappingNeighbors, world), glm::vec3(0.0F)),
                "Zero radius must disable a neighborhood even for exact overlaps");
    }
}

template <typename RuleType>
void invalidParameters() {
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    RuleType rule(2.0F, 3.0F);
    for (const float invalid : {-1.0F, infinity, -infinity, nan}) {
        requireInvalidArgument([invalid] { const RuleType invalidWeight(invalid, 1.0F); });
        requireInvalidArgument([invalid] { const RuleType invalidRadius(1.0F, invalid); });
        requireInvalidArgument([&rule, invalid] { rule.setWeight(invalid); });
        requireInvalidArgument([&rule, invalid] { rule.setRadius(invalid); });
        require(rule.weight() == 2.0F && rule.radius() == 3.0F,
                "A rejected setter must preserve valid rule parameters");
    }
    const RuleType disabled(0.0F, 0.0F);
    require(disabled.weight() == 0.0F && disabled.radius() == 0.0F,
            "Zero rule parameters must be accepted");
    const float maximum = std::numeric_limits<float>::max();
    rule.setWeight(maximum);
    rule.setRadius(maximum);
    require(rule.weight() == maximum && rule.radius() == maximum,
            "Finite nonnegative rule parameters must be accepted");
}

} // namespace

int main() {
    try {
        defaultsAndDirections();
        independentRadii();
        periodicSeams();
        fractionalRadiusBoundary();
        emptyAndOverlappingNeighborhoods();
        invalidParameters<Alignment>();
        invalidParameters<Cohesion>();
        invalidParameters<Separation>();
        std::cout << "Boids rule tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Boids rule test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
