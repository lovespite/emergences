#include "core/ComputeDevice.h"

#include "core/Buffer.h"
#include "core/Context.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace emergences::core {
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

std::vector<std::uint32_t> readSpirv(std::string_view shaderPath) {
    const std::filesystem::path path{shaderPath};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open compute shader: " + path.string());
    const std::streamoff length = file.tellg();
    if (length < 5 * static_cast<std::streamoff>(sizeof(std::uint32_t)) ||
        length % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
        throw std::runtime_error("Invalid compute SPIR-V size: " + path.string());
    }
    const auto bytes = static_cast<std::uintmax_t>(length);
    if (bytes > std::numeric_limits<std::size_t>::max() ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::length_error("Compute SPIR-V is too large");
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes) / sizeof(std::uint32_t));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(bytes));
    if (!file || words.front() != 0x07230203U) {
        throw std::runtime_error("Invalid compute SPIR-V contents: " + path.string());
    }
    return words;
}

// Includes earlier graphics reads as well as transfer/compute writes on the
// shared queue. The ending barrier makes compute output visible to later draws.
void memoryBarrier(VkCommandBuffer commands) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier,
                         0, nullptr, 0, nullptr);
}

struct ComputePipeline {
    explicit ComputePipeline(VkDevice targetDevice) : device(targetDevice) {}
    ~ComputePipeline() {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout, nullptr);
        if (descriptorLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        }
    }
    VkDevice device;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    std::uint32_t storageBindings = 0;
    std::uint32_t pushConstantBytes = 0;
};

struct DescriptorPool {
    explicit DescriptorPool(VkDevice targetDevice) : device(targetDevice) {}
    ~DescriptorPool() {
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
    }
    VkDevice device;
    VkDescriptorPool pool = VK_NULL_HANDLE;
};

} // namespace

struct VulkanComputeDevice::Impl {
    struct Storage {
        std::unique_ptr<Buffer> allocation;
        std::size_t logicalBytes = 0;
    };

    explicit Impl(const Context& target) : context(target), device(target.device()) {
        vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
        try {
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            poolInfo.queueFamilyIndex = context.graphicsQueueFamily();
            check(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
                  "Creating the compute command pool");
            VkCommandBufferAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocation.commandPool = commandPool;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocation.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device, &allocation, &commands),
                  "Allocating compute commands");
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "Creating the compute fence");
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~Impl() { cleanup(); }

    void cleanup() noexcept {
        // A buffer may still be referenced by the particle renderer's last frame.
        static_cast<void>(vkDeviceWaitIdle(device));
        pipelines.clear();
        buffers.clear();
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
        fence = VK_NULL_HANDLE;
        commandPool = VK_NULL_HANDLE;
    }

    template<class Record>
    void submit(Record&& record) {
        check(vkResetCommandPool(device, commandPool, 0), "Resetting compute commands");
        check(vkResetFences(device, 1, &fence), "Resetting the compute fence");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(commands, &begin), "Beginning compute commands");
        record(commands);
        check(vkEndCommandBuffer(commands), "Ending compute commands");
        VkSubmitInfo submission{};
        submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submission.commandBufferCount = 1;
        submission.pCommandBuffers = &commands;
        check(vkQueueSubmit(context.graphicsQueue(), 1, &submission, fence),
              "Submitting compute commands");
        const auto result = vkWaitForFences(device, 1, &fence, VK_TRUE,
                                            std::numeric_limits<std::uint64_t>::max());
        if (result != VK_SUCCESS) {
            // Keep staging buffers and descriptor pools alive until submitted
            // work ends, including when the original fence wait reports failure.
            static_cast<void>(vkQueueWaitIdle(context.graphicsQueue()));
            check(result, "Waiting for compute commands");
        }
    }

    Storage& storage(sim::BufferHandle handle) {
        const auto it = buffers.find(handle);
        if (it == buffers.end()) throw std::invalid_argument("Unknown compute buffer handle");
        return it->second;
    }
    ComputePipeline& pipeline(sim::PipelineHandle handle) {
        const auto it = pipelines.find(handle);
        if (it == pipelines.end()) throw std::invalid_argument("Unknown compute pipeline handle");
        return *it->second;
    }

    const Context& context;
    VkDevice device;
    VkPhysicalDeviceProperties properties{};
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    sim::BufferHandle nextBuffer = 1;
    sim::PipelineHandle nextPipeline = 1;
    std::unordered_map<sim::BufferHandle, Storage> buffers;
    std::unordered_map<sim::PipelineHandle, std::unique_ptr<ComputePipeline>> pipelines;
};

VulkanComputeDevice::VulkanComputeDevice(const Context& context)
    : impl_(std::make_unique<Impl>(context)) {}
