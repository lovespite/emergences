#include "core/Camera.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace emergences::core {
namespace {

constexpr float fieldOfView = std::numbers::pi_v<float> / 4.0F;
constexpr float minimumDistance = 3.0F;
constexpr float maximumDistance = 2000.0F;
constexpr double maximumPitch = std::numbers::pi / 2.0 - 0.01;
constexpr double orbitRadiansPerPixel = 0.005;
constexpr double maximumTargetCoordinate = 100000.0;

bool finiteDeltas(float x, float y) {
    return std::isfinite(x) && std::isfinite(y);
}

} // namespace

Camera::Camera() { reset(); }

glm::vec3 Camera::right() const {
    return {std::cos(yaw_), 0.0F, -std::sin(yaw_)};
}

glm::vec3 Camera::up() const {
    return {-std::sin(pitch_) * std::sin(yaw_), std::cos(pitch_),
            -std::sin(pitch_) * std::cos(yaw_)};
}

glm::vec3 Camera::position() const {
    return target_ + distance_ * glm::vec3{std::cos(pitch_) * std::sin(yaw_),
                                         std::sin(pitch_),
                                         std::cos(pitch_) * std::cos(yaw_)};
}

glm::mat4 Camera::viewProjection(float aspect) const {
    if (!std::isfinite(aspect) || aspect <= 0.0F) {
        aspect = 1.0F;
    }
    // Extremely narrow/tall transient resize values must not overflow projection.
    aspect = std::clamp(aspect, 0.01F, 100.0F);
    auto projection = glm::perspectiveRH_ZO(fieldOfView, aspect, 0.1F, 5000.0F);
    projection[1][1] *= -1.0F;
    return projection * glm::lookAtRH(position(), target_, up());
}

void Camera::orbit(float deltaX, float deltaY) {
    if (!finiteDeltas(deltaX, deltaY)) {
        return;
    }
    // Use doubles before reducing huge deltas so finite input cannot overflow.
    yaw_ = static_cast<float>(std::remainder(
        static_cast<double>(yaw_) - static_cast<double>(deltaX) * orbitRadiansPerPixel,
        2.0 * std::numbers::pi));
    pitch_ = static_cast<float>(std::clamp(
        static_cast<double>(pitch_) + static_cast<double>(deltaY) * orbitRadiansPerPixel,
        -maximumPitch, maximumPitch));
}

void Camera::pan(float deltaX, float deltaY, float viewportHeight) {
    if (!finiteDeltas(deltaX, deltaY) || !std::isfinite(viewportHeight) || viewportHeight <= 0.0F) {
        return;
    }
    const double unitsPerPixel =
        2.0 * distance_ * std::tan(static_cast<double>(fieldOfView) / 2.0) / viewportHeight;
    const glm::dvec3 offset = unitsPerPixel *
        (-static_cast<double>(deltaX) * glm::dvec3(right()) +
         static_cast<double>(deltaY) * glm::dvec3(up()));
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        target_[axis] = static_cast<float>(std::clamp(
            static_cast<double>(target_[axis]) + offset[axis],
            -maximumTargetCoordinate, maximumTargetCoordinate));
    }
}

void Camera::zoom(float wheelDelta) {
    if (!std::isfinite(wheelDelta)) {
        return;
    }
    // Bounding the exponent avoids overflow even for synthetic input events.
    const double exponent = std::clamp(-static_cast<double>(wheelDelta) * 0.12, -20.0, 20.0);
    distance_ = static_cast<float>(std::clamp(
        static_cast<double>(distance_) * std::exp(exponent),
        static_cast<double>(minimumDistance), static_cast<double>(maximumDistance)));
}

void Camera::reset() {
    target_ = glm::vec3(0.0F);
    yaw_ = std::numbers::pi_v<float> / 4.0F;
    pitch_ = std::numbers::pi_v<float> / 6.0F;
    distance_ = 110.0F;
}

} // namespace emergences::core
