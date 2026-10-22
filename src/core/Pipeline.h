#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>
#include <cstdint>
#include <span>

namespace emergences::core {

class Context;

struct PipelineOptions {
    std::filesystem::path vertexShader;
    std::filesystem::path fragmentShader;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    std::span<const VkVertexInputBindingDescription> bindings;
    std::span<const VkVertexInputAttributeDescription> attributes;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    std::uint32_t pushConstantBytes = 0;
    bool depthTest = false;
};

// The Context must outlive this pipeline. Finish submitted uses before destruction.
class Pipeline final {
public:
    Pipeline(const Context& context, VkRenderPass renderPass,
             const std::filesystem::path& shaderDirectory);
    Pipeline(const Context& context, VkRenderPass renderPass, const PipelineOptions& options);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = delete;
    Pipeline& operator=(Pipeline&&) = delete;

    [[nodiscard]] VkPipeline handle() const noexcept { return pipeline_; }
    [[nodiscard]] VkPipelineLayout layout() const noexcept { return layout_; }

private:
    void cleanup() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace emergences::core
