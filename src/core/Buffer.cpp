#include "core/Buffer.h"

#include "core/Context.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
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

std::uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& available,
                             std::uint32_t allowedTypes, VkMemoryPropertyFlags required) {
    for (std::uint32_t i = 0; i < available.memoryTypeCount; ++i) {
        if ((allowedTypes & (1U << i)) != 0 &&
            (available.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error("No Vulkan memory type satisfies the buffer's requested properties");
}

} // namespace

Buffer::Buffer(const Context& context, VkDeviceSize size, VkBufferUsageFlags usage,
               VkMemoryPropertyFlags properties)
    : device_(context.device()), size_(size) {
    if (size == 0 || usage == 0) {
        throw std::invalid_argument("A buffer requires a nonzero size and usage flags");
    }

    try {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &info, nullptr, &buffer_), "Creating a buffer");

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer_, &requirements);
        allocationSize_ = requirements.size;

        VkPhysicalDeviceMemoryProperties available{};
        vkGetPhysicalDeviceMemoryProperties(context.physicalDevice(), &available);
        const auto memoryType = findMemoryType(available, requirements.memoryTypeBits, properties);
        memoryProperties_ = available.memoryTypes[memoryType].propertyFlags;

        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = allocationSize_;
        allocation.memoryTypeIndex = memoryType;
        check(vkAllocateMemory(device_, &allocation, nullptr, &memory_), "Allocating buffer memory");
        check(vkBindBufferMemory(device_, buffer_, memory_, 0), "Binding buffer memory");

        if ((memoryProperties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
            if (size_ > std::numeric_limits<std::size_t>::max()) {
                throw std::runtime_error("The host-visible buffer exceeds the host addressable size");
            }
            VkPhysicalDeviceProperties deviceProperties{};
            vkGetPhysicalDeviceProperties(context.physicalDevice(), &deviceProperties);
            nonCoherentAtomSize_ = deviceProperties.limits.nonCoherentAtomSize;
            check(vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, &mapped_),
                  "Mapping buffer memory");
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

Buffer::~Buffer() { cleanup(); }

void Buffer::write(const void* data, VkDeviceSize bytes, VkDeviceSize offset) {
    if (offset > size_ || bytes > size_ - offset) {
        throw std::out_of_range("Buffer write exceeds the buffer size");
    }
    if (bytes == 0) {
        return;
    }
    if (mapped_ == nullptr) {
        throw std::logic_error("Cannot write directly to a buffer without host-visible memory");
    }
    if (data == nullptr) {
        throw std::invalid_argument("Buffer write requires a nonnull source");
    }

    std::memcpy(static_cast<std::byte*>(mapped_) + static_cast<std::size_t>(offset), data,
                static_cast<std::size_t>(bytes));

    if ((memoryProperties_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        const VkDeviceSize start = offset - offset % nonCoherentAtomSize_;
        const VkDeviceSize end = offset + bytes;
        const VkDeviceSize remainder = end % nonCoherentAtomSize_;
        const VkDeviceSize padding = remainder == 0 ? 0 : nonCoherentAtomSize_ - remainder;
        // The final range may end at the allocation boundary, even when its size
        // is not an atom multiple. Avoid rounding beyond the mapped allocation.
        const VkDeviceSize alignedEnd = padding > allocationSize_ - end
                                            ? allocationSize_ : end + padding;
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = memory_;
        range.offset = start;
        range.size = alignedEnd - start;
        check(vkFlushMappedMemoryRanges(device_, 1, &range), "Flushing buffer memory");
    }
}

void Buffer::cleanup() noexcept {
    if (mapped_ != nullptr) {
        vkUnmapMemory(device_, memory_);
        mapped_ = nullptr;
    }
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

} // namespace emergences::core
