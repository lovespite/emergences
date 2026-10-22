#pragma once

#include <vulkan/vulkan.h>

namespace emergences::core {

class Context;

// Context must outlive the buffer. Synchronize GPU uses before writing or destroying it.
// Each buffer owns its allocation; host-visible allocations stay mapped until destruction.
class Buffer final {
public:
    Buffer(const Context& context, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }
    [[nodiscard]] VkDeviceSize size() const noexcept { return size_; }

    // Writes to host-visible memory and flushes noncoherent allocations when necessary.
    void write(const void* data, VkDeviceSize bytes, VkDeviceSize offset = 0);

private:
    void cleanup() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    VkDeviceSize allocationSize_ = 0;
    VkDeviceSize nonCoherentAtomSize_ = 1;
    VkMemoryPropertyFlags memoryProperties_ = 0;
    void* mapped_ = nullptr;
};

} // namespace emergences::core
