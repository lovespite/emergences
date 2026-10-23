#include "core/ParticleRenderer.h"
#include "core/Buffer.h"
#include "core/Context.h"
#include "core/Descriptor.h"
#include "core/Pipeline.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace emergences::core {
ParticleRenderer::ParticleRenderer(const Context& context, std::filesystem::path shaderDirectory)
    : context_(context), shaderDirectory_(std::move(shaderDirectory)) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_.physicalDevice(), &properties);
    maxStorageRange_ = properties.limits.maxStorageBufferRange;
    const VkDescriptorSetLayoutBinding binding{
        0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    descriptor_ = std::make_unique<Descriptor>(context_, std::span{&binding, 1}, 2);
}
ParticleRenderer::~ParticleRenderer() = default;

void ParticleRenderer::configure(VkRenderPass pass, VkSampleCountFlagBits samples) {
    // Alpha-to-coverage also smooths the shader-defined circle boundary.
    const VkBool32 smoothDots = samples != VK_SAMPLE_COUNT_1_BIT ? VK_TRUE : VK_FALSE;
    const VkSpecializationMapEntry entry{0, 0, sizeof(smoothDots)};
    const VkSpecializationInfo specialization{1, &entry, sizeof(smoothDots), &smoothDots};
    pipeline_ = std::make_unique<Pipeline>(context_, pass, PipelineOptions{
        .vertexShader = shaderDirectory_ / "particle.vert.spv",
        .fragmentShader = shaderDirectory_ / "particle.frag.spv",
        .descriptorLayout = descriptor_->layout(),
        .pushConstantBytes = sizeof(SceneView),
        .depthTest = true,
        .samples = samples,
        .alphaToCoverage = smoothDots == VK_TRUE,
        .fragmentSpecialization = &specialization});
}

void ParticleRenderer::draw(VkCommandBuffer commands, std::size_t frame,
                            std::span<const Particle> particles, const SceneView& view) {
    auto& buffer = buffers_.at(frame);
    if (particles.empty()) return;
    if (!pipeline_) throw std::logic_error("Configure the particle renderer before drawing");
    if (commands == VK_NULL_HANDLE) throw std::invalid_argument("A command buffer is required");
    if (particles.size() > std::numeric_limits<std::uint32_t>::max() ||
        particles.size_bytes() > maxStorageRange_) {
        throw std::length_error("Particle array exceeds the device storage buffer limit");
    }
    const VkDeviceSize bytes = particles.size_bytes();
    if (!buffer || buffer->size() < bytes) {
        // Only this slot's fence has been waited. The other slot remains untouched.
        const auto capacity = std::min<VkDeviceSize>(maxStorageRange_,
                                                   std::max<VkDeviceSize>(bytes * 2, 4096));
        buffer = std::make_unique<Buffer>(context_, capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    buffer->write(particles.data(), bytes);
    drawBuffer(commands, frame, buffer->handle(), buffer->size(),
               static_cast<std::uint32_t>(particles.size()), view);
}

void ParticleRenderer::drawBuffer(VkCommandBuffer commands, std::size_t frame,
                                  VkBuffer buffer, VkDeviceSize bufferBytes,
                                  std::uint32_t count, const SceneView& view) {
    if (frame >= buffers_.size()) throw std::out_of_range("Particle frame slot is invalid");
    if (count == 0) return;
    if (!pipeline_) throw std::logic_error("Configure the particle renderer before drawing");
    if (commands == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE) {
        throw std::invalid_argument("Drawing particles requires command and storage buffers");
    }
    const VkDeviceSize required = static_cast<VkDeviceSize>(count) * sizeof(Particle);
    if (required > bufferBytes || bufferBytes > maxStorageRange_) {
        throw std::length_error("Particle storage buffer range is invalid");
    }
    const auto set = descriptor_->set(static_cast<std::uint32_t>(frame));
    const VkDescriptorBufferInfo bufferInfo{buffer, 0, bufferBytes};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;
    // Update even for an existing CPU allocation: this slot may have used a GPU
    // simulation buffer on its previous frame.
    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                            0, 1, &set, 0, nullptr);
    vkCmdPushConstants(commands, pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT,
                        0, sizeof(view), &view);
    vkCmdDraw(commands, 6, count, 0, 0);
}
} // namespace emergences::core
