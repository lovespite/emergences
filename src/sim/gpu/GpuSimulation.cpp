#include "sim/gpu/GpuSimulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include "sim/cpu/CpuSimulation.h"

namespace emergences::sim::gpu {
namespace {
constexpr std::uint32_t WorkgroupSize = 128;
constexpr std::uint32_t MaximumCells = 32 * 32 * 32;
std::uint32_t groups(std::size_t items) {
    return static_cast<std::uint32_t>((items + WorkgroupSize - 1) / WorkgroupSize);
}
}

// All members occupy vec4 slots, matching boids.comp exactly. Only the scenario
// parameter SSBO is shader-specific; this layout describes generic grid work.
struct GpuSimulation::Constants {
    std::array<float, 4> minimum{}, maximum{};
    std::array<std::uint32_t, 4> counts{}; // agents, cells, stage, boundary
    std::array<std::uint32_t, 4> dimensions{}; // xyz, block count
    std::array<float, 4> dynamics{}; // dt, minimum/maximum speed, maximum acceleration
    std::array<std::uint32_t, 4> miscellaneous{}; // seed, color mode
    std::array<float, 4> cellWidth{};
    std::array<float, 4> color{}; // density radius, saturation
};

GpuSimulation::GpuSimulation(ComputeDevice& device, std::string shaderPath,
    RuleEncoder encoder, std::size_t count, World world, std::uint32_t seed)
    : device_{device}, encoder_{std::move(encoder)}, world_{world}, seed_{seed} {
    static_assert(sizeof(Constants) == 128);
    world_.validate();
    if (!encoder_) throw std::invalid_argument("GPU simulation requires a rule encoder");
    try {
        pipeline_ = device_.createPipeline(shaderPath, 9, sizeof(Constants));
        cellCounts_ = device_.createBuffer(MaximumCells * sizeof(std::uint32_t));
        cellOffsets_ = device_.createBuffer(MaximumCells * sizeof(std::uint32_t));
        cellCursors_ = device_.createBuffer(MaximumCells * sizeof(std::uint32_t));
        blockSums_ = device_.createBuffer(groups(MaximumCells) * sizeof(std::uint32_t));
        parameters_ = device_.createBuffer(16);
        parameterCapacity_ = 16;
        const std::array<std::byte, 16> empty{};
        device_.upload(parameters_, empty);
        cpu::CpuSimulation initial{count, world_, seed_};
        replaceState(initial.agents());
    } catch (...) {
        cleanup();
        throw;
    }
}

GpuSimulation::~GpuSimulation() { cleanup(); }
void GpuSimulation::cleanup() noexcept {
    for (const BufferHandle buffer : {state_[0], state_[1], display_, cellCounts_,
            cellOffsets_, cellCursors_, indices_, blockSums_, parameters_}) {
        if (buffer) device_.destroyBuffer(buffer);
    }
    if (pipeline_) device_.destroyPipeline(pipeline_);
}

void GpuSimulation::replaceState(std::span<const Agent> agents) {
    if (agents.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("GPU agent count exceeds the 32-bit index range");
    }
    const std::size_t capacity = std::max<std::size_t>(1, agents.size());
    std::array<BufferHandle, 4> replacement{};
    try {
        replacement[0] = device_.createBuffer(capacity * sizeof(Agent));
        replacement[1] = device_.createBuffer(capacity * sizeof(Agent));
        replacement[2] = device_.createBuffer(capacity * sizeof(Agent));
        replacement[3] = device_.createBuffer(capacity * sizeof(std::uint32_t));
        if (!agents.empty()) {
            device_.upload(replacement[0], std::as_bytes(agents));
            device_.upload(replacement[2], std::as_bytes(agents));
        }
    } catch (...) {
        for (const auto buffer : replacement) if (buffer) device_.destroyBuffer(buffer);
        throw;
    }
    for (const BufferHandle buffer : {state_[0], state_[1], display_, indices_}) {
        if (buffer) device_.destroyBuffer(buffer);
    }
    state_[0] = replacement[0];
    state_[1] = replacement[1];
    display_ = replacement[2];
    indices_ = replacement[3];
    current_ = 0;
    count_ = agents.size();
}

std::vector<Agent> GpuSimulation::readback() {
    std::vector<Agent> result(count_);
    if (!result.empty()) device_.download(stateBuffer(), std::as_writable_bytes(std::span{result}));
    return result;
}
std::vector<Agent> GpuSimulation::readbackRender() {
    std::vector<Agent> result(count_);
    if (!result.empty()) device_.download(display_, std::as_writable_bytes(std::span{result}));
    return result;
}

void GpuSimulation::reset() {
    cpu::CpuSimulation initial{0, world_, seed_};
    initial.setMotionLimits(limits_);
    initial.setCount(count_);
    replaceState(initial.agents());
}
void GpuSimulation::setCount(std::size_t count) {
    if (count == count_) return;
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("GPU agent count exceeds the 32-bit index range");
    }
    cpu::CpuSimulation normalized{0, world_, seed_};
    normalized.setMotionLimits(limits_);
    normalized.setAgents(readback());
    normalized.setCount(count);
    replaceState(normalized.agents());
}
void GpuSimulation::setAgents(std::span<const Agent> agents) {
    cpu::CpuSimulation normalized{0, world_, seed_};
    normalized.setMotionLimits(limits_);
    normalized.setAgents(agents);
    replaceState(normalized.agents());
}
void GpuSimulation::setWorld(const World& world) {
    world.validate();
    cpu::CpuSimulation normalized{0, world_, seed_};
    normalized.setMotionLimits(limits_);
    normalized.setAgents(readback());
    normalized.setWorld(world);
    replaceState(normalized.agents());
    world_ = world;
}
void GpuSimulation::setMotionLimits(const MotionLimits& limits) {
    cpu::CpuSimulation normalized{0, world_, seed_};
    // Validate before synchronizing or changing live resources.
    normalized.setMotionLimits(limits);
    normalized.setAgents(readback());
    replaceState(normalized.agents());
    limits_ = limits;
}

