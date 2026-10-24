#include "scenarios/particle_life/ParticleLifeScene.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

using emergences::sim::Agent;
using emergences::sim::Backend;
using emergences::sim::BufferHandle;
using emergences::sim::ComputeDevice;
using emergences::sim::ComputeDispatch;
using emergences::sim::PipelineHandle;
using emergences::scenarios::particle_life::ParticleLifeScene;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Callback>
void requireFailure(Callback&& callback, const char* message) {
    bool failed = false;
    try { callback(); }
    catch (const std::exception&) { failed = true; }
    require(failed, message);
}

void requireSameState(std::span<const Agent> left, std::span<const Agent> right) {
    require(left.size() == right.size(), "Backend transfer must preserve particle count");
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto& a = left[index];
        const auto& b = right[index];
        require(a.position == b.position && a.velocity == b.velocity && a.size == b.size &&
                    a.color == b.color && a.reserved == b.reserved && a.id == b.id,
                "Backend transfer must preserve all particle fields");
    }
}

// Tests device ownership, dispatch contracts and explicit transfers without a
// GPU. Only evolve/clamp stages copy state; actual grid and force arithmetic
// have separate tests which execute the real shader.
class MemoryComputeDevice final : public ComputeDevice {
public:
    BufferHandle createBuffer(std::size_t size) override {
        if (failAllocation) throw std::runtime_error("Injected buffer allocation failure");
        const auto handle = nextHandle_++;
        buffers_.emplace(handle, std::vector<std::byte>(size));
        return handle;
    }
    void destroyBuffer(BufferHandle handle) noexcept override { buffers_.erase(handle); }
    void upload(BufferHandle handle, std::span<const std::byte> bytes) override {
        if (uploadsUntilFailure == 0) throw std::runtime_error("Injected upload failure");
        if (uploadsUntilFailure > 0) --uploadsUntilFailure;
        ++uploads;
        auto& storage = buffers_.at(handle);
        require(bytes.size() <= storage.size(), "Uploads must fit allocated storage");
        std::copy(bytes.begin(), bytes.end(), storage.begin());
    }
    void download(BufferHandle handle, std::span<std::byte> bytes) override {
        if (failDownload) throw std::runtime_error("Injected download failure");
        ++downloads;
        const auto& storage = buffers_.at(handle);
        require(bytes.size() <= storage.size(), "Downloads must fit allocated storage");
        std::copy_n(storage.begin(), bytes.size(), bytes.begin());
    }
    PipelineHandle createPipeline(std::string_view, std::uint32_t storageBindings,
                                  std::uint32_t pushConstantBytes) override {
        if (failPipeline) throw std::runtime_error("Injected pipeline failure");
        require(storageBindings == 9 && pushConstantBytes == sizeof(DispatchConstants),
                "Particle life pipelines must expose all grid buffers and push constants");
        const auto handle = nextHandle_++;
        pipelines_.insert(handle);
        return handle;
    }
    void destroyPipeline(PipelineHandle handle) noexcept override { pipelines_.erase(handle); }
    void execute(std::span<const ComputeDispatch> dispatches) override {
        if (failExecution) throw std::runtime_error("Injected compute failure");
        require(dispatches.size() == 1 || dispatches.size() == 7,
                "GPU execution must batch the entire grid build and evolve, or a single clamp/free-motion stage");
        lastStages.clear();
        for (std::size_t index = 0; index < dispatches.size(); ++index) {
            if (dispatchesUntilFailure == 0) throw std::runtime_error("Injected partial compute failure");
            if (dispatchesUntilFailure > 0) --dispatchesUntilFailure;
            const auto& dispatch = dispatches[index];
            require(pipelines_.contains(dispatch.pipeline) && dispatch.groupsX > 0,
                    "Dispatch must reference a live pipeline and nonzero group count");
            for (const auto handle : dispatch.buffers) {
                require(buffers_.contains(handle), "Dispatch must reference live buffers");
            }
            require(dispatch.buffers.size() == 9 && dispatch.buffers == dispatches.front().buffers &&
                        std::set<BufferHandle>(dispatch.buffers.begin(), dispatch.buffers.end()).size() == 9,
                    "Every stage must retain the same distinct particle, species, matrix and grid buffers");
            require(dispatch.constants.size() == sizeof(DispatchConstants),
                    "Dispatch constants must retain the shader's five vec4 slots");
            DispatchConstants constants;
            std::memcpy(&constants, dispatch.constants.data(), sizeof(constants));
            const auto particles = constants.counts[0];
            const auto cells = constants.counts[1];
            const auto stage = constants.counts[2];
            require(particles > 0 && (dispatches.size() == 7 ? stage == index : stage == 6 || stage == 7),
                    "Grid dispatches must execute clear, count, scan, scatter and evolve in dependency order");
            require(buffers_.at(dispatch.buffers[0]).size() >= particles * sizeof(Agent) &&
                        buffers_.at(dispatch.buffers[1]).size() >= particles * sizeof(Agent) &&
                        buffers_.at(dispatch.buffers[2]).size() >= particles * sizeof(std::uint32_t) &&
                        buffers_.at(dispatch.buffers[7]).size() >= particles * sizeof(std::uint32_t) &&
                        buffers_.at(dispatch.buffers[3]).size() >= 64 * sizeof(float),
                    "Particle state, species, indices and interaction matrix must fit allocated storage");
            if (stage != 7) {
                const auto& dimensions = constants.dimensions;
                require(dimensions[0] >= 1 && dimensions[0] <= 32 && dimensions[2] == dimensions[0] &&
                            dimensions[1] == (constants.counts[3] == 1 ? dimensions[0] : 1U) &&
                            cells == dimensions[0] * dimensions[1] * dimensions[2] &&
                            dimensions[3] == (cells + 127) / 128 && constants.counts[3] <= 1,
                        "GPU grid dimensions and scan blocks must match the selected spatial dimension");
                for (const auto binding : {4U, 5U, 6U}) {
                    require(buffers_.at(dispatch.buffers[binding]).size() >= cells * sizeof(std::uint32_t),
                            "Grid occupancy, offsets and cursors must fit every cell");
                }
                require(buffers_.at(dispatch.buffers[8]).size() >= dimensions[3] * sizeof(std::uint32_t),
                        "Grid scans must have space for every block total");
                require(dispatches.size() == 7 || constants.interaction[1] == 0.0F,
                        "Forces require a complete grid build before evolution");
                lastThreeDimensional = constants.counts[3] == 1;
                lastDimensions = dimensions;
            }
            const auto expectedGroups = stage == 3 ? 1U :
                ((stage == 0 || stage == 2 || stage == 4 ? cells : particles) + 127) / 128;
            require(dispatch.groupsX == expectedGroups,
                    "Each compute stage must dispatch enough workgroups for its cells or particles");
            lastStages.push_back(stage);
            if (stage == 6 || stage == 7) {
                buffers_.at(dispatch.buffers[1]) = buffers_.at(dispatch.buffers[0]);
            }
        }
        ++executions;
    }
    void waitIdle() override {}

