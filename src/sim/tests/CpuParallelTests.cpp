#include "sim/cpu/CpuSimulation.h"
#include "sim/cpu/RuleKernel.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace {

using emergences::sim::Agent;
using emergences::sim::BoundaryMode;
using emergences::sim::Intervention;
using emergences::sim::Neighbor;
using emergences::sim::Rule;
using emergences::sim::World;
using emergences::sim::cpu::CpuSimulation;
using emergences::sim::cpu::RuleKernel;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool sameAgents(std::span<const Agent> a, std::span<const Agent> b) {
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size_bytes()) == 0);
}

bool sameWorld(const World& a, const World& b) {
    return a.minimum == b.minimum && a.maximum == b.maximum && a.boundary == b.boundary;
}

std::vector<Agent> snapshot(const CpuSimulation& simulation) {
    return {simulation.agents().begin(), simulation.agents().end()};
}

World box(BoundaryMode boundary = BoundaryMode::Wrap) {
    return {glm::vec3{-20.0F}, glm::vec3{20.0F}, boundary};
}

// An intentionally small test rule: both paths must preserve each rule's
// float accumulation order, its independent radius, and double weighting.
glm::vec3 steering(const Agent& self, std::span<const Neighbor> neighbors, float radius) {
    glm::vec3 sum{0.0F};
    std::size_t count = 0;
    const double radiusSquared = static_cast<double>(radius) * radius;
    for (const Neighbor& neighbor : neighbors) {
        if (radius > 0.0F && neighbor.distanceSquared <= radiusSquared) {
            sum += neighbor.agent->velocity;
            ++count;
        }
    }
    return (count ? sum / static_cast<float>(count) : glm::vec3{0.0F}) -
           self.velocity * 0.25F;
}

class NeighborhoodRule final : public Rule {
public:
    NeighborhoodRule(float radius, float weight) : radius_{radius}, weight_{weight} {}
    glm::vec3 steer(const Agent& self, std::span<const Neighbor> neighbors,
                    const World&) const override {
        return steering(self, neighbors, radius_);
    }
    std::string_view name() const noexcept override { return "Neighborhood test rule"; }
    float radius() const noexcept override { return radius_; }
    float weight() const noexcept override { return weight_; }
    void setRadius(float value) override { radius_ = value; }
    void setWeight(float value) override { weight_ = value; }
private:
    float radius_;
    float weight_;
};

struct KernelProbe {
    const std::thread::id caller{std::this_thread::get_id()};
    std::size_t preparations{0};
    std::atomic<std::size_t> completed{0};
    std::atomic<bool> sawWorker{false};
};

class AgentFailure final : public std::runtime_error {
public:
    explicit AgentFailure(std::uint32_t value) : std::runtime_error("Kernel agent failure"), id{value} {}
    std::uint32_t id;
};

class TestKernel final : public RuleKernel {
public:
    explicit TestKernel(std::shared_ptr<KernelProbe> probe) : probe_{std::move(probe)} {}

    bool prepare(std::span<const std::unique_ptr<Rule>> rules) override {
        require(std::this_thread::get_id() == probe_->caller, "Kernel prepare must run on the caller thread");
        ++probe_->preparations;
        probe_->completed.store(0, std::memory_order_relaxed);
        parameters_.clear();
        for (const auto& rule : rules) {
            if (dynamic_cast<const NeighborhoodRule*>(rule.get()) == nullptr) return false;
            parameters_.push_back({rule->radius(), rule->weight()});
        }
        return true;
    }