VulkanComputeDevice::~VulkanComputeDevice() = default;

sim::BufferHandle VulkanComputeDevice::createBuffer(std::size_t bytes) {
    const auto allocated = std::max<std::size_t>(bytes, sizeof(std::uint32_t));
    if (allocated > impl_->properties.limits.maxStorageBufferRange) {
        throw std::length_error("Compute buffer exceeds the device storage range limit");
    }
    if (impl_->nextBuffer == 0) throw std::overflow_error("Compute buffer handles exhausted");
    auto allocation = std::make_unique<Buffer>(impl_->context, allocated,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const auto handle = impl_->nextBuffer++;
    impl_->buffers.emplace(handle, Impl::Storage{std::move(allocation), bytes});
    return handle;
}

void VulkanComputeDevice::destroyBuffer(sim::BufferHandle handle) noexcept {
    const auto it = impl_->buffers.find(handle);
    if (it == impl_->buffers.end()) return;
    static_cast<void>(vkDeviceWaitIdle(impl_->device));
    impl_->buffers.erase(it);
}

void VulkanComputeDevice::upload(sim::BufferHandle handle, std::span<const std::byte> bytes) {
    const auto& storage = impl_->storage(handle);
    if (bytes.size() > storage.logicalBytes) throw std::out_of_range("Compute upload exceeds buffer size");
    if (bytes.empty()) return;
    Buffer staging{impl_->context, bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT};
    staging.write(bytes.data(), bytes.size());
    impl_->submit([&](VkCommandBuffer commands) {
        memoryBarrier(commands);
        const VkBufferCopy copy{0, 0, bytes.size()};
        vkCmdCopyBuffer(commands, staging.handle(), storage.allocation->handle(), 1, &copy);
        memoryBarrier(commands);
    });
}

void VulkanComputeDevice::download(sim::BufferHandle handle, std::span<std::byte> bytes) {
    const auto& storage = impl_->storage(handle);
    if (bytes.size() > storage.logicalBytes) throw std::out_of_range("Compute download exceeds buffer size");
    if (bytes.empty()) return;
    Buffer staging{impl_->context, bytes.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT};
    impl_->submit([&](VkCommandBuffer commands) {
        memoryBarrier(commands);
        const VkBufferCopy copy{0, 0, bytes.size()};
        vkCmdCopyBuffer(commands, storage.allocation->handle(), staging.handle(), 1, &copy);
        memoryBarrier(commands);
        VkMemoryBarrier hostBarrier{};
        hostBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &hostBarrier, 0, nullptr, 0, nullptr);
    });
    staging.read(bytes.data(), bytes.size());
}

sim::PipelineHandle VulkanComputeDevice::createPipeline(std::string_view shaderPath,
                                                        std::uint32_t storageBindings,
                                                        std::uint32_t pushConstantBytes) {
    const auto& limits = impl_->properties.limits;
    if (storageBindings > limits.maxPerStageDescriptorStorageBuffers ||
        storageBindings > limits.maxDescriptorSetStorageBuffers ||
        storageBindings > limits.maxPerStageResources ||
        pushConstantBytes > limits.maxPushConstantsSize || pushConstantBytes % 4 != 0) {
        throw std::invalid_argument("Compute pipeline layout exceeds device limits");
    }
    if (impl_->nextPipeline == 0) throw std::overflow_error("Compute pipeline handles exhausted");
    const auto words = readSpirv(shaderPath);
    auto pipeline = std::make_unique<ComputePipeline>(impl_->device);
    pipeline->storageBindings = storageBindings;
    pipeline->pushConstantBytes = pushConstantBytes;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(storageBindings);
    for (std::uint32_t i = 0; i < storageBindings; ++i) {
        bindings.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                            VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    }
    VkDescriptorSetLayoutCreateInfo descriptorInfo{};
    descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorInfo.bindingCount = storageBindings;
    descriptorInfo.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(impl_->device, &descriptorInfo, nullptr,
                                     &pipeline->descriptorLayout), "Creating compute descriptor layout");
    const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantBytes};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pipeline->descriptorLayout;
    layoutInfo.pushConstantRangeCount = pushConstantBytes == 0 ? 0U : 1U;
    layoutInfo.pPushConstantRanges = pushConstantBytes == 0 ? nullptr : &push;
    check(vkCreatePipelineLayout(impl_->device, &layoutInfo, nullptr, &pipeline->layout),
          "Creating compute pipeline layout");
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = words.size() * sizeof(std::uint32_t);
    moduleInfo.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(impl_->device, &moduleInfo, nullptr, &module),
          "Creating compute shader module");
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = "main";
    info.layout = pipeline->layout;
    info.basePipelineIndex = -1;
    const auto result = vkCreateComputePipelines(impl_->device, VK_NULL_HANDLE, 1, &info,
                                                 nullptr, &pipeline->pipeline);
    vkDestroyShaderModule(impl_->device, module, nullptr);
    check(result, "Creating compute pipeline");
    const auto handle = impl_->nextPipeline++;
    impl_->pipelines.emplace(handle, std::move(pipeline));
    return handle;
}