    void replaceState(BufferHandle handle, std::span<const Agent> agents) {
        auto& storage = buffers_.at(handle);
        require(storage.size() >= agents.size_bytes(), "Injected state must fit its GPU buffer");
        std::memcpy(storage.data(), agents.data(), agents.size_bytes());
    }
    [[nodiscard]] std::size_t liveBuffers() const { return buffers_.size(); }
    [[nodiscard]] std::size_t livePipelines() const { return pipelines_.size(); }

    bool failAllocation{false};
    bool failPipeline{false};
    bool failDownload{false};
    bool failExecution{false};
    int dispatchesUntilFailure{-1};
    int uploadsUntilFailure{-1};
    std::size_t uploads{};
    std::size_t downloads{};
    std::size_t executions{};
    bool lastThreeDimensional{};
    std::array<std::uint32_t, 4> lastDimensions{};
    std::vector<std::uint32_t> lastStages;

private:
    struct DispatchConstants {
        std::array<std::uint32_t, 4> counts;
        std::array<std::uint32_t, 4> dimensions;
        std::array<float, 4> dynamics;
        std::array<float, 4> interaction;
        std::array<float, 4> cellWidth;
    };
    static_assert(sizeof(DispatchConstants) == 80);
    std::uint64_t nextHandle_{1};
    std::map<BufferHandle, std::vector<std::byte>> buffers_;
    std::set<PipelineHandle> pipelines_;
};

