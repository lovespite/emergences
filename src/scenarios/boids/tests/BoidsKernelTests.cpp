#include "scenarios/boids/Alignment.h"
#include "scenarios/boids/BoidsRuleKernel.h"
#include "scenarios/boids/Cohesion.h"
#include "scenarios/boids/Separation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {

using namespace emergences;
using scenarios::boids::Alignment;
using scenarios::boids::BoidsRuleKernel;
using scenarios::boids::Cohesion;
using scenarios::boids::Separation;
using sim::Agent;
using sim::Neighbor;
using sim::Rule;
using sim::World;
using Rules = std::vector<std::unique_ptr<Rule>>;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireSameBits(const glm::dvec3& actual, const glm::dvec3& expected) {
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        require(std::bit_cast<std::uint64_t>(actual[axis]) ==
                    std::bit_cast<std::uint64_t>(expected[axis]),
                "Fused acceleration must preserve every component's bits");
    }
}

template <typename Action>
void requireInvalidArgument(Action action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Nonfinite unweighted steering must throw");
}

struct Parameters {
    std::array<float, 3> weights{2.0F, 0.45F, 0.11F};
    std::array<float, 3> radii{2.0F, 5.0F, 6.0F};
};

Rules makeRules(const std::array<int, 3>& order, const Parameters& parameters = {}) {
    Rules rules;
    for (const int kind : order) {
        const float weight = parameters.weights[kind];
        const float radius = parameters.radii[kind];
        switch (kind) {
        case 0: rules.push_back(std::make_unique<Separation>(weight, radius)); break;
        case 1: rules.push_back(std::make_unique<Alignment>(weight, radius)); break;
        case 2: rules.push_back(std::make_unique<Cohesion>(weight, radius)); break;
        default: throw std::runtime_error("Invalid test rule kind");
        }
    }
    return rules;
}

// This oracle calls the original three implementations. Passing the same
// ordered input lets it expose changes to accumulation precision or ordering.
glm::dvec3 separateAcceleration(const Rules& rules, const Agent& self,
    std::span<const Neighbor> neighbors, const World& world) {
    glm::dvec3 result{0.0};
    for (const auto& rule : rules) {
        if (rule->weight() == 0.0F) {
            continue;
        }
        const glm::vec3 steering = rule->steer(self, neighbors, world);
        if (!std::isfinite(steering.x) || !std::isfinite(steering.y) || !std::isfinite(steering.z)) {
            throw std::invalid_argument("Rule returned a nonfinite acceleration");
        }
        result += glm::dvec3{steering} * static_cast<double>(rule->weight());
    }
    return result;
}

std::vector<Neighbor> neighborsFor(const std::vector<Agent>& agents, std::size_t self,
    const World& world, bool cached) {
    std::vector<Neighbor> neighbors;
    for (std::size_t index = 0; index < agents.size(); ++index) {
        if (index == self) {
            continue;
        }
        const glm::vec3 displacement = world.displacement(agents[self].position, agents[index].position);
        const glm::dvec3 wide{displacement};
        Neighbor neighbor{&agents[index], glm::dot(wide, wide)};
        if (cached) {
            neighbor.displacement = displacement;
        }
        neighbors.push_back(neighbor);
    }
    return neighbors;
}

std::vector<Agent> fixture() {
    const std::array positions{
        glm::vec3{0.0F}, glm::vec3{0.0F}, glm::vec3{0.0001F, 0.0F, 0.0F},
        glm::vec3{0.1F, 0.0F, 0.0F},
        glm::vec3{std::nextafter(0.1F, 1.0F), 0.0F, 0.0F},
        glm::vec3{2.0F, 0.0F, 0.0F},
        glm::vec3{std::nextafter(5.0F, 0.0F), 0.0F, 0.0F},
        glm::vec3{-5.0F, 0.0F, 0.0F},
        glm::vec3{-4.75F, 0.0F, 0.0F}, glm::vec3{4.75F, 0.0F, 0.0F},
        glm::vec3{2.5F, 1.0F, 0.0F}, glm::vec3{-2.5F, -1.0F, 0.0F},
    };
    const std::array velocities{
        glm::vec3{1.0e8F, 0.3F, -7.0F}, glm::vec3{-1.0e8F, -0.1F, 11.0F},
        glm::vec3{0.125F, 0.2F, -0.007F}, glm::vec3{-0.333F, -0.9F, 0.003F},
    };
    std::vector<Agent> agents(positions.size());
    for (std::size_t index = 0; index < agents.size(); ++index) {
        agents[index].position = positions[index];
        agents[index].velocity = velocities[index % velocities.size()];
    }
    return agents;
}

