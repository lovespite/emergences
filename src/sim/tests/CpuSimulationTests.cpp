#include "sim/cpu/CpuSimulation.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using emergences::sim::Agent;
using emergences::sim::Backend;
using emergences::sim::BoundaryMode;
using emergences::sim::Intervention;
using emergences::sim::Neighbor;
using emergences::sim::Rule;
using emergences::sim::World;
using emergences::sim::cpu::CpuSimulation;
using emergences::sim::cpu::MotionLimits;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.0001F) {
    return glm::length(a - b) < tolerance;
}

bool sameAgent(const Agent& a, const Agent& b) {
    return a.position == b.position && a.size == b.size && a.velocity == b.velocity &&
           a.reserved == b.reserved && a.color == b.color && a.id == b.id;
}

bool sameAgents(std::span<const Agent> a, std::span<const Agent> b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), sameAgent);
}

bool sameWorld(const World& a, const World& b) {
    return a.minimum == b.minimum && a.maximum == b.maximum && a.boundary == b.boundary;
}

std::vector<Agent> snapshot(const CpuSimulation& simulation) {
    return {simulation.agents().begin(), simulation.agents().end()};
}

template<class Exception = std::invalid_argument, class Function>
void requireThrows(Function&& function, const char* message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

Agent agent(glm::vec3 position, glm::vec3 velocity = glm::vec3(0.0F), std::uint32_t id = 0) {
    Agent result;
    result.position = position;
    result.velocity = velocity;
    result.id = id;
    return result;
}

World roomyWorld() {
    return {{-100.0F, -100.0F, -100.0F}, {100.0F, 100.0F, 100.0F}, BoundaryMode::Wrap};
}

void allowMotion(CpuSimulation& simulation) {
    simulation.setMotionLimits({0.0F, 100.0F, 100.0F});
}

class TestRule final : public Rule {
public:
    using Callback = std::function<glm::vec3(const Agent&, std::span<const Neighbor>, const World&)>;

    TestRule(float radius, float weight, Callback callback)
        : radius_(radius), weight_(weight), callback_(std::move(callback)) {}

    glm::vec3 steer(const Agent& self, std::span<const Neighbor> neighbors,
                    const World& world) const override {
        ++calls;
        return callback_(self, neighbors, world);
    }
    std::string_view name() const noexcept override { return "Test rule"; }
    float weight() const noexcept override { return weight_; }
    float radius() const noexcept override { return radius_; }
    void setWeight(float value) override { weight_ = value; }
    void setRadius(float value) override { radius_ = value; }

    mutable std::size_t calls{0};

private:
    float radius_;
    float weight_;
    Callback callback_;
};

TestRule& addRule(CpuSimulation& simulation, float radius, float weight, TestRule::Callback callback) {
    auto rule = std::make_unique<TestRule>(radius, weight, std::move(callback));
    TestRule& reference = *rule;
    simulation.rules().push_back(std::move(rule));
    return reference;
}

class TestIntervention final : public Intervention {
public:
    using Callback = std::function<void(float, std::vector<Agent>&, World&)>;
    explicit TestIntervention(Callback callback) : callback_(std::move(callback)) {}
    void apply(float dt, std::vector<Agent>& agents, World& world) override {
        callback_(dt, agents, world);
    }
    std::string_view name() const noexcept override { return "Test intervention"; }

private:
    Callback callback_;
};

void addIntervention(CpuSimulation& simulation, TestIntervention::Callback callback) {
    simulation.interventions().push_back(std::make_unique<TestIntervention>(std::move(callback)));
}

void synchronousIntegration() {
    CpuSimulation simulation(0, roomyWorld());
    allowMotion(simulation);
    // The second agent must observe the first agent's original velocity, even
    // though the first agent's next velocity has already been calculated.
    const std::array input{
        agent({-1.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}, 7),
        agent({1.0F, 0.0F, 0.0F}, {-2.0F, 0.0F, 0.0F}, 7)};
    simulation.setAgents(input);
    addRule(simulation, 10.0F, 1.0F,
            [](const Agent& self, std::span<const Neighbor> neighbors, const World&) {
                require(neighbors.size() == 1, "Different agents with equal IDs must remain neighbors");
                require(neighbors.front().agent != &self, "An agent must not be its own neighbor");
                require(std::abs(neighbors.front().distanceSquared - 4.0F) < 0.0001F,
                        "Rules must observe old-state neighbor distances");
                return neighbors.front().agent->velocity;
            });
    simulation.step(0.5F);
    require(near(simulation.agents()[0].velocity, {1.0F, 0.0F, 0.0F}) &&
                near(simulation.agents()[1].velocity, {-1.0F, 0.0F, 0.0F}),
            "All rule evaluations must use one shared old-state snapshot");
    require(near(simulation.agents()[0].position, {-0.5F, 0.0F, 0.0F}) &&
                near(simulation.agents()[1].position, {0.5F, 0.0F, 0.0F}),
            "Position integration must use the newly integrated velocity");
}

void ruleRadiiAndWeights() {
    CpuSimulation simulation(0, roomyWorld());
    allowMotion(simulation);
    const std::array input{agent({0.0F, 0.0F, 0.0F}), agent({2.0F, 0.0F, 0.0F}),
                           agent({5.0F, 0.0F, 0.0F})};
    simulation.setAgents(input);
    auto& counting = addRule(simulation, 3.1F, 2.0F,
                            [](const Agent&, std::span<const Neighbor> neighbors, const World&) {
                                return glm::vec3(static_cast<float>(neighbors.size()), 0.0F, 0.0F);
                            });
    auto& constant = addRule(simulation, 1.0F, 3.0F,
                            [](const Agent&, std::span<const Neighbor> neighbors, const World&) {
                                require(neighbors.empty(), "Each rule must receive its own neighborhood");
                                return glm::vec3(0.0F, 1.0F, 0.0F);
                            });
    addRule(simulation, 0.0F, 4.0F,
            [](const Agent&, std::span<const Neighbor> neighbors, const World&) {
                require(neighbors.empty(), "Zero radius must produce an empty neighborhood");
                return glm::vec3(0.0F, 0.0F, 1.0F);
            });
    auto& disabled = addRule(simulation, 100.0F, 0.0F,
                            [](const Agent&, std::span<const Neighbor>, const World&) -> glm::vec3 {
                                throw std::runtime_error("A zero-weight rule must not be evaluated");
                            });
    simulation.step(0.1F);
    require(near(simulation.agents()[0].velocity, {0.2F, 0.3F, 0.4F}) &&
                near(simulation.agents()[1].velocity, {0.4F, 0.3F, 0.4F}) &&
                near(simulation.agents()[2].velocity, {0.2F, 0.3F, 0.4F}),
            "Rule weights must scale the independently filtered steering vectors");
    require(constant.calls == 3 && disabled.calls == 0,
            "Enabled rules must run with empty neighborhoods; disabled rules must not run");

    counting.setRadius(0.5F);
    simulation.setAgents(input);
    simulation.step(0.1F);
    require(near(simulation.agents()[1].velocity, {0.0F, 0.3F, 0.4F}),
            "Shrinking a rule radius must take effect on the next step");
    counting.setRadius(6.0F);
    counting.setWeight(5.0F);
    simulation.setAgents(input);
    simulation.step(0.1F);
    for (const Agent& current : simulation.agents()) {
        require(near(current.velocity, {1.0F, 0.3F, 0.4F}),
                "Growing a rule radius and changing its weight must take effect immediately");
    }
}

void motionLimits() {
    CpuSimulation simulation(0, roomyWorld());
    simulation.setMotionLimits({0.0F, 100.0F, 5.0F});
    const std::array input{agent({0.0F, 0.0F, 0.0F})};
    simulation.setAgents(input);
    addRule(simulation, 0.0F, 1.0F,
            [](const Agent&, std::span<const Neighbor>, const World&) { return glm::vec3(10, 0, 0); });
    addRule(simulation, 0.0F, 1.0F,
            [](const Agent&, std::span<const Neighbor>, const World&) { return glm::vec3(0, 10, 0); });
    simulation.step(0.4F);
    const float diagonal = std::sqrt(2.0F);
    require(near(simulation.agents()[0].velocity, {diagonal, diagonal, 0.0F}),
            "Acceleration limit must clamp the sum of weighted rules, preserving direction");
    simulation.setMotionLimits({0.0F, 3.0F, 5.0F});
    simulation.setAgents(input);
    simulation.step(1.0F);
    require(std::abs(glm::length(simulation.agents()[0].velocity) - 3.0F) < 0.0001F,
            "Integrated speed must respect the maximum speed");
    require(near(simulation.agents()[0].position, simulation.agents()[0].velocity),
            "Position must use the final clamped velocity");

    CpuSimulation stopped(0, roomyWorld(), 42);
    CpuSimulation repeated(0, roomyWorld(), 42);
    stopped.setMotionLimits({2.0F, 4.0F, 0.0F});
    repeated.setMotionLimits({2.0F, 4.0F, 0.0F});
    stopped.setAgents(input);
    repeated.setAgents(input);
    require(sameAgents(stopped.agents(), repeated.agents()),
            "The minimum-speed direction of a stopped agent must be deterministic");
    require(std::abs(glm::length(stopped.agents()[0].velocity) - 2.0F) < 0.0001F,
            "A stopped agent must receive a finite minimum-speed velocity");
    stopped.step(0.25F);
    require(near(stopped.agents()[0].position, repeated.agents()[0].velocity * 0.25F),
            "Minimum-speed initialization must produce usable motion");

    const std::array fast{agent({0.0F, 0.0F, 0.0F}, {30.0F, 40.0F, 0.0F})};
    repeated.setAgents(fast);
    require(near(repeated.agents()[0].velocity, {2.4F, 3.2F, 0.0F}),
            "Imported velocities must preserve direction while respecting speed limits");
}

void periodicNeighborhoods() {
    const World box{{0.0F, 0.0F, 0.0F}, {10.0F, 10.0F, 10.0F}, BoundaryMode::Wrap};
    CpuSimulation simulation(0, box);
    allowMotion(simulation);
    const std::array input{agent({9.8F, 5.0F, 5.0F}), agent({0.2F, 5.0F, 5.0F}),
                           agent({5.0F, 5.0F, 5.0F})};
    simulation.setAgents(input);
    auto& periodic = addRule(simulation, 0.5F, 1.0F,
                            [](const Agent& self, std::span<const Neighbor> neighbors, const World& world) {
                                if (self.position.x == 5.0F) {
                                    require(neighbors.empty(), "Distant agents must remain outside a periodic radius");
                                } else {
                                    require(neighbors.size() == 1,
                                            "Opposite-edge agents must be neighbors across a wrapping boundary");
                                    require(std::abs(neighbors[0].distanceSquared - 0.16F) < 0.0001F,
                                            "Periodic neighbor distance must use the nearest image");
                                    const glm::vec3 direction = world.displacement(self.position,
                                                                                  neighbors[0].agent->position);
                                    require(std::abs(direction.x) < 0.5F,
                                            "Rules must receive the periodic world used for neighbor lookup");
                                }
                                return glm::vec3(0.0F);
                            });
    simulation.step(0.1F);
    require(periodic.calls == 3, "Periodic lookup must still evaluate isolated agents");
}

void exactRadiusBoundary() {
    CpuSimulation simulation(0, roomyWorld());
    allowMotion(simulation);
    const std::array input{agent({0.0F, 0.0F, 0.0F}), agent({0.1F, 0.0F, 0.0F})};
    simulation.setAgents(input);
    addRule(simulation, 0.1F, 1.0F,
            [](const Agent&, std::span<const Neighbor> neighbors, const World&) {
                require(neighbors.size() == 1,
                        "A neighbor at exactly the rule radius must survive all filtering stages");
                return glm::vec3(1.0F, 0.0F, 0.0F);
            });
    simulation.step(0.1F);
    require(near(simulation.agents()[0].velocity, {0.1F, 0.0F, 0.0F}) &&
                near(simulation.agents()[1].velocity, {0.1F, 0.0F, 0.0F}),
            "Exact-radius neighbors must contribute steering on both sides");
}

void initializationAndResizing() {
    CpuSimulation simulation(8, roomyWorld(), 1234);
    CpuSimulation repeated(8, roomyWorld(), 1234);
    require(simulation.backend() == Backend::CPU && simulation.count() == 8,
            "CPU backend must expose its initialized records");
    const auto initial = snapshot(simulation);
    require(sameAgents(initial, repeated.agents()), "Equal seeds must produce identical initial states");
    for (const Agent& current : initial) {
        const float speed = glm::length(current.velocity);
        require(speed >= 0.9999F && speed <= 8.0001F, "Initial speeds must respect motion limits");
        require(current.position.x >= -100.0F && current.position.x < 100.0F &&
                    current.position.y >= -100.0F && current.position.y < 100.0F &&
                    current.position.z >= -100.0F && current.position.z < 100.0F,
                "Initial positions must lie in the world");
    }
    simulation.step(0.5F);
    const auto evolved = snapshot(simulation);
    simulation.setCount(3);
    require(sameAgents(simulation.agents(), std::span<const Agent>(evolved).first(3)),
            "Shrinking must preserve the retained prefix exactly");
    simulation.setCount(8);
    require(sameAgents(simulation.agents().first(3), std::span<const Agent>(evolved).first(3)),
            "Growing must preserve existing agents exactly");
    require(sameAgents(simulation.agents().subspan(3), std::span<const Agent>(initial).subspan(3)),
            "New records must be deterministic by seed and index");

    auto& retainedRule = addRule(simulation, 1.0F, 0.0F,
                                [](const Agent&, std::span<const Neighbor>, const World&) {
                                    return glm::vec3(0.0F);
                                });
    addIntervention(simulation, [](float, std::vector<Agent>&, World&) {});
    const Intervention* retainedIntervention = simulation.interventions().front().get();
    simulation.reset();
    require(sameAgents(simulation.agents(), initial), "Reset must reproduce the initial seed state");
    require(simulation.rules().front().get() == &retainedRule &&
                simulation.interventions().front().get() == retainedIntervention,
            "Reset must retain rule and intervention objects");

    const World small{{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}, BoundaryMode::Wrap};
    simulation.setWorld(small);
    simulation.setMotionLimits({0.5F, 0.5F, 2.0F});
    simulation.reset();
    require(simulation.count() == 8 && sameWorld(simulation.world(), small),
            "Reset must retain the current count and world");
    for (const Agent& current : simulation.agents()) {
        require(std::abs(glm::length(current.velocity) - 0.5F) < 0.0001F,
                "Reset must initialize using current speed limits");
        require(current.position.x >= -2.0F && current.position.x < 2.0F &&
                    current.position.y >= -2.0F && current.position.y < 2.0F &&
                    current.position.z >= -2.0F && current.position.z < 2.0F,
                "Reset must initialize inside the current world");
    }
    simulation.setCount(0);
    simulation.step(1.0F);
    simulation.reset();
    require(simulation.count() == 0 && simulation.agents().empty(), "An empty simulation must remain valid");
}

void boundariesAndInterventions() {
    const World box{{0.0F, 0.0F, 0.0F}, {10.0F, 10.0F, 10.0F}, BoundaryMode::Wrap};
    CpuSimulation simulation(0, box);
    allowMotion(simulation);
    const std::array outside{agent({-21.0F, 31.0F, 5.0F}, {-2.0F, 3.0F, 0.0F}, 99)};
    simulation.setAgents(outside);
    require(near(simulation.agents()[0].position, {9.0F, 1.0F, 5.0F}),
            "Imported positions must wrap across multiple world widths");
    require(simulation.agents()[0].id == 99, "Importing agents must preserve their IDs");
    simulation.step(10.0F);
    require(near(simulation.agents()[0].position, {9.0F, 1.0F, 5.0F}),
            "Integration must wrap across multiple world widths");

    World bouncing = box;
    bouncing.boundary = BoundaryMode::Bounce;
    simulation.setWorld(bouncing);
    const std::array moving{agent({9.0F, 1.0F, 5.0F}, {3.0F, -2.0F, 0.0F})};
    simulation.setAgents(moving);
    simulation.step(1.0F);
    require(near(simulation.agents()[0].position, {8.0F, 1.0F, 5.0F}) &&
                near(simulation.agents()[0].velocity, {-3.0F, 2.0F, 0.0F}),
            "Bounce must reflect the overshoot and the crossed velocity axes");

    simulation.setWorld(roomyWorld());
    simulation.setMotionLimits({0.0F, 3.0F, 100.0F});
    const std::array input{agent({0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F})};
    simulation.setAgents(input);
    std::vector<int> order;
    addIntervention(simulation, [&order](float dt, std::vector<Agent>& agents, World& world) {
        require(dt == 0.5F && near(agents[0].position, {0.5F, 0.0F, 0.0F}),
                "Interventions must receive the integrated state and actual dt");
        order.push_back(1);
        agents[0].position.x += 1.0F;
        world = {{0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}, BoundaryMode::Wrap};
    });
    addIntervention(simulation, [&order](float, std::vector<Agent>& agents, World& world) {
        require(agents[0].position.x == 1.5F && world.maximum.x == 2.0F,
                "Interventions must observe earlier intervention edits in registration order");
        order.push_back(2);
        agents[0].position.x *= 2.0F;
        agents[0].velocity = {10.0F, 0.0F, 0.0F};
    });
    simulation.step(0.5F);
    require(order == std::vector<int>({1, 2}), "Interventions must run in registration order");
    require(near(simulation.agents()[0].position, {1.0F, 0.0F, 0.0F}) &&
                near(simulation.agents()[0].velocity, {3.0F, 0.0F, 0.0F}) &&
                simulation.world().maximum.x == 2.0F,
            "Intervention edits must be normalized against their final world and motion limits");
}

void invalidInputsAndZeroStep() {
    CpuSimulation simulation(2, roomyWorld());
    const auto before = snapshot(simulation);
    const World oldWorld = simulation.world();
    const MotionLimits oldLimits = simulation.motionLimits();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float dt : {-1.0F, nan, infinity}) {
        requireThrows([&] { simulation.step(dt); }, "Invalid dt must be rejected");
        require(sameAgents(simulation.agents(), before), "Rejected dt must leave agents unchanged");
    }
    addIntervention(simulation, [](float, std::vector<Agent>&, World&) {
        throw std::runtime_error("Zero dt must not run interventions");
    });
    auto& unused = addRule(simulation, 1.0F, 1.0F,
                          [](const Agent&, std::span<const Neighbor>, const World&) -> glm::vec3 {
                              throw std::runtime_error("Zero dt must not run rules");
                          });
    simulation.step(0.0F);
    require(unused.calls == 0 && sameAgents(simulation.agents(), before) &&
                sameWorld(simulation.world(), oldWorld), "Zero dt must be a strict no-op");
    simulation.interventions().clear();
    simulation.rules().clear();

    for (std::size_t field = 0; field < 6; ++field) {
        auto invalid = before;
        if (field == 0) invalid[1].position.x = nan;
        if (field == 1) invalid[1].velocity.y = infinity;
        if (field == 2) invalid[1].color.z = nan;
        if (field == 3) invalid[1].size = -1.0F;
        if (field == 4) invalid[1].size = nan;
        if (field == 5) invalid[1].reserved = infinity;
        requireThrows([&] { simulation.setAgents(invalid); }, "Invalid agent records must be rejected");
        require(sameAgents(simulation.agents(), before), "Rejected imports must preserve all old agents");
    }
    for (const MotionLimits limits : {MotionLimits{-1, 8, 20}, MotionLimits{9, 8, 20},
                                      MotionLimits{1, 8, -1}, MotionLimits{nan, 8, 20},
                                      MotionLimits{1, infinity, 20}, MotionLimits{1, 8, nan}}) {
        requireThrows([&] { simulation.setMotionLimits(limits); }, "Invalid motion limits must be rejected");
        const MotionLimits actual = simulation.motionLimits();
        require(actual.minimumSpeed == oldLimits.minimumSpeed &&
                    actual.maximumSpeed == oldLimits.maximumSpeed &&
                    actual.maximumAcceleration == oldLimits.maximumAcceleration &&
                    sameAgents(simulation.agents(), before),
                "Rejected limits must preserve limits and agent state");
    }
    World invalidWorld = oldWorld;
    invalidWorld.minimum.x = invalidWorld.maximum.x;
    requireThrows([&] { simulation.setWorld(invalidWorld); }, "Zero-width worlds must be rejected");
    invalidWorld = oldWorld;
    invalidWorld.maximum.z = infinity;
    requireThrows([&] { simulation.setWorld(invalidWorld); }, "Nonfinite worlds must be rejected");
    require(sameWorld(simulation.world(), oldWorld) && sameAgents(simulation.agents(), before),
            "Rejected worlds must preserve the previous world and all agent state");

    simulation.rules().push_back(nullptr);
    requireThrows([&] { simulation.step(0.1F); }, "Null rules must be rejected");
    simulation.rules().clear();
    simulation.interventions().push_back(nullptr);
    requireThrows([&] { simulation.step(0.1F); }, "Null interventions must be rejected");
    simulation.interventions().clear();
    auto& invalidRule = addRule(simulation, 1.0F, 1.0F,
                               [](const Agent&, std::span<const Neighbor>, const World&) {
                                   return glm::vec3(0.0F);
                               });
    for (const float invalid : {-1.0F, nan, infinity}) {
        invalidRule.setRadius(invalid);
        requireThrows([&] { simulation.step(0.1F); }, "Invalid custom rule radii must be rejected");
        invalidRule.setRadius(1.0F);
        invalidRule.setWeight(invalid);
        requireThrows([&] { simulation.step(0.1F); }, "Invalid custom rule weights must be rejected");
        invalidRule.setWeight(1.0F);
    }
    require(sameAgents(simulation.agents(), before), "Invalid plugin configuration must not advance agents");
}

