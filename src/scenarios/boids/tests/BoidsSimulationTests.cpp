#include "scenarios/boids/Alignment.h"
#include "scenarios/boids/Cohesion.h"
#include "scenarios/boids/Separation.h"
#include "sim/cpu/CpuSimulation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {

using namespace emergences;
using sim::Agent;
using sim::cpu::CpuSimulation;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void addRules(CpuSimulation& simulation) {
    simulation.rules().push_back(std::make_unique<scenarios::boids::Alignment>(1.0F, 4.0F));
    simulation.rules().push_back(std::make_unique<scenarios::boids::Cohesion>(0.6F, 5.5F));
    simulation.rules().push_back(std::make_unique<scenarios::boids::Separation>(1.5F, 2.0F));
}

// Deliberately all-pairs: this oracle does not reuse the spatial index or CPU
// stepping implementation. Compare complete multi-step trajectories, not counts.
std::vector<Agent> bruteStep(const std::vector<Agent>& old, CpuSimulation& simulation, float dt) {
    std::vector<Agent> result = old;
    const auto limits = simulation.motionLimits();
    for (std::size_t i = 0; i < old.size(); ++i) {
        glm::dvec3 acceleration{0.0};
        for (const auto& rule : simulation.rules()) {
            if (rule->weight() == 0.0F) {
                continue;
            }
            std::vector<sim::Neighbor> neighbors;
            for (std::size_t j = 0; j < old.size(); ++j) {
                if (i == j || rule->radius() == 0.0F) {
                    continue;
                }
                const glm::dvec3 displacement{simulation.world().displacement(old[i].position, old[j].position)};
                const double squared = glm::dot(displacement, displacement);
                if (squared <= static_cast<double>(rule->radius()) * rule->radius()) {
                    neighbors.push_back({&old[j], squared});
                }
            }
            acceleration += glm::dvec3{rule->steer(old[i], neighbors, simulation.world())} *
                            static_cast<double>(rule->weight());
        }
        const double magnitude = glm::length(acceleration);
        if (magnitude > limits.maximumAcceleration) {
            acceleration *= static_cast<double>(limits.maximumAcceleration) / magnitude;
        }
        glm::dvec3 velocity = glm::dvec3{old[i].velocity} + acceleration * static_cast<double>(dt);
        const double speed = glm::length(velocity);
        if (speed > limits.maximumSpeed) {
            velocity *= static_cast<double>(limits.maximumSpeed) / speed;
        }
        // The oracle uses min speed zero to avoid coupling to seeded fallback headings.
        result[i].velocity = glm::vec3{velocity};
        result[i].position = glm::vec3{glm::dvec3{old[i].position} +
                                      glm::dvec3{result[i].velocity} * static_cast<double>(dt)};
        simulation.world().applyBoundary(result[i].position, result[i].velocity);
    }
    return result;
}

void compareWithBruteForce(sim::BoundaryMode boundary) {
    sim::World world{{-7.0F, -4.0F, -9.0F}, {7.0F, 4.0F, 9.0F}, boundary};
    CpuSimulation simulation{64, world, 42};
    simulation.setMotionLimits({0.0F, 7.0F, 12.0F});
    addRules(simulation);
    std::vector<Agent> reference{simulation.agents().begin(), simulation.agents().end()};
    for (int step = 0; step < 100; ++step) {
        if (step == 25) {
            simulation.rules()[0]->setRadius(8.0F);
            simulation.rules()[1]->setWeight(0.0F);
        }
        if (step == 50) {
            simulation.rules()[1]->setWeight(0.7F);
            simulation.rules()[2]->setRadius(0.0F);
        }
        if (step == 75) {
            simulation.rules()[2]->setRadius(3.5F);
        }
        reference = bruteStep(reference, simulation, 1.0F / 60.0F);
        simulation.step(1.0F / 60.0F);
        for (std::size_t i = 0; i < reference.size(); ++i) {
            require(glm::length(simulation.world().displacement(reference[i].position,
                        simulation.agents()[i].position)) < 0.001F,
                    "Spatial CPU position must agree with all-pairs trajectory");
            require(glm::length(reference[i].velocity - simulation.agents()[i].velocity) < 0.001F,
                    "Spatial CPU velocity must agree with all-pairs trajectory");
        }
    }
}

void deterministicFlock() {
    CpuSimulation simulation{512};
    addRules(simulation);
    const std::vector<Agent> initial{simulation.agents().begin(), simulation.agents().end()};
    auto evolve = [&] {
        for (int frame = 0; frame < 240; ++frame) {
            simulation.step(1.0F / 60.0F);
            for (const auto& agent : simulation.agents()) {
                const float speed = glm::length(agent.velocity);
                require(std::isfinite(speed) && speed >= 0.9999F && speed <= 8.0001F,
                        "Flock must stay finite and within speed limits");
                for (glm::length_t axis = 0; axis < 3; ++axis) {
                    require(agent.position[axis] >= simulation.world().minimum[axis] &&
                            agent.position[axis] < simulation.world().maximum[axis],
                            "Flock must remain inside periodic bounds");
                }
            }
        }
    };
    evolve();
    const std::vector<Agent> first{simulation.agents().begin(), simulation.agents().end()};
    require(glm::length(first[0].position - initial[0].position) > 0.1F, "Flock must evolve");
    simulation.reset();
    evolve();
    for (std::size_t i = 0; i < first.size(); ++i) {
        require(first[i].position == simulation.agents()[i].position &&
                first[i].velocity == simulation.agents()[i].velocity,
                "Same seed, fixed dt and rules must replay identically after reset");
    }
}

} // namespace

int main() {
    try {
        compareWithBruteForce(sim::BoundaryMode::Wrap);
        compareWithBruteForce(sim::BoundaryMode::Bounce);
        deterministicFlock();
        std::cout << "Boids simulation tests passed (all-pairs trajectories and deterministic flock)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Boids simulation test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
