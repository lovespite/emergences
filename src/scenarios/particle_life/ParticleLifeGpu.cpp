#include "scenarios/particle_life/ParticleLifeGpu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "scenarios/particle_life/ParticleLifeScene.h"

namespace emergences::scenarios::particle_life {
namespace {

constexpr std::uint32_t WorkgroupSize = 128;
constexpr std::uint32_t MaximumDimension = 32;
constexpr std::uint32_t MaximumCells = MaximumDimension * MaximumDimension * MaximumDimension;

std::uint32_t groups(std::size_t items) {
    return static_cast<std::uint32_t>((items + WorkgroupSize - 1) / WorkgroupSize);
}

void validateRange(float value, float minimum, float maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        throw std::invalid_argument("Particle life GPU parameter is outside its allowed range");
    }
}

void validateParameters(const ParticleLifeParameters& parameters) {
    validateRange(parameters.interactionRadius, 0.0F, ParticleLifeScene::WorldHalfExtent);
    if (parameters.interactionRadius == 0.0F) {
        throw std::invalid_argument("Particle life interaction radius must be positive");
    }
    validateRange(parameters.force, 0.0F, 200.0F);
    validateRange(parameters.friction, 0.0F, 30.0F);
    validateRange(parameters.repulsion, 0.0F, 10.0F);
    validateRange(parameters.coreRatio, 0.01F, 0.95F);
    validateRange(parameters.maxSpeed, 0.0F, 100.0F);
}

} // namespace

// Each member occupies a vec4 slot, matching particle_life.comp.
struct ParticleLifeGpu::Constants {
    std::array<std::uint32_t, 4> counts{}; // particles, cells, stage, three-dimensional
    std::array<std::uint32_t, 4> dimensions{}; // xyz, scan block count
    std::array<float, 4> dynamics{}; // dt, damping, maximum speed, unused
    std::array<float, 4> interaction{}; // radius, force, repulsion, core ratio
    std::array<float, 4> cellWidth{};
};

ParticleLifeGpu::ParticleLifeGpu(sim::ComputeDevice& device, std::string shaderPath)
    : device_{device} {
    static_assert(sizeof(Constants) == 80);
    static_assert(ParticleLifeScene::MaxSpecies == 8);
    try {
        pipeline_ = device_.createPipeline(shaderPath, 9, sizeof(Constants));
        matrix_ = device_.createBuffer(sizeof(uploadedMatrix_));
        cellCounts_ = device_.createBuffer(MaximumCells * sizeof(std::uint32_t));
        cellOffsets_ = device_.createBuffer(MaximumCells * sizeof(std::uint32_t));
        cellCursors_ = device_.createBuffer(MaximumCells * sizeof(std::uint32_t));
        blockSums_ = device_.createBuffer(groups(MaximumCells) * sizeof(std::uint32_t));
        setState({}, {});
    } catch (...) {
        cleanup();
        throw;
    }
}

ParticleLifeGpu::~ParticleLifeGpu() { cleanup(); }

void ParticleLifeGpu::cleanup() noexcept {
    for (const auto buffer : {state_[0], state_[1], species_, matrix_, cellCounts_,
                             cellOffsets_, cellCursors_, indices_, blockSums_}) {
        if (buffer) device_.destroyBuffer(buffer);
    }
    if (pipeline_) device_.destroyPipeline(pipeline_);
}

void ParticleLifeGpu::setState(std::span<const sim::Agent> agents,
                               std::span<const std::uint8_t> species) {
    if (agents.size() > ParticleLifeScene::MaxParticles || agents.size() != species.size()) {
        throw std::invalid_argument("Invalid particle life GPU population size");
    }
    std::vector<std::uint32_t> speciesWords;
    speciesWords.reserve(species.size());
    for (const auto type : species) {
        if (type >= ParticleLifeScene::MaxSpecies) {
            throw std::invalid_argument("Particle life GPU species exceeds matrix bounds");
        }
        speciesWords.push_back(type);
    }
    const auto capacity = std::max<std::size_t>(1, agents.size());
    std::array<sim::BufferHandle, 4> replacement{};
    try {
        replacement[0] = device_.createBuffer(capacity * sizeof(sim::Agent));
        replacement[1] = device_.createBuffer(capacity * sizeof(sim::Agent));
        replacement[2] = device_.createBuffer(capacity * sizeof(std::uint32_t));
        replacement[3] = device_.createBuffer(capacity * sizeof(std::uint32_t));
        if (!agents.empty()) {
            device_.upload(replacement[0], std::as_bytes(agents));
            device_.upload(replacement[2], std::as_bytes(std::span{speciesWords}));
        }
    } catch (...) {
        for (const auto buffer : replacement) if (buffer) device_.destroyBuffer(buffer);
        throw;
    }
    // Keep live resources intact until every allocation and upload succeeds.
    for (const auto buffer : {state_[0], state_[1], species_, indices_}) {
        if (buffer) device_.destroyBuffer(buffer);
    }
    state_[0] = replacement[0];
    state_[1] = replacement[1];
    species_ = replacement[2];
    indices_ = replacement[3];
    count_ = agents.size();
    current_ = 0;
}

