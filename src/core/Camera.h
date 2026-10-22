#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <optional>

namespace emergences::core {

// Orbit camera with a right-handed world and a Vulkan [0, 1] depth projection.
// Drag deltas and viewportHeight must use the same units (usually window pixels).
class Camera final {
public:
    struct Ray {
        glm::vec3 origin;
        glm::vec3 direction;
    };

    Camera();

    [[nodiscard]] glm::mat4 viewProjection(float aspect) const;
    [[nodiscard]] glm::vec3 right() const;
    [[nodiscard]] glm::vec3 up() const;
    [[nodiscard]] glm::vec3 position() const;

    // Normalized viewport coordinates are [0, 1], with (0, 0) at the top left.
    // Rays start at the eye with a unit direction. Invalid inputs return nullopt.
    [[nodiscard]] std::optional<Ray> screenRay(
        float normalizedX, float normalizedY, float aspect) const;
    // A nonzero planeNormal need not be normalized. Parallel and behind-eye hits
    // return nullopt, as do invalid inputs or unrepresentable world positions.
    [[nodiscard]] std::optional<glm::vec3> screenToPlane(
        float normalizedX, float normalizedY, float aspect,
        const glm::vec3& planePoint, const glm::vec3& planeNormal) const;

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
