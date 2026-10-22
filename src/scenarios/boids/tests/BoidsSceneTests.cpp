#include "scenarios/boids/BoidsScene.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {

using namespace emergences;
using scenarios::boids::BoidsScene;
using scenarios::boids::ColorMode;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool sameAgent(const sim::Agent& left, const sim::Agent& right) {
    return left.position == right.position && left.velocity == right.velocity &&
           left.color == right.color && left.size == right.size &&
           left.reserved == right.reserved && left.id == right.id;
}

void requireSameState(std::span<const sim::Agent> left, std::span<const sim::Agent> right) {
    require(left.size() == right.size(), "State counts must agree");
    for (std::size_t index = 0; index < left.size(); ++index) {
        require(sameAgent(left[index], right[index]), "Fixed-step trajectories must agree exactly");
    }
}

void framePartitioning() {
    BoidsScene at120Hz{64, 42};
    BoidsScene at60Hz{64, 42};
    BoidsScene at144Hz{64, 42};
    for (int frame = 0; frame < 120; ++frame) {
        at120Hz.advance(1.0 / 120.0);
    }
    for (int frame = 0; frame < 60; ++frame) {
        at60Hz.advance(1.0 / 60.0);
    }
    for (int frame = 0; frame < 144; ++frame) {
        at144Hz.advance(1.0 / 144.0);
    }
    require(at120Hz.totalSteps() == 120 && at60Hz.totalSteps() == 120 && at144Hz.totalSteps() == 120,
            "A second partitioned into ordinary frames must produce 120 steps");
    require(at120Hz.elapsedSimulationSeconds() == 1.0, "Clock must count only simulated steps");
    requireSameState(at120Hz.simulation().agents(), at60Hz.simulation().agents());
    requireSameState(at120Hz.simulation().agents(), at144Hz.simulation().agents());
}

void clockControls() {
    BoidsScene scene{2};
    constexpr double halfStep = BoidsScene::FixedStepSeconds * 0.5;
    require(scene.advance(halfStep) == 0 && scene.totalSteps() == 0,
            "Fractional frame must wait for a full step");
    require(scene.advance(halfStep) == 1, "Fractional frames must accumulate");
    require(!scene.singleStep() && scene.totalSteps() == 1, "Single step is disabled while running");
    scene.advance(halfStep);
    scene.setPaused(true);
    require(scene.advance(30.0) == 0 && scene.totalSteps() == 1, "Paused wall time must be discarded");
    require(scene.singleStep() && scene.totalSteps() == 2, "Paused single step advances exactly once");
    scene.setPaused(false);
    require(scene.advance(halfStep) == 0, "Resume must discard partial time from before pause");
    require(scene.advance(halfStep) == 1, "Resume accumulates new wall time normally");
    const auto beforeStall = scene.totalSteps();
    require(scene.advance(std::numeric_limits<double>::max()) == BoidsScene::MaxStepsPerAdvance,
            "A stalled frame must have bounded simulation work");
    require(scene.totalSteps() == beforeStall + BoidsScene::MaxStepsPerAdvance,
            "Clock must exclude discarded backlog");
    require(scene.advance(0.0) == 0, "Discarded backlog must not leak into later frames");
    require(scene.advance(BoidsScene::FixedStepSeconds) == 1, "Normal stepping resumes after a stall");

    for (bool paused : {false, true}) {
        scene.setPaused(paused);
        const auto beforeInvalid = scene.totalSteps();
        for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected = false;
            try {
                scene.advance(invalid);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "Invalid frame durations must be rejected even when paused");
        }
        require(scene.totalSteps() == beforeInvalid, "Invalid time cannot advance simulation");
    }

    scene.setPaused(false);
    scene.advance(halfStep);
    scene.reset();
    require(scene.advance(halfStep) == 0, "Reset must discard fractional time while running");

    BoidsScene manuallyStepped{8, 99};
    BoidsScene normallyStepped{8, 99};
    manuallyStepped.setPaused(true);
    manuallyStepped.singleStep();
    normallyStepped.advance(BoidsScene::FixedStepSeconds);
    requireSameState(manuallyStepped.simulation().agents(), normallyStepped.simulation().agents());
}