void failedStepsAreAtomic() {
    CpuSimulation simulation(3, roomyWorld());
    const auto before = snapshot(simulation);
    const World oldWorld = simulation.world();
    std::size_t evaluations = 0;
    addRule(simulation, 0.0F, 1.0F,
            [&evaluations](const Agent&, std::span<const Neighbor>, const World&) -> glm::vec3 {
                if (++evaluations == 3) {
                    throw std::runtime_error("Rule failed after two agents were integrated");
                }
                return glm::vec3(10.0F, 0.0F, 0.0F);
            });
    requireThrows<std::runtime_error>([&] { simulation.step(0.1F); }, "Rule exceptions must propagate");
    require(sameAgents(simulation.agents(), before), "Rule failure must not partially advance agents");
    simulation.rules().clear();

    addRule(simulation, 0.0F, 1.0F,
            [](const Agent&, std::span<const Neighbor>, const World&) {
                return glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F);
            });
    requireThrows<std::exception>([&] { simulation.step(0.1F); }, "Nonfinite rule output must be rejected");
    require(sameAgents(simulation.agents(), before), "Invalid rule output must not contaminate state");
    simulation.rules().clear();

    addIntervention(simulation, [](float, std::vector<Agent>& agents, World& world) {
        agents.front().position = glm::vec3(42.0F);
        world.maximum = glm::vec3(50.0F);
    });
    addIntervention(simulation, [](float, std::vector<Agent>&, World&) {
        throw std::runtime_error("Intervention failed after an earlier mutation");
    });
    requireThrows<std::runtime_error>([&] { simulation.step(0.1F); }, "Intervention exceptions must propagate");
    require(sameAgents(simulation.agents(), before) && sameWorld(simulation.world(), oldWorld),
            "A later failed intervention must roll back agent and world edits together");
    simulation.interventions().clear();

    addIntervention(simulation, [](float, std::vector<Agent>& agents, World&) {
        agents.back().velocity.x = std::numeric_limits<float>::infinity();
    });
    requireThrows<std::exception>([&] { simulation.step(0.1F); }, "Invalid intervention state must be rejected");
    require(sameAgents(simulation.agents(), before), "Invalid intervention state must not be committed");
    simulation.interventions().clear();
    addIntervention(simulation, [](float, std::vector<Agent>&, World& world) {
        world.minimum = world.maximum;
    });
    requireThrows<std::exception>([&] { simulation.step(0.1F); }, "Invalid intervention worlds must be rejected");
    require(sameAgents(simulation.agents(), before) && sameWorld(simulation.world(), oldWorld),
            "Invalid intervention worlds must not contaminate agent or world state");
    simulation.interventions().clear();
    CpuSimulation uninterrupted(3, roomyWorld());
    uninterrupted.step(0.1F);
    simulation.step(0.1F);
    require(sameAgents(simulation.agents(), uninterrupted.agents()),
            "A successful step after failed steps must resume from the last committed state");
}

} // namespace

int main() {
    try {
        synchronousIntegration();
        ruleRadiiAndWeights();
        motionLimits();
        periodicNeighborhoods();
        exactRadiusBoundary();
        initializationAndResizing();
        boundariesAndInterventions();
        invalidInputsAndZeroStep();
        failedStepsAreAtomic();
        std::cout << "CPU simulation tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "CPU simulation test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
