#include "core/Descriptor.h"

#include "core/Buffer.h"
#include "core/Context.h"

#include <algorithm>
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

} // namespace

Descriptor::Descriptor(const Context& context,
                       std::span<const VkDescriptorSetLayoutBinding> bindings,
                       std::uint32_t setCount)
    : device_(context.device()) {
    if (setCount == 0 || bindings.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Descriptor set count or binding count is invalid");
    }

    try {
        std::vector<VkDescriptorPoolSize> poolSizes;
        bindings_.reserve(bindings.size());
        for (const auto& binding : bindings) {
            if (binding.descriptorCount == 0) {
                throw std::invalid_argument("Descriptor bindings must have a nonzero count");
            }
            if (std::ranges::any_of(bindings_, [&binding](const Binding& existing) {
                    return existing.number == binding.binding;
                })) {
                throw std::invalid_argument("Descriptor binding numbers must be unique");
            }
            bindings_.push_back({binding.binding, binding.descriptorType});

            const auto count = static_cast<std::uint64_t>(binding.descriptorCount) * setCount;
            auto poolSize = std::ranges::find_if(poolSizes, [&binding](const auto& value) {
                return value.type == binding.descriptorType;
            });
            const auto existingCount = poolSize == poolSizes.end() ? 0U : poolSize->descriptorCount;
            if (count > std::numeric_limits<std::uint32_t>::max() - existingCount) {
                throw std::overflow_error("Descriptor pool size exceeds Vulkan's supported count");
            }
            if (poolSize == poolSizes.end()) {
                poolSizes.push_back({binding.descriptorType, static_cast<std::uint32_t>(count)});
            } else {
                poolSize->descriptorCount += static_cast<std::uint32_t>(count);
            }
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
        uniformOffsetAlignment_ = properties.limits.minUniformBufferOffsetAlignment;
        storageOffsetAlignment_ = properties.limits.minStorageBufferOffsetAlignment;
        maxUniformRange_ = properties.limits.maxUniformBufferRange;
        maxStorageRange_ = properties.limits.maxStorageBufferRange;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout_),
              "Creating a descriptor set layout");

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = setCount;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool_),
              "Creating a descriptor pool");

        const std::vector<VkDescriptorSetLayout> layouts(setCount, layout_);
        sets_.resize(setCount, VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptorPool = pool_;
        allocation.descriptorSetCount = setCount;
        allocation.pSetLayouts = layouts.data();
        check(vkAllocateDescriptorSets(device_, &allocation, sets_.data()),
              "Allocating descriptor sets");
    } catch (...) {
        cleanup();
        throw;
    }
}

Descriptor::~Descriptor() { cleanup(); }

VkDescriptorSet Descriptor::set(std::uint32_t index) const { return sets_.at(index); }

void Descriptor::writeBuffer(std::uint32_t setIndex, std::uint32_t binding, VkDescriptorType type,
                             const Buffer& buffer, VkDeviceSize range, VkDeviceSize offset) {
    const VkDescriptorSet targetSet = set(setIndex);
    const auto targetBinding = std::ranges::find_if(bindings_, [binding](const Binding& value) {
        return value.number == binding;
    });
    if (targetBinding == bindings_.end() || targetBinding->type != type) {
        throw std::invalid_argument("Buffer descriptor does not match its layout binding");
    }
    const bool uniform = type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                         type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    const bool storage = type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                         type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    if (!uniform && !storage) {
        throw std::invalid_argument("writeBuffer requires a uniform or storage buffer descriptor");
    }
    if (offset >= buffer.size()) {
        throw std::out_of_range("Buffer descriptor offset exceeds the buffer size");
    }
    const VkDeviceSize resolvedRange = range == VK_WHOLE_SIZE ? buffer.size() - offset : range;
    if (resolvedRange == 0 || resolvedRange > buffer.size() - offset) {
        throw std::out_of_range("Buffer descriptor range exceeds the buffer size");
    }
    const VkDeviceSize alignment = uniform ? uniformOffsetAlignment_ : storageOffsetAlignment_;
    const VkDeviceSize maxRange = uniform ? maxUniformRange_ : maxStorageRange_;
    if (offset % alignment != 0 || resolvedRange > maxRange) {
        throw std::invalid_argument("Buffer descriptor violates device alignment or range limits");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer.handle();
    bufferInfo.offset = offset;
    bufferInfo.range = resolvedRange;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = targetSet;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void Descriptor::cleanup() noexcept {
    if (pool_ != VK_NULL_HANDLE) {
        // Destroying the pool also frees all allocated sets.
        vkDestroyDescriptorPool(device_, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    sets_.clear();
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}

} // namespace emergences::core