void transferRetainsStateAndSettings(bool threeDimensional) {
    MemoryComputeDevice device;
    {
        ParticleLifeScene scene{12, 21};
        require(scene.backend() == Backend::CPU && !scene.gpuAvailable() && scene.stateBuffer() == 0,
                "A new scene must expose CPU state without a GPU resource");
        scene.setThreeDimensional(threeDimensional);
        scene.setSpeciesCount(4);
        scene.setInteraction(1, 2, 0.87F);
        auto parameters = scene.parameters();
        parameters.force = 54.0F;
        parameters.friction = 2.0F;
        scene.setParameters(parameters);
        scene.advance(ParticleLifeScene::FixedStepSeconds);
        scene.setPaused(true);
        auto expected = scene.readback();
        const auto rules = scene.matrix();
        const std::vector<std::uint8_t> types(scene.species().begin(), scene.species().end());
        scene.initializeGpu(device, "unused.spv");
        require(scene.gpuAvailable() && scene.backend() == Backend::CPU && scene.stateBuffer() == 0,
                "Initialization must make GPU selection available without changing the active backend");
        scene.setBackend(Backend::GPU);
        require(scene.agents().empty() && scene.count() == expected.size() && scene.stateBuffer() != 0 &&
                    scene.totalSteps() == 1 && scene.paused(),
                "GPU state must retain count, pause and clock while hiding stale CPU particles");
        require(device.downloads == 0 && scene.matrix() == rules && scene.seed() == 21 &&
                    scene.speciesCount() == 4 && std::equal(types.begin(), types.end(), scene.species().begin()),
                "Upload must retain matrix, seed and species without any implicit download");
        requireSameState(expected, scene.readback());

        // Emulate a GPU-side state change, including fields which setAgents
        // would rewrite. A switch must download verbatim, without resetting.
        expected[0].position.x += 0.25F;
        expected[0].velocity.z += 0.5F;
        if (threeDimensional) {
            expected[0].position.y += 0.25F;
            expected[0].velocity.y += 0.5F;
        }
        expected[0].color = {0.1F, 0.2F, 0.3F};
        expected[0].reserved = 4.5F;
        device.replaceState(scene.stateBuffer(), expected);
        const auto beforeDownload = device.downloads;
        scene.setBackend(Backend::CPU);
        require(device.downloads == beforeDownload + 1 && scene.stateBuffer() == 0 &&
                    scene.backend() == Backend::CPU && scene.totalSteps() == 1 && scene.paused(),
                "GPU-to-CPU switch must perform one explicit download and retain completed time");
        requireSameState(expected, scene.agents());
        require(scene.parameters().force == 54.0F && scene.parameters().friction == 2.0F &&
                    scene.matrix() == rules && scene.threeDimensional() == threeDimensional,
                "Settings and dimension edited before transfer must be preserved");
        scene.setBackend(Backend::GPU);
        scene.setBackend(Backend::CPU);
        requireSameState(expected, scene.agents());
    }
    require(device.liveBuffers() == 0 && device.livePipelines() == 0,
            "Destroying the scene must release every GPU resource");
}

