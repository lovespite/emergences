#pragma once

#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <type_traits>

namespace emergences::core {
class Buffer;
class Context;
class Descriptor;
class Pipeline;

// Render-facing record: no dependency on a particular simulation backend.
// Matches the std430 Particle in shaders/particle.vert (and sim::Agent ABI).
struct alignas(16) Particle {
    glm::vec3 position{0.0F};
    float size = 1.0F;
    glm::vec3 velocity{0.0F};
    float reserved = 0.0F;
    glm::vec3 color{1.0F};
    std::uint32_t id = 0;
};
static_assert(sizeof(Particle) == 48 && alignof(Particle) == 16);
static_assert(std::is_trivially_copyable_v<Particle>);
static_assert(offsetof(Particle, velocity) == 16 && offsetof(Particle, color) == 32);
static_assert(offsetof(Particle, size) == 12 && offsetof(Particle, id) == 44);

struct alignas(16) SceneView {
    glm::mat4 viewProjection{1.0F};
    glm::vec4 right{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec4 up{0.0F, 1.0F, 0.0F, 0.0F};
    // x: size scale; y: 0 arrows / 1 dots / 2 ant silhouettes / 3 square cells.
    // z: nonzero enables sphere shading for dots; w: reserved.
    glm::vec4 options{1.0F, 0.0F, 0.0F, 0.0F};
};
static_assert(sizeof(SceneView) == 112);
static_assert(offsetof(SceneView, right) == 64 && offsetof(SceneView, up) == 80);
static_assert(offsetof(SceneView, options) == 96);

// Caller must wait for a frame slot before upload/draw and finish GPU work before
// destruction or configure(). The two slots match FrameRenderer::framesInFlight.
class ParticleRenderer final {
public:
    ParticleRenderer(const Context&, std::filesystem::path shaderDirectory);
    ~ParticleRenderer();
    void configure(VkRenderPass, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    void draw(VkCommandBuffer, std::size_t frame, std::span<const Particle>, const SceneView&);
    // The external storage buffer must outlive its in-flight draw. Call only after
    // waiting for this frame slot, as with draw(). It must contain Particle records.
    void drawBuffer(VkCommandBuffer, std::size_t frame, VkBuffer, VkDeviceSize bufferBytes,
                    std::uint32_t count, const SceneView&);
private:
    const Context& context_;
    std::filesystem::path shaderDirectory_;
    VkDeviceSize maxStorageRange_ = 0;
    std::unique_ptr<Descriptor> descriptor_;
    std::array<std::unique_ptr<Buffer>, 2> buffers_;
    std::unique_ptr<Pipeline> pipeline_;
};
} // namespace emergences::core