    glm::dvec3 acceleration(const Agent& self, std::span<const Neighbor> neighbors,
                           const World&) const override {
        if (std::this_thread::get_id() != probe_->caller) {
            probe_->sawWorker.store(true, std::memory_order_relaxed);
        }
        if (failures && (self.id == firstFailureId || self.id == secondFailureId)) {
            // Encourage different arrival orders without relying on sleeps or
            // a barrier that would deadlock in the serial fallback.
            if (self.id == firstFailureId) {
                for (int attempt = 0; attempt < 8; ++attempt) std::this_thread::yield();
            }
            throw AgentFailure{self.id};
        }
        if (invalidComponent != 0.0 && self.id == firstFailureId) {
            return {invalidComponent, 0.0, 0.0};
        }
        glm::dvec3 result{0.0};
        for (const auto& parameter : parameters_) {
            if (parameter.weight != 0.0F) {
                result += glm::dvec3{steering(self, neighbors, parameter.radius)} *
                          static_cast<double>(parameter.weight);
            }
        }
        probe_->completed.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    bool failures{false};
    std::uint32_t firstFailureId{900};
    std::uint32_t secondFailureId{1};
    double invalidComponent{0.0};

private:
    struct Parameter { float radius; float weight; };
    std::shared_ptr<KernelProbe> probe_;
    std::vector<Parameter> parameters_;
};

std::shared_ptr<KernelProbe> installKernel(CpuSimulation& simulation) {
    auto probe = std::make_shared<KernelProbe>();
    simulation.setRuleKernel(std::make_unique<TestKernel>(probe));
    return probe;
}

void addRules(CpuSimulation& simulation) {
    simulation.rules().push_back(std::make_unique<NeighborhoodRule>(6.0F, 0.75F));
    simulation.rules().push_back(std::make_unique<NeighborhoodRule>(2.0F, 0.125F));
    simulation.rules().push_back(std::make_unique<NeighborhoodRule>(12.0F, 0.0F));
}

void equivalentTrajectoriesAndReconfiguration() {
    for (const auto boundary : {BoundaryMode::Wrap, BoundaryMode::Bounce}) {
        CpuSimulation generic(384, box(boundary), 1234);
        CpuSimulation serial(384, box(boundary), 1234);
        CpuSimulation parallel(384, box(boundary), 1234);
        serial.setWorkerCount(1);
        parallel.setWorkerCount(4);
        require(serial.workerCount() == 1 && parallel.workerCount() == 4,
                "Worker count must expose the requested configuration");
        for (auto* simulation : {&generic, &serial, &parallel}) addRules(*simulation);
        installKernel(serial);
        const auto parallelProbe = installKernel(parallel);

        auto input = snapshot(generic);
        input[0].position = {-19.99F, 0.0F, 0.0F};
        input[0].velocity = {-2.0F, 0.0F, 0.0F};
        input[1].position = {19.99F, 0.0F, 0.0F};
        input[1].velocity = {2.0F, 0.0F, 0.0F};
        input[0].id = input[1].id; // IDs remain metadata in worker scratch and queries.
        for (auto* simulation : {&generic, &serial, &parallel}) simulation->setAgents(input);

        for (int step = 0; step < 24; ++step) {
            for (auto* simulation : {&generic, &serial, &parallel}) {
                if (step == 4) simulation->setCount(0);
                if (step == 5) simulation->setCount(1);
                if (step == 6) simulation->setCount(320);
                if (step == 12) simulation->setCount(1536);
                if (step == 18) simulation->setCount(47);
                if (step == 8) simulation->rules()[1]->setRadius(0.0F);
                if (step == 10) simulation->rules()[2]->setWeight(0.0625F);
                if (step == 15) {
                    simulation->rules()[0]->setWeight(0.375F);
                    simulation->rules()[0]->setRadius(4.0F);
                }
                simulation->step(0.125F);
            }
            require(sameAgents(generic.agents(), serial.agents()),
                    "A prepared kernel must match generic rule integration bit for bit");
            require(sameAgents(serial.agents(), parallel.agents()),
                    "Changing thread count must not change trajectories or boundary handling");
        }
#if defined(_OPENMP) && EMERGENCES_HAS_OPENMP
        const int requestedThreads = std::min({4, std::max(1, omp_get_num_procs()),
                                               std::max(1, omp_get_max_threads())});
        int actualThreads = 1;
        // The runtime may shrink a requested team (for example, through
        // OMP_THREAD_LIMIT). Observe an actual team using the same request.
#pragma omp parallel num_threads(requestedThreads)
        {
#pragma omp single
            {
                actualThreads = omp_get_num_threads();
            }
        }
        if (actualThreads > 1 && omp_get_dynamic() == 0) {
            require(parallelProbe->sawWorker.load(std::memory_order_relaxed),
                    "An enabled OpenMP build must execute the prepared kernel on workers");
        }
#endif
        // Remove and replace kernels, and switch worker policy between steps.
        parallel.setRuleKernel(nullptr);
        parallel.setWorkerCount(0);
        require(parallel.workerCount() == 0, "Zero must retain the automatic worker policy");
        generic.step(0.125F);
        parallel.step(0.125F);
        require(sameAgents(generic.agents(), parallel.agents()),
                "Removing the kernel must restore generic evaluation on the next step");
        installKernel(parallel);
        parallel.setWorkerCount(2);
        generic.step(0.125F);
        parallel.step(0.125F);
        require(sameAgents(generic.agents(), parallel.agents()),
                "Replacing a kernel and worker policy between steps must preserve state");
    }
}

struct RecordedCall {
    std::uint32_t id;
    int rule;
    std::thread::id thread;
};

struct CallLog {
    std::mutex mutex;
    std::vector<RecordedCall> calls;
};

class StatefulRule final : public Rule {
public:
    StatefulRule(CallLog& log, int number) : log_{log}, number_{number} {}
    glm::vec3 steer(const Agent& self, std::span<const Neighbor>, const World&) const override {
        // A lock keeps the test well-defined even if the implementation wrongly
        // dispatches this arbitrary callback concurrently.
        const std::lock_guard lock{log_.mutex};
        log_.calls.push_back({self.id, number_, std::this_thread::get_id()});
        return {static_cast<float>(log_.calls.size()) * 0.001F, 0.0F, 0.0F};
    }
    std::string_view name() const noexcept override { return "Stateful test rule"; }
    float radius() const noexcept override { return 0.0F; }
    float weight() const noexcept override { return 1.0F; }
    void setRadius(float) override {}
    void setWeight(float) override {}
private:
    CallLog& log_;
    int number_;
};

void unsupportedRulesStaySerial() {
    CpuSimulation simulation(1536);
    simulation.setWorkerCount(4);
    addRules(simulation);
    const auto probe = installKernel(simulation);
    simulation.step(0.01F);
    simulation.rules().clear();
    CallLog log;
    simulation.rules().push_back(std::make_unique<StatefulRule>(log, 0));
    simulation.rules().push_back(std::make_unique<StatefulRule>(log, 1));
    const auto input = snapshot(simulation);
    const auto caller = std::this_thread::get_id();
    simulation.step(0.01F);
    require(probe->preparations == 2 && probe->completed.load(std::memory_order_relaxed) == 0,
            "A rejected kernel must never execute acceleration");
    require(log.calls.size() == input.size() * 2, "Each enabled fallback rule must run exactly once per agent");
    for (std::size_t index = 0; index < input.size(); ++index) {
        for (int rule = 0; rule < 2; ++rule) {
            const auto& call = log.calls[index * 2 + static_cast<std::size_t>(rule)];
            require(call.id == input[index].id && call.rule == rule && call.thread == caller,
                    "Generic callbacks must preserve caller-thread agent-then-rule order");
        }
    }
}

class CountingIntervention final : public Intervention {
public:
    explicit CountingIntervention(std::size_t& calls) : calls_{calls} {}
    void apply(float, std::vector<Agent>&, World&) override { ++calls_; }
    std::string_view name() const noexcept override { return "Counting intervention"; }
private:
    std::size_t& calls_;
};

void workerFailuresAreDeterministicAndAtomic() {
    CpuSimulation simulation(2048);
    simulation.setWorkerCount(4);
    addRules(simulation);
    auto input = snapshot(simulation);
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index].id = 10000U + static_cast<std::uint32_t>(index);
    }
    input[3].id = 900;
    input[1539].id = 1; // Lowest array index must win, not the smallest agent ID.
    simulation.setAgents(input);
    const auto before = snapshot(simulation);
    const World oldWorld = simulation.world();
    const auto probe = std::make_shared<KernelProbe>();
    auto kernel = std::make_unique<TestKernel>(probe);
    auto* kernelPointer = kernel.get();
    kernelPointer->failures = true;
    simulation.setRuleKernel(std::move(kernel));
    std::size_t interventions = 0;
    simulation.interventions().push_back(std::make_unique<CountingIntervention>(interventions));
    for (int attempt = 0; attempt < 6; ++attempt) {
        bool caught = false;
        try {
            simulation.step(0.125F);
        } catch (const AgentFailure& failure) {
            caught = true;
            require(failure.id == 900, "Worker errors must propagate the exception from the lowest agent index");
        }
        require(caught, "Worker exceptions must reach the calling thread with their original type");
        require(sameAgents(simulation.agents(), before) && sameWorld(simulation.world(), oldWorld),
                "A failed parallel step must leave all committed agents and world unchanged");
        require(interventions == 0, "Interventions must not run after a failed worker phase");
    }
    kernelPointer->failures = false;
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()}) {
        kernelPointer->invalidComponent = invalid;
        bool rejected = false;
        try {
            simulation.step(0.125F);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected && sameAgents(simulation.agents(), before) &&
                    sameWorld(simulation.world(), oldWorld) && interventions == 0,
                "Nonfinite kernel outputs must reject the whole step before interventions");
    }
    kernelPointer->invalidComponent = 0.0;
    CpuSimulation reference(0, oldWorld);
    reference.setAgents(before);
    addRules(reference);
    reference.step(0.125F);
    simulation.step(0.125F);
    require(interventions == 1 && sameAgents(simulation.agents(), reference.agents()),
            "A successful retry must resume from the last committed state");
}

