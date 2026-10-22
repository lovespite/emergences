#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace emergences::core {

class Context;
class Swapchain;
class Window;

// Owns swapchain, command buffers, and two frames in flight. Context and Window
// must outlive it. Draw callbacks may upload only the provided frame slot.
// Scene resources must stay alive until context.waitIdle() or this renderer is destroyed.
class FrameRenderer final {
public:
    using Configure = std::function<void(VkRenderPass, std::uint32_t)>;
    using Draw = std::function<void(VkCommandBuffer, VkExtent2D, std::size_t)>;
    static constexpr std::size_t framesInFlight = 2;

    FrameRenderer(const Context& context, const Window& window);
    ~FrameRenderer();

    FrameRenderer(const FrameRenderer&) = delete;
    FrameRenderer& operator=(const FrameRenderer&) = delete;
    FrameRenderer(FrameRenderer&&) = delete;
    FrameRenderer& operator=(FrameRenderer&&) = delete;

    // False means the window is minimized or acquisition requires recreation.
    [[nodiscard]] bool drawFrame(const Configure& configure, const Draw& draw);
    [[nodiscard]] std::size_t swapchainGeneration() const noexcept { return generation_; }

private:
    struct Frame {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence complete = VK_NULL_HANDLE;
    };

    void recreateSwapchain(std::pair<int, int> framebufferSize, const Configure& configure);
    void record(VkCommandBuffer commands, std::uint32_t imageIndex, const Draw& draw);
    void destroySwapchain() noexcept;
    void cleanup() noexcept;

    const Context& context_;
    const Window& window_;
    std::unique_ptr<Swapchain> swapchain_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::array<Frame, framesInFlight> frames_{};
    // Presentation can outlive a frame fence. Reuse only after reacquiring the
    // SAME swapchain image, which guarantees the previous present wait finished.
    std::vector<VkSemaphore> renderFinished_;
    std::pair<int, int> framebufferSize_{};
    std::size_t currentFrame_ = 0;
    std::size_t generation_ = 0;
    bool recreate_ = true;
};

} // namespace emergences::core
