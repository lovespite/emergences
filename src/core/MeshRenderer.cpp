#include "core/MeshRenderer.h"

#include "core/Buffer.h"
#include "core/ParticleRenderer.h"
#include "core/Pipeline.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace emergences::core {

MeshRenderer::MeshRenderer(const Context& context, std::filesystem::path shaderDirectory,
                           VkPrimitiveTopology topology)
    : context_(context), shaderDirectory_(std::move(shaderDirectory)), topology_(topology) {
    if (topology_ != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && topology_ != VK_PRIMITIVE_TOPOLOGY_LINE_LIST) {
        throw std::invalid_argument("Mesh renderer supports triangle lists and line lists");
    }
}

MeshRenderer::~MeshRenderer() = default;

void MeshRenderer::configure(VkRenderPass renderPass, VkSampleCountFlagBits samples) {
    constexpr std::array bindings{
        VkVertexInputBindingDescription{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    constexpr std::array attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(MeshVertex, position)},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(MeshVertex, normal)},
        VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(MeshVertex, color)}};
    pipeline_ = std::make_unique<Pipeline>(context_, renderPass, PipelineOptions{
        .vertexShader = shaderDirectory_ / "mesh.vert.spv",
        .fragmentShader = shaderDirectory_ / "mesh.frag.spv",
        .topology = topology_,
        .bindings = bindings,
        .attributes = attributes,
        .pushConstantBytes = sizeof(SceneView),
        .depthTest = true,
        .samples = samples});
}

void MeshRenderer::draw(VkCommandBuffer commands, std::size_t frame,
                        std::span<const MeshVertex> vertices, const SceneView& view,
                        std::optional<std::uint64_t> revision) {
    auto& buffer = buffers_.at(frame);
    auto& uploadedRevision = uploadedRevisions_.at(frame);
    if (vertices.empty()) return;
    if (!pipeline_) throw std::logic_error("Configure the mesh renderer before drawing");
    if (commands == VK_NULL_HANDLE) throw std::invalid_argument("A command buffer is required");
    const std::size_t verticesPerPrimitive = topology_ == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ? 2 : 3;
    if (vertices.size() % verticesPerPrimitive != 0) {
        throw std::invalid_argument("Mesh vertices must form complete primitives");
    }
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Mesh vertex count exceeds the Vulkan draw limit");
    }
    const VkDeviceSize bytes = vertices.size_bytes();
    if (!buffer || buffer->size() < bytes) {
        const auto capacity = std::max<VkDeviceSize>(bytes + bytes / 2, 4096);
        buffer = std::make_unique<Buffer>(context_, capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        uploadedRevision.reset();
    }
    if (!revision || uploadedRevision != revision) {
        buffer->write(vertices.data(), bytes);
        uploadedRevision = revision;
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
    const auto handle = buffer->handle();
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commands, 0, 1, &handle, &offset);
    vkCmdPushConstants(commands, pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(view), &view);
    vkCmdDraw(commands, static_cast<std::uint32_t>(vertices.size()), 1, 0, 0);
}

} // namespace emergences::core
