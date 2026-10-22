#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

namespace emergences::core {

class Buffer;
class Context;

// Owns one layout, its pool, and identically laid-out sets. Context must outlive
// this object. Finish GPU uses before updates/destruction; bound buffers must
// remain alive while their descriptors are in use.
class Descriptor final {
public:
    Descriptor(const Context& context, std::span<const VkDescriptorSetLayoutBinding> bindings,
               std::uint32_t setCount);
    ~Descriptor();

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&&) = delete;
    Descriptor& operator=(Descriptor&&) = delete;

    [[nodiscard]] VkDescriptorSetLayout layout() const noexcept { return layout_; }
    [[nodiscard]] VkDescriptorSet set(std::uint32_t index) const;

    // Updates array element zero of a uniform or storage buffer binding.
    // VK_WHOLE_SIZE selects the buffer's remaining bytes after offset.
    void writeBuffer(std::uint32_t setIndex, std::uint32_t binding, VkDescriptorType type,
                     const Buffer& buffer, VkDeviceSize range, VkDeviceSize offset = 0);

private:
    struct Binding {
        std::uint32_t number;
        VkDescriptorType type;
    };

    void cleanup() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> sets_;
    std::vector<Binding> bindings_;
    VkDeviceSize uniformOffsetAlignment_ = 1;
    VkDeviceSize storageOffsetAlignment_ = 1;
    VkDeviceSize maxUniformRange_ = 0;
    VkDeviceSize maxStorageRange_ = 0;
};

} // namespace emergences::core
