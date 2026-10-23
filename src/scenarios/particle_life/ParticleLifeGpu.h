#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sim/Agent.h"
#include "sim/ComputeDevice.h"

namespace emergences::scenarios::particle_life {

struct ParticleLifeParameters;

// GPU-resident Particle Life state. The device must outlive this object.
// Stepping completes synchronously, but does not transfer particle state to the
// CPU: stateBuffer() can be consumed directly by the particle renderer.
class ParticleLifeGpu final {
public:
    ParticleLifeGpu(sim::ComputeDevice& device, std::string shaderPath);
    ~ParticleLifeGpu();
    ParticleLifeGpu(const ParticleLifeGpu&) = delete;
    ParticleLifeGpu& operator=(const ParticleLifeGpu&) = delete;

    // Replaces both simulation buffers and species atomically. Each species
    // must be below eight; particle fields are preserved, including IDs.
    void setState(std::span<const sim::Agent> agents, std::span<const std::uint8_t> species);
    void step(float dt, const ParticleLifeParameters& parameters,
              const std::array<float, 64>& matrix);
    void clampSpeed(float maximum);
    [[nodiscard]] std::vector<sim::Agent> readback();
    [[nodiscard]] sim::BufferHandle stateBuffer() const noexcept { return state_[current_]; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }

private:
    struct Constants;
    void cleanup() noexcept;
    void dispatch(const Constants& constants);

    sim::ComputeDevice& device_;
    sim::PipelineHandle pipeline_{};
    sim::BufferHandle state_[2]{};
    sim::BufferHandle species_{};
    sim::BufferHandle matrix_{};
    std::array<float, 64> uploadedMatrix_{};
    std::size_t count_{};
    unsigned current_{};
    bool hasUploadedMatrix_{};
};

} // namespace emergences::scenarios::particle_life