void residentStepsAndRepopulation(bool threeDimensional) {
    MemoryComputeDevice device;
    ParticleLifeScene scene{8, 17};
    scene.setThreeDimensional(threeDimensional);
    const auto initial = scene.readback();
    scene.advance(ParticleLifeScene::FixedStepSeconds * 1.5);
    scene.initializeGpu(device, "unused.spv");
    scene.setBackend(Backend::GPU);
    require(scene.totalSteps() == 1 && scene.advance(ParticleLifeScene::FixedStepSeconds * 0.5) == 0,
            "Switching must retain completed time and discard the old fractional step");
    require(scene.advance(ParticleLifeScene::FixedStepSeconds * 0.5) == 1 &&
                device.executions == 1 && device.downloads == 0 &&
                device.lastThreeDimensional == threeDimensional,
            "Ordinary GPU stepping must dispatch without downloading particle state");
    scene.setPaused(true);
    require(scene.singleStep() && scene.totalSteps() == 3 && device.downloads == 0,
            "Paused single-step must use GPU state and advance the clock without readback");
    scene.randomizeMatrix(121);
    auto parameters = scene.parameters();
    parameters.force = 11.0F;
    const auto beforeParameterEdit = device.executions;
    scene.setParameters(parameters);
    require(device.executions == beforeParameterEdit && device.downloads == 0,
            "Changing force parameters must not transfer or rewrite GPU particles");
    parameters.maxSpeed = 2.0F;
    scene.setParameters(parameters);
    require(device.executions == beforeParameterEdit + 1 && device.downloads == 0,
            "Lowering speed must use a GPU clamp dispatch without readback");
    require(device.lastStages == std::vector<std::uint32_t>{7},
            "Speed changes must run only the clamp stage");
    const auto rules = scene.matrix();
    scene.reset();
    require(scene.backend() == Backend::GPU && scene.paused() && scene.totalSteps() == 0 &&
                scene.matrix() == rules && scene.parameters().maxSpeed == 2.0F && device.downloads == 0,
            "GPU reset must retain controls while uploading a deterministic initial population");
    requireSameState(initial, scene.readback());
    const auto beforePopulationEdits = device.downloads;
    scene.setCount(11);
    scene.setSpeciesCount(8);
    scene.setSeed(42);
    ParticleLifeScene reference{11, 42};
    reference.setThreeDimensional(threeDimensional);
    reference.setSpeciesCount(8);
    require(scene.count() == 11 && scene.species().size() == 11 && scene.speciesCount() == 8 &&
                scene.seed() == 42 && scene.totalSteps() == 0 && scene.paused() &&
                device.downloads == beforePopulationEdits,
            "GPU population controls must commit metadata and state without downloading stale particles");
    requireSameState(reference.agents(), scene.readback());
    auto replacement = reference.readback();
    replacement[0].position.x = 0.0F;
    scene.setAgents(replacement, reference.species());
    requireSameState(replacement, scene.readback());
    scene.setCount(0);
    const auto beforeEmptyRead = device.downloads;
    require(scene.count() == 0 && scene.species().empty() && scene.agents().empty() &&
                scene.singleStep() && scene.readback().empty() && device.downloads == beforeEmptyRead,
            "Empty GPU scenes must reset and step without pointless downloads");
    scene.setBackend(Backend::CPU);
    require(scene.count() == 0 && scene.agents().empty(), "Empty scenes must transfer back to CPU");
}

