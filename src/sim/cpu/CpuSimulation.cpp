#include "sim/cpu/CpuSimulation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#if EMERGENCES_HAS_OPENMP
#include <omp.h>
#endif

namespace emergences::sim::cpu {
namespace {

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void validateCount(std::size_t count) {
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("CPU agent count exceeds the 32-bit ID range");
    }
}

void validateLimits(const MotionLimits& limits) {
    if (!std::isfinite(limits.minimumSpeed) || !std::isfinite(limits.maximumSpeed) ||
        !std::isfinite(limits.maximumAcceleration) || limits.minimumSpeed < 0.0F ||
        limits.maximumSpeed < limits.minimumSpeed || limits.maximumAcceleration < 0.0F) {
        throw std::invalid_argument("Motion limits must be finite, nonnegative and ordered");
    }
}

// Integer mixing makes reset and newly appended agents independent of step count,
// previous resizing and the standard library's random distribution implementation.
std::uint32_t mix(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

float unit(std::uint32_t value) {
    return static_cast<float>(mix(value) >> 8U) * (1.0F / 16777216.0F);
}

glm::dvec3 initialDirection(std::uint32_t value) {
    glm::dvec3 direction{2.0 * unit(value) - 1.0,
                         2.0 * unit(value + 1U) - 1.0,
                         2.0 * unit(value + 2U) - 1.0};
    const double length = glm::length(direction);
    return length > 0.0 ? direction / length : glm::dvec3{1.0, 0.0, 0.0};
}

glm::vec3 limitVelocity(glm::dvec3 velocity, const MotionLimits& limits,
                        std::uint32_t fallbackSeed) {
    const double speed = glm::length(velocity);
    if (speed > static_cast<double>(limits.maximumSpeed)) {
        velocity *= static_cast<double>(limits.maximumSpeed) / speed;
    } else if (speed < static_cast<double>(limits.minimumSpeed)) {
        velocity = speed > 0.0 ? velocity * (static_cast<double>(limits.minimumSpeed) / speed)
                               : initialDirection(fallbackSeed) * static_cast<double>(limits.minimumSpeed);
    }
    return glm::vec3{velocity};
}

glm::vec3 checkedPosition(const glm::dvec3& value) {
    const double maximum = std::numeric_limits<float>::max();
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(value[axis]) || std::abs(value[axis]) > maximum) {
            throw std::overflow_error("CPU step position overflow; use a smaller time step");
        }
    }
    return glm::vec3{value};
}

} // namespace

CpuSimulation::CpuSimulation(std::size_t count, World world, std::uint32_t seed)
    : world_{world}, seed_{seed} {
    world_.validate();
    setCount(count);
}

