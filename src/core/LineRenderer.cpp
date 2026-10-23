#include "core/LineRenderer.h"

#include "core/Buffer.h"
#include "core/ParticleRenderer.h"
#include "core/Pipeline.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace emergences::core {

namespace {

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> color;
};
static_assert(sizeof(Vertex) == 24 && offsetof(Vertex, color) == 12);

constexpr std::uint32_t gridVertexCount = 44;
constexpr std::uint32_t boundsVertexCount = 24;
constexpr std::array<float, 3> gridColor{0.18F, 0.25F, 0.32F};
constexpr std::array<float, 3> boundsColor{0.40F, 0.63F, 0.78F};

} // namespace

LineRenderer::LineRenderer(const Context& context, std::filesystem::path shaderDirectory,
                           bool planar, float halfExtent)
    : context_(context), shaderDirectory_(std::move(shaderDirectory)), boundsVertexCount_(planar ? 8U : 24U) {
    if (!std::isfinite(halfExtent) || halfExtent <= 0.0F || halfExtent > 100000.0F) {
        throw std::invalid_argument("Line renderer extent must be finite and within (0, 100000]");
    }
    std::vector<Vertex> vertices;
    vertices.reserve(gridVertexCount + boundsVertexCount);
    const auto line = [&vertices](std::array<float, 3> from, std::array<float, 3> to,
                                 std::array<float, 3> color) {
        vertices.push_back({from, color});
        vertices.push_back({to, color});
    };

    // Eleven lines in each horizontal direction, including the floor perimeter.
    const float floor = planar ? 0.0F : -halfExtent;
    for (int index = 0; index <= 10; ++index) {
        const float coordinate = -halfExtent + (2.0F * halfExtent / 10.0F) * static_cast<float>(index);
        line({coordinate, floor, -halfExtent},
             {coordinate, floor, halfExtent}, gridColor);
        line({-halfExtent, floor, coordinate},
             {halfExtent, floor, coordinate}, gridColor);
    }

    // Four edges parallel to each axis form the twelve edges of the world cube.
    if (planar) {
        for (const float edge : {-halfExtent, halfExtent}) {
            line({-halfExtent, 0.0F, edge}, {halfExtent, 0.0F, edge}, boundsColor);
            line({edge, 0.0F, -halfExtent}, {edge, 0.0F, halfExtent}, boundsColor);
        }
    } else {
        for (const float first : {-halfExtent, halfExtent}) {
            for (const float second : {-halfExtent, halfExtent}) {
                line({-halfExtent, first, second}, {halfExtent, first, second}, boundsColor);
                line({first, -halfExtent, second}, {first, halfExtent, second}, boundsColor);
                line({first, second, -halfExtent}, {first, second, halfExtent}, boundsColor);
            }
        }
    }

    const VkDeviceSize bytes = vertices.size() * sizeof(Vertex);
    vertices_ = std::make_unique<Buffer>(context_, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    vertices_->write(vertices.data(), bytes);
}

LineRenderer::~LineRenderer() = default;

void LineRenderer::configure(VkRenderPass renderPass, VkSampleCountFlagBits samples) {
    constexpr std::array bindings{
        VkVertexInputBindingDescription{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    constexpr std::array attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}};
    pipeline_ = std::make_unique<Pipeline>(context_, renderPass, PipelineOptions{
        .vertexShader = shaderDirectory_ / "grid.vert.spv",
        .fragmentShader = shaderDirectory_ / "grid.frag.spv",
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .bindings = bindings,
        .attributes = attributes,
        .pushConstantBytes = sizeof(SceneView),
        .depthTest = true,
        .samples = samples});
}

void LineRenderer::draw(VkCommandBuffer commands, const SceneView& view,
                        bool showGrid, bool showBounds) {
    if (!showGrid && !showBounds) {
        return;
    }
    if (!pipeline_) {
        throw std::logic_error("Configure the line renderer before drawing");
    }
    if (commands == VK_NULL_HANDLE) {
        throw std::invalid_argument("The line renderer requires a valid command buffer");
    }

    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
    const auto buffer = vertices_->handle();
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commands, 0, 1, &buffer, &offset);
    vkCmdPushConstants(commands, pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT,
                        0, sizeof(view), &view);
    if (showGrid) {
        vkCmdDraw(commands, gridVertexCount, 1, 0, 0);
    }
    if (showBounds) {
        vkCmdDraw(commands, boundsVertexCount_, 1, gridVertexCount, 0);
    }
}

} // namespace emergences::core
