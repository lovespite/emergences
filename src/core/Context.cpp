#include "core/Context.h"
#include "core/Window.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

namespace emergences::core {

namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

// Enumeration may change between the count and data calls; retry VK_INCOMPLETE.
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

bool hasExtension(const std::vector<VkExtensionProperties>& available, const char* name) {
    return std::ranges::any_of(available, [name](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

#if defined(EMERGENCES_ENABLE_VALIDATION)
VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userData) {
    const bool isValidation = (messageTypes & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) != 0;
    // Loader discovery diagnostics are GENERAL messages, not application API validation failures.
    if (isValidation && (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 &&
        userData != nullptr) {
        static_cast<std::atomic<std::uint32_t>*>(userData)->fetch_add(1, std::memory_order_relaxed);
    }
    const bool isLoader = data != nullptr && data->pMessageIdName != nullptr &&
                          std::strcmp(data->pMessageIdName, "Loader Message") == 0;
    const char* category = isValidation ? "validation" : isLoader ? "loader" : "general";
    std::fprintf(stderr, "[Vulkan %s] %s\n", category,
                 data != nullptr && data->pMessage != nullptr ? data->pMessage : "(no message)");
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo(std::atomic<std::uint32_t>& errorCount) {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = validationCallback;
    info.pUserData = &errorCount;
    return info;
}
#endif

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    [[nodiscard]] bool complete() const { return graphics.has_value() && present.has_value(); }
};

QueueFamilies findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    properties.resize(count);

    QueueFamilies families;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (properties[i].queueCount == 0) {
            continue;
        }
        VkBool32 supportsPresent = VK_FALSE;
        check(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supportsPresent),
              "Querying queue presentation support");
        const bool supportsGraphics = (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        // Prefer one family for both roles to simplify the first swapchain.
        if (supportsGraphics && supportsPresent == VK_TRUE) {
            return {i, i};
        }
        if (supportsGraphics && !families.graphics) {
            families.graphics = i;
        }
        if (supportsPresent == VK_TRUE && !families.present) {
            families.present = i;
        }
    }
    return families;
}

} // namespace

Context::Context(const Window& window) {
    try {
        if (glfwVulkanSupported() != GLFW_TRUE) {
            throw std::runtime_error("GLFW cannot find a Vulkan loader and compatible ICD; check the GPU driver");
        }
        createInstance();
        check(glfwCreateWindowSurface(instance_, window.nativeHandle(), nullptr, &surface_),
              "Creating the window surface");
        selectPhysicalDevice();
        createDevice();
    } catch (...) {
        cleanup();
        throw;
    }
}

Context::~Context() { cleanup(); }

void Context::createInstance() {
    const auto availableExtensions = enumerate<VkExtensionProperties>(
        [](auto* count, auto* data) { return vkEnumerateInstanceExtensionProperties(nullptr, count, data); },
        "Enumerating instance extensions");

    std::uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == nullptr || glfwExtensionCount == 0) {
        throw std::runtime_error("GLFW could not provide the Vulkan window surface extensions");
    }
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    for (const char* extension : extensions) {
        if (!hasExtension(availableExtensions, extension)) {
            throw std::runtime_error(std::string("Required Vulkan instance extension is unavailable: ") + extension);
        }
    }

    std::vector<const char*> layers;
#if defined(EMERGENCES_ENABLE_VALIDATION)
    constexpr const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    const auto availableLayers = enumerate<VkLayerProperties>(
        [](auto* count, auto* data) { return vkEnumerateInstanceLayerProperties(count, data); },
        "Enumerating instance layers");
    const bool hasValidation = std::ranges::any_of(availableLayers, [](const auto& layer) {
        return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    });
    const bool hasDebugUtils = hasExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (hasValidation) {
        layers.push_back(validationLayer);
        if (hasDebugUtils) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else {
            std::fprintf(stderr, "Warning: VK_EXT_debug_utils is unavailable; validation messages cannot be captured.\n");
        }
    } else {
        std::fprintf(stderr, "Warning: VK_LAYER_KHRONOS_validation is unavailable; continuing without validation. Install the LunarG Vulkan SDK to enable it.\n");
    }
    auto debugInfo = debugMessengerInfo(validationErrorCount_);
#endif

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Emergences";
    application.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    application.pEngineName = "Emergences Core";
    application.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    application.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
#if defined(EMERGENCES_ENABLE_VALIDATION)
    if (hasValidation && hasDebugUtils) {
        info.pNext = &debugInfo;
    }
#endif
    check(vkCreateInstance(&info, nullptr, &instance_), "Creating the Vulkan instance");

#if defined(EMERGENCES_ENABLE_VALIDATION)
    validationEnabled_ = hasValidation;
    if (hasValidation && hasDebugUtils) {
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessenger == nullptr) {
            throw std::runtime_error("Vulkan did not expose vkCreateDebugUtilsMessengerEXT");
        }
        check(createMessenger(instance_, &debugInfo, nullptr, &debugMessenger_),
              "Creating the Vulkan debug messenger");
    }
#endif
}

void Context::selectPhysicalDevice() {
    const auto devices = enumerate<VkPhysicalDevice>(
        [this](auto* count, auto* data) { return vkEnumeratePhysicalDevices(instance_, count, data); },
        "Enumerating Vulkan physical devices");
    int bestScore = -1;
    for (const auto candidate : devices) {
        const auto families = findQueueFamilies(candidate, surface_);
        if (!families.complete()) {
            continue;
        }
        const auto extensions = enumerate<VkExtensionProperties>(
            [candidate](auto* count, auto* data) {
                return vkEnumerateDeviceExtensionProperties(candidate, nullptr, count, data);
            }, "Enumerating device extensions");
        if (!hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }
        const auto formats = enumerate<VkSurfaceFormatKHR>(
            [this, candidate](auto* count, auto* data) {
                return vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface_, count, data);
            }, "Enumerating surface formats");
        const auto modes = enumerate<VkPresentModeKHR>(
            [this, candidate](auto* count, auto* data) {
                return vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, count, data);
            }, "Enumerating presentation modes");
        if (formats.empty() || modes.empty()) {
            continue;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 :
                          properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 10 : 1;
        if (score > bestScore) {
            physicalDevice_ = candidate;
            graphicsQueueFamily_ = *families.graphics;
            presentQueueFamily_ = *families.present;
            deviceName_ = properties.deviceName;
            bestScore = score;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("No Vulkan device supports graphics, window presentation, and VK_KHR_swapchain");
    }
}

void Context::createDevice() {
    constexpr float priority = 1.0F;
    const std::array families{graphicsQueueFamily_, presentQueueFamily_};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (const auto family : families) {
        if (!queueInfos.empty() && family == queueInfos.front().queueFamilyIndex) {
            continue;
        }
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }

    constexpr const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    info.pQueueCreateInfos = queueInfos.data();
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;
    check(vkCreateDevice(physicalDevice_, &info, nullptr, &device_), "Creating the Vulkan logical device");
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
}

void Context::waitIdle() const { check(vkDeviceWaitIdle(device_), "Waiting for the Vulkan device"); }

void Context::cleanup() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        // Destructors must still release resources if the driver reports device loss.
        static_cast<void>(vkDeviceWaitIdle(device_));
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (debugMessenger_ != VK_NULL_HANDLE) {
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger != nullptr) {
            destroyMessenger(instance_, debugMessenger_, nullptr);
        }
        debugMessenger_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

} // namespace emergences::core
