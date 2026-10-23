#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>

namespace emergences::core {

class Buffer;
class Context;
class Pipeline;
struct SceneView;

// Context must outlive this renderer. Finish GPU uses before configure() or
// destruction; the immutable grid/bounds vertex buffer survives reconfiguration.
class LineRenderer final {
public:
    LineRenderer(const Context& context, std::filesystem::path shaderDirectory);
    ~LineRenderer();

    LineRenderer(const LineRenderer&) = delete;
    LineRenderer& operator=(const LineRenderer&) = delete;
    LineRenderer(LineRenderer&&) = delete;
    LineRenderer& operator=(LineRenderer&&) = delete;

    void configure(VkRenderPass renderPass, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    void draw(VkCommandBuffer commands, const SceneView& view, bool showGrid, bool showBounds);

private:
    const Context& context_;
    std::filesystem::path shaderDirectory_;
    std::unique_ptr<Buffer> vertices_;
    std::unique_ptr<Pipeline> pipeline_;
};

} // namespace emergences::core