sim::ComputeDispatch ParticleLifeGpu::dispatch(const Constants& input,
    std::uint32_t stage, std::uint32_t groupCount) const {
    Constants constants = input;
    constants.counts[2] = stage;
    sim::ComputeDispatch work;
    work.pipeline = pipeline_;
    work.buffers = {state_[current_], state_[1 - current_], species_, matrix_,
                   cellCounts_, cellOffsets_, cellCursors_, indices_, blockSums_};
    work.constants.resize(sizeof(constants));
    std::memcpy(work.constants.data(), &constants, sizeof(constants));
    work.groupsX = groupCount;
    return work;
}

void ParticleLifeGpu::step(float dt, const ParticleLifeParameters& parameters,
                           const std::array<float, 64>& matrix, bool threeDimensional) {
    if (!std::isfinite(dt) || dt < 0.0F) {
        throw std::invalid_argument("Particle life GPU dt must be finite and nonnegative");
    }
    validateParameters(parameters);
    for (const auto value : matrix) validateRange(value, -1.0F, 1.0F);
    if (count_ == 0 || dt == 0.0F) return;
    // With bounded particle count and parameters, 20000 acceleration per
    // neighbor is the worst case before damping and velocity limiting.
    const double maximumAcceleration = static_cast<double>(count_) * 20000.0;
    if (maximumAcceleration * dt > std::numeric_limits<float>::max() ||
        static_cast<double>(parameters.maxSpeed) * dt >
            static_cast<double>(std::numeric_limits<float>::max()) - ParticleLifeScene::WorldHalfExtent) {
        throw std::overflow_error("Particle life GPU time step exceeds float range");
    }
    if (!hasUploadedMatrix_ || matrix != uploadedMatrix_) {
        device_.upload(matrix_, std::as_bytes(std::span{matrix}));
        uploadedMatrix_ = matrix;
        hasUploadedMatrix_ = true;
    }
    Constants constants;
    // Cell widths are at least the interaction radius, so the adjacent 27
    // cells contain every possible neighbor. Capping dimensions bounds grid
    // memory for tiny radii without dropping any particles or interactions.
    const auto dimension = static_cast<std::uint32_t>(std::clamp(
        std::floor(100.0 / parameters.interactionRadius), 1.0,
        static_cast<double>(MaximumDimension)));
    const auto yDimension = threeDimensional ? dimension : 1U;
    const auto cells = dimension * yDimension * dimension;
    constants.counts = {static_cast<std::uint32_t>(count_), cells, 0U,
                        threeDimensional ? 1U : 0U};
    constants.dimensions = {dimension, yDimension, dimension, groups(cells)};
    constants.cellWidth = {100.0F / static_cast<float>(dimension),
                           100.0F / static_cast<float>(yDimension),
                           100.0F / static_cast<float>(dimension), 0.0F};
    constants.dynamics = {dt, std::exp(-parameters.friction * dt), parameters.maxSpeed, 0.0F};
    constants.interaction = {parameters.interactionRadius, parameters.force,
                             parameters.repulsion, parameters.coreRatio};
    std::vector<sim::ComputeDispatch> batch;
    batch.reserve(7);
    if (parameters.force > 0.0F) {
        batch.push_back(dispatch(constants, 0, groups(cells))); // clear occupancy
        batch.push_back(dispatch(constants, 1, groups(count_))); // count particles
        batch.push_back(dispatch(constants, 2, groups(cells))); // scan cell blocks
        batch.push_back(dispatch(constants, 3, 1)); // scan block totals
        batch.push_back(dispatch(constants, 4, groups(cells))); // complete offsets
        batch.push_back(dispatch(constants, 5, groups(count_))); // scatter indices
    }
    batch.push_back(dispatch(constants, 6, groups(count_)));
    device_.execute(batch);
    // Publish only a completed snapshot; ordinary steps never download state.
    current_ = 1 - current_;
}

void ParticleLifeGpu::clampSpeed(float maximum) {
    validateRange(maximum, 0.0F, 100.0F);
    if (count_ == 0) return;
    Constants constants;
    constants.counts[0] = static_cast<std::uint32_t>(count_);
    constants.dynamics[2] = maximum;
    const auto work = dispatch(constants, 7, groups(count_));
    device_.execute(std::span{&work, 1});
    current_ = 1 - current_;
}

std::vector<sim::Agent> ParticleLifeGpu::readback() {
    std::vector<sim::Agent> result(count_);
    if (!result.empty()) {
        device_.download(stateBuffer(), std::as_writable_bytes(std::span{result}));
    }
    return result;
}

} // namespace emergences::scenarios::particle_life