void CpuSimulation::step(float dt) {
    if (!std::isfinite(dt) || dt < 0.0F) {
        throw std::invalid_argument("Simulation dt must be finite and nonnegative");
    }
    if (dt == 0.0F) {
        return;
    }

    float maximumRadius = 0.0F;
    bool activeRules = false;
    for (const auto& rule : rules_) {
        if (!rule || !std::isfinite(rule->weight()) || rule->weight() < 0.0F ||
            !std::isfinite(rule->radius()) || rule->radius() < 0.0F) {
            throw std::invalid_argument("Rules must be nonnull with finite nonnegative parameters");
        }
        if (rule->weight() > 0.0F) {
            activeRules = true;
            maximumRadius = std::max(maximumRadius, rule->radius());
        }
    }
    for (const auto& intervention : interventions_) {
        if (!intervention) {
            throw std::invalid_argument("Interventions must be nonnull");
        }
    }
    const bool useKernel = ruleKernel_ && ruleKernel_->prepare(rules_);
    if (activeRules && maximumRadius > 0.0F) {
        grid_.rebuild(agents_, world_, maximumRadius);
    }

    const std::size_t workers = workersForStep(useKernel);
    workerScratch_.resize(workers);
    for (auto& scratch : workerScratch_) {
        scratch.failure = nullptr;
        scratch.failureIndex = agents_.size();
    }
    next_ = agents_;
    const auto integrateOne = [&](std::size_t index, WorkerScratch& scratch) {
        auto& neighbors = scratch.neighbors;
        auto& ruleNeighbors = scratch.ruleNeighbors;
        const Agent& self = agents_[index];
        neighbors.clear();
        if (activeRules && maximumRadius > 0.0F) {
            grid_.query(index, maximumRadius, neighbors);
        }
        glm::dvec3 acceleration{0.0};
        if (useKernel) {
            acceleration = ruleKernel_->acceleration(self, neighbors, world_);
            if (!std::isfinite(acceleration.x) || !std::isfinite(acceleration.y) ||
                !std::isfinite(acceleration.z)) {
                throw std::invalid_argument("Rule kernel returned a nonfinite acceleration");
            }
        } else {
            for (const auto& rule : rules_) {
                if (rule->weight() == 0.0F) {
                    continue;
                }
                std::span<const Neighbor> selectedNeighbors = neighbors;
                // The maximum-radius query already has exactly these neighbors.
                // Smaller rules still receive a precisely filtered view, including
                // custom rules which do not filter their input themselves.
                if (rule->radius() != maximumRadius) {
                    ruleNeighbors.clear();
                    const double radiusSquared = static_cast<double>(rule->radius()) * rule->radius();
                    if (rule->radius() > 0.0F) {
                        for (const Neighbor& neighbor : neighbors) {
                            if (neighbor.distanceSquared <= radiusSquared) {
                                ruleNeighbors.push_back(neighbor);
                            }
                        }
                    }
                    selectedNeighbors = ruleNeighbors;
                }
                const glm::vec3 steering = rule->steer(self, selectedNeighbors, world_);
                if (!finite(steering)) {
                    throw std::invalid_argument("Rule returned a nonfinite acceleration");
                }
                acceleration += glm::dvec3{steering} * static_cast<double>(rule->weight());
            }
        }
        const double magnitude = glm::length(acceleration);
        if (magnitude > static_cast<double>(limits_.maximumAcceleration)) {
            acceleration *= static_cast<double>(limits_.maximumAcceleration) / magnitude;
        }
        Agent& next = next_[index];
        next.velocity = limitVelocity(glm::dvec3{self.velocity} + acceleration * static_cast<double>(dt),
                                      limits_, seed_ ^ self.id);
        next.position = checkedPosition(glm::dvec3{self.position} +
                                         glm::dvec3{next.velocity} * static_cast<double>(dt));
        world_.applyBoundary(next.position, next.velocity);
    };

#if EMERGENCES_HAS_OPENMP
    if (workers > 1) {
        const int requestedThreads = static_cast<int>(workers);
#pragma omp parallel num_threads(requestedThreads)
        {
            auto& scratch = workerScratch_[static_cast<std::size_t>(omp_get_thread_num())];
#pragma omp for schedule(static)
            for (std::int64_t rawIndex = 0; rawIndex < static_cast<std::int64_t>(agents_.size()); ++rawIndex) {
                const auto index = static_cast<std::size_t>(rawIndex);
                try {
                    integrateOne(index, scratch);
                } catch (...) {
                    // Each thread owns its failure slot. Report the lowest agent
                    // index after joining, independently of scheduling order.
                    if (index < scratch.failureIndex) {
                        scratch.failureIndex = index;
                        scratch.failure = std::current_exception();
                    }
                }
            }
        }
        const auto firstFailure = std::min_element(workerScratch_.begin(), workerScratch_.end(),
            [](const WorkerScratch& left, const WorkerScratch& right) {
                return left.failureIndex < right.failureIndex;
            });
        if (firstFailure->failure) {
            std::rethrow_exception(firstFailure->failure);
        }
    } else
#endif
    {
        for (std::size_t index = 0; index < agents_.size(); ++index) {
            integrateOne(index, workerScratch_.front());
        }
    }

    World nextWorld = world_;
    for (const auto& intervention : interventions_) {
        intervention->apply(dt, next_, nextWorld);
    }
    normalize(next_, nextWorld, limits_);
    agents_.swap(next_);
    world_ = nextWorld;
}

void CpuSimulation::setRuleKernel(std::unique_ptr<RuleKernel> kernel) {
    ruleKernel_ = std::move(kernel);
}

void CpuSimulation::setWorkerCount(std::size_t count) noexcept { workerCount_ = count; }
std::size_t CpuSimulation::workerCount() const noexcept { return workerCount_; }

