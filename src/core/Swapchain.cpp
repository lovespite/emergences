#include "core/Swapchain.h"
#include "core/Context.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace emergences::core {

namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

// Surface formats and swapchain image counts may change between queries.
template <typename T, typename Enumerate>
std::vector<T> enumerate(Enumerate&& query, const char* operation) {
    for (;;) {
        std::uint32_t count = 0;
        const VkResult countResult = query(&count, static_cast<T*>(nullptr));
        if (countResult != VK_SUCCESS && countResult != VK_INCOMPLETE) {
            check(countResult, operation);
        }
        if (count == 0) {
            return {};
        }
        std::vector<T> values(count);
        const VkResult result = query(&count, values.data());
        if (result == VK_INCOMPLETE) {
            continue;
        }
        check(result, operation);
        values.resize(count);
        return values;
    }
}

VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    if (formats.empty()) {
        throw std::runtime_error("The window surface has no supported Vulkan formats");
    }
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
        return {VK_FORMAT_B8G8R8A8_SRGB, formats.front().colorSpace};
    }
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
    constexpr std::array candidates{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (const auto candidate : candidates) {
        if ((supported & candidate) != 0) {
            return candidate;
        }
    }
    throw std::runtime_error("The window surface has no supported composite alpha mode");
}

VkFormat chooseDepthFormat(VkPhysicalDevice device, VkExtent2D extent) {
    constexpr std::array candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D16_UNORM,
    };
    for (const auto candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
            continue;
        }
        VkImageFormatProperties imageProperties{};
        const auto result = vkGetPhysicalDeviceImageFormatProperties(
            device, candidate, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &imageProperties);
        if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
            continue;
        }
        check(result, "Querying depth attachment image support");
        if (extent.width <= imageProperties.maxExtent.width &&
            extent.height <= imageProperties.maxExtent.height &&
            (imageProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0) {
            return candidate;
        }
    }
    throw std::runtime_error("No supported depth attachment format matches the swapchain extent");
}

std::uint32_t findDepthMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                                 std::uint32_t allowedTypes) {
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        const auto flags = properties.memoryTypes[i].propertyFlags;
        if ((allowedTypes & (1U << i)) != 0 &&
            (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 &&
            (flags & (VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT | VK_MEMORY_PROPERTY_PROTECTED_BIT)) == 0) {
            return i;
        }
    }
    throw std::runtime_error("No device-local memory is available for a depth attachment");
}

} // namespace

Swapchain::Swapchain(const Context& context, VkExtent2D framebufferExtent)
    : context_(context) {
    try {
        createSwapchain(framebufferExtent);
        createDepthAttachments();
        createRenderPass();
        createFramebuffers();
    } catch (...) {
        cleanup();
        throw;
    }
}

Swapchain::~Swapchain() { cleanup(); }

