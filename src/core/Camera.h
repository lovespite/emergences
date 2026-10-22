#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace emergences::core {

// Orbit camera with a right-handed world and a Vulkan [0, 1] depth projection.
// Drag deltas and viewportHeight must use the same units (usually window pixels).
class Camera final {
public:
    Camera();

    [[nodiscard]] glm::mat4 viewProjection(float aspect) const;
    [[nodiscard]] glm::vec3 right() const;
    [[nodiscard]] glm::vec3 up() const;
    [[nodiscard]] glm::vec3 position() const;

    void orbit(float deltaX, float deltaY);
    void pan(float deltaX, float deltaY, float viewportHeight);
    // Positive wheel deltas move toward the target.
    void zoom(float wheelDelta);
    // Fits the [-20, 20] world box in a square or landscape viewport.
    void reset();

private:
    glm::vec3 target_{0.0F};
    float yaw_ = 0.0F;
    float pitch_ = 0.0F;
    float distance_ = 1.0F;
};

} // namespace emergences::core