class EditingIntervention final : public Intervention {
public:
    EditingIntervention(std::shared_ptr<KernelProbe> probe, const std::vector<Agent>& expected,
                        std::vector<int>& order, int phase)
        : probe_{std::move(probe)}, expected_{expected}, order_{order}, phase_{phase} {}
    void apply(float dt, std::vector<Agent>& agents, World& world) override {
        require(std::this_thread::get_id() == probe_->caller, "Interventions must run on the calling thread");
        require(probe_->completed.load(std::memory_order_relaxed) == expected_.size(),
                "Every kernel evaluation must finish before interventions begin");
        require(dt == 0.125F, "Interventions must receive the actual simulation step");
        if (phase_ == 1) {
            require(sameAgents(agents, expected_), "Interventions must see the fully integrated state");
            require(order_.empty(), "The first intervention must run first");
            world = {glm::vec3{-2.0F}, glm::vec3{2.0F}, BoundaryMode::Wrap};
            agents.resize(2);
            Agent appended;
            appended.id = 98765;
            appended.position = {-11.0F, 7.0F, 0.0F};
            appended.velocity = {90.0F, 0.0F, 0.0F};
            agents.push_back(appended);
        } else {
            require(order_ == std::vector<int>{1} && agents.size() == 3 && world.maximum.x == 2.0F,
                    "Later interventions must see prior count and world edits in registration order");
            require(agents.back().velocity.x == 90.0F,
                    "Final normalization must happen after all interventions");
        }
        order_.push_back(phase_);
    }
    std::string_view name() const noexcept override { return "Editing intervention"; }
private:
    std::shared_ptr<KernelProbe> probe_;
    const std::vector<Agent>& expected_;
    std::vector<int>& order_;
    int phase_;
};