void VulkanComputeDevice::destroyPipeline(sim::PipelineHandle handle) noexcept {
    const auto it = impl_->pipelines.find(handle);
    if (it == impl_->pipelines.end()) return;
    static_cast<void>(vkDeviceWaitIdle(impl_->device));
    impl_->pipelines.erase(it);
}

void VulkanComputeDevice::execute(std::span<const sim::ComputeDispatch> dispatches) {
    if (dispatches.empty()) return;
    if (dispatches.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Too many compute dispatches");
    }
    std::uint64_t descriptorCount = 0;
    std::vector<VkDescriptorSetLayout> layouts;
    layouts.reserve(dispatches.size());
    for (const auto& dispatch : dispatches) {
        const auto& pipeline = impl_->pipeline(dispatch.pipeline);
        if (dispatch.buffers.size() != pipeline.storageBindings ||
            dispatch.constants.size() != pipeline.pushConstantBytes ||
            dispatch.groupsX == 0 ||
            dispatch.groupsX > impl_->properties.limits.maxComputeWorkGroupCount[0]) {
            throw std::invalid_argument("Compute dispatch does not match its pipeline or device limits");
        }
        for (const auto handle : dispatch.buffers) static_cast<void>(impl_->storage(handle));
        descriptorCount += pipeline.storageBindings;
        if (descriptorCount > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("Compute batch requires too many descriptors");
        }
        layouts.push_back(pipeline.descriptorLayout);
    }
    DescriptorPool descriptors{impl_->device};
    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                        static_cast<std::uint32_t>(descriptorCount)};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = static_cast<std::uint32_t>(dispatches.size());
    poolInfo.poolSizeCount = descriptorCount == 0 ? 0U : 1U;
    poolInfo.pPoolSizes = descriptorCount == 0 ? nullptr : &poolSize;
    check(vkCreateDescriptorPool(impl_->device, &poolInfo, nullptr, &descriptors.pool),
          "Creating compute batch descriptor pool");
    std::vector<VkDescriptorSet> sets(dispatches.size());
    VkDescriptorSetAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool = descriptors.pool;
    allocation.descriptorSetCount = static_cast<std::uint32_t>(sets.size());
    allocation.pSetLayouts = layouts.data();
    check(vkAllocateDescriptorSets(impl_->device, &allocation, sets.data()),
          "Allocating compute batch descriptor sets");
    for (std::size_t i = 0; i < dispatches.size(); ++i) {
        const auto& dispatch = dispatches[i];
        std::vector<VkDescriptorBufferInfo> infos;
        infos.reserve(dispatch.buffers.size());
        for (const auto handle : dispatch.buffers) {
            const auto& buffer = *impl_->storage(handle).allocation;
            infos.push_back({buffer.handle(), 0, buffer.size()});
        }
        std::vector<VkWriteDescriptorSet> writes(infos.size());
        for (std::size_t binding = 0; binding < infos.size(); ++binding) {
            auto& write = writes[binding];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = sets[i];
            write.dstBinding = static_cast<std::uint32_t>(binding);
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &infos[binding];
        }
        vkUpdateDescriptorSets(impl_->device, static_cast<std::uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
    impl_->submit([&](VkCommandBuffer commands) {
        memoryBarrier(commands);
        for (std::size_t i = 0; i < dispatches.size(); ++i) {
            const auto& dispatch = dispatches[i];
            const auto& pipeline = impl_->pipeline(dispatch.pipeline);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout,
                                    0, 1, &sets[i], 0, nullptr);
            if (!dispatch.constants.empty()) {
                vkCmdPushConstants(commands, pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                    0, pipeline.pushConstantBytes, dispatch.constants.data());
            }
            vkCmdDispatch(commands, dispatch.groupsX, 1, 1);
            memoryBarrier(commands);
        }
    });
}

void VulkanComputeDevice::waitIdle() { impl_->context.waitIdle(); }
VkBuffer VulkanComputeDevice::buffer(sim::BufferHandle handle) const {
    return impl_->storage(handle).allocation->handle();
}
VkDeviceSize VulkanComputeDevice::bufferSize(sim::BufferHandle handle) const {
    return impl_->storage(handle).allocation->size();
}

} // namespace emergences::core