void boundedWork() {
    BoidsScene scene{8};
    constexpr double step = BoidsScene::FixedStepSeconds;
    require(scene.advance(4.0 * step, 0.0) == 1 && scene.lastAdvanceLimited(),
            "Even a zero work budget must complete one due step and report discarded backlog");
    require(scene.advance(0.0, 0.0) == 0 && !scene.lastAdvanceLimited(),
            "Discarded whole steps must not accumulate into the next frame");
    require(scene.advance(3.5 * step, 0.0) == 1 && scene.lastAdvanceLimited(),
            "A budget-limited frame must discard whole steps but retain fractional time");
    require(scene.advance(0.5 * step, 0.0) == 1 && !scene.lastAdvanceLimited(),
            "The retained fraction must complete one step without falsely reporting slowdown");
    require(scene.advance(0.5 * step, 0.0) == 0 && !scene.lastAdvanceLimited(),
            "A zero budget must not manufacture a step when none is due");
    require(scene.advance(0.5 * step, 0.0) == 1 && !scene.lastAdvanceLimited(),
            "Zero-budget frames must accumulate fractional time normally");
    require(scene.advance(2.0 * step, std::numeric_limits<double>::infinity()) == 2 &&
                !scene.lastAdvanceLimited(),
            "Explicit positive infinity must preserve unrestricted stepping");
    require(scene.advance(step, std::numeric_limits<double>::max()) == 1 &&
                !scene.lastAdvanceLimited(), "Large finite budgets must be accepted");
    require(scene.advance(1.0) == BoidsScene::MaxStepsPerAdvance && scene.lastAdvanceLimited(),
            "The existing elapsed-time cap must also report discarded simulation time");

    scene.setPaused(true);
    require(!scene.lastAdvanceLimited(), "Pausing must clear a stale slowdown indication");
    require(scene.advance(20.0, 0.0) == 0 && !scene.lastAdvanceLimited(),
            "Paused wall time must not be reported as a work-budget slowdown");
    const auto beforeSingleStep = scene.totalSteps();
    require(scene.singleStep() && scene.totalSteps() == beforeSingleStep + 1 &&
                !scene.lastAdvanceLimited(), "A paused single step must clear slowdown status");
    scene.setPaused(false);
    scene.advance(2.0 * step, 0.0);
    require(!scene.singleStep() && !scene.lastAdvanceLimited(),
            "An unavailable single step must not retain a stale slowdown indication");
    scene.advance(2.0 * step, 0.0);
    scene.reset();
    require(!scene.lastAdvanceLimited() && scene.totalSteps() == 0 &&
                scene.advance(0.0, 0.0) == 0,
            "Reset must clear slowdown status, step count, and remaining time");

    for (const bool paused : {false, true}) {
        scene.setPaused(paused);
        const auto beforeInvalid = scene.totalSteps();
        for (const double budget : {-1.0, -std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected = false;
            try {
                scene.advance(2.0 * step, budget);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "Invalid work budgets must be rejected even while paused");
        }
        require(scene.totalSteps() == beforeInvalid && scene.advance(0.0, 0.0) == 0,
                "Rejected work budgets must not advance or accumulate simulation time");
    }
}

void resetAndCount() {
    BoidsScene scene{16, 7};
    require(scene.simulation().rules().size() == 3, "Scene must install the three boids rules");
    require(scene.simulation().world().minimum == glm::vec3{-20.0F} &&
            scene.simulation().world().maximum == glm::vec3{20.0F},
            "Simulation bounds must match the rendered world");
    scene.simulation().rules()[0]->setWeight(2.7F);
    scene.simulation().rules()[1]->setRadius(6.0F);
    scene.simulation().setMotionLimits({0.0F, 6.0F, 11.0F});
    auto world = scene.simulation().world();
    world.boundary = sim::BoundaryMode::Bounce;
    scene.simulation().setWorld(world);
    scene.simulation().setCount(23);
    scene.reset();
    const std::vector<sim::Agent> initial{scene.simulation().agents().begin(), scene.simulation().agents().end()};
    for (int frame = 0; frame < 20; ++frame) {
        scene.advance(1.0 / 60.0);
    }
    const std::vector<sim::Agent> evolved{scene.simulation().agents().begin(), scene.simulation().agents().end()};
    scene.advance(BoidsScene::FixedStepSeconds * 0.5);
    scene.setPaused(true);
    scene.refreshColors(ColorMode::Solid);
    scene.reset();
    require(scene.paused() && scene.totalSteps() == 0 && scene.elapsedSimulationSeconds() == 0.0,
            "Reset must clear the clock while retaining pause state");
    require(scene.simulation().count() == 23 && scene.simulation().rules()[0]->weight() == 2.7F &&
            scene.simulation().rules()[1]->radius() == 6.0F &&
            scene.simulation().world().boundary == sim::BoundaryMode::Bounce &&
            scene.simulation().motionLimits().maximumSpeed == 6.0F,
            "Reset must retain the user's count and simulation parameters");
    requireSameState(initial, scene.simulation().agents());
    require(scene.displayAgents().size() == 23 &&
            scene.displayAgents()[0].color == glm::vec3{0.20F, 0.75F, 1.0F},
            "Reset refreshes display records using the selected color mode");
    scene.setPaused(false);
    for (int frame = 0; frame < 20; ++frame) {
        scene.advance(1.0 / 60.0);
    }
    requireSameState(evolved, scene.simulation().agents());

    scene.simulation().setCount(0);
    scene.reset();
    require(scene.refreshColors(ColorMode::Density).empty(), "Empty flock must support density rendering");
    require(scene.advance(BoidsScene::FixedStepSeconds) == 1, "An empty flock still advances scene time");
    scene.simulation().setCount(1);
    require(scene.refreshColors(ColorMode::Direction).size() == 1, "Color copies must follow count changes");
}

void colorsPreserveSimulation() {
    BoidsScene scene{0};
    scene.simulation().setMotionLimits({0.0F, 8.0F, 20.0F});
    std::array<sim::Agent, 3> agents{};
    agents[0].velocity = {0.0F, 0.0F, 0.0F};
    agents[1].velocity = {4.0F, 0.0F, 0.0F};
    agents[2].velocity = {-8.0F, 0.0F, 0.0F};
    for (std::size_t index = 0; index < agents.size(); ++index) {
        agents[index].id = static_cast<std::uint32_t>(index);
        agents[index].color = {0.3F, 0.4F, 0.5F};
    }
    scene.simulation().setAgents(agents);
    const std::vector<sim::Agent> original{scene.simulation().agents().begin(), scene.simulation().agents().end()};
    auto display = scene.refreshColors(ColorMode::Direction);
    require(display[0].color == glm::vec3{0.5F} && display[1].color == glm::vec3{1.0F, 0.5F, 0.5F} &&
            display[2].color == glm::vec3{0.0F, 0.5F, 0.5F},
            "Direction colors must distinguish headings and handle zero velocity");
    display = scene.refreshColors(ColorMode::Speed);
    require(glm::length(display[0].color - glm::vec3{0.10F, 0.35F, 0.95F}) < 1.0e-6F &&
            glm::length(display[2].color - glm::vec3{1.0F, 0.25F, 0.05F}) < 1.0e-6F,
            "Speed colors must span zero to the configured maximum speed");
    require(glm::length(display[1].color - (display[0].color + display[2].color) * 0.5F) < 1.0e-6F,
            "Speed color scale must be linear and stable");
    scene.refreshColors(ColorMode::Density);
    display = scene.refreshColors(ColorMode::Solid);
    require(display[0].color == display[1].color && display[1].color == display[2].color,
            "Solid mode must use a uniform color");
    requireSameState(original, scene.simulation().agents());
    for (std::size_t index = 0; index < display.size(); ++index) {
        auto record = display[index];
        record.color = original[index].color;
        require(sameAgent(record, original[index]), "Display color generation cannot alter other particle fields");
    }
    scene.simulation().setMotionLimits({0.0F, 0.0F, 0.0F});
    display = scene.refreshColors(ColorMode::Speed);
    require(std::isfinite(display[0].color.x), "Zero maximum speed must produce a finite color");
    bool rejected = false;
    try {
        scene.refreshColors(static_cast<ColorMode>(99));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Invalid color modes must be rejected");
}

void densityUsesLocalPeriodicNeighborhoods() {
    BoidsScene scene{0};
    scene.simulation().setMotionLimits({0.0F, 8.0F, 20.0F});
    std::array<sim::Agent, 3> agents{};
    agents[0].position = {-19.5F, 0.0F, 0.0F};
    agents[1].position = {19.5F, 0.0F, 0.0F};
    agents[2].position = {0.0F, 0.0F, 0.0F};
    scene.simulation().setAgents(agents);
    auto display = scene.refreshColors(ColorMode::Density);
    const glm::vec3 pairColor = display[0].color;
    const glm::vec3 isolatedColor = display[2].color;
    require(display[0].color == display[1].color && pairColor.x > isolatedColor.x,
            "Wrapping density must see neighbors across the world seam");
    scene.simulation().setAgents(std::span<const sim::Agent>{agents}.first(2));
    display = scene.refreshColors(ColorMode::Density);
    require(display[0].color == pairColor, "Density scale cannot change when an unrelated distant agent is removed");
    auto world = scene.simulation().world();
    world.boundary = sim::BoundaryMode::Bounce;
    scene.simulation().setWorld(world);
    display = scene.refreshColors(ColorMode::Density);
    require(display[0].color == isolatedColor && display[1].color == isolatedColor,
            "Bouncing density must not wrap the neighborhood");

    std::vector<sim::Agent> crowded(40);
    scene.simulation().setAgents(crowded);
    display = scene.refreshColors(ColorMode::Density);
    require(display[0].color == glm::vec3{1.0F, 0.25F, 0.05F},
            "Dense neighborhoods must saturate at a finite fixed color");
    crowded.resize(80);
    scene.simulation().setAgents(crowded);
    display = scene.refreshColors(ColorMode::Density);
    require(display[0].color == glm::vec3{1.0F, 0.25F, 0.05F},
            "Density saturation must remain stable as crowding increases");
}

} // namespace

int main() {
    try {
        framePartitioning();
        clockControls();
        boundedWork();
        resetAndCount();
        colorsPreserveSimulation();
        densityUsesLocalPeriodicNeighborhoods();
        std::cout << "Boids scene tests passed (fixed-step controls, replay, colors and periodic density)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Boids scene test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
