#include "scenarios/slime_mold/SlimeMoldScene.h"
#include "scenarios/slime_mold/SlimeMoldModel.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

using namespace emergences::scenarios::slime_mold;
namespace {
void require(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool threw = false; try { f(); } catch (const std::exception&) { threw = true; }
    require(threw, "Invalid input was accepted");
}
void same(const SlimeMoldState& a, const SlimeMoldState& b) {
    require(a.trail == b.trail && a.agents.size() == b.agents.size(), "Seeded fields/counts differ");
    for (std::size_t i = 0; i < a.agents.size(); ++i)
        require(a.agents[i].position == b.agents[i].position && a.agents[i].velocity == b.agents[i].velocity &&
            a.agents[i].reserved == b.agents[i].reserved, "Seeded trajectories differ");
}
void fields() {
    SlimeMoldScene scene{0};
    auto p = scene.parameters(); p.diffusion = 60; p.evaporation = 0;
    scene.setParameters(p);
    auto state = scene.readback(); state.trail[0] = 9; scene.setState(state);
    scene.advance(SlimeMoldScene::FixedStepSeconds);
    const auto field = scene.trail();
    require(std::abs(std::accumulate(field.begin(), field.end(), 0.0)-9) < 1e-5,
        "Diffusion must conserve trail mass without evaporation");
    for (const auto i : {0U, 1U, 255U, 256U, 257U, 511U, 65280U, 65281U, 65535U})
        require(std::abs(field[i]-1) < 1e-6F, "Periodic nine-point diffusion stencil is incorrect");
    p.diffusion = 0; p.evaporation = 3; scene.setParameters(p);
    scene.advance(SlimeMoldScene::FixedStepSeconds);
    require(std::abs(scene.trail()[0]-std::exp(-3.0F/60)) < 1e-6F, "Exponential evaporation incorrect");

    scene.setCount(513); p.speed = 0; p.depositRate = 100; p.evaporation = 0; scene.setParameters(p);
    state = scene.readback();
    for (auto& a : state.agents) a.position = {0,0,0};
    std::fill(state.trail.begin(), state.trail.end(), 0);
    scene.setState(state); scene.advance(SlimeMoldScene::FixedStepSeconds);
    require(std::abs(scene.trail()[model::cell(0,0)] - 513.0F*model::deposit(p)/model::DepositScale) < 1e-4F,
        "Coincident agents lost deposits");
    scene.advance(SlimeMoldScene::FixedStepSeconds);
    require(scene.trail()[model::cell(0,0)] == SlimeMoldScene::MaxTrail, "Trail concentration cap failed");
}
void movement() {
    SlimeMoldScene scene{1}; scene.setFoodCount(0);
    auto p = scene.parameters(); p.speed = 30; p.wander = 0; p.turnSpeed = 0;
    p.diffusion = 0; p.evaporation = 0; scene.setParameters(p);
    auto state = scene.readback(); state.agents[0].position = {49.9F,0,0}; state.agents[0].reserved = 0;
    scene.setState(state); scene.advance(SlimeMoldScene::FixedStepSeconds);
    require(std::abs(scene.agents()[0].position.x+49.6F) < 1e-5F, "Movement did not wrap across boundary");
    require(scene.agents()[0].position.y == 0 && scene.agents()[0].velocity.y == 0, "Agent left XZ plane");
    p.speed = 0; p.turnSpeed = 12; scene.setParameters(p);
    state = scene.readback(); state.agents[0].position = {0,0,0}; state.agents[0].reserved = 0;
    std::fill(state.trail.begin(), state.trail.end(), 0);
    state.trail[model::cell(std::cos(p.sensorAngle)*p.sensorDistance, std::sin(p.sensorAngle)*p.sensorDistance)] = 10;
    scene.setState(state); scene.advance(SlimeMoldScene::FixedStepSeconds);
    require(std::abs(scene.agents()[0].reserved-0.2F) < 1e-6F, "Agent failed to turn toward stronger left sensor");
}
void clockAndValidation() {
    SlimeMoldScene a{257,123}, b{257,123};
    const auto initial = a.readback();
    for (int i = 0; i < 20; ++i) { a.advance(1.0/60); b.advance(1.0/120); b.advance(1.0/120); }
    same(a.readback(), b.readback());
    a.setPaused(true); require(a.advance(3) == 0, "Paused scene advanced");
    require(a.singleStep() && a.totalSteps() == 21, "Single step failed");
    a.reset(); same(initial, a.readback()); require(a.paused() && a.totalSteps() == 0, "Reset lost pause or clock");
    a.setPaused(false); require(!a.singleStep(), "Running scene accepted single step");
    require(a.advance(0.25,0) == 1 && a.lastAdvanceLimited(), "Work budget did not drop catch-up steps");
    a.reset(); require(a.advance(10) == SlimeMoldScene::MaxStepsPerAdvance && a.lastAdvanceLimited(), "Step cap failed");
    const auto before = a.readback(); const auto steps = a.totalSteps();
    auto bad = before; bad.trail[0] = -1; rejects([&] { a.setState(bad); });
    bad = before; bad.agents[0].position.x = 50; rejects([&] { a.setState(bad); });
    bad = before; bad.agents[1].id = bad.agents[0].id; rejects([&] { a.setState(bad); });
    auto p = a.parameters(); p.speed = std::numeric_limits<float>::quiet_NaN(); rejects([&] { a.setParameters(p); });
    rejects([&] { a.setCount(SlimeMoldScene::MaxAgents+1); });
    rejects([&] { a.setFoodCount(13); });
    rejects([&] { a.advance(-1); }); rejects([&] { a.advance(0,-1); });
    rejects([&] { a.advance(std::numeric_limits<double>::infinity()); });
    rejects([&] { a.setBackend(emergences::sim::Backend::GPU); });
    same(before,a.readback()); require(a.totalSteps() == steps, "Rejected input changed clock");
    a.refreshDisplay(0.5F,true); same(before,a.readback());
    require(a.refreshDisplay(0.1F,false).size() == SlimeMoldScene::CellCount, "Field display dimensions wrong");
    a.setFoodCount(0); require(std::all_of(a.nutrients().begin(),a.nutrients().end(),[](float n){return n==0;}), "Food-free field not empty");
}
}
int main() {
    try { fields(); movement(); clockAndValidation(); std::cout << "Slime mold CPU model checks passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
