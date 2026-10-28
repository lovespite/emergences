#include "scenarios/slime_mold/SlimeMoldScene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>
#include "scenarios/slime_mold/SlimeMoldGpu.h"
#include "scenarios/slime_mold/SlimeMoldModel.h"

namespace emergences::scenarios::slime_mold {
SlimeMoldScene::SlimeMoldScene(std::size_t count, std::uint32_t seed)
    : nextTrail_(CellCount), deposits_(CellCount) { regenerate(count, seed, foodCount_); }
SlimeMoldScene::~SlimeMoldScene() = default;

void SlimeMoldScene::initializeGpu(sim::ComputeDevice& device, std::string path) {
    if (backend_ == sim::Backend::GPU) throw std::logic_error("Switch slime mold to CPU before GPU initialization");
    auto replacement = std::make_unique<SlimeMoldGpu>(device, path);
    replacement->setState(state_, nutrients_);
    gpu_ = std::move(replacement);
}
void SlimeMoldScene::setBackend(sim::Backend backend) {
    if (backend != sim::Backend::CPU && backend != sim::Backend::GPU)
        throw std::invalid_argument("Unknown slime mold backend");
    if (backend == backend_) return;
    if (backend == sim::Backend::GPU) {
        if (!gpu_) throw std::logic_error("Slime mold GPU has not been initialized");
        gpu_->setState(state_, nutrients_);
    } else {
        auto state = gpu_->readback();
        state_ = std::move(state);
    }
    backend_ = backend;
    accumulator_ = 0;
    displayDirty_ = true;
}
void SlimeMoldScene::setParameters(const SlimeMoldParameters& parameters) {
    model::validate(parameters);
    parameters_ = parameters;
}
void SlimeMoldScene::regenerate(std::size_t count, std::uint32_t seed, std::size_t foodCount) {
    if (count > MaxAgents || foodCount > 12) throw std::invalid_argument("Slime mold population or food count too large");
    SlimeMoldState state;
    state.agents.resize(count);
    state.trail.resize(CellCount);
    std::vector<glm::vec3> food;
    std::vector<float> nutrients(CellCount);
    for (std::size_t i = 0; i < foodCount; ++i) {
        const float angle = 2 * model::Pi * static_cast<float>(i) / static_cast<float>(foodCount);
        const float radius = foodCount == 1 ? 0 : 28.0F + 6.0F * model::random(seed + static_cast<std::uint32_t>(i));
        food.push_back({radius * std::cos(angle), 0, radius * std::sin(angle)});
    }
    for (std::size_t i = 0; i < CellCount; ++i) {
        const float x = (static_cast<float>(i % GridSize) + 0.5F) * model::CellWidth - WorldHalfExtent;
        const float z = (static_cast<float>(i / GridSize) + 0.5F) * model::CellWidth - WorldHalfExtent;
        for (const auto& f : food) {
            const float dx = model::wrap(x-f.x), dz = model::wrap(z-f.z);
            nutrients[i] += std::exp(-(dx*dx + dz*dz) / 50.0F);
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto& a = state.agents[i];
        const auto key = seed ^ model::hash(static_cast<std::uint32_t>(i));
        a.position = {(model::random(key) - 0.5F) * model::Width, 0,
                      (model::random(key ^ 0xa511e9b3U) - 0.5F) * model::Width};
        a.reserved = (model::random(key ^ 0x63d83595U) * 2 - 1) * model::Pi;
        a.velocity = {std::cos(a.reserved) * parameters_.speed, 0, std::sin(a.reserved) * parameters_.speed};
        a.size = 0.16F;
        a.color = {0.95F, 1.0F, 0.72F};
        a.id = static_cast<std::uint32_t>(i);
    }
    if (backend_ == sim::Backend::GPU) gpu_->setState(state, nutrients);
    state_ = std::move(state); nutrients_ = std::move(nutrients); food_ = std::move(food);
    count_ = count; seed_ = seed; foodCount_ = foodCount;
    steps_ = 0; accumulator_ = 0; limited_ = false; displayDirty_ = true;
}
void SlimeMoldScene::setCount(std::size_t count) { if (count != count_) regenerate(count, seed_, foodCount_); }
void SlimeMoldScene::setSeed(std::uint32_t seed) { if (seed != seed_) regenerate(count_, seed, foodCount_); }
void SlimeMoldScene::setFoodCount(std::size_t count) { if (count != foodCount_) regenerate(count_, seed_, count); }
void SlimeMoldScene::reset() { regenerate(count_, seed_, foodCount_); }
void SlimeMoldScene::setPaused(bool paused) noexcept { paused_ = paused; accumulator_ = 0; limited_ = false; }
std::span<const sim::Agent> SlimeMoldScene::agents() const noexcept {
    return backend_ == sim::Backend::CPU ? std::span<const sim::Agent>{state_.agents} : std::span<const sim::Agent>{};
}
std::span<const float> SlimeMoldScene::trail() const noexcept {
    return backend_ == sim::Backend::CPU ? std::span<const float>{state_.trail} : std::span<const float>{};
}
SlimeMoldState SlimeMoldScene::readback() const { return backend_ == sim::Backend::GPU ? gpu_->readback() : state_; }
void SlimeMoldScene::setState(const SlimeMoldState& input) {
    if (input.agents.size() > MaxAgents || input.trail.size() != CellCount)
        throw std::invalid_argument("Invalid slime mold state dimensions");
    std::unordered_set<std::uint32_t> ids;
    for (const auto& a : input.agents) {
        if (!ids.insert(a.id).second || a.position.y != 0 || a.velocity.y != 0 ||
            a.position.x >= WorldHalfExtent || a.position.z >= WorldHalfExtent || a.size <= 0)
            throw std::invalid_argument("Invalid slime mold agent");
        model::range(a.position.x, -WorldHalfExtent, WorldHalfExtent);
        model::range(a.position.z, -WorldHalfExtent, WorldHalfExtent);
        model::range(a.reserved, -model::Pi, model::Pi);
        model::range(a.size, 0, 100);
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            model::range(a.velocity[axis], -30, 30); model::range(a.color[axis], 0, 1);
        }
    }
    for (const auto t : input.trail) model::range(t, 0, MaxTrail);
    auto state = input;
    if (backend_ == sim::Backend::GPU) gpu_->setState(state, nutrients_);
    state_ = std::move(state); count_ = state_.agents.size();
    steps_ = 0; accumulator_ = 0; limited_ = false; displayDirty_ = true;
}
bool SlimeMoldScene::singleStep() {
    if (!paused_) return false;
    step(); limited_ = false; return true;
}
std::size_t SlimeMoldScene::advance(double seconds, double budget) {
    if (!std::isfinite(seconds) || seconds < 0 || std::isnan(budget) || budget < 0)
        throw std::invalid_argument("Invalid slime mold elapsed time or work budget");
    limited_ = false;
    if (paused_) return 0;
    accumulator_ += std::min(seconds, 0.25);
    const auto start = std::chrono::steady_clock::now();
    std::size_t taken = 0;
    while (accumulator_ + 1e-12 >= FixedStepSeconds && taken < MaxStepsPerAdvance) {
        step(); ++taken; accumulator_ = std::max(0.0, accumulator_ - FixedStepSeconds);
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= budget) break;
    }
    if (accumulator_ + 1e-12 >= FixedStepSeconds) {
        accumulator_ = std::fmod(accumulator_, FixedStepSeconds); limited_ = true;
    }
    return taken;
}
void SlimeMoldScene::step() {
    if (backend_ == sim::Backend::GPU) gpu_->step(parameters_, seed_, static_cast<std::uint32_t>(steps_));
    else {
        const auto& p = parameters_;
        std::fill(deposits_.begin(), deposits_.end(), 0U);
        const auto deposit = model::deposit(p);
        for (auto& a : state_.agents) {
            const auto sense = [&](float angle) {
                const auto i = model::cell(a.position.x + std::cos(angle) * p.sensorDistance,
                                           a.position.z + std::sin(angle) * p.sensorDistance);
                return state_.trail[i] + nutrients_[i] * p.foodAttraction;
            };
            float angle = a.reserved;
            const float f = sense(angle), l = sense(angle+p.sensorAngle), r = sense(angle-p.sensorAngle);
            const float noise = model::random(a.id ^ seed_ ^ (static_cast<std::uint32_t>(steps_) * 0x9e3779b9U)) * 2-1;
            float turn = 0;
            if (f < l && f < r) turn = noise < 0 ? -1.0F : 1.0F;
            else if (!(f > l && f > r)) turn = l > r ? 1.0F : r > l ? -1.0F : 0.0F;
            angle += (turn * p.turnSpeed + noise * p.wander) * model::Dt;
            angle -= std::floor((angle + model::Pi) / (2*model::Pi)) * (2*model::Pi);
            a.reserved = angle;
            a.velocity = {std::cos(angle)*p.speed, 0, std::sin(angle)*p.speed};
            a.position.x = model::wrap(a.position.x + a.velocity.x * model::Dt);
            a.position.z = model::wrap(a.position.z + a.velocity.z * model::Dt);
            deposits_[model::cell(a.position.x, a.position.z)] += deposit;
        }
        const float blend = p.diffusion * model::Dt, decay = std::exp(-p.evaporation * model::Dt);
        const auto deposited = [&](std::size_t i) { return state_.trail[i] + deposits_[i] / model::DepositScale; };
        for (std::size_t z = 0; z < GridSize; ++z) {
            const auto previousRow = (z == 0 ? GridSize-1 : z-1) * GridSize;
            const auto row = z * GridSize;
            const auto nextRow = (z+1 == GridSize ? 0 : z+1) * GridSize;
            for (std::size_t x = 0; x < GridSize; ++x) {
                const auto left = x == 0 ? GridSize-1 : x-1;
                const auto right = x+1 == GridSize ? 0 : x+1;
                // Explicit wrapped indices also avoid a miscompilation of the
                // nested signed-offset/modulo stencil by Apple Clang 21 at -O3.
                // Retain the shader's row-major accumulation order.
                float sum = deposited(previousRow+left);
                sum += deposited(previousRow+x);
                sum += deposited(previousRow+right);
                sum += deposited(row+left);
                sum += deposited(row+x);
                sum += deposited(row+right);
                sum += deposited(nextRow+left);
                sum += deposited(nextRow+x);
                sum += deposited(nextRow+right);
                const auto i = row+x;
                nextTrail_[i] = std::min(MaxTrail, (deposited(i)*(1-blend) + (sum/9)*blend)*decay);
            }
        }
        state_.trail.swap(nextTrail_);
    }
    ++steps_; displayDirty_ = true;
}
std::span<const sim::Agent> SlimeMoldScene::refreshDisplay(float exposure, bool showAgents) {
    model::range(exposure, 0.001F, 10);
    if (displayDirty_ || exposure != exposure_ || showAgents != showAgents_) {
        if (backend_ == sim::Backend::GPU) gpu_->refreshDisplay(exposure, showAgents);
        else {
            display_.resize(renderCount(showAgents));
            for (std::size_t i = 0; i < CellCount; ++i)
                display_[i] = model::fieldParticle(i, state_.trail[i], nutrients_[i], exposure);
            if (showAgents) for (std::size_t i = 0; i < count_; ++i) {
                display_[CellCount+i] = state_.agents[i];
                display_[CellCount+i].position.y = 0.1F;
            }
        }
        displayDirty_ = false; exposure_ = exposure; showAgents_ = showAgents;
    }
    return backend_ == sim::Backend::GPU ? std::span<const sim::Agent>{} : std::span<const sim::Agent>{display_};
}
sim::BufferHandle SlimeMoldScene::renderBuffer() const noexcept {
    return backend_ == sim::Backend::GPU ? gpu_->renderBuffer() : 0;
}
} // namespace emergences::scenarios::slime_mold