void gpuDimensionChangesAndGridDispatches() {
    MemoryComputeDevice device;
    ParticleLifeScene scene{257, 123};
    scene.initializeGpu(device, "unused.spv");
    scene.setBackend(Backend::GPU);
    scene.setSpeciesCount(4);
    scene.setInteraction(1, 2, 0.87F);
    const auto matrix = scene.matrix();
    auto parameters = scene.parameters();
    parameters.friction = 2.0F;
    scene.setParameters(parameters);
    scene.advance(ParticleLifeScene::FixedStepSeconds * 1.5);
    scene.setPaused(true);
    for (const bool threeDimensional : {true, false}) {
        const auto previousDownloads = device.downloads;
        scene.setThreeDimensional(threeDimensional);
        require(scene.threeDimensional() == threeDimensional && scene.backend() == Backend::GPU &&
                    scene.totalSteps() == 0 && scene.paused() && scene.count() == 257 &&
                    scene.seed() == 123 && scene.speciesCount() == 4 && scene.matrix() == matrix &&
                    scene.parameters().friction == 2.0F && device.downloads == previousDownloads,
                "GPU dimension changes must upload new particles and retain controls without reading stale state");
        ParticleLifeScene reference{257, 123};
        reference.setSpeciesCount(4);
        reference.setThreeDimensional(threeDimensional);
        requireSameState(reference.agents(), scene.readback());
        const auto beforeStep = device.downloads;
        require(scene.singleStep() && device.downloads == beforeStep &&
                    device.lastThreeDimensional == threeDimensional &&
                    device.lastStages == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5, 6},
                "Both dimensions must execute the complete GPU grid pipeline without state readback");
        const auto buffer = scene.stateBuffer();
        const auto beforeNoOp = device.uploads;
        scene.setThreeDimensional(threeDimensional);
        require(scene.stateBuffer() == buffer && scene.totalSteps() == 1 && device.uploads == beforeNoOp,
                "Selecting the current GPU dimension must preserve the active snapshot and clock");
    }
    scene.setThreeDimensional(true);
    parameters.interactionRadius = 0.1F;
    scene.setParameters(parameters);
    require(scene.singleStep() && device.lastDimensions == std::array<std::uint32_t, 4>{32, 32, 32, 256},
            "Tiny 3D radii must cap grid allocation and dispatch every scan block");
    parameters.interactionRadius = 50.0F;
    scene.setParameters(parameters);
    require(scene.singleStep() && device.lastDimensions == std::array<std::uint32_t, 4>{2, 2, 2, 1},
            "Large 3D radii must use the complete coarse periodic grid");
    parameters.force = 0.0F;
    scene.setParameters(parameters);
    const auto beforeFreeMotion = device.downloads;
    require(scene.singleStep() && device.lastStages == std::vector<std::uint32_t>{6} &&
                device.lastThreeDimensional && device.downloads == beforeFreeMotion,
            "Force-free 3D evolution must skip grid construction and remain resident on the GPU");
}