void matchesSeparateRules() {
    const std::array parameterSets{
        Parameters{},
        Parameters{{0.0F, 0.0F, 0.0F}, {2.0F, 5.0F, 6.0F}},
        Parameters{{0.0F, 0.45F, 0.11F}, {2.0F, 0.0F, 6.0F}},
        Parameters{{2.0F, 0.0F, 0.11F}, {0.1F, 5.0F, 0.0F}},
        Parameters{{0.0001F, 7.0F, 0.33F}, {5.0F, 0.1F, 2.0F}},
        Parameters{{2.0F, 0.45F, 0.11F}, {0.1F, 0.1F, 0.1F}},
        Parameters{{0.5F, 1.0F, 0.25F}, {1.0e20F, 1.0e20F, 1.0e20F}},
    };
    const auto agents = fixture();
    for (const auto boundary : {sim::BoundaryMode::Wrap, sim::BoundaryMode::Bounce}) {
        const World world{glm::vec3{-5.0F}, glm::vec3{5.0F}, boundary};
        std::array order{0, 1, 2};
        do {
            for (const auto& parameters : parameterSets) {
                auto rules = makeRules(order, parameters);
                BoidsRuleKernel kernel;
                require(kernel.prepare(rules), "Every permutation of the three rules must fuse");
                for (std::size_t self = 0; self < agents.size(); ++self) {
                    requireSameBits(kernel.acceleration(agents[self], {}, world),
                                    separateAcceleration(rules, agents[self], {}, world));
                    for (const bool cached : {false, true}) {
                        auto neighbors = neighborsFor(agents, self, world, cached);
                        requireSameBits(kernel.acceleration(agents[self], neighbors, world),
                                        separateAcceleration(rules, agents[self], neighbors, world));
                        std::reverse(neighbors.begin(), neighbors.end());
                        requireSameBits(kernel.acceleration(agents[self], neighbors, world),
                                        separateAcceleration(rules, agents[self], neighbors, world));
                    }
                }
            }
        } while (std::next_permutation(order.begin(), order.end()));
    }
}

void parameterSnapshotAndRefresh() {
    auto rules = makeRules({0, 1, 2});
    BoidsRuleKernel kernel;
    const World world;
    const auto agents = fixture();
    const auto neighbors = neighborsFor(agents, 0, world, true);
    require(kernel.prepare(rules), "Default rules must fuse");
    const auto original = kernel.acceleration(agents[0], neighbors, world);
    rules[0]->setRadius(0.0F);
    rules[1]->setWeight(0.0F);
    rules[2]->setRadius(0.1F);
    rules[2]->setWeight(2.0F);
    requireSameBits(kernel.acceleration(agents[0], neighbors, world), original);
    require(kernel.prepare(rules), "Parameter changes must retain fusion support");
    const auto refreshed = kernel.acceleration(agents[0], neighbors, world);
    requireSameBits(refreshed, separateAcceleration(rules, agents[0], neighbors, world));
    require(refreshed != original, "A new prepare must refresh changed parameters");
    rules[0]->setRadius(5.0F);
    rules[1]->setWeight(1.5F);
    std::swap(rules[0], rules[2]);
    require(kernel.prepare(rules), "Reordering rules between steps must remain supported");
    requireSameBits(kernel.acceleration(agents[0], neighbors, world),
                    separateAcceleration(rules, agents[0], neighbors, world));
}

class CustomRule final : public Rule {
public:
    glm::vec3 steer(const Agent&, std::span<const Neighbor>, const World&) const override { return {}; }
    std::string_view name() const noexcept override { return "Alignment"; }
    float weight() const noexcept override { return 1.0F; }
    float radius() const noexcept override { return 5.0F; }
    void setWeight(float) override {}
    void setRadius(float) override {}
};

void unsupportedCollectionsFallBack() {
    BoidsRuleKernel kernel;
    auto rules = makeRules({0, 1, 2});
    require(kernel.prepare(rules), "Default rules must prepare before fallback checks");
    rules[1] = std::make_unique<CustomRule>();
    require(!kernel.prepare(rules), "A custom rule must fall back even if it borrows a known name");
    rules = makeRules({0, 0, 2});
    require(!kernel.prepare(rules), "Duplicate rule types must fall back");
    rules = makeRules({0, 1, 2});
    rules.pop_back();
    require(!kernel.prepare(rules), "Missing rules must fall back");
    rules = makeRules({0, 1, 2});
    rules.push_back(std::make_unique<Alignment>());
    require(!kernel.prepare(rules), "Additional rules must fall back");
    rules.clear();
    require(!kernel.prepare(rules), "An empty collection must fall back");
    rules = makeRules({0, 1, 2});
    rules[1].reset();
    require(!kernel.prepare(rules), "Null rules cannot prepare a kernel");
    rules = makeRules({2, 0, 1});
    require(kernel.prepare(rules), "A later supported collection must recover after fallback");
}

void nonfiniteSteeringIsRejectedBeforeWeighting() {
    const World world;
    Agent self;
    Agent other;
    other.velocity = glm::vec3{std::numeric_limits<float>::max()};
    const std::array neighbors{Neighbor{&other, 0.0, glm::vec3{0.0F}},
                               Neighbor{&other, 0.0, glm::vec3{0.0F}}};
    auto rules = makeRules({0, 1, 2});
    // A small weight cannot make an overflowing float accumulator acceptable.
    rules[1]->setWeight(std::numeric_limits<float>::min());
    BoidsRuleKernel kernel;
    require(kernel.prepare(rules), "Finite small weights must prepare");
    requireInvalidArgument([&] { (void) separateAcceleration(rules, self, neighbors, world); });
    requireInvalidArgument([&] { (void) kernel.acceleration(self, neighbors, world); });
    rules[1]->setWeight(0.0F);
    require(kernel.prepare(rules), "Zero weights must prepare");
    requireSameBits(kernel.acceleration(self, neighbors, world),
                    separateAcceleration(rules, self, neighbors, world));
    rules[1]->setWeight(1.0F);
    rules[1]->setRadius(0.0F);
    require(kernel.prepare(rules), "Zero radii must prepare");
    requireSameBits(kernel.acceleration(self, neighbors, world),
                    separateAcceleration(rules, self, neighbors, world));
}

} // namespace

int main() {
    try {
        matchesSeparateRules();
        parameterSnapshotAndRefresh();
        unsupportedCollectionsFallBack();
        nonfiniteSteeringIsRejectedBeforeWeighting();
        std::cout << "Boids kernel tests passed (exact components, parameters and fallback)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Boids kernel test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
