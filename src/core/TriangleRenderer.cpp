#include "core/TriangleRenderer.h"
#include "core/Context.h"
#include "core/Pipeline.h"
#include <utility>

namespace emergences::core {
TriangleRenderer::TriangleRenderer(const Context& context, const Window& window,
                                   std::filesystem::path shaderDirectory)
    : context_(context), shaderDirectory_(std::move(shaderDirectory)), renderer_(context, window) {}

TriangleRenderer::~TriangleRenderer() = default;

bool TriangleRenderer::drawFrame() {
    return renderer_.drawFrame(
        [this](VkRenderPass pass, std::uint32_t) {
            pipeline_ = std::make_unique<Pipeline>(context_, pass, shaderDirectory_);
        },
        [this](VkCommandBuffer commands, VkExtent2D, std::size_t) {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
            vkCmdDraw(commands, 3, 1, 0, 0);
        });
}

std::size_t TriangleRenderer::swapchainGeneration() const noexcept {
    return renderer_.swapchainGeneration();
}
} // namespace emergences::core
