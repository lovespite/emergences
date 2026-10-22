#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/vec3.hpp>

namespace emergences::sim {

// Shared CPU / GPU particle record. The matching GLSL std430 declaration is:
// vec3 position; float size; vec3 velocity; float reserved;
// vec3 color; uint id;
// Keep the shader declaration in sync when changing this layout.
struct alignas(16) Agent {
    glm::vec3 position{0.0F};
    float size{1.0F};
    glm::vec3 velocity{0.0F};
    float reserved{0.0F};
    glm::vec3 color{1.0F};
    std::uint32_t id{0};
};

// Aligned GLM vec3 types change this ABI; use GLM's default packed vectors.
static_assert(sizeof(float) == 4);
static_assert(sizeof(glm::vec3) == 12);
static_assert(std::is_standard_layout_v<Agent>);
static_assert(std::is_trivially_copyable_v<Agent>);
static_assert(alignof(Agent) == 16);
static_assert(sizeof(Agent) == 48);
static_assert(offsetof(Agent, position) == 0);
static_assert(offsetof(Agent, size) == 12);
static_assert(offsetof(Agent, velocity) == 16);
static_assert(offsetof(Agent, reserved) == 28);
static_assert(offsetof(Agent, color) == 32);
static_assert(offsetof(Agent, id) == 44);

} // namespace emergences::sim
