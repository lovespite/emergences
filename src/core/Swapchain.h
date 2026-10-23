#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emergences::core {

class Context;

// Context must outlive this object. The caller must finish GPU work before
// destroying it; acquisition, presentation, and synchronization belong to the renderer.
class Swapchain final {
public:
    Swapchain(const Context& context, VkExtent2D framebufferExtent, bool antialiasing = false);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    [[nodiscard]] VkSwapchainKHR handle() const noexcept { return swapchain_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] VkRenderPass renderPass() const noexcept { return renderPass_; }
    [[nodiscard]] VkFramebuffer framebuffer(std::uint32_t index) const { return framebuffers_.at(index); }
    [[nodiscard]] std::size_t imageCount() const noexcept { return imageViews_.size(); }
    [[nodiscard]] VkSampleCountFlagBits sampleCount() const noexcept { return sampleCount_; }
    [[nodiscard]] VkSampleCountFlagBits supportedAntialiasingSampleCount() const noexcept {
        return supportedAntialiasingSampleCount_;
    }

private:
    struct Attachment {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    void createSwapchain(VkExtent2D framebufferExtent);
    void chooseAttachmentFormats(bool antialiasing);
    void createAttachments(std::vector<Attachment>& attachments, VkFormat format,
                           VkImageUsageFlags usage, VkImageAspectFlags aspect);
    void createRenderPass();
    void createFramebuffers();
    void cleanup() noexcept;

    const Context& context_;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits supportedAntialiasingSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    std::vector<VkImageView> imageViews_;
    // Each acquired image has independent attachment storage for overlapping frames.
    std::vector<Attachment> colorAttachments_;
    std::vector<Attachment> depthAttachments_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
};

} // namespace emergences::core
