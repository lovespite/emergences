#include "scenarios/slime_mold/SlimeMoldGpu.h"

#include <algorithm>
#include <cstring>
#include "scenarios/slime_mold/SlimeMoldModel.h"

namespace emergences::scenarios::slime_mold {
struct SlimeMoldGpu::Constants {
    std::array<std::uint32_t, 4> counts{}; // count, grid width, stage, tick
    std::array<float, 4> motion{}; // dt, speed, sensor offset, angle
    std::array<float, 4> behavior{}; // turn rate, wander, quantized deposit, food weight
    std::array<float, 4> field{}; // diffusion blend, decay multiplier, exposure, show agents
    std::array<std::uint32_t, 4> random{}; // seed
};

SlimeMoldGpu::SlimeMoldGpu(sim::ComputeDevice& device, const std::string& path) : device_{device} {
    static_assert(sizeof(Constants) == 80);
    pipeline_ = device_.createPipeline(path, 7, sizeof(Constants));
}
SlimeMoldGpu::~SlimeMoldGpu() {
    for (const auto buffer : buffers_) if (buffer) device_.destroyBuffer(buffer);
    if (pipeline_) device_.destroyPipeline(pipeline_);
}
void SlimeMoldGpu::setState(const SlimeMoldState& state, std::span<const float> nutrients) {
    if (state.agents.size() > SlimeMoldScene::MaxAgents || state.trail.size() != SlimeMoldScene::CellCount ||
        nutrients.size() != SlimeMoldScene::CellCount)
        throw std::invalid_argument("Invalid slime mold GPU state dimensions");
    const auto agentBytes = std::max<std::size_t>(1, state.agents.size()) * sizeof(sim::Agent);
    const auto fieldBytes = SlimeMoldScene::CellCount * sizeof(float);
    const std::array<std::size_t, 7> sizes{agentBytes, agentBytes, fieldBytes, fieldBytes, fieldBytes,
        fieldBytes, (SlimeMoldScene::CellCount + state.agents.size()) * sizeof(sim::Agent)};
    std::array<sim::BufferHandle, 7> replacement{};
    try {
        for (std::size_t i = 0; i < sizes.size(); ++i) replacement[i] = device_.createBuffer(sizes[i]);
        device_.upload(replacement[0], std::as_bytes(std::span{state.agents}));
        device_.upload(replacement[2], std::as_bytes(std::span{state.trail}));
        device_.upload(replacement[5], std::as_bytes(nutrients));
    } catch (...) {
        for (const auto buffer : replacement) if (buffer) device_.destroyBuffer(buffer);
        throw;
    }
    for (const auto buffer : buffers_) if (buffer) device_.destroyBuffer(buffer);
    buffers_ = replacement;
    count_ = state.agents.size();
    current_ = 0;
}
sim::ComputeDispatch SlimeMoldGpu::dispatch(Constants c, std::uint32_t stage, std::size_t items) const {
    c.counts[0] = static_cast<std::uint32_t>(count_);
    c.counts[1] = SlimeMoldScene::GridSize;
    c.counts[2] = stage;
    sim::ComputeDispatch work;
    work.pipeline = pipeline_;
    work.buffers = {buffers_[current_], buffers_[1-current_], buffers_[2+current_],
        buffers_[3-current_], buffers_[4], buffers_[5], buffers_[6]};
    work.constants.resize(sizeof(c));
    std::memcpy(work.constants.data(), &c, sizeof(c));
    work.groupsX = static_cast<std::uint32_t>((items + 127) / 128);
    return work;
}
void SlimeMoldGpu::step(const SlimeMoldParameters& p, std::uint32_t seed, std::uint32_t tick) {
    model::validate(p);
    Constants c;
    c.counts[3] = tick;
    c.motion = {model::Dt, p.speed, p.sensorDistance, p.sensorAngle};
    c.behavior = {p.turnSpeed, p.wander, static_cast<float>(model::deposit(p)), p.foodAttraction};
    c.field = {p.diffusion * model::Dt, std::exp(-p.evaporation * model::Dt), 0, 0};
    c.random[0] = seed;
    std::vector<sim::ComputeDispatch> work;
    work.push_back(dispatch(c, 0, SlimeMoldScene::CellCount));
    if (count_ != 0) work.push_back(dispatch(c, 1, count_));
    work.push_back(dispatch(c, 2, SlimeMoldScene::CellCount));
    device_.execute(work);
    current_ = 1-current_;
}
void SlimeMoldGpu::refreshDisplay(float exposure, bool showAgents) {
    model::range(exposure, 0.001F, 10);
    Constants c;
    c.field = {0, 0, exposure, showAgents ? 1.0F : 0.0F};
    const auto work = dispatch(c, 3, SlimeMoldScene::CellCount + (showAgents ? count_ : 0));
    device_.execute(std::span{&work, 1});
}
SlimeMoldState SlimeMoldGpu::readback() {
    SlimeMoldState state;
    state.agents.resize(count_);
    state.trail.resize(SlimeMoldScene::CellCount);
    if (count_) device_.download(buffers_[current_], std::as_writable_bytes(std::span{state.agents}));
    device_.download(buffers_[2+current_], std::as_writable_bytes(std::span{state.trail}));
    return state;
}
} // namespace emergences::scenarios::slime_mold