GpuSimulation::Constants GpuSimulation::constants(float dt, float radius) const {
    Constants result;
    std::uint32_t cells = 1;
    for (unsigned axis = 0; axis < 3; ++axis) {
        result.minimum[axis] = world_.minimum[axis];
        result.maximum[axis] = world_.maximum[axis];
        const double extent = static_cast<double>(world_.maximum[axis]) - world_.minimum[axis];
        const double desired = radius > 0.0F ? std::floor(extent / radius) : 1.0;
        const auto dimension = static_cast<std::uint32_t>(std::clamp(desired, 1.0, 32.0));
        result.dimensions[axis] = dimension;
        result.cellWidth[axis] = static_cast<float>(extent / dimension);
        cells *= dimension;
    }
    result.counts = {static_cast<std::uint32_t>(count_), cells, 0,
        world_.boundary == BoundaryMode::Wrap ? 0U : 1U};
    result.dimensions[3] = groups(cells);
    result.dynamics = {dt, limits_.minimumSpeed, limits_.maximumSpeed, limits_.maximumAcceleration};
    result.miscellaneous[0] = seed_;
    return result;
}

ComputeDispatch GpuSimulation::dispatch(const Constants& input,
    std::uint32_t stage, std::uint32_t groupCount) const {
    Constants push = input;
    push.counts[2] = stage;
    ComputeDispatch work;
    work.pipeline = pipeline_;
    work.buffers = {state_[current_], state_[1 - current_], cellCounts_, cellOffsets_,
        cellCursors_, indices_, blockSums_, parameters_, display_};
    work.constants.resize(sizeof(push));
    std::memcpy(work.constants.data(), &push, sizeof(push));
    work.groupsX = groupCount;
    return work;
}
void GpuSimulation::appendGrid(std::vector<ComputeDispatch>& batch, const Constants& push) const {
    batch.push_back(dispatch(push, 0, groups(push.counts[1]))); // clear counts
    batch.push_back(dispatch(push, 1, groups(count_))); // count occupancy
    batch.push_back(dispatch(push, 2, groups(push.counts[1]))); // exclusive scan in blocks
    batch.push_back(dispatch(push, 3, 1)); // exclusive scan block totals
    batch.push_back(dispatch(push, 4, groups(push.counts[1]))); // add block offsets
    batch.push_back(dispatch(push, 5, groups(count_))); // counting-sort agent indices
}
void GpuSimulation::uploadRules(const EncodedRules& encoded) {
    if (!std::isfinite(encoded.maximumRadius) || encoded.maximumRadius < 0.0F || encoded.data.empty()) {
        throw std::invalid_argument("Rule encoder returned invalid GPU parameters");
    }
    if (encoded.data == uploadedRules_) return;
    if (encoded.data.size() > parameterCapacity_) {
        const auto replacement = device_.createBuffer(encoded.data.size());
        try { device_.upload(replacement, encoded.data); }
        catch (...) { device_.destroyBuffer(replacement); throw; }
        device_.destroyBuffer(parameters_);
        parameters_ = replacement;
        parameterCapacity_ = encoded.data.size();
    } else {
        device_.upload(parameters_, encoded.data);
    }
    uploadedRules_ = encoded.data;
}
void GpuSimulation::step(float dt) {
    if (!std::isfinite(dt) || dt < 0.0F) {
        throw std::invalid_argument("Simulation dt must be finite and nonnegative");
    }
    if (!interventions_.empty()) {
        throw std::invalid_argument("GPU simulation does not support CPU interventions");
    }
    // Unsupported rules fail even for zero agents or a zero time step.
    const auto encoded = encoder_(rules_);
    if (dt == 0.0F || count_ == 0) return;
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        const double bound = std::max(std::abs(static_cast<double>(world_.minimum[axis])),
            std::abs(static_cast<double>(world_.maximum[axis]))) +
            static_cast<double>(limits_.maximumSpeed) * dt;
        if (bound > std::numeric_limits<float>::max()) {
            throw std::overflow_error("GPU step position overflow; use a smaller time step");
        }
    }
    uploadRules(encoded);
    const auto push = constants(dt, encoded.maximumRadius);
    std::vector<ComputeDispatch> batch;
    if (encoded.maximumRadius > 0.0F) appendGrid(batch, push);
    batch.push_back(dispatch(push, 6, groups(count_)));
    device_.execute(batch);
    current_ = 1 - current_;
}

BufferHandle GpuSimulation::refreshColors(ColorMode mode, float densityRadius,
    float densitySaturation) {
    if (static_cast<std::uint32_t>(mode) > static_cast<std::uint32_t>(ColorMode::Solid) ||
        !std::isfinite(densityRadius) || densityRadius < 0.0F ||
        !std::isfinite(densitySaturation) || densitySaturation <= 0.0F) {
        throw std::invalid_argument("Invalid GPU display color parameters");
    }
    if (!count_) return display_;
    auto push = constants(0.0F, densityRadius);
    push.miscellaneous[1] = static_cast<std::uint32_t>(mode);
    push.color = {densityRadius, densitySaturation, 0.0F, 0.0F};
    std::vector<ComputeDispatch> batch;
    if (mode == ColorMode::Density && densityRadius > 0.0F) appendGrid(batch, push);
    batch.push_back(dispatch(push, 7, groups(count_)));
    device_.execute(batch);
    return display_;
}

} // namespace emergences::sim::gpu
