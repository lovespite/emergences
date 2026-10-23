#pragma once

#include "sim/ComputeDevice.h"

#include <vulkan/vulkan.h>
#include <memory>

namespace emergences::core {

class Context;

// Synchronous compute on Context's graphics/compute queue. Context must outlive
// this object. Call from the rendering thread; queue access is not thread-safe.
class VulkanComputeDevice final : public sim::ComputeDevice {
public:
    explicit VulkanComputeDevice(const Context&);
    ~VulkanComputeDevice() override;
    VulkanComputeDevice(const VulkanComputeDevice&) = delete;
    VulkanComputeDevice& operator=(const VulkanComputeDevice&) = delete;

    sim::BufferHandle createBuffer(std::size_t bytes) override;
    void destroyBuffer(sim::BufferHandle) noexcept override;
    void upload(sim::BufferHandle, std::span<const std::byte>) override;
    void download(sim::BufferHandle, std::span<std::byte>) override;
    sim::PipelineHandle createPipeline(std::string_view shaderPath,
                                      std::uint32_t storageBindings,
                                      std::uint32_t pushConstantBytes) override;
    void destroyPipeline(sim::PipelineHandle) noexcept override;
    void execute(std::span<const sim::ComputeDispatch>) override;
    void waitIdle() override;

    [[nodiscard]] VkBuffer buffer(sim::BufferHandle) const;
    [[nodiscard]] VkDeviceSize bufferSize(sim::BufferHandle) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emergences::core
