#include "core/FrameRenderer.h"

#include "core/Context.h"
#include "core/Swapchain.h"
#include "core/Window.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace emergences::core {
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

} // namespace

FrameRenderer::FrameRenderer(const Context& context, const Window& window)
    : context_(context), window_(window) {
    try {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = context_.graphicsQueueFamily();
        check(vkCreateCommandPool(context_.device(), &poolInfo, nullptr, &commandPool_),
              "Creating the command pool");

        for (auto& frame : frames_) {
            VkCommandBufferAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocation.commandPool = commandPool_;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocation.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(context_.device(), &allocation, &frame.commands),
                  "Allocating frame commands");
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            check(vkCreateSemaphore(context_.device(), &semaphoreInfo, nullptr, &frame.imageAvailable),
                  "Creating image acquisition semaphore");
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(context_.device(), &fenceInfo, nullptr, &frame.complete),
                  "Creating frame fence");
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

FrameRenderer::~FrameRenderer() { cleanup(); }

void FrameRenderer::recreateSwapchain(std::pair<int, int> framebufferSize, const Configure& configure) {
    context_.waitIdle();
    // Reset recordings before destroying the objects they refer to.
    check(vkResetCommandPool(context_.device(), commandPool_, 0), "Resetting frame commands");
    destroySwapchain();
    swapchain_ = std::make_unique<Swapchain>(context_, VkExtent2D{
        static_cast<std::uint32_t>(framebufferSize.first),
        static_cast<std::uint32_t>(framebufferSize.second)});
    configure(swapchain_->renderPass(), static_cast<std::uint32_t>(swapchain_->imageCount()));

    renderFinished_.resize(swapchain_->imageCount(), VK_NULL_HANDLE);
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& semaphore : renderFinished_) {
        check(vkCreateSemaphore(context_.device(), &info, nullptr, &semaphore),
              "Creating presentation semaphore");
    }
    framebufferSize_ = framebufferSize;
    recreate_ = false;
    ++generation_;
}

bool FrameRenderer::drawFrame(const Configure& configure, const Draw& draw) {
    const auto size = window_.framebufferSize();
    if (size.first <= 0 || size.second <= 0) {
        // Keep the event loop responsive; the application waits for events.
        recreate_ = true;
        return false;
    }
    if (recreate_ || size != framebufferSize_) {
        recreateSwapchain(size, configure);
    }

    auto& frame = frames_[currentFrame_];
    constexpr auto forever = std::numeric_limits<std::uint64_t>::max();
    check(vkWaitForFences(context_.device(), 1, &frame.complete, VK_TRUE, forever),
          "Waiting for frame completion");
    std::uint32_t imageIndex = 0;
    const VkResult acquired = vkAcquireNextImageKHR(context_.device(), swapchain_->handle(),
                                                   forever, frame.imageAvailable,
                                                   VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_ = true;
        return false;
    }
    if (acquired != VK_SUBOPTIMAL_KHR) {
        check(acquired, "Acquiring swapchain image");
    }

    check(vkResetCommandBuffer(frame.commands, 0), "Resetting frame command buffer");
    record(frame.commands, imageIndex, draw);

    constexpr VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.commands;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished_[imageIndex];
    // Do not reset before acquisition: an OUT_OF_DATE return has no submission
    // to signal the fence, and the next frame would otherwise wait forever.
    check(vkResetFences(context_.device(), 1, &frame.complete), "Resetting frame fence");
    check(vkQueueSubmit(context_.graphicsQueue(), 1, &submit, frame.complete), "Submitting frame");

    const VkSwapchainKHR handle = swapchain_->handle();
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &handle;
    present.pImageIndices = &imageIndex;
    const VkResult presented = vkQueuePresentKHR(context_.presentQueue(), &present);
    if (presented != VK_ERROR_OUT_OF_DATE_KHR && presented != VK_SUBOPTIMAL_KHR) {
        check(presented, "Presenting frame");
    }
    recreate_ = acquired == VK_SUBOPTIMAL_KHR || presented == VK_SUBOPTIMAL_KHR ||
                presented == VK_ERROR_OUT_OF_DATE_KHR;
    currentFrame_ = (currentFrame_ + 1) % frames_.size();
    return presented != VK_ERROR_OUT_OF_DATE_KHR;
}

void FrameRenderer::record(VkCommandBuffer commands, std::uint32_t imageIndex, const Draw& draw) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commands, &begin), "Beginning frame commands");

    const auto extent = swapchain_->extent();
    const std::array<VkClearValue, 2> clear{{
        {.color = {{0.018F, 0.025F, 0.045F, 1.0F}}}, {.depthStencil = {1.0F, 0}}}};
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = swapchain_->renderPass();
    pass.framebuffer = swapchain_->framebuffer(imageIndex);
    pass.renderArea.extent = extent;
    pass.clearValueCount = static_cast<std::uint32_t>(clear.size());
    pass.pClearValues = clear.data();
    vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0F, 0.0F, static_cast<float>(extent.width),
                              static_cast<float>(extent.height), 0.0F, 1.0F};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    draw(commands, extent, currentFrame_);
    vkCmdEndRenderPass(commands);
    check(vkEndCommandBuffer(commands), "Ending frame commands");
}

void FrameRenderer::destroySwapchain() noexcept {
    for (const auto semaphore : renderFinished_) {
        vkDestroySemaphore(context_.device(), semaphore, nullptr);
    }
    renderFinished_.clear();
    swapchain_.reset();
}

void FrameRenderer::cleanup() noexcept {
    // Also covers exceptions from record/submit/present while a GPU frame is live.
    static_cast<void>(vkDeviceWaitIdle(context_.device()));
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_.device(), commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    destroySwapchain();
    for (auto& frame : frames_) {
        vkDestroyFence(context_.device(), frame.complete, nullptr);
        vkDestroySemaphore(context_.device(), frame.imageAvailable, nullptr);
        frame = {};
    }
}

} // namespace emergences::core