void interventionsFollowTheWorkerBarrier() {
    CpuSimulation reference(1536, box(), 42);
    CpuSimulation simulation(1536, box(), 42);
    for (auto* current : {&reference, &simulation}) {
        current->setMotionLimits({0.0F, 10.0F, 20.0F});
        addRules(*current);
    }
    reference.step(0.125F);
    const auto expected = snapshot(reference);
    simulation.setWorkerCount(4);
    const auto probe = installKernel(simulation);
    std::vector<int> order;
    simulation.interventions().push_back(std::make_unique<EditingIntervention>(probe, expected, order, 1));
    simulation.interventions().push_back(std::make_unique<EditingIntervention>(probe, expected, order, 2));
    simulation.step(0.125F);
    require(order == std::vector<int>({1, 2}) && simulation.count() == 3,
            "Interventions must commit their final agent count");
    const Agent& appended = simulation.agents().back();
    require(appended.id == 98765 && appended.position == glm::vec3(1.0F, -1.0F, 0.0F) &&
                appended.velocity == glm::vec3(10.0F, 0.0F, 0.0F),
            "Appended agents must be normalized against the final world and motion limits");
    simulation.interventions().clear();
    reference.setWorld(simulation.world());
    reference.setAgents(simulation.agents());
    reference.step(0.125F);
    simulation.step(0.125F);
    require(sameAgents(simulation.agents(), reference.agents()),
            "The next step must rebuild worker state for an intervention's changed count and world");
}

void validationPrecedesPreparation() {
    CpuSimulation simulation(32);
    simulation.setWorkerCount(4);
    addRules(simulation);
    const auto probe = installKernel(simulation);
    const auto before = snapshot(simulation);
    simulation.step(0.0F);
    require(probe->preparations == 0, "Zero dt must not prepare a kernel");
    simulation.rules()[0]->setWeight(std::numeric_limits<float>::quiet_NaN());
    bool rejected = false;
    try {
        simulation.step(0.125F);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && probe->preparations == 0 && sameAgents(simulation.agents(), before),
            "Invalid rules must be rejected before kernel preparation or worker dispatch");
}

} // namespace

int main() {
    try {
        equivalentTrajectoriesAndReconfiguration();
        unsupportedRulesStaySerial();
        workerFailuresAreDeterministicAndAtomic();
        interventionsFollowTheWorkerBarrier();
        validationPrecedesPreparation();
        std::cout << "CPU parallel contract tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "CPU parallel contract test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