void Swapchain::createSwapchain(VkExtent2D framebufferExtent) {
    VkSurfaceCapabilitiesKHR capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
              context_.physicalDevice(), context_.surface(), &capabilities),
          "Querying surface capabilities");
    const auto formats = enumerate<VkSurfaceFormatKHR>(
        [this](auto* count, auto* data) {
            return vkGetPhysicalDeviceSurfaceFormatsKHR(
                context_.physicalDevice(), context_.surface(), count, data);
        }, "Enumerating surface formats");
    const auto surfaceFormat = chooseFormat(formats);
    format_ = surfaceFormat.format;

    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("The window surface does not support color attachment images");
    }
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        extent_ = capabilities.currentExtent;
    } else {
        extent_ = {
            std::clamp(framebufferExtent.width,
                       capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(framebufferExtent.height,
                       capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }
    if (extent_.width == 0 || extent_.height == 0) {
        throw std::runtime_error("Cannot create a swapchain for a zero-sized framebuffer");
    }

    std::uint32_t imageCount = capabilities.minImageCount;
    if (imageCount < std::numeric_limits<std::uint32_t>::max()) {
        ++imageCount;
    }
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    const std::array queueFamilies{context_.graphicsQueueFamily(), context_.presentQueueFamily()};
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = context_.surface();
    info.minImageCount = imageCount;
    info.imageFormat = format_;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (queueFamilies[0] != queueFamilies[1]) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilies.size());
        info.pQueueFamilyIndices = queueFamilies.data();
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = chooseCompositeAlpha(capabilities.supportedCompositeAlpha);
    // FIFO is required for every presentation-capable Vulkan surface.
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(context_.device(), &info, nullptr, &swapchain_),
          "Creating the Vulkan swapchain");

    const auto images = enumerate<VkImage>(
        [this](auto* count, auto* data) {
            return vkGetSwapchainImagesKHR(context_.device(), swapchain_, count, data);
        }, "Enumerating swapchain images");
    if (images.empty()) {
        throw std::runtime_error("The Vulkan swapchain contains no images");
    }
    // Allocate all slots first so a later allocation cannot lose a created handle.
    imageViews_.resize(images.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < images.size(); ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        check(vkCreateImageView(context_.device(), &viewInfo, nullptr, &imageViews_[i]),
              "Creating a swapchain image view");
    }
}

void Swapchain::createDepthAttachments() {
    depthFormat_ = chooseDepthFormat(context_.physicalDevice(), extent_);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(context_.physicalDevice(), &memoryProperties);

    // Allocate all slots before creating handles so constructor cleanup can
    // release every partially initialized attachment after any later failure.
    depthAttachments_.resize(imageViews_.size());
    for (auto& depth : depthAttachments_) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = depthFormat_;
        imageInfo.extent = {extent_.width, extent_.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(context_.device(), &imageInfo, nullptr, &depth.image),
              "Creating a depth attachment image");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(context_.device(), depth.image, &requirements);
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = findDepthMemoryType(memoryProperties, requirements.memoryTypeBits);
        check(vkAllocateMemory(context_.device(), &allocation, nullptr, &depth.memory),
              "Allocating depth attachment memory");
        check(vkBindImageMemory(context_.device(), depth.image, depth.memory, 0),
              "Binding depth attachment memory");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depth.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (depthFormat_ == VK_FORMAT_D24_UNORM_S8_UINT ||
            depthFormat_ == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        check(vkCreateImageView(context_.device(), &viewInfo, nullptr, &depth.view),
              "Creating a depth attachment image view");
    }
}

void Swapchain::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = format_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    const std::array attachments{colorAttachment, depthAttachment};
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    check(vkCreateRenderPass(context_.device(), &info, nullptr, &renderPass_),
          "Creating the swapchain render pass");
}

void Swapchain::createFramebuffers() {
    framebuffers_.resize(imageViews_.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < imageViews_.size(); ++i) {
        const std::array attachments{imageViews_[i], depthAttachments_[i].view};
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass_;
        info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.width = extent_.width;
        info.height = extent_.height;
        info.layers = 1;
        check(vkCreateFramebuffer(context_.device(), &info, nullptr, &framebuffers_[i]),
              "Creating a swapchain framebuffer");
    }
}

void Swapchain::cleanup() noexcept {
    for (const auto framebuffer : framebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(context_.device(), framebuffer, nullptr);
        }
    }
    framebuffers_.clear();
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_.device(), renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    for (const auto& depth : depthAttachments_) {
        if (depth.view != VK_NULL_HANDLE) {
            vkDestroyImageView(context_.device(), depth.view, nullptr);
        }
        if (depth.image != VK_NULL_HANDLE) {
            vkDestroyImage(context_.device(), depth.image, nullptr);
        }
        if (depth.memory != VK_NULL_HANDLE) {
            vkFreeMemory(context_.device(), depth.memory, nullptr);
        }
    }
    depthAttachments_.clear();
    for (const auto imageView : imageViews_) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(context_.device(), imageView, nullptr);
        }
    }
    imageViews_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

} // namespace emergences::core
