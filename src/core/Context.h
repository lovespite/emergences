#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace emergences::core {

class Window;

// The Window must outlive this Context. Owns the Vulkan instance, surface, and
// device lifetimes; swapchain management and frame rendering belong elsewhere.
class Context final {
public:
    explicit Context(const Window& window);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    void waitIdle() const;
    [[nodiscard]] const std::string& deviceName() const noexcept { return deviceName_; }
    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkSurfaceKHR surface() const noexcept { return surface_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkQueue graphicsQueue() const noexcept { return graphicsQueue_; }
    [[nodiscard]] VkQueue presentQueue() const noexcept { return presentQueue_; }
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const noexcept { return graphicsQueueFamily_; }
    [[nodiscard]] std::uint32_t presentQueueFamily() const noexcept { return presentQueueFamily_; }
    [[nodiscard]] bool validationEnabled() const noexcept { return validationEnabled_; }
    [[nodiscard]] std::uint32_t validationErrorCount() const noexcept {
        return validationErrorCount_.load(std::memory_order_relaxed);
    }

private:
    void createInstance();
    void selectPhysicalDevice();
    void createDevice();
    void cleanup() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    PFN_vkGetPhysicalDeviceProperties2KHR getPhysicalDeviceProperties2_ = nullptr;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily_ = 0;
    std::uint32_t presentQueueFamily_ = 0;
    std::string deviceName_;
    bool validationEnabled_ = false;
    std::atomic<std::uint32_t> validationErrorCount_{0};
};

} // namespace emergences::core
