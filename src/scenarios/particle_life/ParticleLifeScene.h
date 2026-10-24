#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "sim/Agent.h"
#include "sim/Backend.h"
#include "sim/ComputeDevice.h"
#include "sim/World.h"
#include "sim/cpu/SpatialHash.h"

namespace emergences::scenarios::particle_life {

class ParticleLifeGpu;

struct ParticleLifeParameters {
    float interactionRadius{12.0F};
    float force{30.0F};
    float friction{4.0F};
    float repulsion{2.0F};
    float coreRatio{0.3F};
    float maxSpeed{20.0F};
};

// Particle life on a periodic XZ plane or in a periodic 3D volume. Matrix rows describe
// the receiving species, columns the source species; interactions may be
// asymmetric. Borrowed views remain valid until their state is changed.
class ParticleLifeScene {
public:
    static constexpr std::size_t MaxParticles = 10000;
    static constexpr std::size_t MaxSpecies = 8;
    static constexpr std::size_t MaxStepsPerAdvance = 8;
    static constexpr double FixedStepSeconds = 1.0 / 120.0;
    static constexpr float WorldHalfExtent = 50.0F;

    explicit ParticleLifeScene(std::size_t count = 2000, std::uint32_t seed = 12345U);
    ~ParticleLifeScene();

    // Initialize only while CPU is active. The compute device must outlive the
    // scene. A failed initialization preserves the existing backend and state.
    void initializeGpu(sim::ComputeDevice& device, std::string shaderPath);
    // Transfers state explicitly; retains parameters, matrix, seed, species,
    // pause and completed steps. Discards only fractional accumulated time.
    // Failure preserves the active state. GPU must have been initialized first.
    void setBackend(sim::Backend backend);
    [[nodiscard]] sim::Backend backend() const noexcept;
    [[nodiscard]] bool gpuAvailable() const noexcept;
    // GPU rendering consumes this buffer directly; CPU mode returns zero.
    [[nodiscard]] sim::BufferHandle stateBuffer() const noexcept;
    // Explicit diagnostic/transfer snapshot; never called by ordinary GPU steps.
    [[nodiscard]] std::vector<sim::Agent> readback() const;

    [[nodiscard]] bool threeDimensional() const noexcept;
    // Switching dimensions regenerates the seeded population and resets time,
    // retaining parameters, matrix, backend and pause. Failure is atomic.
    void setThreeDimensional(bool enabled);

    [[nodiscard]] const ParticleLifeParameters& parameters() const noexcept;
    // Finite ranges: radius (0,50], force [0,200], friction [0,30], repulsion
    // [0,10], coreRatio [0.01,0.95], maxSpeed [0,100]. Invalid changes are atomic.
    // Lowering maxSpeed immediately clamps existing velocities.
    void setParameters(const ParticleLifeParameters& parameters);
    [[nodiscard]] std::size_t count() const noexcept;
    void setCount(std::size_t count);
    [[nodiscard]] std::size_t speciesCount() const noexcept;
    void setSpeciesCount(std::size_t count);
    [[nodiscard]] std::uint32_t seed() const noexcept;
    void setSeed(std::uint32_t seed);
    // Restore seeded particles and clock, preserving settings, matrix and pause.
    void reset();

    // Same fixed-step and optional wall-clock budget semantics as AntScene.
    // Invalid times are rejected even while paused. A zero work budget still
    // permits one pending step; excess complete steps are dropped.
    std::size_t advance(double elapsedSeconds,
                        double workBudgetSeconds = std::numeric_limits<double>::infinity());
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;
    bool singleStep();
    [[nodiscard]] std::uint64_t totalSteps() const noexcept;
    [[nodiscard]] double elapsedSimulationSeconds() const noexcept;
    [[nodiscard]] bool lastAdvanceLimited() const noexcept;

    // Empty while GPU is active: callers must not observe stale CPU positions.
    [[nodiscard]] std::span<const sim::Agent> agents() const noexcept;
    // Species labels remain available on CPU for either backend.
    [[nodiscard]] std::span<const std::uint8_t> species() const noexcept;
    [[nodiscard]] const sim::World& world() const noexcept;
    [[nodiscard]] const std::array<float, MaxSpecies * MaxSpecies>& matrix() const noexcept;
    // Indices may address any of the eight species, including inactive ones.
    // Entries must be finite in [-1,1]; edits retain particle state and clock.
    void setInteraction(std::size_t receiver, std::size_t source, float value);
    void setMatrix(const std::array<float, MaxSpecies * MaxSpecies>& matrix);
    // Changes only the matrix, without changing the population seed.
    void randomizeMatrix(std::uint32_t seed);
    [[nodiscard]] static glm::vec3 speciesColor(std::size_t index) noexcept;

    // Replace the particles and restart the clock. Requires matching lengths,
    // valid active species, unique IDs, finite fields, positive size, RGB in
    // [0,1], positions inside the world, and velocity magnitude <= maxSpeed.
    // In 2D, Y position/velocity must be zero. Display colors come from speciesColor.
    // Validation failure leaves the entire scene unchanged; aliases are safe.
    void setAgents(std::span<const sim::Agent> agents, std::span<const std::uint8_t> species);

    // Signed radial force: positive attracts toward the source. r >= 1 is zero.
    // Requires finite r >= 0, attraction in [-1,1], coreRatio in (0,1), and
    // finite nonnegative repulsion. Throws invalid_argument on invalid input.
    [[nodiscard]] static float interactionForce(float normalizedDistance, float attraction,
                                                 float coreRatio, float repulsion);

private:
    void step();
    void resetClock() noexcept;
    void resetPopulation(std::size_t count, std::size_t speciesCount, std::uint32_t seed,
                         bool threeDimensional);

    ParticleLifeParameters parameters_;
    std::uint32_t seed_;
    std::size_t speciesCount_{6};
    bool threeDimensional_{false};
    sim::World world_{{-WorldHalfExtent, -1.0F, -WorldHalfExtent},
                      {WorldHalfExtent, 1.0F, WorldHalfExtent}, sim::BoundaryMode::Wrap};
    std::array<float, MaxSpecies * MaxSpecies> matrix_{};
    std::vector<sim::Agent> agents_;
    std::vector<std::uint8_t> species_;
    std::vector<glm::vec3> nextVelocities_;
    sim::cpu::SpatialHash grid_;
    std::vector<sim::Neighbor> neighbors_;
    std::unique_ptr<ParticleLifeGpu> gpu_;
    sim::Backend backend_{sim::Backend::CPU};
    double accumulator_{0.0};
    std::uint64_t totalSteps_{0};
    bool paused_{false};
    bool lastAdvanceLimited_{false};
};

} // namespace emergences::scenarios::particle_life
