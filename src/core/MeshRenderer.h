#pragma once

#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>

namespace emergences::core {

class Buffer;
class Context;
class Pipeline;
struct SceneView;

struct MeshVertex {
    glm::vec3 position{0.0F};
    glm::vec3 normal{0.0F, 1.0F, 0.0F};
    glm::vec3 color{1.0F};
};
static_assert(sizeof(MeshVertex) == 36 && offsetof(MeshVertex, normal) == 12 &&
              offsetof(MeshVertex, color) == 24);
static_assert(std::is_trivially_copyable_v<MeshVertex>);

// Opaque, lit triangle meshes or line lists. Wait for the selected FrameRenderer frame slot
// before draw(); finish GPU uses before configure() or destruction. Each frame
// owns its upload buffer, so growing one slot never invalidates the other.
class MeshRenderer final {
public:
    MeshRenderer(const Context& context, std::filesystem::path shaderDirectory,
                 VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    ~MeshRenderer();

    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;
    MeshRenderer(MeshRenderer&&) = delete;
    MeshRenderer& operator=(MeshRenderer&&) = delete;

    void configure(VkRenderPass renderPass,
                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    // Submit all geometry in one call per frame slot. An optional revision
    // reuses that slot's upload while geometry is unchanged; callers must change
    // it whenever vertex data changes. Omit it for dynamic geometry.
    void draw(VkCommandBuffer commands, std::size_t frame,
              std::span<const MeshVertex> vertices, const SceneView& view,
              std::optional<std::uint64_t> revision = std::nullopt);

private:
    const Context& context_;
    std::filesystem::path shaderDirectory_;
    VkPrimitiveTopology topology_;
    std::array<std::unique_ptr<Buffer>, 2> buffers_;
    std::array<std::optional<std::uint64_t>, 2> uploadedRevisions_;
    std::unique_ptr<Pipeline> pipeline_;
};

} // namespace emergences::core
