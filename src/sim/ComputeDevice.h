#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace emergences::sim {

using BufferHandle = std::uint64_t;
using PipelineHandle = std::uint64_t;

struct ComputeDispatch {
    PipelineHandle pipeline{};
    std::vector<BufferHandle> buffers;
    std::vector<std::byte> constants;
    std::uint32_t groupsX{1};
};

// Backend-neutral, synchronous compute service. No native graphics types cross
// this interface. execute orders dispatches and completes them before returning;
// implementations also synchronize storage shared with their renderer. Handles
// belong to the device and stay valid until explicitly destroyed. The device
// must outlive all simulations using it. A zero handle is never a valid resource.
class ComputeDevice {
public:
    virtual ~ComputeDevice() = default;
    virtual BufferHandle createBuffer(std::size_t bytes) = 0;
    virtual void destroyBuffer(BufferHandle) noexcept = 0;
    virtual void upload(BufferHandle, std::span<const std::byte>) = 0;
    virtual void download(BufferHandle, std::span<std::byte>) = 0;
    virtual PipelineHandle createPipeline(std::string_view shaderPath,
        std::uint32_t storageBindings, std::uint32_t pushConstantBytes) = 0;
    virtual void destroyPipeline(PipelineHandle) noexcept = 0;
    virtual void execute(std::span<const ComputeDispatch>) = 0;
    virtual void waitIdle() = 0;
};

} // namespace emergences::sim