void failuresPreserveTheActiveState(bool threeDimensional) {
    MemoryComputeDevice device;
    ParticleLifeScene scene{8, 37};
    scene.setThreeDimensional(threeDimensional);
    scene.advance(ParticleLifeScene::FixedStepSeconds);
    const auto original = scene.readback();
    requireFailure([&] { scene.setBackend(Backend::GPU); }, "Uninitialized GPU selection must fail");
    requireFailure([&] { scene.setBackend(static_cast<Backend>(99)); }, "Unknown backends must fail");
    device.failPipeline = true;
    requireFailure([&] { scene.initializeGpu(device, "unused.spv"); }, "Failed initialization must propagate");
    device.failPipeline = false;
    require(!scene.gpuAvailable() && scene.backend() == Backend::CPU,
            "A failed first initialization must leave CPU active");
    scene.initializeGpu(device, "unused.spv");
    const auto initializedBuffers = device.liveBuffers();
    device.failAllocation = true;
    requireFailure([&] { scene.setBackend(Backend::GPU); }, "GPU allocation failure must reject transfer");
    device.failAllocation = false;
    for (int allowedUploads : {0, 1}) {
        device.uploadsUntilFailure = allowedUploads;
        requireFailure([&] { scene.setBackend(Backend::GPU); }, "Partial GPU upload failure must reject transfer");
        device.uploadsUntilFailure = -1;
        require(scene.backend() == Backend::CPU && scene.totalSteps() == 1 &&
                    device.liveBuffers() == initializedBuffers,
                "A failed upload must retain CPU state and clean up partial allocations");
        requireSameState(original, scene.agents());
    }
    scene.setBackend(Backend::GPU);
    scene.setPaused(true);
    const auto buffer = scene.stateBuffer();
    const auto activeBuffers = device.liveBuffers();
    const auto matrix = scene.matrix();
    const auto parameters = scene.parameters();
    const auto world = scene.world();
    device.failDownload = true;
    requireFailure([&] { scene.setBackend(Backend::CPU); }, "Failed download must reject CPU selection");
    device.failDownload = false;
    requireFailure([&] { scene.initializeGpu(device, "unused.spv"); }, "Active GPU cannot be reinitialized");
    device.uploadsUntilFailure = 1;
    requireFailure([&] { scene.setCount(13); }, "Failed GPU population resize must propagate");
    device.uploadsUntilFailure = 1;
    requireFailure([&] { scene.setSpeciesCount(7); }, "Failed GPU species reset must propagate");
    device.uploadsUntilFailure = 1;
    requireFailure([&] { scene.setSeed(99); }, "Failed GPU seed reset must propagate");
    device.uploadsUntilFailure = 1;
    requireFailure([&] { scene.reset(); }, "Failed GPU reset must propagate");
    device.uploadsUntilFailure = 1;
    requireFailure([&] { scene.setThreeDimensional(!threeDimensional); },
                   "Failed GPU dimension reset must propagate");
    device.uploadsUntilFailure = 1;
    requireFailure([&] { scene.setAgents(original, scene.species()); }, "Failed replacement upload must propagate");
    device.uploadsUntilFailure = -1;
    device.failExecution = true;
    auto slower = parameters;
    slower.maxSpeed = 1.0F;
    requireFailure([&] { scene.setParameters(slower); }, "Failed speed clamp must propagate");
    requireFailure([&] { scene.singleStep(); }, "Failed GPU step must propagate");
    device.failExecution = false;
    for (const int completedStages : {0, 3, 6}) {
        device.dispatchesUntilFailure = completedStages;
        requireFailure([&] { scene.singleStep(); }, "Partially executed GPU grid batches must propagate failure");
        device.dispatchesUntilFailure = -1;
        require(scene.stateBuffer() == buffer && scene.totalSteps() == 1,
                "A partial grid build must never publish the inactive snapshot or advance time");
    }
    require(scene.backend() == Backend::GPU && scene.stateBuffer() == buffer &&
                scene.count() == 8 && scene.speciesCount() == 6 && scene.seed() == 37 &&
                scene.totalSteps() == 1 && scene.paused() && scene.matrix() == matrix &&
                scene.parameters().maxSpeed == parameters.maxSpeed && device.liveBuffers() == activeBuffers &&
                scene.threeDimensional() == threeDimensional && scene.world().minimum == world.minimum &&
                scene.world().maximum == world.maximum,
            "Failed GPU operations must preserve active buffers, metadata, parameters and completed time");
    requireSameState(original, scene.readback());
    scene.setBackend(Backend::CPU);
    requireSameState(original, scene.agents());
    scene.setPaused(false);
    require(scene.advance(ParticleLifeScene::FixedStepSeconds) == 1,
            "CPU stepping must remain usable after recovering from failed GPU operations");
}

} // namespace

int main() {
    try {
        for (const bool threeDimensional : {false, true}) {
            transferRetainsStateAndSettings(threeDimensional);
            residentStepsAndRepopulation(threeDimensional);
            failuresPreserveTheActiveState(threeDimensional);
        }
        gpuDimensionChangesAndGridDispatches();
        std::cout << "Particle life backend tests passed (transfers, residency, reset, clock, and failure recovery)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Particle life backend test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