std::size_t CpuSimulation::workersForStep(bool useKernel) const noexcept {
#if EMERGENCES_HAS_OPENMP
    if (!useKernel || workerCount_ == 1 || agents_.size() < 2 ||
        (workerCount_ == 0 && agents_.size() < 512)) {
        return 1;
    }
    const auto processors = static_cast<std::size_t>(std::max(1, omp_get_num_procs()));
    const auto available = std::min(processors,
        static_cast<std::size_t>(std::max(1, omp_get_max_threads())));
    const std::size_t requested = workerCount_ == 0
        ? std::min<std::size_t>(16, std::max<std::size_t>(1, processors / 2)) : workerCount_;
    return std::min({requested, available, agents_.size()});
#else
    (void)useKernel;
    return 1;
#endif
}

void CpuSimulation::reset() {
    std::vector<Agent> replacement;
    replacement.reserve(agents_.size());
    for (std::size_t index = 0; index < agents_.size(); ++index) {
        replacement.push_back(makeAgent(index));
    }
    agents_.swap(replacement);
}

std::size_t CpuSimulation::count() const noexcept { return agents_.size(); }

void CpuSimulation::setCount(std::size_t count) {
    validateCount(count);
    const std::size_t oldCount = agents_.size();
    // Allocate before modifying live state; makeAgent cannot fail after validation.
    agents_.reserve(count);
    for (std::size_t index = oldCount; index < count; ++index) {
        agents_.push_back(makeAgent(index));
    }
    if (count < oldCount) {
        agents_.resize(count);
    }
}

Backend CpuSimulation::backend() const noexcept { return Backend::CPU; }
std::span<const Agent> CpuSimulation::agents() const noexcept { return agents_; }
std::vector<std::unique_ptr<Rule>>& CpuSimulation::rules() noexcept { return rules_; }
const std::vector<std::unique_ptr<Rule>>& CpuSimulation::rules() const noexcept { return rules_; }
std::vector<std::unique_ptr<Intervention>>& CpuSimulation::interventions() noexcept { return interventions_; }
const World& CpuSimulation::world() const noexcept { return world_; }

void CpuSimulation::setWorld(const World& world) {
    next_ = agents_;
    normalize(next_, world, limits_);
    agents_.swap(next_);
    world_ = world;
}

MotionLimits CpuSimulation::motionLimits() const noexcept { return limits_; }

void CpuSimulation::setMotionLimits(const MotionLimits& limits) {
    validateLimits(limits);
    next_ = agents_;
    normalize(next_, world_, limits);
    agents_.swap(next_);
    limits_ = limits;
}

void CpuSimulation::setAgents(std::span<const Agent> agents) {
    validateCount(agents.size());
    std::vector<Agent> replacement{agents.begin(), agents.end()};
    normalize(replacement, world_, limits_);
    agents_.swap(replacement);
}

Agent CpuSimulation::makeAgent(std::size_t index) const {
    Agent agent;
    agent.id = static_cast<std::uint32_t>(index);
    const std::uint32_t key = mix(seed_ ^ agent.id);
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        const double fraction = unit(key + static_cast<std::uint32_t>(axis));
        agent.position[axis] = static_cast<float>(static_cast<double>(world_.minimum[axis]) +
            (static_cast<double>(world_.maximum[axis]) - world_.minimum[axis]) * fraction);
    }
    const double speed = (static_cast<double>(limits_.minimumSpeed) + limits_.maximumSpeed) * 0.5;
    agent.velocity = glm::vec3{initialDirection(key + 3U) * speed};
    world_.applyBoundary(agent.position, agent.velocity);
    return agent;
}

void CpuSimulation::normalize(std::vector<Agent>& agents, const World& world,
                              const MotionLimits& limits) const {
    world.validate();
    validateCount(agents.size());
    for (Agent& agent : agents) {
        if (!finite(agent.position) || !finite(agent.velocity) || !finite(agent.color) ||
            !std::isfinite(agent.size) || agent.size < 0.0F || !std::isfinite(agent.reserved)) {
            throw std::invalid_argument("Agent records must be finite with a nonnegative size");
        }
        agent.velocity = limitVelocity(glm::dvec3{agent.velocity}, limits, seed_ ^ agent.id);
        world.applyBoundary(agent.position, agent.velocity);
    }
}

} // namespace emergences::sim::cpu
